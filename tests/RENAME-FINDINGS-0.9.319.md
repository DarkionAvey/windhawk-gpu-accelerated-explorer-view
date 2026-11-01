# Rename follow-up

Live Explorer testing of 0.9.318 still showed filename suffixes in narrow
rename fields and strong blinking during mouse selection. The previous
offscreen assertions did not establish either live behavior.

## Filename prefix

A native-control probe reproduced the single-line selection problem with both
USER32 and themed COMCTL32 EDITs. `EM_SETSEL(end, 0)` followed by
`EM_SCROLLCARET` still moved the actual first character to negative X, despite
`EM_GETFIRSTVISIBLELINE` returning zero. The previous assertion used only that
last result. The multiline EDIT retained the leading caret and visible prefix.

0.9.319 uses the existing native wrapping/expansion path in list views too.
Long names remain accessible without a horizontal-scroll workaround, including
right-aligned RTL names. The original Name-column anchor is revealed before
the field may expand to the viewport width. Tests check actual `EM_POSFROMCHAR`
coordinates and preserved basename selection, with both native implementations.
The strengthened assertion fails against archived 0.9.318.

`tests/run-rename-native-probe.ps1` separately exercises genuine native focus on
a private desktop that is never displayed. It never switches the input desktop
or changes the user's focus, mouse, clipboard or files. This is an OS integration
probe, separate from the mod regressions with substituted focus APIs.

## Blinking: unresolved pending live trace

Two repeatable layout problems are corrected: native formatting-height
normalization no longer causes a fresh EM_SETRECTNP/invalidation on each
unchanged layout, and fields that borrowed the viewport width no longer shrink
and expand again just to remeasure. Text/font notifications invalidate their
measurement; preferred geometry, viewport size and padding form the cache key.
The selection API test now includes owner geometry updates and asserts zero
format/font/position changes for the settled expanded field. These corrections
still need live validation against the reported dragging behavior.

Native EDIT selection can draw directly without one WM_PAINT per selection
change. The standalone API probe observed repeated color requests with no
matching paint/erase messages. That does not identify the source of the live
blank frames. EDIT also has CS_PARENTDC, which prevents applying the documented
WS_EX_COMPOSITED style directly to it. No speculative buffering change is made.

Enable **UI transition diagnostics** in 0.9.319, begin logging, open rename and
drag across several characters until the blinking is visible. Close rename,
stop logging and disable the option. Retain the full capture including
`[GPU pipe] START`, any `DROP` records, and `ui.rename.*` records. Include the
view mode and whether the capture used normal dragging or Shift-click.

The existing bounded per-tab asynchronous writer records:

- `ui.rename.message[phase,depth,focus,capture]`: `a` is the Win32 message;
  `b` is WPARAM, WINDOWPOS flags for position messages, or the native parent
  notification code. Phase 0 enters the subclass, 1 returns from native
  processing, and 2 marks a parent color/text notification. Border paint is
  handled by the subclass, so its entry need not have a native-return record.
- `ui.rename.selection[x,y,firstVisible,lines]`: `a`/`b` are selected character
  bounds; values describe the actual first-character coordinates and native
  text scroll/wrapping.
- `ui.rename.window[left,top,right,bottom]`: screen rectangle, window style in
  `a`, and the preceding sibling HWND in `b`.

Tracing performs no invalidation, drawing-DC acquisition, layout mutation,
timers, Shell/provider calls or synchronous log formatting. These records can
separate native painting from reformatting, focus, placement and sibling-order
changes during the real drag. API selection tests do not emulate mouse capture
or prove the absence of displayed flicker.

References: [EM_SETSEL](https://learn.microsoft.com/en-us/windows/win32/controls/em-setsel),
[EDIT default processing](https://learn.microsoft.com/en-us/windows/win32/controls/about-edit-controls),
[extended window styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles).
