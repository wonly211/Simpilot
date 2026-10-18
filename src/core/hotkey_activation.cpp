#include "simpilot/hotkey_activation.hpp"
#include "simpilot/localization.hpp"

#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <set>

namespace simpilot {

bool HotkeyActivationConditions::conditional() const noexcept {
    return require_running_program || require_foreground_program || priority.has_value() || std::ranges::any_of(locks,
        [](auto value) { return value != ToggleRequirement::any; });
}

std::uint8_t HotkeyActivationConditions::allowed_lock_states() const noexcept {
    std::uint8_t result = 0;
    for (unsigned state = 0; state < 8; ++state) {
        bool matches = true;
        for (unsigned key = 0; key < locks.size(); ++key) {
            const auto value = locks[key];
            if (value == ToggleRequirement::any) continue;
            if ((value != ToggleRequirement::on && value != ToggleRequirement::off)
                || (((state >> key) & 1U) != static_cast<unsigned>(value == ToggleRequirement::on))) matches = false;
        }
        if (matches) result |= static_cast<std::uint8_t>(1U << state);
    }
    return result;
}

std::wstring normalize_hotkey_program_path(std::wstring_view path) {
    if (path.empty() || path.find_first_of(L"\r\n\0", 0, 3) != path.npos) return {};
    std::wstring candidate(path);
    if (candidate.starts_with(L"\\\\?\\UNC\\")) candidate = L"\\\\" + candidate.substr(8);
    else if (candidate.starts_with(L"\\\\?\\") && candidate.size() > 6 && candidate[5] == L':')
        candidate.erase(0, 4);
    const std::filesystem::path value(candidate);
    if (!value.is_absolute()) return {};
    const auto normalized = value.lexically_normal().make_preferred().wstring();
    const auto length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        normalized.data(), static_cast<int>(normalized.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!length) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, normalized.data(),
        static_cast<int>(normalized.size()), result.data(), length, nullptr, nullptr, 0)) return {};
    return result;
}

bool HotkeyActivationConditions::valid() const {
    if (!allowed_lock_states() || programs.size() > 32
        || ((require_running_program || require_foreground_program) && programs.empty())
        || (require_running_program && require_foreground_program)
        || (priority && *priority > 999)) return false;
    std::set<std::wstring> identities;
    for (const auto& program : programs) {
        const auto normalized = normalize_hotkey_program_path(program);
        if (normalized.empty() || !normalized.ends_with(L".exe")
            || normalized.find_first_of(L"*?<>|\"") != normalized.npos
            || normalized.find(L':', normalized.size() > 1 && normalized[1] == L':' ? 2 : 0) != normalized.npos
            || std::ranges::any_of(normalized, [](wchar_t character) { return character < 32; })
            || !identities.insert(normalized).second) return false;
    }
    return true;
}

bool activation_overlaps(const HotkeyActivationConditions& left,
                         const HotkeyActivationConditions& right) {
    // Positive process requirements cannot establish mutual exclusion: both
    // programs may run at once, even if they live at different paths.
    if (!(left.allowed_lock_states() & right.allowed_lock_states())) return false;
    if (left.require_foreground_program && right.require_foreground_program) {
        for (const auto& a : left.programs) for (const auto& b : right.programs)
            if (normalize_hotkey_program_path(a) == normalize_hotkey_program_path(b)) return true;
        return false;
    }
    return true;
}

bool activation_conflicts(const HotkeyActivationConditions& left,
                          const HotkeyActivationConditions& right) {
    return activation_overlaps(left, right)
        && !(left.priority && right.priority && left.priority != right.priority);
}

std::wstring activation_description(const HotkeyActivationConditions& conditions,
                                    const Localization& localization) {
    if (!conditions.conditional()) return std::wstring(localization.text("hotkey.activation.any"));
    std::wstring result;
    const auto append = [&](std::wstring_view value) {
        if (!result.empty()) result += L"; ";
        result += value;
    };
    constexpr std::wstring_view names[]{L"Caps Lock", L"Num Lock", L"Scroll Lock"};
    for (std::size_t i = 0; i < conditions.locks.size(); ++i) {
        if (conditions.locks[i] == ToggleRequirement::any) continue;
        append(std::wstring(names[i]) + L": " + std::wstring(localization.text(
            conditions.locks[i] == ToggleRequirement::on ? "hotkey.activation.on" : "hotkey.activation.off")));
    }
    if (conditions.priority) {
        append(std::wstring(localization.text("hotkey.activation.priority")) + L": " + std::to_wstring(*conditions.priority));
    }
    if (conditions.require_running_program || conditions.require_foreground_program) {
        append(localization.text(conditions.require_foreground_program
            ? "hotkey.activation.foreground_any" : "hotkey.activation.running_any"));
        for (const auto& program : conditions.programs) { result += L"\n"; result += program; }
    }
    return result;
}

} // namespace simpilot
