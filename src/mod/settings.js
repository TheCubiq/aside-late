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
let safeMode = false;
let browserVersion = "";
let failure = "";
let github = { api: "https://api.github.com", raw: "https://raw.githubusercontent.com" };
let repos = [];
let catalog = {};        // By repo: { sha, patches, error }.
let catalogLoading = false;
let installing = null;   // { id, text, failed }.
let pollTimer = 0;

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

// A request to the mod is the path of a file that doesn't exist. Each one's unique, so it's never cached.
function ask(path) {
  return read(`${path}/${Date.now()}${Math.random().toString(36).slice(2, 8)}`);
}

async function loadPatches() {
  const list = await read("patches.json").then(response => response.json());
  patches = list.patches;
  safeMode = list.safeMode;
}

async function loadRepos() {
  const text = await read("repos.txt").then(response => response.text());
  repos = text.split(/\r?\n/).filter(Boolean);
}

async function load() {
  const [version, addresses] = await Promise.all([
    read("version.txt").then(response => response.text()),
    read("github.json").then(response => response.json()),
    loadPatches(),
    loadRepos(),
  ]);
  browserVersion = version.trim();
  github = addresses;
}

// What's in a repo, straight from GitHub. Only the page's own session keeps it.
async function loadRepo(repo) {
  const get = async url => {
    const response = await fetch(url, { cache: "no-store" });
    if (!response.ok) throw new Error(response.status === 404 ? "not found" : response.status === 403 ? "GitHub says to try again later" : `GitHub answered ${response.status}`);
    return response.json();
  };
  try {
    const { sha } = await get(`${github.api}/repos/${repo}/commits/HEAD`);
    const entries = await get(`${github.api}/repos/${repo}/contents/patches?ref=${sha}`);
    const folders = entries.filter(entry => entry.type === "dir").map(entry => entry.name);
    const found = await Promise.all(folders.map(folder =>
      get(`${github.raw}/${repo}/${sha}/patches/${folder}/patch.json`).then(manifest => ({ ...manifest, folder })).catch(() => null)));
    catalog[repo] = { sha, patches: found.filter(Boolean) };
  } catch (error) {
    catalog[repo] = { error: error.message };
  }
}

async function loadCatalog() {
  if (catalogLoading) return;
  catalogLoading = true;
  render();
  await Promise.all(repos.filter(repo => !catalog[repo]).map(loadRepo));
  catalogLoading = false;
  render();
}

// The mod works in the background and leaves its progress in files. This follows along until it's done.
function isBusy() {
  return (installing && !installing.failed) || patches.some(patch => patch.status);
}

async function poll() {
  pollTimer = 0;
  try {
    await loadPatches();
    if (installing && !installing.failed) {
      const text = await read("install.txt").then(response => response.text()).catch(() => "");
      const [id, state, message] = text.split("\t");
      if (id === installing.id && state === "failed") installing = { id, failed: true, text: message };
      else if (id === installing.id && state === "done") installing = null;
      else if (id === installing.id) installing.text = message;
    }
  } catch (error) {
    failure = `Couldn't read the patch list: ${error.message}`;
  }
  render();
  follow();
}

function follow() {
  if (!pollTimer && isBusy()) pollTimer = setTimeout(poll, 400);
}

function isOn(patch) {
  return patch.running || (patch.wanted && !patch.error && !safeMode);
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
    control ? make("div", "flex min-w-0 shrink-0 justify-end", control) : null);
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
  const on = isOn(patch);
  button.type = "button";
  button.setAttribute("role", "switch");
  button.setAttribute("aria-checked", String(on));
  button.setAttribute("aria-label", patch.title);
  button.disabled = Boolean(patch.status);
  button.append(make("span"));
  button.addEventListener("click", async () => {
    failure = "";
    patch.status = "Applying";
    patch.running = patch.wanted = !on;
    patch.error = "";
    render();
    try {
      await ask(`set/${patch.id}/${on ? 0 : 1}`);
    } catch (error) {
      failure = `Couldn't change ${patch.title}: ${error.message}`;
    }
    follow();
    document.querySelector(`#${PAGE_ID} [aria-label="${CSS.escape(patch.title)}"]`)?.focus();
  });
  return button;
}

function textButton(label, action) {
  const button = make("button", "aside-late-button", label);
  button.type = "button";
  button.addEventListener("click", action);
  return button;
}

function install(repo, sha, id) {
  installing = { id, text: "Starting" };
  failure = "";
  ask(`install/${repo.replace("/", "~")}/${id}/${sha}`).catch(error => {
    installing = { id, failed: true, text: error.message };
    render();
  });
  render();
  follow();
}

