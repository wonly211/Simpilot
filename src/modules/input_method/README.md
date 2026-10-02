# Application Input Methods

An optional, statically linked module. `SIMPILOT_MODULE_INPUT_METHOD` defaults to
`ON` at build time; the feature defaults to **disabled** at runtime.

Only `input_method_module.hpp` is a public factory contract. The composition
root registers the module; Shell, SettingsWindow, Core, the keyboard hook and
other feature modules contain no input-method-specific branches.

## Behavior

- Rules identify applications by an absolute, lexically normalized,
  case-insensitive executable path. The editor retains the originally selected
  spelling. Relative paths are rejected.
- `fixed` requests the chosen input method and Chinese/English mode on entry.
  It takes precedence over remembered state.
- `remember`, including applications without explicit rules, restores a known
  state or leaves the current state alone on the first visit.
- `ignore` neither samples nor controls the application. Existing remembered
  data is retained, but not restored while the ignore rule exists.
- Manual input-method changes remain possible. Another window of the same
  executable does not reapply a rule.
- The UI dispatcher samples every 200 ms. It caches readings made while the
  application was foreground, rather than querying an inactive application's
  IME after a switch. Very short visits or changes between samples can be missed.
- Switching uses one profile request and one mode request at most per entry,
  with bounded readback over six subsequent samples. Rejection, unavailable
  profiles and permission failures generate an inline status and diagnostic,
  not popups or continuous enforcement.
- Simpilot's own windows are not managed. Shutdown cancels the timer and
  dispatch scope before flushing observed history and removing contributions.

## Backend Boundaries

Installed profiles are enumerated from TSF and loaded keyboard layouts.
Keyboard identities use KLID-style identifiers. TSF identities include the
language ID, CLSID and profile GUID; the corresponding HKL is resolved from the
current system rather than persisting a process-local handle.

The controller sends `WM_INPUTLANGCHANGEREQUEST` to the focused target window.
IMM mode reads/writes use the target's default IME window with a 30 ms
`SendMessageTimeoutW` limit. Foreground, PID and focus-thread checks reject
stale targets. For Microsoft Pinyin, English closes the input context; Chinese
opens it and sets the native-conversion flag while preserving other flags.
Success is reported only after readback matches the requested state.

This does not inject code, install a service, activate profiles across the
whole session, add hotkeys, or create another keyboard-hook thread.

Some TSF profiles cannot be distinguished or selected through their target
HKL. Missing substitute layouts are inferred only when the enabled profile
and loaded layout are unique for the language. Ambiguous or unsupported
profiles are retained and reported, never guessed. Microsoft Pinyin is the
first supported validation target; arbitrary third-party IMEs are not promised.
Higher-integrity, TSF-only or noncooperating applications may reject control.

## Configuration

Explicit rules belong to the standard settings transaction in `Config/Setting.ini`:

```ini
[InputMethod]
InputMethodEnabled=0
InputMethodRuleCount=1
InputMethodRule1Path=C:\Apps\Example.exe
InputMethodRule1Policy=fixed
InputMethodRule1Profile=layout:00000409
InputMethodRule1Mode=english
```

Policies are `remember`, `fixed`, `ignore`; modes are `chinese`, `english`.
Fixed rules require a profile ID. Installed English-only layouts cannot
offer Chinese mode. Uninstalled profiles remain editable and persistable.
The editor rejects duplicate normalized paths.

## Selecting an application

The add button and the rule editor's path button both offer an EXE file picker
or a running-application picker. Selecting an application only fills the rule
editor; accepting that editor changes the draft, and applying settings changes
the runtime. Cancelling either dialog does not change settings.

The running picker takes an asynchronous, current-session snapshot on opening
and explicit refresh. Visible top-level windows, including minimized windows,
appear by default. Hidden, tool and DWM-cloaked windows are excluded; the
background-process checkbox includes other processes with readable EXE paths.
The current Simpilot process is excluded. No elevation, command-line access,
process-memory access or permanent process monitor is used.

Processes are grouped by the existing normalized full path. The original path
is displayed, and all window captions are searchable without persisting them.
Search and background filtering use the snapshot without reenumerating.
An application that exits after enumeration can still be configured by its
resolved path. Inaccessible or exited processes are skipped during enumeration;
snapshot failures appear inline and can be retried.

