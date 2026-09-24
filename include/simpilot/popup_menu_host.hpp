#pragma once
#include <Windows.h>
#include <functional>
#include <utility>

namespace simpilot {
// Coordinates nested Win32 menu loops across independent UI owners.
class PopupMenuHost final {
public:
    bool active() const noexcept { return active_; }
    bool defer_until_closed(std::function<void()> callback) {
        if (!active_) return false;
        deferred_ = std::move(callback);
        EndMenu();
        return true;
    }
    UINT track(HWND owner, HMENU menu, UINT flags, POINT point,
               const std::function<void()>& cleanup = {}) {
        if (active_) { DestroyMenu(menu); return 0; }
        active_ = true;
        SetForegroundWindow(owner);
        const auto selected = TrackPopupMenu(menu,
            flags | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            point.x, point.y, 0, owner, nullptr);
        DestroyMenu(menu);
        PostMessageW(owner, WM_NULL, 0, 0);
        active_ = false;
        if (cleanup) cleanup();
        if (auto pending = std::exchange(deferred_, {})) {
            pending();
            return 0;
        }
        return selected;
    }
private:
    bool active_ = false;
    std::function<void()> deferred_;
};
} // namespace simpilot
