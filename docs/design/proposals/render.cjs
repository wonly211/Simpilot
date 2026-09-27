"use strict";
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const crypto = require("node:crypto");
const defaultModule = "C:/Users/WoNly/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.js";
let chromium;
try {
  ({ chromium } = require(process.env.SIMPILOT_PLAYWRIGHT || defaultModule));
} catch (error) {
  console.error(`Playwright unavailable. Open gallery.html manually or set SIMPILOT_PLAYWRIGHT.\n${error.message}`);
  process.exit(1);
}
const output = path.join(__dirname, "renders");
const n15 = fs.readFileSync(path.join(__dirname, "../evidence/native/N15.txt"), "utf8");
fs.mkdirSync(output, { recursive: true });
const sizes = { general: [1028, 714], hotkeys: [1028, 714], mapping: [963, 651], menu: [868, 752] };
const cases = [];
// Deliver the four review images first, then run the complete browser matrix.
for (const [view, [width, height]] of Object.entries(sizes)) cases.push({ view, width, height, lang: "zh", dpr: 1, name: view });
for (const [view, [width, height]] of Object.entries(sizes)) {
  for (const lang of ["zh", "en"]) for (const dpr of [1, 1.5, 2]) {
    if (lang === "zh" && dpr === 1) continue;
    cases.push({ view, width, height, lang, dpr, name: `${view}-${lang}-${width}-dpr${dpr}` });
  }
}
for (const view of ["general", "hotkeys", "mapping"]) {
  for (const lang of ["zh", "en"]) for (const dpr of [1, 1.5, 2]) {
    const [width, height] = view === "mapping" ? [900, 640] : [960, 714];
    cases.push({ view, width, height, lang, dpr, name: `${view}-${lang}-${width}-dpr${dpr}` });
  }
}
const report = {
  kind: "Offline HTML/CSS design proposal rendering QA",
  revision: "round2",
  generatedAt: new Date().toISOString(),
  nativeDpiValidation: false,
  scope: "Chromium CSS-pixel layout and deviceScaleFactor only. Not native Win32 DPI, native accessibility, key recording, foreground lookup, persistence, or runtime validation.",
  sources: ["../reviews/interaction-review.md", "../reviews/visual-review.md", "../reviews/interaction-final.md", "../evidence/native/N15.txt", "../evidence/native/N21.txt", "../evidence/native/N24.jpg", "../evidence/native/N37.jpg", "../fixture/audit_preview.cpp"],
  cases: []
};
async function inspect(page, spec) {
  return page.evaluate(({ view, width, height, dpr }) => {
    const box = element => {
      if (!element) return null;
      const r = element.getBoundingClientRect();
      return { x: r.x, y: r.y, width: r.width, height: r.height, right: r.right, bottom: r.bottom };
    };
    const all = selector => [...document.querySelectorAll(selector)];
    const rect = selector => box(document.querySelector(selector));
    const checks = {};
    const close = (a, b) => Math.abs(a - b) < 0.6;
    checks.viewport = innerWidth === width && innerHeight === height && devicePixelRatio === dpr;
    checks.noDocumentOverflow = document.documentElement.scrollWidth <= width && document.documentElement.scrollHeight <= height;
    checks.logoLoaded = all("img").every(img => img.complete && img.naturalWidth > 0);
    checks.iconsPresent = all("svg").every(svg => svg.childElementCount > 0);
    const textOverflow = all("button, label, .startup-label, .builtin-label, .nav-item, h1, h2, .cell, .field-label")
      .filter(el => !el.classList.contains("path"))
      .filter(el => el.scrollWidth > el.clientWidth + 1 || el.scrollHeight > el.clientHeight + 1)
      .map(el => ({ text: el.textContent.trim(), box: box(el) }));
    checks.noUnexpectedTextOverflow = textOverflow.length === 0;
    const metrics = { titlebar: rect(".titlebar"), footer: rect(".footer"), nav: rect(".sidebar"), page: rect(".page"), textOverflow };
    if (view !== "menu") {
      checks.titlebar30 = close(metrics.titlebar.height, 30);
      checks.footer72 = close(metrics.footer.height, 72);
      checks.footerButtons = all(".footer button").every(el => close(box(el).width, 88) && close(box(el).height, 36));
      const controls = all("main button, main select, main textarea, main input").filter(el => !el.closest(".table-body"));
      checks.controlsAboveFooter = controls.every(el => box(el).bottom <= metrics.footer.y + 0.6);
      checks.footerOrder = all(".footer button").map(el => el.textContent.trim()).join("|") ===
        (document.documentElement.lang === "en" ? (view === "mapping" ? "Cancel|Save" : "Cancel|Apply|Save") : (view === "mapping" ? "取消|保存" : "取消|应用|保存"));
    }
    if (view === "general" || view === "hotkeys") {
      checks.nav216 = close(metrics.nav.width, 216);
      checks.contentPadding28 = close(rect("h1").x - metrics.page.x, 28) && close(metrics.page.right - rect("h1").right, 28);
    }
    if (view === "general") {
      metrics.simple = rect(".simple");
      checks.simple640 = close(metrics.simple.width, 640);
      checks.startupOff = document.querySelector(".toggle").getAttribute("aria-checked") === "false";
      const startupLabel = document.documentElement.lang === "en" ? "Automatically start Simpilot after signing in to Windows" : "登录 Windows 后自动启动 Simpilot";
      checks.IF03LoginWording = document.querySelector(".startup-label").textContent === startupLabel && document.querySelector(".toggle").getAttribute("aria-label") === startupLabel;
      checks.language = document.querySelector("#language").value === (document.documentElement.lang === "en" ? "en" : "zh");
      checks.rowHeights = all(".setting-row").map(el => box(el).height).join(",") === "64,72";
    }
    if (view === "hotkeys") {
      const data = window.PROPOSAL_FIXTURE.rows;
      metrics.builtinColumns = getComputedStyle(document.querySelector(".builtin-row")).gridTemplateColumns;
      metrics.tableHeader = rect(".table-head");
      metrics.tableBody = rect(".table-body");
      metrics.visibleRows = all(".data-row").filter(el => box(el).y >= metrics.tableBody.y && box(el).bottom <= metrics.tableBody.bottom + 0.6).length;
      checks.fixedBuiltinGrid = metrics.builtinColumns === "132px 12px 228px 8px 32px 12px 44px";
      checks.record228x36 = all(".record-key").every(el => close(box(el).width, 228) && close(box(el).height, 36));
      checks.builtinStates = all(".builtin .toggle").map(el => el.getAttribute("aria-checked")).join(",") === "true,true,true,false";
      checks.builtinKeys = all(".record-key span").map(el => el.textContent).join("|") === "Alt+Space|Ctrl+Space|Ctrl+Alt+S|Win+S";
      checks.sixteenRows = data.length === 16 && all(".data-row").length === 16;
      checks.firstFiveKeys = data.slice(0, 5).every((row, i) => row.key === `Ctrl+Alt+F${i + 1}`);
      checks.everyFourthOff = data.every((row, i) => row.enabled === (i % 4 !== 3));
      checks.fiveVisibleRows = metrics.visibleRows >= 5;
      checks.header32rows36 = close(metrics.tableHeader.height, 32) && all(".data-row").every(el => close(box(el).height, 36));
      checks.toolbar32 = close(rect(".toolbar").height, 32);
      checks.longPathsRetained = all("[data-path]").every((el, i) => el.dataset.path === data[i].path && el.getAttribute("aria-label") === data[i].path);
      checks.scrollable = document.querySelector(".table-body").scrollHeight > document.querySelector(".table-body").clientHeight;
    }
    if (view === "mapping") {
      checks.fourModifiersPerSide = all('[data-side="source"] [data-modifier]').length === 4 && all('[data-side="target"] [data-modifier]').length === 4;
      checks.allKeyFields = !!document.querySelector("#source-key") && !!document.querySelector("#source-chord") && !!document.querySelector("#target-key");
      checks.mappingValues = document.querySelector("#source-key").value === "A" && document.querySelector("#target-key").value === "F1" && document.querySelector("#application").value === "audittool.exe";
      checks.mappingModifiers = document.querySelector("#source-modifier-0").selectedIndex === 1 && document.querySelector("#target-modifier-0").selectedIndex === 3;
      checks.checkboxes = !document.querySelector("#exact-match").checked && document.querySelector("#mapping-enabled").checked;
      checks.IF01RuleScope = document.querySelector(".rule-enabled").textContent.trim() === (document.documentElement.lang === "en" ? "Enable this rule" : "启用此规则");
      checks.IF02ApplicationLabel = document.querySelector("#application-label").textContent === (document.documentElement.lang === "en" ? "Applies to" : "适用应用");
      const scope = document.querySelector(".application-scope");
      checks.IF02ApplicationGroup = scope.getAttribute("role") === "group" && scope.getAttribute("aria-labelledby") === "application-label" && scope.contains(document.querySelector("#application")) && scope.contains(document.querySelector("#exact-match")) && !scope.contains(document.querySelector("#mapping-enabled"));
      metrics.applicationField = rect("#application");
      metrics.exactMatch = rect("#exact-match");
      checks.IF02ExactMatchAligned = close(metrics.exactMatch.x, metrics.applicationField.x);
      const exactLabel = rect(".exact-match");
      const ruleLabel = rect(".rule-enabled");
      checks.IF02AdjacentAndDistinct = exactLabel.y >= metrics.applicationField.bottom && exactLabel.y - metrics.applicationField.bottom <= 12.6 && exactLabel.right + 16 <= ruleLabel.x;
      checks.purposeComplete = document.querySelector("#purpose").value === "Long mapping purpose: review selection across multiple workspace panels / 选择切换";
      const purposeInput = document.querySelector("#purpose");
      const measure = document.createElement("canvas").getContext("2d");
      measure.font = getComputedStyle(purposeInput).font;
      checks.purposeNotClipped = measure.measureText(purposeInput.value).width <= purposeInput.clientWidth - 20;
      checks.recordAndForeground = all(".record-line button").length === 2 && !!document.querySelector(".app-field button");
      checks.noMappingScroll = document.querySelector(".mapping-body").scrollHeight <= document.querySelector(".mapping-body").clientHeight;
    }
    if (view === "menu") {
      metrics.popups = all(".popup").map(box);
      checks.menu280 = metrics.popups.every(r => close(r.width, 280));
      checks.row32 = all(".menu-row").every(el => close(box(el).height, 32));
      checks.separator9 = all(".menu-separator").every(el => close(box(el).height, 9));
      checks.rendererAppendedMenu2 = all(".main-menu > .menu-separator").length === 2 && document.querySelector("[data-second-menu] .menu-label").textContent === (document.documentElement.lang === "en" ? "Menu 2" : "菜单 2") && document.querySelector("[data-second-menu]").getAttribute("aria-haspopup") === "menu" && document.querySelector("[data-second-menu]").getAttribute("aria-expanded") === "false" && !!document.querySelector("[data-second-menu] .arrow");
      checks.menu2AfterSeparator = document.querySelector("[data-second-menu]").previousElementSibling.matches(".menu-separator[data-renderer-appended]") && document.querySelector(".main-menu").lastElementChild.matches("[data-second-menu]");
      checks.firstLevelOnly = all(".popup").length === 2 && all('[aria-expanded="true"]').length === 1 && document.querySelector("[data-review-utilities]").getAttribute("aria-expanded") === "false";
      checks.fixtureDocument2Retained = window.PROPOSAL_MENU[2].children[1].children[0].label === "Document 2 / 文档 - detailed review";
      checks.popupInViewport = metrics.popups.every(r => r.x >= 0 && r.y >= 0 && r.right <= width && r.bottom <= height);
      checks.noPopupOverlap = metrics.popups[0].right <= metrics.popups[1].x;
      checks.neutralWorkspace = getComputedStyle(document.querySelector(".menu-workspace")).backgroundColor === "rgb(231, 232, 234)";
    }
    return { checks, metrics };
  }, spec);
}
(async () => {
  const executablePath = [
    process.env.SIMPILOT_CHROMIUM,
    chromium.executablePath(),
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
  ].filter(Boolean).find(file => fs.existsSync(file));
  if (!executablePath) throw new Error("No local Chromium browser found. Set SIMPILOT_CHROMIUM; no browser will be downloaded automatically.");
  const browser = await chromium.launch({ headless: true, executablePath });
  report.browserExecutable = executablePath;
  report.browserVersion = browser.version();
  try {
    for (const spec of cases) {
      const context = await browser.newContext({ viewport: { width: spec.width, height: spec.height }, deviceScaleFactor: spec.dpr, locale: spec.lang === "en" ? "en-US" : "zh-CN", offline: true });
      const page = await context.newPage();
      const errors = [];
      const externalRequests = [];
      page.on("pageerror", error => errors.push(error.message));
      page.on("request", request => { if (!request.url().startsWith("file:") && !request.url().startsWith("data:")) externalRequests.push(request.url()); });
      const url = pathToFileURL(path.join(__dirname, "index.html"));
      url.search = `?view=${spec.view}${spec.lang === "en" ? "&lang=en" : ""}`;
      await page.goto(url.href);
      await page.waitForFunction(() => window.PROPOSAL_READY === true);
      await page.mouse.move(0, 0);
      const result = await inspect(page, spec);
      result.checks.noScriptErrors = errors.length === 0;
      result.checks.noExternalRequests = externalRequests.length === 0;
      const png = await page.screenshot({ path: path.join(output, `${spec.name}.png`), animations: "disabled" });
      const physicalWidth = png.readUInt32BE(16);
      const physicalHeight = png.readUInt32BE(20);
      result.checks.physicalPngSize = Math.abs(physicalWidth - spec.width * spec.dpr) <= 1 && Math.abs(physicalHeight - spec.height * spec.dpr) <= 1;
      if (spec.view === "hotkeys") {
        const fixturePaths = await page.evaluate(() => window.PROPOSAL_FIXTURE.rows.map(row => row.path));
        result.checks.pathsMatchN15 = fixturePaths.every(value => n15.includes(value));
        await page.locator(".path").first().focus();
        result.checks.fullPathOnFocus = await page.locator(".path-tooltip").textContent() === await page.locator(".path").first().getAttribute("data-path");
        await page.locator(".path").last().focus();
        result.checks.lastRowReachable = await page.locator(".table-body").evaluate(el => el.scrollTop + el.clientHeight >= el.scrollHeight - 1);
        if (spec.name === "hotkeys") {
          await page.locator(".path").last().blur();
          await page.screenshot({ path: path.join(output, "hotkeys-last-rows.png") });
        }
      }
      const entry = { ...spec, file: `renders/${spec.name}.png`, png: { width: physicalWidth, height: physicalHeight, sha256: crypto.createHash("sha256").update(png).digest("hex") }, ...result, errors, externalRequests };
      entry.passed = Object.values(result.checks).every(Boolean);
      report.cases.push(entry);
      console.log(`${entry.passed ? "PASS" : "FAIL"} ${spec.name}${entry.passed ? "" : ": " + Object.keys(result.checks).filter(key => !result.checks[key]).join(", ")}`);
      await context.close();
    }
  } finally {
    await browser.close();
    report.summary = { total: report.cases.length, passed: report.cases.filter(item => item.passed).length, failed: report.cases.filter(item => !item.passed).length };
    fs.writeFileSync(path.join(__dirname, "qa.json"), JSON.stringify(report, null, 2) + "\n");
  }
  if (report.summary.failed) process.exitCode = 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
