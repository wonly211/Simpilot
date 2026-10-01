#pragma once

#include "simpilot/settings_document.hpp"

#include <Windows.h>

#include <map>
#include <optional>

namespace simpilot::input_method {

enum class Policy { remember, fixed, ignore };
enum class Mode { chinese, english };

struct Rule {
    std::wstring executable_path;
    Policy policy = Policy::remember;
    std::wstring profile_id;
    Mode mode = Mode::english;
    bool operator==(const Rule&) const = default;
};

struct State {
    std::wstring profile_id;
    Mode mode = Mode::english;
    bool operator==(const State&) const = default;
};

struct Settings {
    bool enabled = false;
    std::vector<Rule> rules;
    static Settings read(const SettingsDocument& document);
    void write(SettingsDocument& document) const;
    bool operator==(const Settings&) const = default;
};

struct History {
    std::map<std::wstring, State> states;
    static History read(const SettingsDocument& document);
    void write(SettingsDocument& document) const;
};

struct Profile {
    std::wstring id;
    std::wstring name;
    HKL layout = nullptr;
    bool ime = false;
    bool supports_chinese = false;
    bool selectable = true;
    bool operator==(const Profile&) const = default;
};

struct Foreground {
    HWND window = nullptr;
    HWND focus = nullptr;
    DWORD thread = 0;
    DWORD process = 0;
    std::wstring path;
};

struct Status {
    std::string key = "settings.input_method.disabled";
    std::wstring path;
    DWORD error = ERROR_SUCCESS;
    bool operator==(const Status&) const = default;
};

std::wstring normalize_path(std::wstring_view path);
bool validate(const Settings& settings);
const Rule* find_rule(const Settings& settings, std::wstring_view normalized_path);
const Profile* find_profile(const std::vector<Profile>& profiles, std::wstring_view id);
std::optional<State> desired_state(
    const Settings& settings, const History& history, std::wstring_view normalized_path);

} // namespace simpilot::input_method
