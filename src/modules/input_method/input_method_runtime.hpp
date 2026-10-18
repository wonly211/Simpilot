#pragma once

#include "input_method_backend.hpp"

#include <functional>

namespace simpilot::input_method {

class Runtime final {
public:
    using Persist = std::function<bool(const History&)>;
    using Diagnose = std::function<void(const Status&)>;

    Runtime(IBackend& backend, Persist persist, Diagnose diagnose = {});
    void configure(Settings settings);
    void set_history(History history);
    void refresh_profiles();
    void tick();
    void stop() noexcept;
    const std::vector<Profile>& profiles() const noexcept { return profiles_; }
    const Status& status() const noexcept { return status_; }
    const History& history() const noexcept { return history_; }

private:
    void remember();
    void read_current();
    void advance();
    void report(std::string key, DWORD error = ERROR_SUCCESS);
    IBackend& backend_;
    Persist persist_;
    Diagnose diagnose_;
    Settings settings_;
    History history_;
    std::vector<Profile> profiles_;
    Foreground current_;
    std::optional<State> observed_;
    std::optional<State> pending_;
    int attempts_ = 0;
    bool profile_requested_ = false;
    bool mode_requested_ = false;
    bool fixed_ = false;
    bool history_dirty_ = false;
    bool history_write_failed_ = false;
    std::wstring history_error_path_;
    Status status_;
};

} // namespace simpilot::input_method
