#include "about_window.hpp"

#include "simpilot/localization.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(length > 0 && length < path.size(), "Resolve test executable directory");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

std::wstring control_text(const HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

struct ChildSummary {
    int links = 0;
    bool product_name = false;
    bool version = false;
    bool close = false;
    std::vector<RECT> rectangles;
};

BOOL CALLBACK inspect_child(const HWND control, const LPARAM parameter) {
    auto& summary = *reinterpret_cast<ChildSummary*>(parameter);
    wchar_t class_name[32]{};
    GetClassNameW(control, class_name, static_cast<int>(std::size(class_name)));
    const auto value = control_text(control);
    if (_wcsicmp(class_name, L"SysLink") == 0) ++summary.links;
    if (value == L"简驭 | Simpilot") summary.product_name = true;
    if (value == L"Version 0.17.1" || value == L"版本 0.17.1") summary.version = true;
    if (value == L"Close" || value == L"关闭" || value == L"關閉") summary.close = true;
    RECT rectangle{};
    if (GetWindowRect(control, &rectangle)) summary.rectangles.push_back(rectangle);
    return TRUE;
}

bool controls_fit_without_overlap(const HWND window,
                                  const std::vector<RECT>& screen_rectangles) {
    RECT client{};
    GetClientRect(window, &client);
    POINT origin{};
    ClientToScreen(window, &origin);
    std::vector<RECT> rectangles;
    rectangles.reserve(screen_rectangles.size());
    for (auto rectangle : screen_rectangles) {
        OffsetRect(&rectangle, -origin.x, -origin.y);
        if (rectangle.left < client.left || rectangle.top < client.top
            || rectangle.right > client.right || rectangle.bottom > client.bottom) {
            return false;
        }
        rectangles.push_back(rectangle);
    }
    for (std::size_t first = 0; first < rectangles.size(); ++first) {
        for (auto second = first + 1; second < rectangles.size(); ++second) {
            RECT intersection{};
            if (IntersectRect(&intersection, &rectangles[first], &rectangles[second])
                && intersection.right > intersection.left
                && intersection.bottom > intersection.top) {
                return false;
            }
        }
    }
    return true;
}

bool scrolling_reaches_close_button(const HWND window) {
    RECT window_rectangle{};
    if (!GetWindowRect(window, &window_rectangle)) return false;
    const auto dpi = GetDpiForWindow(window);
    SetWindowPos(window, nullptr, window_rectangle.left, window_rectangle.top,
                 window_rectangle.right - window_rectangle.left,
                 MulDiv(260, dpi, 96), SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(window, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
    const auto close = GetDlgItem(window, IDOK);
    RECT close_rectangle{};
    RECT client{};
    if (!close || !GetWindowRect(close, &close_rectangle)
        || !GetClientRect(window, &client)) {
        return false;
    }
    POINT origin{};
    ClientToScreen(window, &origin);
    OffsetRect(&close_rectangle, -origin.x, -origin.y);
    return close_rectangle.top >= client.top
        && close_rectangle.bottom <= client.bottom;
}

bool capture_window(const HWND window, const std::filesystem::path& destination) {
    RECT rectangle{};
    if (!GetWindowRect(window, &rectangle)) {
        std::cerr << "Snapshot GetWindowRect failed\n";
        return false;
    }
    const auto width = rectangle.right - rectangle.left;
    const auto height = rectangle.bottom - rectangle.top;
    const auto window_dc = GetWindowDC(window);
    if (!window_dc) {
        std::cerr << "Snapshot GetWindowDC failed\n";
        return false;
    }
    const auto memory_dc = CreateCompatibleDC(window_dc);
    const auto bitmap = CreateCompatibleBitmap(window_dc, width, height);
    if (!memory_dc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory_dc) DeleteDC(memory_dc);
        ReleaseDC(window, window_dc);
        std::cerr << "Snapshot compatible GDI object creation failed\n";
        return false;
    }
    const auto previous = SelectObject(memory_dc, bitmap);
    constexpr UINT render_full_content = 0x00000002;
    auto rendered = PrintWindow(window, memory_dc, render_full_content) != FALSE;
    if (!rendered) {
        constexpr LPARAM print_flags = PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN
            | PRF_ERASEBKGND | PRF_OWNED;
        SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(memory_dc), print_flags);
        rendered = true;
    }
    BITMAPINFO information{};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = width;
    information.bmiHeader.biHeight = -height;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    const auto row_size = static_cast<std::size_t>(width) * 4;
    std::vector<std::byte> pixels(row_size * static_cast<std::size_t>(height));
    SelectObject(memory_dc, previous);
    const auto copied = GetDIBits(window_dc, bitmap, 0, static_cast<UINT>(height),
                                  pixels.data(), &information, DIB_RGB_COLORS) != 0;
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(window, window_dc);
    if (!rendered || !copied) {
        std::cerr << "Snapshot rendering failed rendered=" << rendered
                  << " copied=" << copied << '\n';
        return false;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits
        + static_cast<DWORD>(pixels.size());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&information.bmiHeader),
                 sizeof(information.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size()));
    if (!output.good()) std::cerr << "Snapshot file write failed\n";
    return output.good();
}

