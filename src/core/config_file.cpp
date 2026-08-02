#include "simpilot/config_file.hpp"
#include "simpilot/text_encoding.hpp"

#include <Windows.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace simpilot {
namespace {

std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Unable to open configuration file");
    }
    const auto size = stream.tellg();
    stream.seekg(0);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

bool starts_with(const std::vector<char>& bytes, const std::initializer_list<unsigned char> prefix) {
    if (bytes.size() < prefix.size()) {
        return false;
    }
    std::size_t index = 0;
    for (const auto expected : prefix) {
        if (static_cast<unsigned char>(bytes[index++]) != expected) {
            return false;
        }
    }
    return true;
}

void write_all(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Unable to write configuration file");
    }
    if (!bytes.empty()) {
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    stream.flush();
    if (!stream) {
        throw std::runtime_error("Unable to finish writing configuration file");
    }
}

} // namespace

std::wstring read_configuration_text(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    if (bytes.empty()) return {};
    const auto offset = starts_with(bytes, {0xEF, 0xBB, 0xBF}) ? 3U : 0U;
    const auto decoded = decode_utf8(
        std::string_view(bytes.data() + offset, bytes.size() - offset));
    if (!decoded) throw std::runtime_error("Configuration file must use UTF-8");
    return *decoded;
}

void write_configuration_text(const std::filesystem::path& path, const std::wstring& text) {
    std::filesystem::create_directories(path.parent_path());
    const auto encoded = encode_utf8(text);
    const std::vector<char> bytes(encoded.begin(), encoded.end());
    const auto temporary_path = std::filesystem::path(path.wstring() + L".tmp");
    write_all(temporary_path, bytes);
    if (!MoveFileExW(temporary_path.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code error;
        std::filesystem::remove(temporary_path, error);
        throw std::runtime_error("Unable to replace configuration file");
    }
}

} // namespace simpilot
