#pragma once

// This dependency-free subset of PowerToys Keyboard Manager's Shortcut class
// preserves the original modifier/action-key state model. WinUI, chord and
// remapping members that Simpilot's recorder does not use are intentionally
// omitted; see docs/powertoys-keyboard-recorder.md.

#include "ModifierKey.h"

#include <Windows.h>

class Shortcut
{
public:
    ModifierKey winKey = ModifierKey::Disabled;
    ModifierKey ctrlKey = ModifierKey::Disabled;
    ModifierKey altKey = ModifierKey::Disabled;
    ModifierKey shiftKey = ModifierKey::Disabled;
    DWORD actionKey = {};

    Shortcut() = default;

    bool IsEmpty() const;
    void Reset();
    static bool IsModifier(DWORD input);
    bool SetKey(DWORD input);
    void ResetKey(DWORD input);
};
