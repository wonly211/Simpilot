#include "input_method_module_internal.hpp"

#include "input_method_page.hpp"
#include "input_method_runtime.hpp"

#include <utility>

namespace simpilot::input_method {
namespace {

class Module final : public IAppModule {
public:
    Module(std::filesystem::path directory, UiDispatcher& dispatcher,
        SettingsRegistry& pages, SettingsParticipantRegistry& participants,
        std::function<void(std::wstring_view)> diagnose, std::unique_ptr<IBackend> backend)
        : directory_(std::move(directory)), dispatcher_(dispatcher), pages_(pages),
          participants_(participants), diagnose_(std::move(diagnose)),
          backend_(std::move(backend)),
          runtime_(*backend_, [this](const History& history) { return persist(history); },
            [this](const Status& status) {
                if (status.error) log(L"input_method: " + std::wstring(
                    status.key.begin(), status.key.end()) + L" error="
                    + std::to_wstring(status.error) + L" application=" + status.path);
            }) {}

    ~Module() override { stop(); }

    void start() override {
        if (started_) return;
        try {
            live_ = Settings::read(SettingsDocument::load(directory_ / L"Setting.ini"));
        } catch (...) {
            // Do not overwrite or silently discard invalid rules from a later
            // build while the rest of the application remains usable.
            config_readable_ = false;
            log(L"input_method: configuration unavailable; module disabled");
        }
        draft_ = live_;
        scope_ = std::make_unique<DispatchScope>();
        runtime_.refresh_profiles();
        started_ = true;
        try {
            participant_ = participants_.add("input_method", 45, SettingsParticipant{
                .begin = [this] { draft_ = live_; },
                .dirty = [this] { return draft_ != live_; },
                .validate = [this] {
                    if (!config_readable_) return draft_ == live_;
                    if (!validate(draft_)) return false;
                    for (const auto& rule : draft_.rules) {
                        const auto* profile = find_profile(runtime_.profiles(), rule.profile_id);
                        if (rule.policy == Policy::fixed && profile
                            && !profile->supports_chinese && rule.mode == Mode::chinese) return false;
                    }
                    return true;
                },
                .prepare = [this] { prepared_ = draft_; return true; },
                .apply = [this] {
                    if (prepared_ == live_) return true;
                    // Reserve any fallible timer before changing runtime state.
                    ensure_timer(prepared_.enabled);
                    transaction_pending_ = true;
                    return true;
                },
                .write = [this](SettingsDocument& doc) {
                    if (config_readable_) draft_.write(doc);
                },
                .rollback = [this] {
                    ensure_timer(live_.enabled);
                    if (!live_.enabled) {
                        timer_.reset();
                        monitoring_ = false;
                    }
                    transaction_pending_ = false;
                    return true;
                },
                .finish = [this] {
                    runtime_.configure(prepared_);
                    live_ = std::move(prepared_);
                    if (!live_.enabled) {
                        timer_.reset();
                        monitoring_ = false;
                    }
                    transaction_pending_ = false;
                },
                .cancel = [this] { draft_ = live_; }});
            page_ = pages_.add("input_method", 45, {
                "settings.tab.input_method", [this] {
                    return make_settings_page(draft_, PageServices{
                        [this]() -> const std::vector<Profile>& { return runtime_.profiles(); },
                        [this] { runtime_.refresh_profiles(); },
                        [this] {
                            if (!config_readable_)
                                return Status{"settings.input_method.config_failed", {}, ERROR_INVALID_DATA};
                            return runtime_.status();
                        }});
                }});
            ensure_timer(live_.enabled);
            runtime_.configure(live_);
        } catch (...) {
            stop();
            throw;
        }
    }

    void stop() noexcept override {
        timer_.reset();
        if (scope_) scope_->cancel();
        runtime_.stop();
        page_.reset();
        participant_.reset();
        scope_.reset();
        started_ = false;
        monitoring_ = false;
        transaction_pending_ = false;
    }

private:
    void ensure_timer(bool enabled) {
        // Keep a live timer until commit. A failed disabling transaction can
        // resume monitoring without allocating a replacement during rollback.
        if (!enabled || monitoring_) return;
        load_history();
        timer_ = dispatcher_.repeat(*scope_, 200, [this] {
            if (transaction_pending_) return;
            runtime_.tick();
        });
        monitoring_ = true;
    }

    void load_history() {
        if (history_loaded_) return;
        history_loaded_ = true;
        try {
            history_document_ = SettingsDocument::load(directory_ / L"InputMethodHistory.ini");
            runtime_.set_history(History::read(history_document_));
        } catch (...) {
            history_readable_ = false;
            log(L"input_method: history unavailable; original file retained");
        }
    }

    bool persist(const History& history) {
        if (!history_readable_) return false;
        try {
            // Re-read before updating owned keys, preserving unrelated external
            // edits. Invalid or unreadable documents are never replaced.
            auto candidate = SettingsDocument::load(directory_ / L"InputMethodHistory.ini");
            (void)History::read(candidate);
            history.write(candidate);
            if (!candidate.save(directory_ / L"InputMethodHistory.ini")) return false;
            history_document_ = std::move(candidate);
            return true;
        } catch (...) {
            return false;
        }
    }

    void log(std::wstring_view message) noexcept {
        try { if (diagnose_) diagnose_(message); } catch (...) {}
    }

    std::filesystem::path directory_;
    UiDispatcher& dispatcher_;
    SettingsRegistry& pages_;
    SettingsParticipantRegistry& participants_;
    std::function<void(std::wstring_view)> diagnose_;
    std::unique_ptr<IBackend> backend_;
    Runtime runtime_;
    Settings live_, draft_, prepared_;
    SettingsDocument history_document_;
    std::unique_ptr<DispatchScope> scope_;
    Registration timer_, participant_, page_;
    bool started_ = false;
    bool monitoring_ = false;
    bool transaction_pending_ = false;
    bool history_loaded_ = false;
    bool history_readable_ = true;
    bool config_readable_ = true;
};

} // namespace

std::unique_ptr<IAppModule> make_module(
    std::filesystem::path config_directory, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose, std::unique_ptr<IBackend> backend) {
    return std::make_unique<Module>(std::move(config_directory), dispatcher, pages,
        participants, std::move(diagnose), std::move(backend));
}

} // namespace simpilot::input_method

namespace simpilot {

std::unique_ptr<IAppModule> make_input_method_module(
    HINSTANCE, std::filesystem::path config_directory,
    const Localization&, UiDispatcher& dispatcher,
    SettingsRegistry& pages, SettingsParticipantRegistry& participants,
    std::function<void(std::wstring_view)> diagnose) {
    return input_method::make_module(std::move(config_directory), dispatcher, pages,
        participants, std::move(diagnose), input_method::make_backend());
}

} // namespace simpilot
