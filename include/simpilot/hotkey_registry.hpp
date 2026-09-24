#pragma once

#include "simpilot/contribution_registry.hpp"
#include "simpilot/hotkey.hpp"
#include "simpilot/settings_registry.hpp"

#include <unordered_map>

namespace simpilot {

struct HotkeyContribution {
    std::string label_key;
    std::function<BuiltInHotKey()> current;
    std::function<void(HWND)> invoke;
    // Present only for bindings editable on the common settings page.
    std::function<BuiltInHotKey*()> draft;
};

struct HotkeyDraftBinding {
    std::string id;
    std::string label_key;
    std::function<BuiltInHotKey()> read;
    // Assignment must not throw, allocate, or invalidate another draft binding.
    std::function<void(BuiltInHotKey)> write;
    std::function<bool()> active;
    bool common_row = true;
};

class HotkeyRegistry final {
public:
    using Register = std::function<bool(int, const HotKeyBinding&)>;
    using DiagnosticSink = std::function<void(std::string_view)>;

    [[nodiscard]] Registration add(std::string id, int order, HotkeyContribution value) {
        if (!value.current || !value.invoke) throw std::invalid_argument("Invalid hotkey");
        return contributions_.add(std::move(id), order, std::move(value));
    }
    template <typename Visitor>
    void visit(Visitor&& visitor) const { contributions_.visit(std::forward<Visitor>(visitor)); }

    using DraftSource = std::function<std::vector<HotkeyDraftBinding>()>;
    [[nodiscard]] Registration add_editable_drafts(std::string id, DraftSource source) {
        return editable_drafts_.add(std::move(id), 0, std::move(source));
    }
    [[nodiscard]] Registration observe_drafts(std::string id, std::function<void()> callback) {
        return draft_observers_.add(std::move(id), 0, std::move(callback));
    }
    void drafts_changed() const {
        ++draft_revision_;
        draft_observers_.visit([](const auto&, const auto& notify) { notify(); });
    }
    [[nodiscard]] std::vector<HotkeyDraftBinding> editable_drafts() const {
        std::vector<HotkeyDraftBinding> result;
        contributions_.visit_guarded([&](const auto& id, const auto& value, auto active) {
            if (!value.draft || !value.draft()) return;
            const auto draft = value.draft;
            result.push_back({id, value.label_key,
                [draft] { return *draft(); },
                [draft](BuiltInHotKey next) { *draft() = next; }, std::move(active)});
        });
        editable_drafts_.visit_guarded([&](const auto&, const auto& source, auto active) {
            for (auto item : source()) {
                const auto inner = item.active;
                item.active = [active, inner] { return active() && (!inner || inner()); };
                result.push_back(std::move(item));
            }
        });
        for (auto item = result.begin(); item != result.end(); ++item) {
            if (item->id.empty() || !item->read || !item->write
                || std::find_if(result.begin(), item, [&](const auto& prior) {
                    return prior.id == item->id;
                }) != item) throw std::invalid_argument("Invalid or duplicate hotkey draft");
        }
        return result;
    }
    // Confirm every replacement before changing any draft. Revalidate after
    // modal confirmation because its nested message loop may revoke a module.
    [[nodiscard]] bool replace_conflicts(
        std::string_view except, const BuiltInHotKey& candidate,
        const std::function<bool(const HotkeyDraftBinding&)>& confirm,
        const std::function<void()>& commit) const {
        if (!candidate.binding.gesture) return false;
        const auto letter = windows_letter_hotkey_index(*candidate.binding.gesture);
        if (letter && *letter == static_cast<std::size_t>(L'L' - L'A')) return false;
        const auto revision = draft_revision_;
        const auto bindings = editable_drafts();
        std::vector<std::pair<HotkeyDraftBinding, BuiltInHotKey>> conflicts;
        for (const auto& item : bindings) {
            if (item.id == except || !item.active()) continue;
            const auto value = item.read();
            if (value.binding.gesture != candidate.binding.gesture) continue;
            conflicts.emplace_back(item, value);
        }
        for (const auto& [item, value] : conflicts) {
            if (!confirm || !confirm(item)) return false;
        }
        if (revision != draft_revision_) return false;
        for (const auto& item : bindings) if (!item.active()) return false;
        for (const auto& [item, value] : conflicts) if (item.read() != value) return false;
        for (const auto& [item, value] : conflicts) item.write({});
        commit();
        drafts_changed();
        return true;
    }

    // Editing sections are owned by modules; the common hotkey page hosts them.
    SettingsRegistry sections;

    [[nodiscard]] bool draft_requires_windows_blocking(std::size_t letter) const {
        if (letter == static_cast<std::size_t>(L'L' - L'A') || letter >= 26) return false;
        bool result = false;
        for (const auto& item : editable_drafts()) {
            const auto value = item.read();
            result = result || (value.enabled && value.binding.gesture
                && windows_letter_hotkey_index(*value.binding.gesture) == letter);
        }
        return result;
    }

    // Caller unregisters the previous backend registrations before refreshing.
    void refresh(const Register& register_binding, const DiagnosticSink& diagnose = {}) {
        dispatch_.clear();
        std::vector<HotKeyGesture> used;
        int identifier = 100;
        contributions_.visit_guarded([&](const auto& id, const auto& contribution, auto active) {
            auto current = contribution.current();
            if (!current.enabled || !current.binding.gesture) return;
            const auto gesture = *current.binding.gesture;
            const auto letter = windows_letter_hotkey_index(gesture);
            if ((letter && *letter == static_cast<std::size_t>(L'L' - L'A'))
                || std::ranges::find(used, gesture) != used.end()
                || identifier > 0xBFFF) {
                if (diagnose) diagnose(id);
                return;
            }
            if (is_supported_windows_letter_hotkey(gesture)) current.binding.force_override = true;
            const auto assigned = identifier++;
            if (!register_binding(assigned, current.binding)) {
                if (diagnose) diagnose(id);
                return;
            }
            used.push_back(gesture);
            dispatch_.emplace(assigned, [active = std::move(active),
                                        invoke = contribution.invoke](HWND owner) {
                if (active()) invoke(owner);
            });
        });
    }
    [[nodiscard]] bool dispatch(int identifier, HWND owner) const {
        const auto found = dispatch_.find(identifier);
        if (found == dispatch_.end()) return false;
        const auto callback = found->second;
        callback(owner);
        return true;
    }
    void clear_dispatch() noexcept { dispatch_.clear(); }
    [[nodiscard]] bool requires_windows_blocking(std::size_t letter) const {
        if (letter == static_cast<std::size_t>(L'L' - L'A') || letter >= 26) return false;
        bool result = false;
        contributions_.visit([&](const auto&, const auto& contribution) {
            const auto value = contribution.current();
            result = result || (value.enabled && value.binding.gesture
                && windows_letter_hotkey_index(*value.binding.gesture) == letter);
        });
        return result;
    }

private:
    ContributionRegistry<HotkeyContribution> contributions_;
    ContributionRegistry<DraftSource> editable_drafts_;
    ContributionRegistry<std::function<void()>> draft_observers_;
    std::unordered_map<int, std::function<void(HWND)>> dispatch_;
    mutable std::size_t draft_revision_ = 0;
};

} // namespace simpilot
