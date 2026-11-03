// A thank-you shown once, on the first start with the patches. A big pointing hand on a stick keeps circling and tapping the
// buttons. It's only a joke, so it never blocks a click and can be closed straight away.

const hand = document.getElementById("hand");
const scribble = document.getElementById("scribble");
const star = document.getElementById("star");
const follow = document.getElementById("follow");
const later = document.getElementById("later");
const title = document.getElementById("title");
const about = document.getElementById("about");
const still = matchMedia("(prefers-reduced-motion: reduce)").matches;

// The hand leans so its stick comes in from the bottom right.
const LEAN = -28;
const SIZE = 1.2;
const TAU = Math.PI * 2;

const at = { x: innerWidth + 160, y: innerHeight + 160 };
const speed = { x: 0, y: 0 };
let goal = { ...at };
let tap = 0;
let shake = 0;
let stiffness = 140;
let last = performance.now();

function frame(now) {
  const dt = Math.min((now - last) / 1000, 1 / 30);
  last = now;
  // A spring that's a little underdamped, so the hand overshoots and wobbles like it's held by someone.
  const damping = 2 * Math.sqrt(stiffness) * 0.72;
  for (const axis of ["x", "y"]) {
    speed[axis] += (stiffness * (goal[axis] - at[axis]) - damping * speed[axis]) * dt;
    at[axis] += speed[axis] * dt;
  }
  const swing = Math.max(-14, Math.min(14, speed.x * 0.018 - speed.y * 0.006));
  const nope = shake ? Math.sin(now / 45) * 12 * shake : 0;
  const angle = LEAN + swing + nope;
  // A tap pushes the finger in along the way it points.
  const push = tap * 10;
  const rad = (angle * Math.PI) / 180;
  const x = at.x + Math.sin(rad) * push;
  const y = at.y - Math.cos(rad) * push;
  hand.style.transform = `translate(${x}px, ${y}px) rotate(${angle}deg) scale(${SIZE * (1 - tap * 0.07)})`;
  requestAnimationFrame(frame);
}

class Interrupted extends Error {}
let generation = 0;

function sleep(ms, mine) {
  return new Promise((resolve, reject) => {
    setTimeout(() => (mine === generation ? resolve() : reject(new Interrupted())), ms);
  });
}

function center(element) {
  const box = element.getBoundingClientRect();
  return { x: box.left + box.width * 0.42, y: box.top + box.height * 0.62 };
}

async function moveTo(point, mine, ms = 650) {
  stiffness = 140;
  goal = { ...point };
  await sleep(ms, mine);
}

async function tapTap(mine, times = 2) {
  for (let i = 0; i < times; i++) {
    for (const value of [1, 0]) {
      tap = value;
      await sleep(value ? 90 : 130, mine);
    }
  }
}

// A wobbly loop around the button, a bit more than once round, drawn by the fingertip as it goes.
function loopAround(element, seed) {
  const box = element.getBoundingClientRect();
  const cx = box.left + box.width / 2, cy = box.top + box.height / 2;
  const rx = box.width / 2 + 16, ry = box.height / 2 + 14;
  const turns = 1.18, steps = 64, start = -2.3 + seed;
  const points = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const theta = start + t * turns * TAU;
    const wobble = 1 + Math.sin(theta * 3 + seed * 5) * 0.05 + t * 0.07;
    points.push({ x: cx + Math.cos(theta) * rx * wobble, y: cy + Math.sin(theta) * ry * wobble - t * 4 });
  }
  return points;
}

