#include "simpilot/mouse_shake_detector.hpp"

#include <algorithm>
#include <cstdlib>

namespace simpilot {
namespace {

int approximate_distance(const int dx, const int dy) noexcept {
    const auto horizontal = std::abs(dx);
    const auto vertical = std::abs(dy);
    return std::max(horizontal, vertical) + std::min(horizontal, vertical) / 2;
}

bool reverses_direction(const int previous_x, const int previous_y,
                        const int current_x, const int current_y) noexcept {
    const auto dot = static_cast<long double>(previous_x) * current_x
        + static_cast<long double>(previous_y) * current_y;
    if (dot >= 0) return false;
    const auto previous_length = static_cast<long double>(previous_x) * previous_x
        + static_cast<long double>(previous_y) * previous_y;
    const auto current_length = static_cast<long double>(current_x) * current_x
        + static_cast<long double>(current_y) * current_y;
    return 4.0L * dot * dot >= previous_length * current_length;
}

} // namespace

MouseShakeDetector::MouseShakeDetector(MouseShakeDetectorOptions options) noexcept
    : options_(options) {}

bool MouseShakeDetector::update(const int x, const int y,
                                const Clock::time_point now) noexcept {
    if (!has_sample_) {
        begin_sequence(x, y, now);
        return false;
    }
    if (now < cooldown_until_) {
        begin_sequence(x, y, now);
        return false;
    }
    if (now < last_sampled_
        || now - last_sampled_ > options_.maximum_sample_interval
        || now - sequence_started_ > options_.maximum_sequence_duration) {
        begin_sequence(x, y, now);
        return false;
    }

    const auto dx = x - last_x_;
    const auto dy = y - last_y_;
    const auto distance = approximate_distance(dx, dy);
    if (distance < options_.minimum_sample_distance) return false;

    total_distance_ += distance;
    if (has_direction_ && reverses_direction(last_dx_, last_dy_, dx, dy)) {
        ++reversals_;
    }
    last_x_ = x;
    last_y_ = y;
    last_dx_ = dx;
    last_dy_ = dy;
    last_sampled_ = now;
    has_direction_ = true;

    if (reversals_ < options_.required_reversals
        || total_distance_ < options_.minimum_total_distance) {
        return false;
    }

    cooldown_until_ = now + options_.cooldown;
    begin_sequence(x, y, now);
    return true;
}

void MouseShakeDetector::reset() noexcept {
    has_sample_ = false;
    has_direction_ = false;
    total_distance_ = 0;
    reversals_ = 0;
    cooldown_until_ = {};
}

void MouseShakeDetector::begin_sequence(
    const int x, const int y, const Clock::time_point now) noexcept {
    has_sample_ = true;
    has_direction_ = false;
    last_x_ = x;
    last_y_ = y;
    last_dx_ = 0;
    last_dy_ = 0;
    total_distance_ = 0;
    reversals_ = 0;
    sequence_started_ = now;
    last_sampled_ = now;
}

} // namespace simpilot
