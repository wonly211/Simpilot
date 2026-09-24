#include "keyboard_mapping_module.hpp"
#include "keyboard_manager.hpp"

#include <commctrl.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        auto document = simpilot::SettingsDocument::parse(
            "; retained\n[KeyboardMappings]\nKeyboardMappingsEnabled=0\n"
            "KeyboardMappingCount=1\nKeyboardMapping1Enabled=1\n"
            "KeyboardMapping1SourceAction=124:100:0\nKeyboardMapping1TargetAction=125:101:0\n"
            "KeyboardMapping1ExactMatch=0\nKeyboardMapping1FutureOption=preserved\n"
            "KeyboardMapping2Enabled=1\nUnrelated=kept\n");
        auto settings = simpilot::KeyboardMappingSettings::read(document);
        require(!settings.enabled && settings.rules.size() == 1 && !settings.rules[0].exact_match,
            "Read disabled state and non-exact process matching");
        settings.write(document);
        require(!document.get(L"KeyboardMapping2Enabled")
            && document.get(L"KeyboardMapping1FutureOption") == L"preserved",
            "Only explicitly owned numbered fields are cleaned");
        simpilot::SettingsRegistry pages;
        simpilot::SettingsParticipantRegistry participants;
        simpilot::KeyboardManager keyboard;
        auto module = simpilot::make_keyboard_mapping_module(
            document, pages, participants, keyboard, {});
        module->start();
        module->start();
        require(pages.size() == 1 && participants.size() == 1, "Idempotent module startup");
        simpilot::SettingsSession session(participants, document);
        HWND parent = CreateWindowW(L"STATIC", L"", WS_OVERLAPPEDWINDOW,
            0, 0, 1000, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Create page parent");
        simpilot::Localization localization(simpilot::UiLanguage::english);
        std::unique_ptr<simpilot::ISettingsPage> page;
        pages.visit([&](const auto&, const auto& contribution) { page = contribution.create(); });
        int changes = 0;
        page->create({GetModuleHandleW(nullptr), parent, 96,
            static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), localization, [&] { ++changes; }});
        page->layout({0, 0, 850, 650}, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        page->show(true);
        auto window = FindWindowExW(parent, nullptr, L"Simpilot.KeyboardMappingSettingsPage", nullptr);
        require(window != nullptr, "Create module-owned page");
        require(ListView_GetItemCount(GetDlgItem(window, 600)) == 1, "Display existing rules");
        const auto toggle = GetDlgItem(window, 604);
        SendMessageW(toggle, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(604, BN_CLICKED), reinterpret_cast<LPARAM>(toggle));
        require(changes == 1 && session.dirty(), "Enable switch changes module draft");
        require(!session.apply([](const auto&) { return false; }) && session.dirty(),
            "Failed commit restores stage and retains draft");
        require(session.apply([&](const auto& candidate) { document = candidate; return true; }),
            "Commit prepared stage and original INI keys");
        require(document.get(L"KeyboardMappingsEnabled") == L"1" && !session.dirty(),
            "Successful save updates applied baseline");
        localization.set_language(simpilot::UiLanguage::traditional_chinese);
        page->refresh_language(localization);
        page->layout({0, 0, 1200, 900}, 144, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        page->show(false);
        page.reset();
        DestroyWindow(parent);
        session.cancel();
        module->stop();
        module->stop();
        require(pages.size() == 0 && participants.size() == 0, "Stop removes settings contributions");
        std::cout << "Keyboard mapping module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
