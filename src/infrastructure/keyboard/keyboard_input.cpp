#include "simpilot/keyboard_input.hpp"
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace simpilot {
namespace {
std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    const auto last = value.find_last_not_of(L" \t\r\n");
    return first == std::wstring::npos ? std::wstring{}
                                      : value.substr(first, last - first + 1);
}

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

template <typename Candidate>
std::size_t modifier_count(const Candidate& candidate) noexcept {
    return std::min(candidate.modifier_count, candidate.modifiers.size());
}

} // namespace

bool is_mapping_modifier(const UINT virtual_key) noexcept {
    return virtual_key == VK_CONTROL || virtual_key == VK_SHIFT
        || virtual_key == VK_MENU
        || is_mapping_physical_modifier(virtual_key);
}

bool is_mapping_physical_modifier(const UINT virtual_key) noexcept {
    return virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL
        || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT
        || virtual_key == VK_LMENU || virtual_key == VK_RMENU
        || virtual_key == VK_LWIN || virtual_key == VK_RWIN;
}

bool is_mapping_key_valid(const PhysicalKey& key) noexcept {
    if (key.virtual_key == 0 || key.virtual_key >= 0xFF) return false;
    if (key.virtual_key == VK_PACKET || key.virtual_key == VK_PROCESSKEY) return false;
    // The low-level hook normally reports an 8-bit make code, but extended
    // keys are also represented by the 0x100+ form by some producers.  The
    // persisted/input representation is a WORD, so accept the full range
    // that can be carried losslessly through INPUT::ki.wScan.
    if (key.scan_code > 0xFFFF) return false;
    return true;
}

std::wstring normalize_mapping_process_name(std::wstring value) {
    value = lowercase(trim(std::move(value)));
    if (value.empty()) return {};
    if (value.find_first_of(L"\\/:") != std::wstring::npos) return {};
    if (!value.ends_with(L".exe")) value.append(L".exe");
    return value;
}

