#include "program_selection_dialog.hpp"

#include <Windows.h>
#include <commctrl.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr auto dialog_class_name = L"Simpilot.ProgramSelectionDialog";
constexpr int candidate_list_identifier = 100;

} // namespace

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-program-selection-dialog-test-"
           + std::to_wstring(GetCurrentProcessId()));
    const auto first = root / L"first" / L"candidate-icon.exe";
    const auto second = root / L"second" / L"candidate-icon.exe";
    const auto third = root / L"third" / L"candidate-icon.exe";
    for (const auto& path : {first, second, third}) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path).put('\0');
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    const std::vector<simpilot::ProgramCandidate> candidates{
        {first, 3, now}, {second, 2, now}, {third, 1, now}};

    const auto main_thread = GetCurrentThreadId();
    std::atomic ui_verified = false;
    std::thread automation([main_thread, &ui_verified] {
        HWND dialog = nullptr;
        for (int attempt = 0; attempt < 200 && !dialog; ++attempt) {
            dialog = FindWindowW(dialog_class_name, nullptr);
            if (!dialog) Sleep(25);
        }
        if (!dialog) {
            PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
            return;
        }
        const auto list = GetDlgItem(dialog, candidate_list_identifier);
        const auto header = list ? ListView_GetHeader(list) : nullptr;
        auto valid = list && header
            && Header_GetItemCount(header) == 3
            && ListView_GetItemCount(list) == 3
            && ListView_GetImageList(list, LVSIL_SMALL) != nullptr
            && SendMessageW(dialog, WM_GETICON, ICON_BIG, 0) != 0
            && SendMessageW(dialog, WM_GETICON, ICON_SMALL, 0) != 0;
        for (int row = 0; valid && row < 3; ++row) {
            LVITEMW item{.mask = LVIF_IMAGE, .iItem = row};
            valid = ListView_GetItem(list, &item) != FALSE && item.iImage >= 0;
        }
        ui_verified = valid;
        if (!valid) {
            PostMessageW(dialog, WM_CLOSE, 0, 0);
            return;
        }
        ListView_SetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    });

    const auto selected = simpilot::ProgramSelectionDialog::show_modal(
        GetModuleHandleW(nullptr), nullptr, simpilot::UiLanguage::simplified_chinese,
        L"candidate-icon.exe", candidates);
    automation.join();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (!ui_verified || !selected || *selected != second) {
        std::cerr << "Program selection dialog test failed\n";
        return 1;
    }
    std::cout << "Program selection dialog test passed\n";
    return 0;
}
