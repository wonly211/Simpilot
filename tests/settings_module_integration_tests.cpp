#include "settings_window.hpp"
#include "general_settings_page.hpp"
#include "hotkey_settings_page.hpp"
#include "keyboard_manager.hpp"
#include "simpilot/app_module.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/tray_menu_registry.hpp"
#include "settings_page_test_support.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class FixturePage final : public simpilot::ISettingsPage {
public:
    explicit FixturePage(bool& draft) : draft_(draft) {}
    ~FixturePage() override { if (window_) DestroyWindow(window_); }
    void create(const simpilot::SettingsPageContext& context) override {
        changed_ = context.changed;
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, context.parent, nullptr, context.instance, nullptr);
        require(window_ != nullptr, "Create contributed page");
    }
    void layout(RECT bounds, UINT dpi, HFONT font) override {
        require(dpi > 0 && font != nullptr, "Host forwards DPI and font");
        MoveWindow(window_, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, TRUE);
    }
    void show(bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
    void refresh_language(const simpilot::Localization&) override { ++language_updates; }
    void edit(bool value) { draft_ = value; changed_(); }
    HWND window() const { return window_; }
    int language_updates = 0;
private:
    bool& draft_;
    HWND window_ = nullptr;
    std::function<void()> changed_;
};

class FixtureModule final : public simpilot::IAppModule {
public:
    FixtureModule(simpilot::SettingsRegistry& pages,
                  simpilot::SettingsParticipantRegistry& participants,
                  simpilot::HotkeyRegistry& hotkeys, simpilot::TrayMenuRegistry& tray)
        : pages_(pages), participants_(participants), hotkeys_(hotkeys), tray_(tray) {}
    void start() override {
        registration_ = participants_.add("fixture", 0, simpilot::SettingsParticipant{
            .begin = [this] { draft = live; },
            .dirty = [this] { return draft != live; },
            .prepare = [this] { previous_ = live; return true; },
            .apply = [this] { live = draft; return true; },
            .write = [this](auto& document) { document.set(L"Fixture", L"FixtureEnabled", draft ? L"1" : L"0"); },
            .rollback = [this] { live = previous_; return true; },
            .cancel = [this] { draft = live; }});
        page_registration_ = pages_.add("fixture", 90, simpilot::SettingsPageContribution{
            "settings.applied", [this] {
                auto result = std::make_unique<FixturePage>(draft);
                page = result.get();
                return result;
            }});
        hotkey_registration = hotkeys_.add("fixture", 0, simpilot::HotkeyContribution{
            "settings.title", [this] { return hotkey_; },
            [this](HWND) { ++invocations; }, [this] { return &hotkey_; }});
        tray_registration_ = tray_.add("fixture", 0, simpilot::TrayCommand{
            "settings.title", {}, {}, [this](HWND) { ++invocations; }});
    }
    void stop() noexcept override {
        tray_registration_.reset();
        hotkey_registration.reset();
        page_registration_.reset();
        registration_.reset();
    }
    ~FixtureModule() override { stop(); }
    FixturePage* page = nullptr;
    bool draft = false;
    bool live = false;
    int invocations = 0;
    simpilot::Registration hotkey_registration;
private:
    simpilot::SettingsRegistry& pages_;
    simpilot::SettingsParticipantRegistry& participants_;
    simpilot::HotkeyRegistry& hotkeys_;
    simpilot::TrayMenuRegistry& tray_;
    simpilot::Registration registration_;
    simpilot::Registration page_registration_;
    simpilot::Registration tray_registration_;
    simpilot::BuiltInHotKey hotkey_{{simpilot::HotKeyGesture{MOD_CONTROL, VK_F24}, false}, true};
    bool previous_ = false;
};