The picker owns a cancellable worker and a dispatch scope. Closing cancels
pending callbacks and joins the worker before releasing window state. Tests
inject snapshots, source selection and file selection through module-private
services; no shared host APIs or configuration schema are extended.

The settings participant stages drafts and prepares the timer before the
shared INI commit. Runtime settings change only after that commit succeeds.
Failed commits roll back monitoring; cancel discards only the draft.
Unreadable or invalid module configuration is not overwritten by unrelated
settings saves.

Automatic history is independent of the settings draft:

```ini
[InputMethodHistory]
InputMethodHistoryCount=1
InputMethodHistory1Path=c:\apps\example.exe
InputMethodHistory1Profile=layout:00000409
InputMethodHistory1Mode=english
```

`Config/InputMethodHistory.ini` is updated with `SettingsDocument` and atomic
replacement. Unknown keys, comments and unrecognized lines survive updates;
only owned numbered fields are pruned. Read errors preserve the original file.
Write failures retain pending history in memory, keep a nonblocking error
visible and retry on subsequent application switches or shutdown.
History never marks the settings session dirty. Rules and history are each
limited to 4,096 applications.

## Settings Page

Page order is 45, after global hotkeys and before keyboard mappings.
The module owns the page, rule dialog, EXE picker and language fragments.
The page reuses the shared typography, toggle, toolbar and list styling.
Five columns fit the standard 650 DIP content width; long paths reuse shared
path compaction, retaining the filename without changing stored values.
Selection exposes a read-only full
path, and the shared list tooltip exposes full row text to mouse/keyboard users.
Non-fixed strategies hide and disable the profile and mode controls.

## Verification

Running-application picker results and remaining manual checks are recorded in
[application_picker_verification.md](tests/application_picker_verification.md).

Release builds use the existing Visual Studio 2022 x64 toolchain:

```powershell
cmake --build build/input-method-vs2022-x64 --config Release --parallel 4
ctest --test-dir build/input-method-vs2022-x64 -C Release --output-on-failure
pwsh -NoProfile -File tools/check-module-boundaries.ps1
```

The new codec/runtime suite covers path identity, rule precedence, manual
changes, one-time application, failure/readback bounds, shutdown persistence,
unknown-content preservation and failed atomic replacement.
The Win32 suite covers policies, cancel, empty/missing profiles, duplicate
paths, non-mutating edits, populated cells across relayout, list scrolling,
three languages, simulated layout scales, transaction rollback, history
independence, contribution cleanup and canceled callbacks.

The native probe must run **after all UI tests have exited**; another UI
process can steal foreground and invalidate its target:

```powershell
./build/input-method-vs2022-x64/src/modules/input_method/Release/simpilot_input_method_native_probe.exe
./build/input-method-vs2022-x64/src/modules/input_method/Release/simpilot_input_method_native_probe.exe --screenshots E:\SynologyDrive\github\Simpilot\build\input-method-native-screenshots-final
```

The probe creates its own cross-process edit window, checks Chinese IME
English/Chinese/English readback, restores its initial state and closes the
target. It does not use personal configuration or launch external applications.
Screenshot mode uses fictional application paths and profiles.

Completed implementation build variants: full build (23 tests), input-method OFF (21 tests),
and all seven modules OFF with every module source directory physically
omitted in an isolated snapshot (10 tests and install manifest check).
The real monitor DPI is 144. The dialog screenshots use actual 144 DPI;
96/144/192 page renders pass explicit layout/font scales to the real page
implementation and are not a three-monitor DPI acceptance test.

Remaining manual acceptance: Notepad/browser/editor switching, real user
manual changes between application visits, EXE picker operation, input-method
uninstallation, elevated or rejecting windows, Windows 10, and actual
100%/200% monitor-DPI changes. Do not treat simulated backends or layout tests
as proof of those workflows.

The module ships in v1.0.6, disabled by default at runtime. Release artifact
sizes are compared with the retained v1.0.0 baseline for reporting only; growth
does not require approval or block publication. Integrity, version, packaging,
module-boundary and test checks remain mandatory.
