// A card in the corner of the new tab page when there's a newer Aside Late: Update fetches it in the background, then Restart
// Aside brings it in with the same windows and tabs. Later puts the card away for that version, and Settings still has it.
// Set to download on their own, updates come in with no card, and Mods offers the restart.

import { download, findUpdate, percent, restart, updateMode, updateState } from "./updates.js";

const DISMISSED_KEY = "asideLate.dismissed";
const POLL_MS = 400;

const STYLE = `
:host { all: initial; }
.card {
  position: fixed; right: 16px; bottom: 16px; z-index: 2147483647; box-sizing: border-box; width: 300px; padding: 14px;
  border-radius: 14px; border: 1px solid var(--border-surface, rgb(0 0 0 / 0.1));
  background: var(--background, #fff); color: var(--foreground, #222);
  box-shadow: 0 1px 2px rgb(0 0 0 / 0.06), 0 8px 24px rgb(0 0 0 / 0.12);
  font: 13px/1.45 var(--font-sans, system-ui, sans-serif); -webkit-font-smoothing: antialiased;
  animation: in 200ms cubic-bezier(0.2, 0, 0, 1);
}
@keyframes in { from { opacity: 0; transform: translateY(8px); } }
@media (prefers-reduced-motion: reduce) { .card { animation: none; } }
h2 { margin: 0; font-size: 13px; font-weight: 600; }
p { margin: 2px 0 0; color: var(--muted-foreground, rgb(0 0 0 / 0.55)); text-wrap: pretty; }
a { color: inherit; }
.actions { display: flex; align-items: center; gap: 6px; margin-top: 12px; }
.actions a { margin-left: auto; color: var(--muted-foreground, rgb(0 0 0 / 0.55)); font-size: 12px; text-underline-offset: 2px; }
button {
  height: 28px; padding: 0 10px; border-radius: 8px; border: 0; font: inherit; font-weight: 500; cursor: pointer;
  background: transparent; color: var(--foreground, #222); transition: background-color 120ms, transform 120ms;
}
button:hover { background: var(--muted, rgb(0 0 0 / 0.05)); }
button:active { transform: scale(0.97); }
button:focus-visible { outline: 2px solid var(--primary, #000); outline-offset: 2px; }
button.primary { background: var(--primary, #000); color: var(--background, #fff); }
button.primary:hover { opacity: 0.9; }
.bar { height: 4px; margin-top: 10px; border-radius: 2px; background: var(--muted, rgb(0 0 0 / 0.08)); overflow: hidden; }
.fill { height: 100%; border-radius: 2px; background: var(--primary, #000); transition: width 200ms; }
.bar.unknown .fill { width: 30% !important; animation: slide 1.1s ease-in-out infinite; }
@keyframes slide { from { transform: translateX(-100%); } to { transform: translateX(340%); } }
`;

let update = null;
let state = { state: "idle" };
let host = null;
let pollTimer = 0;

function dismissed(stage) {
  try {
    return localStorage.getItem(DISMISSED_KEY) === `${update.version}:${stage}`;
  } catch {
    return false;
  }
}

function dismiss(stage) {
  try {
    localStorage.setItem(DISMISSED_KEY, `${update.version}:${stage}`);
  } catch {
    // Without storage it just comes back on the next new tab.
  }
  host?.remove();
  host = null;
}

function make(tag, className, ...children) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  node.append(...children.filter(Boolean));
  return node;
}

function button(label, action, primary) {
  const node = make("button", primary ? "primary" : "", label);
  node.type = "button";
  node.addEventListener("click", action);
  return node;
}

function notesLink() {
  const link = make("a", "", "What's new");
  link.href = update.notes;
  link.target = "_blank";
  link.rel = "noreferrer";
  return link;
}

// What the card says at each stage, or null when there's nothing to show.
function content() {
  const ours = state.version === update.version;
  if (ours && state.state === "downloading") {
    const done = percent(state);
    const fill = make("div", "fill");
    fill.style.width = `${done ?? 0}%`;
    return [make("h2", "", `Downloading Aside Late ${update.version}`), make("p", "", done === null ? "In the background." : `${done}%`), make("div", done === null ? "bar unknown" : "bar", fill)];
  }
  if (ours && state.state === "restarting") return [make("h2", "", "Restarting Aside…")];
  if (ours && state.state === "ready") {
    if (dismissed("ready")) return null;
    return [make("h2", "", `Aside Late ${update.version} is ready`), make("p", "", "Restart Aside to use it. Your windows and tabs come back."),
      make("div", "actions", button("Restart Aside", () => restart().then(poll), true), button("Later", () => dismiss("ready")))];
  }
  if (ours && state.state === "failed") {
    return [make("h2", "", "Couldn't update Aside Late"), make("p", "", state.error),
      make("div", "actions", button("Try again", () => download(update).then(poll), true), button("Later", () => dismiss("offer")))];
  }
  if (dismissed("offer")) return null;
  return [make("h2", "", `Aside Late ${update.version} is out`), make("p", "", `You have ${update.current}. It downloads in the background.`),
    make("div", "actions", button("Update", () => download(update).then(poll), true), button("Later", () => dismiss("offer")), notesLink())];
}

function render() {
  const parts = content();
  if (!parts) {
    host?.remove();
    host = null;
    return;
  }
  if (!host) {
    host = document.createElement("aside-late-notice");
    const shadow = host.attachShadow({ mode: "open" });
    shadow.append(make("style", "", STYLE), make("div", "card"));
    document.body.append(host);
  }
  const card = host.shadowRoot.querySelector(".card");
  card.setAttribute("role", "status");
  card.replaceChildren(...parts);
}

async function poll() {
  clearTimeout(pollTimer);
  state = await updateState();
  render();
  if (state.state === "downloading" || state.state === "restarting") pollTimer = setTimeout(poll, POLL_MS);
}

async function start() {
  update = await findUpdate();
  if (!update) return;
  if (updateMode() === "auto") {
    const now = await updateState();
    if (now.version !== update.version || now.state === "idle") download(update);
    return;
  }
  await poll();
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) poll();
  });
}

if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start, { once: true });
else start();
