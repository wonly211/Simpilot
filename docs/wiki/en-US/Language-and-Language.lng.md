# Language and Language.lng

[简体中文](../zh-CN/语言与-Language.lng) | **English**

Simpilot includes Simplified Chinese, Traditional Chinese, and English. Simplified Chinese is used on first launch and whenever the saved language selection is invalid.

The built-in languages are compressed resources inside `Simpilot.exe`. The release folder does not need a `Languages` directory. Deleting, moving, or damaging the optional external language package never prevents Simpilot from starting or affects the three built-in languages.

## Switch a built-in language

1. Right-click the tray icon and select **Settings**.
2. Open **General**.
3. Select Simplified Chinese, Traditional Chinese, or English under **Display Language**.

The interface updates immediately and does not require restarting Simpilot. The choice is saved in `Config/Setting.ini`. User data, including menu titles, category names, and paths, is never translated automatically.

## Add another language

Other languages are provided by the optional file `Language.lng`. Place it beside `Simpilot.exe`:

```text
Simpilot.exe
Language.lng
Everything/
Config/
```

Restart Simpilot. Languages in the package appear under **Settings > General > Display Language**. One `Language.lng` can contain one or more additional languages.

If `Language.lng` is missing, malformed, incompatible, or cannot be decompressed, Simpilot simply ignores it and continues with the built-in resources. An external package can add languages but cannot replace built-in Simplified Chinese, Traditional Chinese, or English.

## Package format

`Language.lng` is a compressed binary language package, not an editable text file. It uses a fixed header and an XPRESS Huffman-compressed payload. This keeps JSON text out of the release folder and reduces package size.

It is not encryption or a security boundary. To change a translation, edit a JSON source file and generate a new package; do not try to edit the `.lng` file directly.

## Missing translations

Interface text resolves in this order:

```text
Selected language -> built-in English -> [missing translation]
```

Therefore, a package with a few missing strings does not leave blank controls, but a production translation should still include every string.

See [Translating and Contributing](Translating-and-Contributing) to create and publish a language package.
