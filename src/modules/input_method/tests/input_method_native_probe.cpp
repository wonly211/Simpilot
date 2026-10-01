#include "input_method_backend.hpp"
#include "input_method_page.hpp"

#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace simpilot;
using namespace simpilot::input_method;
using Microsoft::WRL::ComPtr;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

LRESULT CALLBACK target_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CREATE) {
        const auto edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE,
            16, 16, 540, 200, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetFocus(edit);
    } else if (message == WM_SETFOCUS) {
        SetFocus(GetWindow(window, GW_CHILD));
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int target_main() {
    const WNDCLASSW klass{.lpfnWndProc = target_procedure,
        .hInstance = GetModuleHandleW(nullptr),
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = L"Simpilot.InputMethodNativeTarget"};
    RegisterClassW(&klass);
    const auto window = CreateWindowW(klass.lpszClassName, L"Simpilot isolated IME target",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 600, 320, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr, "target window could not be created");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

class Child final {
public:
    Child() {
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --target";
        STARTUPINFOW startup{.cb = sizeof(startup)};
        require(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_) != FALSE,
            "could not start isolated target");
        CloseHandle(process_.hThread);
        process_.hThread = nullptr;
        const auto end = GetTickCount64() + 5000;
        while (!window && GetTickCount64() < end) {
            EnumWindows([](HWND candidate, LPARAM data) -> BOOL {
                auto* self = reinterpret_cast<Child*>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(candidate, &pid);
                if (pid == self->process_.dwProcessId && IsWindowVisible(candidate)) {
                    self->window = candidate;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(this));
            pump(30);
        }
    }
    ~Child() {
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (process_.hProcess) {
            if (WaitForSingleObject(process_.hProcess, 1500) == WAIT_TIMEOUT)
                TerminateProcess(process_.hProcess, 1);
            CloseHandle(process_.hProcess);
        }
    }
    HWND window = nullptr;
private:
    PROCESS_INFORMATION process_{};
};

void native_test() {
    const auto previous_window = GetForegroundWindow();
    auto backend = make_backend();
    const auto profiles = backend->enumerate();
    const auto found = std::ranges::find_if(profiles, [](const Profile& profile) {
        return profile.supports_chinese && profile.selectable;
    });
    if (found == profiles.end()) {
        std::cout << "SKIP: no controllable Chinese IME installed\n";
        return;
    }
    Child child;
    require(child.window != nullptr, "target was not ready");
    SetForegroundWindow(child.window);
    pump(300);
    auto target = backend->foreground();
    require(target.window == child.window, "isolated target is not foreground");
    const auto original = backend->read(target, profiles);
    try {
        require(backend->request_profile(target, *found), "profile request failed");
        pump(300);
        for (const auto mode : {Mode::english, Mode::chinese, Mode::english}) {
            target = backend->foreground();
            require(backend->request_mode(target, *found, mode), "mode request failed");
            pump(300);
            const auto actual = backend->read(target, profiles);
            if (!actual || actual->profile_id != found->id || actual->mode != mode) {
                std::cout << "Readback failed; error=" << backend->last_error()
                    << " desired=" << static_cast<int>(mode)
                    << " actual=" << (actual ? static_cast<int>(actual->mode) : -1) << '\n';
                throw std::runtime_error("native mode was not confirmed");
            }
        }
        std::cout << "Native Chinese IME English/Chinese/English readback passed\n";
    } catch (...) {
        if (original) {
            if (const auto* profile = find_profile(profiles, original->profile_id)) {
                (void)backend->request_profile(target, *profile);
                pump(200);
                (void)backend->request_mode(target, *profile, original->mode);
            }
        }
        if (previous_window) SetForegroundWindow(previous_window);
        throw;
    }
    if (original) {
        if (const auto* profile = find_profile(profiles, original->profile_id)) {
            (void)backend->request_profile(target, *profile);
            pump(200);
            (void)backend->request_mode(target, *profile, original->mode);
        }
    }
    if (previous_window) SetForegroundWindow(previous_window);
}

void screenshot(HWND window, const std::filesystem::path& path) {
    RECT client{};
    GetClientRect(window, &client);
    const UINT width = client.right, height = client.bottom;
    auto screen = GetDC(window);
    auto dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), static_cast<LONG>(width),
        -static_cast<LONG>(height), 1, 32, BI_RGB};
    void* pixels = nullptr;
    auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    auto old = SelectObject(dc, bitmap);
    PrintWindow(window, dc, PW_CLIENTONLY);
    auto* image = static_cast<BYTE*>(pixels);
    for (UINT pixel = 0; pixel < width * height; ++pixel) image[pixel * 4 + 3] = 255;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    bool success = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    if (success) success = SUCCEEDED(factory->CreateStream(&stream))
        && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))
        && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, &properties))
        && SUCCEEDED(frame->Initialize(properties.Get()))
        && SUCCEEDED(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (success) success = SUCCEEDED(frame->SetPixelFormat(&format))
        && format == GUID_WICPixelFormat32bppBGRA
        && SUCCEEDED(frame->WritePixels(height, width * 4, width * height * 4,
            static_cast<BYTE*>(pixels))) && SUCCEEDED(frame->Commit())
        && SUCCEEDED(encoder->Commit());
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(window, screen);
    require(success, "screenshot encoding failed");
}

