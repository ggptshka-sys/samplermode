#include "editor_win32.h"

#include "plugin.h"

#include <commctrl.h>
#include <initguid.h>
#include <oleacc.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

extern void* moduleHandle;

namespace samplermode {
using namespace Steinberg;

namespace {
constexpr wchar_t kWindowClass[] = L"SamplerModeAccessibleEditor";

void setAccessibleProperty(HWND control, const MSAAPROPID& property, const std::wstring& text) {
    IAccPropServices* properties = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IAccPropServices, reinterpret_cast<void**>(&properties))) && properties) {
        properties->SetHwndPropStr(control, OBJID_CLIENT, CHILDID_SELF, property, text.c_str());
        properties->Release();
    }
}

void setAccessibleName(HWND control, const wchar_t* name) {
    setAccessibleProperty(control, PROPID_ACC_NAME, name);
}

std::wstring comboText(HWND combo) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index == CB_ERR)
        return {};
    const int length = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
    std::wstring text(static_cast<size_t>(length), L'\0');
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    return text;
}
}

SamplerEditor::SamplerEditor(SamplerPlugin* plugin) : plugin_(plugin) {
    rect.left = 0;
    rect.top = 0;
    rect.right = 800;
    rect.bottom = 540;
    controls_.fill(nullptr);
}

SamplerEditor::~SamplerEditor() {
    if (window_)
        DestroyWindow(window_);
    plugin_->editorClosed(this);
    if (comInitialized_)
        CoUninitialize();
}

tresult PLUGIN_API SamplerEditor::isPlatformTypeSupported(FIDString type) {
    return FIDStringsEqual(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API SamplerEditor::attached(void* parent, FIDString type) {
    if (!parent || !FIDStringsEqual(type, kPlatformTypeHWND))
        return kResultFalse;
    comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    INITCOMMONCONTROLSEX common {sizeof(common), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    HINSTANCE instance = static_cast<HINSTANCE>(moduleHandle);
    WNDCLASSEXW windowClass {sizeof(windowClass)};
    if (!GetClassInfoExW(instance, kWindowClass, &windowClass)) {
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass))
            return kResultFalse;
    }
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, L"SamplerMode",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, rect.getWidth(), rect.getHeight(),
                              static_cast<HWND>(parent), nullptr, instance, this);
    if (!window_)
        return kResultFalse;
    plugin_->editorOpened(this);
    createControls();
    refresh();
    SetTimer(window_, 1, 500, nullptr);
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API SamplerEditor::removed() {
    if (window_) {
        KillTimer(window_, 1);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    plugin_->editorClosed(this);
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
    return CPluginView::removed();
}

tresult PLUGIN_API SamplerEditor::onKeyDown(char16 key, int16, int16) {
    if (key == VK_TAB && window_) {
        HWND next = GetNextDlgTabItem(window_, GetFocus(), GetKeyState(VK_SHIFT) < 0);
        if (next)
            SetFocus(next);
        return kResultTrue;
    }
    return kResultFalse;
}

tresult PLUGIN_API SamplerEditor::getSize(ViewRect* size) {
    if (!size)
        return kResultFalse;
    *size = rect;
    return kResultTrue;
}

tresult PLUGIN_API SamplerEditor::onSize(ViewRect* newSize) {
    if (!newSize)
        return kResultFalse;
    rect = *newSize;
    if (window_)
        MoveWindow(window_, 0, 0, rect.getWidth(), rect.getHeight(), TRUE);
    return kResultTrue;
}

HWND SamplerEditor::addLabel(const wchar_t* text, int x, int y, int width) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                 x, y, width, 24, window_, nullptr, nullptr, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return label;
}

HWND SamplerEditor::addSlider(int id, int x, int y, int minimum, int maximum) {
    HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                  TBS_HORZ | TBS_BOTH | TBS_NOTICKS,
                                  x, y, 340, 38, window_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), nullptr, nullptr);
    SendMessageW(slider, TBM_SETRANGEMIN, FALSE, minimum);
    SendMessageW(slider, TBM_SETRANGEMAX, TRUE, maximum);
    SendMessageW(slider, TBM_SETPAGESIZE, 0, std::max(1, (maximum - minimum) / 10));
    SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    controls_[id - kLibraryButton] = slider;
    return slider;
}