std::wstring version_value(const std::filesystem::path& executable,
                           const wchar_t* name) {
    DWORD ignored = 0;
    const auto size = GetFileVersionInfoSizeW(executable.c_str(), &ignored);
    require(size != 0, "Simpilot.exe has a version resource");
    std::vector<std::byte> data(size);
    require(GetFileVersionInfoW(executable.c_str(), 0, size, data.data()) != FALSE,
            "Read Simpilot.exe version resource");
    const auto query = std::wstring(L"\\StringFileInfo\\080404B0\\") + name;
    wchar_t* value = nullptr;
    UINT length = 0;
    require(VerQueryValueW(data.data(), query.c_str(),
                           reinterpret_cast<void**>(&value), &length) != FALSE,
            "Read requested version string");
    return value ? std::wstring(value) : std::wstring{};
}

} // namespace

int wmain(const int argument_count, wchar_t** arguments) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto directory = executable_directory();
        const auto product = directory / L"Simpilot.exe";
        require(version_value(product, L"ProductName") == L"简驭 | Simpilot",
                "Product name matches the About window");
        require(version_value(product, L"FileVersion") == L"0.17.1.0",
                "File version matches the project version");
        require(version_value(product, L"ProductVersion") == L"0.17.1.0",
                "Product version matches the project version");
        const auto snapshot_path = argument_count > 2
            && _wcsicmp(arguments[1], L"--snapshot") == 0
            ? std::filesystem::path(arguments[2]) : std::filesystem::path{};
        auto snapshot_language = simpilot::UiLanguage::simplified_chinese;
        if (!snapshot_path.empty() && argument_count > 3) {
            snapshot_language = _wcsicmp(arguments[3], L"en-US") == 0
                ? simpilot::UiLanguage::english
                : _wcsicmp(arguments[3], L"zh-TW") == 0
                    ? simpilot::UiLanguage::traditional_chinese
                    : simpilot::UiLanguage::simplified_chinese;
        }

        ChildSummary summary;
        std::atomic<bool> found{false};
        std::atomic<bool> icons_present{false};
        std::atomic<bool> layout_valid{false};
        std::atomic<bool> scrolling_valid{false};
        std::atomic<bool> snapshot_created{false};
        std::jthread inspector([&] {
            for (int attempt = 0; attempt < 200; ++attempt) {
                if (const auto window = FindWindowW(simpilot::about_window_class_name, nullptr)) {
                    const auto small_icon = reinterpret_cast<HICON>(
                        SendMessageW(window, WM_GETICON, ICON_SMALL, 0));
                    const auto large_icon = reinterpret_cast<HICON>(
                        SendMessageW(window, WM_GETICON, ICON_BIG, 0));
                    if (!GetDlgItem(window, IDOK) || !small_icon || !large_icon) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    EnumChildWindows(window, &inspect_child,
                                    reinterpret_cast<LPARAM>(&summary));
                    icons_present = small_icon != nullptr && large_icon != nullptr;
                    layout_valid = controls_fit_without_overlap(
                        window, summary.rectangles);
                    found = true;
                    if (!snapshot_path.empty()) {
                        snapshot_created = capture_window(window, snapshot_path);
                    }
                    scrolling_valid = scrolling_reaches_close_button(window);
                    PostMessageW(window, WM_CLOSE, 0, 0);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        const simpilot::Localization localization(snapshot_path.empty()
            ? simpilot::UiLanguage::english
            : snapshot_language);
        simpilot::AboutWindow::show_modal(
            GetModuleHandleW(nullptr), nullptr, localization, product, L"0.17.1");
        inspector.join();
        require(found, "About window was created");
        require(icons_present, "About window uses Simpilot icons");
        require(layout_valid, "About window controls fit without overlap");
        require(scrolling_valid, "About window scrolls to the Close button");
        if (summary.links != 6) {
            std::cerr << "About window link count: " << summary.links << '\n';
        }
        require(summary.links == 6, "About window exposes six keyboard-accessible links");
        require(summary.product_name, "About window displays the product name");
        require(summary.version, "About window displays the current version");
        require(summary.close, "About window provides a Close button");
        require(snapshot_path.empty() || snapshot_created,
                "About window snapshot was created");
        std::wcout << L"Simpilot About window verified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "About window test failure: " << error.what() << '\n';
        return 1;
    }
}
