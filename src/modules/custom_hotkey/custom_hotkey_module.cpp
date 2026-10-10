#include "custom_hotkey_module.hpp"
#include "custom_hotkey_settings.hpp"
#include "simpilot/variable_expander.hpp"
#include "keyboard_manager.hpp"
#include "simpilot/hotkey_settings_codec.hpp"

namespace simpilot {

void inspect_custom_hotkey_backup(const SettingsSnapshot& snapshot, bool check_environment, std::vector<std::wstring>& warnings) {
    const auto document = SettingsDocument::parse(snapshot.text("Config/Setting.ini"));
    const auto text = document.get(L"CustomGlobalHotKeyCount").value_or(L"0");
    if (text.empty() || text.find_first_not_of(L"0123456789") != text.npos) throw std::runtime_error("Invalid custom hotkey count");
    const auto count = std::stoul(text);
    const auto settings = CustomHotkeySettings::read(document);
    if (count > 128 || settings.items.size() != count) throw std::runtime_error("Invalid custom hotkey rules");
    for (unsigned long i = 1; i <= count; ++i)
        validate_hotkey_setting(document, L"CustomGlobalHotKey" + std::to_wstring(i));
    if (check_environment) for (const auto& item : settings.items)
        if (std::filesystem::path(item.program_path).is_absolute() && !std::filesystem::exists(item.program_path)) warnings.push_back(item.program_path);
}
std::unique_ptr<ISettingsPage> make_custom_hotkey_section(
    CustomHotkeySettings&, HotkeyRegistry&, KeyboardManager&,
    const std::filesystem::path&, std::function<void(std::wstring_view)>);
namespace {
class CustomHotkeyModule final : public IAppModule {
public:
    CustomHotkeyModule(CustomHotkeySettings settings, std::filesystem::path config_directory,
        SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
        KeyboardManager& keyboard, LaunchErrorSink launch_error,
        std::function<void(std::wstring_view)> diagnose)
        : live_(std::move(settings)), draft_(live_), directory_(std::move(config_directory)),
          participants_(participants), hotkeys_(hotkeys), keyboard_(keyboard),
          launch_error_(std::move(launch_error)), diagnose_(std::move(diagnose)) {}
    ~CustomHotkeyModule() override { stop(); }
    void start() override {
        if (started_) return;
        started_ = true;
        // INI compatibility limits the collection to 128 entries. Stable slots
        // avoid allocating contributions during commit or rollback.
        for (std::size_t i = 0; i < 128; ++i) {
            bindings_.push_back(hotkeys_.add("custom_hotkey." + std::to_string(i), 100,
                HotkeyContribution{"settings.section.custom_hotkeys",
                    [this, i] { return i < live_.items.size()
                        ? BuiltInHotKey{live_.items[i].binding, live_.items[i].enabled} : BuiltInHotKey{}; },
                    [this, i](HWND owner) { if (i < live_.items.size()) launch(owner, live_.items[i]); },
                    {}}));
        }
        drafts_ = hotkeys_.add_editable_drafts("custom_hotkey", [this] {
            std::vector<HotkeyDraftBinding> result;
            for (std::size_t i = 0; i < draft_.items.size(); ++i) {
                result.push_back({"custom_hotkey." + std::to_string(i),
                    "settings.section.custom_hotkeys",
                    [this, i] { return BuiltInHotKey{draft_.items.at(i).binding, draft_.items.at(i).enabled}; },
                    [this, i](BuiltInHotKey value) {
                        draft_.items[i].binding = value.binding;
                        draft_.items[i].enabled = value.enabled;
                    }, [this, i] { return i < draft_.items.size(); }, false});
            }
            return result;
        });
        participant_ = participants_.add("custom_hotkey", 40, SettingsParticipant{
            .begin = [this] { draft_ = live_; },
            .dirty = [this] { return draft_ != live_; },
            .validate = [this] { return draft_.items.size() <= 128; },
            .prepare = [this] { prepared_ = draft_; applied_ = false; return true; },
            .apply = [this] { std::swap(live_, prepared_); applied_ = true; return true; },
            .write = [this](SettingsDocument& document) { draft_.write(document); },
            .rollback = [this] {
                if (applied_) std::swap(live_, prepared_);
                applied_ = false;
                return true;
            },
            .finish = [this] { prepared_.items.clear(); applied_ = false; },
            .cancel = [this] { draft_ = live_; prepared_.items.clear(); }});
        section_ = hotkeys_.sections.add("custom_hotkey", 100, SettingsPageContribution{
            "settings.section.custom_hotkeys", [this] {
                return make_custom_hotkey_section(draft_, hotkeys_, keyboard_, directory_, diagnose_);
            }});
    }
    void stop() noexcept override {
        section_.reset();
        participant_.reset();
        drafts_.reset();
        bindings_.clear();
        started_ = false;
    }
private:
    void launch(HWND owner, const CustomGlobalHotKey& item) {
        const VariableExpander expander(directory_.wstring());
        const auto expand = [&](const std::wstring& value) {
            auto path = std::filesystem::path(expander.expand(value));
            if (path.is_relative()) path = directory_ / path;
            return path.lexically_normal().wstring();
        };
        execute_command(owner, LaunchRequest{
            .target = expand(item.program_path), .arguments = item.arguments,
            .working_directory = item.working_directory.empty() ? L"" : expand(item.working_directory),
            .application = item.action == CustomHotKeyAction::open_application,
            .run_as_administrator = item.run_as_administrator,
            .existing_process_action = item.existing_process_action, .visibility = item.visibility},
            launch_error_);
    }
    CustomHotkeySettings live_, draft_, prepared_;
    std::filesystem::path directory_;
    SettingsParticipantRegistry& participants_;
    HotkeyRegistry& hotkeys_;
    KeyboardManager& keyboard_;
    LaunchErrorSink launch_error_;
    std::function<void(std::wstring_view)> diagnose_;
    std::vector<Registration> bindings_;
    Registration drafts_, participant_, section_;
    bool started_ = false, applied_ = false;
};
}
std::unique_ptr<IAppModule> make_custom_hotkey_module(
    const SettingsDocument& document, const std::filesystem::path& config_directory,
    SettingsParticipantRegistry& participants, HotkeyRegistry& hotkeys,
    KeyboardManager& keyboard, LaunchErrorSink launch_error,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<CustomHotkeyModule>(CustomHotkeySettings::read(document),
        config_directory, participants, hotkeys, keyboard, std::move(launch_error), std::move(diagnose));
}
} // namespace simpilot
