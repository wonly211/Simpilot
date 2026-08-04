<p align="center">
  <img src="assets/simpilot-icon.png" width="128" height="128" alt="简驭 | Simpilot 圖示">
</p>

<h1 align="center">简驭 | Simpilot</h1>

<p align="center">
  <a href="README.md">简体中文</a> |
  <strong>繁體中文</strong> |
  <a href="README.en-US.md">English</a>
</p>

<p align="center">
  將常用入口與高頻操作，收進一個通知區域圖示與一組全域快速鍵。
</p>

<p align="center">
  <a href="https://github.com/wonly211/Simpilot/releases/latest"><img src="https://img.shields.io/github/v/release/wonly211/Simpilot?label=Release" alt="最新版本"></a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4" alt="Windows 10 與 11">
  <img src="https://img.shields.io/badge/Architecture-x64-5C6BC0" alt="x64">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/wonly211/Simpilot" alt="授權條款"></a>
</p>

简驭是一款適用於 Windows 的輕量級快速啟動器與全域快速鍵管理工具。無須逐層開啟資料夾，也不必反覆在工作列尋找視窗：透過通知區域選單或熟悉的按鍵組合，即可啟動應用程式、開啟資料夾與檔案、造訪網址，或叫出 Everything 搜尋。

它適合希望減少滑鼠往返、整理分散入口，並讓常用操作隨時觸手可及的辦公使用者、開發者、內容創作者與效率工具愛好者。

## 為什麼選擇简驭

| 功能 | 帶來的效益 |
|---|---|
| 分層快速啟動選單 | 依照自己的工作方式分類整理應用程式、資料夾、檔案與網址 |
| 全域快速鍵 | 從任何一般桌面應用程式直接觸發常用操作 |
| Windows 快速鍵接管 | 在简驭執行期間封鎖選取的 `Win+字母`，並優先執行自訂動作 |
| Everything 整合 | 一鍵開啟或返回搜尋視窗，並協助找出未填寫完整路徑的程式 |
| 選單圖示與佈景主題 | 自動擷取清晰圖示，也可手動指定；支援跟隨系統、淺色與深色主題 |
| 可攜與本地化 | 完整解壓縮後即可執行，設定儲存在本機；內建简体中文、繁體中文與 English |

简驭使用原生 C++20 與 Win32 建置，核心功能不需要帳號或雲端服務，也不會為了封鎖快速鍵而修改 Windows 系統原則。關閉简驭後，由它註冊的快速鍵與即時快速鍵封鎖會自動解除。

## 三個步驟開始使用

