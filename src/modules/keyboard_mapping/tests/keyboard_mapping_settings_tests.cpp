#include "keyboard_mapping_module.hpp"
#include "keyboard_capture_state.hpp"
#include "keyboard_mapping_editor_model.hpp"
#include "keyboard_mapping_engine.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
bool save_settings(const std::filesystem::path& path, const simpilot::KeyboardMappingSettings& settings) {
    try {
        auto document = simpilot::SettingsDocument::load(path);
        settings.write(document);
        return document.save(path);
    } catch (...) { return false; }
}
simpilot::KeyboardMappingSettings load_settings(
    const std::filesystem::path& path, std::function<void(std::wstring_view)> diagnose = {}) {
    return simpilot::KeyboardMappingSettings::read(simpilot::SettingsDocument::load(path), std::move(diagnose));
}
void app_settings_rejects_malformed_keyboard_mappings() {
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-malformed-mapping-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / L"Setting.ini";

    simpilot::KeyboardMappingSettings malformed;
    simpilot::KeyboardMappingRule invalid;
    invalid.trigger.modifier_count = invalid.trigger.modifiers.size() + 1;
    invalid.trigger.action = {VK_F13, 0x64, false};
    invalid.output.single_key = true;
    invalid.output.action = {VK_F14, 0x65, false};
    malformed.rules = {invalid};
    require(!simpilot::validate_keyboard_mappings(malformed.rules).empty(),
            "Malformed mapping must be rejected by validation");
    require(!save_settings(path, malformed),
            "Settings save must reject malformed mapping data");
    require(!std::filesystem::exists(path),
            "Rejected mapping must not create a settings file");

    simpilot::KeyboardMappingRule canonical;
    canonical.trigger.single_key = true;
    canonical.trigger.action = {VK_F13, 0x64, false};
    canonical.output.single_key = true;
    canonical.output.action = {VK_F14, 0x65, false};
    canonical.process_name = L"Editor";
    malformed.rules = {canonical};
    require(save_settings(path, malformed),
            "Valid mapping with a bare process name must save");
    {
        std::ifstream saved(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(saved)),
                                  std::istreambuf_iterator<char>());
        require(content.find("KeyboardMapping1Process=editor.exe") != std::string::npos,
                "Saved process scope must use a canonical executable basename");
    }
    const auto loaded = load_settings(path);
    require(loaded.rules.size() == 1
                && loaded.rules.front().process_name == L"editor.exe",
            "Canonical process scope must round-trip");

    simpilot::KeyboardMappingRule copilot;
    copilot.trigger.single_key = true;
    copilot.trigger.action = {VK_RCONTROL, 0x1D, true};
    copilot.output.modifier_count = 2;
    copilot.output.modifiers[0] = {VK_LSHIFT, 0x2A, false};
    copilot.output.modifiers[1] = {VK_LWIN, 0x5B, true};
    copilot.output.action = {VK_F23, 0x6E, false};
    malformed.rules = {copilot};
    require(simpilot::validate_keyboard_mappings(
                malformed.rules).empty(),
            "A sided modifier must be valid as a standalone physical source");
    require(save_settings(path, malformed),
            "A Right Ctrl to Copilot mapping must save");
    const auto loaded_copilot = load_settings(path);
    require(loaded_copilot.rules == malformed.rules,
            "A Right Ctrl to Copilot mapping must round-trip unchanged");

    auto generic_modifier = copilot;
    generic_modifier.trigger.action = {VK_CONTROL, 0x1D, false};
    require(!simpilot::validate_keyboard_mappings({generic_modifier}).empty(),
            "A generic Ctrl identity must not be accepted as a physical source");

    const auto malformed_config = root / L"malformed.ini";
    {
        std::ofstream stream(malformed_config, std::ios::binary | std::ios::trunc);
        stream << "[KeyboardMappings]\r\n"
               << "KeyboardMappingCount=1\r\n"
               << "KeyboardMapping1Enabled=1\r\n"
               << "KeyboardMapping1SourceAction=124:100junk:0\r\n"
               << "KeyboardMapping1TargetAction=125:101:0\r\n";
    }
    const auto skipped = load_settings(malformed_config);
    require(skipped.rules.empty(),
            "Malformed mapping tokens must be skipped while loading settings");

    const auto isolated_config = root / L"isolated-invalid-mapping.ini";
    {
        std::ofstream stream(isolated_config, std::ios::binary | std::ios::trunc);
        stream << "[General]\r\nStartWithWindows=1\r\n"
               << "[KeyboardMappings]\r\n"
               << "KeyboardMappingCount=2\r\n"
               << "KeyboardMapping1Enabled=1\r\n"
               << "KeyboardMapping1SourceAction=65:30:0\r\n"
               << "KeyboardMapping1TargetAction=162:29:0\r\n"
               << "KeyboardMapping2Enabled=1\r\n"
               << "KeyboardMapping2SourceAction=65:30:0\r\n"
               << "KeyboardMapping2TargetAction=66:48:0\r\n";
    }
    std::vector<std::wstring> diagnostics;
    const auto isolated = load_settings(
        isolated_config,
        [&diagnostics](const std::wstring_view message) {
            diagnostics.emplace_back(message);
        });
    require(simpilot::SettingsDocument::load(isolated_config).get(L"StartWithWindows") == L"1",
            "An invalid mapping must not affect unrelated settings");
    require(isolated.rules.size() == 1
                && isolated.rules.front().output.action
                    == simpilot::PhysicalKey{L'B', 48, false},
            "A semantic error in one mapping must not reject a later valid mapping");
    require(!diagnostics.empty(),
            "Skipped keyboard mappings must report a diagnostic");
    std::filesystem::remove_all(root);
}

