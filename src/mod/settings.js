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
let shownState = "";
let changesSent = 0;
let dragging = false;
let glassMap = "";
const switches = new Map();

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
  const sent = changesSent;
  const list = await read("patches.json").then(response => response.json());
  // An answer from before a click doesn't know about that click.
  if (sent !== changesSent) return;
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

// The mod works in the background, and other tabs can change things too. The page follows along quickly
// while something's happening, and slowly the rest of the time.
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
    renderIfChanged();
  } catch {
    // A file can be caught changing, and the next look gets it right.
  }
  follow();
}

function follow() {
  if (pollTimer || openPage !== "mods" || document.hidden) return;
  pollTimer = setTimeout(poll, isBusy() ? 300 : 2000);
}

function followNow() {
  clearTimeout(pollTimer);
  pollTimer = 0;
  if (openPage === "mods" && !document.hidden) poll();
}

function isOn(patch) {
  return patch.wanted && (patch.running || (!patch.error && !safeMode));
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

// Aside's own switch, from its switch component, whose classes and motion these are. That component
// can't be borrowed because it lives inside React, so the same parts are put together here.
const THUMB = { width: 24, height: 16, pressedWidth: 36, pressedHeight: 24, off: 14, on: 26, y: 2, pressedY: -2 };
const SPRING = { stiffness: 540, damping: 32, mass: 0.65 };

function animateSpring(thumb, target) {
  thumb.target = target;
  if (thumb.running) return;
  thumb.running = true;
  let last = performance.now();
  const step = now => {
    const seconds = Math.min((now - last) / 1000, 1 / 30);
    last = now;
    let moving = false;
    for (const key of Object.keys(thumb.target)) {
      const force = -SPRING.stiffness * (thumb.value[key] - thumb.target[key]) - SPRING.damping * thumb.velocity[key];
      thumb.velocity[key] += (force / SPRING.mass) * seconds;
      thumb.value[key] += thumb.velocity[key] * seconds;
      if (Math.abs(thumb.value[key] - thumb.target[key]) > 0.01 || Math.abs(thumb.velocity[key]) > 0.01) moving = true;
      else (thumb.value[key] = thumb.target[key]), (thumb.velocity[key] = 0);
    }
    thumb.draw();
    if (moving) requestAnimationFrame(step);
    else thumb.running = false;
  };
  requestAnimationFrame(step);
}

// The glass needs a picture Aside draws at runtime, and any of its switches on screen has a copy.
function findGlassMap() {
  if (glassMap) return;
  glassMap = document.querySelector('[data-slot="switch"] feImage')?.getAttribute("href") ?? "";
}

function glassFilter(id) {
  const namespace = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(namespace, "svg");
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("class", "absolute size-0 overflow-hidden");
  const filter = document.createElementNS(namespace, "filter");
  const attributes = { id, filterUnits: "objectBoundingBox", primitiveUnits: "objectBoundingBox", "color-interpolation-filters": "sRGB", x: "-0.18", y: "-0.35", width: "1.36", height: "1.7" };
  for (const [name, value] of Object.entries(attributes)) filter.setAttribute(name, value);
  const parts = [
    ["feFlood", { "flood-color": "rgb(128,128,128)", result: "mapBg" }],
    ["feImage", { href: glassMap, x: "0", y: "0", width: "1", height: "1", preserveAspectRatio: "none", result: "rawMap" }],
    ["feComposite", { in: "rawMap", in2: "mapBg", operator: "over", result: "map" }],
    ["feGaussianBlur", { in: "SourceGraphic", stdDeviation: "0.006", result: "soft" }],
    ["feDisplacementMap", { in: "soft", in2: "map", scale: "0.045", xChannelSelector: "R", yChannelSelector: "G", result: "refracted" }],
    ["feColorMatrix", { in: "refracted", type: "saturate", values: "1.25" }],
  ];
  for (const [tag, values] of parts) {
    const part = document.createElementNS(namespace, tag);
    for (const [name, value] of Object.entries(values)) part.setAttribute(name, value);
    filter.append(part);
  }
  svg.append(filter);
  return svg;
}

let nextGlass = 0;

function makeSwitch(checked, onChange) {
  const BASE = "focus-visible:ring-ring/50 relative inline-flex h-5 min-h-5 w-10 min-w-10 shrink-0 cursor-pointer items-center overflow-visible rounded-full align-middle transition-colors duration-200 focus-visible:ring-3 focus-visible:outline-none disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50";
  const glassId = `aside-late-glass-${nextGlass++}`;
  findGlassMap();
  const button = make("button");
  button.type = "button";
  button.setAttribute("role", "switch");
  button.dataset.slot = "switch";
  const fill = make("span");
  const solid = make("span", "pointer-events-none absolute top-0 left-0 block overflow-hidden rounded-full bg-white shadow-sm");
  const glassTint = make("span");
  glassTint.style.filter = glassMap ? `url(#${glassId})` : "";
  const glass = make("span", "pointer-events-none absolute top-0 left-0 overflow-hidden rounded-full bg-white/15 opacity-0 shadow-[0_0_0_1px_rgb(255_255_255_/_0.42),inset_1px_0_0_rgb(255_255_255_/_0.95),inset_-1px_0_0_rgb(125_211_252_/_0.45),inset_0_-1px_1px_rgb(0_0_0_/_0.18)] backdrop-blur-[1px]", glassTint);
  button.append(glassMap ? glassFilter(glassId) : "", fill, solid, glass);

  let isChecked = checked;
  let center = null;       // Where a press has the thumb, or null.
  let press = null;
  let justReleased = false;
  const thumb = { value: {}, velocity: { x: 0, y: 0, width: 0, height: 0, solid: 0 }, target: {}, running: false };
  const targets = () => {
    const width = center !== null ? THUMB.pressedWidth : THUMB.width;
    return {
      x: center !== null ? center - width / 2 : isChecked ? THUMB.on - THUMB.width / 2 : THUMB.off - THUMB.width / 2,
      y: center !== null ? THUMB.pressedY : THUMB.y,
      width,
      height: center !== null ? THUMB.pressedHeight : THUMB.height,
      solid: center !== null ? 0 : 1,
    };
  };
  thumb.draw = () => {
    const { x, y, width, height, solid: opacity } = thumb.value;
    for (const part of [solid, glass]) {
      part.style.width = `${width}px`;
      part.style.height = `${height}px`;
      part.style.transform = `translateX(${x}px) translateY(${y}px)`;
    }
    solid.style.opacity = opacity;
    glass.style.opacity = 1 - opacity;
    glassTint.style.transform = `translate(${-x}px, ${-y}px)`;
  };
  const paint = () => {
    button.className = `${BASE} ${isChecked ? "bg-emerald-500" : "dark:bg-muted bg-neutral-200"}`;
    button.setAttribute("aria-checked", String(isChecked));
    button.dataset.state = isChecked ? "checked" : "unchecked";
    fill.className = `absolute inset-0 rounded-full bg-emerald-500 transition-opacity duration-200 ${isChecked ? "opacity-100" : "opacity-0"}`;
    glassTint.className = `absolute top-[-2px] left-[-2px] h-5 w-10 rounded-full transition-colors duration-200 ${isChecked ? "bg-emerald-500/35" : "dark:bg-muted/55 bg-neutral-200"}`;
  };
  const clamp = event => {
    const left = button.getBoundingClientRect().left;
    return Math.min(Math.max(event.clientX - left - press.offset, THUMB.off), THUMB.on);
  };
  const release = () => {
    center = null;
    press = null;
    dragging = false;
  };

  button.addEventListener("pointerdown", event => {
    center = isChecked ? THUMB.on : THUMB.off;
    press = { pointer: event.pointerId, offset: event.clientX - button.getBoundingClientRect().left - center, start: center, dragged: false };
    dragging = true;
    button.focus();
    button.setPointerCapture(event.pointerId);
    animateSpring(thumb, targets());
  });
  button.addEventListener("pointermove", event => {
    if (!press || press.pointer !== event.pointerId) return;
    center = clamp(event);
    press.dragged ||= Math.abs(center - press.start) > 3;
    animateSpring(thumb, targets());
  });
  button.addEventListener("pointerup", event => {
    if (!press) return;
    const at = clamp(event);
    const next = press.dragged || Math.abs(at - press.start) > 3 ? at >= (THUMB.off + THUMB.on) / 2 : !isChecked;
    if (button.hasPointerCapture(event.pointerId)) button.releasePointerCapture(event.pointerId);
    release();
    // The click that follows is this same press. A redraw can mean it never comes.
    justReleased = true;
    setTimeout(() => {
      justReleased = false;
    });
    if (next === isChecked) animateSpring(thumb, targets());
    else onChange(next);
  });
  button.addEventListener("pointercancel", () => {
    release();
    animateSpring(thumb, targets());
  });
  // The keyboard and assistive technology arrive as a click with no press before it.
  button.addEventListener("click", () => {
    if (justReleased) {
      justReleased = false;
      return;
    }
    onChange(!isChecked);
  });

  button.setChecked = value => {
    isChecked = value;
    paint();
    animateSpring(thumb, targets());
  };
  thumb.value = targets();
  paint();
  thumb.draw();
  return button;
}

function toggle(patch) {
  let button = switches.get(patch.id);
  if (!button) {
    button = makeSwitch(isOn(patch), next => {
      const current = patches.find(entry => entry.id === patch.id);
      if (!current || current.status) {
        if (current) button.setChecked(isOn(current));
        return;
      }
      failure = "";
      changesSent++;
      current.status = "Applying";
      current.wanted = next;
      if (!current.needsRestart) current.running = next;
      current.error = "";
      renderIfChanged();
      ask(`set/${current.id}/${next ? 1 : 0}`)
        .catch(error => {
          failure = `Couldn't change ${current.title}: ${error.message}`;
        })
        .finally(() => {
          changesSent++;
          followNow();
        });
    });
    switches.set(patch.id, button);
  }
  button.setAttribute("aria-label", patch.title);
  button.setChecked(isOn(patch));
  return button;
}

// Aside's outline button and text field, from their components.
function textButton(label, action) {
  const button = make("button", "focus-visible:border-ring focus-visible:ring-ring/50 aria-invalid:ring-destructive/20 dark:aria-invalid:ring-destructive/40 aria-invalid:border-destructive dark:aria-invalid:border-destructive/50 group/button inline-flex shrink-0 items-center justify-center rounded-lg border bg-clip-padding text-sm font-medium whitespace-nowrap transition-all will-change-transform outline-none select-none focus-visible:ring-3 disabled:pointer-events-none disabled:opacity-50 aria-invalid:ring-3 border-border bg-background hover:bg-muted hover:text-foreground dark:bg-input/30 dark:border-input dark:hover:bg-input/50 h-8 gap-1.5 px-2.5", make("span", "", label));
  button.dataset.slot = "button";
  button.type = "button";
  button.addEventListener("click", action);
  return button;
}

function install(repo, sha, id) {
  installing = { id, text: "Starting" };
  failure = "";
  ask(`install/${repo.replace("/", "~")}/${id}/${sha}`)
    .catch(error => {
      installing = { id, failed: true, text: error.message };
      render();
    })
    .finally(followNow);
  render();
}

function describe(patch) {
  // Switching only takes a moment, and the switch itself already shows it.
  if (patch.status && patch.status !== "Applying") return patch.status;
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
      followNow();
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
  const input = make("input", "border-input file:text-foreground placeholder:text-muted-foreground focus-visible:border-ring focus-visible:ring-ring/50 disabled:bg-input/50 aria-invalid:border-destructive aria-invalid:ring-destructive/20 dark:bg-input/30 dark:disabled:bg-input/80 dark:aria-invalid:border-destructive/50 dark:aria-invalid:ring-destructive/40 h-8 w-full min-w-0 truncate rounded-lg border bg-transparent px-2.5 py-1 text-base text-sm transition-colors outline-none focus-visible:ring-3 disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50 aria-invalid:ring-3");
  input.dataset.slot = "input";
  input.style.width = "160px";
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
  // Redrawing shouldn't lose what someone's typing, or where the keyboard was.
  const focused = document.activeElement;
  const typing = focused?.dataset?.slot === "input" && body.contains(focused) ? focused.value : null;
  body.replaceChildren(...(openPage === "mods" ? modsPage() : aboutPage()).filter(Boolean));
  shownState = stateKey();
  if (typing !== null) {
    const input = body.querySelector('[data-slot="input"]');
    input.value = typing;
    input.focus();
  } else if (focused?.dataset?.slot === "switch" && body.contains(focused)) {
    focused.focus();
  }
}

function stateKey() {
  return JSON.stringify([patches, safeMode, installing, failure, repos, Object.keys(catalog), catalogLoading]);
}

// The mods page is looked at again and again, and most of the time nothing's different.
function renderIfChanged() {
  if (!dragging && stateKey() !== shownState) render();
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
  clearTimeout(pollTimer);
  pollTimer = 0;
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
  if (name === "mods") {
    loadCatalog();
    followNow();
  }
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

async function start() {
  try {
    await load();
  } catch (error) {
    failure = `Couldn't read the patch list: ${error.message}`;
  }
  addNavigation();
  // Aside draws and redraws its settings whenever it likes.
  new MutationObserver(addNavigation).observe(document.body, { childList: true, subtree: true });
  document.addEventListener("visibilitychange", followNow);
}

start();
