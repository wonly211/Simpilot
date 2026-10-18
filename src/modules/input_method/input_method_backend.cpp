#include "input_method_backend.hpp"

#include <imm.h>
#include <msctf.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>

namespace simpilot::input_method {
namespace {

using Microsoft::WRL::ComPtr;

// WM_IME_CONTROL commands are documented by Windows but not defined by the
// desktop SDK's imm.h. Keep them local rather than extending shared headers.
constexpr WPARAM get_conversion = 0x0001;
constexpr WPARAM set_conversion = 0x0002;
constexpr WPARAM get_open = 0x0005;
constexpr WPARAM set_open = 0x0006;

class Apartment final {
public:
    Apartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~Apartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
    bool available() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT result_;
};

std::wstring hex(DWORD value) {
    wchar_t text[9]{};
    swprintf_s(text, L"%08X", value);
    return text;
}

std::wstring guid(const GUID& value) {
    wchar_t text[40]{};
    StringFromGUID2(value, text, 40);
    return text;
}

std::wstring layout_id(HKL layout) {
    const auto value = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(layout));
    const auto language = LOWORD(value);
    const auto device = HIWORD(value);
    if (device == language || device == 0) return hex(language);
    if ((device & 0xF000) != 0xF000) return hex(value);

    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
            0, KEY_READ, &root) == ERROR_SUCCESS) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[256]{};
            DWORD length = 256;
            if (RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr)
                != ERROR_SUCCESS) break;
            HKEY child = nullptr;
            if (RegOpenKeyExW(root, name, 0, KEY_READ, &child) != ERROR_SUCCESS) continue;
            wchar_t id[32]{};
            DWORD bytes = sizeof(id);
            const auto result = RegGetValueW(child, nullptr, L"Layout Id",
                RRF_RT_REG_SZ, nullptr, id, &bytes);
            RegCloseKey(child);
            if (result != ERROR_SUCCESS) continue;
            wchar_t* end = nullptr;
            const auto numeric = wcstoul(id, &end, 16);
            const auto key = wcstoul(name, nullptr, 16);
            if (end != id && numeric == static_cast<unsigned long>(device & 0x0FFF)
                && LOWORD(key) == language) {
                RegCloseKey(root);
                return name;
            }
        }
        RegCloseKey(root);
    }
    return hex(value);
}

std::wstring language_name(LANGID id) {
    wchar_t text[256]{};
    if (GetLocaleInfoW(MAKELCID(id, SORT_DEFAULT), LOCALE_SLOCALIZEDDISPLAYNAME,
                        text, 256)) return text;
    return hex(id);
}

std::wstring process_path(HWND window, DWORD& process, DWORD& thread) {
    thread = GetWindowThreadProcessId(window, &process);
    if (!thread || !process) return {};
    const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!handle) return {};
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const auto queried = QueryFullProcessImageNameW(handle, 0, path.data(), &length);
    const auto error = GetLastError();
    CloseHandle(handle);
    if (!queried) { SetLastError(error); return {}; }
    path.resize(length);
    return path;
}

