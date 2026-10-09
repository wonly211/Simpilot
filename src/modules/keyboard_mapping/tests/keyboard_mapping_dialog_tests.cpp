#include "keyboard_mapping_dialog.hpp"

#include <commctrl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace simpilot;
std::function<void(HWND)> action;
std::string failure;
std::string screenshot_path;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

HWND control(HWND dialog, int identifier) {
    struct Search { int id; HWND window = nullptr; } search{identifier};
    EnumChildWindows(dialog, [](HWND child, LPARAM value) -> BOOL {
        auto& found = *reinterpret_cast<Search*>(value);
        if (GetDlgCtrlID(child) == found.id) { found.window = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.window != nullptr, "Dialog control is missing");
    return search.window;
}

void click(HWND dialog, int identifier) {
    SendMessageW(control(dialog, identifier), BM_CLICK, 0, 0);
}

void scope(HWND dialog, int selected) {
    const auto combo = control(dialog, 150);
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(150, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
}

void add(HWND dialog, const wchar_t* name) {
    SetWindowTextW(control(dialog, 151), name);
    click(dialog, 153);
}

std::wstring text(HWND window) {
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, result.data(), static_cast<int>(result.size()));
    result.pop_back();
    return result;
}

void CALLBACK timer(HWND, UINT, UINT_PTR, DWORD) {
    const auto window = FindWindowW(L"Simpilot.KeyboardMappingDialog", nullptr);
    if (!window) return;
    try {
        auto next = std::exchange(action, {});
        if (next) next(window);
    } catch (const std::exception& error) {
        failure = error.what();
        SendMessageW(window, WM_CLOSE, 0, 0);
    }
}

KeyboardMappingRule initial_rule() {
    KeyboardMappingRule result;
    result.trigger.single_key = true;
    result.trigger.action = {VK_LWIN, 0x5B, true};
    result.output.single_key = true;
    result.output.action = {VK_RCONTROL, 0x1D, true};
    return result;
}

std::optional<KeyboardMappingRule> run(const KeyboardMappingRule& initial,
    std::function<void(HWND)> steps, std::string language = "en-US",
    KeyboardMappingDialog::CaptureCallbacks callbacks = {}) {
    failure.clear();
    action = std::move(steps);
    auto owner = CreateWindowW(L"STATIC", L"", WS_OVERLAPPEDWINDOW,
        0, 0, 1000, 900, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(owner != nullptr, "Create test owner");
    const auto timer_id = SetTimer(nullptr, 0, 30, timer);
    require(timer_id != 0, "Create dialog timer");
    auto result = KeyboardMappingDialog::show_modal(GetModuleHandleW(nullptr), owner,
        std::move(language), &initial, std::move(callbacks));
    KillTimer(nullptr, timer_id);
    DestroyWindow(owner);
    require(failure.empty(), failure.c_str());
    return result;
}

void save_screenshot(HWND window) {
    if (screenshot_path.empty()) return;
    RECT rect{};
    GetWindowRect(window, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    const auto dc = GetDC(window), memory = CreateCompatibleDC(dc);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* pixels = nullptr;
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(bitmap && pixels && memory, "Create screenshot bitmap");
    const auto old = SelectObject(memory, bitmap);
    PrintWindow(window, memory, 0);
    const auto bytes = static_cast<DWORD>(width * height * 4);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + bytes;
    std::ofstream stream(screenshot_path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    stream.write(static_cast<const char*>(pixels), bytes);
    require(stream.good(), "Write dialog screenshot");
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, dc);
}

void application_editor() {
    auto initial = initial_rule();
    int validation_calls = 0;
    KeyboardMappingDialog::CaptureCallbacks callbacks;
    callbacks.foreground_process = [] { return std::optional<std::wstring>{L"NOTEPAD.EXE"}; };
    callbacks.validate = [&](const KeyboardMappingRule&) -> std::optional<std::wstring> {
        return ++validation_calls == 1 ? std::optional<std::wstring>{L"Conflict in notepad.exe"} : std::nullopt;
    };
    const auto saved = run(initial, [](HWND dialog) {
        require(SendMessageW(control(dialog, 150), CB_GETCURSEL, 0, 0) == 0
            && !IsWindowEnabled(control(dialog, 152)), "Existing global rule must open in explicit global mode");
        scope(dialog, 1);
        click(dialog, IDOK);
        require(IsWindow(dialog) && text(control(dialog, 156)).find(L"at least one") != std::wstring::npos,
            "Empty specific scope must stay open and explain the error");
        add(dialog, L"C:\\Editor.exe");
        require(SendMessageW(control(dialog, 152), LB_GETCOUNT, 0, 0) == 0,
            "Invalid typed paths must not be added");
        add(dialog, L"Editor");
        add(dialog, L"EDITOR.exe");
        click(dialog, 102);
        click(dialog, 102);
        require(SendMessageW(control(dialog, 152), LB_GETCOUNT, 0, 0) == 2,
            "Typed/foreground entries must append and deduplicate");
        click(dialog, IDOK);
        require(IsWindow(dialog) && text(control(dialog, 156)) == L"Conflict in notepad.exe"
            && SendMessageW(control(dialog, 152), LB_GETCOUNT, 0, 0) == 2,
            "Cross-rule validation must keep the complete editable draft");
        click(dialog, IDOK);
    }, "en-US", callbacks);
    require(saved && saved->process_names == std::vector<std::wstring>{L"editor.exe", L"notepad.exe"}
        && saved->trigger == initial.trigger && saved->output == initial.output && validation_calls == 2,
        "Saving the app list must preserve sided modifier keys and call validation");

    const auto removed = run(*saved, [](HWND dialog) {
        require(SendMessageW(control(dialog, 152), LB_GETCOUNT, 0, 0) == 2, "Reopen all applications");
        SendMessageW(control(dialog, 152), LB_SETCURSEL, 0, 0);
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(152, LBN_SELCHANGE), 0);
        click(dialog, 154);
        click(dialog, 154);
        require(SendMessageW(control(dialog, 150), CB_GETCURSEL, 0, 0) == 1,
            "Deleting the last item must not switch to global");
        click(dialog, IDOK);
        require(IsWindow(dialog), "An empty specific list must not save");
        SetWindowTextW(control(dialog, 151), L"Browser");
        click(dialog, IDOK);
    });
    require(removed && removed->process_names == std::vector<std::wstring>{L"browser.exe"},
        "Saving a pending typed entry must add it without losing input");

    const auto global = run(*saved, [](HWND dialog) { scope(dialog, 0); click(dialog, IDOK); });
    require(global && global->process_names.empty(), "Only explicit all-applications mode should create global scope");
    require(!run(*saved, [](HWND dialog) { scope(dialog, 0); click(dialog, IDCANCEL); }),
        "Cancel must not commit the edited scope");
}

void limits_and_layout() {
    auto initial = initial_rule();
    for (int i = 0; i < 32; ++i) initial.process_names.push_back(L"app" + std::to_wstring(i) + L".exe");
    const auto bounded = run(initial, [](HWND dialog) {
        add(dialog, L"overflow.exe");
        require(SendMessageW(control(dialog, 152), LB_GETCOUNT, 0, 0) == 32
            && text(control(dialog, 156)).find(L"32") != std::wstring::npos, "Application limit must be visible");
        add(dialog, L"app0.exe");
        click(dialog, IDOK);
    });
    require(bounded && bounded->process_names.size() == 32, "Duplicates at the limit remain harmless");

    initial.process_names = {L"code.exe", L"notepad.exe"};
    for (const auto locale : {"zh-CN", "zh-TW", "en-US"}) {
        require(!run(initial, [&](HWND dialog) {
            for (const UINT dpi : {96U, 144U, 192U}) {
                RECT suggested{0, 0, MulDiv(1000, dpi, 96), MulDiv(850, dpi, 96)};
                SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
                for (const int id : {150, 151, 152, 153, 154, 155, 156, 102}) {
                    auto child = control(dialog, id);
                    RECT bounds{};
                    GetWindowRect(child, &bounds);
                    MapWindowPoints(HWND_DESKTOP, GetParent(child), reinterpret_cast<POINT*>(&bounds), 2);
                    require(bounds.left >= 0 && bounds.top >= 0 && bounds.right > bounds.left
                        && bounds.bottom > bounds.top, "Application controls must have a valid DPI-aware layout");
                }
                RECT list{}, buttons{};
                GetWindowRect(control(dialog, 152), &list);
                GetWindowRect(control(dialog, 102), &buttons);
                require(list.right < buttons.left, "Application list must not overlap its buttons");
                require(text(control(dialog, 153)).find(L"settings.") == std::wstring::npos,
                    "New controls must be translated in every bundled language");
                SetWindowPos(dialog, nullptr, 0, 0, MulDiv(650, dpi, 96), MulDiv(520, dpi, 96),
                    SWP_NOZORDER | SWP_NOMOVE);
                SetFocus(control(dialog, 152));
                RECT viewport{}, visible{};
                GetWindowRect(GetParent(GetParent(control(dialog, 152))), &viewport);
                GetWindowRect(control(dialog, 152), &visible);
                require(visible.top >= viewport.top && visible.bottom <= viewport.bottom,
                    "Keyboard focus must reveal the application list in a small window");
                RECT footer{}, client{};
                GetWindowRect(control(dialog, IDOK), &footer);
                MapWindowPoints(HWND_DESKTOP, dialog, reinterpret_cast<POINT*>(&footer), 2);
                GetClientRect(dialog, &client);
                require(footer.top >= 0 && footer.bottom <= client.bottom, "Save stays accessible when scrolling");
            }
            if (std::string_view(locale) == "zh-CN" && !screenshot_path.empty()) {
                RECT normal{0, 0, 1000, 850};
                SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&normal));
                UpdateWindow(dialog);
                save_screenshot(dialog);
            }
            click(dialog, IDCANCEL);
        }, locale), "Layout verification must not save changes");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 1) screenshot_path = argv[1];
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        application_editor();
        limits_and_layout();
        std::cout << "Keyboard mapping dialog tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
