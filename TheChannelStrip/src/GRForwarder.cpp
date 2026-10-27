#include "GRForwarder.h"

#include <atomic>
#include <cstring>

// v2.15.5: Steinberg COM plumbing. These are public include paths of
// juce_audio_processors when JUCE_PLUGINHOST_VST3=1 (verified against the
// JUCE 8.0.6 module layout). ivsteditcontroller.h alone does not provide a
// complete IComponent, so ivstcomponent.h is included explicitly (the
// v2.15.2 lesson: always check what each header really provides).
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"

// PreSonus' public-domain gain-reduction extension ships inside JUCE
// (juce_audio_processors/format_types/pslextensions/). If that folder is
// not on the include path in a given build, fall back to a verbatim copy
// of the tiny public-domain interface below (same FUID, written and placed
// in the public domain by PreSonus Software Ltd., 03/2015).
#if __has_include ("pslextensions/ipslgainreduction.h")
 #include "pslextensions/ipslgainreduction.h"
#else

#include "pluginterfaces/base/falignpush.h"

namespace Presonus {

struct IGainReductionInfo: Steinberg::FUnknown
{
    // Get current gain reduction for display. The returned value in dB is
    // either 0.0 (no reduction) or negative. The host calls this function
    // periodically while the plug-in is active. The value is used AS IS for
    // UI display purposes. Be sure to return zero if processing is bypassed
    // internally. For multiple reduction stages, report the sum in dB.
    virtual double PLUGIN_API getGainReductionValueInDb () = 0;

    static const Steinberg::FUID iid;
};

DECLARE_CLASS_IID (IGainReductionInfo, 0x8e3c292c, 0x95924f9d, 0xb2590b1e, 0x100e4198)

} // namespace Presonus

#include "pluginterfaces/base/falignpop.h"

#endif

// Define the PreSonus IGainReductionInfo iid exactly once: the VST3 SDK's
// vstinitiids.cpp (compiled inside juce_audio_processors) covers only the
// standard Steinberg interfaces, nobody defines the PreSonus one. The macro
// fully qualifies the name, so this is valid at namespace scope.
namespace Presonus {

DEF_CLASS_IID (IGainReductionInfo)

} // namespace Presonus

namespace
{
    void logGainReduction (const juce::String& message)
    {
        juce::Logger::writeToLog ("[TheChannelStrip] gain reduction: " + message);
    }

    // Diagnostic helper for the fallback path: if a console does NOT expose
    // IGainReductionInfo, some vendors expose the gain reduction as a
    // read-only (non-automatable) parameter instead. Dumping the inventory
    // to the Terminal tells us which parameter it is (and its normalized
    // value) so a future version could read it and map it to dB.
    void logReadOnlyParameterInventory (juce::AudioPluginInstance& instance)
    {
        juce::StringArray names;
        juce::StringArray values;

        for (auto* p : instance.getParameters())
        {
            if (p == nullptr || p->isAutomatable())
                continue;

            names.add (p->getName (64));
            values.add (juce::String (p->getValue(), 3));
        }

        if (names.isEmpty())
        {
            logGainReduction ("the console exposes no read-only parameter either "
                              "(no fallback path to a GR parameter)");
            return;
        }

        juce::String line;

        for (int i = 0; i < names.size(); ++i)
            line << (i > 0 ? "; " : "") << names[i] << " = " << values[i] << " (normalized)";

        logGainReduction ("read-only parameter inventory (" + juce::String (names.size())
                          + "): " + line);
    }
} // namespace

//==============================================================================
class GRForwarder::Pimpl
{
public:
    Pimpl() : extensions (*this), grInfo (*this) {}

    ~Pimpl()
    {
        // Defensive: the processor normally calls disconnect() first, but
        // never leak a borrowed COM reference if it did not.
        const juce::ScopedLock swapLock (swapGuard);
        const juce::ScopedLock grLockRef (grLock);
        releaseConsoleRefs();
    }

    //==========================================================================
    // The COM object Studio One talks to. Idiom from JUCE's official
    // ReaperEmbeddedViewPluginDemo: the object is owned by this Pimpl
    // forever (release() never deletes it), it addRefs itself when handed
    // out, and queryInterface compares the raw TUID bytes. The JUCE VST3
    // wrapper does NOT addRef the pointer it gets from the hook
    // (queryAdditionalInterfaces in juce_audio_plugin_client_VST3.cpp),
    // which is exactly why queryInterface here addRefs before returning.
    struct GainReductionInfo final : public Presonus::IGainReductionInfo
    {
        explicit GainReductionInfo (Pimpl& ownerToUse) : owner (ownerToUse) {}

        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return ++refCount;
        }

        Steinberg::uint32 PLUGIN_API release() override
        {
            // Never delete: the object lives as long as the processor.
            return --refCount;
        }

        Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID tuid,
                                                      void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            *obj = nullptr;

            if (std::memcmp (tuid, Presonus::IGainReductionInfo::iid,
                             sizeof (Steinberg::TUID)) == 0)
            {
                ++refCount;
                *obj = this;
                return Steinberg::kResultOk;
            }

