# Contributing

Keep it small, test it hard, show it working.

## Pull requests

- One change per PR.
- Fill in the template: What, Why, Proof, Tested on, Checklist. Short sentences.
- Never leave a section empty or delete it. If one doesn't apply, write `N/A` in it, and tick a checklist item that doesn't apply with `(N/A)` after it. Proof is never N/A for a change you can see in Aside.
- Proof is required for anything visible in Aside: a screenshot for static changes, a screen recording for anything that animates. Show before and after when you change existing behaviour.
- Try to break what you made before you open the PR, and fix what you find.
- Build with `make` and run it on your own Aside with `.\build\aside-late.exe`.

## Adding a patch

A patch is one folder in `patches/`. [Patches](README.md#patches) in the README covers its files, the SDK and its options.

- Store no offsets. Find everything by name or shape in whatever build is loaded, and return what's missing from `PATCH_START` so the patch refuses and the others carry on.
- Prefer classes and methods to code patterns. They're what survives a rebuild of Aside.
- Turning it off has to put everything back, while the browser runs.
- List the builds you ran it on under `tested` in `patch.json`.
- Add what it does and how it works to the README.

## Testing

Test in a profile of its own, so a bad patch can't break your real browser:

```powershell
$env:ASIDE_LATE_HOME = "$env:TEMP\aside-late-home"
.\build\aside-late.exe --user-data-dir="$env:TEMP\aside-late-profile"
```

`ASIDE_LATE_HOME` moves what aside-late keeps, including `mod.log`, which is the first place to look when something goes wrong. `--scan <Aside.dll>` shows whether each patch finds what it needs, without running anything.

Windows swallows exceptions in `WM_TIMER` callbacks, so code on a timer can fail with nothing in `mod.log` and no crash.

## Commits

- An emoji and a short sentence: `📂 Add New folder to a folder's menu`.
- Imperative, under 72 characters. Add a body only if the diff would mislead without it.
- Documentation goes in its own commit, starting with `📝`.
- No tool or AI attribution trailers (`Co-authored-by: Claude`, `Generated with …`).

## AI tools

Use whatever tools you like. You're responsible for what you submit: it has to work, and you have to have tested it yourself.

No slop: no padded descriptions, generated summaries or filler ("comprehensive", "robust", "seamless", "this PR introduces…"). PRs that read like unchecked AI output are closed without review.

## Never

- Commit Aside itself or anything from its installation.
- Change a file of Aside's on disk.
