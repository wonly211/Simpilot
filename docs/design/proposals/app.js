(() => {
  "use strict";
  const params = new URLSearchParams(location.search);
  const en = params.get("lang") === "en";
  const t = (zh, english) => en ? english : zh;
  const requested = params.get("view") || "general";
  const view = ["general", "hotkeys", "mapping", "menu"].includes(requested) ? requested : "general";
  document.documentElement.lang = en ? "en" : "zh-CN";
  document.title = `Simpilot - ${view} - ${t("设计提案", "Design proposal")}`;
  const esc = value => String(value).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  const icon = name => {
    const nodes = (window.PROPOSAL_ICONS || {})[name] || [];
    return `<svg class="icon" aria-hidden="true" viewBox="0 0 24 24">${nodes.map(([tag, attrs]) => `<${tag} ${Object.entries(attrs).map(([k, v]) => `${k}="${esc(v)}"`).join(" ")}></${tag}>`).join("")}</svg>`;
  };
  const logo = "../../../assets/simpilot-icon.png";
  const tool = (name, label, extra = "") => `<button class="icon-button quiet" title="${esc(label)}" aria-label="${esc(label)}" ${extra}>${icon(name)}</button>`;
  const toggle = (label, on) => `<button class="toggle" role="switch" aria-label="${esc(label)}" aria-checked="${on}" title="${esc(label)}"><span class="toggle-track"></span></button>`;
  const titlebar = (name, modal = false) => `<header class="titlebar"><div class="window-name"><img src="${logo}" alt=""><span>${t("简驭 | ", "")}Simpilot - ${name}</span></div><div class="window-actions">${modal ? "" : tool("Minus", t("最小化", "Minimize")) + tool("Square", t("最大化", "Maximize"))}${tool("X", t("关闭", "Close"))}</div></header>`;
  const footer = modal => `<footer class="footer"><button class="quiet">${t("取消", "Cancel")}</button>${modal ? "" : `<button disabled>${t("应用", "Apply")}</button>`}<button class="primary">${t("保存", "Save")}</button></footer>`;
  const navItems = [
    ["general", "常规", "General"],
    ["launch", "快捷启动菜单", "Quick launch menu"],
    ["icons", "菜单图标", "Menu icons"],
    ["hotkeys", "全局热键", "Global hotkeys"],
    ["mapping", "键盘映射", "Keyboard mapping"],
    ["everything", "Everything 搜索", "Everything search"],
    ["cursor", "光标定位", "Cursor locator"],
    ["windows", "Windows 快捷键屏蔽", "Windows hotkey blocking"]
  ];
  const sidebar = () => `<aside class="sidebar"><div class="brand"><img src="${logo}" alt=""><div><strong>Simpilot</strong><span class="aux">${t("设置", "Settings")}</span></div></div><nav class="nav-list" aria-label="${t("设置", "Settings")}">${navItems.map(([key, zh, english]) => `<button class="nav-item ${view === key ? "active" : ""}" ${view === key ? 'aria-current="page"' : ""} data-view="${key}">${t(zh, english)}</button>`).join("")}</nav></aside>`;
  const startupLabel = t("登录 Windows 后自动启动 Simpilot", "Automatically start Simpilot after signing in to Windows");
  const general = () => `<main class="page"><h1>${t("常规", "General")}</h1><section class="simple"><div class="setting-row"><span class="startup-label">${startupLabel}</span>${toggle(startupLabel, false)}</div><div class="setting-row two-line"><div><label for="language">${t("显示语言", "Display language")}</label><div class="aux">${t("立即保存", "Saved immediately")}</div></div><select id="language"><option value="zh" ${en ? "" : "selected"}>简体中文</option><option value="en" ${en ? "selected" : ""}>English</option></select></div></section></main>`;

  // Fixture paths are transcribed from N15, not resolved or executed.
  const root = "C:\\Users\\WoNly\\AppData\\Local\\Temp\\Simpilot-native-audit-{9D8AF4D3-8530-4855-8C00-9A2DFB87A6A8}\\Variant-1-rows-16\\Mock files\\Long project name for native column overflow review";
  const rows = Array.from({ length: 16 }, (_, i) => ({
    key: `Ctrl+Alt+${i >= 12 ? "Shift+" : ""}F${i % 12 + 1}`,
    action: [t("打开应用", "Open app"), t("打开文件夹", "Open folder"), t("打开文件", "Open file")][i % 3],
    path: i % 3 === 0 ? `${root}\\AuditTool.exe` : i % 3 === 1 ? root : `${root}\\Sample document ${i + 1} - long descriptive target for truncation review.txt`,
    enabled: i % 4 !== 3
  }));
  window.PROPOSAL_FIXTURE = { view, language: en ? "en" : "zh", rows };
  const builtins = [
    [t("快捷启动菜单", "Quick launch menu"), "Alt+Space", true],
    [t("第二菜单", "Second menu"), "Ctrl+Space", true],
    [t("设置窗口", "Settings window"), "Ctrl+Alt+S", true],
    [t("Everything 搜索", "Everything search"), "Win+S", false]
  ];
  const hotkeys = () => `<main class="page hotkeys-page"><h1>${t("全局热键", "Global hotkeys")}</h1><section class="builtin"><div class="builtin-header"><h2>${t("内置热键", "Built-in hotkeys")}</h2><span class="key-heading">${t("热键", "Hotkey")}</span><span class="enabled-heading">${t("启用", "On")}</span></div>${builtins.map(([label, key, enabled]) => `<div class="builtin-row"><span class="builtin-label">${label}</span><button class="record-key" title="${t("录制", "Record")} ${label}" aria-label="${t("录制", "Record")} ${label}: ${key}"><span>${key}</span>${icon("Keyboard")}</button>${tool("X", `${t("清除热键", "Clear hotkey")}: ${label}`, 'data-clear class-unused=""').replace('class="icon-button quiet"', 'class="icon-button quiet clear"')}${toggle(`${t("启用", "Enable")}: ${label}`, enabled)}</div>`).join("")}</section><section class="custom"><div class="toolbar"><h2>${t("自定义热键", "Custom hotkeys")}</h2><div class="toolbar-actions"><button>${icon("Plus")}${t("添加", "Add")}</button><button data-selection-action disabled>${icon("Pencil")}${t("编辑", "Edit")}</button><button data-selection-action disabled>${icon("Trash2")}${t("删除", "Delete")}</button></div></div><div class="table" role="table" aria-label="${t("自定义热键", "Custom hotkeys")}" aria-rowcount="17"><div class="table-head" role="row"><span role="columnheader">${t("启用", "On")}</span><span role="columnheader">${t("热键", "Hotkey")}</span><span role="columnheader">${t("操作类型", "Action")}</span><span role="columnheader">${t("目标", "Target")}</span></div><div class="table-body">${rows.map((row, i) => `<div class="data-row" role="row" data-index="${i}">${toggle(`${t("启用", "Enable")} ${row.key}`, row.enabled)}<span class="cell hotkey-value" role="cell">${row.key}</span><span class="cell" role="cell">${row.action}</span><button class="path" data-path="${esc(row.path)}" title="${esc(row.path)}" aria-label="${esc(row.path)}"></button></div>`).join("")}</div></div></section></main>`;
  const select = (id, label, value, choices, extra = "") => `<select id="${id}" aria-label="${esc(label)}" ${extra}>${choices.map(choice => `<option ${value === choice ? "selected" : ""}>${esc(choice)}</option>`).join("")}</select>`;
  const none = t("无", "None");
  const modifiers = [none, t("左 Ctrl", "Left Ctrl"), t("右 Ctrl", "Right Ctrl"), t("左 Alt", "Left Alt"), t("右 Alt", "Right Alt"), t("左 Shift", "Left Shift"), t("右 Shift", "Right Shift"), t("左 Win", "Left Win"), t("右 Win", "Right Win")];
  const keys = [none, "A", "B", "C", "F1", "F2", "Space", "Enter", "Esc"];
  const mappingHalf = source => {
    const name = source ? t("源触发", "Source trigger") : t("目标输出", "Target output");
    const id = source ? "source" : "target";
    const mod = source ? modifiers[1] : modifiers[3];
    const mainKey = source ? "A" : "F1";
    return `<section class="mapping-half" data-side="${id}"><h2>${name}</h2><div class="record-line"><input aria-label="${name}" value="${mod} + ${mainKey}" readonly><button>${icon("Keyboard")}${source ? t("录制源按键", "Record source") : t("录制目标按键", "Record target")}</button></div><div class="field-label" id="${id}-modifiers">${t("修饰键（最多 4 个）", "Modifiers (up to 4)")}</div><div class="modifier-grid">${Array.from({ length: 4 }, (_, i) => select(`${id}-modifier-${i}`, `${name} ${t("修饰键", "modifier")} ${i + 1}`, i === 0 ? mod : none, modifiers, 'data-modifier')).join("")}</div><div class="key-fields ${source ? "" : "target"}"><div><label class="field-label" for="${id}-key">${t("主键", "Main key")}</label>${select(`${id}-key`, `${name} ${t("主键", "main key")}`, mainKey, keys)}</div>${source ? `<div><label class="field-label" for="source-chord">${t("同时按下键（可选）", "Simultaneous key (optional)")}</label>${select("source-chord", t("同时按下键（可选）", "Simultaneous key (optional)"), none, keys)}</div>` : ""}</div></section>`;
  };
  const purpose = "Long mapping purpose: review selection across multiple workspace panels / 选择切换";
  const mapping = () => `<div class="window mapping-window">${titlebar(t("键盘映射", "Keyboard mapping"), true)}<main class="mapping-body"><h1>${t("编辑键盘映射", "Edit keyboard mapping")}</h1><div class="mapping-columns">${mappingHalf(true)}${mappingHalf(false)}</div><section class="mapping-details"><div class="detail-row"><label for="purpose">${t("用途", "Purpose")}</label><input id="purpose" spellcheck="false" value="${esc(purpose)}"></div><div class="application-options"><div class="application-scope" role="group" aria-labelledby="application-label"><label id="application-label" for="application">${t("适用应用", "Applies to")}</label><div class="app-field"><input id="application" value="audittool.exe" spellcheck="false"><button>${t("使用前台应用", "Use foreground app")}</button></div><label class="checkbox exact-match"><input id="exact-match" type="checkbox">${t("精确匹配应用", "Exact application match")}</label></div><label class="checkbox rule-enabled"><input id="mapping-enabled" type="checkbox" checked>${t("启用此规则", "Enable this rule")}</label></div></section></main>${footer(true)}</div>`;

  const menuRow = (label, image, selected = false, submenu = false, access = false, extra = "") => `<button role="menuitem" class="menu-row ${selected ? "selected" : ""} ${access ? "with-access" : ""}" title="${esc(label)}" aria-label="${esc(label)}" ${submenu ? 'aria-haspopup="menu"' : ""} ${extra}>${image === "logo" ? `<img src="${logo}" alt="">` : icon(image)}<span class="menu-label">${label}</span>${access ? '<span class="menu-access">[T]</span>' : ""}${submenu ? icon("ChevronRight").replace('class="icon"', 'class="icon arrow"') : "<span></span>"}</button>`;
  window.PROPOSAL_MENU = [
    { label: "Audit Tool / 模拟应用", target: `${root}\\AuditTool.exe` },
    { separator: true },
    { label: "Tools / 工具", accessKey: "T", children: [
      { label: "Fixture folder / 测试文件夹", target: root },
      { label: "Review utilities / 审核工具", children: Array.from({ length: 8 }, (_, i) => ({
        label: `Document ${(i + 1) * 2} / 文档 - detailed review`,
        target: `${root}\\Sample document ${(i + 1) * 2} - long descriptive target for truncation review.txt`
      })) }
    ] },
    { separator: true, rendererAppended: true },
    { label: t("菜单 2", "Menu 2"), rendererAppended: true, children: [
      { label: "Second menu tool / 第二菜单", target: `${root}\\AuditTool.exe` }
    ] }
  ];
  const menu = () => `<main class="menu-workspace"><div class="popup main-menu" role="menu" aria-label="${t("主菜单", "Main menu")}">${menuRow("Audit Tool / 模拟应用", "logo")}<div class="menu-separator" role="separator"></div>${menuRow("Tools / 工具", "Folder", true, true, true, 'aria-expanded="true"')}<div class="menu-separator" role="separator" data-renderer-appended></div>${menuRow(t("菜单 2", "Menu 2"), "Folder", false, true, false, 'aria-expanded="false" data-second-menu')}</div><div class="popup submenu" role="menu" aria-label="Tools / 工具">${menuRow("Fixture folder / 测试文件夹", "Folder")}${menuRow("Review utilities / 审核工具", "Folder", false, true, false, 'aria-expanded="false" data-review-utilities')}</div></main>`;
  document.getElementById("app").innerHTML = view === "mapping" ? mapping() : view === "menu" ? menu() : `<div class="window">${titlebar(t("设置", "Settings"))}<div class="settings-body">${sidebar()}${view === "hotkeys" ? hotkeys() : general()}</div>${footer(false)}</div>`;

  // All actions stay inside the disposable HTML draft. No persistence or native calls.
  document.querySelectorAll(".toggle").forEach(button => button.addEventListener("click", () => button.setAttribute("aria-checked", String(button.getAttribute("aria-checked") !== "true"))));
  document.querySelectorAll("[data-view]").forEach(button => button.addEventListener("click", () => {
    if (!["general", "hotkeys", "mapping"].includes(button.dataset.view)) return;
    location.search = `?view=${button.dataset.view}${en ? "&lang=en" : ""}`;
  }));
  document.getElementById("language")?.addEventListener("change", event => { location.search = `?view=general${event.target.value === "en" ? "&lang=en" : ""}`; });
  document.querySelectorAll("[data-clear]").forEach(button => button.addEventListener("click", () => {
    button.closest(".builtin-row").querySelector(".record-key span").textContent = none;
  }));
  document.querySelectorAll(".data-row").forEach(row => row.addEventListener("click", () => {
    document.querySelectorAll(".data-row").forEach(other => other.classList.toggle("selected", other === row));
    document.querySelectorAll("[data-selection-action]").forEach(button => { button.disabled = false; });
  }));
  const measure = document.createElement("canvas").getContext("2d");
  function fitPaths() {
    document.querySelectorAll("[data-path]").forEach(button => {
      measure.font = getComputedStyle(button).font;
      const full = button.dataset.path;
      const available = button.clientWidth - 24;
      if (measure.measureText(full).width <= available) { button.textContent = full; return; }
      const leaf = full.slice(full.lastIndexOf("\\") + 1);
      let candidate = `C:\\…\\${leaf}`;
      if (measure.measureText(candidate).width <= available) { button.textContent = candidate; return; }
      const suffix = leaf.includes(".") ? leaf.slice(leaf.lastIndexOf(".")) : "";
      let prefix = leaf.slice(0, leaf.length - suffix.length);
      while (prefix.length && measure.measureText(`…\\${prefix}…${suffix}`).width > available) prefix = prefix.slice(0, -1);
      button.textContent = `…\\${prefix}…${suffix}`;
    });
  }
  function hidePath() { document.querySelector(".path-tooltip")?.remove(); }
  function showPath(button) {
    hidePath();
    const tip = document.createElement("div");
    tip.className = "path-tooltip";
    tip.id = "full-path";
    tip.role = "tooltip";
    tip.textContent = button.dataset.path;
    document.body.append(tip);
    button.setAttribute("aria-describedby", tip.id);
    const rect = button.getBoundingClientRect();
    tip.style.left = `${Math.max(8, Math.min(rect.left, innerWidth - tip.offsetWidth - 8))}px`;
    tip.style.top = `${Math.max(8, rect.top - tip.offsetHeight - 8)}px`;
  }
  document.querySelectorAll("[data-path]").forEach(button => {
    button.addEventListener("mouseenter", () => showPath(button));
    button.addEventListener("focus", () => showPath(button));
    button.addEventListener("mouseleave", hidePath);
    button.addEventListener("blur", hidePath);
  });
  document.addEventListener("keydown", event => { if (event.key === "Escape") hidePath(); });
  document.querySelector(".table-body")?.addEventListener("scroll", hidePath);
  document.fonts.ready.then(() => { fitPaths(); window.PROPOSAL_READY = true; });
  window.addEventListener("resize", fitPaths);
})();
