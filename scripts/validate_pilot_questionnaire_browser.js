#!/usr/bin/env node
"use strict";

const fs = require("fs");
const http = require("http");
const path = require("path");
const { chromium } = require("playwright");

async function main() {
  const htmlPath = path.resolve(process.argv[2]);
  const analysisPath = path.resolve(process.argv[3]);
  const screenshotPath = path.resolve(process.argv[4]);
  const analysis = JSON.parse(fs.readFileSync(analysisPath, "utf8"));
  const root = path.dirname(htmlPath);
  const server = http.createServer((request, response) => {
    const requested = decodeURIComponent(
      new URL(request.url, "http://127.0.0.1").pathname);
    if (requested === "/favicon.ico") {
      response.writeHead(204).end();
      return;
    }
    const relative = requested === "/" ? path.basename(htmlPath) : requested.slice(1);
    const file = path.resolve(root, relative);
    if (file !== root && !file.startsWith(root + path.sep)) {
      response.writeHead(403).end();
      return;
    }
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
      response.writeHead(404).end();
      return;
    }
    const types = {
      ".html": "text/html; charset=utf-8",
      ".png": "image/png",
      ".json": "application/json",
    };
    response.writeHead(200, {
      "Content-Type": types[path.extname(file)] || "application/octet-stream",
    });
    fs.createReadStream(file).pipe(response);
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const browser = await chromium.launch({
    headless: true,
    executablePath: process.env.EI_MASK_REVIEW_BROWSER ||
      "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  });
  const page = await browser.newPage({
    viewport: { width: 1600, height: 1100 }, deviceScaleFactor: 1,
  });
  const errors = [];
  page.on("console", message => {
    if (message.type() === "error") errors.push(message.text());
  });
  page.on("pageerror", error => errors.push(error.message));
  await page.addInitScript(() => {
    window.openai = {
      sendFollowUpMessage: async payload =>
        localStorage.setItem("pilot-review-submit", JSON.stringify(payload)),
    };
  });
  try {
    const url = `http://127.0.0.1:${server.address().port}/${path.basename(htmlPath)}`;
    await page.goto(url, { waitUntil: "networkidle" });
    const questions = await page.locator(".pilot-question").count();
    if (questions !== analysis.questionCount)
      throw new Error(`question count ${questions} != ${analysis.questionCount}`);
    const images = await page.locator("img").count();
    const brokenImages = await page.locator("img").evaluateAll(items =>
      items.filter(image => !image.complete || image.naturalWidth === 0)
        .map(image => image.getAttribute("src")));
    if (brokenImages.length)
      throw new Error(`broken images: ${brokenImages.slice(0, 10).join(", ")}`);
    const first = page.locator(".pilot-question").first();
    await first.locator(".answer").selectOption({ index: 1 });
    await first.locator(".note").fill("browser validation note");
    await page.locator("#show-answers").click();
    const exportText = await page.locator("#answer-export-text").inputValue();
    if (!exportText.includes(analysis.questions[0].key) ||
        !exportText.includes("browser validation note"))
      throw new Error("export text did not preserve the selected answer and note");
    await page.locator("#submit-answers").click();
    await page.waitForFunction(() => localStorage.getItem("pilot-review-submit"));
    const submitted = await page.evaluate(() =>
      JSON.parse(localStorage.getItem("pilot-review-submit")));
    if (!JSON.stringify(submitted).includes(analysis.questions[0].key))
      throw new Error("direct submit did not preserve the first question key");
    await first.scrollIntoViewIfNeeded();
    await page.screenshot({ path: screenshotPath, fullPage: false });
    if (errors.length) throw new Error(errors.join("\n"));
    console.log(JSON.stringify({
      status: "PASS", questions, images, brokenImages: 0,
      export: "PASS", directSubmit: "PASS", consoleErrors: 0,
      screenshot: screenshotPath,
    }, null, 2));
  } finally {
    await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => {
  console.error(error.stack || error.message);
  process.exitCode = 1;
});
