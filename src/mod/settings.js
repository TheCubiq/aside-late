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
let optionsPopup = null;  // { patchId, controls, close }.
let optionsSending = 0;

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
  return read(`${path}/${Date.now().toString(36)}${Math.random().toString(36).slice(2, 4)}`);
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

// Aside's own icons, which are filled rather than drawn with a line.
const GLYPHS = {
  configure: "M9 13C10.8645 13 12.4302 14.2744 12.874 16H20C20.5523 16 21 16.4477 21 17C21 17.5523 20.5523 18 20 18H12.874C12.4302 19.7256 10.8645 21 9 21C7.13554 21 5.56982 19.7256 5.12598 18H4C3.44772 18 3 17.5523 3 17C3 16.4477 3.44772 16 4 16H5.12598C5.56982 14.2744 7.13554 13 9 13ZM9 15C7.89478 15 7 15.8948 7 17C7 18.1052 7.89478 19 9 19C10.1052 19 11 18.1052 11 17C11 15.8948 10.1052 15 9 15ZM15 3C16.8645 3 18.4302 4.27443 18.874 6H20C20.5523 6 21 6.44772 21 7C21 7.55228 20.5523 8 20 8H18.874C18.4302 9.72557 16.8645 11 15 11C13.1355 11 11.5698 9.72557 11.126 8H4C3.44772 8 3 7.55228 3 7C3 6.44772 3.44772 6 4 6H11.126C11.5698 4.27443 13.1355 3 15 3ZM15 5C13.8948 5 13 5.89478 13 7C13 8.10522 13.8948 9 15 9C16.1052 9 17 8.10522 17 7C17 5.89478 16.1052 5 15 5Z",
  chevron: "M17.2929 8.79289C17.6834 8.40237 18.3164 8.40237 18.707 8.79289C19.0975 9.18342 19.0975 9.81643 18.707 10.207L12.707 16.207C12.3164 16.5975 11.6834 16.5975 11.2929 16.207L5.29289 10.207C4.90237 9.81643 4.90237 9.18342 5.29289 8.79289C5.68342 8.40237 6.31643 8.40237 6.70696 8.79289L11.9999 14.0859L17.2929 8.79289Z",
  check: "M18.0663 6.16872C18.5254 5.65338 19.3154 5.60731 19.8309 6.06618C20.3463 6.52529 20.3923 7.31536 19.9335 7.83083L10.1337 18.8308C9.90611 19.0862 9.58295 19.2374 9.24108 19.2488C8.89935 19.2601 8.56803 19.131 8.32408 18.8914L4.12389 14.7664C3.6315 14.2826 3.62457 13.4913 4.10826 12.9988C4.59201 12.5064 5.38336 12.4995 5.87584 12.9832L9.13951 16.1882L18.0663 6.16872Z",
  close: "M2 12C2 6.47715 6.47715 2 12 2C17.5228 2 22 6.47715 22 12C22 17.5228 17.5228 22 12 22C6.47715 22 2 17.5228 2 12ZM9.70711 8.29289C9.31658 7.90237 8.68342 7.90237 8.29289 8.29289C7.90237 8.68342 7.90237 9.31658 8.29289 9.70711L10.5858 12L8.29289 14.2929C7.90237 14.6834 7.90237 15.3166 8.29289 15.7071C8.68342 16.0976 9.31658 16.0976 9.70711 15.7071L12 13.4142L14.2929 15.7071C14.6834 16.0976 15.3166 16.0976 15.7071 15.7071C16.0976 15.3166 16.0976 14.6834 15.7071 14.2929L13.4142 12L15.7071 9.70711C16.0976 9.31658 16.0976 8.68342 15.7071 8.29289C15.3166 7.90237 14.6834 7.90237 14.2929 8.29289L12 10.5858L9.70711 8.29289Z",
};

