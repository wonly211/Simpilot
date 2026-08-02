#include "simpilot/program_resolver.hpp"

#include "simpilot/command.hpp"
#include "simpilot/program_cache.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <utility>
#include <vector>

namespace simpilot {
namespace {

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

std::vector<std::filesystem::path> search_directories() {
    std::vector<std::filesystem::path> result;
    wchar_t buffer[MAX_PATH];
    if (const auto length = GetSystemDirectoryW(buffer, MAX_PATH); length > 0) {
        result.emplace_back(std::wstring(buffer, length));
    }
    if (const auto length = GetWindowsDirectoryW(buffer, MAX_PATH); length > 0) {
        result.emplace_back(std::wstring(buffer, length));
    }
    const auto required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (required > 0) {
        std::wstring path_value(required, L'\0');
        path_value.resize(GetEnvironmentVariableW(L"PATH", path_value.data(), required));
        std::size_t start = 0;
        while (start <= path_value.size()) {
            const auto end = path_value.find(L';', start);
            auto item = path_value.substr(start, end - start);
            if (item.size() >= 2 && item.front() == L'\"' && item.back() == L'\"') {
                item = item.substr(1, item.size() - 2);
            }
            if (!item.empty()) result.emplace_back(std::move(item));
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }
    return result;
}

bool shell_command(const std::wstring& executable) {
    const auto normalized = lowercase(executable);
    return normalized.starts_with(L"shell:") || normalized.starts_with(L"ms-settings:")
        || executable.starts_with(L"::{");
}

void mark_unavailable(MenuEntry& entry, std::wstring reason) {
    entry.is_available = false;
    entry.unavailable_reason = std::move(reason);
    entry.resolved_value.reset();
}

} // namespace

ProgramResolver::ProgramResolver(const IProgramSearch* search, ProgramResolutionCache* cache,
                                 ProgramCandidateSelector candidate_selector)
    : search_(search), cache_(cache), candidate_selector_(std::move(candidate_selector)) {}

std::optional<std::filesystem::path> ProgramResolver::resolve(const std::wstring& executable) const {
    if (executable.empty()) return std::nullopt;
    const std::filesystem::path value(executable);
    if (value.is_absolute() || std::filesystem::exists(value)) return value;
    if (value.has_parent_path()) return std::nullopt;
    for (const auto& directory : search_directories()) {
        const auto candidate = directory / value;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }

    if (cache_) {
        if (const auto cached = cache_->find(executable)) return cached;
    }

    if (search_ && search_->available()) {
        auto candidates = search_->find_exact_file_name(value.filename().wstring());
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(), [](const ProgramCandidate& candidate) {
                std::error_code error;
                return !std::filesystem::is_regular_file(candidate.path, error);
            }),
            candidates.end());
        std::sort(candidates.begin(), candidates.end(), [](const ProgramCandidate& left,
                                                           const ProgramCandidate& right) {
            if (left.version != right.version) return left.version > right.version;
            if (left.last_write_time != right.last_write_time) {
                return left.last_write_time > right.last_write_time;
            }
            return lowercase(left.path.wstring()) < lowercase(right.path.wstring());
        });
        if (candidates.empty()) return std::nullopt;

        auto selected = candidates.front().path;
        if (candidates.size() > 1 && candidate_selector_) {
            const auto requested = candidate_selector_(executable, candidates);
            if (!requested) return std::nullopt;
            const auto matching = std::find_if(
                candidates.begin(), candidates.end(), [&requested](const ProgramCandidate& candidate) {
                    return lowercase(candidate.path.wstring())
                        == lowercase(requested->wstring());
                });
            if (matching == candidates.end()) return std::nullopt;
            selected = matching->path;
        }
        if (cache_) cache_->store(executable, selected);
        return selected;
    }
    return std::nullopt;
}

MenuResolutionService::MenuResolutionService(ProgramResolver resolver) : resolver_(std::move(resolver)) {}

void MenuResolutionService::resolve(MenuDocument& document,
                                    const VariableExpander& variable_expander) const {
    for (auto* entry : document.entries()) {
        if (entry->kind != MenuEntryKind::command) continue;
        entry->is_available = true;
        entry->unavailable_reason.reset();
        const auto expanded = variable_expander.expand(entry->value);
        const auto parsed = ParsedCommand::try_parse(expanded);
        if (!parsed) {
            mark_unavailable(*entry, L"Invalid command format");
            continue;
        }
        if (shell_command(parsed->executable)) {
            entry->resolved_value = expanded;
            continue;
        }
        const std::filesystem::path executable(parsed->executable);
        if (executable.is_absolute()) {
            if (std::filesystem::exists(executable)) {
                entry->resolved_value = parsed->with_executable(executable.wstring());
            } else {
                mark_unavailable(*entry, L"Path does not exist");
            }
            continue;
        }
        if (executable.has_parent_path()) {
            const auto absolute = std::filesystem::absolute(
                std::filesystem::path(variable_expander.config_directory()) / executable);
            if (std::filesystem::exists(absolute)) {
                entry->resolved_value = parsed->with_executable(absolute.wstring());
            } else {
                mark_unavailable(*entry, L"Relative path does not exist");
            }
            continue;
        }
        if (const auto resolved = resolver_.resolve(parsed->executable)) {
            entry->resolved_value = parsed->with_executable(resolved->wstring());
        } else {
            mark_unavailable(*entry, L"Program was not found in Windows PATH or Everything");
        }
    }
}

} // namespace simpilot
