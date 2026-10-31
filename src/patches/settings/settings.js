// Adds Patched stuff to Aside's settings by working with the page as it's drawn. It relies on
// the page's structure and its CSS classes, not its code, so a new build at worst looks plainer.

const HOME = "/aside-late";
const NAV = "nav.flex.flex-col.gap-3";
const GROUP_ID = "aside-late-nav";
const PAGE_ID = "aside-late-page";

const ICONS = {
  mods: "M14.7 6.3a4 4 0 0 0-5.4 5.1L4 16.7V20h3.3l5.3-5.3a4 4 0 0 0 5.1-5.4l-2.7 2.7-2.9-.7-.7-2.9z",
  about: "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zm0 8v5m0-8.5v.5",
};

let openPage = null;
let hiddenContent = null;
let activeNative = [];
let patches = [];
let disabled = new Set();
let startedDisabled = null;
let browserVersion = "";
let failure = "";

function make(tag, className, ...children) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  node.append(...children.filter(child => child !== null && child !== undefined));
  return node;
}

async function read(name) {
  const response = await fetch(`${HOME}/${name}`, { cache: "no-store" });
  if (!response.ok) throw new Error(`${name}: ${response.status}`);
  return response;
}

async function load() {
  const [list, off, version] = await Promise.all([
    read("patches.json").then(response => response.json()),
    read("disabled.txt").then(response => response.text()),
    read("version.txt").then(response => response.text()),
  ]);
  patches = list.patches;
  browserVersion = version.trim();
  disabled = new Set(off.split(/\r?\n/).filter(Boolean));
  // What's running was decided when Aside started, whatever's been switched since.
  startedDisabled = new Set(patches.filter(patch => !patch.running).map(patch => patch.id));
}

async function setEnabled(id, enabled) {
  await read(`set/${id}/${enabled ? 1 : 0}/${Date.now()}`);
  const response = await read("disabled.txt");
  disabled = new Set((await response.text()).split(/\r?\n/).filter(Boolean));
  if (disabled.has(id) === enabled) throw new Error("the change wasn't saved");
}

function icon(name, template) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  for (const { name: attribute, value } of template?.attributes ?? []) {
    if (attribute !== "viewBox") svg.setAttribute(attribute, value);
  }
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("fill", "none");
  svg.setAttribute("aria-hidden", "true");
  const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
  path.setAttribute("d", ICONS[name]);
  path.setAttribute("stroke", "currentColor");
  path.setAttribute("stroke-width", "1.75");
  path.setAttribute("stroke-linecap", "round");
  path.setAttribute("stroke-linejoin", "round");
  svg.append(path);
  return svg;
}

function row(title, description, control) {
  return make("div", "bg-surface-secondary flex min-h-13 items-center gap-3 p-2.5",
    make("div", "min-w-0 grow px-1",
      make("h4", "text-primary text-sm font-medium", title),
      description ? make("p", "text-muted-foreground text-xs font-medium", description) : null),
    control ? make("div", "flex max-w-40 min-w-0 shrink justify-end", control) : null);
}

function section(title, ...children) {
  return make("section", "scroll-mt-10",
    make("div", "mb-2", make("h2", "font-display text-muted-foreground/80 flex items-center gap-1.5 text-sm font-[550]", title)),
    make("div", "divide-border -mx-3.5 divide-y overflow-hidden rounded-xl border dark:space-y-px dark:divide-y-0 dark:border-transparent dark:bg-transparent", ...children));
}

function header(title) {
  return make("header", "[&_h1]:font-display mb-6 [&_h1]:text-2xl [&_h1]:font-[450]", make("h1", "", title));
}

function toggle(patch) {
  const button = make("button", "aside-late-switch");
  const enabled = !disabled.has(patch.id);
  button.type = "button";
  button.setAttribute("role", "switch");
  button.setAttribute("aria-checked", String(enabled));
  button.setAttribute("aria-label", patch.title);
  button.append(make("span"));
  button.addEventListener("click", async () => {
    button.disabled = true;
    failure = "";
    try {
      await setEnabled(patch.id, disabled.has(patch.id));
    } catch (error) {
      failure = `Couldn't change ${patch.title}: ${error.message}`;
    }
    render();
    document.querySelector(`#${PAGE_ID} [aria-label="${CSS.escape(patch.title)}"]`)?.focus();
  });
  return button;
}

function needsRestart() {
  return patches.some(patch => disabled.has(patch.id) !== startedDisabled.has(patch.id));
}

function modsPage() {
  const list = patches.filter(patch => patch.id !== "settings");
  return [
    header("Mods"),
    failure ? make("p", "text-destructive mb-4 text-sm", failure) : null,
    needsRestart() ? make("p", "text-muted-foreground mb-4 text-sm", "Close Aside and start it with aside-late.exe for these changes to apply.") : null,
    section("Installed mods", ...list.map(patch => row(patch.title, patch.description, toggle(patch)))),
  ];
}

