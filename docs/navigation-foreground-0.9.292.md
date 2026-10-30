# Navigation feedback and stalls, 0.9.292

The supplied `log t.txt` records a committed Back release at 00:12:36.676,
followed by early destination display. The first retained render frame, about
0.27 ms later by QPC, already lacks the swipe-fade flag. Directory reset and
native view replacement call `StopTouchpad(false)`, which previously cleared
the visual before the later preservation helper could run.

Input cleanup now cancels unfinished contact input through the existing
retraction animation and leaves released foreground motion intact. Explicit
resource teardown and native fallback still clear feedback. Bubble state has
its own monotonic revision and bounded pending packet on the existing tab-owned
presentation lane. Release publishes before Shell work; stale content snapshots
cannot rewind it. While names are missing, the accepted content model remains
in place and the foreground continues on the compositor clock. Its frames remain
control acknowledgments, not acceptance of the new folder's content.

## What the lag trace establishes

- One complete `ShellTabWindowClass` message `0x700` span takes 112.48 ms,
  including 111.57 ms inside the native dispatch call. The coarse stall reports
  also record 125 ms dispatches and 78 ms queued frame wakes during navigation.
- A `DirectUIHWND` update takes 59.03 ms; the mod's measured UI Automation
  provider-root lookups take 0.098 ms, and batched notification delivery is 0 ms.
- Native-view rebinding reports 0 ms in both recorded gesture navigations.

These spans include reentrant callbacks and other installed hooks. They do not
identify the native handler's internal call stack. No speculative suppression,
delay or cross-apartment move of Shell navigation was added. Diagnosing the
remaining UI-thread stall requires a sampled stack during reproduction; the
existing `tests/live-stall-sampler.cpp` provides that read-only diagnostic.

Build a portable capture ZIP with `tests/build-stack-capture.ps1`. On the
affected PC, extract it, run `Capture.cmd`, select the affected Explorer window,
press Enter and reproduce the swipes during the ten-second capture. Return the
generated text file and the Windhawk log from the same reproduction. No symbol
download is needed there: the helper records module PDB identities and offsets
for later resolution. Snapshot validation used a private waiting process and
verified normal resume/exit, stack unwinding and module identities.

## Verification scope

Native calibrated contacts exercise Back/Forward commit and cancel paths,
input-resource cleanup, full cold-directory reset, and stale content snapshots
in LTR/RTL. The presentation worker must retire feedback while the UI owner
does not dispatch messages or supply replacement names. Existing pixel checks
cover touch centering and the 80 ms successful fade. These are native API and
offscreen GPU tests, not measurements of live Explorer frame delivery.
