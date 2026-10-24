# UI transition capture

For status-bar view-button focus issues, enable **Performance logging**. Version
0.9.195 logs `[GPU focus] VIEW_COMMAND`, `VIEW_COMMIT`, `VIEW_RETURN` and
`VIEW_RETURN_CANCEL`. These show whether status input was recognized, focus at
mode commit, the accepted return target, or the context that canceled it. They
are independent of the high-volume tracing options below.

The diagnostics added in 0.9.177 remain available. Version 0.9.178 repairs the
confirmed highlight layering problem: temporary composition siblings were
inserted in reverse order, placing background fills over item content. See
[layering findings](LAYERING-0.9.178.md). This option remains useful for other
appearance problems and interrupted transitions.

1. Install `latest.cpp` and enable **UI transition diagnostics** in Windhawk.
2. Start capturing Windhawk/DebugView output before entering the child folder.
3. Navigate from the Details parent into the grid child, then back. Let the
   parent settle for a second. Repeat twice, including one child in List as a
   comparison if practical.
4. Disable the option and retain the full capture with `[GPU pipe] START`,
   any `DROP` records and the `op=ui.*` records.

Frame time logging and Detailed pipeline tracing are optional. UI diagnostics
alone uses the existing tab-owned asynchronous writer and bounded event ring.
It does not start a second writer when full tracing is already active. Event
producers try rather than wait for the ring, and losses are reported. Formatting
and output stay off the UI thread. No timer, repaint, Shell/provider query or
GPU pixel readback is introduced. Logging/viewer overhead can still affect a
capture, so keep it short.

The existing [pipeline schema](PIPELINE-TRACING.md#record-schema) applies. Use
producer QPC timestamps, rather than DebugView write times. With UI-only
diagnostics, `f` advances at each existing Paint. With full tracing enabled, its
normal frame IDs are retained. Item identity hashes join records across rows
and reordering; `ui.item` notes contain the corresponding path.

| Records | Captured state |
| --- | --- |
| `ui.navigation.*`, `ui.rows.*`, `ui.native.rebind`, `ui.returnedChild`, `ui.layout.*` | Folder token (`a`), existing pipeline flags (`b`), logical/native view modes, header height and selected index (`v`). Folder path is in `note`. |
| `ui.clipboard`, `ui.opacity.change`, `ui.opacity.retarget` | Clipboard observation or actual effective-alpha retarget. The retarget includes old/new alpha, hidden/Cut reasons and item identity. |
| `ui.surface` | Normal/mode visual addresses; whether the mode root is attached, published, visible or minimized. |
| `ui.native` | Active tab/native HWNDs, native visibility, owned host/viewport counts and whether the destination layout is pinned. |
| `ui.metrics`, `ui.motion`, `ui.column` | Raw native text COLORREF, row/font/icon sizes, scroll, animation QPC origins, header endpoints, row-motion progress and column bounds. Columns are logged at operations, capped at 16. |
| `ui.item[rowOpacity,target,effective,index]` | Row alpha, logical dimming target, actual combined draw alpha and current index. `b` contains raw file attributes in bits 0–31, Cut in bit 32, dot prefix in bit 33 and selected in bit 34. Dimming reasons do not compound. |
| `ui.label.rect`, `ui.icon.rect` | Logical item slots, before text pixel rounding or image aspect fitting. |
| `ui.icon.key`, `ui.icon.draw`, `ui.icon.missing`, `ui.icon.actual` | Requested cache key, chosen bitmap address, opacity, requested/decoded size, association fallback and actual aspect-fitted physical rectangle. |
| `ui.text.raster`, `ui.text.direct`, `ui.text.origin` | Actual filename drawing path, opacity/color, geometry, rounded physical origin and mask address where applicable. |
| `ui.scene.ready/replace/restore/cleared` | Temporary scene construction and retirement, including the switch back to the normal visual. |
| `ui.endDraw`, `ui.submit` | EndDraw/commit/Present HRESULTs, temporary-scene use, pending status and root ownership. |

Paint samples the selected item and the first drawn row for comparison, at
most once per 8 ms, for one second after a relevant operation. Operations
themselves are recorded immediately. Idle periods do not generate paints just
to collect diagnostics. All row work remains bounded by those sampled items.

During a temporary composition scene, item/text coordinates are canonical
surface coordinates. `ui.motion` supplies the logical header and animated
header endpoints; subtract the canonical header and apply the existing 180 ms
cubic at the recorded QPC origin to recover the body translation. A change in
that transform, draw arguments, palette, image/mask identity or scene root can
then be distinguished from a Cut/hidden opacity change.

These records describe submitted drawing state and API results. They do not
read screen pixels, measure scan-out or prove that DWM displayed a given frame.
