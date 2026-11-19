#pragma once

#include "public.sdk/source/common/pluginview.h"

#include <windows.h>

#include <array>

namespace samplermode {

class SamplerPlugin;

class SamplerEditor : public Steinberg::CPluginView {
public:
    explicit SamplerEditor(SamplerPlugin* plugin);
    ~SamplerEditor() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyMessage, Steinberg::int16 modifiers) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;

    void refresh(bool parametersOnly = false);

private:
    enum ControlId {
        kLibraryButton = 1000,
        kLibraryPath,
        kConfigCombo,
        kStatus,
        kVolume,
        kPan,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
        kPedalCombo,
        kPitchRangeCombo,
        kModCombo
    };

    void createControls();
    void chooseFolder();
    void loadSelectedConfiguration();
    void updateParameterFromSlider(int id);
    void updateAccessibleSliderValue(int id, int value);
    void updateParameterFromCombo(int id);
    HWND addLabel(const wchar_t* text, int x, int y, int width);
    HWND addSlider(int id, int x, int y, int minimum, int maximum);
    HWND addCombo(int id, int x, int y, int width);
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    SamplerPlugin* plugin_;
    HWND window_ = nullptr;
    bool comInitialized_ = false;
    bool updatingControls_ = false;
    std::array<HWND, 14> controls_ {};
};

} // namespace samplermode