void keyboard_mapping_validation_is_order_insensitive() {
    const simpilot::PhysicalKey left_ctrl{VK_LCONTROL, 29, false};
    const simpilot::PhysicalKey left_shift{VK_LSHIFT, 42, false};
    const simpilot::PhysicalKey action{L'A', 30, false};
    const simpilot::PhysicalKey first_target{L'B', 48, false};
    const simpilot::PhysicalKey second_target{L'C', 46, false};

    simpilot::KeyboardMappingRule first;
    first.trigger.modifier_count = 2;
    first.trigger.modifiers[0] = left_ctrl;
    first.trigger.modifiers[1] = left_shift;
    first.trigger.action = action;
    first.output.single_key = true;
    first.output.action = first_target;
    first.process_name = L"Editor.exe";

    auto reordered = first;
    reordered.trigger.modifiers[0] = left_shift;
    reordered.trigger.modifiers[1] = left_ctrl;
    reordered.output.action = second_target;
    require(!simpilot::validate_keyboard_mappings({first, reordered}).empty(),
            "Reordered modifiers must still be detected as duplicate sources");

    auto disabled = reordered;
    disabled.enabled = false;
    require(!simpilot::validate_keyboard_mappings({first, disabled}).empty(),
            "Disabled duplicate sources must be rejected consistently");

    auto chord = first;
    chord.trigger.chord_action = second_target;
    chord.trigger.modifiers[0] = left_shift;
    chord.trigger.modifiers[1] = left_ctrl;
    require(!simpilot::validate_keyboard_mappings({first, chord}).empty(),
            "Reordered modifiers must not bypass prefix validation");

    simpilot::KeyboardMappingRule cycle_start;
    cycle_start.trigger.single_key = true;
    cycle_start.trigger.action = action;
    cycle_start.output.modifier_count = 2;
    cycle_start.output.modifiers[0] = left_ctrl;
    cycle_start.output.modifiers[1] = left_shift;
    cycle_start.output.action = first_target;

    simpilot::KeyboardMappingRule cycle_end;
    cycle_end.trigger.modifier_count = 2;
    cycle_end.trigger.modifiers[0] = left_shift;
    cycle_end.trigger.modifiers[1] = left_ctrl;
    cycle_end.trigger.action = first_target;
    cycle_end.output.single_key = true;
    cycle_end.output.action = action;
    require(!simpilot::validate_keyboard_mappings({cycle_start, cycle_end}).empty(),
            "Reordered modifier sets must still detect mapping cycles");

    auto exact_source = first;
    exact_source.process_name = L"editor.exe";
    exact_source.exact_match = true;
    auto prefix_source = reordered;
    prefix_source.process_name = L"editor.exe";
    prefix_source.exact_match = false;
    require(simpilot::validate_keyboard_mappings(
                {exact_source, prefix_source}).empty(),
            "Exact and prefix process scopes must be distinct rule scopes");

    auto disjoint_chord = chord;
    disjoint_chord.process_name = L"browser.exe";
    require(simpilot::validate_keyboard_mappings(
                {first, disjoint_chord}).empty(),
            "Disjoint exact process scopes must not create a prefix conflict");

    cycle_start.process_name = L"editor.exe";
    cycle_end.process_name = L"browser.exe";
    require(simpilot::validate_keyboard_mappings(
                {cycle_start, cycle_end}).empty(),
            "Disjoint exact process scopes must not create a mapping cycle");
}

