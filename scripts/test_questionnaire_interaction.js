"use strict";

const fs = require("fs");
const vm = require("vm");

const script = fs.readFileSync(0, "utf8");
const handlers = new Map();

function field(value = "") {
  return {
    value,
    listeners: {},
    addEventListener(name, callback) { this.listeners[name] = callback; },
    focus() {},
    select() { this.selected = true; },
  };
}

function button(id) {
  return {
    id,
    addEventListener(name, callback) { handlers.set(`${id}:${name}`, callback); },
  };
}

const answer = field("");
const note = field("");
const block = {
  dataset: { key: "fixture-corner" },
  querySelector(selector) { return selector === ".answer" ? answer : note; },
};
const textarea = field("");
const status = { textContent: "" };
const elements = {
  status,
  "answer-export-text": textarea,
  "show-answers": button("show-answers"),
  "copy-answers": button("copy-answers"),
  "download-answers": button("download-answers"),
  "submit-answers": button("submit-answers"),
};
const storage = new Map();
let sentPrompt = "";

const context = {
  Blob,
  URL: { createObjectURL() { return "blob:test"; }, revokeObjectURL() {} },
  document: {
    querySelectorAll() { return [block]; },
    getElementById(id) { return elements[id]; },
    createElement() {
      return { click() {}, remove() {}, href: "", download: "" };
    },
    body: { appendChild() {} },
  },
  localStorage: {
    getItem(key) { return storage.get(key) || null; },
    setItem(key, value) { storage.set(key, value); },
  },
  navigator: { clipboard: { async writeText(value) { sentPrompt = value; } } },
  setTimeout(callback) { callback(); },
  window: {
    openai: {
      async sendFollowUpMessage({ prompt }) { sentPrompt = prompt; },
    },
  },
};

async function main() {
  vm.runInNewContext(script, context, { filename: "questionnaire.js" });
  if (!textarea.value.includes("focused review decisions")) {
    throw new Error("live export text was not initialized");
  }
  answer.value = "Verified family";
  note.value = "Visible note";
  answer.listeners.change();
  note.listeners.input();
  if (!textarea.value.includes("fixture-corner: Verified family — Visible note")) {
    throw new Error(`live export text did not update after input: ${JSON.stringify(textarea.value)}`);
  }
  if (!storage.size) throw new Error("answers were not persisted");
  await handlers.get("copy-answers:click")();
  if (!sentPrompt.includes("Verified family")) throw new Error("copy omitted the answer");
  sentPrompt = "";
  await handlers.get("submit-answers:click")();
  if (!sentPrompt.includes("Verified family")) throw new Error("submit omitted the answer");
  context.window.openai = undefined;
  textarea.selected = false;
  await handlers.get("submit-answers:click")();
  if (!textarea.selected || !status.textContent.includes("unavailable")) {
    throw new Error("standalone submit fallback did not expose selectable review text");
  }
  handlers.get("download-answers:click")();
  if (!status.textContent.includes("downloaded")) throw new Error("download action did not complete");
  handlers.get("show-answers:click")();
  if (!textarea.selected) throw new Error("manual export text was not selected");
}

main().catch(error => {
  console.error(error.stack || String(error));
  process.exitCode = 1;
});
