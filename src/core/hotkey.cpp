#include "simpilot/hotkey.hpp"

#include <iterator>

namespace simpilot {
namespace {

std::wstring key_name(const UINT key) {
    if (key >= L'A' && key <= L'Z') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= L'0' && key <= L'9') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    switch (key) {
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_SPACE: return L"Space";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_END: return L"End";
    case VK_HOME: return L"Home";
    case VK_LEFT: return L"Left";
    case VK_UP: return L"Up";
    case VK_RIGHT: return L"Right";
    case VK_DOWN: return L"Down";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_CAPITAL: return L"CapsLock";
    case VK_PAUSE: return L"Pause";
    case VK_SNAPSHOT: return L"PrintScreen";
    case VK_OEM_3: return L"`";
    default: break;
    }
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16U;
    wchar_t buffer[64]{};
    return GetKeyNameTextW(static_cast<LONG>(scan), buffer, static_cast<int>(std::size(buffer))) > 0
        ? std::wstring(buffer) : L"VK" + std::to_wstring(key);
}

} // namespace

std::wstring HotKeyGesture::display_text() const {
    std::wstring result;
    if ((modifiers & MOD_CONTROL) != 0) result.append(L"Ctrl+");
    if ((modifiers & MOD_ALT) != 0) result.append(L"Alt+");
    if ((modifiers & MOD_SHIFT) != 0) result.append(L"Shift+");
    if ((modifiers & MOD_WIN) != 0) result.append(L"Win+");
    result.append(key_name(virtual_key));
    return result;
}

bool HotKeyGesture::is_modifier_key(const UINT virtual_key) noexcept {
    return virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL
        || virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT
        || virtual_key == VK_MENU || virtual_key == VK_LMENU || virtual_key == VK_RMENU
        || virtual_key == VK_LWIN || virtual_key == VK_RWIN;
}

std::optional<std::size_t> windows_letter_hotkey_index(
    const HotKeyGesture& gesture) noexcept {
    if (gesture.modifiers != MOD_WIN
        || gesture.virtual_key < L'A' || gesture.virtual_key > L'Z') {
        return std::nullopt;
    }
    return static_cast<std::size_t>(gesture.virtual_key - L'A');
}

bool is_supported_windows_letter_hotkey(
    const HotKeyGesture& gesture) noexcept {
    const auto index = windows_letter_hotkey_index(gesture);
    return index && *index != static_cast<std::size_t>(L'L' - L'A');
}

} // namespace simpilot
