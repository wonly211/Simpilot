# Compile-Time Modules

## Status

Simpilot is a statically linked modular monolith. Features are internal modules,
not runtime-installable plugins. All six feature implementations have moved out
of the application and shared infrastructure.

| Area | Current state |
| --- | --- |
| Lifecycle, revocable contributions, UI dispatcher | Implemented and tested |
| Lossless INI document and module settings transactions | Shared document/session; module-owned codecs, drafts and rollback |
| Everything | Module owns runtime, hotkey, settings page, translations, tests and packaging |
| Mouse locator | Module owns runtime, configuration, settings page, translations and tests |
| Quick launch | Module owns parser, resolver, cache, watcher, menus, editor, icons, theme, pages and file recovery |
| Custom hotkeys | Module owns launch actions, codec, drafts, dialog and contributed editing section |
| Keyboard mapping | Module owns rules, engine, editor, page, configuration, translations and tests |
| Windows hotkey blocking | Module owns user policy, page, configuration and translations; shared override mechanism remains independent |
| Settings host | Registry-driven navigation, child pages, DPI/language forwarding and transactions |
| AppSettings | Shell language, external language code, startup and open-settings hotkey only |

There is no legacy settings adapter or feature-specific Shell message dispatch.
The all-disabled build retains the tray, General settings, shared hotkey page
with the open-settings binding, About and Exit. Tray primary activation falls
back to opening settings when no module contributes a primary action.

## Source Ownership

- `src/app`: process entry, tray/settings hosts, General page and composition root.
- `src/modules/<feature>`: feature runtime, settings, resources, tests and CMake target.
- `src/core` and `include/simpilot`: configuration, logging, localization, file
  utilities and narrow shared contracts.
- `src/infrastructure/keyboard`: one hook thread, capture and optional processing stage.
- `src/infrastructure/ui`: Win32 controls and the shared hotkey settings page.
- `src/infrastructure/command_executor.cpp`: command execution shared by launchers.

Modules do not include one another's implementation headers or application
implementation headers. The composition root is the only Shell file that
includes module factories. `tools/check-module-boundaries.ps1` enforces these
include and compile-condition boundaries.

## Module Contract

`IAppModule` contains only `start()` and `stop() noexcept`. Factories explicitly
receive the registries and services they need. Only
`src/app/register_builtin_modules.cpp` includes feature factory headers.

Each contribution returns a move-only `Registration`. Keep it in the module and
reset it before destroying callback targets. Contributions are UI-thread-only.
Module shutdown is idempotent, startup failures clean up partially started
modules, and shutdown runs in reverse order.

Use `UiDispatcher::post` with a `DispatchScope` for worker notifications. Stop/join
workers before destroying a scope they reference, and cancel the scope before
destroying its callback targets. Periodic callbacks additionally own a registration.
Cancelled callbacks and removed hotkey contributions cannot invoke their previous
owner. The dispatcher must outlive workers using it.

Use `ProgramSearchRegistry` to offer optional `IProgramSearch` implementations.
The launcher resolves system paths and cached programs without a provider.
Search readiness notifications defer menu reloads while a menu is active.

## Settings Ownership

A module owns its typed live settings and edit draft. Register a settings
participant for begin/dirty/validate/prepare/apply/write/rollback/finish/cancel.
Validation and document construction happen before applying side effects.
Preparation retains recovery data until the transaction finishes. Rollback
must handle partially completed preparation/application. A failed rollback
must preserve recovery files.

The session's commit callback must either persist its candidate or restore its
external changes before reporting failure. Participant runtime state is then
rolled back in reverse order. This is handled-error recovery, not multi-file
crash atomicity.

`SettingsDocument` retains unknown lines, comments, sections, duplicate keys and
line endings. Lookup is case-insensitive and uses the final matching key across
sections, matching the legacy reader. Only explicitly owned numbered fields are
removed when lists shrink. Saving while a module is omitted preserves its keys.
Unreadable or invalid UTF-8 documents are not overwritten.

Settings page factories create module-owned child windows. The host forwards
layout, font, DPI, visibility and language changes. Pages do not save themselves.
The default navigation order is General, Quick launch, Menu icons, Global hotkeys,
Keyboard mapping, Everything, Mouse locator and Windows hotkey blocking. Omitted
modules do not contribute their pages.

Hotkey contributions with a draft accessor appear in the common hotkey page;
their settings still belong to their module. Custom hotkeys contribute an editing
section, not the shared page itself. `HotkeyRegistry` allocates runtime IDs, rejects
runtime conflicts and mediates draft conflict replacement. Confirmation completes
before either module's draft changes; cancellation leaves runtime state untouched.
Settings sessions/pages must close before their module objects are destroyed.

Language changes immediately update the live language and persist only its INI
key. The next full commit overlays that language onto the session document,
without committing other drafts during language selection. Sessions retain the
INI snapshot from opening; concurrent external INI edits are not rebased.

Quick launch owns menu/icon snapshots and `SettingsRecovery-*` backups. These
survive a failed rollback, which is reported through the diagnostic sink; the
session reports failure instead of success. Recovery files require inspection
before retrying a session with an unresolved rollback.

