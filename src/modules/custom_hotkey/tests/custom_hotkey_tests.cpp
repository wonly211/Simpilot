#include "custom_hotkey_module.hpp"
#include "custom_hotkey_settings.hpp"
#include "hotkey_settings_page.hpp"
#include "keyboard_manager.hpp"
#include <commctrl.h>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void codec_tests() {
    simpilot::CustomHotkeySettings settings;
#include "legacy_fixture.inc"
    auto document = simpilot::SettingsDocument::parse(
        "; retain\r\n[Other]\r\nUnknown=42\r\nCustomGlobalHotKey12Future=keep\r\n");
    settings.write(document);
    const auto content = document.serialize();
    require(content.find("CustomGlobalHotKey1Action=0") != std::string::npos
        && content.find("CustomGlobalHotKey2Action=2") != std::string::npos
        && content.find("CustomGlobalHotKey3Action=1") != std::string::npos, "Legacy action values");
    auto expected = settings;
    expected.items.pop_back();
    expected.items[0].binding.force_override = true;
    require(simpilot::CustomHotkeySettings::read(document) == expected,
        "Unicode actions, disabled state, process behavior and Win+letter compatibility");
    settings.items.resize(1);
    settings.write(document);
    require(!document.get(L"CustomGlobalHotKey2Program"), "Shortened collection removes owned old keys");
    require(document.get(L"CustomGlobalHotKey12Future") == L"keep" && document.get(L"Unknown") == L"42",
        "Do not remove unowned numbered fields or unrelated settings");
    document.set(L"General", L"Language", L"en-US");
    require(simpilot::CustomHotkeySettings::read(document).items.size() == 1,
        "Other modules can save while custom hotkeys are absent");
    const auto malformed = simpilot::SettingsDocument::parse(
        "[WrongSection]\ncustomglobalhotkeycount=1\nCUSTOMGLOBALHOTKEY1CODE=3,88\n"
        "CustomGlobalHotKey1Program=C:\\Apps\\Incomplete.exe\n");
    require(simpilot::CustomHotkeySettings::read(malformed).items.empty(), "Require explicit Enabled");
    auto legacy = malformed;
    legacy.set(L"Ignored", L"CustomGlobalHotKey1Enabled", L"0");
    require(simpilot::CustomHotkeySettings::read(legacy).items.size() == 1,
        "Sectionless case-insensitive legacy lookup");
}
void module_tests() {
    const auto document = simpilot::SettingsDocument::parse(
        "[CustomGlobalHotkeys]\nCustomGlobalHotKeyCount=1\nCustomGlobalHotKey1Code=3,88\n"
        "CustomGlobalHotKey1Program=C:\\Windows\\notepad.exe\nCustomGlobalHotKey1Enabled=1\n");
    simpilot::SettingsParticipantRegistry participants;
    simpilot::HotkeyRegistry hotkeys;
    simpilot::KeyboardManager keyboard;
    auto module = simpilot::make_custom_hotkey_module(document, L".", participants,
        hotkeys, keyboard, {}, {});
    module->start();
    module->start();
    require(hotkeys.sections.size() == 1 && participants.size() == 1, "Idempotent start");
    simpilot::SettingsSession session(participants, document);
    auto drafts = hotkeys.editable_drafts();
    require(drafts.size() == 1 && !drafts[0].common_row, "Custom section owns its variable-length rows");
    auto next = drafts[0].read();
    next.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    drafts[0].write(next);
    require(session.dirty(), "Draft edit changes participant dirty state");
    const auto runtime_key = [&] {
        UINT key = 0;
        hotkeys.refresh([&](int, const auto& binding) { key = binding.gesture->virtual_key; return true; });
        return key;
    };
    require(runtime_key() == L'X', "Draft edit does not mutate runtime");
    require(!session.apply([](const auto&) { return false; }), "Injected INI failure");
    require(runtime_key() == L'X' && session.dirty(), "Rollback restores runtime and retains draft");
    require(session.apply([](const auto& candidate) {
        return candidate.get(L"CustomGlobalHotKey1Code") == L"2,135";
    }), "Successful transaction writes legacy key");
    require(runtime_key() == VK_F24 && !session.dirty(), "Apply publishes new runtime");

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const auto instance = GetModuleHandleW(nullptr);
    const auto parent = CreateWindowW(L"STATIC", L"test", WS_OVERLAPPEDWINDOW,
        0, 0, 900, 700, nullptr, nullptr, instance, nullptr);
    require(parent != nullptr, "Create isolated settings host");
    const simpilot::Localization english("en-US");
    auto page = simpilot::make_hotkey_settings_page(hotkeys, keyboard, {}, {});
    int changes = 0;
    page->create({instance, parent, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        english, [&] { ++changes; }});
    page->layout({0, 0, 700, 500}, 144, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    page->show(true);
    require(FindWindowExW(FindWindowExW(parent, nullptr, L"Simpilot.HotkeySettingsPage", nullptr),
        nullptr, L"Simpilot.CustomHotkeySection", nullptr) != nullptr, "Common page hosts module section");
    for (const auto locale : {"en-US", "zh-CN", "zh-TW"}) {
        const simpilot::Localization localization(locale);
        require(localization.text("custom_hotkey.invalid_target") != L"[missing translation]",
            "Module language resources are linked");
        page->refresh_language(localization);
    }
    page->refresh_language(english);
    page.reset();
    DestroyWindow(parent);
    session.cancel();
    module->stop();
    module->stop();
    require(!drafts[0].active() && hotkeys.sections.size() == 0 && participants.size() == 0,
        "Stop revokes draft and section callbacks");
    require(runtime_key() == 0, "Stop revokes all runtime bindings");
}
}
int main() {
    try {
        codec_tests();
        module_tests();
        std::cout << "Custom hotkey module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
