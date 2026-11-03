// The pointing hand on a stick, shared by the thank-you page and the GitHub and X pages it opens. It's a classic script, so it can
// be injected into another page as well as loaded by ours, and all it adds is asideLateHand. It never takes a click.

(() => {
  if (globalThis.asideLateHand) return;

  // Points straight up, with the fingertip at (64, 6) of the drawing. The hand turns it.
  const MARKUP = `<svg width="128" height="1400" viewBox="-64 -6 128 1400">
  <defs>
    <radialGradient id="glove" cx="0.38" cy="0.3" r="0.9">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="0.55" stop-color="#f6f6f8"/>
      <stop offset="1" stop-color="#d6d6de"/>
    </radialGradient>
    <linearGradient id="stick" x1="0" x2="1">
      <stop offset="0" stop-color="#3a3a3e"/>
      <stop offset="0.45" stop-color="#1c1c1f"/>
      <stop offset="1" stop-color="#0d0d0f"/>
    </linearGradient>
  </defs>
  <g stroke="#1d1d21" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">
    <rect x="13" y="132" width="20" height="1300" rx="10" fill="url(#stick)" stroke="none"/>
    <path d="M-3 118 C-6 130 -9 140 -8 147 C-6 156 52 158 56 149 C58 142 54 130 51 118 Z" fill="url(#glove)"/>
    <path d="M-6 133 C10 137 37 137 53 133" fill="none" stroke-width="2.5"/>
    <path d="M0 -1 C7 -1 12 4 13 13 C14 24 14 34 13 44 C16 36 26 37 30 45 C36 39 46 43 48 51
             C56 49 62 57 60 67 C64 77 65 89 62 101 C58 117 42 125 22 125 C4 125 -13 120 -19 110 C-26 100 -28 86 -24 78
             C-21 71 -14 69 -13 60 C-13 44 -13 26 -12 13 C-12 4 -7 -1 0 -1 Z" fill="url(#glove)"/>
    <path d="M13.5 47 C14 51 14 54 12 58" fill="none" stroke-width="2.5"/>
    <path d="M30 45 C32 53 32 59 30 63 M48 51 C50 59 48 65 46 69" fill="none" stroke-width="2.5"/>
    <path d="M-6 6 C-3 3 3 3 6 6" fill="none" stroke="#c8c8d0" stroke-width="2"/>
    <path d="M17 80 L20 103 M30 78 L33 101 M43 76 L46 99" fill="none" stroke="#111" stroke-width="5.5"/>
  </g>
</svg>`;
  const SVG = "http://www.w3.org/2000/svg";
  const TAU = Math.PI * 2;

  class Interrupted extends Error {}

  // A wobbly loop around a box, a bit more than once round.
  function loopAround(box, seed) {
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

  // Puts the hand and its ink into `layer`. The hand leans so its stick comes in from the bottom right.
  function create({ layer, ink = "#1d9bf0", lean = -28, size = 1.2 }) {
    const hand = document.createElement("div");
    hand.setAttribute("aria-hidden", "true");
    Object.assign(hand.style, { position: "fixed", left: "0", top: "0", pointerEvents: "none", willChange: "transform", zIndex: "2147483647" });
    hand.innerHTML = MARKUP;
    Object.assign(hand.firstElementChild.style, { position: "absolute", left: "-64px", top: "-6px", overflow: "visible", filter: "drop-shadow(0 6px 10px rgb(0 0 0 / 0.35))" });

    const paper = document.createElementNS(SVG, "svg");
    paper.setAttribute("aria-hidden", "true");
    Object.assign(paper.style, { position: "fixed", inset: "0", width: "100%", height: "100%", pointerEvents: "none", overflow: "visible", zIndex: "2147483646" });
    const scribble = document.createElementNS(SVG, "path");
    for (const [name, value] of Object.entries({ fill: "none", stroke: ink, "stroke-width": "4", "stroke-linecap": "round", "stroke-linejoin": "round", opacity: "0" })) scribble.setAttribute(name, value);
    scribble.style.transition = "opacity 400ms ease";
    paper.append(scribble);
    layer.append(paper, hand);

    const at = { x: innerWidth + 160, y: innerHeight + 160 };
    const speed = { x: 0, y: 0 };
    let goal = { ...at };
    let tap = 0, shake = 0, stiffness = 140, last = 0, running = false, generation = 0;

    function frame(now) {
      if (!running) return;
      const dt = Math.min((now - last) / 1000, 1 / 30);
      last = now;
      // A spring that's a little underdamped, so the hand overshoots and wobbles like someone's holding it.
      const damping = 2 * Math.sqrt(stiffness) * 0.72;
      for (const axis of ["x", "y"]) {
        speed[axis] += (stiffness * (goal[axis] - at[axis]) - damping * speed[axis]) * dt;
        at[axis] += speed[axis] * dt;
      }
      const swing = Math.max(-14, Math.min(14, speed.x * 0.018 - speed.y * 0.006));
      const nope = shake ? Math.sin(now / 45) * 12 * shake : 0;
      const angle = lean + swing + nope;
      // A tap pushes the finger in along the way it points.
      const rad = (angle * Math.PI) / 180, push = tap * 10;
      const x = at.x + Math.sin(rad) * push, y = at.y - Math.cos(rad) * push;
      hand.style.transform = `translate(${x}px, ${y}px) rotate(${angle}deg) scale(${size * (1 - tap * 0.07)})`;
      requestAnimationFrame(frame);
    }

    const api = {
      Interrupted,
      get generation() { return generation; },
      // Makes every wait that's still pending give up, so whoever is driving the hand can start over.
      interrupt() { generation++; },
      start() {
        if (running) return;
        running = true;
        last = performance.now();
        requestAnimationFrame(frame);
      },
      sleep(ms, mine) {
        return new Promise((resolve, reject) => setTimeout(() => (mine === generation ? resolve() : reject(new Interrupted())), ms));
      },
      // Where to put the fingertip on a button: a little left of centre and below, so the finger doesn't hide the label.
      center(element) {
        const box = element.getBoundingClientRect();
        return { x: box.left + box.width * 0.42, y: box.top + box.height * 0.62 };
      },
      async moveTo(point, mine, ms = 650) {
        stiffness = 140;
        goal = { ...point };
        await api.sleep(ms, mine);
      },
      async leave(mine) {
        await api.moveTo({ x: innerWidth + 200, y: innerHeight + 260 }, mine, 700);
      },
      async tapTap(mine, times = 2) {
        try {
          for (let i = 0; i < times; i++) {
            for (const value of [1, 0]) {
              tap = value;
              await api.sleep(value ? 90 : 130, mine);
            }
          }
        } finally {
          tap = 0;
        }
      },
      async shakeHead(mine, ms = 700) {
        shake = 1;
        try {
          await api.sleep(ms, mine);
        } finally {
          shake = 0;
        }
      },
      // Draws the loop with the fingertip as it goes, so the ink comes out of the finger.
      async circle(element, mine) {
        const points = loopAround(element.getBoundingClientRect(), Math.random());
        scribble.setAttribute("d", points.map((p, i) => `${i ? "L" : "M"}${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(""));
        const length = scribble.getTotalLength();
        scribble.style.transition = "none";
        scribble.style.strokeDasharray = `${length}`;
        scribble.style.strokeDashoffset = `${length}`;
        scribble.setAttribute("opacity", "1");
        await api.moveTo(points[0], mine, 420);
        stiffness = 900;
        const drawing = 820, began = performance.now();
        try {
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
        } finally {
          stiffness = 140;
        }
      },
      fadeInk() {
        scribble.style.transition = "opacity 400ms ease";
        scribble.setAttribute("opacity", "0");
      },
      // For reduced motion: the hand just points, with the loop already drawn.
      pointStill(element) {
        const point = api.center(element);
        hand.style.transform = `translate(${point.x}px, ${point.y}px) rotate(${lean}deg) scale(${size})`;
        const points = loopAround(element.getBoundingClientRect(), 0.3);
        scribble.setAttribute("d", points.map((p, i) => `${i ? "L" : "M"}${p.x} ${p.y}`).join(""));
        scribble.setAttribute("opacity", "1");
      },
      remove() {
        running = false;
        generation++;
        hand.remove();
        paper.remove();
      },
    };
    return api;
  }

  globalThis.asideLateHand = { create, Interrupted };
})();
