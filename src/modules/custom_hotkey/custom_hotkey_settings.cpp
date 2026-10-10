#include "custom_hotkey_settings.hpp"
#include <algorithm>
#include <cwctype>
#include "simpilot/hotkey_settings_codec.hpp"

namespace simpilot {
namespace {
unsigned number(const SettingsDocument& document, const std::wstring& key,
                unsigned fallback, unsigned maximum) {
    const auto value = document.get(key);
    if (!value) return fallback;
    try { return std::min(static_cast<unsigned>(std::stoul(*value)), maximum); }
    catch (...) { return fallback; }
}
bool boolean(const SettingsDocument& document, const std::wstring& key, bool fallback = false) {
    auto value = document.get(key);
    if (!value) return fallback;
    std::ranges::transform(*value, value->begin(), [](wchar_t c) { return std::towlower(c); });
    return *value == L"1" || *value == L"true" || *value == L"yes" || *value == L"on";
}
}
static CustomHotkeySettings read_collection(const SettingsDocument& document,
                                           const std::wstring& stem) {
    CustomHotkeySettings result;
    const auto count = number(document, stem + L"Count", 0, 128);
    result.items.reserve(count);
    for (unsigned i = 1; i <= count; ++i) {
        const auto prefix = stem + std::to_wstring(i);
        if (!document.get(prefix + L"Enabled")) continue;
        CustomGlobalHotKey item;
        const auto stored_code = document.get(prefix + L"Code");
        const auto code = stored_code.value_or(L"");
        const auto comma = code.find(L',');
        if (comma != std::wstring::npos) {
            try {
                const auto modifiers = static_cast<UINT>(std::stoul(code.substr(0, comma)));
                const auto key = static_cast<UINT>(std::stoul(code.substr(comma + 1)));
                if (key > 0 && key <= 0xff) {
                    item.binding.gesture = HotKeyGesture{
                        modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN), key};
                }
            } catch (...) {}
        }
        item.program_path = document.get(prefix + L"Program").value_or(L"");
        item.enabled = boolean(document, prefix + L"Enabled");
        const auto cleared_draft = stored_code && code.empty() && !item.enabled;
        if ((!item.binding.gesture && !cleared_draft) || item.program_path.empty()) continue;
        if (item.binding.gesture) {
            const auto letter = windows_letter_hotkey_index(*item.binding.gesture);
            if (letter == static_cast<std::size_t>(L'L' - L'A')) continue;
            item.binding.force_override = boolean(document, prefix + L"Force")
                || is_supported_windows_letter_hotkey(*item.binding.gesture);
        }
        item.action = static_cast<CustomHotKeyAction>(number(document, prefix + L"Action", 0, 2));
        item.arguments = document.get(prefix + L"Arguments").value_or(L"");
        item.working_directory = document.get(prefix + L"WorkingDirectory").value_or(L"");
        item.run_as_administrator = boolean(document, prefix + L"RunAsAdministrator");
        item.existing_process_action = static_cast<ExistingProcessAction>(
            number(document, prefix + L"ExistingProcessAction", 0, 2));
        item.visibility = static_cast<LaunchVisibility>(number(document, prefix + L"Visibility", 0, 3));
        result.items.push_back(std::move(item));
    }
    return result;
}
static void write_collection(const CustomHotkeySettings& settings, SettingsDocument& document,
                             const std::wstring& stem) {
    constexpr std::wstring_view suffixes[]{L"Code", L"Force", L"Action", L"Program",
        L"Arguments", L"WorkingDirectory", L"RunAsAdministrator", L"ExistingProcessAction",
        L"Visibility", L"Enabled", L"Order"};
    document.erase_numbered(stem, suffixes);
    document.set(L"CustomGlobalHotkeys", stem + L"Count", std::to_wstring(settings.items.size()));
    for (std::size_t i = 0; i < settings.items.size(); ++i) {
        const auto prefix = stem + std::to_wstring(i + 1);
        const auto& item = settings.items[i];
        const auto set = [&](std::wstring_view suffix, std::wstring_view value) {
            document.set(L"CustomGlobalHotkeys", prefix + std::wstring(suffix), value);
        };
        set(L"Code", item.binding.gesture
            ? std::to_wstring(item.binding.gesture->modifiers) + L","
                + std::to_wstring(item.binding.gesture->virtual_key) : L"");
        set(L"Force", item.binding.force_override ? L"1" : L"0");
        set(L"Action", std::to_wstring(static_cast<int>(item.action)));
        set(L"Program", item.program_path);
        set(L"Arguments", item.arguments);
        set(L"WorkingDirectory", item.working_directory);
        set(L"RunAsAdministrator", item.run_as_administrator ? L"1" : L"0");
        set(L"ExistingProcessAction", std::to_wstring(static_cast<int>(item.existing_process_action)));
        set(L"Visibility", std::to_wstring(static_cast<int>(item.visibility)));
        set(L"Enabled", item.enabled ? L"1" : L"0");
    }
}

namespace {
unsigned strict_number(const SettingsDocument& document, const std::wstring& key,
                       unsigned maximum, std::optional<unsigned> fallback = {}) {
    const auto value = document.get(key);
    if (!value && fallback) return *fallback;
    if (!value || value->empty() || value->find_first_not_of(L"0123456789") != value->npos)
        throw std::runtime_error("Invalid conditional hotkey setting");
    const auto result = std::stoul(*value);
    if (result > maximum) throw std::runtime_error("Conditional hotkey setting out of range");
    return static_cast<unsigned>(result);
}
constexpr std::wstring_view lock_suffixes[]{L"CapsLock", L"NumLock", L"ScrollLock"};
}