1. 前往 [Releases](https://github.com/wonly211/Simpilot/releases/latest) 下載最新的 `Simpilot-*-win-x64.zip`。
2. 將壓縮檔完整解壓縮到目前使用者可寫入的固定目錄，並維持 `Everything/` 目錄結構不變。
3. 執行 `Simpilot.exe`。按一下通知區域圖示或按反引號鍵開啟快速啟動選單；在通知區域圖示按右鍵可進入設定與維護。

简驭是通知區域應用程式，啟動後不會顯示一般主視窗。若沒有看到圖示，請檢查工作列的隱藏圖示區域。

> 目前發佈套件尚未進行數位簽章。若 Windows SmartScreen 首次執行時提示未知發行者，請先確認檔案來自本儲存庫的正式 Release，並使用隨附的 `.sha256` 檔案核對下載內容。

## 常見使用方式

- 使用一個分層選單集中管理工作軟體、專案目錄、常用文件與網站。
- 為會議、螢幕擷取、終端機、編輯器或資料目錄設定順手的全域快速鍵。
- 將 `Win+S`、`Win+G` 等組合交給自己的操作，並在简驭結束後自動恢復系統行為。
- 使用相同快速鍵叫回已在執行的應用程式視窗，避免重複啟動。
- 在主選單之外建立第二選單，將工作與個人入口分開。
- 直接叫出 Everything；當同名程式存在多個版本時，自行選擇真正需要的路徑。

## 功能概覽

- 按一下通知區域圖示只會顯示快速啟動選單；按右鍵只會顯示設定、語言、Everything 維護、關於與結束。
- 以視覺化方式編輯主選單與第二選單，支援分類、分隔線、排序、層級與選單快速鍵。
- 自訂快速鍵可開啟應用程式、資料夾或檔案；應用程式動作支援參數、工作目錄、系統管理員權限、重複啟動策略與視窗狀態。
- 內建快速啟動選單、第二選單、設定視窗與 Everything 搜尋四類功能快速鍵，可分別錄製、清除、啟用或暫停。
- 支援即時封鎖受支援的 `Win+A` 至 `Win+Z`，其中 `Win+L` 因 Windows 安全性限制而無法覆寫。
- 自動監看選單設定變更；讀取失敗時保留上一份有效選單。
- Windows 檔案總管重新啟動後會自動恢復通知區域圖示。
- 自動產生透明的 128x128 圖示快取，也可從 ICO、EXE 或 DLL 手動選擇圖示。
- 使用 Everything 預設執行個體與官方 SDK；Everything 無法使用時不影響简驭本身啟動，並可在選單編輯器中檢視或重新選擇未填寫完整路徑之程式的解析結果。
- 支援 Per-Monitor V2 DPI，選單在不同縮放比例的顯示器上仍能保持清晰。
- 記錄統一儲存在 `Log/Simpilot.log`，啟動時會清理超過 90 天的記錄。

## 使用者手冊

安裝、選單編輯、快速鍵錄製、Windows 快速鍵封鎖、Everything、備份移轉與疑難排解，請閱讀：

**[简驭 | Simpilot 使用者手冊（簡體中文）](docs/user-manual.zh-CN.md)**

遇到問題時，可先查看手冊的疑難排解與常見問題章節，再到 [GitHub Issues](https://github.com/wonly211/Simpilot/issues) 回報。提交問題前請檢查記錄，避免公開個人目錄名稱或檔案名稱。

## 系統需求

- Windows 10 或 Windows 11
- x64 處理器與作業系統
- 一個目前使用者可寫入的程式目錄

简驭採用可攜方式發佈，不提供安裝程式。執行階段設定、快取與記錄均儲存在程式目錄中，方便備份與移轉。

<details>
<summary><strong>從原始碼建置</strong></summary>

需要 Visual Studio 2022 的「使用 C++ 的桌面開發」工作負載，以及 CMake 3.24 或更新版本。

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --build --preset package-release
```

發佈套件產生於：

```text
build/vs2022-x64/Simpilot-<version>-win-x64.zip
```

</details>

<details>
<summary><strong>執行階段目錄</strong></summary>

```text
Config/
  Simpilot.ini
  Simpilot2.ini
  Setting.ini
Cache/
  program-cache.tsv
  RunIcon/
Log/
  Simpilot.log
```

`Simpilot.ini` 與選用的 `Simpilot2.ini` 是快速啟動選單的資料來源；`Setting.ini` 儲存顯示語言與應用程式設定。設定統一以 UTF-8 原子方式儲存，一般使用者無須手動修改。

</details>

<details>
<summary><strong>開發與設計文件（簡體中文）</strong></summary>

- [多語言資源設計](docs/localization.md)
- [快速啟動選單編輯器](docs/menu-editor.md)
- [自訂全域快速鍵](docs/custom-global-hotkeys.md)
- [手動指定選單圖示](docs/menu-custom-icons.md)
- [選單圖示與主題](docs/menu-icons-and-themes.md)
- [Windows 快速鍵檢查與封鎖方案](docs/windows-hotkey-audit.md)
- [PowerToys Keyboard Manager 錄製架構重用說明](docs/powertoys-keyboard-recorder.md)

</details>

## 開放原始碼授權與第三方元件

简驭依據 [GNU General Public License v3.0](LICENSE) 開放原始碼。

發佈套件包含 Everything 執行元件，並重用 Microsoft PowerToys Keyboard Manager 的部分原始碼；完整授權與來源說明請參閱 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)。Everything 元件位於獨立的 `Everything/` 目錄，不會嵌入 `Simpilot.exe`，也不會在執行階段下載。
