# Large tile drawing and backpressure (0.9.239)

The supplied trace records a 2506 by 1486 viewport, a 599.8-pixel row height,
12 entries and a 120 Hz refresh hint. All 91 captured `render.present` records
return `0x887A000A` (`DXGI_ERROR_WAS_STILL_DRAWING`). Frame summaries also record
419–733 successful CPU submissions per second in some bursts. These are
submission statistics, not displayed frame rates. The trace drops records, so
it cannot establish the outcome of every submitted frame or GPU execution time.
It also records a 234 ms native timer dispatch. The trace therefore does not
prove that every perceived stall originates in GPU drawing.

The renderer could draw the entire scene again on the next wake after a busy
Present. Some wakes are only about a millisecond apart. Fewer large icons do not
necessarily mean less work: scaling, filtering and blending depend on covered
pixels, and repeated rejected frames still incur drawing work.

## Changes

- Each tab's existing swap chain now exposes a frame latency readiness handle,
  with a maximum queued-frame latency of one. Before ordinary drawing, the
  existing presentation lane probes readiness with a zero timeout. Busy wakes
  retain the newest model and skip icon uploads, glyph rasterization and drawing.
  Animation advancement and input handling continue. No GPU wait is added to the
  owner STA or shared frame registry, and tabs keep independent devices/workers.
- Deferred receipts cannot acknowledge the preceding frame as accepted for the
  current revision. A rejected nonblocking Present has a refresh-paced recovery
  attempt, so a DXGI slot already acquired before rejection cannot strand a tab.
  Busy wakes without a newer model retain the last successful receipt, allowing
  the owner to read it even if its message dispatch has been delayed.
- DComp transition planes and the completed-frame handoff keep their existing
  path: those surfaces do not present the ordinary swap chain.
- At destinations of at least 512 physical pixels, icons magnified at least 2x
  use hardware bilinear sampling, as thumbnails already do. Native-size icons
  retain nearest-neighbor sampling; downscaling and ordinary-size reconstruction
  retain high-quality cubic. Bilinear magnification can look slightly softer
  than cubic; it does not change the requested or cached image resolution.
- Resize retains the waitable flag, and device teardown closes its handle.
  Failed readiness probes and device removal enter the existing target-recovery
  path, so an unsignaled readiness object cannot hide device loss indefinitely.

The 512-pixel condition selects a drawing filter; it is not an image-size or
zoom limit. Pinch classification, accumulation, zoom range, metrics, anchoring,
layout and animation duration are unchanged.

Microsoft's [DXGI latency guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains)
describes readiness before rendering, the per-swap-chain latency setting and
preserving the creation flag on resize. The
[handle contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)
requires closing the returned handle. The
[interpolation definitions](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/ne-d2d1_1-d2d1_interpolation_mode)
describe linear sampling and cubic downscale prefiltering.

## Validation

`tests/large-tile-smoke.h` adds private-surface tests for one icon over 2K pixels
wide per row, 1,000 simulated busy wakes, latest input retention, deferred receipt
acceptance, rejected-Present recovery, resize and handle lifetime. Pixel readback
checks large rotated Cut icons in LTR and RTL. GPU timestamp queries compare the
previous cubic filter with the shared adaptive draw path at 512 and 2048 pixels.
These measurements include target clearing and exclude scan-out.
The actual render-worker test leaves the owner unpumped, forces the swap chain
busy, checks that its unread accepted receipt survives, then submits a newer
revision and verifies it remains deferred until its pixels are drawn.

Two existing retarget continuity assertions sampled progressing wall time on
either side of font/geometry work. They could fail at different checks in full
and focused runs (0.2-pixel header and 3-pixel cell tolerances). Those comparisons
now hold one test-thread QPC instant and sample zoom metrics before comparing
the old/new cell pose. Sampled selection-visibility frames likewise hold one
instant across metrics and geometry, preventing subpixel discrepancies caused
by advancing time between the two calculations. Their tolerances are unchanged.
Worker clocks, elapsed
animation tests, GPU timestamps and all production timing stay real. The focused
`--animation-continuity` suite passes without changing the mod's zoom functions.
The rapid-navigation check also permits the exact completed header pose when
its artificially advanced 165 ms transition finishes before the owner observes
the remaining 15 ms. Its first four 45 ms reversals still require a committed
scene; all eight retain warm-device, origin, native suppression and monotonic
header checks. Diagnostics confirmed a ready, accepted, fully settled receipt
at the previously failing near-end observation, rather than a lost revision.

The focused suite passed.
Both filters draw the same 256-pixel source onto a 2560 by 1440 target; the
2048-pixel destination is clipped at the bottom, as for a tile taller than its
viewport. Each sample includes clearing that same target.
On this machine, one release-gate run recorded these offscreen GPU medians:

| Icon destination | Previous cubic | Adaptive sampling |
| --- | ---: | ---: |
| 512 pixels | 0.124 ms | 0.089 ms |
| 2048 pixels | 0.664 ms | 0.273 ms |

The 2048-pixel case used about 59% less GPU time in this fixture. These are
per-test measurements, not a hardware-independent guarantee or a measurement
of displayed Explorer frames. The busy test skipped drawing for all 1,000
wakes, retained the current hover and resumed acceptance once readiness returned.

`build.bat -SourceFile 0.9.239.cpp` passed the complete release gate: repository
publication checks, real Windhawk DLL linking, callback stack budgets, the full
C++/graphics/input/tab/attachment suites, process exit and 21 actual DLL unloads.
The focused continuity and navigation suites also passed. Published source,
Windhawk export and the new archive have identical SHA256:

`42514707C5555C546512661DA26C2DF6C625A46866CE9A63DAC645D8DFC91E43`

All 42 earlier archives match `tests/archive-hashes-before-239.json`.
`UpdateMetrics`, `AdvanceRowHeight` and `ApplyRowHeight` are byte-identical to
0.9.238. Live Explorer pinching and displayed frame rates have not been measured
by these tests.
