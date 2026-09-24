#include "mouse_locator_module.hpp"
#include "mouse_shake_detector.hpp"
#include "simpilot/app_settings.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void detector_tests() {
    using Detector = simpilot::MouseShakeDetector;
    const auto start = Detector::Clock::time_point{};
    const auto at = [start](const int milliseconds) {
        return start + std::chrono::milliseconds(milliseconds);
    };
    Detector rapid;
    require(!rapid.update(0, 0, at(0)), "Initialize mouse shake detector");
    require(!rapid.update(70, 0, at(50)), "First fast movement does not trigger");
    require(!rapid.update(-70, 0, at(100)), "First reversal does not trigger");
    require(!rapid.update(70, 0, at(150)), "Second reversal does not trigger");
    require(!rapid.update(-70, 0, at(200)), "Third reversal does not trigger");
    require(rapid.update(70, 0, at(250)), "Fast reversals trigger highlighting");
    require(!rapid.update(-70, 0, at(300)) && !rapid.update(70, 0, at(350))
            && !rapid.update(-70, 0, at(400)) && !rapid.update(70, 0, at(450)),
            "Cooldown prevents repeated highlighting");
    Detector one_direction;
    require(!one_direction.update(0, 0, at(0)), "Initialize one-direction sample");
    for (int index = 1; index <= 8; ++index) {
        require(!one_direction.update(index * 50, 0, at(index * 50)),
                "One-direction movement never counts as a shake");
    }
    Detector slow;
    require(!slow.update(0, 0, at(0)), "Initialize slow sample");
    for (int index = 1; index <= 6; ++index) {
        require(!slow.update(index % 2 == 0 ? -70 : 70, 0, at(index * 300)),
                "Slow reversals reset");
    }
    Detector jitter;
    require(!jitter.update(0, 0, at(0)), "Initialize small movement sample");
    for (int index = 1; index <= 20; ++index) {
        require(!jitter.update(index % 2 == 0 ? 0 : 3, 0, at(index * 20)),
                "Small jitter does not trigger");
    }
}

void settings_tests() {
    for (auto language : {simpilot::UiLanguage::english,
                          simpilot::UiLanguage::simplified_chinese,
                          simpilot::UiLanguage::traditional_chinese}) {
        const simpilot::Localization catalog(language);
        for (auto key : {"settings.section.cursor_locator", "settings.cursor_locator",
                         "settings.cursor_locator.description", "ui.cursor_locator_update_failed"}) {
            require(catalog.text(key) != L"[missing translation]", "Module translations present");
        }
    }
    auto document = simpilot::SettingsDocument::parse(
        "[General]\nMouseShakeLocatorEnabled=YES\n[Future]\nKeep=42\n");
    require(simpilot::MouseLocatorSettings::read(document).enabled, "Legacy boolean decoding");
    require(!simpilot::MouseLocatorSettings::read({}).enabled, "Default disabled");
    simpilot::MouseLocatorSettings{false}.write(document);
    require(document.get(L"MouseShakeLocatorEnabled") == L"0", "Legacy key name preserved");
    require(document.get(L"Keep") == L"42", "Unknown module settings retained");

    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-mouse-settings-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = root / L"Setting.ini";
    std::filesystem::create_directories(root);
    require(document.save(path), "Save module setting");
    require(simpilot::AppSettingsStore::save(path, {}), "Save legacy settings independently");
    require(!simpilot::MouseLocatorSettings::read(simpilot::SettingsDocument::load(path)).enabled,
            "Unrelated save preserves module-owned settings");
    std::filesystem::remove_all(root);
}

void module_tests() {
    simpilot::SettingsRegistry pages;
    simpilot::SettingsParticipantRegistry participants;
    auto module = simpilot::make_mouse_locator_module(
        GetModuleHandleW(nullptr), {}, pages, participants, {});
    module->start();
    module->start();
    require(pages.size() == 1 && participants.size() == 1, "Page and settings contributions");
    simpilot::SettingsSession session(participants, {});
    require(!session.dirty(), "Fresh module session is clean");
    require(session.apply([](const auto& document) {
        return document.get(L"MouseShakeLocatorEnabled") == L"0";
    }), "Module writes its own settings");
    session.cancel();
    module->stop();
    module->stop();
    require(pages.size() == 0 && participants.size() == 0, "Module stop revokes contributions");
}
}

int main() {
    try {
        detector_tests();
        settings_tests();
        module_tests();
        std::cout << "Mouse locator module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