HWND SamplerEditor::addCombo(int id, int x, int y, int width) {
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
                                 x, y, width, 240, window_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), nullptr, nullptr);
    SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    controls_[id - kLibraryButton] = combo;
    return combo;
}

void SamplerEditor::createControls() {
    const auto button = CreateWindowExW(0, L"BUTTON", L"Выбор библиотеки...",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        20, 20, 175, 30, window_, reinterpret_cast<HMENU>(kLibraryButton),
                                        nullptr, nullptr);
    controls_[0] = button;
    const auto path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
                                      WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                      210, 20, 565, 28, window_, reinterpret_cast<HMENU>(kLibraryPath),
                                      nullptr, nullptr);
    controls_[kLibraryPath - kLibraryButton] = path;
    addLabel(L"Конфигурация:", 20, 68, 145);
    controls_[kConfigCombo - kLibraryButton] = addCombo(kConfigCombo, 170, 63, 350);
    const auto status = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
                                        WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                        20, 105, 755, 50, window_, reinterpret_cast<HMENU>(kStatus),
                                        nullptr, nullptr);
    controls_[kStatus - kLibraryButton] = status;

    addLabel(L"Громкость, дБ:", 20, 180, 150);
    controls_[kVolume - kLibraryButton] = addSlider(kVolume, 170, 170, -60, 12);
    addLabel(L"Панорама:", 20, 240, 150);
    controls_[kPan - kLibraryButton] = addSlider(kPan, 170, 230, -100, 100);
    addLabel(L"Атака, мс:", 20, 300, 150);
    controls_[kAttack - kLibraryButton] = addSlider(kAttack, 170, 290, 1, 5000);
    addLabel(L"Декей, мс:", 20, 360, 150);
    controls_[kDecay - kLibraryButton] = addSlider(kDecay, 170, 350, 1, 5000);
    addLabel(L"Сустейн, %:", 20, 420, 150);
    controls_[kSustain - kLibraryButton] = addSlider(kSustain, 170, 410, 0, 100);
    addLabel(L"Релиз, мс:", 20, 480, 150);
    controls_[kRelease - kLibraryButton] = addSlider(kRelease, 170, 470, 1, 10000);

    addLabel(L"Педаль сустейна:", 535, 180, 180);
    controls_[kPedalCombo - kLibraryButton] = addCombo(kPedalCombo, 535, 205, 240);
    addLabel(L"Диапазон питч-бенда:", 535, 255, 220);
    controls_[kPitchRangeCombo - kLibraryButton] = addCombo(kPitchRangeCombo, 535, 280, 240);
    addLabel(L"Колесо модуляции:", 535, 330, 200);
    controls_[kModCombo - kLibraryButton] = addCombo(kModCombo, 535, 355, 240);
    addLabel(L"Tab — следующий элемент, стрелки — изменение.", 535, 420, 240);

    for (int index = 0; index < static_cast<int>(controls_.size()); ++index) {
        if (controls_[index])
            SendMessageW(controls_[index], WM_SETFONT,
                         reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }

    SendMessageW(controls_[kPedalCombo - kLibraryButton], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pedal Off"));
    SendMessageW(controls_[kPedalCombo - kLibraryButton], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pedal On"));
    for (int value = 1; value <= 12; ++value) {
        const std::wstring text = std::to_wstring(value) + L" полутон(ов)";
        SendMessageW(controls_[kPitchRangeCombo - kLibraryButton], CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text.c_str()));
    }
    for (const wchar_t* value : {L"Mod Off", L"Cut Off", L"Vibrato"})
        SendMessageW(controls_[kModCombo - kLibraryButton], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));

    setAccessibleName(path, L"Папка библиотеки");
    setAccessibleName(controls_[kConfigCombo - kLibraryButton], L"Файл конфигурации");
    setAccessibleName(status, L"Статус загрузки библиотеки");
    setAccessibleName(controls_[kVolume - kLibraryButton], L"Громкость в децибелах");
    setAccessibleName(controls_[kPan - kLibraryButton], L"Панорама, от минус 100 влево до 100 вправо");
    setAccessibleName(controls_[kAttack - kLibraryButton], L"Атака в миллисекундах");
    setAccessibleName(controls_[kDecay - kLibraryButton], L"Декей в миллисекундах");
    setAccessibleName(controls_[kSustain - kLibraryButton], L"Сустейн в процентах");
    setAccessibleName(controls_[kRelease - kLibraryButton], L"Релиз в миллисекундах");
    setAccessibleName(controls_[kPedalCombo - kLibraryButton], L"Педаль сустейна");
    setAccessibleName(controls_[kPitchRangeCombo - kLibraryButton], L"Диапазон питч-бенда в полутонах");
    setAccessibleName(controls_[kModCombo - kLibraryButton], L"Функция колеса модуляции");
}

