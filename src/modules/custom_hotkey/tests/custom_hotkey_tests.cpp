#include "custom_hotkey_module.hpp"
#include "custom_hotkey_settings.hpp"
#include "hotkey_settings_page.hpp"
#include "keyboard_manager.hpp"
#include "toggle_switch.hpp"
#include <commctrl.h>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void codec_tests() {
    simpilot::CustomHotkeySettings settings;
#include "legacy_fixture.inc"
    auto document = simpilot::SettingsDocument::parse(
        "; retain\r\n[Other]\r\nUnknown=42\r\nCustomGlobalHotKey12Future=keep\r\n");
    settings.write(document);
    const auto content = document.serialize();
    require(content.find("CustomGlobalHotKey1Action=0") != std::string::npos
        && content.find("CustomGlobalHotKey2Action=2") != std::string::npos
        && content.find("CustomGlobalHotKey3Action=1") != std::string::npos, "Legacy action values");
    auto expected = settings;
    expected.items.pop_back();
    expected.items[0].binding.force_override = true;
    require(simpilot::CustomHotkeySettings::read(document) == expected,
        "Unicode actions, disabled state, process behavior and Win+letter compatibility");
    settings.items.resize(1);
    settings.write(document);
    require(!document.get(L"CustomGlobalHotKey2Program"), "Shortened collection removes owned old keys");
    require(document.get(L"CustomGlobalHotKey12Future") == L"keep" && document.get(L"Unknown") == L"42",
        "Do not remove unowned numbered fields or unrelated settings");
    document.set(L"General", L"Language", L"en-US");
    require(simpilot::CustomHotkeySettings::read(document).items.size() == 1,
        "Other modules can save while custom hotkeys are absent");
    const auto malformed = simpilot::SettingsDocument::parse(
        "[WrongSection]\ncustomglobalhotkeycount=1\nCUSTOMGLOBALHOTKEY1CODE=3,88\n"
        "CustomGlobalHotKey1Program=C:\\Apps\\Incomplete.exe\n");
    require(simpilot::CustomHotkeySettings::read(malformed).items.empty(), "Require explicit Enabled");
    auto legacy = malformed;
    legacy.set(L"Ignored", L"CustomGlobalHotKey1Enabled", L"0");
    require(simpilot::CustomHotkeySettings::read(legacy).items.size() == 1,
        "Sectionless case-insensitive legacy lookup");
}
void switch_image_tests() {
    for (const UINT dpi : {96U, 144U, 192U}) {
        const auto images = simpilot::toggle_switch::create_state_image_list(dpi);
        require(images && ImageList_GetImageCount(images) == 2, "Both switch states exist");
        const auto screen = GetDC(nullptr);
        const auto dc = CreateCompatibleDC(screen);
        const int width = MulDiv(64, dpi, 96), height = MulDiv(36, dpi, 96);
        const auto bitmap = CreateCompatibleBitmap(screen, width, height);
        const auto previous = SelectObject(dc, bitmap);
        const RECT bounds{0, 0, width, height};
        for (int state = 0; state < 2; ++state) {
            FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            require(ImageList_Draw(images, state, dc, 0, 0, ILD_TRANSPARENT),
                "Draw switch image");
            require(GetPixel(dc, 0, 0) == RGB(255, 255, 255),
                "Switch image corners must remain transparent");
            require(GetPixel(dc, width / 2, height / 2) != RGB(255, 0, 255),
                "Switch glyph must not contain the transparency key");
            if (state == 1) require(GetPixel(dc, width / 2, height / 2) == RGB(0, 103, 184),
                "Enabled switch renders its blue track, not a black rectangle");
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        ImageList_Destroy(images);
    }
}
void module_tests() {
    const auto document = simpilot::SettingsDocument::parse(
        "[CustomGlobalHotkeys]\nCustomGlobalHotKeyCount=1\nCustomGlobalHotKey1Code=3,88\n"
        "CustomGlobalHotKey1Program=C:\\Windows\\notepad.exe\nCustomGlobalHotKey1Enabled=1\n");
    simpilot::SettingsParticipantRegistry participants;
    simpilot::HotkeyRegistry hotkeys;
    simpilot::KeyboardManager keyboard;
    auto module = simpilot::make_custom_hotkey_module(document, L".", participants,
        hotkeys, keyboard, {}, {});
    module->start();
    module->start();
    require(hotkeys.sections.size() == 1 && participants.size() == 1, "Idempotent start");
    simpilot::SettingsSession session(participants, document);
    auto drafts = hotkeys.editable_drafts();
    require(drafts.size() == 1 && !drafts[0].common_row, "Custom section owns its variable-length rows");
    auto next = drafts[0].read();
    next.binding.gesture = simpilot::HotKeyGesture{MOD_CONTROL, VK_F24};
    drafts[0].write(next);
    require(session.dirty(), "Draft edit changes participant dirty state");
    const auto runtime_key = [&] {
        UINT key = 0;
        hotkeys.refresh([&](int, const auto& binding) { key = binding.gesture->virtual_key; return true; });
        return key;
    };
    require(runtime_key() == L'X', "Draft edit does not mutate runtime");
    require(!session.apply([](const auto&) { return false; }), "Injected INI failure");
    require(runtime_key() == L'X' && session.dirty(), "Rollback restores runtime and retains draft");
    require(session.apply([](const auto& candidate) {
        return candidate.get(L"CustomGlobalHotKey1Code") == L"2,135";
    }), "Successful transaction writes legacy key");
    require(runtime_key() == VK_F24 && !session.dirty(), "Apply publishes new runtime");

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const auto instance = GetModuleHandleW(nullptr);
    const auto parent = CreateWindowW(L"STATIC", L"test", WS_OVERLAPPEDWINDOW,
        0, 0, 900, 700, nullptr, nullptr, instance, nullptr);
    require(parent != nullptr, "Create isolated settings host");
    const simpilot::Localization english("en-US");
    auto page = simpilot::make_hotkey_settings_page(hotkeys, keyboard, {}, {});
    int changes = 0;
    page->create({instance, parent, 96, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
        english, [&] { ++changes; }});
    page->layout({0, 0, 700, 500}, 144, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    page->show(true);
    const auto section = FindWindowExW(
        FindWindowExW(parent, nullptr, L"Simpilot.HotkeySettingsPage", nullptr),
        nullptr, L"Simpilot.CustomHotkeySection", nullptr);
    require(section != nullptr, "Common page hosts module section");
    for (const UINT dpi : {96U, 144U, 192U}) {
        page->layout({0, 0, MulDiv(760, dpi, 96), MulDiv(600, dpi, 96)}, dpi,
            static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        const auto list = GetDlgItem(section, 4);
        for (int column = 0; column < 4; ++column) {
            HDITEMW header{.mask = HDI_FORMAT};
            require(Header_GetItem(ListView_GetHeader(list), column, &header)
                && (header.fmt & HDF_STRING), "Every custom hotkey header draws its title");
        }
        require(ListView_GetColumnWidth(list, 0) == MulDiv(64, dpi, 96),
            "Custom enable column matches the shared sliding switch width");
        RECT client{}, button{}, heading{};
        GetClientRect(section, &client);
        GetWindowRect(GetDlgItem(section, 1), &button);
        GetWindowRect(FindWindowExW(section, nullptr, L"STATIC", nullptr), &heading);
        require(button.bottom - button.top == MulDiv(32, dpi, 96)
            && heading.right < button.left && heading.top < button.bottom,
            "Section heading and compact toolbar share one row without overlap");
        LOGFONTW label_font{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(
            FindWindowExW(section, nullptr, L"STATIC", nullptr), WM_GETFONT, 0, 0)),
            sizeof(label_font), &label_font);
        require(label_font.lfHeight == -MulDiv(14, dpi, 96) && label_font.lfWeight == FW_SEMIBOLD,
            "Custom section uses a section heading, not a duplicate page title");
    }
    for (const auto locale : {"en-US", "zh-CN", "zh-TW"}) {
        const simpilot::Localization localization(locale);
        require(localization.text("custom_hotkey.invalid_target") != L"[missing translation]",
            "Module language resources are linked");
        page->refresh_language(localization);
    }
    page->refresh_language(english);
    page.reset();
    DestroyWindow(parent);
    session.cancel();
    module->stop();
    module->stop();
    require(!drafts[0].active() && hotkeys.sections.size() == 0 && participants.size() == 0,
        "Stop revokes draft and section callbacks");
    require(runtime_key() == 0, "Stop revokes all runtime bindings");
}
}
int main() {
    try {
        codec_tests();
        switch_image_tests();
        module_tests();
        std::cout << "Custom hotkey module tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
