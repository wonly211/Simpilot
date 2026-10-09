#include "keyboard_mapping_module.hpp"
#include "keyboard_mapping_engine.hpp"
#include "simpilot/text_encoding.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace simpilot;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

KeyboardMappingRule mapping(UINT source = VK_F13, UINT target = VK_F14,
    std::vector<std::wstring> applications = {}) {
    KeyboardMappingRule result;
    result.trigger.single_key = true;
    result.trigger.action = {source, source - VK_F1 + 0x3B, false};
    result.output.single_key = true;
    result.output.action = {target, target - VK_F1 + 0x3B, false};
    result.process_names = std::move(applications);
    return result;
}

KBDLLHOOKSTRUCT event(PhysicalKey key) {
    return {.vkCode = key.virtual_key, .scanCode = key.scan_code,
        .flags = key.extended ? DWORD{LLKHF_EXTENDED} : DWORD{0}};
}

SettingsDocument document(std::string scope) {
    return SettingsDocument::parse("[KeyboardMappings]\nKeyboardMappingCount=1\n"
        "KeyboardMapping1Enabled=1\nKeyboardMapping1SourceAction=124:100:0\n"
        "KeyboardMapping1TargetAction=125:101:0\n" + scope);
}

void normalization_and_limits() {
    const auto names = normalize_mapping_process_names({L" Editor ", L"EDITOR.EXE", L"Notepad"});
    require(names && *names == std::vector<std::wstring>{L"editor.exe", L"notepad.exe"},
        "Names must normalize, deduplicate and sort independently of input order");
    require(normalize_mapping_process_names({})->empty(), "An explicit global scope stays global");
    for (const auto& name : {L"", L"  ", L"C:\\editor.exe", L"app?.exe", L"a\nb.exe", L":multi-app:"}) {
        require(!normalize_mapping_process_names({L"editor.exe", name}),
            "A bad item must invalidate the list rather than be dropped or broaden the scope");
    }
    auto valid = mapping();
    for (std::size_t i = 0; i < maximum_mapping_applications; ++i) {
        valid.process_names.push_back(L"app" + std::to_wstring(i));
    }
    require(validate_keyboard_mappings({valid}).empty(), "32 applications must be accepted");
    valid.process_names.push_back(L"overflow");
    require(!validate_keyboard_mappings({valid}).empty(), "33 applications must be rejected");
}

void configuration_migration_and_fail_closed_loading() {
    for (const auto& legacy : {std::string{}, std::string("KeyboardMapping1Process=\n"),
                               std::string("KeyboardMapping1Process= Editor \n")}) {
        auto ini = document(legacy);
        const auto loaded = KeyboardMappingSettings::read(ini);
        require(loaded.rules.size() == 1, "Existing global and single-app rules must load");
        require(loaded.rules[0].process_names.empty() == (legacy.find("Editor") == std::string::npos),
            "Legacy global/specific meaning must be preserved");
        loaded.write(ini);
        require(KeyboardMappingSettings::read(ini) == loaded, "Migrated rules must round-trip");
    }

    auto ini = document("KeyboardMapping1FutureOption=keep\nKeyboardMapping1Process32=stale.exe\n");
    KeyboardMappingSettings settings;
    settings.rules = {mapping(VK_F13, VK_F14, {L"Editor", L"NOTEPAD.exe", L"editor.exe"})};
    settings.write(ini);
    const auto loaded = KeyboardMappingSettings::read(ini);
    require(loaded.rules.size() == 1
        && loaded.rules[0].process_names == std::vector<std::wstring>{L"editor.exe", L"notepad.exe"},
        "Multi-app rules must preserve every distinct app");
    require(ini.get(L"KeyboardMapping1ProcessCount") == L"2"
        && ini.get(L"KeyboardMapping1Process") == L":multi-app:",
        "Multi-app rules must carry the guard that the legacy reader rejects");
    require(!normalize_mapping_process_name(*ini.get(L"KeyboardMapping1Process")).size(),
        "Legacy basename parsing must reject the downgrade guard");
    require(!ini.get(L"KeyboardMapping1Process32")
        && ini.get(L"KeyboardMapping1FutureOption") == L"keep", "Clean owned fields only");
    for (const auto legacy_scope : {L"", L"other.exe"}) {
        auto stale = ini;
        stale.set(L"KeyboardMappings", L"KeyboardMapping1Process", legacy_scope);
        require(KeyboardMappingSettings::read(stale).rules.empty(),
            "Stale list metadata after an old-version save must never be attached to a different rule");
    }
    auto count_missing = ini;
    count_missing.erase(L"KeyboardMapping1ProcessCount");
    require(KeyboardMappingSettings::read(count_missing).rules.empty(),
        "Losing list metadata must never turn a multi-app rule global");

    for (const auto count : {"", "-1", "2junk", "33", "4294967296", "999999999999999999"}) {
        auto malformed = ini;
        malformed.set(L"KeyboardMappings", L"KeyboardMapping1ProcessCount", decode_utf8(count).value());
        std::vector<std::wstring> diagnostics;
        require(KeyboardMappingSettings::read(malformed, [&](auto message) {
            diagnostics.emplace_back(message);
        }).rules.empty() && !diagnostics.empty(), "Invalid counts must fail closed with diagnostics");
    }
    for (const auto bad : {L"", L"  ", L"C:\\Other.exe"}) {
        auto malformed = ini;
        malformed.set(L"KeyboardMappings", L"KeyboardMapping1Process2", bad);
        require(KeyboardMappingSettings::read(malformed).rules.empty(),
            "Missing/invalid applications must not partially load a rule");
    }
    auto missing = ini;
    missing.erase(L"KeyboardMapping1Process2");
    require(KeyboardMappingSettings::read(missing).rules.empty(), "Missing list entries must reject the rule");
    missing = ini;
    missing.set(L"KeyboardMappings", L"KeyboardMapping1ProcessCount", L"0");
    require(KeyboardMappingSettings::read(missing).rules.empty(), "A corrupt count must not bypass the guard");

    settings.rules[0].process_names = {L"Editor"};
    settings.write(ini);
    require(ini.get(L"KeyboardMapping1Process") == L"editor.exe"
        && !ini.get(L"KeyboardMapping1Process2") && !ini.get(L"KeyboardMapping1ProcessCount"),
        "Single-app rules keep only legacy fields, without stale parallel metadata");
    settings.rules[0].process_names.clear();
    settings.write(ini);
    require(ini.get(L"KeyboardMapping1Process") == L""
        && !ini.get(L"KeyboardMapping1Process1")
        && KeyboardMappingSettings::read(ini).rules[0].process_names.empty(), "Explicit global round-trip");
    const auto before = ini.serialize();
    settings.rules[0].process_names = {L""};
    bool rejected = false;
    try { settings.write(ini); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && ini.serialize() == before, "Reject bad settings without modifying the document");
}

