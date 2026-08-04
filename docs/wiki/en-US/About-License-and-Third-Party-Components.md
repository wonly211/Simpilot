# About, License, and Third-Party Components

[简体中文](../zh-CN/关于、许可与第三方组件) | [繁體中文](../zh-TW/關於、授權條款與第三方元件) | **English**

Simpilot is a lightweight quick launcher and global hotkey manager for Windows. It runs portably: configuration, caches, and diagnostic logs stay in its program folder, and its core features do not require an account or cloud service.

## Help and feedback

- [Project home](https://github.com/wonly211/Simpilot): source code, documentation, and development updates.
- [Latest release](https://github.com/wonly211/Simpilot/releases/latest): official packages and checksums.
- [Report an issue](https://github.com/wonly211/Simpilot/issues): reproducible defects, feature requests, and translation feedback.

When reporting a problem, include the Simpilot version, Windows version, reproduction steps, expected behavior, and actual behavior. `Log/Simpilot.log` can help diagnose an issue, but remove usernames, personal paths, filenames, and other sensitive details before sharing it.

## Open-source license

Simpilot is released under the [GNU General Public License v3.0](https://github.com/wonly211/Simpilot/blob/main/LICENSE). When distributing a modified version, comply with GPL-3.0 requirements for source availability, license preservation, and the same user freedoms.

This is a summary only. The complete and binding terms are in the `LICENSE` file included in the repository and release package.

## Third-party components

Simpilot uses the following third-party components:

| Component | Purpose |
| --- | --- |
| Everything SDK | Queries the index database of the default Everything instance. |
| nlohmann/json | Parses language resources. |
| Microsoft PowerToys Keyboard Manager | Provides reusable foundations and reference behavior for hotkey recording. |

See the [third-party notices](https://github.com/wonly211/Simpilot/blob/main/THIRD-PARTY-NOTICES.txt) for component versions, copyright notices, reuse boundaries, and full license texts. Everything is optional: when it is unavailable, Simpilot still starts, but Everything Search and pathless-program resolution are unavailable.

## Related pages

- [Wiki Home](Home.en-US)
- [FAQ and Troubleshooting](FAQ-and-Troubleshooting)
- [Translating and Contributing](Translating-and-Contributing)
