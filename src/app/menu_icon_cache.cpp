#include "menu_icon_cache.hpp"

#include "simpilot/command.hpp"

#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <format>
#include <vector>

namespace simpilot {
namespace {

constexpr int cached_icon_size = 128;

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

#pragma pack(push, 1)
struct IconDirectory {
    std::uint16_t reserved = 0;
    std::uint16_t type = 1;
    std::uint16_t count = 1;
};

struct IconDirectoryEntry {
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t color_count = 0;
    std::uint8_t reserved = 0;
    std::uint16_t planes = 1;
    std::uint16_t bit_count = 32;
    std::uint32_t bytes_in_resource = 0;
    std::uint32_t image_offset = 0;
};
#pragma pack(pop)

std::vector<std::uint32_t> render_icon(const HICON icon, const std::uint8_t background) {
    BITMAPINFO information{};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = cached_icon_size;
    information.bmiHeader.biHeight = -cached_icon_size;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    void* raw_pixels = nullptr;
    const auto bitmap = CreateDIBSection(
        nullptr, &information, DIB_RGB_COLORS, &raw_pixels, nullptr, 0);
    if (!bitmap || !raw_pixels) {
        if (bitmap) DeleteObject(bitmap);
        return {};
    }
    auto* pixels = static_cast<std::uint32_t*>(raw_pixels);
    const auto background_pixel = static_cast<std::uint32_t>(background)
        | (static_cast<std::uint32_t>(background) << 8U)
        | (static_cast<std::uint32_t>(background) << 16U);
    std::fill_n(pixels, cached_icon_size * cached_icon_size, background_pixel);
    const auto dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        DeleteObject(bitmap);
        return {};
    }
    const auto previous = SelectObject(dc, bitmap);
    const auto drawn = DrawIconEx(dc, 0, 0, icon, cached_icon_size, cached_icon_size,
                                  0, nullptr, DI_NORMAL) != FALSE;
    std::vector<std::uint32_t> result(
        pixels, pixels + cached_icon_size * cached_icon_size);
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
    return drawn ? result : std::vector<std::uint32_t>{};
}

int recovered_alpha(const std::uint32_t black_pixel,
                    const std::uint32_t white_pixel) noexcept {
    const auto transparency = std::clamp(
        (static_cast<int>(white_pixel & 0xFFU)
         - static_cast<int>(black_pixel & 0xFFU)
         + static_cast<int>((white_pixel >> 8U) & 0xFFU)
         - static_cast<int>((black_pixel >> 8U) & 0xFFU)
         + static_cast<int>((white_pixel >> 16U) & 0xFFU)
         - static_cast<int>((black_pixel >> 16U) & 0xFFU)) / 3,
        0, 255);
    return 255 - transparency;
}

bool save_icon(const std::filesystem::path& path, const HICON icon) {
    const auto black = render_icon(icon, 0);
    const auto white = render_icon(icon, 255);
    if (black.empty() || white.size() != black.size()) return false;

    std::vector<std::uint32_t> pixels(black.size());
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto black_pixel = black[index];
        const auto white_pixel = white[index];
        const auto blue_black = static_cast<int>(black_pixel & 0xFFU);
        const auto green_black = static_cast<int>((black_pixel >> 8U) & 0xFFU);
        const auto red_black = static_cast<int>((black_pixel >> 16U) & 0xFFU);
        const auto alpha = recovered_alpha(black_pixel, white_pixel);
        const auto unpremultiply = [alpha](const int component) {
            return alpha == 0 ? 0 : std::clamp(component * 255 / alpha, 0, 255);
        };
        const auto blue = unpremultiply(blue_black);
        const auto green = unpremultiply(green_black);
        const auto red = unpremultiply(red_black);
        pixels[index] = static_cast<std::uint32_t>(blue)
            | (static_cast<std::uint32_t>(green) << 8U)
            | (static_cast<std::uint32_t>(red) << 16U)
            | (static_cast<std::uint32_t>(alpha) << 24U);
    }

    constexpr auto mask_stride = ((cached_icon_size + 31) / 32) * 4;
    std::array<std::uint8_t, mask_stride * cached_icon_size> mask{};
    for (int y = 0; y < cached_icon_size; ++y) {
        for (int x = 0; x < cached_icon_size; ++x) {
            const auto alpha = (pixels[static_cast<std::size_t>(y * cached_icon_size + x)]
                                >> 24U) & 0xFFU;
            if (alpha < 128) {
                const auto mask_y = cached_icon_size - 1 - y;
                mask[static_cast<std::size_t>(mask_y * mask_stride + x / 8)]
                    |= static_cast<std::uint8_t>(0x80U >> (x % 8));
            }
        }
    }

