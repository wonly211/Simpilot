#include "simpilot/settings_registry.hpp"

#include <stdexcept>

namespace simpilot {

SettingsSession::SettingsSession(const SettingsParticipantRegistry& participants,
                                 SettingsDocument document, DiagnosticSink diagnose)
    : document_(std::move(document)), diagnostic_sink_(std::move(diagnose)) {
    try {
        participants.visit_guarded([this](const auto& id, const auto& callbacks, auto active) {
            participants_.push_back({id, callbacks, std::move(active)});
            if (callbacks.begin) callbacks.begin();
        });
    } catch (...) {
        cancel();
        throw;
    }
}

SettingsSession::~SettingsSession() { cancel(); }

bool SettingsSession::dirty() const {
    for (const auto& participant : participants_) {
        if (participant.active() && participant.callbacks.dirty
            && participant.callbacks.dirty()) return true;
    }
    return false;
}

bool SettingsSession::apply(const Commit& commit) {
    if (closed_ || !commit) return false;
    std::size_t prepared = 0;
    bool committed = false;
    try {
        auto next = document_;
        for (const auto& participant : participants_) {
            if (!participant.active()) return false;
            const auto& callbacks = participant.callbacks;
            if (callbacks.validate && !callbacks.validate()) return false;
            if (callbacks.write) callbacks.write(next);
        }
        for (const auto& participant : participants_) {
            if (!participant.active()) throw std::runtime_error("Settings owner stopped");
            ++prepared;
            if (participant.callbacks.prepare && !participant.callbacks.prepare()) {
                throw std::runtime_error("Settings preparation failed");
            }
        }
        for (const auto& participant : participants_) {
            if (!participant.active()) throw std::runtime_error("Settings owner stopped");
            if (participant.callbacks.apply && !participant.callbacks.apply()) {
                throw std::runtime_error("Settings application failed");
            }
        }
        if (!commit(next)) throw std::runtime_error("Settings persistence failed");
        document_ = std::move(next);
        committed = true;
    } catch (...) {
        diagnose("Settings transaction failed");
    }
    if (!committed) {
        while (prepared != 0) {
            const auto& participant = participants_[--prepared];
            try {
                if (!participant.active()) continue;
                if (participant.callbacks.rollback && !participant.callbacks.rollback()) {
                    diagnose("Settings rollback failed; recovery data must be retained");
                    diagnose(participant.id);
                }
            } catch (...) {
                diagnose("Settings rollback threw; recovery data must be retained");
                diagnose(participant.id);
            }
        }
        return false;
    }
    for (const auto& participant : participants_) {
        try {
            if (!participant.active()) continue;
            if (participant.callbacks.finish) participant.callbacks.finish();
        } catch (...) {
            diagnose("Settings committed, but finalization failed");
            diagnose(participant.id);
        }
    }
    return true;
}

void SettingsSession::cancel() noexcept {
    if (std::exchange(closed_, true)) return;
    for (auto participant = participants_.rbegin(); participant != participants_.rend();
         ++participant) {
        try {
            if (!participant->active()) continue;
            if (participant->callbacks.cancel) participant->callbacks.cancel();
        } catch (...) {
            diagnose("Settings draft cleanup failed");
        }
    }
}

void SettingsSession::diagnose(std::string_view message) const noexcept {
    try { if (diagnostic_sink_) diagnostic_sink_(message); } catch (...) {}
}

} // namespace simpilot
