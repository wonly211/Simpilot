#include "simpilot/app_module.hpp"
#include "simpilot/app_settings.hpp"
#include "simpilot/hotkey_registry.hpp"
#include "simpilot/contribution_registry.hpp"
#include "simpilot/program_search_registry.hpp"
#include "simpilot/settings_document.hpp"
#include "simpilot/settings_registry.hpp"
#include "simpilot/ui_dispatcher.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class TestModule final : public simpilot::IAppModule {
public:
    TestModule(std::vector<int>& events, int id, bool fail = false)
        : events_(events), id_(id), fail_(fail) {}
    void start() override {
        events_.push_back(id_);
        if (fail_) throw std::runtime_error("Injected startup failure");
    }
    void stop() noexcept override { events_.push_back(-id_); }
private:
    std::vector<int>& events_;
    int id_;
    bool fail_;
};

void lifecycle_tests() {
    std::vector<int> events;
    simpilot::ModuleRegistry registry;
    registry.add("one", std::make_unique<TestModule>(events, 1));
    registry.add("two", std::make_unique<TestModule>(events, 2));
    bool duplicate = false;
    try { registry.add("one", std::make_unique<TestModule>(events, 3)); }
    catch (const std::invalid_argument&) { duplicate = true; }
    require(duplicate, "Duplicate module IDs must fail");
    registry.start();
    registry.start();
    registry.stop();
    registry.stop();
    require(events == std::vector<int>{1, 2, -2, -1}, "Ordered idempotent lifecycle");
    events.clear();
    registry.start();
    registry.stop();
    require(events == std::vector<int>{1, 2, -2, -1}, "Restart lifecycle");

    events.clear();
    simpilot::ModuleRegistry failed;
    failed.add("one", std::make_unique<TestModule>(events, 1));
    failed.add("two", std::make_unique<TestModule>(events, 2, true));
    failed.add("three", std::make_unique<TestModule>(events, 3));
    bool threw = false;
    try { failed.start(); } catch (const std::runtime_error&) { threw = true; }
    require(threw && !failed.running(), "Failed startup must propagate");
    require(events == std::vector<int>{1, 2, -2, -1}, "Rollback includes partial startup");

    struct LifetimeModule final : simpilot::IAppModule {
        std::vector<int>& events;
        int id;
        LifetimeModule(std::vector<int>& target, int identifier) : events(target), id(identifier) {}
        ~LifetimeModule() override { events.push_back(100 + id); }
        void start() override { events.push_back(id); }
        void stop() noexcept override { events.push_back(-id); }
    };
    std::vector<int> destruction;
    destruction.reserve(8);
    simpilot::ModuleRegistry shutdown;
    shutdown.add("first", std::make_unique<LifetimeModule>(destruction, 1));
    shutdown.add("second", std::make_unique<LifetimeModule>(destruction, 2));
    shutdown.start();
    shutdown.clear();
    shutdown.clear();
    require(shutdown.size() == 0 && destruction == std::vector<int>{1, 2, -2, -1, 102, 101},
        "Explicit shutdown destroys modules in reverse order while shared services are alive");
}

void contribution_tests() {
    simpilot::ContributionRegistry<std::function<void()>> registry;
    std::vector<int> values;
    auto second = registry.add("second", 20, [&] { values.push_back(2); });
    auto first = registry.add("first", 10, [&] {
        values.push_back(1);
        second.reset();
    });
    bool duplicate = false;
    try { (void)registry.add("first", 0, [] {}); }
    catch (const std::invalid_argument&) { duplicate = true; }
    require(duplicate, "Duplicate contribution IDs must fail");
    registry.visit([](const auto&, const auto& callback) { callback(); });
    require(values == std::vector<int>{1}, "Revoked entries must not run in snapshots");
    auto moved = std::move(first);
    first.reset();
    require(registry.size() == 1, "Moving a registration must retain ownership");
    moved.reset();
    require(registry.size() == 0, "Reset must revoke ownership");

    simpilot::Registration survivor;
    {
        simpilot::ContributionRegistry<int> temporary;
        survivor = temporary.add("survivor", 0, 1);
    }
    survivor.reset();
}

