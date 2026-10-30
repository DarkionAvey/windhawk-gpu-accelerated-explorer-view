# Rename appearance and visibility

The working source is `0.9.301.cpp`; publication is blocked by an existing
navigation regression. `latest.cpp` remains at 0.9.300.

The rename field uses the tab's cached app palette with exact system colors in
high contrast. Its brush is released after the EDIT HWND is destroyed. A custom
nonclient frame leaves native text editing intact and avoids repeatedly
insetting a multiline EDIT's formatting rectangle. Vertical alignment uses
`GetTextMetricsW` on the selected HFONT and the native wrapped line count.

Rename reveals the row immediately, scrolls the field into view on both axes,
and bounds the field to the current item viewport on text changes and real
window moves/resizes. Reveal calculations precede mapping into the frame's
possibly mirrored coordinates. The presentation model retains selection
feedback while the editor owns input.

## Validation

- The optimized `--rename` suite passed, including fixture cleanup. Coverage
  includes dark/light/high-contrast native paint colors, Arial/Consolas font
  metrics, wrapped names, stable formatting rectangles, both horizontal reveal
  directions, actual resize/move delivery, grouped/ungrouped Details/List/grid,
  LTR/mirrored RTL and 96/192 DPI. GPU readback verifies selection backgrounds.
- The release build linked the Windhawk DLL and passed repository checks and
  the complete DLL-unload suite. Compiled message/COM callback stack frames
  stayed within 8 KiB, with a maximum of 2,248 bytes.
- The first full run stopped at a resize acknowledgement stall. The same
  optimized binary's `--options` suite subsequently passed in isolation.
- The next full run passed the rename checks but stopped at
  `native view confirmation erased cached group headers before revalidation`.
  Its `--navigation` suite reproduced that failure in isolation. An unchanged
  0.9.300 source compiled with the existing navigation harness at `-O0` also
  reproduced the same assertion; the new rename test was excluded from that
  baseline harness because its APIs do not exist in 0.9.300.
  A private read-only binding probe found byte-different PIDLs for the same
  folder that Windows' `ILIsEqual` considered equal. The navigation code was
  left unchanged; resolving its binding-validation policy is separate work.

These are private offscreen component checks, not live Explorer visual or
displayed-frame-rate measurements. Failed release gates did not publish or
prune source archives. Diagnostic logs remain under `tests/` locally.
