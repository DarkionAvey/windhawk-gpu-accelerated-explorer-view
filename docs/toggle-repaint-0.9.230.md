# Switch repaint correction, 0.9.230

Live feedback on 0.9.229 reported a missing switch on a new tab, stale hover
decoration and accumulated pixels after clicks. The previous isolated test drew
on a fresh bitmap against a cooperative patterned parent. It did not model a
frame that acknowledges background requests without filling the child's area,
or repeated redraws onto the same destination.

The switch is a child of Explorer's frame, rather than the native status chrome.
Its old paint copied destination pixels before requesting the parent backdrop.
Pixels not repainted by that request could preserve previous hover and icon
output. `WS_EX_TRANSPARENT` does not provide per-pixel alpha compositing: it
controls painting order relative to same-thread siblings. See Microsoft's
[extended-style documentation](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles#ws_ex_transparent)
and [parent-background contract](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-drawthemeparentbackgroundex).

Use the user's accepted solid-background fallback. Fill the entire client with
the existing status-chrome palette, then draw hover/press/focus decoration and
the cached native icon. The palette follows Windows app colors, with exact
system colors in high contrast. Buffered GDI output remains opaque for the
compositor. Direct GDI fallback also fills every pixel. No copying of old button
pixels or dependency on parent printing remains. Remove `WS_EX_TRANSPARENT`,
retain the native BUTTON interaction, and add `WS_CLIPSIBLINGS`. Invalidate after
show/placement so a new tab does not wait for pointer interaction to repaint.
Explorer extensions that paint private custom chrome may still use other colors.

Mouse moves now test the client bounds, including moves delivered outside while
the native button holds capture. Reconcile hover against the current cursor on
release/capture changes, and clear it for cancellation, hiding/showing and
disabling. No timer, polling loop, extra worker or alternate input route is added.

Remove the sharper grayscale dropdown item and renderer branch. A saved `sharp`
value resolves to Windows default through the ordinary unsupported-value fallback.
Transparent DirectWrite text, smooth grayscale and aliased choices are retained.

Regression fixtures reuse the same pixels through repeated idle/hover/pressed
cycles, start with deliberately undefined destination contents, and model a
parent that does not draw. They exercise buffered output and direct GDI fallback.
The attachment fixture checks real BUTTON print/state messages with light/dark
icons, captured out-of-bounds moves, capture loss, cancel and hide transitions.
Cursor positions use the private substituted API; no desktop pointer movement,
activation, clipboard changes or user folders are involved.

The focused `--render-quality` run and the complete
`build.bat -SourceFile 0.9.230.cpp` release gate pass. The full gate links the
Windhawk DLL, checks repository/export publication and all 34 option contracts,
and runs the complete C++/offscreen graphics/input and real DLL-unload suites,
including 21 API-hook unload cycles. Existing MacType injection still overrides
native text choices; direct and cached effective policies agree. The removed
choice's persisted-value fallback passes the settings contract.

The initial full run stopped at the existing sort-details cache assertion,
before attachment tests. The retained executable then passed the entire suite
without changes to the mod or sorting test, and the subsequent mandatory build
passed too. Its cause was not isolated. Keep the initial diagnostic log rather
than treating that first run as successful: `tests/build-0.9.230.log`. Successful
runs are `tests/pipeline-0.9.230-diagnostic.log`,
`tests/render-quality-0.9.230.log` and `tests/build-0.9.230-retry.log`.

SHA-256 checks confirm latest.cpp, gpu-accelerated-explorer-view.wh.cpp and
previous-versions/0.9.230.cpp are identical; all 33 earlier archives are unchanged.
These are private offscreen checks; live Explorer visual behavior has not been
manually verified during this revision.
