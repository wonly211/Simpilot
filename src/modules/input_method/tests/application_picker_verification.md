# Running Application Picker Verification

Verified on 2026-10-02 with the Visual Studio 2022 x64 Release build.
No personal configuration was read or modified. No version was released.

## Automated Results

- Full application build succeeded; all 25 registered tests passed.
- Input-method-disabled build succeeded; all 22 registered tests passed.
- Module include-boundary checks passed in both configurations.
- The native source menu was exercised with keyboard selection and cancellation.
- An isolated child process provided visible, minimized, hidden and tool windows.
  Visible/minimized captions were merged by EXE path; hidden/tool captions were excluded.
- Injected snapshots covered inaccessible/invalid paths, background filtering,
  same-name/different-path applications, case-insensitive deduplication, title/path
  search, stable selection on refresh, empty results and enumeration errors.
- The real picker tests covered Enter, double-click, Esc, disabled confirmation,
  failure/retry, 120-row scrolling and a keyboard-focusable full-path field.
- Source and rule-editor tests covered both entry points, file-selection forwarding,
  duplicate rejection, cancellation and draft-only changes with the module disabled.
- Cancellation tests covered a running worker, a pending UI callback and a closed
  dispatcher. The worker was joined and callbacks could not revive the dialog.
- Minimum-layout control bounds were checked in three languages at simulated
  96/144/192 DPI, in addition to the existing input-method UI/transaction tests.

## Native Screenshots

Generate isolated screenshots using only mock application data:

```powershell
build/vs2022-x64/src/modules/input_method/Release/simpilot_input_method_native_probe.exe `
    --application-screenshots build/application-picker-preview
```

The fixture produces 18 PNGs: Simplified Chinese, Traditional Chinese and English,
each at default/minimum dimensions and simulated 96/144/192 layout DPI.
Names use `applications-<locale>-layout-<dpi>-<size>.png`.

The display's actual DPI was 144 (150%). Those captures use the actual display
scale; 96 and 192 captures send `WM_DPICHANGED` to exercise layout and font sizing.
They are not evidence of actual 100% or 200% display scaling. Shared native
controls continue to use the display DPI in the simulated cases.

Visual inspection confirmed that the 150% default/minimum layouts fit, the
selected full path is readable, long cells compact without altering data, and
the refresh glyph is not cropped. Representative 100%/200% simulated layouts
were also inspected. Screenshots depict the implemented Win32 dialog, not a mockup.

## Remaining Manual Checks

- Actual 100% and 200% displays and cross-monitor DPI transitions.
- Human-driven mouse scrollbar dragging, row tooltips and clipboard copying.
- DWM-cloaked applications, protected/elevated process access and other user sessions.
- High-contrast display settings and screen-reader announcements.

These checks are not claimed as completed by layout tests or static screenshots.
