# Toggle placement during boundary feedback, 0.9.270

The supplied recording shows the custom switch changing its horizontal
separation from the native status buttons during overscroll, then settling.
Placement cached native control rectangles and the footer query in screen
coordinates. Frame position messages also invalidated all measurements. A frame
translation therefore discarded a valid anchor, queued another accessibility
measurement and temporarily used estimated fallback placement. The existing
tests covered resizing and repeated queries at one screen position, not moves.
A private Windows probe confirmed that boundary feedback started on a child
translated both the child and root frame by 12 physical pixels, then restored
both through `EndPanningFeedback`.

Status discovery, accessibility results, HWND validation and placement now share
physical frame-client coordinates. Mirrored-parent positioning is applied once,
when assigning the final child location. Pure frame position messages still
queue visibility/placement updates but retain measurements. Real size, DPI,
theme and status-control changes retain their existing invalidation paths.
This also preserves pending provider results through a move and avoids cold
accessibility work on each overscroll sample. No settling timer or touch-only
placement exception is added; native overscroll behavior stays unchanged.

The native-window regression failed before the change with `frame translation
invalidated status layout geometry`. Checks use actual `SetWindowPos` moves,
the real toolbar accessibility provider and Windows' boundary-feedback APIs
when enabled. They verify stable cache generations, native-peer separation,
pending/warm measurements, resized/stale results and hidden/restored controls.
The attached-view tests verify actual toggle HWND placement in LTR and mirrored
hosts. These are private offscreen/native API checks, not a physical touchscreen
or displayed-frame validation. Microsoft documents window displacement in
[UpdatePanningFeedback](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-updatepanningfeedback).
