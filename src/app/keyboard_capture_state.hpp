#pragma once

#include "simpilot/keyboard_mapping.hpp"
#include "simpilot/hotkey.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <optional>

namespace simpilot {

// The recorder has one public state machine for the legacy RegisterHotKey
// format and for the physical-key mapping editor.  The latter keeps scan-code
// information so a left/right or extended key is not silently collapsed.
enum class CaptureMode {
    legacy,
    mapping_trigger,
    mapping_output,
};

struct CaptureCompletion {
    CaptureMode mode = CaptureMode::legacy;
    std::uint64_t session = 0;
    HotKeyGesture legacy_gesture{};
    KeyboardTrigger trigger{};
    KeyboardOutput output{};

    bool operator==(const CaptureCompletion&) const = default;
};

struct CaptureEventResult {
    CaptureMode mode = CaptureMode::legacy;
    bool suppress = false;
    bool completed = false;
    bool cancelled = false;
    HotKeyGesture legacy_gesture{};
    KeyboardTrigger trigger{};
    KeyboardOutput output{};
};

class KeyboardCaptureState final {
public:
    KeyboardCaptureState() = default;

    KeyboardCaptureState(const KeyboardCaptureState&) = delete;
    KeyboardCaptureState& operator=(const KeyboardCaptureState&) = delete;

    // Starts a fresh recording session.  Starting again while active drops
    // the previous candidate and any unconsumed completion.
    void begin(CaptureMode mode) noexcept;

    // Stops recording and clears all pressed/candidate state.  A completion
    // that has not been consumed is also discarded.
    void end() noexcept;

    // Clears state without changing the monotonically increasing session id.
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] CaptureMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint64_t session() const noexcept { return session_; }

    // Handles one low-level keyboard message.  Only the four keyboard hook
    // message kinds are consumed.  The hook caller can return `suppress`
    // immediately; no waiting or platform calls are performed here.
    [[nodiscard]] CaptureEventResult handle(
        UINT message, const PhysicalKey& key) noexcept;

    // Returns the completion once and removes it from the state.
    [[nodiscard]] std::optional<CaptureCompletion> take_completed() noexcept;

private:
    static constexpr std::size_t max_pressed_keys = 256;

    struct PressedKey {
        PhysicalKey key{};
        bool down = false;
    };

    [[nodiscard]] static bool is_key_message(UINT message) noexcept;
    [[nodiscard]] static bool is_key_down_message(UINT message) noexcept;
    [[nodiscard]] static bool is_key_up_message(UINT message) noexcept;
    [[nodiscard]] static bool is_escape(const PhysicalKey& key) noexcept;
    [[nodiscard]] static bool is_modifier(const PhysicalKey& key) noexcept;
    [[nodiscard]] static UINT modifier_mask(const PhysicalKey& key) noexcept;

    [[nodiscard]] std::size_t find_pressed(const PhysicalKey& key) const noexcept;
    [[nodiscard]] bool add_pressed(const PhysicalKey& key) noexcept;
    void remove_pressed(std::size_t index) noexcept;
    [[nodiscard]] bool all_released() const noexcept;
    [[nodiscard]] bool already_captured(const PhysicalKey& key) const noexcept;

    [[nodiscard]] bool add_modifier(const PhysicalKey& key) noexcept;
    [[nodiscard]] bool add_action(const PhysicalKey& key) noexcept;
    void remove_modifier(const PhysicalKey& key) noexcept;
    [[nodiscard]] CaptureCompletion make_completion() const noexcept;
    [[nodiscard]] CaptureEventResult complete() noexcept;
    [[nodiscard]] CaptureEventResult cancel() noexcept;

    bool active_ = false;
    CaptureMode mode_ = CaptureMode::legacy;
    std::uint64_t session_ = 0;

    std::array<PressedKey, max_pressed_keys> pressed_{};
    std::size_t pressed_count_ = 0;

    // The legacy gesture stores only modifier classes and one action key.  The
    // physical mapping candidates retain up to four modifiers and, for a
    // trigger, one optional second action key.
    HotKeyGesture legacy_candidate_{};
    KeyboardTrigger trigger_candidate_{};
    KeyboardOutput output_candidate_{};
    std::optional<PhysicalKey> standalone_modifier_candidate_;
    bool has_action_ = false;
    bool invalid_candidate_ = false;
    UINT live_modifier_mask_ = 0;
    std::array<PhysicalKey, 4> live_modifiers_{};
    std::size_t live_modifier_count_ = 0;

    std::optional<CaptureCompletion> completed_;
};

} // namespace simpilot
