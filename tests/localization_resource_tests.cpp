#include "simpilot/language_pack.hpp"
#include "simpilot/localization.hpp"

#include <Windows.h>
#include <compressapi.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path executable_directory() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(length > 0 && length < path.size(), "Resolve test executable directory");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

void write_external_pack(const std::filesystem::path& path) {
    const auto payload = Json{{"languages", Json::array({Json{
        {"locale", "fr-FR"},
        {"name", "Francais"},
        {"strings", Json{{"ui.settings", "Parametres"}}},
    }})}}.dump();
    COMPRESSOR_HANDLE compressor = nullptr;
    require(CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor) != FALSE,
            "Create external language compressor");
    SIZE_T required = 0;
    (void)Compress(compressor, payload.data(), payload.size(), nullptr, 0, &required);
    require(required > 0, "Size external language payload");
    std::vector<std::byte> compressed(required);
    SIZE_T written = 0;
    require(Compress(compressor, payload.data(), payload.size(), compressed.data(),
                     compressed.size(), &written) != FALSE,
            "Compress external language payload");
    CloseCompressor(compressor);
    compressed.resize(written);
    const simpilot::LanguagePackHeader header{
        .magic = simpilot::language_pack_magic,
        .version = simpilot::language_pack_version,
        .compression = simpilot::language_pack_xpress_huffman,
        .uncompressed_size = static_cast<std::uint32_t>(payload.size()),
        .compressed_size = static_cast<std::uint32_t>(compressed.size()),
    };
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(stream.good(), "Create external language pack");
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(compressed.data()),
                 static_cast<std::streamsize>(compressed.size()));
    require(stream.good(), "Write external language pack");
}

} // namespace

int wmain() {
    try {
        (void)executable_directory();

        const simpilot::Localization simplified(simpilot::UiLanguage::simplified_chinese);
        const simpilot::Localization traditional(simpilot::UiLanguage::traditional_chinese);
        const simpilot::Localization english(simpilot::UiLanguage::english);
        require(simplified.text(simpilot::UiText::settings) != L"[missing translation]",
                "Read Simplified Chinese from the embedded pack");
        require(traditional.text(simpilot::UiText::settings) != L"[missing translation]",
                "Read Traditional Chinese from the embedded pack");
        require(english.text(simpilot::UiText::settings) != L"[missing translation]",
                "Read English from the embedded pack");
        require(english.available_languages().size() == 3,
                "Expose the three built-in languages");

        const auto root = std::filesystem::temp_directory_path()
            / (L"simpilot-language-pack-test-" + std::to_wstring(GetCurrentProcessId()));
        const auto language_directory = root / L"Languages";
        std::filesystem::create_directories(language_directory);
        write_external_pack(language_directory / L"Language.lng");
        const simpilot::Localization french("fr-FR", language_directory);
        require(french.text("ui.settings") == L"Parametres",
                "Read an external language from Language.lng");
        const auto languages = french.available_languages();
        require(languages.size() == 4 && languages.back().code == "fr-FR",
                "List an external language package");
        const auto missing_directory = root / L"Missing";
        std::filesystem::create_directories(missing_directory);
        const simpilot::Localization missing("fr-FR", missing_directory);
        require(missing.text("ui.settings") == L"[missing translation]",
                "Ignore a missing external language package");
        const auto corrupt_directory = root / L"Corrupt";
        std::filesystem::create_directories(corrupt_directory);
        {
            std::ofstream corrupt(corrupt_directory / L"Language.lng",
                                  std::ios::binary | std::ios::trunc);
            corrupt << "not a language package";
        }
        const simpilot::Localization corrupt("fr-FR", corrupt_directory);
        require(corrupt.text("ui.settings") == L"[missing translation]",
                "Ignore a corrupt external language package");
        std::filesystem::remove_all(root);
        std::wcout << L"Embedded and external language resources verified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Localization resource test failure: " << error.what() << '\n';
        return 1;
    }
}