Module language fragments are merged with the application catalogs at build
time. Duplicate keys are rejected. Existing string keys and external language
pack behavior are preserved.

## Build and Verification

The six default-on options are:

- `SIMPILOT_MODULE_EVERYTHING`
- `SIMPILOT_MODULE_MOUSE_LOCATOR`
- `SIMPILOT_MODULE_WINDOWS_HOTKEY_BLOCKER`
- `SIMPILOT_MODULE_KEYBOARD_MAPPING`
- `SIMPILOT_MODULE_CUSTOM_HOTKEY`
- `SIMPILOT_MODULE_QUICK_LAUNCH`

Module CMake files own their sources, tests, language fragments and runtime
files. Disabled Everything builds do not require or install its vendor files.
`module-runtime-files.txt` records the active module packaging contributions.

Run the normal configure/build/CTest presets, then:

```powershell
./tools/test-module-builds.ps1 -BaselineBuildDirectory build/vs2022-x64
```

The matrix tests each module switched off, then tests a source
snapshot that physically omits all of `src/modules`. Each variant
is built, tested and installed into a fresh staging directory; installed files
are checked against its module manifest. The working tree is never deleted or
moved. CI invokes this matrix after its full-build tests.

`simpilot_settings_module_tests` uses an independent test module to contribute
a settings page, hotkey, tray command and settings participant. It exercises
navigation, DPI/language propagation, failed-commit rollback, successful apply,
and opening the host with and without a contributed hotkey.

The automated suite covers lifecycle failure cleanup, reverse stop, duplicate
contribution IDs, revocation, cancelled dispatcher callbacks, legacy INI
round-trips, unknown/omitted-module keys, hotkey conflicts and failed menu/icon/INI
transactions. Native UI tests exercise page creation/navigation, apply/cancel,
language propagation and DPI messages in isolated temporary configurations.
They do not replace visual inspection on mixed-DPI monitors or a real-keyboard
regression pass for capture, replay and operating-system shortcut interactions.

### Verified Refactor Baseline

The local MSVC x64 Release verification completed with the following results:

| Configuration | Passed tests | Build/install manifest |
| --- | ---: | --- |
| All modules enabled | 20 | Full ZIP and checksum verified |
| Custom hotkeys disabled | 19 | Passed |
| Everything disabled | 19 | Passed; no Everything runtime files |
| Keyboard mapping disabled | 17 | Passed |
| Mouse locator disabled | 19 | Passed |
| Quick launch disabled | 16 | Passed |
| Windows hotkey blocking disabled | 19 | Passed |
| All module source directories absent | 9 | Passed; executable and license files only |

The complete build used the repository's CI configure preset. Workflow, vendor,
version, Release configuration, artifact whitelist and SHA-256 checks passed.
The executable grew by 3.4586% and the ZIP by 0.4704% against the unchanged
release baseline, within the existing 5% gate. Release builds use cross-library
optimization; CI also accepts CMake's IPO-specific empty `LinkIncremental`
property only when whole-program optimization is enabled.

Native tests used isolated temporary configurations. No manual mixed-monitor
DPI/visual or physical-keyboard acceptance pass was performed.

## Adding Or Removing A Module

1. Create `src/modules/<name>` with an `IAppModule` implementation and a factory.
   Inject only the services and contribution registries it needs.
2. Add typed settings, a `SettingsParticipant` and optional `ISettingsPage`
   factories. Preserve owned INI keys through `SettingsDocument`; do not add
   fields to `AppSettings`.
3. Keep tray, hotkey, search and settings registration handles in the module.
   Implement idempotent stop and revoke them before destroying callback targets.
4. Add the factory call and conditional factory include to
   `src/app/register_builtin_modules.cpp`.
5. Add the default-on CMake option, static target, link and compile definition.
   The module's CMake file owns sources, language fragments, tests and install
   contributions. Add string translation keys to its language fragments.

No changes to `TrayApplication`, `SettingsWindow` or Core are needed.
The fixture in `tests/settings_module_integration_tests.cpp` demonstrates page,
configuration, tray and hotkey contributions without a production feature.
To remove a module, remove its directory, composition-root registration and
build entries. Run the boundary check, tests and matrix afterward.

## Keyboard Ownership

`src/infrastructure/keyboard` owns the single hook thread, capture state,
physical-input contracts and Windows-key state. Its optional
`IKeyboardProcessingStage` executes after capture and before hotkeys and
Windows-key processing. The mapping module compiles a replacement stage
outside the hook callback and installs it using a synchronous control message.
Shared ownership keeps the stage alive until the hook has relinquished it.
No locks, allocations or cross-thread waits were added to the event callback.

User blocking policy and forced-hotkey override masks are independent and are
combined inside the shared Windows-key handler. Removing the policy module
does not remove forced Win+letter behavior. Win+L is excluded from both.
The keyboard manager retains the policy and processing stage for thread recovery.

`src/infrastructure/ui` provides the existing Win32 visual helpers and toggle
controls through a separate static target. Modules do not include application
implementation headers.
