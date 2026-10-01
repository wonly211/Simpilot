#pragma once

#include "input_method_backend.hpp"

namespace simpilot::input_method::test {

class Backend final : public IBackend {
public:
    std::vector<Profile> enumerate() override { ++enumerations; return catalog; }
    Foreground foreground() override { ++polls; return active; }
    std::optional<State> read(const Foreground& target, const std::vector<Profile>&) override {
        ++reads;
        const auto found = state.find(normalize_path(target.path));
        return found == state.end() ? std::nullopt : std::optional(found->second);
    }
    bool request_profile(const Foreground& target, const Profile& profile) override {
        ++profile_requests;
        if (fail) return false;
        if (!reject) state[normalize_path(target.path)].profile_id = profile.id;
        return true;
    }
    bool request_mode(const Foreground& target, const Profile&, Mode mode) override {
        ++mode_requests;
        if (fail) return false;
        if (!reject) state[normalize_path(target.path)].mode = mode;
        return true;
    }
    DWORD last_error() const noexcept override { return error; }
    void activate(const wchar_t* path, int window = 1) {
        active = {reinterpret_cast<HWND>(static_cast<INT_PTR>(window)),
            reinterpret_cast<HWND>(static_cast<INT_PTR>(window)), 1, 1, path};
    }
    std::vector<Profile> catalog{
        {L"layout:00000409", L"English (US)", reinterpret_cast<HKL>(1), false, false, true},
        {L"tsf:pinyin", L"Microsoft Pinyin", reinterpret_cast<HKL>(2), true, true, true}};
    Foreground active;
    std::map<std::wstring, State> state;
    bool fail = false, reject = false;
    DWORD error = ERROR_ACCESS_DENIED;
    int enumerations = 0, polls = 0, reads = 0, profile_requests = 0, mode_requests = 0;
};

} // namespace simpilot::input_method::test
