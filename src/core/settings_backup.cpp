#include "simpilot/settings_backup.hpp"
#include "simpilot/atomic_file.hpp"
#include "simpilot/settings_document.hpp"
#include "simpilot/text_encoding.hpp"

#include <Windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <format>
#include <set>
#include <sstream>
#include <stdexcept>

namespace simpilot {
namespace {
using Bytes = std::vector<unsigned char>;
using Json = nlohmann::json;
// Stop deeply nested input before the recursive CBOR reader can exhaust the
// stack. Valid manifests have only three container levels.
class ManifestShape final : public nlohmann::json_sax<Json> {
public:
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override { return ++depth_ <= 8; }
    bool key(string_t&) override { return true; }
    bool end_object() override { --depth_; return true; }
    bool start_array(std::size_t) override { return ++depth_ <= 8; }
    bool end_array() override { --depth_; return true; }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
private:
    unsigned depth_ = 0;
};
constexpr std::size_t maximum_size = 128 * 1024 * 1024;
constexpr std::size_t maximum_files = 8192;
constexpr std::array<char, 8> magic{'S','I','M','P','B','A','K','\0'};
constexpr const char* fixed_files[]{"Config/Setting.ini", "Config/Simpilot.ini",
    "Config/Simpilot2.ini", "Config/InputMethodHistory.ini", "Cache/program-cache.tsv", "Language.lng"};
constexpr auto pending_file = L"Backups/restore.pending";
constexpr auto journal_file = L"Backups/restore.journal";

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string path_identity(std::string name) {
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

bool managed(const std::string& name) {
    if (std::ranges::find(fixed_files, name) != std::end(fixed_files)) return true;
    constexpr std::string_view prefix = "Cache/RunIcon/";
    if (!name.starts_with(prefix)) return false;
    const auto file = std::string_view(name).substr(prefix.size());
    return file.size() > 4 && file.size() < 200 && file.ends_with(".ico")
        && file.find("..") == file.npos
        && file.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == file.npos;
}

void safe_path(const std::filesystem::path& root, const std::filesystem::path& relative) {
    auto path = root;
    for (const auto& component : relative) {
        if (component == L".." || component.is_absolute()) fail("Unsafe backup path");
        path /= component;
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            fail("Configuration paths cannot be symbolic links or junctions");
        }
    }
}

Bytes read_bytes(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > maximum_size) fail("Backup file exceeds 128 MiB");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("Cannot read: " + path.generic_string());
    Bytes bytes(static_cast<std::size_t>(size));
    if (size && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        fail("Incomplete file: " + path.generic_string());
    }
    if (stream.peek() != std::char_traits<char>::eof()) fail("File changed during backup");
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const Bytes& bytes) {
    AtomicFileReplacement replacement(path);
    const auto file = CreateFileW(replacement.temporary_path().c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail("Cannot write backup/configuration file");
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !replacement.commit()) fail("Cannot persist backup/configuration file");
}

std::string hash(const Bytes& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) fail("SHA-256 unavailable");
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) fail("SHA-256 failed");
    std::string result;
    for (const auto value : digest) result += std::format("{:02x}", value);
    return result;
}

void put32(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (i * 8)));
}
std::uint32_t get32(const Bytes& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes.at(offset + i)) << (i * 8);
    return value;
}

Json manifest(const SettingsSnapshot& snapshot) {
    Json result{{"format", 1}, {"application", "Simpilot"},
        {"version", snapshot.application_version}, {"files", Json::array()}};
    for (const auto& [name, bytes] : snapshot.files) result["files"].push_back({
        {"path", name}, {"size", bytes.size()}, {"sha256", hash(bytes)}, {"data", Json::binary(bytes)}});
    return result;
}

SettingsSnapshot decode(const Json& document) {
    if (document.at("format") != 1 || document.at("application") != "Simpilot") fail("Unsupported backup format");
    SettingsSnapshot result{document.at("version").get<std::string>(), {}};
    const auto& files = document.at("files");
    if (!files.is_array() || files.size() > maximum_files) fail("Too many backup files");
    std::size_t size = 0;
    std::set<std::string> identities;
    for (const auto& item : files) {
        auto name = item.at("path").get<std::string>();
        if (!managed(name) || !item.at("data").is_binary()) fail("Invalid backup entry");
        if (!identities.insert(path_identity(name)).second) fail("Duplicate Windows file path");
        Bytes bytes = item.at("data").get_binary();
        size += bytes.size();
        if (size > maximum_size || item.at("size") != bytes.size() || item.at("sha256") != hash(bytes)) {
            fail("Backup checksum or length mismatch");
        }
        if (!result.files.emplace(std::move(name), std::move(bytes)).second) fail("Duplicate backup entry");
    }
    return result;
}

