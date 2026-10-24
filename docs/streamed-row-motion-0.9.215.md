# Streamed restore/delete motion, 0.9.215

## Evidence from the two captures

The 19:19 trace contains native 0xB04 dispatches lasting 204.84, 393.31,
1,190.25 and 706.47 ms. Their target is distinct from the GPU overlay. The mod's
directory-delivery scopes peak at 0.83 ms and GPU drawing scopes at 2.78 ms.
These records support leaving the remaining synchronous native work separate
from the mod's animation diagnosis. They do not identify the internal cause of
each native stall. No change is made to drop or suppress those native messages.

The smaller 19:22 trace identifies a different, reproducible mod problem:

- The list shrinks from 691 to 672 items, then grows back to 691. Directory
  commits are interleaved with successful presentations, usually about one
  165 Hz refresh interval apart.
- `ui.motion` repeatedly reports row progress **0**, including after successful
  submissions. For example, records 294609–295540 span about 39.6 ms of deletion
  updates; 298116–299140 span about 71.7 ms of restoration updates at progress 0.
- Each commit rebases from the last submitted picture, clears the animation
  clock, and makes the next accepted frame start at zero again. If another
  result arrives before the next progressing frame, moving/fading rows stay
  at the same phase. They only advance normally after updates stop.
- The GPU draw scope averages 0.73 ms and peaks at 2.81 ms. Present CPU time
  averages 3.77 ms, peaks at 5.31 ms. This capture establishes repeated motion
  restarts rather than an expensive per-item GPU draw as the main finding.

Some long intervals in the overall trace occur before an operation begins,
when the view has no active structural animation. They are not counted as
dropped animation frames merely because consecutive frame timestamps differ.
All calculations use recorded QPC values, not buffered log-writer timestamps.

## Fixes

The first structural transition still begins at its first successful
presentation, preserving the existing behavior when Shell work or DXGI
backpressure delays its first frame.

Once that motion has begun, every subsequent directory commit samples one
current QPC time and evaluates the existing easing curve at that time. The
visible old-row sample is rebased to this continuous position, height and
opacity before the logical vector changes. The replacement motion retains a
started clock at that same sample time. Several commits between paints can
therefore advance the model's motion; they no longer keep restarting from an
unpainted zero phase. Existing durations, easing, geometry and paced drawing
remain shared with the established row/group/grid paths.

The same current sample updates grouped headings when a grouping transition
is already active. Outgoing rows preserve their current height/opacity across
both list and grid updates. Grid/group transitions previously cleared all old
row ghosts, so a second deletion could abruptly remove a row still fading out.
The shared transition now retains mapped removed rows, including when the last
grid item is deleted. Ghosts have no logical selection or hit targets.

Both transition constructors use one shared index of the captured viewport's
new row indices. A single-column layout walks its neighbors in order; grid
layouts use indexed lookup. This removes repeated full-sample searches for
every output row. Work remains bounded by the viewport, without Shell/provider
calls, whole-directory copies or another worker/animation implementation.

The immediate watcher/completion-coalesced names updates from 0.9.213 and
input-completion frame handoff from 0.9.214 remain intact. No directory quiet
timer, extra presentation surface, guessed operation-end timeout or delayed
filesystem response is introduced. Synchronous native calls can still prevent
the UI thread from submitting a frame while they are executing.

Under pipeline tracing, `rows.retarget[progress,rows,ghosts]` records the sampled
progress, logical row count and retained outgoing-row count at a continuation.
These records, like accepted Present measurements, do not measure monitor
display times or prove a live frame rate.

## Logging settings

The five existing switches now appear under **Logging**. Runtime reads use
`Logging.perfLogging`, `Logging.frameTimeLogging`, `Logging.pipelineTracing`,
`Logging.uiTransitionLogging` and `Logging.gpuMemoryLogging`, matching the
nested settings schema. All five defaults remain off. Old flat setting names
are no longer consulted; logging can be enabled in the new group when needed.

## Validation

The private burst fixture inserts ten files into the visible sorted interval,
then deletes them one by one, in both LTR/RTL and list/grid layouts. It models
the first accepted zero-phase frame and then advances animation time across
nine subsequent real names-worker commits without another paint between them.
Assertions require a started clock, advancing opacity, retained/shrinking ghosts
and unchanged logical membership. Slow background work may legitimately finish
a fade, so those checks do not assume a scan finishes within a guessed interval.

The existing real-GPU overlapping-batch test now checks that motion continues
toward its target at the current clock, rather than remaining frozen at the
previous submitted position. Wheel/collapse, held input frames, repeat arrows,
scroll-thumb retargeting and accepted-presentation boundaries remain covered.

Focused filesystem/burst tests and the final complete release gate passed:
repository workflow, installed Windhawk DLL linking, all C++/graphics/input
checks and 21 actual DLL unloads. The same burst test fails on preserved
0.9.214 with `insert burst reset its started animation to unpainted`, confirming
that it catches the regression. Actual GPU overlapping batches, native
selection and restored viewport, filesystem/cache, input-completion frame
handoff and warm-tab lifecycle tests all passed. The maximum compiled critical
callback stack frame remains 1,928 bytes. Twenty warm 4K devices retain
1,483.52 MiB local GPU memory and return to 9.86 MiB after close. All eighteen
older archives and README retain their hashes; the tested archive matches
published SHA256 `353E96C2447F13DD757CD37034E142FDC3A36A404DB8EEDDDA794D17C7A5EE84`.
See `tests/build-0.9.215.log` and `tests/burst-0.9.214-negative.log`.
No file operations were performed against the user's directories, and no user
clipboard or focus changes were made.
