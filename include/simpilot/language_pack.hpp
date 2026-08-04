#pragma once

#include <array>
#include <cstdint>

namespace simpilot {

inline constexpr std::array<char, 8> language_pack_magic{
    'S', 'I', 'M', 'P', 'L', 'N', 'G', '\0'};
inline constexpr std::uint32_t language_pack_version = 1;
inline constexpr std::uint32_t language_pack_xpress_huffman = 4;

#pragma pack(push, 1)
struct LanguagePackHeader {
    std::array<char, 8> magic;
    std::uint32_t version;
    std::uint32_t compression;
    std::uint32_t uncompressed_size;
    std::uint32_t compressed_size;
};
#pragma pack(pop)

static_assert(sizeof(LanguagePackHeader) == 24);

} // namespace simpilot