std::filesystem::path dialog_capture;
void CALLBACK capture_dialog(HWND, UINT, UINT_PTR, DWORD) {
    auto window = FindWindowW(L"Simpilot.InputMethodRuleDialog", nullptr);
    if (!window) return;
    screenshot(window, dialog_capture);
    SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
}

void screenshots(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    require(SUCCEEDED(apartment), "screenshot COM initialization failed");
    const auto instance = GetModuleHandleW(nullptr);
    LOGFONTW font_data{};
    font_data.lfHeight = -14;
    wcscpy_s(font_data.lfFaceName, L"Segoe UI");
    const auto body_font = CreateFontIndirectW(&font_data);
    require(body_font != nullptr, "preview font creation failed");
    auto host = CreateWindowW(L"STATIC", L"Isolated input method preview",
        WS_OVERLAPPEDWINDOW, 0, 0, 1200, 1000, nullptr, nullptr, instance, nullptr);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    Settings draft{true, {
        {L"C:\\Apps\\Visual Studio Code\\Code.exe", Policy::fixed, L"pinyin", Mode::english},
        {L"C:\\Apps\\Chat\\Chat.exe", Policy::fixed, L"pinyin", Mode::chinese},
        {L"C:\\Apps\\Browser\\Browser.exe", Policy::remember, {}, Mode::english},
        {L"C:\\Apps\\Game\\Game.exe", Policy::ignore, {}, Mode::english},
        {L"C:\\Apps\\Removed IME\\Example.exe", Policy::fixed, L"missing", Mode::chinese}}};
    const std::vector<Profile> profiles{
        {L"pinyin", L"Microsoft Pinyin", reinterpret_cast<HKL>(2), true, true, true},
        {L"english", L"English (United States)", reinterpret_cast<HKL>(1), false, false, true}};
    for (auto language : {UiLanguage::simplified_chinese, UiLanguage::english}) {
        Localization localization(language);
        const auto code = language == UiLanguage::english ? L"en" : L"zh";
        auto page = make_settings_page(draft, {
            [&]() -> const std::vector<Profile>& { return profiles; },
            [] {}, [] { return Status{"settings.input_method.state_applied", L"C:\\Apps\\Code.exe", 0}; }});
        page->create({instance, host, 96,
            body_font, localization, {}});
        page->show(true);
        const auto page_window = FindWindowExW(host, nullptr, L"Simpilot.InputMethodSettingsPage", nullptr);
        for (UINT dpi : {96U, 144U, 192U}) {
            page->layout({0, 0, MulDiv(650, dpi, 96), MulDiv(440, dpi, 96)},
                dpi, body_font);
            pump(80);
            screenshot(page_window, directory / (L"settings-" + std::wstring(code)
                + L"-" + std::to_wstring(dpi) + L".png"));
        }
        page.reset();
        dialog_capture = directory / (L"rule-" + std::wstring(code)
            + L"-actual-" + std::to_wstring(GetDpiForWindow(host)) + L".png");
        const auto timer = SetTimer(nullptr, 0, 100, capture_dialog);
        (void)show_rule_dialog(instance, host, localization, draft.rules[0], profiles, {});
        KillTimer(nullptr, timer);
    }
    DestroyWindow(host);
    DeleteObject(body_font);
    CoUninitialize();
    std::cout << "Isolated native screenshots captured\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        if (argc == 2 && std::string_view(argv[1]) == "--target") return target_main();
        if (argc == 3 && std::string_view(argv[1]) == "--screenshots") {
            screenshots(std::filesystem::path(argv[2]));
            return 0;
        }
        native_test();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
