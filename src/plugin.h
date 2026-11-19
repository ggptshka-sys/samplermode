#pragma once

#include "sampler_engine.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <mutex>

namespace samplermode {

enum ParameterId : Steinberg::Vst::ParamID {
    kVolume = 0,
    kPan,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kSustainPedal,
    kPitchBendRange,
    kModMode,
    kModWheelValue = 100,
    kPitchBendValue = 101
};

class SamplerProcessor : public Steinberg::Vst::AudioEffect {
public:
    SamplerProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new SamplerProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    OBJ_METHODS(SamplerProcessor, Steinberg::Vst::AudioEffect)
    REFCOUNT_METHODS(Steinberg::Vst::AudioEffect)

private:
    Parameters parametersFromNormalized() const;
    void loadLibrary(const std::wstring& root, const std::wstring& config);

    SamplerEngine engine_;
    Parameters currentParameters_ {};
    std::array<double, 9> normalizedValues_ {};
    mutable std::mutex libraryMutex_;
    std::wstring libraryRoot_;
    std::wstring configName_;
};

class SamplerEditor;

class SamplerPlugin : public Steinberg::Vst::EditControllerEx1,
                      public Steinberg::Vst::IMidiMapping {
public:
    SamplerPlugin() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new SamplerPlugin);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(const char* name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel, Steinberg::Vst::CtrlNumber controller,
        Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

    void setParameterFromUi(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
    double normalized(Steinberg::Vst::ParamID id) const;
    void selectLibrary(const std::wstring& root, const std::wstring& config);
    std::wstring libraryRoot() const;
    std::wstring configName() const;
    std::wstring libraryStatus() const;
    void editorOpened(SamplerEditor* editor);
    void editorClosed(SamplerEditor* editor);

    OBJ_METHODS(SamplerPlugin, Steinberg::Vst::EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(Steinberg::Vst::EditControllerEx1)
    REFCOUNT_METHODS(Steinberg::Vst::EditControllerEx1)

private:
    void applyDefaultsOnce();
    void refreshEditor();

    SamplerEditor* editor_ = nullptr;
    mutable std::mutex libraryMutex_;
    std::wstring libraryRoot_;
    std::wstring configName_;
    std::wstring libraryStatus_ = L"Библиотека не выбрана.";
    bool defaultsApplied_ = false;
    bool stateLoaded_ = false;
};

} // namespace samplermode
