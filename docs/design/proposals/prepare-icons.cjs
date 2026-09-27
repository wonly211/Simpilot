"use strict";
const fs = require("node:fs");
const path = require("node:path");
const base = process.env.SIMPILOT_NODE_MODULES || "C:/Users/WoNly/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules";
const lucide = require(path.join(base, "lucide/dist/cjs/lucide.js"));
const names = ["Minus", "Square", "X", "Keyboard", "Plus", "Pencil", "Trash2", "Folder", "ChevronRight"];
const icons = Object.fromEntries(names.map(name => {
  if (!lucide[name]) throw new Error(`Missing Lucide icon: ${name}`);
  return [name, lucide[name]];
}));
fs.writeFileSync(path.join(__dirname, "icons.js"), `// Generated subset of Lucide icons. See icons-LICENSE.txt.\nwindow.PROPOSAL_ICONS = ${JSON.stringify(icons)};\n`);
const license = ["LICENSE", "LICENSE.txt"].map(name => path.join(base, "lucide", name)).find(file => fs.existsSync(file));
if (!license) throw new Error("Lucide license not found");
fs.copyFileSync(license, path.join(__dirname, "icons-LICENSE.txt"));
console.log("Prepared local Lucide icon subset and license.");
