"use strict";
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const os = require("node:os");
const root = path.resolve(__dirname, "..");
const repo = path.resolve(root, "../..");
const readJson = file => JSON.parse(fs.readFileSync(file, "utf8").replace(/^\uFEFF/, ""));
const hash = file => crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
const write = (file, content) => fs.writeFileSync(path.join(root, file), content, "utf8");
const escape = text => String(text).replace(/[&<>"']/g, c => ({
  "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
})[c]);
const indexFile = path.join(root, "evidence/native/index.json");
const index = readJson(indexFile);
for (const item of index) {
  if (Number(item.id.slice(1)) <= 8) {
    item.scale = "Native DPI not measured; tool-normalized logical screenshot dimensions";
    item.metadataCorrection = "Removed invalid 100% inference from sidebar width.";
  }
  item.reviewEligible = item.id !== "N14";
  if (item.id === "N14") item.supersededBy = "N29";
  for (const image of item.images) {
    if (hash(path.join(root, "evidence/native", image.file)) !== image.sha256)
      throw new Error(`Original image changed: ${image.file}`);
  }
}
write("evidence/native/index.json", JSON.stringify(index, null, 2) + "\n");
const before = readJson(path.join(root, "evidence/source-before.json"));
const after = readJson(path.join(root, "evidence/source-after.json"));
const oldFiles = new Map(before.files.map(file => [file.path, file.sha256]));
const changed = after.files.filter(file => oldFiles.get(file.path) !== file.sha256);
const removed = before.files.filter(file => !after.files.some(other => other.path === file.path));
const inputs = fs.readFileSync(path.join(repo, "build/design-audit/input-provenance.txt"), "utf8");
write("evidence/fixture-inputs.txt", inputs);
const libraryChecks = [];
for (const line of inputs.split(/\r?\n/)) {
  const match = /^([a-f0-9]{64})  (.+)$/i.exec(line);
  if (match) libraryChecks.push({ path: match[2], expected: match[1], actual: hash(match[2]) });
}
const selfTests = fs.readdirSync(os.tmpdir(), { withFileTypes: true })
  .filter(item => item.isDirectory() && item.name.startsWith("Simpilot-native-audit-"))
  .map(item => path.join(os.tmpdir(), item.name, "self-test-results.ini"))
  .filter(file => fs.existsSync(file))
  .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
if (selfTests.length) fs.copyFileSync(selfTests[0], path.join(root, "evidence/self-test-results.ini"));
const verification = {
  generatedAt: new Date().toISOString(),
  productionFilesCompared: after.files.length,
  productionFilesUnchanged: changed.length === 0 && removed.length === 0,
  changed, removed, headUnchanged: before.head === after.head,
  originalImageFilesVerified: index.reduce((sum, item) => sum + item.images.length, 0),
  evidenceIds: index.length, retiredEvidenceIds: ["N14"],
  linkedInputsUnchangedAfterCurrentWorkspaceBuild: libraryChecks.every(item => item.expected === item.actual),
  libraryChecks,
  fixtureExecutableSha256: hash(path.join(repo, "build/design-audit/Release/audit_preview.exe")),
  fixtureSelfTestReceipt: selfTests.length ? "self-test-results.ini" : null,
  nativeDpiMeasured: [144],
  nativeDpiUnverified: [96, 192],
  nativeCoverageComplete: false,
};
write("evidence/verification.json", JSON.stringify(verification, null, 2) + "\n");
if (!verification.productionFilesUnchanged || !verification.headUnchanged ||
    !verification.linkedInputsUnchangedAfterCurrentWorkspaceBuild)
  throw new Error("Source or linked-input verification failed. Inspect verification.json.");

