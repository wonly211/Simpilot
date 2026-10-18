#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace simpilot {

// Paths are portable, canonical names relative to the executable directory.
// Files absent from a snapshot are absent after a full restore too.
struct SettingsSnapshot {
    std::string application_version;
    std::map<std::string, std::vector<unsigned char>> files;
    [[nodiscard]] std::string text(std::string_view name) const {
        const auto found = files.find(std::string(name));
        return found == files.end() ? std::string{} : std::string(found->second.begin(), found->second.end());
    }
};

class SettingsBackup final {
public:
    using Validator = std::function<void(const SettingsSnapshot&)>;
    struct ExternalState {
        std::function<std::string()> capture;
        std::function<void()> apply;
        std::function<void(const std::string&)> rollback;
    };
    // Fault injection for durable transaction tests. Never used by the UI.
    using Checkpoint = std::function<void(std::string_view)>;

    static SettingsSnapshot capture(const std::filesystem::path& root, std::string version);
    static void write(const SettingsSnapshot&, const std::filesystem::path&);
    static SettingsSnapshot read(const std::filesystem::path&);
    static void validate(const SettingsSnapshot&);
    static bool has_configuration(const std::filesystem::path& root);
    static bool pending(const std::filesystem::path& root);
    // Only an unapplied restore can be cancelled. Preserve its package for diagnosis.
    static void cancel_pending(const std::filesystem::path& root);
    static void stage(const std::filesystem::path& root, const SettingsSnapshot&, const Validator& = {});
    // Call while holding the single-instance lock and before starting modules.
    // On interruption the next call rolls back, never retries a partial restore.
    static bool recover(const std::filesystem::path& root, const Validator& = {},
        const ExternalState& = {}, const Checkpoint& = {});
};

} // namespace simpilot
