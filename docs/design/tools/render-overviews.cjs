"use strict";
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { chromium } = require(process.env.SIMPILOT_PLAYWRIGHT ||
  "C:/Users/WoNly/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.js");
const root = path.resolve(__dirname, "..");
(async () => {
  const options = { headless: true };
  const browserExe = process.env.SIMPILOT_CHROMIUM ||
    "C:/Program Files/Google/Chrome/Application/chrome.exe";
  if (fs.existsSync(browserExe)) options.executablePath = browserExe;
  const browser = await chromium.launch(options);
  const page = await browser.newPage({ viewport: { width: 1600, height: 1100 }, deviceScaleFactor: 1 });
  const directory = path.join(root, "evidence/contact-sheets");
  const files = fs.readdirSync(directory).filter(file => file.endsWith(".html"));
  const records = [];
  for (const file of files) {
    await page.goto(pathToFileURL(path.join(directory, file)).href);
    await page.evaluate(() => document.fonts.ready);
    const loaded = await page.locator("img").evaluateAll(images => images.every(image => image.complete && image.naturalWidth));
    if (!loaded) throw new Error(`Broken overview image: ${file}`);
    const output = path.join(directory, file.replace(".html", ".png"));
    await page.screenshot({ path: output, fullPage: true });
    records.push({ file, output: path.basename(output), allImagesLoaded: true });
  }
  await page.goto(pathToFileURL(path.join(root, "COMPARE.html")).href);
  const comparisonImagesLoaded = await page.locator("img").evaluateAll(images => images.every(image => image.complete && image.naturalWidth));
  if (!comparisonImagesLoaded) throw new Error("Broken comparison image");
  fs.writeFileSync(path.join(root, "evidence/overview-check.json"), JSON.stringify({
    generatedAt: new Date().toISOString(), kind: "HTML evidence overview only",
    comparisonImagesLoaded, records
  }, null, 2) + "\n");
  await browser.close();
  console.log(`${files.length} contact sheets rendered; comparison images verified.`);
})().catch(error => { console.error(error); process.exitCode = 1; });
