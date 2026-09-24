#include "keyboard_mapping_module.hpp"
#include "keyboard_mapping_engine.hpp"
#include "keyboard_manager.hpp"

#include <stdexcept>

namespace simpilot {

std::unique_ptr<ISettingsPage> make_keyboard_mapping_page(
    KeyboardMappingSettings&, KeyboardManager&, std::function<void(std::wstring_view)>);

namespace {

class KeyboardMappingModule final : public IAppModule {
public:
    KeyboardMappingModule(KeyboardMappingSettings settings, SettingsRegistry& pages,
        SettingsParticipantRegistry& participants, KeyboardManager& keyboard,
        std::function<void(std::wstring_view)> diagnose)
        : live_(std::move(settings)), draft_(live_), pages_(pages), participants_(participants),
          keyboard_(keyboard), diagnose_(std::move(diagnose)) {}
    ~KeyboardMappingModule() override { stop(); }

    void start() override {
        if (started_) return;
        started_ = true;
        current_ = compile(live_);
        if (!current_ || !keyboard_.set_processing_stage(current_)) {
            throw std::runtime_error("Cannot start keyboard mapping stage");
        }
        participant_ = participants_.add("keyboard_mapping", 50, SettingsParticipant{
            .begin = [this] { draft_ = live_; },
            .dirty = [this] { return draft_ != live_; },
            .validate = [this] { return validate_keyboard_mappings(draft_.rules).empty(); },
            .prepare = [this] {
                prepared_settings_ = draft_;
                prepared_ = compile(prepared_settings_);
                return prepared_ != nullptr;
            },
            .apply = [this] { return keyboard_.set_processing_stage(prepared_); },
            .write = [this](SettingsDocument& document) { draft_.write(document); },
            .rollback = [this] {
                const auto restored = keyboard_.set_processing_stage(current_);
                if (restored) prepared_.reset();
                return restored;
            },
            .finish = [this] {
                live_ = std::move(prepared_settings_);
                current_ = std::move(prepared_);
            },
            .cancel = [this] { draft_ = live_; prepared_.reset(); }});
        page_ = pages_.add("keyboard_mapping", 50, SettingsPageContribution{
            "settings.tab.keyboard_mappings", [this] {
                return make_keyboard_mapping_page(draft_, keyboard_, diagnose_);
            }});
    }
    void stop() noexcept override {
        page_.reset();
        participant_.reset();
        if (started_) {
            keyboard_.end_mapping_capture();
            (void)keyboard_.set_processing_stage({});
        }
        prepared_.reset();
        current_.reset();
        started_ = false;
    }

private:
    static std::shared_ptr<KeyboardMappingEngine> compile(const KeyboardMappingSettings& settings) {
        auto engine = std::make_shared<KeyboardMappingEngine>();
        return engine->replace_rules(settings.enabled, settings.rules) ? engine : nullptr;
    }
    KeyboardMappingSettings live_;
    KeyboardMappingSettings draft_;
    KeyboardMappingSettings prepared_settings_;
    SettingsRegistry& pages_;
    SettingsParticipantRegistry& participants_;
    KeyboardManager& keyboard_;
    std::function<void(std::wstring_view)> diagnose_;
    std::shared_ptr<KeyboardMappingEngine> current_;
    std::shared_ptr<KeyboardMappingEngine> prepared_;
    Registration page_;
    Registration participant_;
    bool started_ = false;
};

} // namespace

std::unique_ptr<IAppModule> make_keyboard_mapping_module(
    const SettingsDocument& document, SettingsRegistry& pages,
    SettingsParticipantRegistry& participants, KeyboardManager& keyboard,
    std::function<void(std::wstring_view)> diagnose) {
    return std::make_unique<KeyboardMappingModule>(
        KeyboardMappingSettings::read(document, diagnose), pages, participants,
        keyboard, std::move(diagnose));
}

} // namespace simpilot