CustomHotkeySettings CustomHotkeySettings::read(const SettingsDocument& document) {
    auto result = read_collection(document, L"CustomGlobalHotKey");
    const auto count = strict_number(document, L"ConditionalGlobalHotKeyCount", 128, 0);
    const auto version = strict_number(document, L"ConditionalGlobalHotKeyVersion", 1, 0);
    if ((document.get(L"ConditionalGlobalHotKeyCount") && version != 1)
        || (document.get(L"ConditionalGlobalHotKeyVersion")
            && (!version || !document.get(L"ConditionalGlobalHotKeyCount"))))
        throw std::runtime_error("Unsupported conditional hotkey version");
    auto conditional = read_collection(document, L"ConditionalGlobalHotKey");
    if (conditional.items.size() != count || result.items.size() + count > 128)
        throw std::runtime_error("Invalid conditional hotkey collection");
    std::vector<std::pair<unsigned, CustomGlobalHotKey>> ordered;
    for (std::size_t i = 0; i < result.items.size(); ++i) {
        const auto order = strict_number(document, L"CustomGlobalHotKey" + std::to_wstring(i + 1) + L"Order",
                                         127, static_cast<unsigned>(i));
        ordered.emplace_back(order, std::move(result.items[i]));
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto prefix = L"ConditionalGlobalHotKey" + std::to_wstring(i + 1);
        validate_hotkey_setting(document, prefix);
        auto& item = conditional.items[i];
        // Unlike the legacy reader, a conditional record must never repair an
        // unknown enum or a truncated condition into an unconditional action.
        (void)strict_number(document, prefix + L"Action", 2);
        (void)strict_number(document, prefix + L"ExistingProcessAction", 2);
        (void)strict_number(document, prefix + L"Visibility", 3);
        for (const auto suffix : {L"Enabled", L"Force", L"RunAsAdministrator"})
            (void)strict_number(document, prefix + suffix, 1);
        auto& activation = item.binding.activation;
        for (std::size_t lock = 0; lock < 3; ++lock)
            activation.locks[lock] = static_cast<ToggleRequirement>(
                strict_number(document, prefix + std::wstring(lock_suffixes[lock]), 2));
        activation.require_running_program = strict_number(document, prefix + L"RequireRunning", 1) != 0;
        const auto programs = strict_number(document, prefix + L"ProgramCount", 32);
        for (unsigned program = 1; program <= programs; ++program) {
            activation.programs.push_back(document.get(prefix + L"RelatedProgram" + std::to_wstring(program)).value_or(L""));
        }
        if (!activation.valid()) throw std::runtime_error("Invalid hotkey activation conditions");
        const auto order = strict_number(document, prefix + L"Order", 127);
        ordered.emplace_back(order, std::move(item));
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (count) {
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            if (ordered[i].first != i) throw std::runtime_error("Invalid conditional hotkey ordering");
        }
    }
    result.items.clear();
    for (auto& [order, item] : ordered) { (void)order; result.items.push_back(std::move(item)); }
    if (count && !result.valid()) throw std::runtime_error("Overlapping conditional hotkeys");
    return result;
}

bool CustomHotkeySettings::valid() const {
    if (items.size() > 128) return false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!items[i].binding.activation.valid()) return false;
        if (!items[i].binding.gesture) continue;
        for (std::size_t j = 0; j < i; ++j) {
            if (items[i].binding.gesture == items[j].binding.gesture
                && activation_overlaps(items[i].binding.activation, items[j].binding.activation)) return false;
        }
    }
    return true;
}

void CustomHotkeySettings::write(SettingsDocument& document) const {
    CustomHotkeySettings legacy, conditional;
    for (const auto& item : items) {
        if (!item.binding.activation.valid()) throw std::runtime_error("Invalid hotkey activation conditions");
        // Keep dormant program selections too, without exposing them to an old reader.
        (item.binding.activation.conditional() || !item.binding.activation.programs.empty()
            ? conditional : legacy).items.push_back(item);
    }
    write_collection(legacy, document, L"CustomGlobalHotKey");
    write_collection(conditional, document, L"ConditionalGlobalHotKey");
    document.set(L"CustomGlobalHotkeys", L"ConditionalGlobalHotKeyVersion", L"1");
    constexpr std::wstring_view suffixes[]{L"CapsLock", L"NumLock", L"ScrollLock", L"RequireRunning", L"ProgramCount"};
    document.erase_numbered(L"ConditionalGlobalHotKey", suffixes);
    for (unsigned i = 1; i <= 32; ++i) {
        const auto suffix = L"RelatedProgram" + std::to_wstring(i);
        const std::wstring_view view = suffix;
        document.erase_numbered(L"ConditionalGlobalHotKey", std::span(&view, 1));
    }
    unsigned legacy_index = 0, conditional_index = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& activation = items[i].binding.activation;
        const bool guarded = activation.conditional() || !activation.programs.empty();
        const auto prefix = std::wstring(guarded ? L"ConditionalGlobalHotKey" : L"CustomGlobalHotKey")
            + std::to_wstring(guarded ? ++conditional_index : ++legacy_index);
        const auto set = [&](std::wstring_view suffix, std::wstring_view value) {
            document.set(L"CustomGlobalHotkeys", prefix + std::wstring(suffix), value);
        };
        set(L"Order", std::to_wstring(i));
        if (!guarded) continue;
        for (std::size_t lock = 0; lock < 3; ++lock)
            set(lock_suffixes[lock], std::to_wstring(static_cast<unsigned>(activation.locks[lock])));
        set(L"RequireRunning", activation.require_running_program ? L"1" : L"0");
        set(L"ProgramCount", std::to_wstring(activation.programs.size()));
        for (std::size_t program = 0; program < activation.programs.size(); ++program)
            set(L"RelatedProgram" + std::to_wstring(program + 1), activation.programs[program]);
    }
}
} // namespace simpilot