const rows = index.map(item => `| ${item.id}${item.reviewEligible ? "" : " (退役)"} | ${item.page} | ${item.language} | ${item.nativeDpi || "未记录"} | ${item.state} | ${
  item.images.map(image => `[${image.file}](evidence/native/${image.file}) (${image.width}×${image.height})`).join("<br>")
} |`).join("\n");
write("SCREENSHOT-INDEX.md", `# 原生截图索引

${index.length} 个唯一编号；${verification.originalImageFilesVerified} 个原图文件。N14 已退役，由 N29 替代，保留溯源，不进入最终产品审计。
图像尺寸为采集工具归一化逻辑像素；DPI 是独立原生测量。N01–N08 不据侧栏宽度推断 100%。
时间、SHA、窗口来源详见 [机器索引](evidence/native/index.json)；完整元数据见 [metadata](evidence/metadata/)。
N28 是重建托盘宿主；N32 是系统 MessageBox。模拟数据及执行边界见 [夹具说明](fixture/README)。

| 编号 | 页面 | 语言 | 原生 DPI | 状态与边界 | 原图 / 逻辑尺寸 |
| --- | --- | --- | --- | --- | --- |
${rows}

未录制/未捕获的状态见 [审计缺口](AUDIT.md#覆盖与缺口)，不以空白图或效果图填补。
`);
const css = `body{margin:0;padding:32px;background:#f7f8fa;color:#202124;font:14px/1.5 "Segoe UI","Microsoft YaHei UI",sans-serif;letter-spacing:0}h1{font-size:26px;margin:0 0 8px}h2{font-size:20px;margin:0 0 16px}p{max-width:1000px}a{color:#0067b8}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:24px}figure{margin:0;min-width:0}figure img{width:100%;height:220px;object-fit:contain;object-position:top left;background:#eceef1;border:1px solid #e3e6ea;box-sizing:border-box}figcaption{margin-top:8px;overflow-wrap:anywhere}section{padding:28px 0;border-top:1px solid #e3e6ea}.pair{display:grid;grid-template-columns:1fr 1fr;gap:24px}.pair img{height:auto;max-height:none}.tag{font-weight:600;color:#60656d}.retired{opacity:.55}@media(max-width:850px){.grid,.pair{grid-template-columns:1fr}}`;
const cards = index.map(item => `<figure class="${item.reviewEligible ? "" : "retired"}"><a href="evidence/native/${item.images[0].file}"><img src="evidence/native/${item.images[0].file}" alt="${escape(item.id + " " + item.page)}"></a><figcaption><strong>${item.id} ${escape(item.page)}</strong><br>${escape(item.state)}${item.reviewEligible ? "" : "<br>退役，由N29替代"}<br>${item.images.map(image => `<a href="evidence/native/${image.file}">${escape(image.file)}</a>`).join(" · ")}</figcaption></figure>`);
write("GALLERY.html", `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Simpilot 原生截图证据</title><style>${css}</style><h1>原生截图证据</h1><p>2026-09-26 · 原始采集图片 · 模拟数据 · 不是改版效果图。N14退役；N28为重建托盘宿主；N32为系统界面。点击原图查看全尺寸。<a href="AUDIT.md">审计边界</a> · <a href="COMPARE.html">前后对照</a></p><div class="grid">${cards.join("")}</div></html>`);
const comparisons = [
  ["常规设置", "N09", "general", "相同1028×714画布、简体中文、启动关闭。"],
  ["全局热键", "N15", "hotkeys", "相同1028×714画布、相同四个内置值与16条模拟数据；完整长路径取自N15。"],
  ["键盘映射编辑", "N21", "mapping", "相同963×651画布、相同规则值/用途/应用；文字与分组为修订提案。"],
  ["快捷启动菜单", "N24", "menu", "相同主菜单数据；原生只捕获根菜单，主菜单子菜单仍是证据缺口。提案展示一级子菜单；第二菜单真实展开另见N27。不能将此组当作同状态原生实现对照。"],
];
write("COMPARE.html", `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Simpilot 原生与设计提案对照</title><style>${css}</style><h1>Simpilot 设计对照</h1><p>左侧：当前工作区原生截图，未修改。右侧：离线设计提案，未实现。真实证据与提案分别存档。<a href="GALLERY.html">全部原生截图</a> · <a href="proposals/gallery.html">提案与缩放矩阵</a></p>${comparisons.map(([title,id,view,note],i)=>`<section><h2>${i+1}. ${title}</h2><p>${note}</p><div class="pair"><figure><div class="tag">原生证据 ${id}</div><a href="evidence/native/${id}.jpg"><img src="evidence/native/${id}.jpg"></a></figure><figure><div class="tag">设计提案 P0${i+1} · 非产品截图</div><a href="proposals/renders/${view}.png"><img src="proposals/renders/${view}.png"></a></figure></div></section>`).join("")}</html>`);
fs.mkdirSync(path.join(root, "evidence/contact-sheets"), { recursive: true });
for (let start = 0; start < cards.length; start += 6) {
  write(`evidence/contact-sheets/sheet-${Math.floor(start/6)+1}.html`,
    `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><base href="../../"><title>Native evidence overview</title><style>${css}</style><h1>原生证据 ${index[start].id}–${index[Math.min(start+5,index.length-1)].id}</h1><p>总览不替代全尺寸原图。N14退役；N28模拟托盘宿主；N32系统界面。</p><div class="grid">${cards.slice(start,start+6).join("")}</div></html>`);
}
console.log(JSON.stringify(verification, null, 2));
