# Parallel regression suite and option effects (0.9.341)

## Why parallel runs fell back to serial

Suites were 10 grab-bag processes; each child also ran `TestNativeSuppression`
first to register native host window classes. Evidence from runs at 4 and 16
processes on a 16-core machine:

- Four tests compared the desktop's real foreground window before and after
  (native suppression, paste focus, timing readiness). Any other process
  changes it; `touch` failed this way in 0.4 s at `-Jobs 4` and passed alone.
  They now compare `Regression::Activation` (this thread's focus/active
  window and whether this process owns the foreground).
- `initial-sorting` read DefView's row order after one message pump. DefView
  sorts asynchronously, so under load the order was the previous sort. It now
  waits (bounded) for the native order to converge to the GPU order.
- The private touch digitizer was identified by diffing the session-wide
  pointer-device list, which another test process could change. Creation and
  identification are serialized by a session mutex; tests are not.
- `large-tile-backpressure` asserted a closed handle *value* was invalid;
  values are recycled by other threads. It now compares kernel objects.
- `graphics-isolation` required a 180 ms deletion transition to still be in
  flight after real-time pacing checks; the clock is held for those checks.
- `selection-checkboxes` depended on native DefView reacting to Shell
  broker-delivered deletions and dropped the overlay's private selection
  messages in its fixture; it now refreshes the native view and forwards them.

After isolation, all 85 existing tests passed one per process. Machine-wide
resources were inventoried; only the preprocessing benchmark (`quiet-cpu`)
and the visible swipe compositor probe (`input-desktop`) are Serial.

## Product defects found by the new tests

- Header checkbox Select All could clear all selection under a slow UI
  thread. Mod log: `Ctrl+A native=6 displayed=6`, then a late
  `EXTERNAL_IMPORT`, then `selection recovery queued (native selection commit
  not verified): observed=6 expected=6`. A refresh between capture and import
  changed `directoryToken`, so a stale import was treated as a failed commit.
  48 concurrent runs: about 10–15% failures before, 0 after.
- Small icons: filename highlight leading padding 2 px vs trailing 5 px at
  every text size (inset clamped to the layout box before the cell clamp).
- Two-line grid captions: first-line descenders uncovered where the second
  line is narrower (overlap split at the midpoint), and a fractional seam row.
- Two `Wh_Log` calls passed 64-bit values to 32-bit specifiers; found by the
  compile-time format check now applied to every `Wh_Log` call.

## Option crash search

The live sweeps apply every value of all 36 options (530 changes) on a
genuine DefView from a separate thread, in six busy states, visiting every
item (images incl. 4000x3000 and 3000x10, shortcut, archive, hidden/system),
and start 272 views with a non-default value configured. No crash, hang or
native fallback occurred in-process. Not covered: Windhawk's API hooks, the
real tabbed Explorer frame and command bar, real touchpad/touch hardware and
user files' third-party property/thumbnail handlers.

## Failures previously masked by MacType (fixed)

Midway through the session MacType stopped running on the test PC. Two checks
then failed deterministically, also with the original pre-change binary:

- `drag-preview`: operation emoji were normalized to Direct2D's conservative
  recorded-image bounds (53 px for 47.5 px of ink), so they drew ~9% small.
  The symbol is now measured from its layered outlines, refined by one
  CPU-only software rasterization at the final transform (no GPU readback),
  and drawn directly from the shaped layout.
- `graphics-isolation` header fade: the reference frame was the cold first
  paint (direct DirectWrite), the fade frames cached masks. Reproduced in
  isolation by drawing direct first on fresh text: masks were always
  rasterized with a white brush, but grayscale alpha correction depends on
  brush luminance (dark text differed by up to 59/255); the direct path also
  placed trailing/centered text in a different box (up to 177/255) and
  applied alpha correction to translucent brushes (17/255). Masks are now
  keyed by and rasterized in the text colour, the direct path uses the
  mask's canonical box, and translucent direct text is drawn opaque in a
  bounded layer. `render-quality` asserts direct == cached (direct first) for
  3 alignments x body/header/bold header x 3 colours x 2 opacities.

## Machine conditions that remain

- Process churn on this PC stalls the machine-wide Shell change-notification
  broker (explorer.exe) for 20-50 s at a time: measured with a private
  SHChangeNotifyRegister probe while merely launching and exiting the test
  executable 200 times (`--list-tests`, no test logic); idle and shell-script
  file churn stayed under 1 s. Windhawk and Microsoft Defender both act on
  process creation here. At `-Jobs 16`, tests that wait for broker-delivered
  notifications (provider navigation/selection, rename notification, history
  cleanup) can then exceed their bounds.
- `graphics-isolation` still contains real-time animation assumptions that
  fail under heavy GPU/CPU contention (8+ concurrent heavy processes):
  "deletion has no intermediate submitted frame", "drained atlas cells were
  never reused", "retargeted frame did not submit". Converting those checks to
  `AnimationClockFixture` is the remaining isolation work.

At the default `-Jobs 4` the full gate passed (94 tasks, 191 s including
compilation).

Observation, not changed: in RTL at extreme sizes (115 px text in a narrow
Name column), DirectWrite shows only "…" when not even one character fits;
LTR shows a clipped first character.