void hotkey_page_layout_tests() {
    settings_page_test::Host host;
    simpilot::HotkeyRegistry hotkeys;
    simpilot::KeyboardManager keyboard;
    std::array<simpilot::BuiltInHotKey, 4> bindings;
    constexpr std::string_view labels[]{"settings.main_menu", "settings.second_menu",
        "settings.open_settings", "settings.open_everything_search"};
    std::vector<simpilot::Registration> registrations;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        bindings[i] = {{simpilot::HotKeyGesture{MOD_CONTROL, VK_F20 + static_cast<UINT>(i)}, false}, true};
        registrations.push_back(hotkeys.add("layout." + std::to_string(i), static_cast<int>(i),
            simpilot::HotkeyContribution{std::string(labels[i]), [&, i] { return bindings[i]; },
                [](HWND) {}, [&, i] { return &bindings[i]; }}));
    }
    bool section_draft = false;
    FixturePage* section = nullptr;
    auto section_registration = hotkeys.sections.add("layout.section", 0, {
        "settings.title", [&] {
            auto page = std::make_unique<FixturePage>(section_draft);
            section = page.get();
            return page;
        }});
    simpilot::Localization localization(simpilot::UiLanguage::english);
    const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto page = simpilot::make_hotkey_settings_page(hotkeys, keyboard, {}, {});
    int changes = 0;
    page->create({GetModuleHandleW(nullptr), host.window, 96, font, localization, [&] { ++changes; }});
    const auto window = FindWindowExW(host.window, nullptr, L"Simpilot.HotkeySettingsPage", nullptr);
    require(window != nullptr && section != nullptr, "Create shared hotkey page and contributed section");
    page->show(true);
    for (auto language : {simpilot::UiLanguage::english, simpilot::UiLanguage::simplified_chinese,
                          simpilot::UiLanguage::traditional_chinese}) {
        localization.set_language(language);
        page->refresh_language(localization);
        for (UINT dpi : {96U, 144U, 192U}) {
            LONG capture_left = -1;
            for (int width : {650, 840, 1100}) {
                page->layout({0, 0, MulDiv(width, dpi, 96), MulDiv(600, dpi, 96)}, dpi, font);
                const auto capture = settings_page_test::visible_bounds(window, GetDlgItem(window, 100));
                require(capture.left <= MulDiv(264, dpi, 96),
                    "Hotkey capture follows a bounded description column");
                require(capture_left < 0 || capture.left == capture_left,
                    "Widening the page must not grow the description-to-capture gap");
                capture_left = capture.left;
                const auto label = GetWindow(GetDlgItem(window, 100), GW_HWNDPREV);
                const auto label_bounds = settings_page_test::visible_bounds(window, label);
                require(capture.left == MulDiv(144, dpi, 96)
                    && capture.left - label_bounds.right == MulDiv(12, dpi, 96),
                    "Description and capture use a consistent compact gutter");
            }
            for (int height : {600, 400, 600}) {
                page->layout({0, 0, MulDiv(650, dpi, 96), MulDiv(height, dpi, 96)}, dpi, font);
                LONG previous_bottom = 0;
                for (int row = 0; row < 4; ++row) {
                    const auto enabled = settings_page_test::visible_bounds(window, GetDlgItem(window, 102 + row * 3));
                    const auto capture = settings_page_test::visible_bounds(window, GetDlgItem(window, 100 + row * 3));
                    const auto clear = settings_page_test::visible_bounds(window, GetDlgItem(window, 101 + row * 3));
                    require(capture.right < clear.left && clear.right < enabled.left,
                            "Compact capture, clear and enable controls do not overlap");
                    require(capture.right - capture.left <= MulDiv(228, dpi, 96)
                        && clear.right - clear.left == MulDiv(32, dpi, 96)
                        && enabled.right - enabled.left == MulDiv(44, dpi, 96),
                        "Hotkey controls retain compact widths instead of stretching with the window");
                    require(enabled.top == capture.top + MulDiv(2, dpi, 96) && enabled.top == clear.top
                        && enabled.top > previous_bottom, "Hotkey rows align without overlap");
                    previous_bottom = enabled.bottom;
                }
                if (height == 600) {
                    const auto section_bounds = settings_page_test::visible_bounds(window, section->window());
                    require(section_bounds.top > previous_bottom
                        && section_bounds.bottom == MulDiv(height, dpi, 96),
                        "Contributed section fills remaining page height without overlapping built-in rows");
                } else {
                    SendMessageW(window, WM_VSCROLL, SB_PAGEDOWN, 0);
                    const auto section_bounds = settings_page_test::visible_bounds(window, section->window());
                    require(section_bounds.bottom == MulDiv(height, dpi, 96),
                            "Scrolling reveals the complete contributed section");
                    SendMessageW(window, WM_VSCROLL, SB_PAGEUP, 0);
                }
            }
        }
    }
    for (UINT dpi : {96U, 144U, 192U}) {
        page->layout({0, 0, MulDiv(460, dpi, 96), MulDiv(720, dpi, 96)}, dpi, font);
        LONG previous_bottom = 0;
        for (int row = 0; row < 4; ++row) {
            const auto capture = settings_page_test::visible_bounds(window, GetDlgItem(window, 100 + row * 3));
            const auto clear = settings_page_test::visible_bounds(window, GetDlgItem(window, 101 + row * 3));
            const auto enabled = settings_page_test::visible_bounds(window, GetDlgItem(window, 102 + row * 3));
            require(capture.top > previous_bottom && capture.right < clear.left
                && clear.right < enabled.left, "Narrow hotkey page stacks rows without clipping");
            previous_bottom = capture.bottom;
        }
    }
    const auto toggle = GetDlgItem(window, 102);
    SendMessageW(toggle, BM_CLICK, 0, 0);
    require(!bindings[0].enabled && changes == 1, "Enable switch click updates its owning hotkey draft");
    SendMessageW(toggle, BM_CLICK, 0, 0);
    require(bindings[0].enabled && changes == 2, "Enable switch can be turned back on");
    SendMessageW(GetDlgItem(window, 101), BM_CLICK, 0, 0);
    require(!bindings[0].binding.gesture && !IsWindowEnabled(toggle),
            "Clearing a binding disables its enable switch");
    page->show(false);
    require(!IsWindowVisible(toggle), "Page navigation hides its switches");
    page->show(true);
    require(IsWindowVisible(toggle) != FALSE, "Returning to hotkeys restores its switches");
}

