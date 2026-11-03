// A thank-you shown once, on the first start with the patches. A big pointing hand on a stick keeps circling and tapping the
// buttons, and follows a click to GitHub or X to point at the real Star or Follow there. It's only a joke, so it never blocks a
// click and can be closed straight away. The hand itself is in hand.js.

const star = document.getElementById("star");
const follow = document.getElementById("follow");
const later = document.getElementById("later");
const title = document.getElementById("title");
const about = document.getElementById("about");
const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
const hand = asideLateHand.create({ layer: document.body, ink: getComputedStyle(document.documentElement).getPropertyValue("--ink").trim() });

let starred = false, followed = false, hovered = null;

function nextTarget(previous) {
  if (hovered) return hovered;
  if (starred && followed) return later;
  if (starred) return follow;
  if (followed) return star;
  return previous === star ? follow : star;
}

async function show() {
  const mine = hand.generation;
  let target = null;
  try {
    await hand.sleep(500, mine);
    for (;;) {
      target = nextTarget(target);
      if (target === later && !(starred && followed)) {
        // Hovering "maybe later" gets a disappointed shake.
        const point = hand.center(later);
        await hand.moveTo({ x: point.x + 40, y: point.y + 30 }, mine, 450);
        await hand.shakeHead(mine);
        await hand.sleep(600, mine);
        continue;
      }
      if (target === later && !hovered) {
        // Both done: circle Close, wave goodbye on the way out, tap it, then circle it again, a smaller wave, and tap it again.
        await hand.circle(later, mine);
        await Promise.all([hand.moveTo(hand.waveSpot(later), mine, 380), hand.wave(mine, 1500)]);
        await hand.moveTo(hand.center(later), mine, 380);
        await hand.tapTap(mine);
        hand.fadeInk();
        await hand.sleep(500, mine);
        await hand.circle(later, mine);
        await Promise.all([hand.moveTo(hand.waveSpot(later), mine, 380), hand.wave(mine, 800, 0.55)]);
        await hand.moveTo(hand.center(later), mine, 380);
        await hand.tapTap(mine);
        hand.fadeInk();
        await hand.sleep(1300, mine);
        continue;
      }
      await hand.circle(target, mine);
      await hand.moveTo(hand.center(target), mine, 380);
      await hand.tapTap(mine);
      await hand.sleep(700, mine);
      hand.fadeInk();
      await hand.sleep(hovered ? 300 : 1100, mine);
    }
  } catch (error) {
    if (!(error instanceof hand.Interrupted)) throw error;
    hand.fadeInk();
    show();
  }
}

// Runs inside the GitHub or X tab, after hand.js, so it can only use what's there. Finds the button, points at it a few times,
// and leaves early once it's clicked. On X only the Follow for `handle` counts, since the page is full of other people's, and on
// that account's own profile there's none, so the hand doesn't come at all. Logged out, X draws its profile differently: its
// Follow is labelled just "Follow", which on that account's profile can only be theirs, and bars about cookies and logging in
// cover the bottom of the page, so those are hidden while the hand is there.
function pointInTab(kind, handle) {
  if (globalThis.asideLatePointing) return;
  globalThis.asideLatePointing = true;
  const onProfile = location.pathname.split("/")[1]?.toLowerCase() === handle.toLowerCase();
  const SELECTORS = {
    star: ['[data-testid="star-button"]', '[data-testid="unstar-button"]', '[aria-label^="Star this repository"]', '[aria-label^="Unstar this repository"]'],
    follow: [
      `[data-testid$="-follow"][aria-label$="@${handle}" i]`,
      `[data-testid$="-unfollow"][aria-label$="@${handle}" i]`,
      ...(onProfile ? ['a[aria-label="Follow"]', 'button[aria-label="Follow"]', 'a[aria-label="Following"]', 'button[aria-label="Following"]'] : []),
    ],
  };
  const COVERS = /cookies|continue to x|log in|sign up/i;
  const LABELS = { star: /^(Star|Starred)\b/ };
  const onScreen = element => {
    const box = element.getBoundingClientRect();
    return box.width > 0 && box.height > 0 && box.bottom > 0 && box.top < innerHeight && box.left < innerWidth;
  };
  const find = () => {
    for (const selector of SELECTORS[kind]) {
      const found = [...document.querySelectorAll(selector)].find(onScreen);
      if (found) return found;
    }
    if (!LABELS[kind]) return null;
    return [...document.querySelectorAll("a, button, [role=button]")].find(element => onScreen(element) && LABELS[kind].test(element.textContent.trim()));
  };

  // Bars fixed over the lower part of X's page, put back as they were when the hand leaves.
  const hidden = new Map();
  const uncover = () => {
    if (kind !== "follow") return;
    for (const element of document.querySelectorAll('[data-testid="BottomBar"], #layers > div, aside, div[class*="fixed"]')) {
      if (hidden.has(element) || getComputedStyle(element).position !== "fixed" || !COVERS.test(element.textContent)) continue;
      if (element.getBoundingClientRect().top < innerHeight / 3) continue;
      hidden.set(element, element.style.getPropertyValue("visibility"));
      element.style.setProperty("visibility", "hidden", "important");
    }
  };
  const recover = () => {
    for (const [element, visibility] of hidden) element.style.setProperty("visibility", visibility);
    hidden.clear();
  };

  const host = document.createElement("div");
  Object.assign(host.style, { position: "fixed", inset: "0", pointerEvents: "none", zIndex: "2147483647" });
  const layer = host.attachShadow({ mode: "closed" });
  document.documentElement.append(host);
  const hand = asideLateHand.create({ layer });
  const done = () => {
    hand.remove();
    host.remove();
    recover();
  };

  (async () => {
    // Sites like X draw their buttons a while after the page has loaded.
    let target = null;
    for (let waited = 0; !target && waited < 20000; waited += 300) {
      target = find();
      uncover();
      if (!target) await new Promise(resolve => setTimeout(resolve, 300));
    }
    if (!target) return done();
    if (matchMedia("(prefers-reduced-motion: reduce)").matches) {
      hand.pointStill(target);
      return setTimeout(done, 5000);
    }
    let clicked = false;
    addEventListener("click", event => {
      if (live().contains(event.target)) {
        clicked = true;
        hand.interrupt();
      }
    }, true);
    hand.start();
    // Sites redraw themselves and swap the button for a new copy, so it's looked up again before every move.
    const live = () => {
      if (!target.isConnected || !onScreen(target)) target = find() || target;
      return target;
    };
    let mine = hand.generation;
    try {
      for (let round = 0; round < 3; round++) {
        uncover();
        await hand.circle(live(), mine);
        await hand.moveTo(hand.center(live()), mine, 380);
        await hand.tapTap(mine);
        await hand.sleep(700, mine);
        hand.fadeInk();
        await hand.sleep(900, mine);
      }
    } catch (error) {
      if (!(error instanceof hand.Interrupted)) throw error;
    }
    mine = hand.generation;
    hand.fadeInk();
    if (clicked) await hand.tapTap(mine, 3).catch(() => {});
    await hand.leave(mine).catch(() => {});
    done();
  })();
}

