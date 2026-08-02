#include "simpilot/everything.hpp"

#include <shellapi.h>
#include <tlhelp32.h>
#include <winver.h>
#include <winsvc.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace simpilot {
namespace {

constexpr DWORD maximum_results = 4096;

template <typename Function>
Function load_export(const HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    return address ? std::bit_cast<Function>(address) : nullptr;
}

std::wstring escape_regex(const std::wstring& value) {
    static constexpr std::wstring_view special = L"\\.^$|()[]{}*+?";
    std::wstring result;
    result.reserve(value.size() * 2);
    for (const auto character : value) {
        if (special.find(character) != std::wstring_view::npos) result.push_back(L'\\');
        result.push_back(character);
    }
    return result;
}

std::uint64_t file_version(const std::filesystem::path& path) {
    DWORD handle = 0;
    const auto size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return 0;
    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return 0;
    VS_FIXEDFILEINFO* information = nullptr;
    UINT information_size = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&information), &information_size)
        || !information || information_size < sizeof(VS_FIXEDFILEINFO)) {
        return 0;
    }
    return (static_cast<std::uint64_t>(information->dwFileVersionMS) << 32U)
        | information->dwFileVersionLS;
}

std::filesystem::path executable_from_command_line(std::wstring command_line) {
    const auto first = command_line.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    if (command_line[first] == L'\"') {
        const auto end = command_line.find(L'\"', first + 1);
        return end == std::wstring::npos ? std::filesystem::path{}
                                        : std::filesystem::path(command_line.substr(first + 1, end - first - 1));
    }
    const auto end = command_line.find_first_of(L" \t", first);
    return std::filesystem::path(command_line.substr(first, end - first));
}

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) noexcept {
    try {
        return _wcsicmp(std::filesystem::absolute(left).lexically_normal().c_str(),
                        std::filesystem::absolute(right).lexically_normal().c_str()) == 0;
    } catch (...) {
        return false;
    }
}

std::optional<std::filesystem::path> process_path(const DWORD process_id) noexcept {
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return std::nullopt;
    std::wstring buffer(32768, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    const auto queried = QueryFullProcessImageNameW(process, 0, buffer.data(), &size);
    CloseHandle(process);
    if (!queried || size == 0) return std::nullopt;
    buffer.resize(size);
    return std::filesystem::path(std::move(buffer));
}

EverythingManager::ServiceState translate_service_state(const DWORD state) {
    switch (state) {
    case SERVICE_STOPPED: return EverythingManager::ServiceState::stopped;
    case SERVICE_START_PENDING: return EverythingManager::ServiceState::start_pending;
    case SERVICE_RUNNING: return EverythingManager::ServiceState::running;
    case SERVICE_PAUSED:
    case SERVICE_PAUSE_PENDING:
    case SERVICE_CONTINUE_PENDING: return EverythingManager::ServiceState::paused;
    default: return EverythingManager::ServiceState::unknown;
    }
}

std::wstring_view service_state_name(const EverythingManager::ServiceState state) {
    switch (state) {
    case EverythingManager::ServiceState::not_installed: return L"not-installed";
    case EverythingManager::ServiceState::stopped: return L"stopped";
    case EverythingManager::ServiceState::start_pending: return L"start-pending";
    case EverythingManager::ServiceState::running: return L"running";
    case EverythingManager::ServiceState::paused: return L"paused";
    case EverythingManager::ServiceState::inaccessible: return L"inaccessible";
    case EverythingManager::ServiceState::unknown: return L"unknown";
    }
    return L"unknown";
}

} // namespace

EverythingSearch::EverythingSearch(const HMODULE module) : module_(module) {}

EverythingSearch::~EverythingSearch() {
    if (module_) FreeLibrary(module_);
}

std::unique_ptr<EverythingSearch> EverythingSearch::try_create(
    const std::filesystem::path& library_path) {
    if (!std::filesystem::is_regular_file(library_path)) return nullptr;
    const auto module = LoadLibraryW(library_path.c_str());
    if (!module) return nullptr;

    auto result = std::unique_ptr<EverythingSearch>(new EverythingSearch(module));
    result->set_search_ = load_export<SetSearchFunction>(module, "Everything_SetSearchW");
    result->set_match_case_ = load_export<SetBooleanFunction>(module, "Everything_SetMatchCase");
    result->set_match_whole_word_ = load_export<SetBooleanFunction>(module, "Everything_SetMatchWholeWord");
    result->set_maximum_ = load_export<SetMaximumFunction>(module, "Everything_SetMax");
    result->query_ = load_export<QueryFunction>(module, "Everything_QueryW");
    result->get_file_result_count_ = load_export<GetCountFunction>(module, "Everything_GetNumFileResults");
    result->get_full_path_ = load_export<GetFullPathFunction>(module, "Everything_GetResultFullPathNameW");
    result->is_database_loaded_ = load_export<GetBooleanFunction>(module, "Everything_IsDBLoaded");
    if (!result->set_search_ || !result->set_match_case_ || !result->set_match_whole_word_
        || !result->set_maximum_ || !result->query_ || !result->get_file_result_count_
        || !result->get_full_path_ || !result->is_database_loaded_) {
        return nullptr;
    }
    return result;
}