std::wstring format_mapping_key(const PhysicalKey& key) {
    if (key.empty()) return L"";
    if ((key.virtual_key >= L'A' && key.virtual_key <= L'Z')
        || (key.virtual_key >= L'0' && key.virtual_key <= L'9')) {
        return std::wstring(1, static_cast<wchar_t>(key.virtual_key));
    }
    if (key.virtual_key >= VK_F1 && key.virtual_key <= VK_F24) {
        return L"F" + std::to_wstring(key.virtual_key - VK_F1 + 1);
    }
    switch (key.virtual_key) {
    case VK_CONTROL: return L"Ctrl";
    case VK_LCONTROL: return L"Left Ctrl";
    case VK_RCONTROL: return L"Right Ctrl";
    case VK_MENU: return L"Alt";
    case VK_LMENU: return L"Left Alt";
    case VK_RMENU: return L"Right Alt";
    case VK_SHIFT: return L"Shift";
    case VK_LSHIFT: return L"Left Shift";
    case VK_RSHIFT: return L"Right Shift";
    case VK_LWIN: return L"Left Win";
    case VK_RWIN: return L"Right Win";
    case VK_ESCAPE: return L"Esc";
    case VK_TAB: return L"Tab";
    case VK_CAPITAL: return L"Caps Lock";
    case VK_BACK: return L"Backspace";
    case VK_RETURN: return key.extended ? L"Numpad Enter" : L"Enter";
    case VK_SPACE: return L"Space";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_LEFT: return L"Left Arrow";
    case VK_RIGHT: return L"Right Arrow";
    case VK_UP: return L"Up Arrow";
    case VK_DOWN: return L"Down Arrow";
    case VK_SNAPSHOT: return L"Print Screen";
    case VK_NUMLOCK: return L"Num Lock";
    case VK_SCROLL: return L"Scroll Lock";
    case VK_PAUSE: return L"Pause";
    case VK_APPS: return L"Applications";
    case VK_DECIMAL: return L"Numpad Decimal";
    case VK_DIVIDE: return L"Numpad Divide";
    case VK_MULTIPLY: return L"Numpad Multiply";
    case VK_SUBTRACT: return L"Numpad Subtract";
    case VK_ADD: return L"Numpad Add";
    case VK_BROWSER_BACK: return L"Browser Back";
    case VK_BROWSER_FORWARD: return L"Browser Forward";
    case VK_BROWSER_REFRESH: return L"Browser Refresh";
    case VK_BROWSER_STOP: return L"Browser Stop";
    case VK_BROWSER_SEARCH: return L"Browser Search";
    case VK_BROWSER_FAVORITES: return L"Browser Favorites";
    case VK_BROWSER_HOME: return L"Browser Home";
    case VK_VOLUME_MUTE: return L"Volume Mute";
    case VK_VOLUME_DOWN: return L"Volume Down";
    case VK_VOLUME_UP: return L"Volume Up";
    case VK_MEDIA_NEXT_TRACK: return L"Media Next Track";
    case VK_MEDIA_PREV_TRACK: return L"Media Previous Track";
    case VK_MEDIA_STOP: return L"Media Stop";
    case VK_MEDIA_PLAY_PAUSE: return L"Media Play/Pause";
    case VK_LAUNCH_MAIL: return L"Launch Mail";
    case VK_LAUNCH_MEDIA_SELECT: return L"Launch Media";
    case VK_LAUNCH_APP1: return L"Launch App 1";
    case VK_LAUNCH_APP2: return L"Launch App 2";
    default: break;
    }
    if (key.virtual_key >= VK_NUMPAD0 && key.virtual_key <= VK_NUMPAD9) {
        return L"Numpad " + std::to_wstring(key.virtual_key - VK_NUMPAD0);
    }

    const auto oem_name = [&key](const std::wstring_view canonical) {
        auto scan_code = key.scan_code == 0
            ? MapVirtualKeyW(key.virtual_key, MAPVK_VK_TO_VSC)
            : key.scan_code;
        auto extended = key.extended;
        if ((scan_code & 0xFF00U) == 0x0100U) {
            scan_code &= 0x00FFU;
            extended = true;
        }
        const auto scan = (scan_code << 16U)
            | (extended ? (1U << 24U) : 0U);
        wchar_t buffer[64]{};
        std::wstring result;
        if (GetKeyNameTextW(static_cast<LONG>(scan), buffer,
                            static_cast<int>(std::size(buffer))) > 0) {
            result = buffer;
        }
        if (!result.empty()) result.append(L" (");
        result.append(canonical);
        if (result != canonical) result.push_back(L')');
        return result;
    };
    switch (key.virtual_key) {
    case VK_OEM_1: return oem_name(L"OEM 1");
    case VK_OEM_PLUS: return oem_name(L"OEM Plus");
    case VK_OEM_COMMA: return oem_name(L"OEM Comma");
    case VK_OEM_MINUS: return oem_name(L"OEM Minus");
    case VK_OEM_PERIOD: return oem_name(L"OEM Period");
    case VK_OEM_2: return oem_name(L"OEM 2");
    case VK_OEM_3: return oem_name(L"OEM 3");
    case VK_OEM_4: return oem_name(L"OEM 4");
    case VK_OEM_5: return oem_name(L"OEM 5");
    case VK_OEM_6: return oem_name(L"OEM 6");
    case VK_OEM_7: return oem_name(L"OEM 7");
    case VK_OEM_8: return oem_name(L"OEM 8");
    case VK_OEM_102: return oem_name(L"OEM 102 / ISO");
    default: break;
    }

    std::wostringstream fallback;
    fallback << L"VK 0x" << std::uppercase << std::hex
             << std::setw(2) << std::setfill(L'0') << key.virtual_key;
    return fallback.str();
}

std::wstring format_mapping_trigger(const KeyboardTrigger& trigger) {
    if (trigger.single_key) return format_mapping_key(trigger.action);
    std::wstring result;
    for (std::size_t index = 0; index < modifier_count(trigger); ++index) {
        if (!result.empty()) result.push_back(L'+');
        result.append(format_mapping_key(trigger.modifiers[index]));
    }
    if (!result.empty()) result.push_back(L'+');
    result.append(format_mapping_key(trigger.action));
    if (trigger.chord_action) {
        result.append(L" + ").append(format_mapping_key(*trigger.chord_action));
    }
    return result;
}

std::wstring format_mapping_output(const KeyboardOutput& output) {
    if (output.single_key) return format_mapping_key(output.action);
    std::wstring result;
    for (std::size_t index = 0; index < modifier_count(output); ++index) {
        if (!result.empty()) result.push_back(L'+');
        result.append(format_mapping_key(output.modifiers[index]));
    }
    if (!result.empty()) result.push_back(L'+');
    result.append(format_mapping_key(output.action));
    return result;
}

} // namespace simpilot