// Opens the page in a tab next to this one, and sends the hand after it once it has loaded.
async function openAndPoint(link, kind) {
  try {
    const here = await chrome.tabs.getCurrent();
    const tab = await chrome.tabs.create({ url: link.href, windowId: here.windowId, index: here.index + 1 });
    const loaded = (id, change) => {
      if (id !== tab.id || change.status !== "complete") return;
      chrome.tabs.onUpdated.removeListener(loaded);
      chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["aside-late/hand.js"] })
        .then(() => chrome.scripting.executeScript({ target: { tabId: tab.id }, func: pointInTab, args: [kind, new URL(link.href).pathname.split("/")[1]] }))
        .catch(() => {});
    };
    chrome.tabs.onUpdated.addListener(loaded);
  } catch {
    window.open(link.href, "_blank", "noopener");
  }
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
    about.textContent = "That means a lot. The other button is also right there, just saying.";
  }
}

for (const [button, kind, mark] of [[star, "star", () => (starred = true)], [follow, "follow", () => (followed = true)]]) {
  button.addEventListener("click", event => {
    // A plain click goes through us, so the hand can follow. Ctrl and middle clicks stay the browser's.
    if (event.button === 0 && !event.ctrlKey && !event.metaKey && !event.shiftKey) {
      event.preventDefault();
      openAndPoint(button, kind);
    }
    mark();
    button.classList.add("done");
    thanked();
    hand.interrupt();
  });
  button.addEventListener("pointerenter", () => {
    hovered = button;
    hand.interrupt();
  });
  button.addEventListener("pointerleave", () => {
    if (hovered === button) hovered = null;
  });
}
later.addEventListener("pointerenter", () => {
  hovered = later;
  hand.interrupt();
});
later.addEventListener("pointerleave", () => {
  if (hovered === later) hovered = null;
});
later.addEventListener("click", close);
addEventListener("keydown", event => {
  if (event.key === "Escape") close();
});

if (still) {
  hand.pointStill(star);
} else {
  // Opened at startup, the tab can sit hidden for a while, so the entrance and the hand wait until someone can see them.
  // Someone moving the mouse over it can see it, whatever the page was told.
  const wake = ["pointermove", "pointerdown", "focus", "keydown"];
  let begun = false;
  const begin = event => {
    if (begun || (document.visibilityState !== "visible" && !wake.includes(event?.type))) return;
    begun = true;
    document.querySelector(".card").classList.add("enter");
    hand.start();
    show();
  };
  document.addEventListener("visibilitychange", begin);
  for (const type of wake) addEventListener(type, begin, { once: true });
  begin();
}