function describe(patch) {
  if (patch.status) return patch.status;
  if (patch.error) return patch.error;
  if (patch.needsRestart && patch.wanted !== patch.running) return patch.wanted ? "Applies the next time Aside starts." : "Goes away the next time Aside starts.";
  return patch.installed ? `${patch.description} From ${patch.source}.` : patch.description;
}

function installedRow(patch) {
  const description = describe(patch);
  const newer = patch.installed && catalog[patch.source]?.sha;
  const controls = make("div", "flex items-center gap-2", toggle(patch));
  if (patch.installed) {
    if (newer && newer !== patch.sha) controls.prepend(textButton("Update", () => install(patch.source, newer, patch.id)));
    controls.prepend(textButton("Remove", async () => {
      patch.status = "Removing";
      render();
      await ask(`remove/${patch.id}`);
      follow();
    }));
  }
  return row(patch.title, description, controls);
}

function availableRows() {
  const installedIds = new Set(patches.map(patch => patch.id));
  const rows = [];
  for (const repo of repos) {
    const entry = catalog[repo];
    const remove = textButton("Remove", async () => {
      await ask(`repo/remove/${repo.replace("/", "~")}`);
      delete catalog[repo];
      await loadRepos();
      render();
    });
    if (!entry) {
      rows.push(row(repo, "Looking for patches.", remove));
    } else if (entry.error) {
      rows.push(row(repo, `Couldn't read this repo: ${entry.error}.`, remove));
    } else {
      const available = entry.patches.filter(patch => !installedIds.has(patch.id));
      rows.push(row(repo, available.length ? null : "Nothing new here.", remove));
      for (const patch of available) {
        const busy = installing?.id === patch.id;
        const text = busy ? installing.text : patch.description;
        const control = busy && !installing.failed ? null : textButton("Install", () => install(repo, entry.sha, patch.folder));
        rows.push(row(patch.title ?? patch.id, busy && installing.failed ? `Couldn't install it: ${text}` : text, control));
      }
    }
  }
  return rows;
}

function addRepoRow() {
  const input = make("input", "aside-late-input");
  input.placeholder = "owner/name";
  input.setAttribute("aria-label", "GitHub repo");
  const add = async event => {
    event?.preventDefault();
    const repo = input.value.trim().replace(/^https:\/\/github\.com\//, "").replace(/\/$/, "");
    if (!/^[\w.-]+\/[\w.-]+$/.test(repo)) {
      failure = "A repo looks like owner/name.";
      render();
      return;
    }
    failure = "";
    await ask(`repo/add/${repo.replace("/", "~")}`);
    await loadRepos();
    render();
    loadCatalog();
  };
  input.addEventListener("keydown", event => {
    if (event.key === "Enter") add(event);
  });
  const controls = make("div", "flex items-center gap-2", input, textButton("Add", add));
  return row("Add a repo", "A GitHub repo with a patches folder.", controls);
}

function modsPage() {
  return [
    header("Mods"),
    failure ? make("p", "text-destructive mb-4 text-sm", failure) : null,
    safeMode ? make("p", "text-muted-foreground mb-4 text-sm", "Aside crashed while starting last time, so mods are off for now. Turn back on the ones you want.") : null,
    section("Installed", ...patches.map(installedRow)),
    section("Get more", ...availableRows(), addRepoRow()),
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
  // Redrawing shouldn't lose what someone's typing.
  const typing = document.activeElement?.matches?.(".aside-late-input") ? document.activeElement.value : null;
  body.replaceChildren(...(openPage === "mods" ? modsPage() : aboutPage()).filter(Boolean));
  if (typing !== null) {
    const input = body.querySelector(".aside-late-input");
    input.value = typing;
    input.focus();
  }
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
  if (name === "mods") loadCatalog();
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
    .aside-late-button { height: 28px; padding: 0 10px; border-radius: 8px; font-size: 13px; font-weight: 500; border: 1px solid var(--border); cursor: pointer; white-space: nowrap; }
    .aside-late-button:hover { background: var(--muted); }
    .aside-late-button:focus-visible, .aside-late-input:focus-visible { outline: 2px solid var(--ring, currentColor); outline-offset: 2px; }
    .aside-late-input { width: 150px; height: 28px; padding: 0 8px; border-radius: 8px; font-size: 13px; border: 1px solid var(--border); background: transparent; }
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
  }
  addNavigation();
  // Aside draws and redraws its settings whenever it likes.
  new MutationObserver(addNavigation).observe(document.body, { childList: true, subtree: true });
}

start();
