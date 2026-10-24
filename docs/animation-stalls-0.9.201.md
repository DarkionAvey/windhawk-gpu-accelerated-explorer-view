# Directory-switch column stalls and wider animation audit

## What the supplied capture establishes

The capture contains 15,307 lines, including 15,143 pipeline events and 31
trace-drop notices. Its source line references match the 0.9.200 implementation.
Dropped records prevent a complete reconstruction of every operation, but four
unambiguous interruptions are sufficient to identify this failure mode:

| Consecutive CPU frames during column motion | Native DirectUI message | Native dispatch span, QPC ms | Frame-begin gap, QPC ms |
| --- | --- | ---: | ---: |
| 672 → 673 | `0x4C8` | 37.59 | 47.43 |
| 727 → 728 | `0x4C8` | 38.85 | 46.97 |
| 782 → 783 | `0x4C8` | 34.34 | 43.98 |
| 841 → 842 | `0x4C8` | 35.08 | 43.93 |

The ordinary frame flags include active column motion (`0x40`). The matching
frame summaries report these gaps, and the tick-based stall messages report
31/47 ms waits. The display is reported as 165 Hz: one refresh interval is
about 6.06 ms. The columns previously needed each owner-thread paint to move;
blocking that thread therefore froze their last submitted position and then
jumped ahead on the next paint.

Rendering itself is much shorter than these gaps: representative affected
windows average roughly 0.27–0.29 ms drawing and 0.35–0.37 ms flushing. Present
can consume several milliseconds, but the large interruption coincides with
native message dispatch rather than a long fit/property/draw operation.
Longer native navigation messages also precede the new destination's frames;
they are separate from the interruptions within a running column slide.

There are no `columns.composition` records in this capture. The old compositor
path applied to Details/List/grid **mode** transitions, while ordinary width
refits were advanced and submitted by the Explorer STA. These findings support
the reported symptom without requiring a replacement capture. They do not
measure monitor scan-out or prove an exact count of displayed dropped frames.

## Fix in 0.9.201

Extend the existing per-tab DirectComposition path to ordinary column fitting,
including the fit triggered after a directory switch. The same absolute QPC
origin and cubic drive compositor position, width clips and alpha, and the
existing CPU geometry used for pointer input and interruption. Once committed,
the compositor can keep sampling the slide while the owner STA is occupied.

Widths can overlap during reflow. Each column therefore has a bounded,
transparent source painted by the existing renderer; it cannot reveal a
neighbor's glyphs through an expanding clip. Header/body clips share the column
motion, and dividers/sort chevrons retain their own trailing-edge anchors.
Backgrounds and scrollbars retain their existing screen geometry. Text is not
stretched. Metadata updates and vertical scrolling can repaint sources without
restarting the committed curves. Settlement and replacement use the existing
accepted-frame handoff and device/tab lifetime rules.

The ordinary renderer remains the fallback for unsupported combinations:
active pointer manipulation, spring reordering, outgoing removed columns,
filename-only highlights in Details and This PC's width-dependent capacity bars,
concurrent structural/zoom/horizontal motion, oversized source widths, excessive
source allocation, and unavailable composition resources. Normal viewport-fitting
column sets fit within the surface budget. Resource/size rejection is remembered
for that transition and viewport, avoiding repeated setup/allocation on every
frame. A new transition, viewport size or device recovery permits another try.
Fallback does not reconnect an old swap-chain image before replacement pixels
are ready.

## Wider audit requested after the column finding

GPU-accelerated drawing does not imply compositor-owned animation. The following
paths were checked for the same owner-thread dependency:

