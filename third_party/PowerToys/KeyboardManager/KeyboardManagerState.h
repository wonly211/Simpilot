#pragma once

#include "KeyboardHookDecision.h"
#include "LowlevelKeyboardEvent.h"
#include "Shortcut.h"

#include <array>

// Dependency-free adaptation of the recording portion of PowerToys'
// KeyboardManagerState. The DetectShortcutUIBackend decision flow and the
// SelectDetectedShortcut/ResetDetectedShortcutKey behavior are retained.
class KeyboardManagerState
{
public:
    void SetRecording(bool recording) noexcept;
    Helpers::KeyboardHookDecision DetectShortcutUIBackend(
        LowlevelKeyboardEvent* data) noexcept;
    bool TakeCompletedShortcut(Shortcut& shortcut) noexcept;

private:
    void SelectDetectedShortcut(DWORD key) noexcept;
    void ResetDetectedShortcutKey(DWORD key) noexcept;
    bool AllKeysReleased() const noexcept;

    bool recording_ = false;
    bool completed_ = false;
    Shortcut detectedShortcut_;
    Shortcut currentShortcut_;
    Shortcut completedShortcut_;
    std::array<bool, 256> pressedKeys_{};
};
