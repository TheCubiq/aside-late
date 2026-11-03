# Window glass

Makes the window glass, the way Windows Terminal does it: what's behind it shows through blurred, wherever Aside leaves its
window see through, like the sidebar, under black or white, with the theme, at the opacity that's set.

| File | What's in it |
| --- | --- |
| `patch.c` | The backdrop, and the colour over it |
| `resolve.c` | Finding the browser's view and how it paints, by the same rules as window-tweaks |

## How it works

- **The blur.** Aside asks Windows for Mica, through `DwmSetWindowAttribute` with `DWMWA_SYSTEMBACKDROP_TYPE`, as it makes each
  window. Mica only tints the window from the wallpaper. This hooks that call and asks for acrylic instead, which blurs what's
  behind the window. Aside loads `dwmapi.dll` only when it first needs it, so the call goes through Aside.dll's delay-load table,
  and the hook is on its slot there. Windows already open when it's switched on get acrylic straight away, and Mica back when
  it's switched off. Acrylic is dark or light with the window, which Aside sets from its theme.
- **The colour over it.** The browser's view lies under the sidebar, the top bar and the page, so its `OnPaint` fills the canvas
  with black in dark mode or white in light, at **Background opacity**, before Aside paints anything of its
  own. That's `cc::PaintCanvas::drawColor`, seven slots after `concat`. Before using it on a kind of canvas the first time, it
  checks the method there records a colour with the mode it's given, and the next one, `clear`, the same with `kSrc`. A canvas
  that doesn't gets no colour. Which theme it is comes from the same `DwmSetWindowAttribute` hook, when Aside sets
  `DWMWA_USE_IMMERSIVE_DARK_MODE`.

## Limits

- How strong the blur is isn't adjustable. Windows' acrylic doesn't take one, and the older accent blur, which does, draws
  nothing on Aside's window. That draws through DirectComposition with no redirection surface.
- The page itself stays opaque.
- Aside's own paint over the glass, like the sidebar's, still shows at 0%.