std::vector<std::string> inventory(const std::filesystem::path& root) {
    std::vector<std::string> result;
    for (const auto* name : fixed_files) {
        safe_path(root, name);
        if (std::filesystem::exists(root / name)) result.emplace_back(name);
    }
    safe_path(root, L"Cache/RunIcon");
    if (std::filesystem::exists(root / L"Cache/RunIcon")) {
        for (const auto& entry : std::filesystem::directory_iterator(root / L"Cache/RunIcon")) {
            const auto name = "Cache/RunIcon/" + entry.path().filename().generic_string();
            if (managed(name)) {
                safe_path(root, name);
                if (!entry.is_regular_file()) fail("Configuration entry is not a regular file");
                result.push_back(name);
            }
        }
    }
    if (result.size() > maximum_files) fail("Too many configuration files");
    std::ranges::sort(result);
    return result;
}

void replace_files(const std::filesystem::path& root, const SettingsSnapshot& snapshot,
    const SettingsBackup::Checkpoint& checkpoint) {
    const auto current = inventory(root);
    std::set<std::string> restored_paths;
    for (const auto& [name, bytes] : snapshot.files) {
        if (!managed(name)) fail("Invalid restore target");
        safe_path(root, name);
        write_bytes(root / name, bytes);
        restored_paths.insert(path_identity(name));
        if (checkpoint) checkpoint(name);
    }
    for (const auto& name : current) if (!restored_paths.contains(path_identity(name))) {
        safe_path(root, name);
        std::filesystem::remove(root / name);
        if (checkpoint) checkpoint(name);
    }
}

void validate_ini(const Bytes& bytes) {
    const std::string text(bytes.begin(), bytes.end());
    (void)SettingsDocument::parse(text);
    // The normal tolerant reader preserves unknown lines. Imports must not
    // silently turn a damaged file into an empty/default configuration.
    std::istringstream lines(text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto start = line.find_first_not_of(" \r\t");
        if (start == line.npos || line[start] == ';' || line[start] == '#') continue;
        const auto end = line.find_last_not_of(" \r\t");
        if (line[start] == '[' && line[end] == ']') continue;
        const auto equals = line.find('=', start);
        if (equals == line.npos || equals == start) fail("Malformed INI configuration");
    }
}
} // namespace

SettingsSnapshot SettingsBackup::capture(const std::filesystem::path& root, std::string version) {
    SettingsSnapshot snapshot{std::move(version), {}};
    std::size_t size = 0;
    const auto before = inventory(root);
    for (const auto& name : before) {
        auto bytes = read_bytes(root / name);
        size += bytes.size();
        if (size > maximum_size) fail("Configuration exceeds 128 MiB");
        snapshot.files.emplace(name, std::move(bytes));
    }
    // Detect externally edited files instead of producing a mixed snapshot.
    if (before != inventory(root)) fail("Configuration changed during backup; retry");
    for (const auto& [name, bytes] : snapshot.files) {
        if (read_bytes(root / name) != bytes) fail("Configuration changed during backup; retry");
    }
    return snapshot;
}

void SettingsBackup::write(const SettingsSnapshot& snapshot, const std::filesystem::path& path) {
    const auto raw = Json::to_cbor(manifest(snapshot));
    if (raw.empty() || raw.size() > maximum_size) fail("Backup exceeds 128 MiB");
    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) fail("Compression unavailable");
    SIZE_T required = 0;
    (void)Compress(compressor, raw.data(), raw.size(), nullptr, 0, &required);
    Bytes compressed(required);
    const bool ok = required && Compress(compressor, raw.data(), raw.size(), compressed.data(), compressed.size(), &required);
    CloseCompressor(compressor);
    if (!ok) fail("Compression failed");
    compressed.resize(required);
    Bytes output(magic.begin(), magic.end());
    put32(output, 1);
    put32(output, static_cast<std::uint32_t>(raw.size()));
    put32(output, static_cast<std::uint32_t>(compressed.size()));
    const auto checksum = hash(raw);
    output.insert(output.end(), checksum.begin(), checksum.end());
    output.insert(output.end(), compressed.begin(), compressed.end());
    write_bytes(path, output);
}

