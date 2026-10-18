#pragma once

#include <Windows.h>
#include <commctrl.h>

#include <string_view>

namespace simpilot::toggle_switch {

[[nodiscard]] HWND create(HINSTANCE instance, HWND parent, int identifier,
                          std::wstring_view accessible_name, bool checked,
                          bool draw_label = false);

[[nodiscard]] HIMAGELIST create_state_image_list(UINT dpi);

} // namespace simpilot::toggle_switch
