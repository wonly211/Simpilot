#include "Shortcut.h"

// The method bodies below are copied from Microsoft PowerToys' Shortcut.cpp,
// with only references to PowerToys-only VK_WIN_BOTH and unused chord state
// removed. Copyright and license: ../LICENSE.txt.

bool Shortcut::IsEmpty() const
{
    return winKey == ModifierKey::Disabled && ctrlKey == ModifierKey::Disabled &&
           altKey == ModifierKey::Disabled && shiftKey == ModifierKey::Disabled &&
           actionKey == 0;
}

void Shortcut::Reset()
{
    winKey = ModifierKey::Disabled;
    ctrlKey = ModifierKey::Disabled;
    altKey = ModifierKey::Disabled;
    shiftKey = ModifierKey::Disabled;
    actionKey = {};
}

// Function to set a key in the shortcut based on the passed key code argument.
// Returns false if it is already set to the same value.
bool Shortcut::SetKey(const DWORD input)
{
    if (input == VK_LWIN)
    {
        if (winKey == ModifierKey::Left)
        {
            return false;
        }
        winKey = ModifierKey::Left;
    }
    else if (input == VK_RWIN)
    {
        if (winKey == ModifierKey::Right)
        {
            return false;
        }
        winKey = ModifierKey::Right;
    }
    else if (input == VK_LCONTROL)
    {
        if (ctrlKey == ModifierKey::Left)
        {
            return false;
        }
        ctrlKey = ModifierKey::Left;
    }
    else if (input == VK_RCONTROL)
    {
        if (ctrlKey == ModifierKey::Right)
        {
            return false;
        }
        ctrlKey = ModifierKey::Right;
    }
    else if (input == VK_CONTROL)
    {
        if (ctrlKey == ModifierKey::Both)
        {
            return false;
        }
        ctrlKey = ModifierKey::Both;
    }
    else if (input == VK_LMENU)
    {
        if (altKey == ModifierKey::Left)
        {
            return false;
        }
        altKey = ModifierKey::Left;
    }
    else if (input == VK_RMENU)
    {
        if (altKey == ModifierKey::Right)
        {
            return false;
        }
        altKey = ModifierKey::Right;
    }
    else if (input == VK_MENU)
    {
        if (altKey == ModifierKey::Both)
        {
            return false;
        }
        altKey = ModifierKey::Both;
    }
    else if (input == VK_LSHIFT)
    {
        if (shiftKey == ModifierKey::Left)
        {
            return false;
        }
        shiftKey = ModifierKey::Left;
    }
    else if (input == VK_RSHIFT)
    {
        if (shiftKey == ModifierKey::Right)
        {
            return false;
        }
        shiftKey = ModifierKey::Right;
    }
    else if (input == VK_SHIFT)
    {
        if (shiftKey == ModifierKey::Both)
        {
            return false;
        }
        shiftKey = ModifierKey::Both;
    }
    else
    {
        if (actionKey == input)
        {
            return false;
        }
        actionKey = input;
    }

    return true;
}

// Function to reset the state of a shortcut key based on the passed key code.
void Shortcut::ResetKey(const DWORD input)
{
    if (input == VK_LWIN || input == VK_RWIN)
    {
        winKey = ModifierKey::Disabled;
    }
    else if (input == VK_LCONTROL || input == VK_RCONTROL || input == VK_CONTROL)
    {
        ctrlKey = ModifierKey::Disabled;
    }
    else if (input == VK_LMENU || input == VK_RMENU || input == VK_MENU)
    {
        altKey = ModifierKey::Disabled;
    }
    else if (input == VK_LSHIFT || input == VK_RSHIFT || input == VK_SHIFT)
    {
        shiftKey = ModifierKey::Disabled;
    }

    actionKey = {};
}

bool Shortcut::IsModifier(const DWORD input)
{
    auto shortcut = Shortcut();
    shortcut.SetKey(input);
    return shortcut.actionKey == 0;
}
