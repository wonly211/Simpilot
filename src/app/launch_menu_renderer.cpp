#include "launch_menu_renderer.hpp"

#include "menu_theme.hpp"

#include <algorithm>

namespace simpilot {
namespace {

COLORREF background_color(const bool dark, const bool high_contrast,
                          const bool selected) noexcept {
    if (high_contrast) return GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_MENU);
    if (dark) return selected ? RGB(64, 64, 64) : RGB(32, 32, 32);
    return selected ? RGB(229, 243, 255) : GetSysColor(COLOR_MENU);
}

COLORREF foreground_color(const bool dark, const bool high_contrast,
                          const bool selected, const bool disabled) noexcept {
    if (disabled) return high_contrast ? GetSysColor(COLOR_GRAYTEXT)
                                      : dark ? RGB(145, 145, 145) : GetSysColor(COLOR_GRAYTEXT);
    if (high_contrast) return GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT);
    return dark ? RGB(245, 245, 245) : GetSysColor(COLOR_MENUTEXT);
}

} // namespace

UINT launch_menu_alignment(const POINT cursor, const RECT work_area,
                           const SIZE menu_size) noexcept {
    const auto open_left = work_area.right - cursor.x < menu_size.cx;
    const auto open_up = work_area.bottom - cursor.y < menu_size.cy;
    return (open_left ? TPM_RIGHTALIGN : TPM_LEFTALIGN)
        | (open_up ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
}

LaunchMenuRenderer::~LaunchMenuRenderer() {
    end();
}

void LaunchMenuRenderer::begin(const MenuTheme theme, const UINT dpi) {
    end();
    dpi_ = dpi == 0 ? 96 : dpi;
    dark_ = MenuThemeController::dark_mode_enabled(theme);
    HIGHCONTRASTW contrast{.cbSize = sizeof(contrast)};
    high_contrast_ = SystemParametersInfoW(
        SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    font_ = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void LaunchMenuRenderer::end() noexcept {
    items_.clear();
    if (font_) DeleteObject(font_);
    font_ = nullptr;
}

bool LaunchMenuRenderer::append(const HMENU menu, const UINT command_id,
                                const std::wstring_view text, const HICON icon,
                                const HMENU submenu) {
    if (!menu || !font_) return false;
    auto item = std::make_unique<Item>(Item{
        .text = std::wstring(text),
        .icon = icon,
        .submenu = submenu != nullptr,
    });
    MENUITEMINFOW information{
        .cbSize = sizeof(information),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STRING,
        .fType = MFT_OWNERDRAW,
        .wID = command_id,
        .dwItemData = reinterpret_cast<ULONG_PTR>(item.get()),
        .dwTypeData = item->text.data(),
        .cch = static_cast<UINT>(item->text.size()),
    };
    if (submenu) {
        information.fMask |= MIIM_SUBMENU;
        information.hSubMenu = submenu;
    }
    if (!InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE,
                         &information)) {
        return false;
    }
    items_.push_back(std::move(item));
    return true;
}

bool LaunchMenuRenderer::append_separator(const HMENU menu) {
    if (!menu || !font_) return false;
    auto item = std::make_unique<Item>(Item{.separator = true});
    MENUITEMINFOW information{
        .cbSize = sizeof(information),
        .fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA,
        .fType = MFT_OWNERDRAW,
        .wID = 0,
        .dwItemData = reinterpret_cast<ULONG_PTR>(item.get()),
    };
    if (!InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE,
                         &information)) {
        return false;
    }
    items_.push_back(std::move(item));
    return true;
}

bool LaunchMenuRenderer::measure(MEASUREITEMSTRUCT& measurement) const noexcept {
    if (measurement.CtlType != ODT_MENU || measurement.itemData == 0 || !font_) return false;
    const auto* item = reinterpret_cast<const Item*>(measurement.itemData);
    if (item->separator) {
        measurement.itemHeight = static_cast<UINT>(scale(9));
        measurement.itemWidth = 0;
        return true;
    }
    const auto dc = GetDC(nullptr);
    if (!dc) return false;
    const auto previous_font = SelectObject(dc, font_);
    RECT text_rectangle{};
    DrawTextW(dc, item->text.c_str(), static_cast<int>(item->text.size()),
              &text_rectangle, DT_CALCRECT | DT_SINGLELINE);
    SelectObject(dc, previous_font);
    ReleaseDC(nullptr, dc);
    const auto icon_size = scale(24);
    const auto text_height = static_cast<int>(
        text_rectangle.bottom - text_rectangle.top);
    measurement.itemHeight = static_cast<UINT>(
        std::max(icon_size + scale(10), text_height + scale(14)));
    measurement.itemWidth = static_cast<UINT>(
        scale(10) + icon_size + scale(10)
        + (text_rectangle.right - text_rectangle.left)
        + (item->submenu ? scale(28) : scale(16)));
    return true;
}

SIZE LaunchMenuRenderer::measure_menu(const HMENU menu) const noexcept {
    SIZE result{};
    if (!menu || !font_) return result;
    const auto count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index) {
        MENUITEMINFOW information{
            .cbSize = sizeof(information),
            .fMask = MIIM_FTYPE | MIIM_DATA,
        };
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &information)
            || information.dwItemData == 0
            || (information.fType & MFT_OWNERDRAW) == 0) {
            continue;
        }
        MEASUREITEMSTRUCT measurement{
            .CtlType = ODT_MENU,
            .itemData = information.dwItemData,
        };
        if (!measure(measurement)) continue;
        result.cx = std::max(result.cx, static_cast<LONG>(measurement.itemWidth));
        result.cy += static_cast<LONG>(measurement.itemHeight);
    }
    const auto border = std::max(1, GetSystemMetricsForDpi(SM_CXEDGE, dpi_));
    result.cx += border * 2;
    result.cy += border * 2;
    return result;
}

