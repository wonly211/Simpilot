#include "scrollable_form.hpp"
#include "settings_visual_style.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void form_test() {
    const auto instance = GetModuleHandleW(nullptr);
    const auto host = CreateWindowW(L"STATIC", L"Isolated form test", WS_OVERLAPPEDWINDOW,
        0, 0, 900, 650, nullptr, nullptr, instance, nullptr);
    const auto field = CreateWindowW(L"EDIT", L"Field", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        400, 800, 160, 32, host, reinterpret_cast<HMENU>(10), instance, nullptr);
    const auto footer = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        700, 550, 88, 36, host, reinterpret_cast<HMENU>(1), instance, nullptr);
    const auto second_field = CreateWindowW(L"EDIT", L"Second field", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        400, 850, 160, 32, host, reinterpret_cast<HMENU>(11), instance, nullptr);
    require(host && field && footer, "Create isolated native controls");
    {
        simpilot::ScrollableForm form;
        form.attach(host, {footer});
        form.layout(600, 400, 800, 1000);
        require(GetParent(footer) == host && GetParent(field) != host,
            "Only the body is reparented; footer remains fixed");
        require(GetNextDlgTabItem(host, field, FALSE) == second_field
            && GetNextDlgTabItem(host, second_field, FALSE) == footer,
            "Reparenting preserves body Tab order before the fixed footer");
        const auto viewport = GetParent(GetParent(field));
        form.reveal(field);
        require(GetScrollPos(viewport, SB_VERT) > 0, "Keyboard focus reveals lower fields");
        RECT bounds{}, visible{};
        GetWindowRect(field, &bounds);
        MapWindowPoints(HWND_DESKTOP, viewport, reinterpret_cast<POINT*>(&bounds), 2);
        GetClientRect(viewport, &visible);
        require(bounds.top >= 0 && bounds.bottom <= visible.bottom,
            "Revealed field is inside the clipped viewport");
        form.layout(1000, 1100, 800, 1000);
        require(GetScrollPos(viewport, SB_VERT) == 0 && GetScrollPos(viewport, SB_HORZ) == 0,
            "Growing the viewport clears obsolete scroll positions");
        form.layout(600, 400, 800, 1000);
        form.layout(800, 1000, 800, 1000);
        require((GetWindowLongPtrW(viewport, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)) == 0,
            "Exact-fit content removes both bars instead of retaining them mutually");
        DestroyWindow(host);
    }
}

void typography_test() {
    simpilot::settings_visual_style::PageTypography fonts;
    for (UINT dpi : {96U, 144U, 192U}) {
        fonts.update(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), dpi);
        LOGFONTW body{}, title{};
        GetObjectW(fonts.body(), sizeof(body), &body);
        GetObjectW(fonts.title(), sizeof(title), &title);
        require(body.lfHeight == -MulDiv(14, dpi, 96) && title.lfHeight == -MulDiv(24, dpi, 96),
            "Body and title are DIP-sized, independent of container dimensions");
        require(body.lfWeight == FW_NORMAL && title.lfWeight == FW_SEMIBOLD,
            "Typography retains restrained weight hierarchy");
    }
    if (!simpilot::settings_visual_style::high_contrast_enabled())
        require(simpilot::settings_visual_style::background_color() == RGB(247, 248, 250),
            "Settings canvas uses the selected baseline token");
}

