#include "input_method_runtime.hpp"

#include <utility>

namespace simpilot::input_method {

Runtime::Runtime(IBackend& backend, Persist persist, Diagnose diagnose)
    : backend_(backend), persist_(std::move(persist)), diagnose_(std::move(diagnose)) {}

void Runtime::configure(Settings settings) {
    if (settings_ == settings) return;
    // The caller commits its settings transaction before configuring us.
    // Preserve the last sampled state before disabling or replacing rules.
    remember();
    settings_ = std::move(settings);
    current_ = {};
    observed_.reset();
    pending_.reset();
    report(settings_.enabled ? "settings.input_method.waiting" : "settings.input_method.disabled");
}

void Runtime::set_history(History history) { history_ = std::move(history); }

void Runtime::refresh_profiles() { profiles_ = backend_.enumerate(); }

void Runtime::report(std::string key, DWORD error) {
    if (!error && history_write_failed_) {
        key = "settings.input_method.history_failed";
        error = ERROR_WRITE_FAULT;
    }
    Status next{std::move(key), current_.path, error};
    if (next.key == "settings.input_method.history_failed") next.path = history_error_path_;
    if (next == status_) return;
    status_ = std::move(next);
    if (diagnose_) diagnose_(status_);
}

void Runtime::remember() {
    if (observed_ && !current_.path.empty()) {
        const auto* rule = find_rule(settings_, current_.path);
        const auto found = history_.states.find(current_.path);
        if (!(rule && rule->policy == Policy::ignore)
            && (found != history_.states.end() || history_.states.size() < 4096)
            && (found == history_.states.end() || found->second != *observed_)) {
            history_.states[current_.path] = *observed_;
            history_dirty_ = true;
        }
    }
    if (history_dirty_) {
        if (!persist_ || persist_(history_)) {
            history_dirty_ = false;
            history_write_failed_ = false;
            history_error_path_.clear();
        } else {
            history_write_failed_ = true;
            if (!current_.path.empty()) history_error_path_ = current_.path;
            report("settings.input_method.history_failed", ERROR_WRITE_FAULT);
        }
    }
}

void Runtime::read_current() {
    const auto* rule = find_rule(settings_, current_.path);
    if (rule && rule->policy == Policy::ignore) return;
    const auto state = backend_.read(current_, profiles_);
    if (state) observed_ = state;
    else report("settings.input_method.state_failed", backend_.last_error());
}

void Runtime::tick() {
    if (!settings_.enabled) return;
    auto next = backend_.foreground();
    next.path = normalize_path(next.path);
    if (next.path != current_.path) {
        // The cached reading was taken while the previous application was
        // active. Reading its IME after the switch can yield the new state.
        remember();
        current_ = std::move(next);
        observed_.reset();
        pending_.reset();
        profile_requested_ = mode_requested_ = false;
        attempts_ = 0;
        if (current_.path.empty()) {
            const auto error = backend_.last_error();
            if (error) report("settings.input_method.target_unavailable", error);
            return;
        }
        const auto* rule = find_rule(settings_, current_.path);
        if (rule && rule->policy == Policy::ignore) {
            report("settings.input_method.state_ignored");
            return;
        }
        fixed_ = rule && rule->policy == Policy::fixed;
        pending_ = desired_state(settings_, history_, current_.path);
        if (!pending_) report("settings.input_method.state_unchanged");
    } else {
        // Same executable, new window or focus: observe its state, but do not
        // replay a rule merely because an application switched its own tabs.
        current_ = std::move(next);
    }
    if (current_.path.empty()) return;
    if (pending_) advance();
    else read_current();
}

void Runtime::advance() {
    const auto* profile = find_profile(profiles_, pending_->profile_id);
    if (!profile) {
        report("settings.input_method.unavailable_profile", ERROR_NOT_FOUND);
        pending_.reset();
        return;
    }
    if (!profile->selectable || !profile->layout
        || (pending_->mode == Mode::chinese && !profile->supports_chinese)) {
        report("settings.input_method.unsupported_profile", ERROR_NOT_SUPPORTED);
        pending_.reset();
        return;
    }
    if (++attempts_ > 6) {
        report("settings.input_method.state_failed", ERROR_TIMEOUT);
        pending_.reset();
        return;
    }
    const auto reading = backend_.read(current_, profiles_);
    if (reading && *reading == *pending_) {
        observed_ = reading;
        pending_.reset();
        report(fixed_ ? "settings.input_method.state_applied"
                      : "settings.input_method.state_remembered");
        return;
    }
    if (!reading || reading->profile_id != profile->id) {
        if (!profile_requested_) {
            profile_requested_ = true;
            if (!backend_.request_profile(current_, *profile)) {
                report("settings.input_method.state_failed", backend_.last_error());
                pending_.reset();
            }
        }
        return;
    }
    if (!mode_requested_) {
        mode_requested_ = true;
        if (!backend_.request_mode(current_, *profile, pending_->mode)) {
            report("settings.input_method.state_failed", backend_.last_error());
            pending_.reset();
        }
    }
}

void Runtime::stop() noexcept {
    try { remember(); } catch (...) {}
    settings_.enabled = false;
    current_ = {};
    observed_.reset();
    pending_.reset();
}

} // namespace simpilot::input_method