FixtureModule* active_module = nullptr;
bool failed = false;
bool reject_commit = true;
int callback_step = 0;
std::string error_text;
std::filesystem::path settings_path;

BOOL CALLBACK find_settings(HWND window, LPARAM parameter) {
    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, 128);
    if (std::wstring_view(class_name) == L"Simpilot.SettingsWindow") {
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

void CALLBACK exercise_window(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND window = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), find_settings, reinterpret_cast<LPARAM>(&window));
    if (!window) return;
    try {
        if (callback_step++ == 0) {
            require(active_module->page != nullptr, "Host instantiates contributed page");
            const auto navigation = GetDlgItem(window, 250);
            const auto count = SendMessageW(navigation, LB_GETCOUNT, 0, 0);
            require(count > 0, "Settings navigation present");
            SendMessageW(navigation, LB_SETCURSEL, count - 1, 0);
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(250, LBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(navigation));
            require(IsWindowVisible(active_module->page->window()) != FALSE, "Navigate to module page");
            RECT page_bounds{}, client{};
            GetWindowRect(active_module->page->window(), &page_bounds);
            GetClientRect(window, &client);
            require(page_bounds.right > page_bounds.left && page_bounds.bottom > page_bounds.top,
                    "Contributed page receives nonempty layout");
            RECT bounds{};
            GetWindowRect(window, &bounds);
            SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144, 144),
                         reinterpret_cast<LPARAM>(&bounds));
            const auto general = FindWindowExW(window, nullptr, L"Simpilot.GeneralSettingsPage", nullptr);
            const auto languages = GetDlgItem(general, 12);
            require(languages != nullptr, "General page owns language control");
            active_module->page->edit(true);
            for (int index = 0; index < 3; ++index) {
                SendMessageW(languages, CB_SETCURSEL, index, 0);
                SendMessageW(general, WM_COMMAND, MAKEWPARAM(12, CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(languages));
            }
            require(active_module->page->language_updates > 0, "Module page receives language refresh");
            require(active_module->draft && !active_module->live,
                    "Immediate language change preserves pending module draft without applying it");
            const auto language_document = simpilot::SettingsDocument::load(settings_path);
            require(language_document.get(L"Language").has_value()
                && !language_document.get(L"FixtureEnabled"),
                "Language persistence does not write uncommitted module settings");
            require(IsWindowEnabled(GetDlgItem(window, 3)) != FALSE, "Module dirty state enables Apply");
            const auto save_bounds = settings_page_test::visible_bounds(window, GetDlgItem(window, 1));
            const auto apply_bounds = settings_page_test::visible_bounds(window, GetDlgItem(window, 3));
            const auto cancel_bounds = settings_page_test::visible_bounds(window, GetDlgItem(window, 2));
            require(cancel_bounds.right < apply_bounds.left && apply_bounds.right < save_bounds.left,
                "Footer presents Cancel, Apply, Save in a stable action hierarchy");
            require(GetNextDlgTabItem(window, GetDlgItem(window, 2), FALSE) == GetDlgItem(window, 3)
                && GetNextDlgTabItem(window, GetDlgItem(window, 3), FALSE) == GetDlgItem(window, 1),
                "Footer keyboard order follows its visual order");
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(3, BN_CLICKED), 0);
            require(!active_module->live && active_module->draft,
                    "Persistence failure restores runtime and retains draft");
            reject_commit = false;
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(3, BN_CLICKED), 0);
            require(active_module->live, "Successful apply commits module runtime");
            require(IsWindowEnabled(GetDlgItem(window, 3)) == FALSE, "Successful apply clears dirty state");
            return;
        }
        KillTimer(nullptr, timer);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(2, BN_CLICKED), 0);
    } catch (const std::exception& error) {
        failed = true;
        error_text = error.what();
        KillTimer(nullptr, timer);
        DestroyWindow(window);
    }
}

} // namespace

