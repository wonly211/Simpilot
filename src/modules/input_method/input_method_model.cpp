#include "input_method_model.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace simpilot::input_method {
namespace {

std::wstring lower(std::wstring value) {
    std::ranges::transform(value, value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring get(const SettingsDocument& document, const std::wstring& key) {
    return document.get(key).value_or(L"");
}

std::size_t count(const SettingsDocument& document, const wchar_t* key) {
    const auto value = get(document, key);
    if (value.empty()) return 0;
    if (value.find_first_not_of(L"0123456789") != std::wstring::npos) {
        throw std::runtime_error("Invalid input method item count");
    }
    const auto result = std::stoull(value);
    if (result > 4096) throw std::runtime_error("Too many input method items");
    return static_cast<std::size_t>(result);
}

Mode read_mode(const std::wstring& text) {
    if (text.empty() || lower(text) == L"english") return Mode::english;
    if (lower(text) == L"chinese") return Mode::chinese;
    throw std::runtime_error("Invalid input method mode");
}

Policy read_policy(const std::wstring& text) {
    if (text.empty() || lower(text) == L"remember") return Policy::remember;
    if (lower(text) == L"fixed") return Policy::fixed;
    if (lower(text) == L"ignore") return Policy::ignore;
    throw std::runtime_error("Invalid input method policy");
}

const wchar_t* mode_text(Mode mode) {
    return mode == Mode::chinese ? L"chinese" : L"english";
}

const wchar_t* policy_text(Policy policy) {
    switch (policy) {
    case Policy::fixed: return L"fixed";
    case Policy::ignore: return L"ignore";
    default: return L"remember";
    }
}

} // namespace

std::wstring normalize_path(std::wstring_view path) {
    if (path.empty() || path.find_first_of(L"\r\n") != std::wstring_view::npos) return {};
    std::wstring candidate(path);
    if (candidate.starts_with(L"\\\\?\\UNC\\")) candidate = L"\\\\" + candidate.substr(8);
    else if (candidate.starts_with(L"\\\\?\\") && candidate.size() > 6
             && candidate[5] == L':') candidate.erase(0, 4);
    const std::filesystem::path value{candidate};
    // Rules are portable between working directories; relative paths must not
    // silently acquire a different identity when Simpilot is launched elsewhere.
    if (!value.is_absolute()) return {};
    const auto normalized = value.lexically_normal().make_preferred().wstring();
    const auto size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        normalized.data(), static_cast<int>(normalized.size()), nullptr, 0,
        nullptr, nullptr, 0);
    if (!size) return {};
    std::wstring identity(static_cast<std::size_t>(size), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        normalized.data(), static_cast<int>(normalized.size()), identity.data(), size,
        nullptr, nullptr, 0)) return {};
    return identity;
}

bool validate(const Settings& settings) {
    if (settings.rules.size() > 4096) return false;
    std::set<std::wstring> identities;
    for (const auto& rule : settings.rules) {
        const auto path = normalize_path(rule.executable_path);
        if (path.empty() || !identities.insert(path).second
            || rule.profile_id.find_first_of(L"\r\n") != std::wstring::npos
            || (rule.mode != Mode::chinese && rule.mode != Mode::english)
            || (rule.policy != Policy::remember && rule.policy != Policy::fixed
                && rule.policy != Policy::ignore)
            || (rule.policy == Policy::fixed && rule.profile_id.empty())) return false;
    }
    return true;
}

const Rule* find_rule(const Settings& settings, std::wstring_view path) {
    const auto found = std::ranges::find_if(settings.rules, [path](const Rule& rule) {
        return normalize_path(rule.executable_path) == path;
    });
    return found == settings.rules.end() ? nullptr : &*found;
}

const Profile* find_profile(const std::vector<Profile>& profiles, std::wstring_view id) {
    const auto found = std::ranges::find(profiles, id, &Profile::id);
    return found == profiles.end() ? nullptr : &*found;
}

std::optional<State> desired_state(
    const Settings& settings, const History& history, std::wstring_view path) {
    if (const auto* rule = find_rule(settings, path)) {
        if (rule->policy == Policy::ignore) return std::nullopt;
        if (rule->policy == Policy::fixed) return State{rule->profile_id, rule->mode};
    }
    const auto found = history.states.find(std::wstring(path));
    return found == history.states.end() ? std::nullopt : std::optional(found->second);
}

Settings Settings::read(const SettingsDocument& document) {
    Settings result;
    const auto enabled = lower(get(document, L"InputMethodEnabled"));
    result.enabled = enabled == L"1" || enabled == L"true" || enabled == L"yes"
        || enabled == L"on";
    const auto length = count(document, L"InputMethodRuleCount");
    for (std::size_t index = 1; index <= length; ++index) {
        const auto prefix = L"InputMethodRule" + std::to_wstring(index);
        result.rules.push_back({get(document, prefix + L"Path"),
            read_policy(get(document, prefix + L"Policy")),
            get(document, prefix + L"Profile"), read_mode(get(document, prefix + L"Mode"))});
    }
    if (!validate(result)) throw std::runtime_error("Invalid input method rules");
    return result;
}

void Settings::write(SettingsDocument& document) const {
    if (!validate(*this)) throw std::invalid_argument("Invalid input method rules");
    constexpr std::array<std::wstring_view, 4> suffixes{L"Path", L"Policy", L"Profile", L"Mode"};
    document.erase_numbered(L"InputMethodRule", suffixes);
    document.set(L"InputMethod", L"InputMethodEnabled", enabled ? L"1" : L"0");
    document.set(L"InputMethod", L"InputMethodRuleCount", std::to_wstring(rules.size()));
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto prefix = L"InputMethodRule" + std::to_wstring(index + 1);
        const auto& rule = rules[index];
        document.set(L"InputMethod", prefix + L"Path", rule.executable_path);
        document.set(L"InputMethod", prefix + L"Policy", policy_text(rule.policy));
        document.set(L"InputMethod", prefix + L"Profile", rule.profile_id);
        document.set(L"InputMethod", prefix + L"Mode", mode_text(rule.mode));
    }
}

History History::read(const SettingsDocument& document) {
    History result;
    const auto length = count(document, L"InputMethodHistoryCount");
    for (std::size_t index = 1; index <= length; ++index) {
        const auto prefix = L"InputMethodHistory" + std::to_wstring(index);
        const auto path = normalize_path(get(document, prefix + L"Path"));
        const auto profile = get(document, prefix + L"Profile");
        if (path.empty() || profile.empty()) {
            throw std::runtime_error("Invalid input method history");
        }
        result.states[path] = {profile, read_mode(get(document, prefix + L"Mode"))};
    }
    return result;
}

void History::write(SettingsDocument& document) const {
    constexpr std::array<std::wstring_view, 3> suffixes{L"Path", L"Profile", L"Mode"};
    document.erase_numbered(L"InputMethodHistory", suffixes);
    document.set(L"InputMethodHistory", L"InputMethodHistoryCount", std::to_wstring(states.size()));
    std::size_t index = 1;
    for (const auto& [path, state] : states) {
        const auto prefix = L"InputMethodHistory" + std::to_wstring(index++);
        document.set(L"InputMethodHistory", prefix + L"Path", path);
        document.set(L"InputMethodHistory", prefix + L"Profile", state.profile_id);
        document.set(L"InputMethodHistory", prefix + L"Mode", mode_text(state.mode));
    }
}

} // namespace simpilot::input_method