void path_text_test() {
    const auto dc = CreateCompatibleDC(nullptr);
    const std::wstring path = L"C:\\Applications\\" + std::wstring(180, L'a') + L".exe";
    const auto display = simpilot::settings_visual_style::compact_path_text(dc, path, 240);
    SIZE size{};
    GetTextExtentPoint32W(dc, display.data(), static_cast<int>(display.size()), &size);
    require(size.cx <= 240 && display.starts_with(L"C:") && display.ends_with(L".exe"),
        "Long basenames retain their extension inside the column");
    require(path.size() == 200, "Compacting a path does not mutate the stored value");
    require(simpilot::settings_visual_style::compact_path_text(dc, L"C:\\a.exe", 240) == L"C:\\a.exe",
        "Short paths remain unchanged");
    require(simpilot::settings_visual_style::compact_path_text(dc, path, 0).empty(),
        "Zero-width cells do not overflow");
    const std::wstring unicode = L"C:\\" + std::wstring(40, L'a') + L"\xD83D\xDE00-file.exe";
    for (int width = 1; width < 300; ++width) {
        const auto compact = simpilot::settings_visual_style::compact_path_text(dc, unicode, width);
        SIZE extent{};
        GetTextExtentPoint32W(dc, compact.data(), static_cast<int>(compact.size()), &extent);
        require(extent.cx <= width, "Compacted text fits even very narrow cells");
        for (std::size_t i = 0; i < compact.size(); ++i) {
            if (compact[i] >= 0xD800 && compact[i] <= 0xDBFF)
                require(i + 1 < compact.size() && compact[i + 1] >= 0xDC00 && compact[i + 1] <= 0xDFFF,
                    "Compacting preserves UTF-16 high-surrogate pairs");
            if (compact[i] >= 0xDC00 && compact[i] <= 0xDFFF)
                require(i > 0 && compact[i - 1] >= 0xD800 && compact[i - 1] <= 0xDBFF,
                    "Compacting preserves UTF-16 low-surrogate pairs");
        }
    }
    DeleteDC(dc);
}

void button_state_test() {
    const auto instance = GetModuleHandleW(nullptr);
    const auto host = CreateWindowW(L"STATIC", L"Buttons", WS_OVERLAPPEDWINDOW,
        0, 0, 600, 400, nullptr, nullptr, instance, nullptr);
    const auto content = CreateWindowW(L"STATIC", L"", WS_CHILD,
        0, 0, 400, 300, host, nullptr, instance, nullptr);
    const auto button = CreateWindowW(L"BUTTON", L"Move up", WS_CHILD | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
        0, 0, 100, 36, host, nullptr, instance, nullptr);
    simpilot::settings_visual_style::style_button(button, simpilot::settings_visual_style::ButtonStyle::up);
    struct Search { HWND button; HWND owner; HWND tooltip; } search{button, host, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(value);
        wchar_t name[64]{};
        GetClassNameW(window, name, 64);
        if (wcscmp(name, TOOLTIPS_CLASSW) == 0) {
            wchar_t text[256]{};
            TOOLINFOW tool{sizeof(tool)};
            tool.hwnd = search.owner;
            tool.uId = reinterpret_cast<UINT_PTR>(search.button);
            tool.lpszText = text;
            if (SendMessageW(window, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool)))
                search.tooltip = window;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.tooltip != nullptr, "Icon button exposes a tooltip");
    SetParent(button, content);
    SetWindowTextW(button, L"Move higher");
    wchar_t tooltip_text[256]{};
    TOOLINFOW info{sizeof(info)};
    info.hwnd = host;
    info.uId = reinterpret_cast<UINT_PTR>(button);
    info.lpszText = tooltip_text;
    require(SendMessageW(search.tooltip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&info))
        && std::wstring_view(info.lpszText) == L"Move higher",
        "Language updates retain the tooltip identity after reparenting");

    simpilot::settings_visual_style::style_button(button);
    const auto screen = GetDC(host);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 100, 36);
    const auto previous = SelectObject(dc, bitmap);
    SendMessageW(button, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    const auto normal = GetPixel(dc, 10, 10);
    SendMessageW(button, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(button, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    require(GetPixel(dc, 10, 10) != normal, "Checked menu segments remain visually distinct without focus");
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(host, screen);
    DestroyWindow(host);
}
}

int main() {
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        form_test();
        typography_test();
        path_text_test();
        button_state_test();
        std::cout << "Visual infrastructure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
