# Window glass

Makes the window frosted glass: what's behind it shows through, blurred, wherever Aside leaves its window see through, like the
sidebar.

| File | What's in it |
| --- | --- |
| `patch.c` | All of it |

## How it works

Aside asks Windows for Mica, through `DwmSetWindowAttribute` with `DWMWA_SYSTEMBACKDROP_TYPE`, as it makes each window. Mica only
tints the window from the wallpaper. This hooks that call and asks for acrylic instead, which blurs what's behind the window. Aside
loads `dwmapi.dll` only when it first needs it, so the call goes through Aside.dll's delay-load table, and the hook is on its slot
there. Windows already open when it's switched on get acrylic straight away, and Mica back when it's switched off.

## Limits

- There's no setting for how strong the tint or blur is. Windows' acrylic doesn't take one. The older accent blur does, but it
  draws nothing on Aside's window, which draws through DirectComposition with no redirection surface.
- How much of the window is glass is up to how see through Aside paints it. The page itself stays opaque.
