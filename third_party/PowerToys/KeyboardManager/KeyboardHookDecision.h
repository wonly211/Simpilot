#pragma once

// Extracted unchanged from KeyboardManagerState.h in Microsoft PowerToys.
namespace Helpers
{
    // Enum type to store possible decision for input in the low level hook
    enum class KeyboardHookDecision
    {
        ContinueExec,
        Suppress,
        SkipHook
    };
}