void intersections_and_cycles() {
    auto first = mapping(VK_F13, VK_F14, {L"A", L"B"});
    auto second = mapping(VK_F13, VK_F15, {L"B", L"C"});
    for (auto rules : {std::vector{first, second}, std::vector{second, first}}) {
        const auto errors = validate_keyboard_mappings(rules);
        require(!errors.empty() && errors[0].kind == KeyboardMappingValidationKind::duplicate_source
            && errors[0].process == L"b.exe", "Detect the shared application, independent of rule order");
        rules[1].enabled = false;
        require(!validate_keyboard_mappings(rules).empty(), "Disabled overlapping duplicates stay invalid");
    }
    second.process_names = {L"C", L"D"};
    require(validate_keyboard_mappings({first, second}).empty(), "Disjoint app groups may reuse a source");

    auto a = mapping(VK_F13, VK_F14, {L"A", L"B"});
    auto b = mapping(VK_F14, VK_F15, {L"B", L"C"});
    auto c = mapping(VK_F15, VK_F13, {L"A", L"C"});
    require(validate_keyboard_mappings({a, b, c}).empty()
        && validate_keyboard_mappings({c, b, a}).empty(), "Pairwise overlap is not a cycle in one application");
    c.process_names.push_back(L"B");
    const auto errors = validate_keyboard_mappings({a, b, c});
    require(!errors.empty() && errors[0].kind == KeyboardMappingValidationKind::cycle
        && errors[0].process == L"b.exe", "A cycle in a shared application must be rejected");
    a.process_names = {L"app", L"other"};
    a.exact_match = false;
    b.process_names = {L"application"};
    b.exact_match = false;
    c.process_names = {L"application-host"};
    require(!validate_keyboard_mappings({a, b, c}).empty(), "Mixed exact/prefix cycles must be rejected");
    c.exact_match = false;
    require(!validate_keyboard_mappings({a, b, c}).empty(), "Prefix-only cycles must be rejected");
    c.process_names = {L"unrelated"};
    require(validate_keyboard_mappings({a, b, c}).empty(), "Disjoint prefix branches must remain independent");

    auto modifier = mapping(VK_F13, VK_F14, {L"A", L"B"});
    modifier.trigger.action = {VK_LWIN, 0x5B, true};
    modifier.output.action = {VK_RCONTROL, 0x1D, true};
    auto shortcut = mapping(VK_F13, VK_F15, {L"B", L"C"});
    shortcut.trigger.single_key = false;
    shortcut.trigger.modifier_count = 1;
    shortcut.trigger.modifiers[0] = modifier.trigger.action;
    require(!validate_keyboard_mappings({modifier, shortcut}).empty(), "Shared modifier conflicts must be detected");
    shortcut.process_names = {L"C"};
    require(validate_keyboard_mappings({modifier, shortcut}).empty(), "Disjoint modifier uses remain valid");
}

