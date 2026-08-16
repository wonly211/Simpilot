#pragma once

#include <chrono>

namespace simpilot {

struct MouseShakeDetectorOptions final {
    int minimum_sample_distance = 6;
    int minimum_total_distance = 200;
    unsigned int required_reversals = 4;
    std::chrono::milliseconds maximum_sequence_duration{700};
    std::chrono::milliseconds maximum_sample_interval{200};
    std::chrono::milliseconds cooldown{1000};
};

class MouseShakeDetector final {
public:
    using Clock = std::chrono::steady_clock;

    explicit MouseShakeDetector(MouseShakeDetectorOptions options = {}) noexcept;

    [[nodiscard]] bool update(int x, int y, Clock::time_point now) noexcept;
    void reset() noexcept;

private:
    void begin_sequence(int x, int y, Clock::time_point now) noexcept;

    MouseShakeDetectorOptions options_;
    bool has_sample_ = false;
    bool has_direction_ = false;
    int last_x_ = 0;
    int last_y_ = 0;
    int last_dx_ = 0;
    int last_dy_ = 0;
    int total_distance_ = 0;
    unsigned int reversals_ = 0;
    Clock::time_point sequence_started_{};
    Clock::time_point last_sampled_{};
    Clock::time_point cooldown_until_{};
};

} // namespace simpilot
