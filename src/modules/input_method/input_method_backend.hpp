#pragma once

#include "input_method_model.hpp"

#include <memory>

namespace simpilot::input_method {

class IBackend {
public:
    virtual ~IBackend() = default;
    virtual std::vector<Profile> enumerate() = 0;
    virtual Foreground foreground() = 0;
    virtual std::optional<State> read(
        const Foreground& target, const std::vector<Profile>& profiles) = 0;
    virtual bool request_profile(const Foreground& target, const Profile& profile) = 0;
    virtual bool request_mode(const Foreground& target, const Profile& profile, Mode mode) = 0;
    virtual DWORD last_error() const noexcept = 0;
};

std::unique_ptr<IBackend> make_backend();

} // namespace simpilot::input_method