void matching_priority_and_key_lifetimes() {
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine([&](const INPUT* inputs, UINT count) {
        injected.insert(injected.end(), inputs, inputs + count);
        return count;
    });
    auto global = mapping(VK_F13, VK_F14);
    auto prefix = mapping(VK_F13, VK_F15, {L"app", L"unrelated-long-name"});
    prefix.exact_match = false;
    auto longer = mapping(VK_F13, VK_F16, {L"application", L"editor"});
    longer.exact_match = false;
    auto exact = mapping(VK_F13, VK_F17, {L"application", L"notepad"});
    for (const auto& rules : {std::vector{global, prefix, longer, exact}, std::vector{exact, longer, prefix, global}}) {
        require(engine.replace_rules(true, rules), "Multi-app rules must compile with existing priorities");
        for (const auto& [process, scan] : std::vector<std::pair<std::wstring, UINT>>{
                 {L"APPLICATION.exe", exact.output.action.scan_code},
                 {L"notepad.exe", exact.output.action.scan_code},
                 {L"application-host.exe", longer.output.action.scan_code},
                 {L"editor-host.exe", longer.output.action.scan_code},
                 {L"app-host.exe", prefix.output.action.scan_code},
                 {L"other.exe", global.output.action.scan_code}, {L"", global.output.action.scan_code}}) {
            injected.clear();
            engine.set_foreground_process(process);
            const auto source = event(global.trigger.action);
            require(engine.handle(WM_KEYDOWN, source).decision == MappingEventDecision::suppress,
                "Matching app must consume the source");
            (void)engine.handle(WM_KEYUP, source);
            require(injected.size() == 2 && injected[0].ki.wScan == scan
                && injected[1].ki.wScan == scan && (injected[1].ki.dwFlags & KEYEVENTF_KEYUP),
                "Priority uses the matched list entry and balances down/up");
        }
    }

    for (const bool num_lock : {false, true}) {
        auto rule = mapping(VK_F24, VK_F14, {L"Editor", L"Notepad"});
        rule.trigger.action = num_lock ? PhysicalKey{VK_F24, 0x76, false} : PhysicalKey{VK_LWIN, 0x5B, true};
        rule.output.action = num_lock ? PhysicalKey{VK_NUMLOCK, 0x45, true} : PhysicalKey{VK_RCONTROL, 0x1D, true};
        require(engine.replace_rules(true, {rule}), "Num Lock and sided modifier app groups must compile");
        for (const auto app : {L"editor.exe", L"notepad.exe"}) {
            injected.clear();
            engine.set_foreground_process(app);
            const auto source = event(rule.trigger.action);
            require(engine.handle(WM_KEYDOWN, source).decision == MappingEventDecision::suppress,
                "Each associated app must enable the rule");
            engine.set_foreground_process(L"unrelated.exe");
            (void)engine.handle(WM_KEYUP, source);
            require(injected.size() == 2 && injected[0].ki.wScan == rule.output.action.scan_code
                && injected[1].ki.wScan == rule.output.action.scan_code
                && (injected[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY)
                && (injected[1].ki.dwFlags & KEYEVENTF_KEYUP),
                "Switching away while held must release the original extended target");
            injected.clear();
            require(engine.handle(WM_KEYDOWN, source).decision == MappingEventDecision::pass,
                "An unrelated app must not enable the rule");
            (void)engine.handle(WM_KEYUP, source);
            require(injected.empty(), "Unrelated app must receive no injected output");
        }
        injected.clear();
        engine.set_foreground_process(L"editor.exe");
        (void)engine.handle(WM_KEYDOWN, event(rule.trigger.action));
        require(engine.replace_rules(false, {}), "Disabling app groups must succeed");
        require(injected.size() == 2 && (injected.back().ki.dwFlags & KEYEVENTF_KEYUP),
            "Replacing rules while held must release the target");
    }
}

void user_rule_limit_does_not_count_expanded_apps() {
    std::vector<KeyboardMappingRule> rules;
    for (std::size_t i = 0; i < maximum_keyboard_mappings; ++i) {
        auto rule = mapping();
        for (std::size_t app = 0; app < maximum_mapping_applications; ++app) {
            rule.process_names.push_back(L"app" + std::to_wstring(i) + L"-" + std::to_wstring(app));
        }
        rules.push_back(std::move(rule));
    }
    std::vector<INPUT> injected;
    KeyboardMappingEngine engine([&](const INPUT* inputs, UINT count) {
        injected.insert(injected.end(), inputs, inputs + count); return count;
    });
    require(engine.replace_rules(true, rules), "128 rules with 32 apps each must compile without truncation");
    engine.set_foreground_process(L"app127-31.exe");
    (void)engine.handle(WM_KEYDOWN, event(rules.back().trigger.action));
    (void)engine.handle(WM_KEYUP, event(rules.back().trigger.action));
    require(injected.size() == 2, "The last expanded application must remain functional");
    rules.push_back(mapping());
    require(!engine.replace_rules(true, rules), "129 user rules must be rejected");
}
}

int main() {
    try {
        normalization_and_limits();
        configuration_migration_and_fail_closed_loading();
        intersections_and_cycles();
        matching_priority_and_key_lifetimes();
        user_rule_limit_does_not_count_expanded_apps();
        std::cout << "Keyboard mapping application tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