bool LaunchMenuRenderer::draw(const DRAWITEMSTRUCT& drawing) const noexcept {
    if (drawing.CtlType != ODT_MENU || drawing.itemData == 0 || !font_ || !drawing.hDC) {
        return false;
    }
    const auto* item = reinterpret_cast<const Item*>(drawing.itemData);
    const auto selected = (drawing.itemState & ODS_SELECTED) != 0;
    const auto disabled = (drawing.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    const auto background = CreateSolidBrush(
        background_color(dark_, high_contrast_, selected));
    FillRect(drawing.hDC, &drawing.rcItem, background);
    DeleteObject(background);

    if (item->separator) {
        const auto line_color = high_contrast_ ? GetSysColor(COLOR_MENUTEXT)
            : dark_ ? RGB(82, 82, 82) : RGB(210, 210, 210);
        const auto pen = CreatePen(PS_SOLID, std::max(1, scale(1)), line_color);
        const auto previous_pen = SelectObject(drawing.hDC, pen);
        const auto y = (drawing.rcItem.top + drawing.rcItem.bottom) / 2;
        MoveToEx(drawing.hDC, drawing.rcItem.left + scale(10), y, nullptr);
        LineTo(drawing.hDC, drawing.rcItem.right - scale(10), y);
        SelectObject(drawing.hDC, previous_pen);
        DeleteObject(pen);
        return true;
    }

    const auto icon_size = scale(24);
    const auto icon_x = drawing.rcItem.left + scale(10);
    const auto icon_y = drawing.rcItem.top
        + ((drawing.rcItem.bottom - drawing.rcItem.top) - icon_size) / 2;
    if (item->icon) {
        DrawIconEx(drawing.hDC, icon_x, icon_y, item->icon, icon_size, icon_size,
                   0, nullptr, DI_NORMAL);
    }

    const auto previous_font = SelectObject(drawing.hDC, font_);
    const auto previous_background_mode = SetBkMode(drawing.hDC, TRANSPARENT);
    const auto previous_text_color = SetTextColor(
        drawing.hDC, foreground_color(dark_, high_contrast_, selected, disabled));
    RECT text_rectangle = drawing.rcItem;
    text_rectangle.left = icon_x + icon_size + scale(10);
    text_rectangle.right -= item->submenu ? scale(28) : scale(12);
    DrawTextW(drawing.hDC, item->text.c_str(), static_cast<int>(item->text.size()),
              &text_rectangle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    // Windows draws the submenu glyph after WM_DRAWITEM. We only reserve its
    // space here; drawing another chevron would produce overlapping arrows.

    SetTextColor(drawing.hDC, previous_text_color);
    SetBkMode(drawing.hDC, previous_background_mode);
    SelectObject(drawing.hDC, previous_font);
    return true;
}

int LaunchMenuRenderer::scale(const int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

} // namespace simpilot
