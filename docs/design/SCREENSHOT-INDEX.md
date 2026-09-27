# 原生截图索引

37 个唯一编号；39 个原图文件。N14 已退役，由 N29 替代，保留溯源，不进入最终产品审计。
图像尺寸为采集工具归一化逻辑像素；DPI 是独立原生测量。N01–N08 不据侧栏宽度推断 100%。
时间、SHA、窗口来源详见 [机器索引](evidence/native/index.json)；完整元数据见 [metadata](evidence/metadata/)。
N28 是重建托盘宿主；N32 是系统 MessageBox。模拟数据及执行边界见 [夹具说明](fixture/README)。

| 编号 | 页面 | 语言 | 原生 DPI | 状态与边界 | 原图 / 逻辑尺寸 |
| --- | --- | --- | --- | --- | --- |
| N01 | general | zh-CN | 未记录 | default | [N01.jpg](evidence/native/N01.jpg) (1028×714) |
| N02 | global-hotkeys | zh-CN | 未记录 | two custom rows; mixed enabled | [N02.jpg](evidence/native/N02.jpg) (1028×714) |
| N03 | everything | zh-CN | 未记录 | default; actions not invoked | [N03.jpg](evidence/native/N03.jpg) (1028×714) |
| N04 | mouse-locator | zh-CN | 未记录 | disabled default | [N04.jpg](evidence/native/N04.jpg) (1028×714) |
| N05 | windows-hotkey-blocker | zh-CN | 未记录 | all controls visible; default | [N05.jpg](evidence/native/N05.jpg) (1028×714) |
| N06 | keyboard-mapping | zh-CN | 未记录 | empty list | [N06.jpg](evidence/native/N06.jpg) (1028×714) |
| N07 | menu-icons | zh-CN | 未记录 | empty list; controls disabled | [N07.jpg](evidence/native/N07.jpg) (1028×714) |
| N08 | quick-launch-editor | zh-CN | 未记录 | empty main menu; no selection | [N08.jpg](evidence/native/N08.jpg) (1028×714) |
| N09 | general | zh-CN | 144 | real settings host, full fixture, default | [N09.jpg](evidence/native/N09.jpg) (1028×714) |
| N10 | quick-launch-editor | zh-CN | 144 | populated main menu; selected application | [N10.jpg](evidence/native/N10.jpg) (1028×714) |
| N11 | quick-launch-editor | zh-CN | 144 | selected category | [N11.jpg](evidence/native/N11.jpg) (1028×714) |
| N12 | quick-launch-editor | zh-CN | 144 | selected folder | [N12.jpg](evidence/native/N12.jpg) (1028×714) |
| N13 | quick-launch-editor | zh-CN | 144 | selected separator | [N13.jpg](evidence/native/N13.jpg) (1028×714) |
| N14 (退役) | menu-icons | zh-CN | 144 | 16-row synthetic data; missing menu label localization in fixture, not attributed to production | [N14.jpg](evidence/native/N14.jpg) (1028×714) |
| N15 | global-hotkeys | zh-CN | 144 | 16 synthetic rows; long paths; no selection | [N15.jpg](evidence/native/N15.jpg) (1028×714) |
| N16 | global-hotkeys | zh-CN | 144 | scrolled to last four rows | [N16.jpg](evidence/native/N16.jpg) (1028×714) |
| N17 | keyboard-mapping | zh-CN | 144 | 16 rows; long names; mixed enabled states | [N17.jpg](evidence/native/N17.jpg) (1028×714) |
| N18 | custom-hotkey-add | zh-CN | 144 | default empty; save disabled; recording unavailable | [N18.jpg](evidence/native/N18.jpg) (968×694) |
| N19 | custom-hotkey-edit | zh-CN | 144 | existing Ctrl+Alt+F1; long path; fixture calls existing dialog with data, title remains add | [N19.jpg](evidence/native/N19.jpg) (968×694) |
| N20 | mapping-add | zh-CN | 144 | empty; all source and target controls | [N20.jpg](evidence/native/N20.jpg) (963×651) |
| N21 | mapping-edit | zh-CN | 144 | Left Ctrl+A -> Left Alt+F1; long purpose; audittool.exe | [N21.jpg](evidence/native/N21.jpg) (963×651) |
| N22 | program-selection | zh-CN | 144 | four synthetic programs; first selected | [N22.jpg](evidence/native/N22.jpg) (888×514) |
| N23 | about | zh-CN | 144 | real AboutWindow; simulated version; links blocked | [N23.jpg](evidence/native/N23.jpg) (683×451) |
| N24 | main-menu | zh-CN | 144 | root popup; real owner-draw menu; system theme light | [N24.jpg](evidence/native/N24.jpg) (226×127) |
| N25 | main-menu | zh-CN | 144 | Tools item highlighted; child popup not captured; not submenu coverage | [N25.jpg](evidence/native/N25.jpg) (226×127) |
| N26 | second-menu | zh-CN | 144 | root second menu | [N26.jpg](evidence/native/N26.jpg) (280×83) |
| N27 | second-menu-with-submenu | zh-CN | 144 | complete owner composite plus full parent and child originals | [N27.jpg](evidence/native/N27.jpg) (868×754)<br>[N27-1.jpg](evidence/native/N27-1.jpg) (280×83)<br>[N27-2.jpg](evidence/native/N27-2.jpg) (333×281) |
| N28 | tray-recreated-host | zh-CN | 144 | real contributions; recreated host; NOT production TrayApplication evidence | [N28.jpg](evidence/native/N28.jpg) (199×173) |
| N29 | menu-icons | zh-CN | 144 | valid language resource; supersedes N14 | [N29.jpg](evidence/native/N29.jpg) (1028×714) |
| N30 | general | zh-CN | 144 | pending draft; focused toggle; simulated startup only | [N30.jpg](evidence/native/N30.jpg) (1028×714) |
| N31 | general | zh-CN | 144 | Apply fake commit succeeded; status applied; application disabled | [N31.jpg](evidence/native/N31.jpg) (1028×714) |
| N32 | close-confirmation-system | zh-CN | 144 | native Windows MessageBox; unsaved draft; default No; integration evidence only | [N32.jpg](evidence/native/N32.jpg) (328×167) |
| N33 | general | zh-CN | 144 | Save fake commit failure; draft retained; window remains; failure text | [N33.jpg](evidence/native/N33.jpg) (1028×714) |
| N34 | general | en-US | 144 | English default size; DPI144 | [N34.jpg](evidence/native/N34.jpg) (1028×714) |
| N35 | global-hotkeys | en-US | 144 | English default; 16 rows; DPI144 | [N35.jpg](evidence/native/N35.jpg) (1028×714) |
| N36 | mapping-edit | en-US | 144 | English long purpose; DPI144 | [N36.jpg](evidence/native/N36.jpg) (963×651) |
| N37 | main-menu | en-US | 144 | English UI; user bilingual labels unchanged | [N37.jpg](evidence/native/N37.jpg) (226×127) |

未录制/未捕获的状态见 [审计缺口](AUDIT.md#覆盖与缺口)，不以空白图或效果图填补。
