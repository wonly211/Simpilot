#include "simpilot/language_pack.hpp"

#include <Windows.h>
#include <compressapi.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

Json read_language(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to open language JSON");
    auto language = Json::parse(stream);
    if (!language.is_object()
        || !language.contains("locale") || !language.at("locale").is_string()
        || language.at("locale").get_ref<const std::string&>().empty()
        || !language.contains("strings") || !language.at("strings").is_object()) {
        throw std::runtime_error("invalid language JSON schema");
    }
    for (const auto& [key, value] : language.at("strings").items()) {
        if (key.empty() || !value.is_string()
            || value.get_ref<const std::string&>().empty()) {
            throw std::runtime_error("invalid language string");
        }
    }
    return language;
}

std::vector<std::byte> compress_payload(const std::string& payload) {
    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) {
        throw std::runtime_error("unable to create language compressor");
    }
    SIZE_T required = 0;
    (void)Compress(compressor, payload.data(), payload.size(), nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0) {
        CloseCompressor(compressor);
        throw std::runtime_error("unable to size compressed language payload");
    }
    std::vector<std::byte> compressed(required);
    SIZE_T written = 0;
    const auto succeeded = Compress(
        compressor, payload.data(), payload.size(), compressed.data(),
        compressed.size(), &written);
    CloseCompressor(compressor);
    if (!succeeded || written == 0 || written > compressed.size()) {
        throw std::runtime_error("unable to compress language payload");
    }
    compressed.resize(written);
    return compressed;
}

void write_pack(const std::filesystem::path& path, const Json& languages) {
    const auto payload = Json{{"languages", languages}}.dump();
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("language payload is too large");
    }
    const auto compressed = compress_payload(payload);
    if (compressed.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("compressed language payload is too large");
    }
    const simpilot::LanguagePackHeader header{
        .magic = simpilot::language_pack_magic,
        .version = simpilot::language_pack_version,
        .compression = simpilot::language_pack_xpress_huffman,
        .uncompressed_size = static_cast<std::uint32_t>(payload.size()),
        .compressed_size = static_cast<std::uint32_t>(compressed.size()),
    };
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("unable to create language pack");
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(compressed.data()),
                 static_cast<std::streamsize>(compressed.size()));
    if (!stream) throw std::runtime_error("unable to write language pack");
}

} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    try {
        if (argc < 3) {
            std::cerr << "usage: language_pack_builder output.lng language.json [...]\n";
            return 2;
        }
        Json languages = Json::array();
        for (int index = 2; index < argc; ++index) {
            languages.push_back(read_language(argv[index]));
        }
        write_pack(argv[1], languages);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "language pack error: " << error.what() << '\n';
        return 1;
    }
}
