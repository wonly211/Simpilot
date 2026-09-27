#pragma once

#include <Windows.h>
#include <commctrl.h>
#include <initializer_list>

namespace simpilot {

// A clipped native form body. Commands still belong to the original dialog;
// footer controls remain siblings and never scroll with the fields.
class ScrollableForm final {
public:
    ~ScrollableForm();
    void attach(HWND owner, std::initializer_list<HWND> fixed = {});
    void layout(int width, int height, int content_width, int content_height);
    void reveal(HWND control);
private:
    void arrange();
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK focus_procedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    HWND owner_ = nullptr, viewport_ = nullptr, content_ = nullptr;
    int width_ = 0, height_ = 0, x_ = 0, y_ = 0, wheel_ = 0;
};

} // namespace simpilot