int main() {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        hotkey_page_layout_tests();
        simpilot::SettingsRegistry pages;
        simpilot::SettingsParticipantRegistry participants;
        simpilot::HotkeyRegistry hotkeys;
        simpilot::TrayMenuRegistry tray;
        FixtureModule module(pages, participants, hotkeys, tray);
        module.start();
        active_module = &module;
        const auto root = std::filesystem::temp_directory_path()
            / (L"simpilot-settings-module-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        settings_path = root / L"Setting.ini";
        simpilot::KeyboardManager keyboard;
        simpilot::AppSettings general_draft;
        auto general_page = pages.add("general", 10, {"settings.tab.general",
            [&] { return simpilot::make_general_settings_page(general_draft); }});
        auto hotkey_page = pages.add("hotkeys", 40, {"settings.tab.global_hotkeys",
            [&] { return simpilot::make_hotkey_settings_page(hotkeys, keyboard, {}, {}); }});
        for (int iteration = 0; iteration < 2; ++iteration) {
            callback_step = 0;
            reject_commit = true;
            module.live = false;
            module.page = nullptr;
            if (iteration == 1) module.hotkey_registration.reset();
            const auto timer = SetTimer(nullptr, 0, 30, exercise_window);
            require(timer != 0, "Create UI test timer");
            const auto result = simpilot::SettingsWindow::show_modal(
                GetModuleHandleW(nullptr), nullptr, "zh-CN", root / L"Setting.ini",
                pages, participants, [](const simpilot::SettingsDocument& document) {
                    return !reject_commit && document.get(L"FixtureEnabled") == L"1";
                }, [](std::string language) {
                    auto document = simpilot::SettingsDocument::load(settings_path);
                    document.set(L"General", L"Language", std::wstring(language.begin(), language.end()));
                    return document.save(settings_path);
                });
            KillTimer(nullptr, timer);
            require(!failed, error_text.c_str());
            require(result, "Settings host returns applied result");
        }
        auto broken_page = pages.add("broken", 100, simpilot::SettingsPageContribution{
            "settings.title", []() -> std::unique_ptr<simpilot::ISettingsPage> {
                throw std::runtime_error("Injected page factory failure");
            }});
        const auto failed_creation = simpilot::SettingsWindow::show_modal(
            GetModuleHandleW(nullptr), nullptr, "zh-CN", root / L"Setting.ini",
            pages, participants, {});
        require(!failed_creation, "Page creation failure closes the incomplete host");
        broken_page.reset();
        module.stop();
        general_page.reset();
        hotkey_page.reset();
        require(pages.size() == 0 && tray.size() == 0, "Fixture module removes all contributions");
        std::filesystem::remove_all(root);
        std::cout << "Settings module integration tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
