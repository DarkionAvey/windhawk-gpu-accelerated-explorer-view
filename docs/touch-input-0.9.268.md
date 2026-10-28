# Marquee visibility and pinch commitment, 0.9.268

## Evidence and missed coverage

The supplied marquee log contains ten touch DOWN records and ten accepted
marquee releases in a seven-item directory. Captured coordinates change and
rendering continues. Focus records include `InputSiteWindowClass` and
`DirectUIHWND`, distinct from the recorded native list HWND. The log does not
include their parent hierarchy, so that alone cannot establish focus ownership.
It does establish that input/selection completion was not the missing step.

The touch path called `PrepareMarquee()` directly, skipping the mouse press's
focus transfer. Painting intentionally hides marquee/selection feedback without
list focus. A native-recognizer reproduction starting with a sibling chrome
control focused selected items but produced no marquee pixels through the actual
presentation model and GPU paint path. It failed before the source change and
passes afterward. Separately, zero-width/height bounds were rejected by drawing
even though the selection geometry accepts a vertical crossing in Details.

Earlier touch tests verified capture and Shell selection but did not paint the
result, and their focus fixture started on the list. Earlier pinch tests also
expected recognition movement to change size immediately. Those assertions
encoded the wrong commitment behavior rather than detecting its visual jump.

## Changes

Touch marquee uses the existing guarded `ReturnListInputFocus()` handoff before
selection/capture begins. It retains native-list preference, GPU fallback,
foreground/modal/editor/tab guards and reentrancy checks. Drawing still follows
actual input focus; no focus predicate is bypassed. Pure vertical/horizontal
marquees use the shared snapped stroke path, preserving exact selection bounds.

The original two-contact reference geometry is now used only for intent.
On pinch commitment, capture the current span and midpoint and initialize the
existing shared zoom path without applying scale. Only subsequent span changes
scale the view, using current span / last applied span. The existing fractional
row/grid metrics, item anchoring, clamps and gesture exclusivity are retained.

The contact-replacement reproduction also observed a native cumulative scale
change from 1.0 to about 1.15024 while measured span changed from 135 to 141
pixels (about 4.44%). Using that native value as a stable post-commit baseline
would replay old scale on a later frame. Touchscreen zoom now follows its tracked
pair directly; Ninput continues to provide recognition, taps, holding, pan and
inertia. Touchpad DirectManipulation keeps its existing path.

## Verification

GPU readback tests cover list/grid, LTR/RTL, first marquee before any list click,
straight vertical bounds, empty-content pixels and suppression after foreground
loss. They use real production model capture/application and painting. Gradual
inward/outward pinch checks require unchanged row/icon/font/scroll metrics and
identical content pixels at commitment, followed by proportional fractional
metrics and changed pixels after further one-pixel contact movements.

The wider native suite retains DPI, grouping, staggered fingers, rotation,
contact replacement, column/history edges, taps, holding/dragging, capture loss
and independent contexts. No Windows input messages or recognizer outputs are
manufactured, and no desktop input is injected. These are native API/GPU component
checks, not touchscreen-driver delivery or displayed-frame validation; this
development host has no physical touchscreen.

The independent gradual-pinch regression also fails against archived 0.9.267
at the first visible size change during recognition. The full
`build.bat -SourceFile 0.9.268.cpp` gate passed, including installed Windhawk
linking, GPU/native-input checks, repository/full C++ regressions and actual DLL
unload. Published and archived source hashes match; retained historical source
bytes are unchanged.