bool EverythingSearch::available() const {
    return module_ && is_database_loaded_ && is_database_loaded_() != FALSE;
}

std::vector<ProgramCandidate> EverythingSearch::find_exact_file_name(
    const std::wstring& file_name) const {
    if (file_name.empty() || !available()) return {};
    std::scoped_lock lock(query_mutex_);
    const auto query_text = L"file: regex:\"^" + escape_regex(file_name) + L"$\"";
    set_match_case_(FALSE);
    set_match_whole_word_(FALSE);
    set_maximum_(maximum_results);
    set_search_(query_text.c_str());
    if (!query_(TRUE)) return {};

    std::vector<ProgramCandidate> results;
    const auto count = std::min(get_file_result_count_(), maximum_results);
    std::vector<wchar_t> path_buffer(32768);
    results.reserve(count);
    for (DWORD index = 0; index < count; ++index) {
        if (get_full_path_(index, path_buffer.data(), static_cast<DWORD>(path_buffer.size())) == 0) continue;
        const std::filesystem::path path(path_buffer.data());
        std::error_code error;
        const auto last_write = std::filesystem::last_write_time(path, error);
        if (error || !std::filesystem::is_regular_file(path, error)) continue;
        results.push_back(ProgramCandidate{path, file_version(path), last_write});
    }
    return results;
}

EverythingManager::EverythingManager(std::filesystem::path directory,
                                     DiagnosticSink diagnostic_sink)
    : directory_(std::filesystem::absolute(std::move(directory))),
      executable_path_(directory_ / L"Everything.exe"),
      sdk_path_(directory_ / L"Everything64.dll"),
      diagnostic_sink_(std::move(diagnostic_sink)) {}

bool EverythingManager::components_available() const {
    return std::filesystem::is_regular_file(executable_path_)
        && std::filesystem::is_regular_file(sdk_path_);
}

EverythingManager::ServiceInfo EverythingManager::service_info() const noexcept {
    ServiceInfo result;
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        result.state = ServiceState::inaccessible;
        result.win32_exit_code = GetLastError();
        return result;
    }
    const auto service = OpenServiceW(manager, L"Everything", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!service) {
        result.win32_exit_code = GetLastError();
        result.state = result.win32_exit_code == ERROR_SERVICE_DOES_NOT_EXIST
            ? ServiceState::not_installed : ServiceState::inaccessible;
        CloseServiceHandle(manager);
        return result;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD required = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                             reinterpret_cast<BYTE*>(&status), sizeof(status), &required)) {
        result.state = translate_service_state(status.dwCurrentState);
        result.win32_exit_code = status.dwWin32ExitCode;
    } else {
        result.state = ServiceState::unknown;
        result.win32_exit_code = GetLastError();
    }

    QueryServiceConfigW(service, nullptr, 0, &required);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && required > 0) {
        std::vector<std::byte> buffer(required);
        auto* configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (QueryServiceConfigW(service, configuration, required, &required)
            && configuration->lpBinaryPathName) {
            result.executable_path = executable_from_command_line(configuration->lpBinaryPathName);
            result.uses_bundled_executable = same_path(result.executable_path, executable_path_);
        }
    }
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return result;
}

std::optional<std::filesystem::path> EverythingManager::client_source_path() const noexcept {
    if (const auto notification_window = FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION", nullptr)) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(notification_window, &process_id);
        if (const auto path = process_path(process_id)) return path;
    }
    if (const auto window = FindWindowW(L"EVERYTHING", nullptr)) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (const auto path = process_path(process_id)) return path;
    }

    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
    DWORD current_session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &current_session);
    std::optional<std::filesystem::path> first;
    PROCESSENTRY32W entry{.dwSize = sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Everything.exe") != 0) continue;
            DWORD session = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &session) || session != current_session) continue;
            const auto path = process_path(entry.th32ProcessID);
            if (!path) continue;
            if (same_path(*path, executable_path_)) {
                CloseHandle(snapshot);
                return path;
            }
            if (!first) first = path;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return first;
}

