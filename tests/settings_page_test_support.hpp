#pragma once

#include "simpilot/settings_registry.hpp"

#include <stdexcept>

namespace settings_page_test {

inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class Host final {
public:
    Host() {
        window = CreateWindowW(L"STATIC", L"Settings page regression",
            WS_OVERLAPPEDWINDOW, 0, 0, 1400, 1000, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Create settings page test host");
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    ~Host() { DestroyWindow(window); }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    HWND window = nullptr;
};

inline RECT visible_bounds(HWND page, HWND control) {
    require(control && IsWindowVisible(control), "Settings control must be visible");
    RECT bounds{}, client{};
    GetWindowRect(control, &bounds);
    MapWindowPoints(HWND_DESKTOP, page, reinterpret_cast<POINT*>(&bounds), 2);
    GetClientRect(page, &client);
    require(bounds.right > bounds.left && bounds.bottom > bounds.top,
            "Settings control must have a nonempty layout");
    require(bounds.left >= 0 && bounds.top >= 0
        && bounds.right <= client.right && bounds.bottom <= client.bottom,
        "Settings control must fit inside the page");
    return bounds;
}

inline void uniform_background(HWND page) {
    const auto screen = GetDC(page);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 8, 8);
    ReleaseDC(page, screen);
    require(dc && bitmap, "Create background test surface");
    const auto previous = SelectObject(dc, bitmap);
    const RECT area{0, 0, 8, 8};
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SendMessageW(page, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
    const auto background = GetPixel(dc, 0, 0);
    HIGHCONTRASTW contrast{sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    const auto expected = contrast.dwFlags & HCF_HIGHCONTRASTON
        ? GetSysColor(COLOR_WINDOW) : RGB(247, 248, 250);
    bool matched = background == expected;
    int checked = 0;
    for (auto child = GetWindow(page, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t name[32]{};
        GetClassNameW(child, name, 32);
        const bool label = _wcsicmp(name, L"STATIC") == 0;
        if (!label && _wcsicmp(name, L"BUTTON") != 0) continue;
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(255, 0, 255));
        const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(page,
            label ? WM_CTLCOLORSTATIC : WM_CTLCOLORBTN,
            reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(child)));
        matched = matched && brush && GetBkMode(dc) == TRANSPARENT
            && GetBkColor(dc) == background;
        if (brush) FillRect(dc, &area, brush);
        matched = matched && GetPixel(dc, 0, 0) == background;
        ++checked;
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    require(checked > 0 && matched, "Labels and buttons must share the settings page background");
}

inline void check_page(simpilot::SettingsRegistry& pages, const wchar_t* class_name) {
    Host host;
    simpilot::Localization localization(simpilot::UiLanguage::english);
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    pages.visit([&](const auto&, const auto& contribution) {
        auto page = contribution.create();
        page->create({GetModuleHandleW(nullptr), host.window, 96, font, localization, {}});
        const auto window = FindWindowExW(host.window, nullptr, class_name, nullptr);
        require(window != nullptr, "Create module-owned settings page");
        for (auto language : {simpilot::UiLanguage::english, simpilot::UiLanguage::simplified_chinese,
                              simpilot::UiLanguage::traditional_chinese}) {
            localization.set_language(language);
            page->refresh_language(localization);
            for (UINT dpi : {96U, 144U, 192U}) {
                page->layout({0, 0, MulDiv(650, dpi, 96), MulDiv(440, dpi, 96)}, dpi, font);
                page->show(true);
                for (auto child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
                    if (IsWindowVisible(child)) (void)visible_bounds(window, child);
                uniform_background(window);
                page->show(false);
                require(!IsWindowVisible(window), "Hiding a page hides its controls");
            }
        }
    });
}

} // namespace settings_page_test
