#pragma once

#include "simpilot/keyboard_mapping.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace simpilot {

enum class MappingEventDecision {
    pass,
    suppress,
};

struct MappingEventResult {
    MappingEventDecision decision = MappingEventDecision::pass;
    bool diagnostic = false;
};

class KeyboardMappingEngine final {
public:
    using InputSink = std::function<UINT(const INPUT*, UINT)>;
    using Clock = std::function<std::uint64_t()>;

    static constexpr ULONG_PTR target_injected_marker = 0x53494D50;
    static constexpr ULONG_PTR replay_injected_marker = 0x53494D52;
    static constexpr UINT pending_timer_id = 0x5A71;
    static constexpr std::uint64_t pending_timeout_ms = 250;

    KeyboardMappingEngine();
    explicit KeyboardMappingEngine(InputSink input_sink, Clock clock = {});

    KeyboardMappingEngine(const KeyboardMappingEngine&) = delete;
    KeyboardMappingEngine& operator=(const KeyboardMappingEngine&) = delete;

    [[nodiscard]] bool replace_rules(
        bool enabled, const std::vector<KeyboardMappingRule>& rules) noexcept;
    [[nodiscard]] MappingEventResult handle(
        UINT message, const KBDLLHOOKSTRUCT& event) noexcept;
    void on_timer() noexcept;
    void set_foreground_process(std::wstring process_name) noexcept;
    void reset(bool replay_pending) noexcept;
    [[nodiscard]] bool pending() const noexcept { return pending_count_ != 0; }
    [[nodiscard]] bool consume_diagnostic() noexcept;

private:
    struct CompiledRule {
        KeyboardMappingRule rule;
        std::wstring process_name;
    };

    struct PressedKey {
        PhysicalKey key{};
        bool down = false;
    };

    struct PendingEvent {
        UINT message = 0;
        KBDLLHOOKSTRUCT event{};
    };

    struct ActiveMapping {
        std::size_t rule_index = 0;
        std::array<PhysicalKey, 6> source_keys{};
        std::size_t source_count = 0;
        bool active = false;
    };

    [[nodiscard]] static bool is_key_down(UINT message) noexcept;
    [[nodiscard]] static bool is_key_up(UINT message) noexcept;
    [[nodiscard]] static PhysicalKey physical_key(
        const KBDLLHOOKSTRUCT& event) noexcept;
    [[nodiscard]] static bool same_key(
        const PhysicalKey& left, const PhysicalKey& right) noexcept;
    [[nodiscard]] static bool rule_precedes(
        const CompiledRule& candidate,
        const CompiledRule& current) noexcept;
    [[nodiscard]] bool pressed_contains(const PhysicalKey& key) const noexcept;
    [[nodiscard]] bool all_pressed_are_source(
        const KeyboardTrigger& trigger) const noexcept;
    [[nodiscard]] bool matches_rule(
        const CompiledRule& rule, bool require_complete) const noexcept;
    [[nodiscard]] bool is_prefix(const CompiledRule& rule) const noexcept;
    [[nodiscard]] const CompiledRule* select_match(
        bool require_complete) const noexcept;
    [[nodiscard]] const CompiledRule* select_prefix() const noexcept;
    [[nodiscard]] bool append_pending(
        UINT message, const KBDLLHOOKSTRUCT& event) noexcept;
    [[nodiscard]] bool pending_contains_key(
        const PhysicalKey& key) const noexcept;
    [[nodiscard]] bool pending_has_key_up() const noexcept;
    void clear_pending() noexcept;
    [[nodiscard]] bool replay_pending() noexcept;
    [[nodiscard]] bool send_output_down(const KeyboardOutput& output) noexcept;
    [[nodiscard]] bool send_output_up(const KeyboardOutput& output) noexcept;
    [[nodiscard]] bool send_output_tap(const KeyboardOutput& output) noexcept;
    [[nodiscard]] bool send_single(
        const PhysicalKey& key, bool key_down, ULONG_PTR marker) noexcept;
    [[nodiscard]] bool send_events(
        INPUT* inputs, UINT count) noexcept;
    [[nodiscard]] bool activate(const CompiledRule& rule) noexcept;
    [[nodiscard]] bool belongs_to_active(const PhysicalKey& key) const noexcept;
    [[nodiscard]] bool active_released() const noexcept;
    void clear_pressed() noexcept;
    void mark_pressed(const PhysicalKey& key, bool down) noexcept;
    void mark_diagnostic() noexcept { diagnostic_ = true; }

    InputSink input_sink_;
    Clock clock_;
    bool enabled_ = false;
    std::wstring foreground_process_;
    std::vector<CompiledRule> rules_;
    std::array<PressedKey, 32> pressed_{};
    std::size_t pressed_count_ = 0;
    std::array<PendingEvent, 16> pending_events_{};
    std::size_t pending_count_ = 0;
    std::uint64_t pending_deadline_ = 0;
    std::optional<std::size_t> pending_modifier_rule_index_;
    std::array<ActiveMapping, 8> active_mappings_{};
    bool diagnostic_ = false;
};

} // namespace simpilot
