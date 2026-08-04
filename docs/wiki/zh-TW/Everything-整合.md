# Everything 整合

[简体中文](../zh-CN/Everything-集成) | **繁體中文** | [English](../en-US/Everything-Integration)

Everything 是简驭 | Simpilot 的選用搜尋能力。它用於開啟或還原 Everything 搜尋視窗，並在快速啟動選單只填寫程式名稱時協助定位檔案。

简驭本身不依賴 Everything 才能啟動。Everything 無法使用時，通知區域選單、已有完整路徑的啟動項目和大多數全域快速鍵仍可正常使用；受影響的是 Everything 搜尋，以及依賴 Everything 尋找的無完整路徑程式。

## 隨附元件

請保持發佈套件中的目錄結構不變：

```text
Simpilot.exe
Everything/
  Everything.exe
  Everything64.dll
  Everything.ini
```

- `Everything64.dll` 是简驭查詢 Everything 資料庫所使用的官方 SDK；它位於 `Everything/`，不會嵌入 `Simpilot.exe`。
- `Everything.exe` 是隨附的預設用戶端與服務修復來源。
- 简驭不會在執行階段下載 Everything，也不會修改你單獨安裝的 Everything 程式檔案。

## 啟動與重複使用規則

啟動简驭時會嘗試連線到預設 Everything 執行個體：

1. 預設執行個體的資料庫已可用時，直接重複使用它；該執行個體不必來自简驭目錄。
2. 資料庫無法使用且名為 `Everything` 的 Windows 服務已安裝但已停止時，嘗試啟動該服務。
3. 資料庫仍無法使用時，嘗試啟動隨附的 `Everything/Everything.exe`。
4. 上述操作失敗或元件缺失時，简驭會繼續啟動，並在需要 Everything 的操作中顯示不可用提示。

「預設執行個體」是 Everything 未指定執行個體名稱時使用的標準執行個體。絕大多數使用者執行的都是預設執行個體。若你只執行了一個具名執行個體，简驭目前可能無法連線到其資料庫，接著會嘗試啟動隨附的預設執行個體。

## 開啟 Everything

可透過下列任一方式開啟或還原搜尋視窗：

- 在通知區域圖示按右鍵，選擇「維護 > 開啟 Everything」；
- 在「設定 > 全域快速鍵」中啟用「Everything 搜尋」內建快速鍵；預設按鍵為 `Win+S`，預設不啟用。

已存在 Everything 搜尋視窗時，简驭會優先還原並前置該視窗。沒有視窗時，简驭優先使用目前執行中預設用戶端的路徑；找不到時使用隨附的 `Everything/Everything.exe`。因此，關閉搜尋視窗後再次觸發仍可重新顯示它。

## Everything 服務

Everything 服務不是執行简驭的前提。它可讓 Everything 更穩定地建立和維護索引，但安裝或修復服務需要系統管理員授權。

1. 在通知區域圖示按右鍵。
2. 選擇「維護 > 安裝/修復 Everything 服務...」。
3. 在 Windows 權限確認視窗中允許操作。

简驭會使用隨附 `Everything.exe` 的服務安裝功能修復預設服務。取消權限確認、服務無法安裝或服務無法啟動，都不會影響简驭的其他功能。

## 用於程式定位

當「開啟應用程式」只填寫例如 `tool.exe` 的檔名時，简驭按以下順序定位：Windows 一般搜尋路徑、`PATH`，然後是可用的 Everything 資料庫。

Everything 傳回多個候選項目時，简驭會顯示選擇視窗。候選項目依序按檔案版本號、修改時間、完整路徑排序，你可自行選擇實際使用的檔案。選擇結果會記錄在 `Cache/program-cache.tsv`；目標移動或刪除後會自動重新定位。

## 已知範圍

- 僅支援預設 Everything 執行個體；不保證可連線到具名執行個體。
- 只要預設執行個體正在執行，简驭查詢時不在意該 `Everything.exe` 的安裝路徑。
- `Everything64.dll` 缺失時，简驭無法查詢 Everything 資料庫；但不影響简驭啟動。
- `Ctrl+Alt+Del`、權限更高的桌面工作階段和其他使用者工作階段不屬於简驭或 Everything 的控制範圍。

遇到無法開啟或無法定位時，請先確認 `Everything/Everything.exe` 與 `Everything/Everything64.dll` 存在，再查看[常見問題與疑難排解](常見問題與疑難排解)。