function aboutPage() {
  return [
    header("About patched stuff"),
    section("Aside, with a few personal touches",
      row("Browser version", browserVersion || "Unknown"),
      row("Settings version", chrome?.runtime?.getManifest?.().version ?? "Unknown"),
      row("Made with aside-late", "These community mods are independent of Aside.")),
  ];
}

function render() {
  const page = document.getElementById(PAGE_ID);
  if (!page) return;
  const body = page.firstElementChild;
  body.replaceChildren(...(openPage === "mods" ? modsPage() : aboutPage()).filter(Boolean));
}

function content() {
  return document.querySelector(NAV)?.closest(".h-dvh")?.querySelector(":scope > .min-w-0.grow");
}

function close() {
  document.getElementById(PAGE_ID)?.remove();
  if (hiddenContent) hiddenContent.style.display = "";
  hiddenContent = null;
  for (const node of activeNative) node.setAttribute("data-active", "true");
  activeNative = [];
  openPage = null;
  markActive();
}

function open(name) {
  const host = content();
  if (!host) return;
  openPage = name;
  if (!document.getElementById(PAGE_ID)) {
    hiddenContent = host.firstElementChild;
    if (hiddenContent) hiddenContent.style.display = "none";
    const page = make("div", "h-full overflow-y-auto p-4", make("div", "mx-auto max-w-2xl space-y-8 py-8"));
    page.id = PAGE_ID;
    host.append(page);
    const nav = document.querySelector(NAV);
    activeNative = [...nav.querySelectorAll("[data-active]")].filter(node => !node.closest(`#${GROUP_ID}`));
    for (const node of activeNative) node.removeAttribute("data-active");
  }
  markActive();
  render();
}

function markActive() {
  for (const link of document.querySelectorAll(`#${GROUP_ID} a`)) {
    const active = link.dataset.asideLatePage === openPage;
    if (active) {
      link.setAttribute("data-active", "true");
      link.setAttribute("aria-current", "page");
    } else {
      link.removeAttribute("data-active");
      link.removeAttribute("aria-current");
    }
  }
}

// Native items are copied so they keep whatever Aside styles them with. A copy has no React
// handlers, so it can't navigate anywhere.
function navItem(template, name, label) {
  const item = template.cloneNode(true);
  const link = item.querySelector("a");
  link.removeAttribute("id");
  link.removeAttribute("href");
  link.removeAttribute("aria-current");
  link.removeAttribute("data-active");
  link.setAttribute("role", "link");
  link.setAttribute("tabindex", "0");
  link.dataset.asideLatePage = name;
  const svg = link.querySelector("svg");
  if (svg) svg.replaceWith(icon(name, svg));
  const text = [...link.querySelectorAll("span")].find(span => !span.children.length && span.textContent.trim());
  if (text) text.textContent = label;
  const go = event => {
    event.preventDefault();
    open(name);
  };
  link.addEventListener("click", go);
  link.addEventListener("keydown", event => {
    if (event.key === "Enter" || event.key === " ") go(event);
  });
  return item;
}

function addNavigation() {
  const nav = document.querySelector(NAV);
  if (!nav || nav.querySelector(`#${GROUP_ID}`)) return;
  const heading = nav.querySelector("h5");
  const template = [...nav.querySelectorAll("li")].find(item => item.querySelector("a") && item.querySelectorAll("a").length === 1);
  if (!heading || !template) return;
  const group = make("div", "", make("h5", heading.className, "Patched stuff"),
    make("ul", heading.nextElementSibling?.className ?? "", navItem(template, "mods", "Mods"), navItem(template, "about", "About")));
  group.id = GROUP_ID;
  nav.append(group);
  markActive();
  // Choosing anything of Aside's own goes back to Aside's own page.
  nav.addEventListener("click", event => {
    if (openPage && event.target.closest("a, button") && !event.target.closest(`#${GROUP_ID}`)) close();
  }, true);
}

function addStyle() {
  const style = document.createElement("style");
  style.textContent = `
    .aside-late-switch { position: relative; width: 40px; height: 20px; flex: none; border-radius: 9999px; background: var(--muted); cursor: pointer; transition: background-color .2s; }
    .aside-late-switch[aria-checked="true"] { background: #10b981; }
    .aside-late-switch span { position: absolute; top: 2px; left: 2px; width: 16px; height: 16px; border-radius: 9999px; background: #fff; box-shadow: 0 1px 2px #0003; transition: transform .2s; }
    .aside-late-switch[aria-checked="true"] span { transform: translateX(20px); }
    .aside-late-switch:focus-visible { outline: 2px solid var(--ring, currentColor); outline-offset: 2px; }
    .aside-late-switch:disabled { opacity: .5; cursor: default; }
    @media (prefers-reduced-motion: reduce) { .aside-late-switch, .aside-late-switch span { transition: none; } }
  `;
  document.head.append(style);
}

async function start() {
  addStyle();
  try {
    await load();
  } catch (error) {
    failure = `Couldn't read the patch list: ${error.message}`;
    startedDisabled = new Set();
  }
  addNavigation();
  // Aside draws and redraws its settings whenever it likes.
  new MutationObserver(addNavigation).observe(document.body, { childList: true, subtree: true });
}

start();
