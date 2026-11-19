// What the settings page and the new tab page share to find a newer Aside Late on GitHub, fetch it and restart Aside into it. The
// mod does the fetching and the restarting, and says how it's going in update.json.

const HOME = "/aside-late";
const CACHE_KEY = "asideLate.release";
// GitHub only answers so many questions an hour without an account, and every new tab would ask.
const LOOK_EVERY_MS = 6 * 60 * 60 * 1000;

async function read(name) {
  const response = await fetch(`${HOME}/${name}`, { cache: "no-store" });
  if (!response.ok) throw new Error(`${name}: ${response.status}`);
  return response.json();
}

// A request to the mod is the path of a file that doesn't exist. Each one's unique, so it's never cached.
function ask(path) {
  return fetch(`${HOME}/${path}/${Date.now().toString(36)}${Math.random().toString(36).slice(2, 4)}`, { cache: "no-store" });
}

// Like semver: 1.10.0 is newer than 1.9.0, and 0.2.0 is newer than 0.2.0-alpha.3.
export function isNewer(version, than) {
  const split = text => {
    const [core, ...pre] = text.split("-");
    return [core.split(".").map(part => Number(part) || 0), pre.length ? pre.join("-").split(".") : null];
  };
  const [[left, leftPre], [right, rightPre]] = [split(version), split(than)];
  for (let i = 0; i < 3; i++) {
    if ((left[i] ?? 0) !== (right[i] ?? 0)) return (left[i] ?? 0) > (right[i] ?? 0);
  }
  if (!leftPre || !rightPre) return !leftPre && !!rightPre;
  for (let i = 0; i < Math.min(leftPre.length, rightPre.length); i++) {
    const [a, b] = [leftPre[i], rightPre[i]];
    if (a === b) continue;
    const [numberA, numberB] = [/^\d+$/.test(a), /^\d+$/.test(b)];
    if (numberA && numberB) return Number(a) > Number(b);
    if (numberA !== numberB) return numberB;
    return a > b;
  }
  return leftPre.length > rightPre.length;
}

function remembered() {
  try {
    const saved = JSON.parse(localStorage.getItem(CACHE_KEY));
    return saved && Date.now() - saved.at < LOOK_EVERY_MS ? saved : null;
  } catch {
    return null;
  }
}

// The newest release with an exe and its SHA-256, which GitHub lists for the file, or the notes give as a fallback. A
// prerelease counts only for someone already on one.
async function newestRelease(late, github) {
  const response = await fetch(`${github.api}/repos/${late.repo}/releases?per_page=20`, { cache: "no-store" });
  if (!response.ok) throw new Error(`GitHub answered ${response.status}`);
  const takesPrereleases = late.version.includes("-");
  let newest = null;
  for (const release of await response.json()) {
    if (release.draft || (release.prerelease && !takesPrereleases)) continue;
    const version = release.tag_name.replace(/^v/, "");
    const asset = release.assets?.find(asset => asset.name === `AsideLate-${version}.exe`);
    const sha256 = asset?.digest?.match(/^sha256:([0-9a-f]{64})$/)?.[1] ?? release.body?.match(/SHA-256: `([0-9a-f]{64})`/)?.[1];
    if (!asset || !sha256 || (newest && !isNewer(version, newest.version))) continue;
    newest = { version, sha256, notes: release.html_url, size: asset.size };
  }
  return newest;
}

// The newer version, or null. Offline or rate limited, it's null too, and asks again later.
export async function findUpdate() {
  const [late, github] = await Promise.all([read("late.json"), read("github.json")]);
  if (!/^\d/.test(late.version)) return null;
  let saved = remembered();
  if (!saved || saved.from !== late.version) {
    try {
      saved = { at: Date.now(), from: late.version, release: await newestRelease(late, github) };
      localStorage.setItem(CACHE_KEY, JSON.stringify(saved));
    } catch {
      return null;
    }
  }
  const release = saved.release;
  return release && isNewer(release.version, late.version) ? { ...release, current: late.version } : null;
}

// idle, downloading, ready, restarting or failed, with how far a download's got.
export async function updateState() {
  try {
    return await read("update.json");
  } catch {
    return { state: "idle" };
  }
}

export function download(update) {
  return ask(`update/${update.version}/${update.sha256}`);
}

export function restart() {
  return ask("restart");
}

export function percent(state) {
  return state.total ? Math.min(99, Math.floor((state.received / state.total) * 100)) : null;
}