void recorded_num_lock_survives_save_reload_and_execution() {
    simpilot::KeyboardCaptureState capture;
    capture.begin(simpilot::CaptureMode::mapping_output);
    const simpilot::PhysicalKey num_lock{VK_NUMLOCK, 0x45, true};
    require(capture.handle(WM_KEYDOWN, num_lock).suppress,
            "Recording Num Lock must suppress the physical press");
    const auto release = capture.handle(WM_KEYUP, num_lock);
    require(release.suppress && release.completed,
            "Releasing Num Lock must complete target recording");
    const auto completion = capture.take_completed();
    require(completion && completion->output.single_key
                && completion->output.action == num_lock,
            "Recording must preserve the Num Lock physical identity");

    simpilot::KeyboardMappingEditorModel editor;
    editor.set_source_action(simpilot::PhysicalKey{VK_F24, 0x76, false});
    editor.set_output(completion->output);
    const auto draft = editor.build();
    require(static_cast<bool>(draft), "Recorded Num Lock must form a valid draft");
    simpilot::KeyboardMappingRule rule;
    rule.trigger = draft.trigger;
    rule.output = draft.output;
    simpilot::KeyboardMappingSettings settings;
    settings.rules = {rule};
    const auto path = std::filesystem::temp_directory_path()
        / (L"simpilot-num-lock-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    require(save_settings(path, settings), "Recorded Num Lock mapping must save");
    const auto loaded = load_settings(path);
    std::filesystem::remove(path);
    require(loaded.rules == settings.rules,
            "Num Lock mapping must reload with unchanged physical key data");

    std::vector<INPUT> injected;
    simpilot::KeyboardMappingEngine engine(
        [&injected](const INPUT* inputs, const UINT count) {
            injected.insert(injected.end(), inputs, inputs + count);
            return count;
        });
    require(engine.replace_rules(loaded.enabled, loaded.rules),
            "Saved Num Lock mapping must activate");
    const KBDLLHOOKSTRUCT source{.vkCode = VK_F24, .scanCode = 0x76};
    require(engine.handle(WM_KEYDOWN, source).decision
                == simpilot::MappingEventDecision::suppress
                && engine.handle(WM_KEYUP, source).decision
                    == simpilot::MappingEventDecision::suppress,
            "Reloaded Num Lock mapping must consume its source press and release");
    require(injected.size() == 2
                && injected.front().ki.wVk == VK_NUMLOCK
                && injected.front().ki.dwFlags == KEYEVENTF_EXTENDEDKEY
                && injected.back().ki.wVk == VK_NUMLOCK
                && injected.back().ki.dwFlags == (KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP),
            "Recorded and reloaded Num Lock must emit usable virtual-key input");
}

} // namespace
int main() {
    try {
        app_settings_rejects_malformed_keyboard_mappings();
        keyboard_mapping_validation_is_order_insensitive();
        recorded_num_lock_survives_save_reload_and_execution();
        std::cout << "Keyboard mapping configuration tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
