#pragma once

#include "simpilot/contribution_registry.hpp"
#include "simpilot/localization.hpp"
#include "simpilot/settings_document.hpp"

#include <Windows.h>

namespace simpilot {

struct SettingsPageContext {
    HINSTANCE instance;
    HWND parent;
    UINT dpi;
    HFONT font;
    const Localization& localization;
    std::function<void()> changed;
    std::function<bool(std::string)> change_language;
};

class ISettingsPage {
public:
    virtual ~ISettingsPage() = default;
    virtual void create(const SettingsPageContext& context) = 0;
    virtual void layout(RECT bounds, UINT dpi, HFONT font) = 0;
    virtual void show(bool visible) = 0;
    virtual void refresh_language(const Localization& localization) = 0;
};

struct SettingsPageContribution {
    std::string title_key;
    std::function<std::unique_ptr<ISettingsPage>()> create;
};

using SettingsRegistry = ContributionRegistry<SettingsPageContribution>;

// Session callbacks close over module-owned typed drafts. No aggregate feature
// settings type is required by either the transaction or the settings host.
struct SettingsParticipant {
    std::function<void()> begin;
    std::function<bool()> dirty;
    std::function<bool()> validate;
    std::function<bool()> prepare;
    std::function<bool()> apply;
    std::function<void(SettingsDocument&)> write;
    std::function<bool()> rollback;
    std::function<void()> finish;
    std::function<void()> cancel;
};

using SettingsParticipantRegistry = ContributionRegistry<SettingsParticipant>;

class SettingsSession final {
public:
    using DiagnosticSink = std::function<void(std::string_view)>;
    using Commit = std::function<bool(const SettingsDocument&)>;

    SettingsSession(const SettingsParticipantRegistry& participants,
                    SettingsDocument document, DiagnosticSink diagnose = {});
    ~SettingsSession();
    SettingsSession(const SettingsSession&) = delete;
    SettingsSession& operator=(const SettingsSession&) = delete;
    [[nodiscard]] bool dirty() const;
    // commit must either persist the candidate or restore its own external
    // changes before returning false. The session restores participant state.
    [[nodiscard]] bool apply(const Commit& commit);
    void cancel() noexcept;
    [[nodiscard]] const SettingsDocument& document() const noexcept { return document_; }

private:
    struct Participant {
        std::string id;
        SettingsParticipant callbacks;
        std::function<bool()> active;
    };
    void diagnose(std::string_view message) const noexcept;
    std::vector<Participant> participants_;
    SettingsDocument document_;
    DiagnosticSink diagnostic_sink_;
    bool closed_ = false;
};

} // namespace simpilot
