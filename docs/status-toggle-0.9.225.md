# Native status-button measurements

The initial read-only desktop probe found virtual DirectUI radio buttons under
`ViewButtonsGroup`, not individual `Button` HWNDs:

| Control | Initial physical bounds | Size |
| --- | --- | --- |
| Overlay toggle | 1576,1558–1616,1598 | 40×40 |
| Native Details | 1614,1557–1655,1598 | 41×41 |
| Native Large Icons | 1655,1557–1696,1598 | 41×41 |

The toggle overlapped Details by two pixels and sat one pixel lower. Its old
one-time HWND-only lookup missed both native controls and used a 32-DIP size
and fixed trailing reservation. Applying another DPI estimate could not copy
the native template's actual pixel dimensions or peer spacing.

## Placement and ownership

Discovery first checks native child windows. When chrome uses virtual controls,
the tab's existing Columns lane queries only the footer's accessibility
containers and publishes ordinary rectangles. No apartment-owned element
escapes the worker, and no accessibility provider call runs in paint/input/
resize handlers. There is no new worker lane or polling timer.

Placement copies the innermost trailing control's width, height and Y coordinate
and the gap between its native peers. A zero peer gap remains zero. Physical
placement is mapped into the frame's coordinates, including mirrored parents.
The accessibility worker explicitly uses per-monitor DPI awareness: the real
toolbar regression caught legacy proxies returning 80%-scaled rectangles on
an unaware worker at 125% density.

Each tab keeps its own snapshot and cancellation generation. Resize/move,
DPI/theme/settings and native-control visibility/geometry events invalidate
discovery. Frame changes invalidate inactive tabs without scheduling their
provider work until they return. A result from an obsolete resize or lifetime
cannot replace current geometry. Queue contention posts a coalesced retry;
completion does not depend on an idle timer or another user action.

When native controls are hidden or unavailable, or while the first measurement
is pending, an estimated fallback keeps the switch available. Performance
logging identifies `HWND`, `accessibility` or `fallback` placement and records
the measured bounds and discovery outcome. Queries are capped at eight HWND
roots, 64 accessibility elements, four container levels and 16 buttons.
Incomplete native viewport geometry uses a bounded footer search rather than
exposing directory descendants to that search.

## Validation

`tests/status-button-smoke.h` checks physical sizes and actual peer gaps at
96/120/144/192 DPI in LTR/RTL, a real private toolbar whose buttons have no HWNDs,
worker-gated/coalesced resizing, cached reuse, hidden/restored controls, queue
contention and incomplete content bounds. Existing attachment tests verify
native HWND button sizing, painter/input behavior and mirrored placement.
The option contract now expects the 42% grid default.

`tests/run-statusbar-probe.ps1 -SourceFile <source>` invokes the production
measurement and placement functions read-only against visible Explorer chrome.
Both working versions found the user's actual virtual buttons at 120 DPI,
with `source=accessibility`, two buttons and `gap=0`. The final 0.9.226 probe,
after the window moved, reported:

```
NATIVE   4927,1265,4968,1306
NATIVE   4968,1265,5009,1306
PROPOSED 4886,1265,4927,1306 size=41x41
```

This validates the production discovery and calculated placement on this PC;
the probe does not install the mod or reposition its live button. Local
captures are in `tests/live-statusbar-225.log`,
`tests/live-status-production-225.log` and
`tests/live-status-production-226.log`.

The complete 0.9.225 and 0.9.226 release gates passed: repository checks,
Windhawk DLL linking, all 33 option contracts, C++/offscreen graphics/input and
21 real DLL-unload cycles. The final gate includes both queue-contention and
partial-viewport tests. Critical callback stacks remain below 8 KiB (maximum
1,960 bytes). latest.cpp is identical to the tested 0.9.226 archive, and all
29 prior archives are unchanged. Full output is in `tests/build-0.9.226.log`.
Offscreen GPU tests and live geometry inspection do not measure Explorer's
displayed frame rate.