| Animation | Advancement/render path | Can the same STA blockage freeze its pixels? |
| --- | --- | --- |
| Eligible ordinary column fitting | Shared DirectComposition column sources, position/clip/alpha curves; CPU hit geometry catches up with `AdvanceColumnTransition` | The committed column curves continue. Uncommitted/new source data still needs the owner. |
| Eligible Details/List/grid header/body mode handoff | Existing `PrepareModeComposition` curves and surfaces | The committed mode curves continue. This does not make general grid reflow autonomous. |
| Wheel, keyboard/history and smooth vertical thumb scrolling | `MoveScroll` / `AdvanceScroll`, followed by ordinary painting | Yes. The elapsed clock catches up afterward; missing paints are not recovered as displayed intermediate frames. |
| Horizontal track easing | `AdvanceHorizontalTrack` and ordinary painting | Yes. Direct horizontal thumb dragging also needs pointer input dispatch. |
| Touchpad contact/inertia and pinch | Windows computes DirectManipulation input; the tab consumes status/deltas and updates its rendered viewport on the owner STA | Yes. OS-owned inertia does not independently move this mod's content surfaces. |
| Grid row-height zoom, cell repacking and structural insertion/deletion/reorder | `AdvanceRowHeight`, shared row/group geometry and `LayoutRowVisual` / `Paint` | Yes. Zoom/reflow disables the temporary column compositor when the source geometry is incompatible. |
| Group collapse/expand, sticky heading movement and trailing zebra extent | `AdvanceAnimationFrame`, `LayoutGroupSections`, row/zebra paint | Yes. Geometry remains time-based and concurrent input is preserved, but it needs new paints. |
| Keyboard selection movement, hover, selection fade and focus feedback | Shared elapsed-time rectangle/opacity sampling and `FillSelectionRect` | Yes. These effects are not covered by the column curves. |
| Cut/hidden opacity transitions, clipboard Copy/Cut icon wiggle | `ItemContentOpacity`, `ClipboardIconWiggleAngle` and icon drawing during `Paint` | Yes. Clipboard state remains reactive; its visual motion still needs owner-thread frames. |
| Scrollbar hover/press fades, marquee/swipe feedback | Existing paint-time curves and feedback drawing | Yes. Gesture completion remains based on actual release/status, independently of this presentation limitation. |
| Column fallback combinations listed above | Ordinary shared column renderer | Yes. They retain correct geometry/input and bounded resource use, but no independent slide guarantee. |

The capture does **not** establish that these other effects stuttered in the
user's session. It establishes column stalls; the table records the same
architectural exposure elsewhere. This release fixes the column case and the
shared repeated-setup failure path. It does not claim to offload all other
animations or eliminate native Explorer's blocking work.

## Diagnostics and regression evidence

- `columns.motion.start` records the real motion origin and mode/spring kind.
- `columns.composition.ready` uses `b=1` for fitting and `b=0` for a mode scene.
  Commit records preserve that distinction in bit 1. Perf/UI diagnostics also
  label the committed scene `kind=fit` or `kind=mode`.
- `columns.composition.fallback` reports a rejected setup once per transition
  and viewport, rather than silently reattempting resource allocation per paint.
- Existing per-frame flags now include hover (16), selection movement (17),
  selection fade (18), zebra extent (19), item opacity (20), clipboard wiggle
  (21), marquee fade (22), swipe fade (23), scrollbar fade (24), grouping (25),
  a committed DWM scene (26), mode transition (27), spring columns (28), and a
  committed fitting scene (29). Existing bits 0–15 retain their meanings.
  Frame-time summaries include the OR of observed flags as `activity=0x...`.
  A summary containing a DWM scene bit still measures **CPU submissions**, not
  compositor/display FPS; its OR mask can cover overlapping kinds of activity.

`tests/column-fit-composition-smoke.h` exercises the actual fitter and real
offscreen GPU surfaces in LTR/RTL. It checks header/body/edge pixels, independent
neighbor sources, scoped transforms, retargeting, concurrent vertical scrolling,
bounded fallback and plane retirement. It commits a real compositor scene,
leaves the test owner thread entirely unpumped for 65 ms, verifies advancement
of the DWM clock with unchanged committed origin/CPU pose, then verifies CPU
geometry catch-up. The timing-readiness suite separately checks that a
completion-driven automatic fit publishes this scene before settlement.

These are component/contract regressions, not a recording of live Explorer or
an independent measurement of displayed animation frames. The release gate and
results are recorded in `CHANGELOG.md`. If another live artifact remains, the
existing Frame time logging plus UI transition diagnostics now identify which
path was running; high-volume pipeline tracing is optional unless native
message/operation correlation is needed.
