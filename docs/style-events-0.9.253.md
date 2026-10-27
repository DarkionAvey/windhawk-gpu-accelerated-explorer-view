# Event-driven style resolution (0.9.253)

The 2.5-second color check in `UpdateDirectory` is removed. Existing tabs reuse
their resolved palettes during navigation, refresh, scrolling and idle directory
maintenance. Initial preparation resolves once; optional button creation uses
the cached palette instead of probing colors again.

Relevant events post one lifetime-checked owner-thread message per tab. Bursts
merge their color/font flags. Frame broadcasts reach all tabs, including those
with no toggle button; direct native-host, overlay and button events use the
same queue. Borrowed WM_SETTINGCHANGE strings are classified synchronously and
are never posted as pointers.

| Event | Work |
| --- | --- |
| Initial attachment | Resolve native/app palette and cache button colors. |
| Mod settings | Queue palette refresh; retain the existing settings update path. |
| WM_THEMECHANGED | Refresh colors and fonts. |
| WM_SYSCOLORCHANGE / WM_DWMCOLORIZATIONCOLORCHANGED | Refresh colors/accent. |
| ImmersiveColorSet, Colors, WindowsThemeElement, high contrast, unspecified global settings | Refresh the affected style properties. |
| Font collection, font metrics or smoothing | Refresh fonts without color checks. |
| Other named settings, folder jobs and timers | No color checks. |

The [DWM accent notification](https://learn.microsoft.com/en-us/windows/win32/dwm/wm-dwmcolorizationcolorchanged)
targets top-level windows, so it must be forwarded by the frame rather than
depending on a child control. [WM_SETTINGCHANGE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-settingchange)
can carry a temporary area string or null; only classified flags survive the handler.

`--styles` counts actual theme, app-palette, system-color and accent API calls
across six private directory jobs and 48 maintenance passes. It also checks
coalesced frame/overlay events, all tabs with buttons disabled, dark-mode string
lifetime, mod settings, unrelated/font events and stale notifications. Windows
settings, clipboard and focus are unchanged. These are component tests.

Validation: `build.bat -SourceFile 0.9.253.cpp` passed the complete repository,
Windhawk DLL link/stack, C++ pipeline and DLL-unload suites and finalized 0.9.253.
