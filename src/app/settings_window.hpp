#pragma once
#include "simpilot/settings_registry.hpp"
#include "settings_visual_style.hpp"
namespace simpilot {
class SettingsWindow final {
public:
    using DiagnosticSink = std::function<void(std::wstring_view)>;
    using LanguageChangeSink = std::function<bool(std::string)>;
    static bool show_modal(HINSTANCE, HWND, std::string language,
        const std::filesystem::path&, const SettingsRegistry&, const SettingsParticipantRegistry&,
        SettingsSession::Commit, LanguageChangeSink = {}, DiagnosticSink = {});
private:
    SettingsWindow(HINSTANCE, HWND, std::string, const std::filesystem::path&,
        const SettingsRegistry&, const SettingsParticipantRegistry&, SettingsSession::Commit,
        LanguageChangeSink, DiagnosticSink);
    ~SettingsWindow();
    bool run();
    void create_controls();
    void layout();
    void update_font();
    void refresh_language();
    bool change_language(std::string);
    void select_page();
    bool apply();
    bool request_close();
    void changed();
    void paint_background(HDC dc);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    HINSTANCE instance_;
    HWND owner_, window_ = nullptr, navigation_ = nullptr, save_ = nullptr,
         apply_ = nullptr, cancel_ = nullptr, status_ = nullptr,
         brand_ = nullptr, navigation_label_ = nullptr, brand_icon_ = nullptr;
    HFONT font_ = nullptr;
    settings_visual_style::PageTypography typography_;
    int navigation_width_ = 216;
    int footer_height_ = 72;
    UINT dpi_ = 96;
    int selected_ = 0;
    bool applied_ = false;
    bool save_failed_ = false;
    Localization localization_;
    const SettingsRegistry& registry_;
    SettingsSession session_;
    SettingsSession::Commit commit_;
    LanguageChangeSink language_change_;
    DiagnosticSink diagnose_;
    struct Page { std::string title; std::unique_ptr<ISettingsPage> instance; };
    std::vector<Page> pages_;
};
} // namespace simpilot
