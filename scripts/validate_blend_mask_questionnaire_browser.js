#!/usr/bin/env node
"use strict";

const fs = require("fs");
const http = require("http");
const path = require("path");
const { chromium } = require("playwright");

async function main() {
  const htmlPath = path.resolve(process.argv[2]);
  const screenshotPath = path.resolve(process.argv[3]);
  const fragmentPath = process.argv[4] ? path.resolve(process.argv[4]) : null;
  const analysis = JSON.parse(fs.readFileSync(
    path.join(path.dirname(htmlPath), "blend-mask-analysis.json"), "utf8"));
  const expectedQuestions = analysis.questions.length;
  const root = path.dirname(path.dirname(htmlPath));
  const server = http.createServer((request, response) => {
    const requested = decodeURIComponent(new URL(request.url, "http://127.0.0.1").pathname);
    if (requested === "/favicon.ico") {
      response.writeHead(204).end();
      return;
    }
    const relative = requested === "/" ? path.relative(root, htmlPath) : requested.slice(1);
    const file = path.resolve(root, relative);
    if (file !== root && !file.startsWith(root + path.sep)) {
      response.writeHead(403).end();
      return;
    }
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
      response.writeHead(404).end();
      return;
    }
    const types = { ".html": "text/html; charset=utf-8", ".png": "image/png", ".json": "application/json" };
    response.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream" });
    fs.createReadStream(file).pipe(response);
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;
  const browserExecutable = process.env.EI_MASK_REVIEW_BROWSER
    || "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
  const browser = await chromium.launch({ headless: true,
    executablePath: browserExecutable });
  const page = await browser.newPage({ viewport: { width: 1500, height: 1000 }, deviceScaleFactor: 1 });
  const errors = [];
  page.on("console", message => { if (message.type() === "error") errors.push(message.text()); });
  page.on("pageerror", error => errors.push(error.message));
  await page.addInitScript(() => {
    window.openai = {
      sendFollowUpMessage: async payload => localStorage.setItem("mask-review-submit", JSON.stringify(payload))
    };
  });
  try {
    const url = `http://127.0.0.1:${port}/${path.relative(root, htmlPath).split(path.sep).join("/")}`;
    await page.goto(url, { waitUntil: "networkidle" });
    const questions = await page.locator(".mask-question").count();
    if (questions !== expectedQuestions)
      throw new Error(`question count ${questions} != ${expectedQuestions}`);
    const bindingErrors = await page.evaluate(items => {
      const errors = [];
      for (const item of items) {
        const card = document.querySelector(`[data-key="${item.key}"]`);
        if (!card) { errors.push(`missing ${item.key}`); continue; }
        const expectedPosition = `${-item.column * 256}px ${-item.row * 256}px`;
        const checks = [
          [card.querySelector(".requested-uniform img")?.getAttribute("src"), `../blend-mask-review/${item.ownerUniform}`, "requested uniform"],
          [card.querySelector(".support-uniform img")?.getAttribute("src"), `../blend-mask-review/${item.supportUniform}`, "support uniform"],
          [card.querySelector(".ownership-mask .sprite")?.style.backgroundImage, item.maskSheet, "mask sheet"],
          [card.querySelector(".resulting-blend .sprite")?.style.backgroundImage, item.resultSheet, "result sheet"],
          [card.querySelector(".authored-source .sprite")?.style.backgroundImage, item.sourceSheet, "source sheet"],
        ];
        for (const [actual, expected, label] of checks)
          if (!actual || !actual.includes(expected))
            errors.push(`${item.key} ${label}: ${actual} != ${expected}`);
        for (const sprite of card.querySelectorAll(".sprite"))
          if (sprite.style.backgroundPosition !== expectedPosition)
            errors.push(`${item.key} position: ${sprite.style.backgroundPosition} != ${expectedPosition}`);
        if (errors.length >= 20) break;
      }
      return errors;
    }, analysis.questions);
    if (bindingErrors.length)
      throw new Error(`card binding errors: ${bindingErrors.join(" | ")}`);
    const first = page.locator(".mask-question").first();
    await first.scrollIntoViewIfNeeded();
    const imageState = await first.evaluate(card => ({
      figures: card.querySelectorAll("figure").length,
      loadedImages: [...card.querySelectorAll("img")].every(image => image.complete && image.naturalWidth === 256 && image.naturalHeight === 256),
      backgrounds: [...card.querySelectorAll(".sprite")].every(sprite => getComputedStyle(sprite).backgroundImage !== "none")
    }));
    if (imageState.figures !== 5 || !imageState.loadedImages || !imageState.backgrounds)
      throw new Error(`first-card visuals ${JSON.stringify(imageState)}`);
    const key = await first.getAttribute("data-key");
    await first.locator(".answer").selectOption({ label: "Reject: wrong orientation" });
    await first.locator(".note").fill("browser persistence check");
    await page.reload({ waitUntil: "networkidle" });
    const reloaded = page.locator(`[data-key="${key}"]`);
    if (await reloaded.locator(".answer").inputValue() !== "Reject: wrong orientation")
      throw new Error("answer did not survive reload");
    if (await reloaded.locator(".note").inputValue() !== "browser persistence check")
      throw new Error("note did not survive reload");
    await page.locator("#style-filter").selectOption({ index: 1 });
    const visible = await page.locator(".mask-question:not([hidden])").count();
    if (visible <= 0 || visible >= questions)
      throw new Error(`style filter visible count ${visible}`);
    await page.locator("#style-filter").selectOption("");
    await page.locator("#show-answers").click();
    const exportText = await page.locator("#answer-export-text").inputValue();
    if (!exportText.includes(`${key}: Reject: wrong orientation — browser persistence check`)
        || !exportText.includes("Apply only explicit mask decisions"))
      throw new Error("export text contract failed");
    await page.locator("#submit-answers").click();
    await page.waitForFunction(() => localStorage.getItem("mask-review-submit") !== null);
    await reloaded.locator(".answer").selectOption({ index: 0 });
    await reloaded.locator(".note").fill("");
    await reloaded.scrollIntoViewIfNeeded();
    await page.screenshot({ path: screenshotPath, fullPage: false });
    if (fragmentPath) {
      const encoded = fs.readFileSync(screenshotPath).toString("base64");
      fs.writeFileSync(fragmentPath,
        `<div id="r38-mask-review-preview"><img src="data:image/png;base64,${encoded}" alt="R38 blend-mask questionnaire showing requested and support uniform tiles, selected ownership mask, generated blend outcome, authored stock transition, and decision controls" style="display:block;width:100%;height:auto"></div>\n`,
        "utf8");
    }
    if (errors.length) throw new Error(`browser errors: ${errors.join(" | ")}`);
    console.log(JSON.stringify({ status: "PASS", questions,
      allCardBindings: "PASS", firstCardFigures: 5,
      persistence: "PASS", styleFilterVisible: visible, export: "PASS",
      directSubmit: "PASS", consoleErrors: 0, screenshot: screenshotPath,
      fragment: fragmentPath }, null, 2));
  } finally {
    await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => { console.error(error.stack || error); process.exit(1); });