void SamplerEditor::chooseFolder() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Выберите папку библиотеки SamplerMode");
    if (SUCCEEDED(dialog->Show(window_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR selectedPath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath)) && selectedPath) {
                const auto configurations = listConfigurations(selectedPath);
                plugin_->selectLibrary(selectedPath, configurations.empty() ? L"" : configurations.front());
                CoTaskMemFree(selectedPath);
            }
            item->Release();
        }
    }
    dialog->Release();
}

void SamplerEditor::loadSelectedConfiguration() {
    const std::wstring config = comboText(controls_[kConfigCombo - kLibraryButton]);
    if (!config.empty())
        plugin_->selectLibrary(plugin_->libraryRoot(), config);
}

void SamplerEditor::updateParameterFromSlider(int id) {
    const int value = static_cast<int>(SendMessageW(controls_[id - kLibraryButton], TBM_GETPOS, 0, 0));
    updateAccessibleSliderValue(id, value);
    double normalized = 0.0;
    switch (id) {
    case kVolume: normalized = (value + 60.0) / 72.0; break;
    case kPan: normalized = (value + 100.0) / 200.0; break;
    case kAttack: normalized = (value / 1000.0 - 0.001) / 4.999; break;
    case kDecay: normalized = (value / 1000.0 - 0.001) / 4.999; break;
    case kSustain: normalized = value / 100.0; break;
    case kRelease: normalized = (value / 1000.0 - 0.001) / 9.999; break;
    default: return;
    }
    plugin_->setParameterFromUi(static_cast<Steinberg::Vst::ParamID>(id - kVolume), std::clamp(normalized, 0.0, 1.0));
}

void SamplerEditor::updateAccessibleSliderValue(int id, int value) {
    std::wstring text;
    switch (id) {
    case kVolume: text = std::to_wstring(value) + L" дБ"; break;
    case kPan:
        text = value == 0 ? L"0, центр" : std::to_wstring(value) + (value < 0 ? L", влево" : L", вправо");
        break;
    case kAttack:
    case kDecay:
    case kRelease:
        text = std::to_wstring(value) + L" миллисекунд";
        break;
    case kSustain: text = std::to_wstring(value) + L" процентов"; break;
    default: return;
    }
    setAccessibleProperty(controls_[id - kLibraryButton], PROPID_ACC_VALUE, text);
}

void SamplerEditor::updateParameterFromCombo(int id) {
    const int selection = static_cast<int>(SendMessageW(controls_[id - kLibraryButton], CB_GETCURSEL, 0, 0));
    if (selection == CB_ERR)
        return;
    Steinberg::Vst::ParamID parameter = kPedalCombo;
    double normalized = 0.0;
    if (id == kPedalCombo) {
        parameter = kSustainPedal;
        normalized = selection;
    } else if (id == kPitchRangeCombo) {
        parameter = kPitchBendRange;
        normalized = selection / 11.0;
    } else if (id == kModCombo) {
        parameter = kModMode;
        normalized = selection / 2.0;
    }
    plugin_->setParameterFromUi(parameter, normalized);
}

