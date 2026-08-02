#pragma once

#include "simpilot/program_resolver.hpp"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace simpilot {

class EverythingSearch final : public IProgramSearch {
public:
    ~EverythingSearch() override;

    EverythingSearch(const EverythingSearch&) = delete;
    EverythingSearch& operator=(const EverythingSearch&) = delete;

    [[nodiscard]] static std::unique_ptr<EverythingSearch> try_create(
        const std::filesystem::path& library_path);
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::vector<ProgramCandidate> find_exact_file_name(
        const std::wstring& file_name) const override;

private:
    explicit EverythingSearch(HMODULE module);

    using SetSearchFunction = void(WINAPI*)(const wchar_t*);
    using SetBooleanFunction = void(WINAPI*)(BOOL);
    using SetMaximumFunction = void(WINAPI*)(DWORD);
    using QueryFunction = BOOL(WINAPI*)(BOOL);
    using GetCountFunction = DWORD(WINAPI*)();
    using GetFullPathFunction = DWORD(WINAPI*)(DWORD, wchar_t*, DWORD);
    using GetBooleanFunction = BOOL(WINAPI*)();

    HMODULE module_;
    SetSearchFunction set_search_ = nullptr;
    SetBooleanFunction set_match_case_ = nullptr;
    SetBooleanFunction set_match_whole_word_ = nullptr;
    SetMaximumFunction set_maximum_ = nullptr;
    QueryFunction query_ = nullptr;
    GetCountFunction get_file_result_count_ = nullptr;
    GetFullPathFunction get_full_path_ = nullptr;
    GetBooleanFunction is_database_loaded_ = nullptr;
    mutable std::mutex query_mutex_;
};

class EverythingManager final {
public:
    enum class ServiceState {
        not_installed,
        stopped,
        start_pending,
        running,
        paused,
        inaccessible,
        unknown,
    };

    struct ServiceInfo {
        ServiceState state = ServiceState::unknown;
        std::filesystem::path executable_path;
        bool uses_bundled_executable = false;
        DWORD win32_exit_code = ERROR_SUCCESS;
    };

    using DiagnosticSink = std::function<void(std::wstring_view)>;

    explicit EverythingManager(std::filesystem::path directory,
                               DiagnosticSink diagnostic_sink = {});

    [[nodiscard]] bool components_available() const;
    [[nodiscard]] ServiceInfo service_info() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> client_source_path() const noexcept;
    [[nodiscard]] bool request_start(const EverythingSearch& search) const;
    [[nodiscard]] bool repair_service(
        const EverythingSearch& search,
        HWND owner,
        std::chrono::milliseconds timeout = std::chrono::seconds(15)) const;
    [[nodiscard]] bool show_window(HWND owner) const;

private:
    void log(std::wstring_view message) const noexcept;
    [[nodiscard]] bool start_service() const noexcept;
    [[nodiscard]] bool start_client() const;
    [[nodiscard]] bool run_service_install_elevated(HWND owner,
                                                    std::chrono::milliseconds timeout) const;

    std::filesystem::path directory_;
    std::filesystem::path executable_path_;
    std::filesystem::path sdk_path_;
    DiagnosticSink diagnostic_sink_;
};

} // namespace simpilot