            return Steinberg::kNoInterface;
        }

        double PLUGIN_API getGainReductionValueInDb() override
        {
            return owner.pullGainReductionDb();
        }

        Pimpl& owner;
        std::atomic<Steinberg::uint32> refCount { 1 };
    };

    //==========================================================================
    // The object handed to the JUCE VST3 wrapper through
    // AudioProcessor::getVST3ClientExtensions(). The wrapper calls
    // queryIEditController when the host queries an interface the wrapper
    // itself does not implement.
    struct VST3Extensions final : public juce::VST3ClientExtensions
    {
        explicit VST3Extensions (Pimpl& ownerToUse) : owner (ownerToUse) {}

        int32_t queryIEditController (const Steinberg::TUID tuid, void** obj) override
        {
            if (obj == nullptr)
                return Steinberg::kInvalidArgument;

            return owner.grInfo.queryInterface (tuid, obj);
        }

        Pimpl& owner;
    };

    //==========================================================================
    // The console's own IGainReductionInfo plus the COM chain we queried it
    // through (borrowed pointers, addRef'd by us, released by disconnect()
    // or the next connect()).
    void connect (juce::AudioPluginInstance& instance)
    {
        // The swapLock serialises connect()/disconnect() with each other;
        // the grLock protects the pointers below (a pull in flight blocks
        // the swap, and the swap is short). Always take the swapLock first.
        const juce::ScopedLock swapLock (swapGuard);
        Steinberg::Vst::IComponent* component = nullptr;

        // 1) Fetch the console's VST3 IComponent through JUCE's hosting.
        struct Fetcher : public juce::ExtensionsVisitor
        {
            Steinberg::Vst::IComponent* found = nullptr;

            void visitVST3Client (const VST3Client& client) override
            {
                // getIComponentPtr() returns a pointer BORROWED from the
                // JUCE wrapper: addRef it ourselves before keeping it.
                if (auto* c = client.getIComponentPtr())
                {
                    c->addRef();
                    found = c;
                }
            }
        };

        Fetcher fetcher;
        instance.getExtensions (fetcher);

        if (fetcher.found == nullptr)
        {
            // Not a VST3 instance (e.g. an AU) or the visitor is not
            // available: release whatever the previous console lent us.
            releaseConsoleRefsWithLock();
            logGainReduction ("hosted plugin has no VST3 IComponent - nothing to forward "
                              "(expected for a non-VST3 console)");
            return;
        }

        component = fetcher.found;

        // 2) IComponent -> IEditController -> Presonus::IGainReductionInfo.
        void* ecPtr = nullptr;

        if (component->queryInterface (Steinberg::Vst::IEditController::iid, &ecPtr)
                != Steinberg::kResultOk
            || ecPtr == nullptr)
        {
            releaseConsoleRefsWithLock();
            component->release();
            logGainReduction ("NOT forwarded: the console's IComponent has no IEditController");
            return;
        }

        auto* editController = static_cast<Steinberg::Vst::IEditController*> (ecPtr);

        void* grPtr = nullptr;
        const bool hasGainReduction = editController->queryInterface (Presonus::IGainReductionInfo::iid,
                                                                      &grPtr) == Steinberg::kResultOk
                                     && grPtr != nullptr;

        if (! hasGainReduction)
        {
            releaseConsoleRefsWithLock();
            editController->release();
            component->release();
            logGainReduction ("NOT forwarded: '" + instance.getName()
                              + "' does not expose Presonus::IGainReductionInfo "
                                "(dumping the read-only parameters for diagnosis)");
            logReadOnlyParameterInventory (instance);
            return;
        }

        // 3) Swap the new pointers in (any previous console's references
        // are released first - the old instance may still be crossfading
        // out, but we stop reporting its GR immediately, which is the
        // honest value once the host is told the console changed).
        {
            const juce::ScopedLock grLockRef (grLock);

            releaseConsoleRefs();

            consoleComponent      = component;
            consoleEditController = editController;
            consoleGainReduction  = static_cast<Presonus::IGainReductionInfo*> (grPtr);
        }

        logGainReduction ("forwarding to the host ('" + instance.getName() + "')");
    }

    void disconnect()
    {
        const juce::ScopedLock swapLock (swapGuard);

        const juce::ScopedLock grLockRef (grLock);

        releaseConsoleRefs();
    }

    double pullGainReductionDb() const
    {
        const juce::ScopedLock grLockRef (grLock);

        if (consoleGainReduction != nullptr)
            return consoleGainReduction->getGainReductionValueInDb();

        return 0.0;
    }

    VST3Extensions extensions;
    GainReductionInfo grInfo;

private:
    // grLock must already be held (swapLock is held by the caller).
    void releaseConsoleRefs()
    {
        if (consoleGainReduction != nullptr)
        {
            consoleGainReduction->release();
            consoleGainReduction = nullptr;
        }

        if (consoleEditController != nullptr)
        {
            consoleEditController->release();
            consoleEditController = nullptr;
        }

        if (consoleComponent != nullptr)
        {
            consoleComponent->release();
            consoleComponent = nullptr;
        }
    }

    // Same, but takes the grLock itself (for the failure paths of
    // connect(), where no pull can be given a half-released chain).
    void releaseConsoleRefsWithLock()
    {
        const juce::ScopedLock grLockRef (grLock);
        releaseConsoleRefs();
    }

    Steinberg::Vst::IComponent*      consoleComponent    = nullptr;
    Steinberg::Vst::IEditController* consoleEditController = nullptr;
    Presonus::IGainReductionInfo*    consoleGainReduction   = nullptr;

    juce::CriticalSection swapGuard;              // serialises connect/disconnect
    mutable juce::CriticalSection grLock;        // protects the three pointers
};

//==============================================================================
GRForwarder::GRForwarder()
    : pimpl (new Pimpl())
{
}

GRForwarder::~GRForwarder()
{
    // Pimpl's destructor releases anything left over; the COM objects are
    // members of the Pimpl and die with it (they are never deleted by COM).
    delete pimpl;
}

void GRForwarder::connect (juce::AudioPluginInstance& instance)
{
    pimpl->connect (instance);
}

void GRForwarder::disconnect()
{
    pimpl->disconnect();
}

double GRForwarder::pullGainReductionDb() const
{
    return pimpl->pullGainReductionDb();
}

juce::VST3ClientExtensions* GRForwarder::getVST3ClientExtensions()
{
    return &(pimpl->extensions);
}
