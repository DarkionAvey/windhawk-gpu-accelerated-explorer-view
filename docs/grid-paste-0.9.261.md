# Grid copy/paste compositor pressure, 0.9.261

The fourth supplied trace spans a large directory receiving copied files in a
4,751 × 1,958 grid on a 165 Hz display. Completed CPU spans show:

| Stage | Samples | Median | p95 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Drawing | 352 | 8.13 ms | 10.96 ms | 14.79 ms |
| Items | 369 | 5.24 ms | 7.06 ms | 9.09 ms |
| Labels | 353 | 1.81 ms | 3.58 ms | 5.86 ms |
| Presentation snapshot, owner STA | 1,066 | 2.41 ms | 3.15 ms | 5.42 ms |
| Directory delivery, owner STA | 93 | 7.20 ms | 8.22 ms | 8.94 ms |
| Presentation | 346 | 0.07 ms | 0.10 ms | 407.42 ms |

Fifteen presentation spans exceed 50 ms. The longest complete span surrounds
the row scene's `SetRoot`/DComp `Commit`, followed by a successful submission;
it is not an ordinary swap-chain Present. A second stall reaches 248.53 ms.
The longest scene includes 741 item slices and reports 350,281,728 bytes
(334.05 MiB) for its three source bitmaps plus DComp surfaces, excluding
background/chrome and driver overhead. The trace drops records, so event
counts do not establish FPS or complete totals. Native message dispatch also
contains stalls up to 213.05 ms, separate from the renderer's commit.

Each slice previously created four cubic animation objects, a rectangle clip
and an opacity effect for each of its three ordered layers. Even stationary
properties and opacity 1 → 1 received animations. Replacing a dense scene
therefore generated thousands of redundant DComp objects and commands.

The replacement uses scalar properties for constant values, shares identical
curves within the scene's existing clock, and shares each item's clip and
opacity effect across its layers. Fully opaque items require no effect group.
The curve table exists only during publication; visual references retain the
needed curves. Pixel surfaces, layer order, easing, interruption/rebasing,
scrolling, provider lanes and resource limits retain their existing behavior.
`rows.composition.build` and `rows.composition.curves[shared]` make scene
construction time and unique curve count visible in future traces.

A private stress fixture publishes 12 consecutive insertion scenes per
direction without waiting for animations to settle. Its 4,000 synthetic items
produce 786 visible/transition slices with 291,901,440 source bytes. The
unmodified 0.9.260 publication code measured scene construction p50 of
40.66 ms LTR / 48.17 ms RTL, and commit p50 of 187.50 ms / 227.97 ms.
This isolates compositor publication; it does not draw an Explorer folder,
include native copy/paste handlers, or measure scan-out.

With the replacement and identical fixture, LTR / RTL scene construction p50
is 14.09 ms / 14.24 ms, and commit p50 is 2.56 ms / 2.74 ms (maximum
3.79 ms / 2.88 ms). Source bytes and slice counts are unchanged. The dense
fixture checks hit geometry after each interrupted publication; existing row
composition tests cover LTR/RTL grid/Details, grouping, ghost clipping,
layer pixels, unpumped-owner compositor liveness, concurrent scrolling,
retargeting and standby cleanup. These results address a reproduced commit
stall, without establishing a live Explorer frame rate or eliminating native
message-handler delays.

Release validation initially encountered an intermittent cached sort-property
assertion also present in older build logs. Its isolated run and the next full
run passed unchanged; cell/epoch diagnostics were added for recurrence. A later
selection-recovery fixture read live modifier state for its synthetic Delete
key. It now uses the existing private modifier fixture and reports whether a
failure means unhandled input or an unexpected captured command. File-command
blocking assertions are unchanged. No product sorting or selection code changed.

The final full release gate passed, including repository publication guards,
all C++ suites, the row/source pixel and lifecycle checks, and 21 actual DLL
unloads. Under that suite's load, scene construction p50 was 16.00 ms in both
directions, with commit p50 3.91 ms LTR / 3.40 ms RTL. The release was published
only after this complete passing run; failed runs left 0.9.260 published.
