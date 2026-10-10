#include "simpilot/settings_backup.hpp"
#include <Windows.h>
#include <aclapi.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace simpilot;
namespace fs = std::filesystem;
namespace {
// Only the per-test temporary directory is restricted. Preserve its original
// ACL so the failed transaction can be retried after access is restored.
class DenyDirectoryWrites final {
public:
    explicit DenyDirectoryWrites(const fs::path& path) : path_(path.wstring()) {
        if (GetNamedSecurityInfoW(path_.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                nullptr, nullptr, &original_acl_, nullptr, &original_) != ERROR_SUCCESS) {
            throw std::runtime_error("Cannot read test directory permissions");
        }
        std::array<unsigned char, SECURITY_MAX_SID_SIZE> everyone{};
        DWORD size = static_cast<DWORD>(everyone.size());
        EXPLICIT_ACCESSW entry{};
        entry.grfAccessPermissions = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
        entry.grfAccessMode = DENY_ACCESS;
        entry.grfInheritance = NO_INHERITANCE;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = reinterpret_cast<wchar_t*>(everyone.data());
        PACL restricted = nullptr;
        const bool prepared = CreateWellKnownSid(WinWorldSid, nullptr, everyone.data(), &size)
            && SetEntriesInAclW(1, &entry, original_acl_, &restricted) == ERROR_SUCCESS;
        const bool applied = prepared && SetNamedSecurityInfoW(path_.data(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, restricted, nullptr) == ERROR_SUCCESS;
        if (restricted) LocalFree(restricted);
        if (!applied) {
            LocalFree(original_);
            throw std::runtime_error("Cannot restrict test directory writes");
        }
    }
    ~DenyDirectoryWrites() {
        SetNamedSecurityInfoW(path_.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, original_acl_, nullptr);
        LocalFree(original_);
    }
    DenyDirectoryWrites(const DenyDirectoryWrites&) = delete;
    DenyDirectoryWrites& operator=(const DenyDirectoryWrites&) = delete;
private:
    std::wstring path_;
    PACL original_acl_ = nullptr;
    PSECURITY_DESCRIPTOR original_ = nullptr;
};
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F action, const char* reason) {
    bool rejected = false;
    try { action(); } catch (...) { rejected = true; }
    require(rejected, reason);
}
void put(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << value;
}
std::string get(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
SettingsSnapshot sample(const std::string& value) {
    return {"1.0.10", {{"Config/Setting.ini", {value.begin(), value.end()}},
        {"Config/Simpilot.ini", {'A','|','a','.','e','x','e','\n'}},
        {"Cache/RunIcon/a.custom.ico", {0,0,1,0}}}};
}
void crash_child(const fs::path& executable, const fs::path& root, const std::wstring& phase) {
    auto command = L"\"" + executable.wstring() + L"\" --crash \"" + root.wstring() + L"\" " + phase;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process), "start crash subprocess");
    require(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0, "crash subprocess finished");
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    require(code == 79, "subprocess terminated at requested checkpoint");
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"--crash") {
        const std::wstring phase(argv[3]);
        SettingsBackup::recover(argv[2], {}, {}, [&](std::string_view point) {
            if (std::wstring(point.begin(), point.end()) == phase) ExitProcess(79);
        });
        return 80;
    }
    const auto root = fs::temp_directory_path() / (L"Simpilot-backup-test-" + std::to_wstring(GetCurrentProcessId()));
    try {
        const auto target = root / L"目标";
        fs::create_directories(target);
        require(!SettingsBackup::has_configuration(target), "empty directory offers migration");
        auto original = sample("[General]\nLanguage=zh-CN\nUnknownFutureKey=preserved\n");
        SettingsBackup::write(original, root / L"export.simpilot-backup");
        const auto decoded = SettingsBackup::read(root / L"export.simpilot-backup");
        require(decoded.files == original.files, "lossless file bytes and unknown keys");
        SettingsBackup::stage(target, decoded);
        SettingsBackup::recover(target);
        require(SettingsBackup::capture(target, "test").files == original.files, "initial restore");
        require(SettingsBackup::has_configuration(target), "existing configuration never treated as new");
        auto differently_cased = original;
        differently_cased.files.erase("Cache/RunIcon/a.custom.ico");
        differently_cased.files.emplace("Cache/RunIcon/A.custom.ico", std::vector<unsigned char>{0,0,1,0,2});
        SettingsBackup::stage(target, differently_cased);
        SettingsBackup::recover(target);
        require(get(target / L"Cache/RunIcon/a.custom.ico") == std::string("\0\0\1\0\2", 5),
            "case-only path changes must not delete a restored icon");
        SettingsBackup::stage(target, original);
        SettingsBackup::recover(target);
        put(target / L"Config/Simpilot2.ini", "Extra|extra.exe\n");
        put(target / L"Config/my-script.cmd", "user-owned\n");
        SettingsBackup::stage(target, original);
        SettingsBackup::recover(target);
        require(!fs::exists(target / L"Config/Simpilot2.ini"), "missing managed file removes stale menu");
        require(get(target / L"Config/my-script.cmd") == "user-owned\n", "unmanaged file retained");
        auto changed = sample("[General]\nLanguage=en-US\n");
        for (const auto phase : {"prepared", "Config/Setting.ini", "Cache/RunIcon/a.custom.ico", "external"}) {
            SettingsBackup::stage(target, changed);
            rejects([&] { SettingsBackup::recover(target, {}, {}, [phase](std::string_view point) {
                if (point == phase) throw std::runtime_error("injected disk/application failure");
            }); }, "failed commit reported");
            require(SettingsBackup::capture(target, "test").files == original.files, "failed transaction restores all files");
            require(!SettingsBackup::pending(target), "rolled back transaction cleared");
        }
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        for (const auto phase : {L"prepared", L"Config/Setting.ini", L"external", L"committed"}) {
            SettingsBackup::stage(target, changed);
            crash_child(executable, target, phase);
            const auto committed = SettingsBackup::recover(target);
            require(committed == (std::wstring_view(phase) == L"committed"),
                "startup reports whether the interrupted transaction had committed");
            require(SettingsBackup::capture(target, "test").files == (std::wstring_view(phase) == L"committed"
                ? changed.files : original.files), "startup repairs interrupted transaction");
        }
        auto invalid = original;
        invalid.files.emplace("../outside.ini", std::vector<unsigned char>{1});
        rejects([&] { SettingsBackup::stage(target, invalid); }, "path traversal rejected");
        invalid = original;
        invalid.files.emplace("Cache/RunIcon/A.custom.ico", std::vector<unsigned char>{1});
        rejects([&] { SettingsBackup::stage(target, invalid); }, "case-insensitive duplicate Windows path rejected");
        invalid = sample("broken without an equals sign\n");
        rejects([&] { SettingsBackup::stage(target, invalid); }, "corrupt INI rejected before backup changes");
        auto corrupted = get(root / L"export.simpilot-backup");
        corrupted.back() ^= 1;
        put(root / L"corrupt.simpilot-backup", corrupted);
        rejects([&] { (void)SettingsBackup::read(root / L"corrupt.simpilot-backup"); }, "corrupt payload rejected");
        corrupted = get(root / L"export.simpilot-backup"); corrupted[8] = 2;
        put(root / L"future.simpilot-backup", corrupted);
        rejects([&] { (void)SettingsBackup::read(root / L"future.simpilot-backup"); }, "unknown format rejected");
        const auto before = SettingsBackup::capture(target, "test");
        SettingsBackup::stage(target, original);
        const auto lock = CreateFileW((target / L"Config/Setting.ini").c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "lock configuration");
        rejects([&] { SettingsBackup::recover(target); }, "file lock stops restore");
        CloseHandle(lock);
        if (SettingsBackup::pending(target)) SettingsBackup::recover(target);
        require(SettingsBackup::capture(target, "test").files == before.files, "locked-file recovery preserves configuration");
        bool rolled_back = false;
        SettingsBackup::stage(target, original);
        rejects([&] { SettingsBackup::recover(target, {}, {
            [] { return std::string("original startup registration"); },
            [] { throw std::runtime_error("startup registration denied"); },
            [&](const std::string& value) { rolled_back = value == "original startup registration"; }}); }, "external state failure reported");
        require(rolled_back, "external state restored");
        SettingsBackup::stage(target, original);
        SettingsBackup::cancel_pending(target);
        require(!SettingsBackup::pending(target), "unapplied restore can be cancelled");
        require(SettingsBackup::capture(target, "test").files == before.files, "cancelling restore leaves current settings untouched");
        const auto no_read = CreateFileW((target / L"Config/Setting.ini").c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        require(no_read != INVALID_HANDLE_VALUE, "deny reads while simulating inaccessible configuration");
        rejects([&] { (void)SettingsBackup::capture(target, "test"); }, "unreadable configuration does not export defaults");
        CloseHandle(no_read);
        require(SettingsBackup::capture(target, "test").files == before.files, "read failure preserves original files");
        SettingsBackup::stage(target, original);
        {
            DenyDirectoryWrites restricted(target / L"Backups");
            rejects([&] { SettingsBackup::recover(target); }, "backup directory access denial stops restore");
            require(!fs::exists(target / L"Backups/restore.journal"), "no commit starts without the automatic backup");
            require(SettingsBackup::capture(target, "test").files == before.files, "backup failure leaves all configuration intact");
        }
        SettingsBackup::cancel_pending(target);
        auto denied_change = original;
        denied_change.files["Cache/RunIcon/a.custom.ico"].push_back(9);
        SettingsBackup::stage(target, denied_change);
        {
            DenyDirectoryWrites restricted(target / L"Config");
            rejects([&] { SettingsBackup::recover(target); }, "configuration directory access denial reports failure");
            require(fs::exists(target / L"Backups/restore.journal"), "failed rollback retains its recovery record");
        }
        require(!SettingsBackup::recover(target), "retry rolls back rather than reapplying the pending restore");
        require(SettingsBackup::capture(target, "test").files == before.files, "access restored permits complete rollback");
        auto with_second_menu = before;
        with_second_menu.files["Config/Simpilot2.ini"] = {'B', '|', 'b', '.', 'e', 'x', 'e', '\n'};
        SettingsBackup::stage(target, with_second_menu);
        SettingsBackup::recover(target);
        SettingsBackup::stage(target, original);
        crash_child(executable, target, L"Config/Simpilot2.ini");
        require(!fs::exists(target / L"Config/Simpilot2.ini"), "subprocess interrupted after stale menu deletion");
        require(!SettingsBackup::recover(target), "interrupted file deletion rolls back");
        require(SettingsBackup::capture(target, "test").files == with_second_menu.files, "rollback restores deleted files as well as overwritten files");
        fs::remove_all(root);
        std::cout << "Backup, migration and interrupted recovery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nRetained test directory: " << root.string() << '\n';
        return 1;
    }
}
