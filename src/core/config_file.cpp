#include "simpilot/config_file.hpp"
#include "simpilot/atomic_file.hpp"
#include "simpilot/text_encoding.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace simpilot {
namespace {

std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Unable to open configuration file");
    }
    const auto end_position = stream.tellg();
    if (end_position == std::streampos(-1)) {
        throw std::runtime_error("Unable to determine configuration file size");
    }
    const auto size = static_cast<std::streamoff>(end_position);
    if (size < 0
        || static_cast<std::uintmax_t>(size)
            > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())
        || static_cast<std::uintmax_t>(size)
            > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("Configuration file size is unsupported");
    }
    stream.seekg(0, std::ios::beg);
    if (!stream) {
        throw std::runtime_error("Unable to seek configuration file");
    }
    std::vector<char> bytes;
    if (static_cast<std::uintmax_t>(size) > bytes.max_size()) {
        throw std::runtime_error("Configuration file is too large");
    }
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw std::runtime_error("Unable to read the complete configuration file");
        }
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
    const auto encoded = encode_utf8(text);
    const std::vector<char> bytes(encoded.begin(), encoded.end());
    AtomicFileReplacement replacement(path);
    write_all(replacement.temporary_path(), bytes);
    if (!replacement.commit()) {
        throw std::runtime_error("Unable to replace configuration file");
    }
}

} // namespace simpilot