    BITMAPINFOHEADER bitmap_header{
        .biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = cached_icon_size,
        .biHeight = cached_icon_size * 2,
        .biPlanes = 1,
        .biBitCount = 32,
        .biCompression = BI_RGB,
        .biSizeImage = static_cast<DWORD>(pixels.size() * sizeof(std::uint32_t)),
    };
    const auto image_size = static_cast<std::uint32_t>(
        sizeof(bitmap_header) + pixels.size() * sizeof(std::uint32_t) + mask.size());
    const IconDirectory directory;
    const IconDirectoryEntry entry{
        .width = static_cast<std::uint8_t>(cached_icon_size),
        .height = static_cast<std::uint8_t>(cached_icon_size),
        .bytes_in_resource = image_size,
        .image_offset = static_cast<std::uint32_t>(
            sizeof(IconDirectory) + sizeof(IconDirectoryEntry)),
    };

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(reinterpret_cast<const char*>(&directory), sizeof(directory));
    stream.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    stream.write(reinterpret_cast<const char*>(&bitmap_header), sizeof(bitmap_header));
    for (int y = cached_icon_size - 1; y >= 0; --y) {
        const auto* row = pixels.data() + static_cast<std::size_t>(y * cached_icon_size);
        stream.write(reinterpret_cast<const char*>(row),
                     cached_icon_size * sizeof(std::uint32_t));
    }
    stream.write(reinterpret_cast<const char*>(mask.data()),
                 static_cast<std::streamsize>(mask.size()));
    stream.close();
    if (!stream || !MoveFileExW(temporary.c_str(), path.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::uint64_t stable_hash(const std::wstring_view value) noexcept {
    std::uint64_t result = 1469598103934665603ULL;
    for (const auto character : value) {
        result ^= static_cast<std::uint16_t>(character);
        result *= 1099511628211ULL;
    }
    return result;
}

bool cache_is_current(const std::filesystem::path& cache,
                      const std::wstring& source,
                      const bool synthetic) noexcept {
    std::error_code error;
    if (!std::filesystem::is_regular_file(cache, error)) return false;
    if (synthetic) return true;
    const auto source_time = std::filesystem::last_write_time(source, error);
    if (error) return true;
    const auto cache_time = std::filesystem::last_write_time(cache, error);
    return !error && cache_time >= source_time;
}

HICON system_image_list_icon(const int icon_index, const int image_list_size) noexcept {
    IImageList* image_list = nullptr;
    const auto list_result = SHGetImageList(
        image_list_size, __uuidof(IImageList), reinterpret_cast<void**>(&image_list));
    if (FAILED(list_result) || !image_list) return nullptr;

    HICON icon = nullptr;
    const auto icon_result = image_list->GetIcon(
        icon_index, ILD_TRANSPARENT, &icon);
    image_list->Release();
    return SUCCEEDED(icon_result) ? icon : nullptr;
}

bool icon_needs_upscaling(const HICON icon) {
    const auto black = render_icon(icon, 0);
    const auto white = render_icon(icon, 255);
    if (black.empty() || white.size() != black.size()) return false;

    auto left = cached_icon_size;
    auto top = cached_icon_size;
    auto right = -1;
    auto bottom = -1;
    for (int y = 0; y < cached_icon_size; ++y) {
        for (int x = 0; x < cached_icon_size; ++x) {
            const auto index = static_cast<std::size_t>(y * cached_icon_size + x);
            const auto black_pixel = black[index];
            const auto white_pixel = white[index];
            if (recovered_alpha(black_pixel, white_pixel) < 32) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < left || bottom < top) return false;
    return std::max(right - left + 1, bottom - top + 1) < cached_icon_size / 2;
}

HICON high_resolution_shell_icon(const std::wstring& path,
                                 const DWORD attributes,
                                 const bool use_file_attributes) {
    SHFILEINFOW information{};
    auto flags = SHGFI_SYSICONINDEX;
    if (use_file_attributes) flags |= SHGFI_USEFILEATTRIBUTES;
    if (SHGetFileInfoW(path.c_str(), attributes, &information,
                       sizeof(information), flags) == 0) {
        return nullptr;
    }

    auto icon = system_image_list_icon(information.iIcon, SHIL_JUMBO);
    if (icon && icon_needs_upscaling(icon)) {
        if (const auto scalable = system_image_list_icon(
                information.iIcon, SHIL_EXTRALARGE)) {
            DestroyIcon(icon);
            icon = scalable;
        }
    }
    return icon;
}

HICON fallback_shell_icon(const std::wstring& path,
                          const DWORD attributes,
                          const bool use_file_attributes) noexcept {
    SHFILEINFOW information{};
    auto flags = SHGFI_ICON | SHGFI_LARGEICON;
    if (use_file_attributes) flags |= SHGFI_USEFILEATTRIBUTES;
    if (SHGetFileInfoW(path.c_str(), attributes, &information,
                       sizeof(information), flags) == 0) {
        return nullptr;
    }
    return information.hIcon;
}

} // namespace

MenuIconCache::MenuIconCache(std::filesystem::path cache_directory)
    : cache_directory_(std::move(cache_directory)) {}

MenuIconCache::~MenuIconCache() {
    clear();
}

void MenuIconCache::clear() noexcept {
    for (const auto& [key, icon] : icons_) {
        (void)key;
        if (icon) DestroyIcon(icon);
    }
    icons_.clear();
}

HICON MenuIconCache::icon_for(const MenuEntry& entry) {
    const auto custom_key = custom_key_for(entry);
    if (entry.kind == MenuEntryKind::web) {
        return icon_for_customization(custom_key, L".url", entry.kind);
    }
    const auto target = target_for(entry);
    return target ? icon_for_customization(custom_key, *target, entry.kind) : nullptr;
}

HICON MenuIconCache::icon_for_customization(const std::wstring& custom_key,
                                            const std::wstring& icon_source,
                                            const MenuEntryKind kind) {
    if (custom_key.empty()) return nullptr;
    if (kind == MenuEntryKind::web) {
        return load_icon(custom_key, L"shell:.url", L".url",
                         FILE_ATTRIBUTE_NORMAL, true);
    }
    if (icon_source.empty()) return nullptr;
    return load_icon(custom_key, L"file:" + lowercase(icon_source), icon_source,
                     FILE_ATTRIBUTE_NORMAL, false);
}

HICON MenuIconCache::folder_icon() {
    return load_icon(L"shell:folder", L"shell:folder", L"folder",
                     FILE_ATTRIBUTE_DIRECTORY, true);
}

bool MenuIconCache::has_custom_icon(const std::wstring& custom_key) const noexcept {
    if (custom_key.empty()) return false;
    std::error_code error;
    return std::filesystem::is_regular_file(custom_icon_path_for(custom_key), error);
}

bool MenuIconCache::set_custom_icon(const std::wstring& custom_key,
                                    const std::filesystem::path& source,
                                    const int source_index) {
    if (custom_key.empty() || source.empty()) return false;
    HICON icon = nullptr;
    UINT icon_identifier = 0;
    const auto count = PrivateExtractIconsW(source.c_str(), source_index,
                                            cached_icon_size, cached_icon_size,
                                            &icon, &icon_identifier, 1,
                                            LR_DEFAULTCOLOR);
    if (count == 0 || !icon) return false;
    const auto saved = save_icon(custom_icon_path_for(custom_key), icon);
    DestroyIcon(icon);
    if (saved) clear();
    return saved;
}

bool MenuIconCache::remove_custom_icon(const std::wstring& custom_key) noexcept {
    if (custom_key.empty()) return false;
    std::error_code error;
    const auto removed = std::filesystem::remove(custom_icon_path_for(custom_key), error);
    if (!error) clear();
    return !error && removed;
}

std::wstring MenuIconCache::custom_key_for(const MenuEntry& entry) {
    return std::format(L"{}:{}:{}", entry.kind == MenuEntryKind::web ? L"web" : L"command",
                       entry.run_as_administrator ? L"admin" : L"normal",
                       entry.value);
}

std::optional<std::wstring> MenuIconCache::target_for(const MenuEntry& entry) {
    if (entry.kind != MenuEntryKind::command) return std::nullopt;
    const auto command = ParsedCommand::try_parse(entry.effective_value());
    if (!command || command->executable.empty()) return std::nullopt;
    return command->executable;
}

HICON MenuIconCache::load_icon(const std::wstring& memory_key,
                               const std::wstring& cache_key,
                               const std::wstring& shell_path,
                               const DWORD attributes,
                               const bool use_file_attributes) {
    if (const auto found = icons_.find(memory_key); found != icons_.end()) {
        return found->second;
    }
    const auto custom_path = custom_icon_path_for(memory_key);
    if (const auto custom = static_cast<HICON>(LoadImageW(
            nullptr, custom_path.c_str(), IMAGE_ICON, cached_icon_size, cached_icon_size,
            LR_LOADFROMFILE))) {
        icons_.emplace(memory_key, custom);
        return custom;
    }
    const auto cache_path = cache_path_for(cache_key);
    if (cache_is_current(cache_path, shell_path, use_file_attributes)) {
        const auto cached = static_cast<HICON>(LoadImageW(
            nullptr, cache_path.c_str(), IMAGE_ICON, cached_icon_size, cached_icon_size,
            LR_LOADFROMFILE));
        if (cached) {
            icons_.emplace(memory_key, cached);
            return cached;
        }
    }
    auto icon = high_resolution_shell_icon(
        shell_path, attributes, use_file_attributes);
    if (!icon) {
        icon = fallback_shell_icon(shell_path, attributes, use_file_attributes);
    }
    if (!icon) {
        icons_.emplace(memory_key, nullptr);
        return nullptr;
    }
    (void)save_icon(cache_path, icon);
    icons_.emplace(memory_key, icon);
    return icon;
}

std::filesystem::path MenuIconCache::cache_path_for(const std::wstring& key) const {
    return cache_directory_ / std::format(L"{:016x}.ico", stable_hash(key));
}

std::filesystem::path MenuIconCache::custom_icon_path_for(const std::wstring& key) const {
    return cache_directory_ / std::format(L"{:016x}.custom.ico", stable_hash(key));
}

} // namespace simpilot