void SamplerEditor::refresh(bool parametersOnly) {
    if (!window_)
        return;
    updatingControls_ = true;
    SetWindowTextW(controls_[kStatus - kLibraryButton], plugin_->libraryStatus().c_str());
    if (!parametersOnly) {
        SetWindowTextW(controls_[kLibraryPath - kLibraryButton], plugin_->libraryRoot().c_str());
        HWND config = controls_[kConfigCombo - kLibraryButton];
        SendMessageW(config, CB_RESETCONTENT, 0, 0);
        const auto configurations = listConfigurations(plugin_->libraryRoot());
        const std::wstring selected = plugin_->configName();
        int selectedIndex = 0;
        for (const auto& name : configurations) {
            const int index = static_cast<int>(SendMessageW(config, CB_ADDSTRING, 0,
                                                            reinterpret_cast<LPARAM>(name.c_str())));
            if (name == selected)
                selectedIndex = index;
        }
        if (!configurations.empty())
            SendMessageW(config, CB_SETCURSEL, selectedIndex, 0);
    }

    const auto setSlider = [&](int id, int position) {
        SendMessageW(controls_[id - kLibraryButton], TBM_SETPOS, TRUE, position);
        updateAccessibleSliderValue(id, position);
    };
    setSlider(kVolume, static_cast<int>(std::lround(plugin_->normalized(::samplermode::kVolume) * 72.0 - 60.0)));
    setSlider(kPan, static_cast<int>(std::lround(plugin_->normalized(::samplermode::kPan) * 200.0 - 100.0)));
    setSlider(kAttack, static_cast<int>(std::lround((plugin_->normalized(::samplermode::kAttack) * 4.999 + 0.001) * 1000.0)));
    setSlider(kDecay, static_cast<int>(std::lround((plugin_->normalized(::samplermode::kDecay) * 4.999 + 0.001) * 1000.0)));
    setSlider(kSustain, static_cast<int>(std::lround(plugin_->normalized(::samplermode::kSustain) * 100.0)));
    setSlider(kRelease, static_cast<int>(std::lround((plugin_->normalized(::samplermode::kRelease) * 9.999 + 0.001) * 1000.0)));
    SendMessageW(controls_[kPedalCombo - kLibraryButton], CB_SETCURSEL,
                 static_cast<WPARAM>(std::lround(plugin_->normalized(kSustainPedal))), 0);
    SendMessageW(controls_[kPitchRangeCombo - kLibraryButton], CB_SETCURSEL,
                 static_cast<WPARAM>(std::lround(plugin_->normalized(kPitchBendRange) * 11.0)), 0);
    SendMessageW(controls_[kModCombo - kLibraryButton], CB_SETCURSEL,
                 static_cast<WPARAM>(std::lround(plugin_->normalized(kModMode) * 2.0)), 0);
    updatingControls_ = false;
}

LRESULT CALLBACK SamplerEditor::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* editor = reinterpret_cast<SamplerEditor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE && editor)
        editor->window_ = window;
    return editor ? editor->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SamplerEditor::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_TIMER:
        if (wParam == 1) {
            refresh(true);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == kLibraryButton && HIWORD(wParam) == BN_CLICKED) {
            chooseFolder();
            return 0;
        }
        if (!updatingControls_ && LOWORD(wParam) == kConfigCombo && HIWORD(wParam) == CBN_SELCHANGE) {
            loadSelectedConfiguration();
            return 0;
        }
        if (!updatingControls_ && HIWORD(wParam) == CBN_SELCHANGE) {
            updateParameterFromCombo(LOWORD(wParam));
            return 0;
        }
        break;
    case WM_HSCROLL:
        if (!updatingControls_)
            updateParameterFromSlider(static_cast<int>(GetDlgCtrlID(reinterpret_cast<HWND>(lParam))));
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC device = reinterpret_cast<HDC>(wParam);
        SetBkColor(device, GetSysColor(COLOR_BTNFACE));
        SetTextColor(device, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

} // namespace samplermode