async function circle(element, mine) {
  const points = loopAround(element, Math.random());
  scribble.setAttribute("d", points.map((p, i) => `${i ? "L" : "M"}${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(""));
  const length = scribble.getTotalLength();
  scribble.style.transition = "none";
  scribble.style.strokeDasharray = `${length}`;
  scribble.style.strokeDashoffset = `${length}`;
  scribble.setAttribute("opacity", "1");
  await moveTo(points[0], mine, 420);
  // Following the line closely, so the ink comes out of the finger.
  stiffness = 900;
  const drawing = 820;
  const began = performance.now();
  await new Promise((resolve, reject) => {
    const step = now => {
      if (mine !== generation) return reject(new Interrupted());
      const t = Math.min((now - began) / drawing, 1);
      const eased = t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2;
      const index = eased * (points.length - 1);
      const a = points[Math.floor(index)], b = points[Math.min(Math.ceil(index), points.length - 1)], f = index % 1;
      goal = { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f };
      scribble.style.strokeDashoffset = `${length * (1 - eased)}`;
      if (t < 1) requestAnimationFrame(step);
      else resolve();
    };
    requestAnimationFrame(step);
  });
  stiffness = 140;
}

function fadeInk() {
  scribble.style.transition = "";
  scribble.setAttribute("opacity", "0");
}

let starred = false, followed = false, hovered = null;

function nextTarget(previous) {
  if (hovered) return hovered;
  if (starred && followed) return later;
  if (starred) return follow;
  if (followed) return star;
  return previous === star ? follow : star;
}

async function show() {
  const mine = generation;
  let target = null;
  try {
    await sleep(500, mine);
    for (;;) {
      target = nextTarget(target);
      if (target === later && !(starred && followed)) {
        // Hovering "maybe later" gets a disappointed shake.
        await moveTo({ x: center(later).x + 40, y: center(later).y + 30 }, mine, 450);
        shake = 1;
        await sleep(700, mine);
        shake = 0;
        await sleep(600, mine);
        continue;
      }
      await circle(target, mine);
      await moveTo(center(target), mine, 380);
      await tapTap(mine);
      await sleep(700, mine);
      fadeInk();
      await sleep(hovered ? 300 : 1100, mine);
    }
  } catch (error) {
    if (!(error instanceof Interrupted)) throw error;
    shake = 0;
    tap = 0;
    fadeInk();
    show();
  }
}

function restart() {
  generation++;
}

// The last tab in a window becomes a new tab instead, since closing it would close the window, and on a first start that's all of Aside.
async function close() {
  try {
    const tab = await chrome.tabs.getCurrent();
    const others = await chrome.tabs.query({ windowId: tab.windowId });
    if (others.length > 1) await chrome.tabs.remove(tab.id);
    else await chrome.tabs.update(tab.id, { url: "chrome://newtab/" });
  } catch {
    window.close();
  }
}

function thanked() {
  if (starred && followed) {
    title.textContent = "ok you're officially the best";
    about.textContent = "Thank you, really. You can close this now. The hand will miss you.";
    later.textContent = "Close";
  } else {
    title.textContent = "ahh, thank you!!";
    about.textContent = `That means a lot. The other button is also right there, just saying.`;
  }
}

for (const [button, mark] of [[star, () => (starred = true)], [follow, () => (followed = true)]]) {
  button.addEventListener("click", () => {
    mark();
    button.classList.add("done");
    thanked();
    restart();
  });
  button.addEventListener("pointerenter", () => {
    hovered = button;
    restart();
  });
  button.addEventListener("pointerleave", () => {
    if (hovered === button) hovered = null;
  });
}
later.addEventListener("pointerenter", () => {
  hovered = later;
  restart();
});
later.addEventListener("pointerleave", () => {
  if (hovered === later) hovered = null;
});
later.addEventListener("click", close);
addEventListener("keydown", event => {
  if (event.key === "Escape") close();
});

if (still) {
  // No motion: the hand just points at the star, with the loop already drawn.
  const point = center(star);
  hand.style.transform = `translate(${point.x}px, ${point.y}px) rotate(${LEAN}deg) scale(${SIZE})`;
  const points = loopAround(star, 0.3);
  scribble.setAttribute("d", points.map((p, i) => `${i ? "L" : "M"}${p.x} ${p.y}`).join(""));
  scribble.setAttribute("opacity", "1");
} else {
  // Opened at startup, the tab can sit hidden for a while, so the entrance and the hand wait until someone can see them.
  // Someone moving the mouse over it can see it, whatever the page was told.
  const wake = ["pointermove", "pointerdown", "focus", "keydown"];
  let begun = false;
  const begin = event => {
    if (begun || (document.visibilityState !== "visible" && !wake.includes(event?.type))) return;
    begun = true;
    document.querySelector(".card").classList.add("enter");
    last = performance.now();
    requestAnimationFrame(frame);
    show();
  };
  document.addEventListener("visibilitychange", begin);
  for (const type of wake) addEventListener(type, begin, { once: true });
  begin();
}
