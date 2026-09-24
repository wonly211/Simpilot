#include "simpilot/settings_document.hpp"

#include "simpilot/atomic_file.hpp"
#include "simpilot/text_encoding.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace simpilot {
namespace {

std::wstring_view trim(std::wstring_view value) {
    const auto start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring_view::npos) return {};
    return value.substr(start, value.find_last_not_of(L" \t\r\n") - start + 1);
}

bool equal_key(std::wstring_view left, std::wstring_view right) {
    left = trim(left);
    right = trim(right);
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

std::wstring_view key_of(std::wstring_view line) {
    line = trim(line);
    if (line.empty() || line.front() == L';' || line.front() == L'#'
        || line.front() == L'[') return {};
    const auto separator = line.find(L'=');
    return separator == std::wstring_view::npos ? std::wstring_view{}
                                               : trim(line.substr(0, separator));
}

std::wstring_view section_of(std::wstring_view line) {
    line = trim(line);
    return line.size() >= 2 && line.front() == L'[' && line.back() == L']'
        ? trim(line.substr(1, line.size() - 2)) : std::wstring_view{};
}

} // namespace

SettingsDocument SettingsDocument::parse(std::string_view content) {
    SettingsDocument result;
    result.bom_ = content.starts_with("\xEF\xBB\xBF");
    if (result.bom_) content.remove_prefix(3);
    const auto decoded = decode_utf8(content);
    if (!decoded) throw std::runtime_error("Setting.ini is not valid UTF-8");
    std::size_t begin = 0;
    while (begin < decoded->size()) {
        const auto end = decoded->find_first_of(L"\r\n", begin);
        if (end == std::wstring::npos) {
            result.lines_.push_back({decoded->substr(begin), {}});
            break;
        }
        auto next = end + 1;
        if ((*decoded)[end] == L'\r' && next < decoded->size()
            && (*decoded)[next] == L'\n') ++next;
        result.lines_.push_back({decoded->substr(begin, end - begin),
                                 decoded->substr(end, next - end)});
        begin = next;
    }
    return result;
}

SettingsDocument SettingsDocument::load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (!std::filesystem::exists(path)) return {};
        throw std::runtime_error("Cannot read Setting.ini");
    }
    const std::string content((std::istreambuf_iterator<char>(stream)), {});
    if (stream.bad()) throw std::runtime_error("Cannot read Setting.ini");
    return parse(content);
}

std::optional<std::wstring> SettingsDocument::get(std::wstring_view key) const {
    if (trim(key).empty()) return std::nullopt;
    for (auto line = lines_.rbegin(); line != lines_.rend(); ++line) {
        if (equal_key(key_of(line->text), key)) {
            return std::wstring(trim(std::wstring_view(line->text).substr(
                line->text.find(L'=') + 1)));
        }
    }
    return std::nullopt;
}

void SettingsDocument::set(std::wstring_view section, std::wstring_view key,
                           std::wstring_view value) {
    if (trim(key).empty() || key.find_first_of(L"=\r\n[];#") != std::wstring_view::npos
        || section.find_first_of(L"\r\n[]") != std::wstring_view::npos
        || value.find_first_of(L"\r\n") != std::wstring_view::npos) {
        throw std::invalid_argument("Invalid INI setting");
    }
    for (auto line = lines_.rbegin(); line != lines_.rend(); ++line) {
        if (equal_key(key_of(line->text), key)) {
            line->text.erase(line->text.find(L'=') + 1);
            line->text.append(value);
            return;
        }
    }
    auto insertion = lines_.size();
    bool found = section.empty();
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        const auto name = section_of(lines_[index].text);
        if (name.empty()) continue;
        if (found) { insertion = index; break; }
        found = equal_key(name, section);
    }
    if (!found) {
        if (!lines_.empty() && lines_.back().ending.empty()) lines_.back().ending = L"\r\n";
        lines_.push_back({L"[" + std::wstring(section) + L"]", L"\r\n"});
        insertion = lines_.size();
    }
    if (insertion > 0 && lines_[insertion - 1].ending.empty()) {
        lines_[insertion - 1].ending = L"\r\n";
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertion),
                  {std::wstring(key) + L"=" + std::wstring(value), L"\r\n"});
}

void SettingsDocument::erase(std::wstring_view key) {
    if (trim(key).empty()) return;
    std::erase_if(lines_, [key](const Line& line) {
        return equal_key(key_of(line.text), key);
    });
}

void SettingsDocument::erase_numbered(
    std::wstring_view prefix, std::span<const std::wstring_view> suffixes) {
    std::erase_if(lines_, [&](const Line& line) {
        const auto key = key_of(line.text);
        if (key.size() <= prefix.size()
            || !equal_key(key.substr(0, prefix.size()), prefix)) return false;
        const auto suffix_start = key.find_first_not_of(L"0123456789", prefix.size());
        if (suffix_start == prefix.size() || suffix_start == std::wstring_view::npos) {
            return false;
        }
        return std::ranges::any_of(suffixes, [&](const auto suffix) {
            return equal_key(key.substr(suffix_start), suffix);
        });
    });
}

void SettingsDocument::overlay(const SettingsDocument& updates) {
    if (&updates == this) return;
    std::wstring section;
    for (const auto& line : updates.lines_) {
        const auto name = section_of(line.text);
        if (!name.empty()) section = name;
        const auto key = key_of(line.text);
        if (!key.empty()) {
            set(section, key, trim(std::wstring_view(line.text).substr(
                line.text.find(L'=') + 1)));
        }
    }
}

std::string SettingsDocument::serialize() const {
    std::wstring content;
    for (const auto& line : lines_) {
        content.append(line.text);
        content.append(line.ending);
    }
    return (bom_ ? std::string("\xEF\xBB\xBF") : std::string{}) + encode_utf8(content);
}

bool SettingsDocument::save(const std::filesystem::path& path) const noexcept {
    try {
        AtomicFileReplacement replacement(path);
        {
            std::ofstream stream(replacement.temporary_path(), std::ios::binary);
            const auto content = serialize();
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.close();
            if (!stream) return false;
        }
        return replacement.commit();
    } catch (...) {
        return false;
    }
}

} // namespace simpilot
