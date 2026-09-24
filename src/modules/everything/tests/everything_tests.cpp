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