function glyph(name, className) {
  const namespace = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(namespace, "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("fill", "none");
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("class", className);
  const path = document.createElementNS(namespace, "path");
  path.setAttribute("d", GLYPHS[name]);
  path.setAttribute("fill", "currentColor");
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

// Aside's buttons and text field, from their components.
const BUTTON = "focus-visible:border-ring focus-visible:ring-ring/50 aria-invalid:ring-destructive/20 dark:aria-invalid:ring-destructive/40 aria-invalid:border-destructive dark:aria-invalid:border-destructive/50 group/button inline-flex shrink-0 items-center justify-center rounded-lg border bg-clip-padding text-sm font-medium whitespace-nowrap transition-all will-change-transform outline-none select-none focus-visible:ring-3 disabled:pointer-events-none disabled:opacity-50 aria-invalid:ring-3 [&_svg]:pointer-events-none [&_svg]:shrink-0";
const BUTTON_OUTLINE = "border-border bg-background hover:bg-muted hover:text-foreground dark:bg-input/30 dark:border-input dark:hover:bg-input/50 aria-expanded:bg-muted aria-expanded:text-foreground";
const BUTTON_SECONDARY = "border-transparent bg-secondary text-surface-primary-foreground hover:bg-secondary/80";
const BUTTON_DEFAULT = "border-transparent bg-primary text-primary-foreground hover:bg-primary/90";
const BUTTON_TEXT = "h-8 gap-1.5 px-2.5";

function button(look, label, action) {
  const node = make("button", `${BUTTON} ${look} ${BUTTON_TEXT}`, make("span", "", label));
  node.dataset.slot = "button";
  node.type = "button";
  node.addEventListener("click", action);
  return node;
}

function textButton(label, action) {
  return button(BUTTON_OUTLINE, label, action);
}

// The Configure button of Aside's preferred languages.
function configureButton(patch) {
  const icon = glyph("configure", "text-muted-foreground size-4");
  icon.dataset.icon = "inline-start";
  const node = make("button", `${BUTTON} ${BUTTON_OUTLINE} ${BUTTON_TEXT} has-data-[icon=inline-start]:pl-2`, icon, make("span", "", "Configure"));
  node.dataset.slot = "button";
  node.dataset.asideLateConfigure = patch.id;
  node.type = "button";
  node.setAttribute("aria-label", `Configure ${patch.title}`);
  node.setAttribute("aria-haspopup", "dialog");
  node.addEventListener("click", () => openOptions(patch.id));
  return node;
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
  if (optionsOf(patch).length) controls.prepend(configureButton(patch));
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

function optionsOf(patch) {
  try {
    const list = JSON.parse(patch.options ?? "[]");
    return Array.isArray(list) ? list.filter(option => option.key) : [];
  } catch {
    return [];
  }
}

function fallbackOf(option) {
  return String(option.default);
}

function sendOption(patchId, key, value) {
  optionsSending++;
  changesSent++;
  return ask(`option/${patchId}/${key}/${value}`).catch(error => {
    failure = `Couldn't change that: ${error.message}`;
  }).finally(() => {
    optionsSending--;
    changesSent++;
    followNow();
  });
}

// A dragged slider changes far more often than anything can be drawn, and requests can overtake each other.
// So one goes at a time with a short gap, always with the latest value.
function latestOnly(send) {
  let waiting = null;
  let busy = false;
  const next = () => {
    busy = waiting !== null;
    if (!busy) return;
    const value = waiting;
    waiting = null;
    Promise.all([send(value), new Promise(resolve => setTimeout(resolve, 50))]).then(next);
  };
  return value => {
    waiting = value;
    if (!busy) next();
  };
}

// Aside's select, from its component. That one lives inside React, so its parts are put together here.
function makeSelect(option, value, onChange) {
  const SELECT = "border-input focus-visible:border-ring focus-visible:ring-ring/50 data-placeholder:text-muted-foreground dark:bg-input/30 dark:hover:bg-input/50 flex w-fit items-center justify-between gap-1.5 rounded-lg border bg-transparent px-2.5 py-1 text-sm font-medium whitespace-nowrap transition-colors outline-none select-none focus-visible:ring-3 disabled:cursor-not-allowed disabled:opacity-50 data-[size=default]:h-8 [&_svg]:pointer-events-none [&_svg]:shrink-0";
  const ITEM = "focus:bg-accent focus:text-accent-foreground relative flex w-full cursor-default items-center gap-1.5 rounded-xl py-1.5 pr-10 pl-2 text-sm outline-hidden select-none";
  const choices = option.choices ?? [];
  let current = value;
  const shown = make("span", "");
  shown.dataset.slot = "select-value";
  const trigger = make("button", SELECT, shown, glyph("chevron", "text-muted-foreground size-4"));
  trigger.type = "button";
  trigger.dataset.slot = "select-trigger";
  trigger.dataset.size = "default";
  trigger.setAttribute("role", "combobox");
  trigger.setAttribute("aria-haspopup", "listbox");
  trigger.setAttribute("aria-expanded", "false");
  trigger.setAttribute("aria-label", option.title);
  const paint = () => {
    shown.textContent = choices.find(choice => choice.value === current)?.title ?? current;
  };

  let popup = null;
  const close = refocus => {
    if (!popup) return;
    popup.remove();
    popup = null;
    trigger.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", outside, true);
    if (refocus) trigger.focus();
  };
  const outside = event => {
    if (popup && !popup.contains(event.target) && !trigger.contains(event.target)) close(false);
  };
  const open = () => {
    const items = choices.map(choice => {
      const selected = choice.value === current;
      const check = selected ? make("span", "pointer-events-none absolute right-2 flex size-4 items-center justify-center", glyph("check", "pointer-events-none size-3!")) : null;
      const item = make("div", ITEM, make("span", "inline-flex min-w-0 flex-1 items-center gap-1.5 truncate", choice.title), check);
      item.dataset.slot = "select-item";
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", String(selected));
      item.tabIndex = -1;
      item.addEventListener("pointermove", () => item.focus());
      item.addEventListener("click", () => {
        close(true);
        if (choice.value === current) return;
        current = choice.value;
        paint();
        onChange(current);
      });
      return item;
    });
    popup = make("div", "fixed isolate z-50 flex flex-col overflow-hidden rounded-2xl p-1.5 shadow-lg bg-glass border-glass", ...items);
    popup.dataset.slot = "select-content";
    popup.setAttribute("role", "listbox");
    popup.setAttribute("aria-label", option.title);
    // In the popup rather than over it, so that the popup's keyboard handling still applies.
    trigger.closest('[data-slot="dialog-content"]').append(popup);
    const area = trigger.getBoundingClientRect();
    const frame = popup.parentElement.getBoundingClientRect();
    popup.style.minWidth = `${area.width}px`;
    // The popup moves its contents with a transform, which also makes it what "fixed" is relative to.
    const below = area.bottom + 4 + popup.offsetHeight <= window.innerHeight - 8;
    popup.style.left = `${area.right - frame.left - Math.max(popup.offsetWidth, area.width)}px`;
    popup.style.top = `${(below ? area.bottom + 4 : area.top - 4 - popup.offsetHeight) - frame.top}px`;
    trigger.setAttribute("aria-expanded", "true");
    document.addEventListener("pointerdown", outside, true);
    (items[choices.findIndex(choice => choice.value === current)] ?? items[0])?.focus();
    popup.addEventListener("keydown", event => {
      const index = items.indexOf(document.activeElement);
      const go = {
        ArrowDown: Math.min(index + 1, items.length - 1),
        ArrowUp: Math.max(index - 1, 0),
        Home: 0,
        End: items.length - 1,
      }[event.key];
      if (go !== undefined) items[go].focus();
      else if (event.key === "Enter" || event.key === " ") document.activeElement.click();
      else if (event.key === "Tab") close(true);
      else if (event.key === "Escape") close(true);
      else return;
      event.preventDefault();
      event.stopPropagation();
    });
  };
  trigger.addEventListener("click", () => {
    if (popup) close(true);
    else open();
  });
  trigger.addEventListener("keydown", event => {
    if (event.key !== "ArrowDown" && event.key !== "ArrowUp") return;
    event.preventDefault();
    if (!popup) open();
  });
  trigger.setValue = next => {
    current = next;
    paint();
  };
  trigger.closeList = () => close(false);
  paint();
  return trigger;
}

// Aside has no slider of its own, so this takes the colors and thumb of its switch. The input underneath
// is a real one, and that gives it the keyboard and assistive technology.
function makeSlider(option, value, onChange) {
  const min = Number(option.min ?? 0);
  const max = Number(option.max ?? 100);
  const unit = option.unit ?? "";
  const fill = make("span", "absolute inset-y-0 left-0 rounded-full bg-emerald-500");
  const track = make("span", "dark:bg-muted absolute inset-x-0 top-1/2 block h-1.5 -translate-y-1/2 overflow-hidden rounded-full bg-neutral-200", fill);
  const thumb = make("span", "pointer-events-none absolute top-1/2 block h-3.5 w-5 -translate-y-1/2 rounded-full bg-white shadow-sm ring-1 ring-black/10");
  const input = make("input", "peer absolute inset-0 m-0 size-full cursor-pointer opacity-0 disabled:cursor-not-allowed");
  input.type = "range";
  input.min = min;
  input.max = max;
  input.step = option.step ?? 1;
  input.setAttribute("aria-label", option.title);
  const reading = make("span", "text-muted-foreground w-9 text-right text-xs font-medium tabular-nums");
  const paint = () => {
    const share = (Number(input.value) - min) / (max - min || 1);
    fill.style.width = `${share * 100}%`;
    // The thumb stays inside the track at both ends.
    thumb.style.left = `calc(${share} * (100% - 1.25rem))`;
    reading.textContent = `${input.value}${unit}`;
    input.setAttribute("aria-valuetext", reading.textContent);
  };
  input.addEventListener("input", () => {
    paint();
    onChange(input.value);
  });
  input.value = value;
  paint();
  const node = make("div", "flex items-center gap-2",
    make("div", "has-focus-visible:ring-ring/50 relative block h-5 w-32 rounded-full has-focus-visible:ring-3", track, thumb, input), reading);
  node.setValue = next => {
    input.value = next;
    paint();
  };
  return node;
}

function optionControl(patch, option, value, onChange) {
  if (option.type === "switch") {
    const control = makeSwitch(value === "true", next => {
      control.setChecked(next);
      onChange(String(next));
    });
    control.setAttribute("aria-label", option.title);
    control.setValue = next => control.setChecked(next === "true");
    return control;
  }
  if (option.type === "select") return makeSelect(option, value, onChange);
  return makeSlider(option, value, onChange);
}

// The row and bordered box from Aside's Create routine popup.
function optionRow(option, control) {
  return make("div", "flex min-h-11 items-center justify-between gap-3 py-1.5 pr-2 pl-3", make("span", "text-sm select-none", option.title), control);
}

// Aside's accordion, from its component, with the same animation.
function accordionItem(title, open, content) {
  const trigger = make("button", "group/accordion-trigger focus-visible:border-ring focus-visible:ring-ring/50 relative flex flex-1 items-start justify-between rounded-lg border border-transparent py-2.5 text-left text-sm font-medium transition-all outline-none focus-visible:ring-3", title,
    glyph("chevron", "text-muted-foreground pointer-events-none ml-auto size-4 shrink-0 transition-transform group-aria-expanded/accordion-trigger:rotate-180"));
  trigger.type = "button";
  trigger.dataset.slot = "accordion-trigger";
  const panel = make("div", "data-open:animate-accordion-down data-closed:animate-accordion-up overflow-hidden text-sm", make("div", "pt-0 pb-2.5", content));
  panel.dataset.slot = "accordion-content";
  panel.setAttribute("role", "region");
  const show = (next, animate) => {
    open = next;
    trigger.setAttribute("aria-expanded", String(open));
    panel.hidden = false;
    panel.style.setProperty("--accordion-panel-height", `${panel.scrollHeight}px`);
    panel.toggleAttribute("data-open", open);
    panel.toggleAttribute("data-closed", !open);
    if (!animate) panel.style.animationName = "none";
    else panel.style.animationName = "";
    if (!open && !animate) panel.hidden = true;
  };
  panel.addEventListener("animationend", () => {
    if (!open) panel.hidden = true;
  });
  trigger.addEventListener("click", () => show(!open, true));
  const item = make("div", "not-last:border-b", make("h3", "flex", trigger), panel);
  item.dataset.slot = "accordion-item";
  item.reveal = () => show(open, false);
  return item;
}

// Options are in the order the manifest has them, under their groups. One with no group sits above any accordion.
function optionsContent(patch, controls) {
  const options = optionsOf(patch);
  const groups = [];
  for (const option of options) {
    let group = groups.find(entry => entry.title === option.group);
    if (!group) groups.push((group = { title: option.group, options: [] }));
    group.options.push(option);
  }
  const fields = new Map();
  const depend = () => {
    for (const [key, fieldset] of fields) fieldset.disabled = currentValue(patch.id, key) !== "true";
  };
  const send = new Map();
  const box = list => {
    const parts = [];
    for (const option of list) {
      const control = optionControl(patch, option, currentValue(patch.id, option.key), value => changed(option, value));
      controls.set(option.key, control);
      if (!option.when) {
        parts.push(optionRow(option, control));
        continue;
      }
      let fieldset = fields.get(option.when);
      if (!fieldset) {
        fieldset = make("fieldset", "divide-border divide-y disabled:opacity-50");
        fieldset.dataset.when = option.when;
        fields.set(option.when, fieldset);
        parts.push(fieldset);
      }
      fieldset.append(optionRow(option, control));
    }
    return make("div", "border-input divide-border divide-y rounded-lg border", ...parts);
  };
  const changed = (option, value) => {
    const current = patches.find(entry => entry.id === patch.id);
    if (current) current.values = { ...current.values, [option.key]: value };
    if (!send.has(option.key)) send.set(option.key, latestOnly(next => sendOption(patch.id, option.key, next)));
    send.get(option.key)(value);
    depend();
    renderIfChanged();
  };
  const ungrouped = groups.filter(group => !group.title);
  const accordions = groups.filter(group => group.title);
  const items = accordions.map((group, index) => accordionItem(group.title, index === 0, box(group.options)));
  const nodes = [...ungrouped.map(group => box(group.options))];
  if (items.length) {
    const accordion = make("div", "flex w-full flex-col", ...items);
    accordion.dataset.slot = "accordion";
    nodes.push(accordion);
  }
  depend();
  return { nodes, reveal: () => items.forEach(item => item.reveal()) };
}

function currentValue(patchId, key) {
  const patch = patches.find(entry => entry.id === patchId);
  const option = patch && optionsOf(patch).find(entry => entry.key === key);
  return patch?.values?.[key] ?? (option ? fallbackOf(option) : "");
}

// Aside's dialog, from its component. Changes apply as they're made, like the switches on the page.
function openOptions(patchId) {
  const patch = patches.find(entry => entry.id === patchId);
  if (!patch || optionsPopup) return;
  const controls = new Map();
  const titleId = "aside-late-options-title";
  const { nodes, reveal } = optionsContent(patch, controls);
  const title = make("h2", "font-display text-lg leading-snug font-medium", patch.title);
  title.id = titleId;
  title.dataset.slot = "dialog-title";
  const note = patch.needsRestart ? make("p", "text-muted-foreground text-sm", "Changes apply the next time Aside starts.") : null;

  const overlay = make("div", "data-open:animate-in data-open:fade-in-0 data-closed:animate-out data-closed:fade-out-0 fixed inset-0 isolate z-50 bg-black/15 duration-100 supports-backdrop-filter:backdrop-blur-xs");
  overlay.dataset.slot = "dialog-overlay";
  overlay.toggleAttribute("data-open", true);
  const closeIcon = make("button", `${BUTTON} hover:bg-muted hover:text-foreground dark:hover:bg-muted/50 text-secondary-foreground/50 absolute top-2 right-2 size-6 rounded-full border-transparent`, glyph("close", "size-4!"));
  closeIcon.type = "button";
  closeIcon.dataset.slot = "dialog-close";
  closeIcon.setAttribute("aria-label", "Close");

  const reset = button(BUTTON_SECONDARY, "Reset", () => {
    const current = patches.find(entry => entry.id === patchId);
    if (!current) return;
    const values = {};
    for (const option of optionsOf(current)) values[option.key] = fallbackOf(option);
    current.values = values;
    optionsSending++;
    changesSent++;
    ask(`reset/${patchId}`).finally(() => {
      optionsSending--;
      changesSent++;
      followNow();
    });
    syncOptions();
  });
  const done = button(BUTTON_DEFAULT, "Done", () => close());
  const footer = make("div", "flex shrink-0 justify-end gap-1", reset, done);
  footer.dataset.slot = "dialog-footer";

  const content = make("div", "no-drag data-open:animate-in data-open:fade-in-0 data-open:zoom-in-95 data-closed:animate-out data-closed:fade-out-0 data-closed:zoom-out-95 fixed top-1/2 left-1/2 z-50 grid w-full max-w-[calc(100%-2rem)] -translate-x-1/2 -translate-y-1/2 gap-4 text-sm duration-100 outline-none sm:max-w-sm bg-popover border-glass rounded-2xl px-6 py-5 shadow-2xl",
    make("div", "flex max-h-[calc(100dvh-4.5rem)] min-w-0 flex-col gap-4", title, note, make("div", "-mx-1 min-h-0 overflow-y-auto px-1", make("div", "flex w-full flex-col gap-4 py-1", ...nodes)), footer), closeIcon);
  content.dataset.slot = "dialog-content";
  content.toggleAttribute("data-open", true);
  content.setAttribute("role", "dialog");
  content.setAttribute("aria-modal", "true");
  content.setAttribute("aria-labelledby", titleId);
  content.tabIndex = -1;

  const close = () => {
    if (!optionsPopup) return;
    optionsPopup = null;
    for (const control of controls.values()) control.closeList?.();
    for (const node of [overlay, content]) {
      node.removeAttribute("data-open");
      node.toggleAttribute("data-closed", true);
    }
    setTimeout(() => {
      overlay.remove();
      content.remove();
    }, 100);
    document.querySelector(`[data-aside-late-configure="${patchId}"]`)?.focus();
  };
  overlay.addEventListener("click", close);
  closeIcon.addEventListener("click", close);
  content.addEventListener("keydown", event => {
    if (event.key === "Escape") {
      event.preventDefault();
      close();
    } else if (event.key === "Tab") {
      const focusable = [...content.querySelectorAll("button, input")].filter(node => !node.disabled && node.offsetParent !== null);
      const edge = event.shiftKey ? focusable[0] : focusable[focusable.length - 1];
      if (document.activeElement === edge) {
        event.preventDefault();
        (event.shiftKey ? focusable[focusable.length - 1] : focusable[0]).focus();
      }
    }
  });
  document.body.append(overlay, content);
  reveal();
  optionsPopup = { patchId, controls, close };
  content.focus();
}

// Another tab can change the same options, and Reset changes them all.
function syncOptions() {
  if (!optionsPopup || optionsSending || dragging) return;
  const patch = patches.find(entry => entry.id === optionsPopup.patchId);
  if (!patch) return optionsPopup.close();
  for (const [key, control] of optionsPopup.controls) {
    const value = currentValue(patch.id, key);
    if (control.shownValue !== value) {
      control.shownValue = value;
      control.setValue(value);
    }
  }
  for (const fieldset of document.querySelectorAll('[data-slot="dialog-content"] fieldset')) {
    const key = fieldset.dataset.when;
    if (key) fieldset.disabled = currentValue(patch.id, key) !== "true";
  }
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
  syncOptions();
  if (!dragging && stateKey() !== shownState) render();
}

function content() {
  return document.querySelector(NAV)?.closest(".h-dvh")?.querySelector(":scope > .min-w-0.grow");
}

function close() {
  optionsPopup?.close();
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
