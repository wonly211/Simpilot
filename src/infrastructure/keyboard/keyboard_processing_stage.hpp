#pragma once

#include <Windows.h>
#include <string>

namespace simpilot {

inline constexpr ULONG_PTR keyboard_target_injected_marker = 0x53494D50;
inline constexpr ULONG_PTR keyboard_replay_injected_marker = 0x53494D52;

enum class MappingEventDecision { pass, suppress };
struct MappingEventResult {
    MappingEventDecision decision = MappingEventDecision::pass;
    bool diagnostic = false;
};

// Called only on the hook thread. handle() must not allocate, lock or wait.
class IKeyboardProcessingStage {
public:
    virtual ~IKeyboardProcessingStage() = default;
    virtual MappingEventResult handle(UINT message, const KBDLLHOOKSTRUCT& event) noexcept = 0;
    virtual void on_timer() noexcept = 0;
    virtual void set_foreground_process(std::wstring process_name) noexcept = 0;
    virtual void reset(bool replay_pending) noexcept = 0;
    virtual bool consume_diagnostic() noexcept = 0;
};

} // namespace simpilot
