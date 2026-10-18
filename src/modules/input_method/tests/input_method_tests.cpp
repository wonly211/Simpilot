#include "input_method_model.hpp"
#include "input_method_runtime.hpp"
#include "test_backend.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace simpilot;
using namespace simpilot::input_method;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr auto app_a = L"C:\\Apps\\Editor.exe";
constexpr auto app_b = L"C:\\Apps\\Browser.exe";
const State chinese{L"tsf:pinyin", Mode::chinese};
const State english{L"layout:00000409", Mode::english};

void codec_tests() {
    auto document = SettingsDocument::parse(
        "; preserved\r\n[InputMethod]\r\nInputMethodEnabled=1\r\nInputMethodRuleCount=2\r\n"
        "InputMethodRule1Path=C:\\Apps\\Editor.exe\r\nInputMethodRule1Policy=fixed\r\n"
        "InputMethodRule1Profile=layout:00000409\r\nInputMethodRule1Mode=english\r\n"
        "InputMethodRule2Path=C:\\Apps\\Browser.exe\r\nInputMethodRule2Policy=ignore\r\n"
        "InputMethodRule2Future=keep\r\nUnknownInputMethodKey=keep\r\n");
    auto settings = Settings::read(document);
    require(settings.enabled && settings.rules.size() == 2, "legacy INI keys not read");
    require(settings.rules[0].policy == Policy::fixed
        && settings.rules[1].policy == Policy::ignore, "policies not read");
    settings.rules.resize(1);
    settings.rules[0].mode = Mode::chinese;
    settings.write(document);
    const auto output = document.serialize();
    require(output.starts_with("; preserved\r\n"), "comments were lost");
    require(output.find("UnknownInputMethodKey=keep") != std::string::npos
        && output.find("InputMethodRule2Future=keep") != std::string::npos,
        "unknown fields were lost");
    require(output.find("InputMethodRule2Path=") == std::string::npos,
        "owned obsolete keys were not removed");
    require(Settings::read(document) == settings, "settings round-trip failed");
    document.set(L"General", L"Language", L"en-US");
    require(Settings::read(document) == settings, "unrelated save changed rules");

    settings.rules.push_back({L"c:\\apps\\EDITOR.exe"});
    require(!validate(settings), "case-insensitive duplicate paths accepted");
    settings.rules.pop_back();
    settings.rules[0].profile_id.clear();
    require(!validate(settings), "fixed rule with no profile accepted");
    require(normalize_path(L"C:\\Apps\\..\\Apps\\EDITOR.exe") == normalize_path(app_a),
        "absolute path normalization failed");
    require(normalize_path(L"Apps\\Editor.exe").empty(), "relative path accepted");
    require(normalize_path(L"C:/Apps/EDITOR.exe") == normalize_path(app_a)
        && normalize_path(L"\\\\?\\C:\\Apps\\Editor.exe") == normalize_path(app_a),
        "separator or long-path spelling created duplicate identities");
    require(normalize_path(L"C:\\Apps\\\u00C4PP.exe")
        == normalize_path(L"C:\\Apps\\\u00E4pp.exe"), "Unicode path casing not normalized");

    History history;
    history.states.emplace(normalize_path(app_a), chinese);
    auto history_doc = SettingsDocument::parse(
        "# custom\n[InputMethodHistory]\nUnknown=keep\nInputMethodHistory8Future=keep\n");
    history.write(history_doc);
    require(History::read(history_doc).states == history.states, "history round-trip failed");
    const auto persisted = history_doc.serialize();
    require(persisted.find("Unknown=keep") != std::string::npos
        && persisted.find("InputMethodHistory8Future=keep") != std::string::npos,
        "unknown history fields lost");
    const auto file = std::filesystem::temp_directory_path()
        / (L"Simpilot-InputMethodTest-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    require(history_doc.save(file), "atomic history save failed");
    require(History::read(SettingsDocument::load(file)).states == history.states,
        "history not restored after reopening");
    auto replacement = history_doc;
    replacement.set(L"InputMethodHistory", L"Unknown", L"modified");
    const auto locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(locked != INVALID_HANDLE_VALUE, "could not lock isolated history fixture");
    const bool replaced = replacement.save(file);
    CloseHandle(locked);
    require(!replaced && SettingsDocument::load(file).serialize() == persisted,
        "failed atomic replacement damaged original history");
    std::filesystem::remove(file);
    bool rejected = false;
    try { (void)Settings::read(SettingsDocument::parse("InputMethodRuleCount=-1\n")); }
    catch (...) { rejected = true; }
    require(rejected, "corrupt counts silently accepted");
}

void runtime_tests() {
    test::Backend backend;
    int saves = 0;
    History persisted;
    Runtime runtime(backend, [&](const History& history) {
        ++saves;
        persisted = history;
        return true;
    });
    backend.activate(app_a);
    backend.state[normalize_path(app_a)] = english;
    backend.state[normalize_path(app_b)] = chinese;
    runtime.refresh_profiles();
    runtime.tick();
    require(backend.polls == 0 && backend.reads == 0 && saves == 0,
        "default-disabled runtime performed work");
    runtime.configure({true, {}});
    runtime.tick();
    require(backend.profile_requests == 0, "first visit changed unknown state");
    backend.state[normalize_path(app_a)] = chinese;
    runtime.tick();
    backend.activate(app_b);
    runtime.tick();
    require(persisted.states.at(normalize_path(app_a)) == chinese,
        "departing application state was contaminated");
    backend.state[normalize_path(app_a)] = english;
    backend.activate(app_a);
    for (int i = 0; i < 4; ++i) runtime.tick();
    require(backend.state.at(normalize_path(app_a)) == chinese, "history not restored");
    require(runtime.status().key == "settings.input_method.state_remembered",
        "restoration not verified");
    const int applied = backend.profile_requests + backend.mode_requests;
    backend.state[normalize_path(app_a)] = english;
    runtime.tick();
    backend.activate(app_a, 9);
    runtime.tick();
    require(backend.profile_requests + backend.mode_requests == applied,
        "same application or manual changes reapplied rule");
    runtime.stop();
    require(persisted.states.at(normalize_path(app_a)) == english,
        "shutdown did not flush last observed manual state");
    const int polls = backend.polls;
    runtime.tick();
    require(backend.polls == polls, "stopped runtime kept polling");

    test::Backend fixed_backend;
    fixed_backend.activate(app_a);
    fixed_backend.state[normalize_path(app_a)] = chinese;
    Runtime fixed(fixed_backend, [](const History&) { return true; });
    fixed.refresh_profiles();
    fixed.set_history(History{{{normalize_path(app_a), chinese}}});
    const Settings fixed_settings{true, {{app_a, Policy::fixed, english.profile_id, english.mode}}};
    fixed.configure(fixed_settings);
    for (int i = 0; i < 4; ++i) fixed.tick();
    require(fixed_backend.state.at(normalize_path(app_a)) == english,
        "fixed rule did not override memory");
    require(fixed.status().key == "settings.input_method.state_applied", "fixed rule unverified");
    fixed_backend.error = ERROR_SUCCESS;
    fixed_backend.active = {};
    fixed.tick();
    require(fixed.status().key == "settings.input_method.state_applied",
        "Simpilot foreground hid the recent application result");
    auto draft = fixed_settings;
    draft.rules[0] = {app_a, Policy::ignore, {}, Mode::english};
    fixed_backend.activate(app_b);
    fixed.tick();
    fixed_backend.activate(app_a);
    fixed_backend.state[normalize_path(app_a)] = chinese;
    for (int i = 0; i < 4; ++i) fixed.tick();
    require(fixed_backend.state.at(normalize_path(app_a)) == english,
        "unsubmitted draft changed runtime");

    test::Backend ignored_backend;
    ignored_backend.activate(app_a);
    Runtime ignored(ignored_backend, [&](const History&) { ++saves; return true; });
    ignored.refresh_profiles();
    ignored.configure({true, {{app_a, Policy::ignore, {}, Mode::english}}});
    ignored.tick();
    require(ignored_backend.reads == 0 && ignored_backend.profile_requests == 0,
        "ignored application was read or controlled");
    ignored_backend.activate(app_b);
    ignored.tick();
    require(!ignored.history().states.contains(normalize_path(app_a)),
        "ignored application was recorded");
}

void failure_tests() {
    test::Backend backend;
    backend.activate(app_a);
    backend.state[normalize_path(app_a)] = english;
    Runtime runtime(backend, [](const History&) { return false; });
    runtime.refresh_profiles();
    runtime.configure({true, {{app_a, Policy::fixed, L"missing", Mode::chinese}}});
    runtime.tick();
    require(runtime.status().key == "settings.input_method.unavailable_profile",
        "missing profile not reported");
    require(backend.profile_requests == 0, "missing profile generated request");
    backend.fail = true;
    runtime.configure({true, {{app_a, Policy::fixed, chinese.profile_id, chinese.mode}}});
    runtime.tick();
    require(runtime.status().error == ERROR_ACCESS_DENIED, "permission failure not reported");
    backend.fail = false;
    backend.reject = true;
    runtime.configure({false, {}});
    runtime.configure({true, {{app_a, Policy::fixed, chinese.profile_id, chinese.mode}}});
    for (int i = 0; i < 8; ++i) runtime.tick();
    require(runtime.status().key == "settings.input_method.state_failed"
        && runtime.status().error == ERROR_TIMEOUT, "rejected request was reported as success");
    const int requests = backend.profile_requests;
    for (int i = 0; i < 10; ++i) runtime.tick();
    require(backend.profile_requests == requests, "failure generated continuous enforcement");

    test::Backend history_backend;
    history_backend.activate(app_a);
    history_backend.state[normalize_path(app_a)] = chinese;
    history_backend.state[normalize_path(app_b)] = english;
    bool writable = false;
    int writes = 0;
    Runtime history_runtime(history_backend, [&](const History&) {
        ++writes;
        return writable;
    });
    history_runtime.configure({true, {}});
    history_runtime.refresh_profiles();
    history_runtime.tick();
    history_backend.activate(app_b);
    history_runtime.tick();
    require(history_runtime.status().key == "settings.input_method.history_failed",
        "new foreground status hid failed history persistence");
    writable = true;
    history_backend.activate(app_a);
    history_runtime.tick();
    require(writes == 2
        && history_runtime.status().key != "settings.input_method.history_failed",
        "failed history was not retried or recovered");
    writable = false;
    history_backend.error = ERROR_SUCCESS;
    history_backend.state[normalize_path(app_a)] = english;
    history_runtime.tick();
    history_backend.active = {};
    history_runtime.tick();
    history_runtime.configure({false, {}});
    require(history_runtime.status().key == "settings.input_method.history_failed"
        && !history_runtime.status().path.empty(), "empty foreground hid history write failure");
    writable = true;
    history_runtime.configure({true, {}});
    require(history_runtime.status().key == "settings.input_method.waiting",
        "recovered history retained a stale error");

    history_backend.activate(app_a);
    History disabled_history;
    Runtime disabling(history_backend, [&](const History& history) {
        disabled_history = history;
        return true;
    });
    disabling.refresh_profiles();
    disabling.configure({true, {}});
    disabling.tick();
    history_backend.state[normalize_path(app_a)] = english;
    disabling.tick();
    disabling.configure({false, {}});
    require(disabled_history.states.at(normalize_path(app_a)) == english,
        "disabling discarded latest observed state");

    test::Backend unavailable;
    unavailable.catalog[1].selectable = false;
    unavailable.activate(app_a);
    Runtime unsupported(unavailable, {});
    unsupported.refresh_profiles();
    unsupported.configure({true, {{app_a, Policy::fixed, chinese.profile_id, Mode::chinese}}});
    unsupported.tick();
    require(unsupported.status().error == ERROR_NOT_SUPPORTED
        && unavailable.profile_requests == 0, "ambiguous profile was guessed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--catalog") {
            auto backend = make_backend();
            for (const auto& profile : backend->enumerate()) {
                std::wcout << profile.id << L" layout="
                    << reinterpret_cast<ULONG_PTR>(profile.layout)
                    << L" selectable=" << profile.selectable
                    << L" chinese=" << profile.supports_chinese
                    << L" name=" << profile.name << L'\n';
            }
            return 0;
        }
        codec_tests();
        runtime_tests();
        failure_tests();
        std::cout << "input method codec, runtime and failure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