bool EverythingManager::request_start(const EverythingSearch& search) const {
    if (search.available()) {
        const auto service = service_info();
        log(std::format(L"everything service state={} managed={} path={}",
            service_state_name(service.state), service.uses_bundled_executable ? 1 : 0,
            service.executable_path.empty() ? L"unknown" : service.executable_path.wstring()));
        const auto source = client_source_path();
        log(source ? L"everything database reused client=" + source->wstring()
                   : L"everything database reused client=unknown");
        return true;
    }

    const auto service = service_info();
    auto service_message = std::format(L"everything service state={} managed={} path={}",
        service_state_name(service.state), service.uses_bundled_executable ? 1 : 0,
        service.executable_path.empty() ? L"unknown" : service.executable_path.wstring());
    log(service_message);
    if (service.state == ServiceState::stopped || service.state == ServiceState::paused) {
        log(start_service() ? L"everything service start requested"
                            : L"everything service start failed; continuing with client fallback");
    }
    if (!components_available()) {
        log(L"everything bundled components unavailable; file search disabled");
        return false;
    }
    if (!start_client()) {
        log(L"everything client start failed; file search disabled");
        return false;
    }
    log(L"everything bundled client start requested");
    return search.available();
}

bool EverythingManager::repair_service(const EverythingSearch& search, const HWND owner,
                                       const std::chrono::milliseconds timeout) const {
    if (!components_available()) {
        log(L"everything service repair unavailable because bundled components are missing");
        return false;
    }
    log(L"everything service install or repair requested by user");
    if (!run_service_install_elevated(owner, timeout)) {
        log(L"everything service install or repair failed or was cancelled");
        return false;
    }
    log(L"everything service install or repair completed");
    (void)request_start(search);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (search.available()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return search.available();
}

void EverythingManager::log(const std::wstring_view message) const noexcept {
    if (!diagnostic_sink_) return;
    try {
        diagnostic_sink_(message);
    } catch (...) {
    }
}

bool EverythingManager::start_service() const noexcept {
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    const auto service = OpenServiceW(manager, L"Everything",
                                      SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_PAUSE_CONTINUE);
    if (!service) {
        CloseServiceHandle(manager);
        return false;
    }
    SERVICE_STATUS_PROCESS process_status{};
    DWORD required = 0;
    bool started = false;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                             reinterpret_cast<BYTE*>(&process_status),
                             sizeof(process_status), &required)
        && (process_status.dwCurrentState == SERVICE_PAUSED
            || process_status.dwCurrentState == SERVICE_PAUSE_PENDING)) {
        SERVICE_STATUS status{};
        started = ControlService(service, SERVICE_CONTROL_CONTINUE, &status) != FALSE;
    } else {
        started = StartServiceW(service, 0, nullptr) != FALSE
            || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return started;
}

bool EverythingManager::start_client() const {
    auto command_line = L"\"" + executable_path_.wstring() + L"\" -startup -first-instance";
    STARTUPINFOW startup{.cb = sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto created = CreateProcessW(
        executable_path_.c_str(), command_line.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, nullptr, directory_.c_str(), &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool EverythingManager::run_service_install_elevated(
    const HWND owner, const std::chrono::milliseconds timeout) const {
    const std::wstring parameters =
        L"-install-service -disable-update-notification -uninstall-run-on-system-startup";
    SHELLEXECUTEINFOW information{
        .cbSize = sizeof(information),
        .fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI,
        .hwnd = owner,
        .lpVerb = L"runas",
        .lpFile = executable_path_.c_str(),
        .lpParameters = parameters.c_str(),
        .lpDirectory = directory_.c_str(),
        .nShow = SW_HIDE,
    };
    if (!ShellExecuteExW(&information) || !information.hProcess) return false;
    const auto wait = WaitForSingleObject(information.hProcess, static_cast<DWORD>(timeout.count()));
    DWORD exit_code = ERROR_GEN_FAILURE;
    const auto succeeded = wait == WAIT_OBJECT_0
        && GetExitCodeProcess(information.hProcess, &exit_code)
        && exit_code == ERROR_SUCCESS;
    CloseHandle(information.hProcess);
    return succeeded;
}

bool EverythingManager::show_window(const HWND owner) const {
    if (const auto window = FindWindowW(L"EVERYTHING", nullptr)) {
        log(L"restoring existing Everything search window");
        ShowWindow(window, SW_RESTORE);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        return true;
    }

    auto launch_path = executable_path_;
    if (const auto source = client_source_path()) {
        std::error_code source_error;
        if (std::filesystem::is_regular_file(*source, source_error)) launch_path = *source;
    }

    std::error_code executable_error;
    if (!std::filesystem::is_regular_file(launch_path, executable_error)) {
        log(L"Everything search window unavailable; executable not found");
        return false;
    }

    const auto working_directory = launch_path.parent_path();
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        owner, L"open", launch_path.c_str(), nullptr,
        working_directory.c_str(), SW_SHOWNORMAL));
    if (result > 32) {
        log(L"requested Everything search window through default instance path="
            + launch_path.wstring());
    } else {
        log(std::format(L"Everything launch failed path={} shellError={}",
            launch_path.wstring(), result));
    }
    return result > 32;
}

} // namespace simpilot