void document_tests() {
    const std::string text = "\xEF\xBB\xBF; preserved\n[General]\r\nLanguage=zh-CN\n"
        "Mixed = first\n[Unknown]\nMiXeD=last\nFutureOption=42\n; no terminator";
    auto document = simpilot::SettingsDocument::parse(text);
    require(document.serialize() == text, "Unchanged INI must round-trip byte-for-byte");
    require(document.get(L"MIXED") == L"last", "Legacy last-key lookup ignores sections");
    document.set(L"General", L"mixed", L"updated");
    require(document.get(L"Mixed") == L"updated", "Update effective duplicate key");
    require(document.serialize().find("Mixed = first") != std::string::npos,
            "Untouched duplicate and formatting preserved");
    document.set(L"MouseLocator", L"NewSetting", L"1");
    require(document.get(L"FutureOption") == L"42", "Unknown settings retained");
    document.erase(L"MIXED");
    require(!document.get(L"mixed"), "Erase removes all duplicate owned keys");
    require(document.serialize().find("; no terminator") != std::string::npos,
            "Unknown comments retained");
    bool rejected = false;
    try { document.set(L"General", L"Bad", L"first\ninjected=1"); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "INI values cannot inject lines");

    const auto root = std::filesystem::temp_directory_path()
        / (L"simpilot-document-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto file = root / L"Setting.ini";
    require(document.save(file), "Atomic settings document save");
    require(simpilot::SettingsDocument::load(file).serialize() == document.serialize(),
            "Document disk round-trip");
    require(simpilot::AppSettingsStore::save(file, {}), "Legacy save uses document overlay");
    auto updated = simpilot::SettingsDocument::load(file);
    require(updated.get(L"FutureOption") == L"42" && updated.get(L"NewSetting") == L"1",
            "Legacy save preserves unregistered modules and future keys");
    updated.set(L"Future", L"CustomGlobalHotKey99Program", L"obsolete");
    updated.set(L"Future", L"CustomGlobalHotKey99FutureField", L"preserved");
    require(updated.save(file), "Prepare obsolete array entry");
    require(simpilot::AppSettingsStore::save(file, {}), "Save without custom hotkey owner");
    updated = simpilot::SettingsDocument::load(file);
    require(updated.get(L"CustomGlobalHotKey99Program") == L"obsolete"
            && updated.get(L"CustomGlobalHotKey99FutureField") == L"preserved",
            "Shell does not clean up entries belonging to an absent module");
    constexpr std::wstring_view owned_suffixes[]{L"Program"};
    updated.erase_numbered(L"CustomGlobalHotKey", owned_suffixes);
    require(!updated.get(L"CustomGlobalHotKey99Program")
            && updated.get(L"CustomGlobalHotKey99FutureField") == L"preserved",
            "Array owner cleans only explicitly owned fields");
    require(!document.save(root), "Directory cannot be replaced with settings file");
    std::filesystem::remove_all(root);
}

void transaction_tests() {
    int runtime = 1;
    int draft = 1;
    int previous = 1;
    bool fail = false;
    bool restored = false;
    simpilot::SettingsParticipantRegistry registry;
    auto participant = registry.add("test", 0, simpilot::SettingsParticipant{
        .begin = [&] { draft = runtime; },
        .dirty = [&] { return draft != runtime; },
        .validate = [&] { return draft >= 0; },
        .prepare = [&] { previous = runtime; return true; },
        .apply = [&] { runtime = draft; return !fail; },
        .write = [&](auto& document) { document.set(L"Test", L"Value", std::to_wstring(draft)); },
        .rollback = [&] { runtime = previous; restored = true; return true; },
        .finish = [&] { previous = runtime; },
        .cancel = [&] { draft = runtime; }});
    simpilot::SettingsSession session(registry,
        simpilot::SettingsDocument::parse("[Unknown]\nKeep=42\n"));
    draft = 2;
    require(session.dirty(), "Session detects module-owned draft changes");
    require(!session.apply([](const auto&) { return false; }), "Reject failed persistence");
    require(runtime == 1 && restored && draft == 2, "Restore runtime but retain draft");
    require(!session.document().get(L"Value"), "Failed commit must not advance document");
    fail = true;
    require(!session.apply([](const auto&) { return true; }), "Reject partial runtime update");
    require(runtime == 1, "Partial update rollback");
    fail = false;
    require(session.apply([](const auto& document) {
        return document.get(L"Value") == L"2" && document.get(L"Keep") == L"42";
    }), "Successful commit retains unknown sections");
    require(runtime == 2 && !session.dirty(), "Advance applied baseline");
    draft = 3;
    session.cancel();
    require(draft == 2 && runtime == 2, "Cancel restores last applied baseline");
    require(!session.apply([](const auto&) { return true; }), "Closed session cannot apply");

    std::vector<int> events;
    simpilot::SettingsParticipantRegistry failure_registry;
    auto one = failure_registry.add("one", 0, simpilot::SettingsParticipant{
        .prepare = [&] { events.push_back(1); return true; },
        .rollback = [&] { events.push_back(-1); return true; }});
    auto two = failure_registry.add("two", 1, simpilot::SettingsParticipant{
        .prepare = [&] { events.push_back(2); return false; },
        .rollback = [&] { events.push_back(-2); return true; }});
    simpilot::SettingsSession failure_session(failure_registry, {});
    require(!failure_session.apply([](const auto&) { return true; }), "Prepare failure");
    require(events == std::vector<int>{1, 2, -2, -1}, "Preparation rolls back in reverse");
    one.reset();
    two.reset();
    require(!failure_session.apply([](const auto&) { return true; }),
            "Revoked participants cannot be called by an existing session");
    failure_session.cancel();
}

void hotkey_tests() {
    simpilot::HotkeyRegistry registry;
    simpilot::BuiltInHotKey binding{{simpilot::HotKeyGesture{MOD_WIN, L'S'}, false}, true};
    int invoked = 0;
    auto first = registry.add("search", 10, simpilot::HotkeyContribution{
        "search", [&] { return binding; }, [&](HWND) { ++invoked; }, {}});
    auto duplicate = registry.add("duplicate", 20, simpilot::HotkeyContribution{
        "duplicate", [&] { return binding; }, [&](HWND) { invoked += 100; }, {}});
    int registered = 0;
    int conflicts = 0;
    int identifier = 0;
    registry.refresh([&](int id, const simpilot::HotKeyBinding& value) {
        require(value.force_override, "Win+letter forces override");
        identifier = id;
        ++registered;
        return true;
    }, [&](auto) { ++conflicts; });
    require(registered == 1 && conflicts == 1, "Registry resolves duplicate gestures centrally");
    require(registry.dispatch(identifier, nullptr) && invoked == 1, "Dispatch registered action");
    require(registry.requires_windows_blocking(L'S' - L'A'), "Registry exposes required blocking");
    first.reset();
    require(registry.dispatch(identifier, nullptr) && invoked == 1, "Revoked action cannot run");
    binding.binding.gesture = simpilot::HotKeyGesture{MOD_WIN, L'L'};
    registered = 0;
    registry.refresh([&](int, const auto&) { ++registered; return true; });
    require(registered == 0, "Win+L cannot be registered");
    duplicate.reset();
    registry.clear_dispatch();
    require(!registry.dispatch(identifier, nullptr), "Cleared dispatch drops queued identifiers");
}

void hotkey_draft_tests() {
    simpilot::HotkeyRegistry registry;
    const simpilot::BuiltInHotKey original{{simpilot::HotKeyGesture{MOD_WIN, L'S'}, true}, true};
    auto first = original;
    auto second = original;
    simpilot::BuiltInHotKey target;
    auto a = registry.add("one", 0, {"one", [&] { return original; }, [](HWND) {}, [&] { return &first; }});
    auto b = registry.add_editable_drafts("two", [&] {
        return std::vector<simpilot::HotkeyDraftBinding>{{"two", "two",
            [&] { return second; }, [&](auto value) { second = value; }, {}}};
    });
    int confirmations = 0;
    int notifications = 0;
    auto observer = registry.observe_drafts("test", [&] { ++notifications; });
    require(!registry.replace_conflicts("target", original, [&](const auto&) {
        return ++confirmations != 2;
    }, [&] { target = original; }), "Declining any conflict cancels the entire edit");
    require(first == original && second == original && !target.enabled && notifications == 0,
        "No partial conflict clearing when confirmation is cancelled");
    require(registry.draft_requires_windows_blocking(L'S' - L'A'), "Policy sees all contributed drafts");
    require(registry.replace_conflicts("target", original, [](const auto&) { return true; },
        [&] { target = original; }), "Accept cross-provider replacement");
    require(!first.binding.gesture && !second.binding.gesture && target == original && notifications == 1,
        "Update both drafts before notifying observers");
    require(registry.requires_windows_blocking(L'S' - L'A'), "Runtime is independent of draft changes");
    first = original;
    second = original;
    require(!registry.replace_conflicts("target", original, [&](const auto&) {
        b.reset();
        return true;
    }, [] {}), "Revocation during modal confirmation invalidates the plan");
    require(first == original && second == original, "Revoked edit does not clear other owners");
    observer.reset();
    registry.drafts_changed();
    require(notifications == 1, "Destroyed page stops receiving draft notifications");
}

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void dispatcher_tests() {
    simpilot::UiDispatcher dispatcher(GetModuleHandleW(nullptr));
    simpilot::DispatchScope alive;
    int calls = 0;
    const auto ui_thread = GetCurrentThreadId();
    std::thread worker([&] {
        require(dispatcher.post(alive, [&] {
            require(GetCurrentThreadId() == ui_thread, "Dispatch on UI thread");
            ++calls;
        }), "Post from worker");
    });
    worker.join();
    pump();
    require(calls == 1, "Posted callback executed");
    {
        simpilot::DispatchScope dead;
        require(dispatcher.post(dead, [&] { ++calls; }), "Post cancellable callback");
    }
    pump();
    require(calls == 1, "Destroyed scope suppresses queued callback");
    auto timer = dispatcher.repeat(alive, 1, [&] { ++calls; });
    const auto deadline = GetTickCount64() + 1000;
    while (calls == 1 && GetTickCount64() < deadline) { Sleep(10); pump(); }
    require(calls > 1, "UI timer executed");
    timer.reset();
    const auto before = calls;
    Sleep(30);
    pump();
    require(calls == before, "Cancelled timer cannot execute");
    bool caught = false;
    dispatcher.set_error_sink([&] { caught = true; });
    require(dispatcher.post(alive, [] { throw std::runtime_error("Injected"); }), "Post failure");
    pump();
    require(caught, "Callback exception contained at Win32 boundary");
    require(dispatcher.post(alive, [&] { ++calls; }), "Queue callback before shutdown");
    auto pending_timer = dispatcher.repeat(alive, 1, [&] { ++calls; });
    dispatcher.close();
    dispatcher.close();
    pump();
    require(calls == before && !dispatcher.post(alive, [&] { ++calls; }),
        "Closed dispatcher discards queued callbacks and rejects new work");
    bool rejected = false;
    try { (void)dispatcher.repeat(alive, 1, [] {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Closed dispatcher must not create a thread timer without an owner window");
    pending_timer.reset();
}

class Search final : public simpilot::IProgramSearch {
public:
    bool available() const override { return true; }
    std::vector<simpilot::ProgramCandidate> find_exact_file_name(
        const std::wstring&) const override { return {{L"C:\\example.exe"}}; }
};

void search_tests() {
    simpilot::ProgramSearchRegistry registry;
    require(!registry.available(), "Search is optional");
    Search provider;
    auto registration = registry.add("test", provider);
    require(registry.available(), "Provider availability");
    require(registry.find_exact_file_name(L"example.exe").size() == 1, "Provider delegation");
    auto duplicate = registry.add("duplicate", provider);
    require(registry.find_exact_file_name(L"example.exe").size() == 1, "Deduplicate candidates");
    registration.reset();
    duplicate.reset();
    require(!registry.available(), "Provider revocation");
}

} // namespace

int main() {
    try {
        lifecycle_tests();
        contribution_tests();
        document_tests();
        transaction_tests();
        hotkey_tests();
        hotkey_draft_tests();
        dispatcher_tests();
        search_tests();
        std::cout << "Module support tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
