#include "everything.hpp"
#include "everything_module.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/program_search_registry.hpp"
#include "simpilot/tray_menu_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"
#include "settings_page_test_support.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void task_row_tests(simpilot::SettingsRegistry& pages) {
    settings_page_test::Host host;
    simpilot::Localization localization(simpilot::UiLanguage::english);
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    pages.visit([&](const auto&, const auto& contribution) {
        auto page = contribution.create();
        page->create({GetModuleHandleW(nullptr), host.window, 96, font, localization, {}});
        const auto window = FindWindowExW(host.window, nullptr, L"Simpilot.EverythingSettingsPage", nullptr);
        page->show(true);
        for (const auto language : {simpilot::UiLanguage::english, simpilot::UiLanguage::simplified_chinese,
                                    simpilot::UiLanguage::traditional_chinese}) {
            localization.set_language(language);
            page->refresh_language(localization);
            for (const UINT dpi : {96U, 144U, 192U}) {
                for (const int width : {420, 650, 840}) {
                    page->layout({0, 0, MulDiv(width, dpi, 96), MulDiv(360, dpi, 96)}, dpi, font);
                    LONG previous_bottom = 0;
                    for (int id : {1, 2}) {
                        const auto button = GetDlgItem(window, id);
                        const auto action = settings_page_test::visible_bounds(window, button);
                        require(action.right - action.left > MulDiv(100, dpi, 96)
                            && action.bottom - action.top == MulDiv(32, dpi, 96),
                            "Everything task actions stay compact at every width and DPI");
                        require(action.left == 0 && action.top > previous_bottom
                            && !IsWindowVisible(GetDlgItem(window, id + 10)),
                            "Actions are left-aligned text commands without duplicate labels");
                        require(!IsWindowEnabled(button), "Unavailable Everything tasks remain disabled");
                        LOGFONTW actual_font{};
                        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0)),
                                   sizeof(actual_font), &actual_font);
                        require(actual_font.lfHeight == -MulDiv(14, dpi, 96), "Task actions use 14dip body type");
                        previous_bottom = action.bottom;
                    }
                }
            }
        }
    });
}
}

int main() {
    try {
        const auto sdk = simpilot::EverythingSearch::try_create(SIMPILOT_EVERYTHING_SDK);
        require(sdk != nullptr, "Bundled Everything SDK exports");
        simpilot::Localization localization(simpilot::UiLanguage::english);
        for (auto language : {simpilot::UiLanguage::english,
                              simpilot::UiLanguage::simplified_chinese,
                              simpilot::UiLanguage::traditional_chinese}) {
            const simpilot::Localization catalog(language);
            for (auto key : {"settings.open_everything_search", "ui.open_everything",
                             "ui.everything_unavailable", "ui.repair_everything",
                             "ui.repair_everything_success", "ui.repair_everything_failed"}) {
                require(catalog.text(key) != L"[missing translation]", "Module translations present");
            }
        }
        simpilot::ProgramSearchRegistry search;
        simpilot::TrayMenuRegistry tray;
        simpilot::UiDispatcher dispatcher(GetModuleHandleW(nullptr));
        simpilot::HotkeyRegistry hotkeys;
        simpilot::SettingsRegistry pages;
        simpilot::SettingsParticipantRegistry participants;
        auto module = simpilot::make_everything_module(
            std::filesystem::temp_directory_path() / L"simpilot-missing-everything",
            localization, search, tray, dispatcher, hotkeys, pages, participants, {}, {});
        module->start();
        require(!search.available(), "Missing SDK degrades to unavailable search");
        require(tray.size() == 2, "Module contributes both maintenance commands");
        require(pages.size() == 1, "Module contributes its own settings page");
        settings_page_test::check_page(pages, L"Simpilot.EverythingSettingsPage");
        task_row_tests(pages);
        {
            simpilot::SettingsSession session(participants, {});
            hotkeys.visit([](const auto&, const auto& contribution) {
                const auto current = contribution.current();
                require(current.binding.gesture
                    && current.binding.gesture->modifiers == MOD_WIN
                    && current.binding.gesture->virtual_key == L'S'
                    && current.binding.force_override && !current.enabled,
                    "Default disabled Win+S binding preserved");
                contribution.draft()->enabled = true;
            });
            require(session.dirty(), "Contributed hotkey edits participate in settings session");
            require(session.apply([](const auto& document) {
                return document.get(L"EverythingSearchCode") == L"8,83"
                    && document.get(L"EverythingSearchEnabled") == L"1";
            }), "Module-owned hotkey persists legacy keys");
        }
        module->stop();
        module->stop();
        require(tray.size() == 0, "Stop revokes tray commands");
        require(pages.size() == 0, "Stop revokes settings pages");
        int contributions = 0;
        hotkeys.visit([&](const auto&, const auto&) { ++contributions; });
        require(contributions == 0, "Stop revokes hotkeys");
        module->start();
        require(tray.size() == 2, "Restart does not retain stale contributions");
        module.reset();
        require(tray.size() == 0, "Destruction revokes contributions");
        std::cout << "Everything module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