SettingsSnapshot SettingsBackup::read(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    constexpr std::size_t header_size = 84;
    if (bytes.size() < header_size || !std::equal(magic.begin(), magic.end(), bytes.begin())
        || get32(bytes, 8) != 1 || !get32(bytes, 12) || get32(bytes, 12) > maximum_size
        || get32(bytes, 16) != bytes.size() - header_size) fail("Invalid or unsupported backup");
    Bytes raw(get32(bytes, 12));
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor)) fail("Decompression unavailable");
    SIZE_T written = 0;
    const bool ok = Decompress(decompressor, bytes.data() + header_size, bytes.size() - header_size,
        raw.data(), raw.size(), &written);
    CloseDecompressor(decompressor);
    if (!ok || written != raw.size() || hash(raw) != std::string(bytes.begin() + 20, bytes.begin() + 84)) {
        fail("Backup integrity check failed");
    }
    ManifestShape shape;
    if (!Json::sax_parse(raw, &shape, Json::input_format_t::cbor)) fail("Invalid backup manifest structure");
    return decode(Json::from_cbor(raw));
}

void SettingsBackup::validate(const SettingsSnapshot& snapshot) {
    if (snapshot.files.size() > maximum_files) fail("Too many backup files");
    std::size_t size = 0;
    std::set<std::string> identities;
    for (const auto& [name, bytes] : snapshot.files) {
        if (!managed(name)) fail("Unknown backup path");
        if (!identities.insert(path_identity(name)).second) fail("Duplicate Windows file path");
        size += bytes.size();
        if (size > maximum_size) fail("Configuration exceeds 128 MiB");
        if (name == "Config/Setting.ini" || name == "Config/InputMethodHistory.ini") validate_ini(bytes);
    }
}

bool SettingsBackup::has_configuration(const std::filesystem::path& root) {
    return !inventory(root).empty();
}
bool SettingsBackup::pending(const std::filesystem::path& root) {
    safe_path(root, pending_file);
    safe_path(root, journal_file);
    return std::filesystem::exists(root / pending_file) || std::filesystem::exists(root / journal_file);
}
void SettingsBackup::stage(const std::filesystem::path& root, const SettingsSnapshot& snapshot, const Validator& validator) {
    if (pending(root)) fail("A restore is already pending");
    validate(snapshot);
    if (validator) validator(snapshot);
    write(snapshot, root / pending_file);
}

void SettingsBackup::cancel_pending(const std::filesystem::path& root) {
    if (!pending(root)) return;
    if (std::filesystem::exists(root / journal_file)) fail("Finish recovery before cancelling a restore");
    const auto archive = root / L"Backups" / std::format("CancelledRestore-{}-{}.simpilot-backup",
        std::chrono::system_clock::now().time_since_epoch().count(), GetCurrentProcessId());
    std::filesystem::rename(root / pending_file, archive);
}

bool SettingsBackup::recover(const std::filesystem::path& root, const Validator& validator,
    const ExternalState& external, const Checkpoint& checkpoint) {
    if (!pending(root)) return false;
    const auto journal = root / journal_file;
    auto rollback = [&] {
        const auto bytes = read_bytes(journal);
        const auto record = Json::parse(bytes);
        const auto backup = record.at("backup").get<std::string>();
        if (backup.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != backup.npos) {
            fail("Invalid recovery record");
        }
        safe_path(root, std::filesystem::path(L"Backups") / backup);
        const bool committed = record.at("state") == "committed";
        if (!committed) {
            replace_files(root, read(root / L"Backups" / backup), {});
            if (external.rollback) external.rollback(record.at("external").get<std::string>());
        }
        // Keep journal until pending is removed, so a crash cannot retry it.
        std::filesystem::remove(root / pending_file);
        std::filesystem::remove(journal);
        return committed;
    };
    if (std::filesystem::exists(journal)) {
        return rollback();
    }
    const auto snapshot = read(root / pending_file);
    validate(snapshot);
    if (validator) validator(snapshot);
    const auto original = capture(root, "recovery");
    const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto backup_name = std::format("BeforeRestore-{}-{}.simpilot-backup", timestamp, GetCurrentProcessId());
    const auto backup = root / L"Backups" / backup_name;
    safe_path(root, std::filesystem::path(L"Backups") / backup_name);
    write(original, backup);
    (void)read(backup);
    Json record{{"backup", backup_name}, {"state", "applying"},
        {"external", external.capture ? external.capture() : ""}};
    auto persist_record = [&] {
        const auto text = record.dump();
        write_bytes(journal, Bytes(text.begin(), text.end()));
    };
    persist_record();
    try {
        if (checkpoint) checkpoint("prepared");
        replace_files(root, snapshot, checkpoint);
        if (external.apply) external.apply();
        if (checkpoint) checkpoint("external");
        record["state"] = "committed";
        persist_record();
    } catch (...) {
        rollback();
        throw;
    }
    if (checkpoint) checkpoint("committed");
    rollback(); // A committed record only needs its transaction files removed.
    return true;
}
} // namespace simpilot