class Backend final : public IBackend {
public:
    std::vector<Profile> enumerate() override {
        std::vector<Profile> result;
        const int count = GetKeyboardLayoutList(0, nullptr);
        std::vector<HKL> layouts(static_cast<std::size_t>(std::max(0, count)));
        if (count > 0) GetKeyboardLayoutList(count, layouts.data());
        Apartment apartment;
        ComPtr<ITfInputProcessorProfiles> descriptions;
        ComPtr<ITfInputProcessorProfileMgr> profiles;
        if (apartment.available()
            && SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(descriptions.GetAddressOf())))
            && SUCCEEDED(descriptions.As(&profiles))) {
            LANGID* languages = nullptr;
            ULONG length = 0;
            if (SUCCEEDED(descriptions->GetLanguageList(&languages, &length))) {
                std::vector<LANGID> language_list(languages, languages + length);
                CoTaskMemFree(languages);
                for (const auto language : language_list) {
                    ComPtr<IEnumTfInputProcessorProfiles> enumerator;
                    if (FAILED(profiles->EnumProfiles(language, &enumerator))) continue;
                    TF_INPUTPROCESSORPROFILE entry{};
                    ULONG fetched = 0;
                    while (enumerator->Next(1, &entry, &fetched) == S_OK && fetched == 1) {
                        if (entry.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR
                            || !(entry.dwFlags & TF_IPP_FLAG_ENABLED)) continue;
                        BSTR text = nullptr;
                        std::wstring name;
                        if (SUCCEEDED(descriptions->GetLanguageProfileDescription(
                                entry.clsid, entry.langid, entry.guidProfile, &text))
                            && text) {
                            name.assign(text, SysStringLen(text));
                            SysFreeString(text);
                        }
                        const auto id = L"tsf:" + hex(language) + L":"
                            + guid(entry.clsid) + L":" + guid(entry.guidProfile);
                        result.push_back({id, name.empty() ? language_name(language) : name,
                            entry.hklSubstitute ? entry.hklSubstitute : entry.hkl,
                            true, PRIMARYLANGID(language) == LANG_CHINESE, true});
                    }
                }
            }
        }
        // A TSF-only profile may omit its substitute layout. Infer a target
        // only when both the enabled profile and loaded layout are unique for
        // that language. Never silently choose between two third-party IMEs.
        for (auto& profile : result) {
            if (profile.layout) continue;
            const auto language = static_cast<LANGID>(
                wcstoul(profile.id.substr(4, 8).c_str(), nullptr, 16));
            std::vector<HKL> matching;
            for (const auto layout : layouts)
                if (LOWORD(reinterpret_cast<ULONG_PTR>(layout)) == language)
                    matching.push_back(layout);
            const auto peers = std::ranges::count_if(result, [language](const Profile& peer) {
                return wcstoul(peer.id.substr(4, 8).c_str(), nullptr, 16) == language;
            });
            if (matching.size() == 1 && peers == 1) profile.layout = matching.front();
            else profile.selectable = false;
        }
        for (auto& profile : result) {
            const auto peers = std::ranges::count(result, profile.layout, &Profile::layout);
            if (!profile.layout || peers > 1) profile.selectable = false;
        }
        for (const auto layout : layouts) {
            if (std::ranges::any_of(result, [layout](const Profile& profile) {
                    return profile.layout == layout;
                })) continue;
            const auto language = LOWORD(reinterpret_cast<ULONG_PTR>(layout));
            const auto ime = ImmIsIME(layout) != FALSE;
            result.push_back({L"layout:" + layout_id(layout),
                language_name(language) + L" (" + layout_id(layout) + L")",
                layout, ime, ime && PRIMARYLANGID(language) == LANG_CHINESE, true});
        }
        std::ranges::sort(result, {}, &Profile::name);
        return result;
    }

    Foreground foreground() override {
        error_ = ERROR_SUCCESS;
        Foreground result;
        result.window = GetForegroundWindow();
        if (!result.window) return result;
        result.path = process_path(result.window, result.process, result.thread);
        if (result.path.empty()) { error_ = GetLastError(); return {}; }
        // Simpilot's settings/menu/dialogs must not learn or overwrite users'
        // application states while they are editing rules.
        if (result.process == GetCurrentProcessId()) return {};
        GUITHREADINFO info{.cbSize = sizeof(info)};
        if (GetGUIThreadInfo(result.thread, &info)) result.focus = info.hwndFocus;
        if (!result.focus) result.focus = result.window;
        result.thread = GetWindowThreadProcessId(result.focus, nullptr);
        return result;
    }

    std::optional<State> read(
        const Foreground& target, const std::vector<Profile>& profiles) override {
        error_ = ERROR_SUCCESS;
        if (!valid(target, true)) return std::nullopt;
        const auto layout = GetKeyboardLayout(target.thread);
        const Profile* selected = nullptr;
        for (const auto& profile : profiles) {
            if (profile.layout != layout || !profile.selectable) continue;
            if (selected) { error_ = ERROR_NOT_SUPPORTED; return std::nullopt; }
            selected = &profile;
        }
        if (!selected) { error_ = ERROR_NOT_FOUND; return std::nullopt; }
        State state{selected->id, Mode::english};
        if (!selected->ime) return state;
        if (!selected->supports_chinese) { error_ = ERROR_NOT_SUPPORTED; return std::nullopt; }
        const auto ime = ImmGetDefaultIMEWnd(target.focus);
        DWORD_PTR open = 0, conversion = 0;
        if (!ime) { error_ = ERROR_NOT_SUPPORTED; return std::nullopt; }
        if (!control(ime, get_open, 0, open)
            || !control(ime, get_conversion, 0, conversion)) return std::nullopt;
        state.mode = open && (conversion & IME_CMODE_NATIVE) ? Mode::chinese : Mode::english;
        if (!valid(target, true)) return std::nullopt;
        return state;
    }

    bool request_profile(const Foreground& target, const Profile& profile) override {
        error_ = ERROR_SUCCESS;
        if (!valid(target, true)) return false;
        if (!profile.selectable || !profile.layout) {
            error_ = ERROR_NOT_SUPPORTED;
            return false;
        }
        // Target the focused window, not Simpilot's own thread and not the
        // entire session (TF_IPPMF_FORSESSION would affect unrelated apps).
        if (!PostMessageW(target.focus, WM_INPUTLANGCHANGEREQUEST, 0,
                          reinterpret_cast<LPARAM>(profile.layout))) {
            error_ = GetLastError();
            return false;
        }
        return true;
    }

    bool request_mode(const Foreground& target, const Profile& profile, Mode mode) override {
        error_ = ERROR_SUCCESS;
        if (!valid(target, true)) return false;
        if (GetKeyboardLayout(target.thread) != profile.layout) {
            error_ = ERROR_INVALID_STATE;
            return false;
        }
        if (!profile.ime) {
            if (mode != Mode::english) error_ = ERROR_NOT_SUPPORTED;
            return mode == Mode::english;
        }
        if (!profile.supports_chinese) { error_ = ERROR_NOT_SUPPORTED; return false; }
        const auto ime = ImmGetDefaultIMEWnd(target.focus);
        DWORD_PTR conversion = 0, ignored = 0;
        if (!ime) { error_ = ERROR_NOT_SUPPORTED; return false; }
        if (!control(ime, get_conversion, 0, conversion)) return false;
        if (mode == Mode::english) {
            // Modern Microsoft Pinyin may ignore conversion-mode writes across
            // processes, while closing its input context reliably selects
            // direct English input. The runtime verifies the resulting mode.
            return control(ime, set_open, FALSE, ignored);
        }
        if (!control(ime, set_conversion,
                static_cast<LPARAM>(conversion | IME_CMODE_NATIVE), ignored)
            || !control(ime, set_open, TRUE, ignored)) return false;
        return true;
    }

    DWORD last_error() const noexcept override { return error_; }

private:
    bool valid(const Foreground& target, bool must_be_active) {
        DWORD window_process = 0;
        DWORD focus_process = 0;
        GetWindowThreadProcessId(target.window, &window_process);
        const auto focus_thread = GetWindowThreadProcessId(target.focus, &focus_process);
        if (!IsWindow(target.window) || !IsWindow(target.focus)
            || window_process != target.process || focus_process != target.process
            || focus_thread != target.thread
            || (must_be_active && GetForegroundWindow() != target.window)) {
            error_ = ERROR_INVALID_WINDOW_HANDLE;
            return false;
        }
        return true;
    }

    bool control(HWND ime, WPARAM command, LPARAM value, DWORD_PTR& result) {
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(ime, WM_IME_CONTROL, command, value,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 30, &result)) {
            error_ = GetLastError();
            if (!error_) error_ = ERROR_TIMEOUT;
            return false;
        }
        return true;
    }
    DWORD error_ = ERROR_SUCCESS;
};

} // namespace

std::unique_ptr<IBackend> make_backend() { return std::make_unique<Backend>(); }

} // namespace simpilot::input_method
