#include "windows_hotkey_blocker_module.hpp"
#include "settings_page_test_support.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        auto document = simpilot::SettingsDocument::parse(
            "; preserve\r\n[Other]\r\nUnknown=retained\r\nDisabledWindowsHotkeys=aFG lz\r\n");
        auto settings = simpilot::WindowsHotkeyBlockingSettings::read(document);
        require(settings.disabled[0] && settings.disabled[5] && settings.disabled[6]
            && settings.disabled[25] && !settings.disabled[11], "Read legacy policy; never block Win+L");
        settings.disabled[11] = true;
        settings.write(document);
        require(document.get(L"DisabledWindowsHotkeys") == L"AFGZ", "Write canonical supported letters");
        require(document.get(L"Unknown") == L"retained", "Keep unknown configuration");
        require(!simpilot::WindowsHotkeyBlockingSettings::read(document).disabled[11],
            "Round-trip rejects Win+L");

        simpilot::SettingsRegistry pages;
        simpilot::SettingsParticipantRegistry participants;
        simpilot::HotkeyRegistry hotkeys;
        std::array<bool, 26> runtime{};
        auto module = simpilot::make_windows_hotkey_blocker_module(
            document, pages, participants, hotkeys,
            [&](const auto& policy) { runtime = policy; return true; });
        module->start();
        module->start();
        require(pages.size() == 1 && participants.size() == 1 && runtime[0],
            "Idempotent startup contributes page and policy");
        simpilot::SettingsSession session(participants, document);
        simpilot::BuiltInHotKey draft{{simpilot::HotKeyGesture{MOD_WIN, L'A'}, true}, true};
        auto source = hotkeys.add_editable_drafts("test", [&] {
            return std::vector<simpilot::HotkeyDraftBinding>{{"test", "test",
                [&] { return draft; }, [&](auto value) { draft = value; }, {}}};
        });
        require(hotkeys.draft_requires_windows_blocking(0)
            && !hotkeys.requires_windows_blocking(0), "Draft linkage does not alter runtime");
        draft.enabled = false;
        require(!hotkeys.draft_requires_windows_blocking(0), "Disabled drafts do not link");
        draft.enabled = true;
        draft.binding.gesture = simpilot::HotKeyGesture{MOD_WIN, L'L'};
        require(!hotkeys.draft_requires_windows_blocking(11), "Win+L never links");

        settings_page_test::Host host;
        const auto parent = host.window;
        simpilot::Localization localization(simpilot::UiLanguage::english);
        std::unique_ptr<simpilot::ISettingsPage> page;
        pages.visit([&](const auto&, const auto& contribution) { page = contribution.create(); });
        int changes = 0;
        page->create({GetModuleHandleW(nullptr), parent, 96,
            static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), localization, [&] { ++changes; }});
        page->layout({0, 0, 800, 600}, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        page->show(true);
        const auto child = FindWindowExW(parent, nullptr, L"Simpilot.WindowsHotkeyBlockingPage", nullptr);
        require(child != nullptr && !GetDlgItem(child, 111), "No Win+L control is created");
        for (UINT dpi : {96U, 144U, 192U}) {
            for (int width : {650, 900}) {
                page->layout({0, 0, MulDiv(width, dpi, 96), MulDiv(520, dpi, 96)}, dpi,
                    static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
                for (int index = 0; index < 26; ++index) {
                    if (index == 11) continue;
                    SendMessageW(child, WM_COMMAND, MAKEWPARAM(100 + index, BN_SETFOCUS),
                        reinterpret_cast<LPARAM>(GetDlgItem(child, 100 + index)));
                    const auto bounds = settings_page_test::visible_bounds(child, GetDlgItem(child, 100 + index));
                    require(bounds.bottom - bounds.top == MulDiv(32, dpi, 96),
                        "Blocking controls retain the full switch height");
                }
                SendMessageW(child, WM_VSCROLL, SB_TOP, 0);
                settings_page_test::uniform_background(child);
            }
            page->layout({0, 0, MulDiv(650, dpi, 96), MulDiv(320, dpi, 96)}, dpi,
                static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
            SendMessageW(child, WM_COMMAND, MAKEWPARAM(125, BN_SETFOCUS),
                reinterpret_cast<LPARAM>(GetDlgItem(child, 125)));
            (void)settings_page_test::visible_bounds(child, GetDlgItem(child, 125));
            require(GetScrollPos(child, SB_VERT) > 0,
                "Keyboard focus reveals lower rows without shrinking switches");
            SendMessageW(child, WM_COMMAND, MAKEWPARAM(100, BN_SETFOCUS),
                reinterpret_cast<LPARAM>(GetDlgItem(child, 100)));
            (void)settings_page_test::visible_bounds(child, GetDlgItem(child, 100));
            SendMessageW(child, WM_VSCROLL, SB_TOP, 0);
        }
        const auto toggle = GetDlgItem(child, 100);
        SendMessageW(toggle, BM_CLICK, 0, 0);
        require(SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "Sliding switch toggles on click");
        require(changes == 1 && session.dirty() && runtime[0], "UI only changes draft");
        require(!session.apply([](const auto&) { return false; }) && runtime[0],
            "Failed INI commit restores runtime");
        require(session.dirty(), "Failed save retains editable draft");
        require(session.apply([&](const auto& candidate) {
            document = candidate;
            return true;
        }) && !runtime[0], "Successful apply commits policy");
        require(document.get(L"DisabledWindowsHotkeys") == L"FGZ", "Draft uses original key");
        draft.binding.gesture = simpilot::HotKeyGesture{MOD_WIN, L'F'};
        page->show(true);
        require(!IsWindowEnabled(GetDlgItem(child, 105)), "Linked draft disables manual toggle");
        source.reset();
        page->show(true);
        require(IsWindowEnabled(GetDlgItem(child, 105)) != FALSE, "Revocation restores manual toggle");
        page->layout({0, 0, 1200, 900}, 144, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        localization.set_language(simpilot::UiLanguage::traditional_chinese);
        page->refresh_language(localization);
        page.reset();
        session.cancel();
        module->stop();
        module->stop();
        require(pages.size() == 0 && participants.size() == 0 && runtime == std::array<bool, 26>{},
            "Stop revokes contributions and user policy");
        std::cout << "Windows shortcut policy module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
