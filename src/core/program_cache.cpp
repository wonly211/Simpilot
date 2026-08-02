#include "simpilot/program_cache.hpp"
#include "simpilot/text_encoding.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string_view>

namespace simpilot {
namespace {

constexpr std::string_view cache_header = "# Simpilot program resolution cache v2";

std::string escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case '\\': result.append("\\\\"); break;
        case '\t': result.append("\\t"); break;
        case '\r': result.append("\\r"); break;
        case '\n': result.append("\\n"); break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

std::optional<std::string> unescape(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\') {
            result.push_back(value[index]);
            continue;
        }
        if (++index == value.size()) return std::nullopt;
        switch (value[index]) {
        case '\\': result.push_back('\\'); break;
        case 't': result.push_back('\t'); break;
        case 'r': result.push_back('\r'); break;
        case 'n': result.push_back('\n'); break;
        default: return std::nullopt;
        }
    }
    return result;
}

} // namespace

ProgramResolutionCache::ProgramResolutionCache(std::filesystem::path path)
    : path_(std::move(path)) {
    load();
}

std::optional<std::filesystem::path> ProgramResolutionCache::find(
    const std::wstring& executable) {
    const auto found = entries_.find(key_for(executable));
    if (found == entries_.end()) return std::nullopt;
    std::error_code error;
    if (std::filesystem::is_regular_file(found->second, error)) return found->second;
    entries_.erase(found);
    save();
    return std::nullopt;
}

void ProgramResolutionCache::store(const std::wstring& executable,
                                   const std::filesystem::path& resolved_path) {
    if (executable.empty() || resolved_path.empty()) return;
    entries_.insert_or_assign(key_for(executable), std::filesystem::absolute(resolved_path));
    save();
}

std::size_t ProgramResolutionCache::size() const noexcept {
    return entries_.size();
}

void ProgramResolutionCache::load() noexcept {
    try {
        std::ifstream stream(path_, std::ios::binary);
        if (!stream) return;
        std::string line;
        if (!std::getline(stream, line)) return;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != cache_header) return;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto separator = line.find('\t');
            if (separator == std::string::npos) continue;
            const auto key_value = unescape(std::string_view(line).substr(0, separator));
            const auto path_value = unescape(std::string_view(line).substr(separator + 1));
            if (!key_value || !path_value) continue;
            const auto key_text = decode_utf8(*key_value);
            const auto path_text = decode_utf8(*path_value);
            if (!key_text || !path_text) continue;
            auto key = *key_text;
            auto cached_path = std::filesystem::path(*path_text);
            std::error_code error;
            if (!key.empty() && std::filesystem::is_regular_file(cached_path, error)) {
                entries_.insert_or_assign(std::move(key), std::move(cached_path));
            }
        }
    } catch (...) {
        entries_.clear();
    }
}

void ProgramResolutionCache::save() noexcept {
    try {
        std::filesystem::create_directories(path_.parent_path());
        const auto temporary = std::filesystem::path(path_.wstring() + L".tmp");
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) return;
            stream << cache_header << "\r\n";
            for (const auto& [key, cached_path] : entries_) {
                stream << escape(encode_utf8(key)) << '\t'
                       << escape(encode_utf8(cached_path.wstring())) << "\r\n";
            }
            if (!stream) return;
        }
        if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
        }
    } catch (...) {
    }
}

std::wstring ProgramResolutionCache::key_for(std::wstring executable) {
    const auto first = executable.find_first_not_of(L" \t\r\n");
    const auto last = executable.find_last_not_of(L" \t\r\n");
    executable = first == std::wstring::npos ? std::wstring{}
        : executable.substr(first, last - first + 1);
    std::transform(executable.begin(), executable.end(), executable.begin(), towlower);
    return executable;
}

} // namespace simpilot
