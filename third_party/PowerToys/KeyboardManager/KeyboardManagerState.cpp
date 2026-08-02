#include "KeyboardManagerState.h"

#include <algorithm>

void KeyboardManagerState::SetRecording(const bool recording) noexcept
{
    recording_ = recording;
    completed_ = false;
    detectedShortcut_.Reset();
    currentShortcut_.Reset();
    completedShortcut_.Reset();
    pressedKeys_.fill(false);
}

void KeyboardManagerState::SelectDetectedShortcut(const DWORD key) noexcept
{
    // PowerToys SelectDetectedShortcut stores the last shortcut displayed by
    // the UI separately from the live pressed-key buffer.
    if (detectedShortcut_.SetKey(key))
    {
        currentShortcut_ = detectedShortcut_;
    }
}

void KeyboardManagerState::ResetDetectedShortcutKey(const DWORD key) noexcept
{
    // This is the same modifier-release rule used by PowerToys.
    if (Shortcut::IsModifier(key))
    {
        detectedShortcut_.ResetKey(key);
    }
}

bool KeyboardManagerState::AllKeysReleased() const noexcept
{
    return std::ranges::none_of(pressedKeys_, [](const bool pressed) {
        return pressed;
    });
}

Helpers::KeyboardHookDecision KeyboardManagerState::DetectShortcutUIBackend(
    LowlevelKeyboardEvent* data) noexcept
{
    if (recording_)
    {
        const auto key = data->lParam->vkCode;
        if (data->wParam == WM_KEYDOWN || data->wParam == WM_SYSKEYDOWN)
        {
            if (key < pressedKeys_.size()) pressedKeys_[key] = true;
            SelectDetectedShortcut(key);
        }
        else if (data->wParam == WM_KEYUP || data->wParam == WM_SYSKEYUP)
        {
            if (key < pressedKeys_.size()) pressedKeys_[key] = false;
            ResetDetectedShortcutKey(key);
            if (currentShortcut_.actionKey != 0 && AllKeysReleased())
            {
                completedShortcut_ = currentShortcut_;
                completed_ = true;
                recording_ = false;
                detectedShortcut_.Reset();
            }
        }

        // PowerToys returns Suppress for every keyboard message while its
        // detect-shortcut UI is active.
        return Helpers::KeyboardHookDecision::Suppress;
    }

    if (!detectedShortcut_.IsEmpty())
    {
        detectedShortcut_.Reset();
    }
    return Helpers::KeyboardHookDecision::ContinueExec;
}

bool KeyboardManagerState::TakeCompletedShortcut(Shortcut& shortcut) noexcept
{
    if (!completed_) return false;
    shortcut = completedShortcut_;
    completed_ = false;
    completedShortcut_.Reset();
    currentShortcut_.Reset();
    pressedKeys_.fill(false);
    return true;
}
