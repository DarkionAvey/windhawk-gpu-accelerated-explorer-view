# Slow-PC navigation capture, 0.9.293

The returned capture contains 1,000 snapshots of Explorer PID 6580, UI thread
1252, starting at 2026-10-30 00:49:56 +01:00. The log confirms 0.9.292. Sampling
paused that thread for 90.365 ms in total, at most 1.841 ms per copy.

## Findings

- 35 retained complete native `ShellTabWindowClass` message `0x700` spans take
  121.10–195.78 ms, averaging 151.41 ms. Matching outer dispatch spans add
  0.42–11.43 ms, averaging 1.09 ms. These are inclusive elapsed times, including
  reentrant callbacks and scheduling; they are not exclusive native CPU costs.
- Matching Microsoft PDBs resolve native stacks into Shell view creation and
  destruction, tooltip creation, DirectUI item realization/layout, navigation
  tree updates, and command-bar command/property evaluation. Examples include
  `CShellBrowser::_OnGoto`, `CShellViewFactory::_CreateNewConnection`,
  `CDefView::CreateViewWindow3`, `UIItemsView::_ResetRoot`,
  `LineScroller::_RealizeContent`, and `CommandBarViewAdapter::UpdateCommands`.
  Nearest-export labels in the original text are not function identification.
- 395 samples are explicitly in `WaitMessage`, including DUser's nested wait.
  Stack counts are inclusive; the mod appearing under native dispatch does not
  assign that native work to the mod. The original sampler omitted less common
  stacks outside selected native modules and retained no sample timeline.
  It cannot pair an individual stack with a particular slow dispatch.
- The largest retained UIA provider-root lookup total is 0.2664 ms. Native
  selection spans top out at 5.73 ms. Neither establishes the cause of the
  repeated 150 ms navigation stall.
- 101 retained render submissions carry the fade flag and succeed. In 16 slow
  native dispatches, multiple such submissions occur on renderer thread 11964
  while the UI handler is still running. Some cover about 70–75 ms. This
  confirms the foreground is no longer canceled by that handler; it does not
  measure scan-out or prove every intermediate opacity was displayed.

## Pacing defect and change

All 1,657 retained `frame.wake` records report a DWM wait of 0–0.015 ms.
Among 554 pairs of consecutive retained renderer frames, 518 are less than
5 ms apart, despite the reported 60 Hz refresh rate. This is submission timing,
not displayed FPS. `DwmFlush` was draining application updates without waiting
for a composition cycle; the old short fallback wait allowed excess redraws.

0.9.293 uses `DCompositionWaitForCompositorClock` on the existing shared pacing
worker. Control-event wakes recheck the registry without issuing frame pulses;
only newly active animations wake it, so continuous input does not repeatedly
interrupt the heartbeat wait. The same event interrupts shutdown. Each tab
still draws independently, and Explorer's owner STA never waits for the clock. Legacy API/error fallback is
retained. No Shell navigation call, provider apartment or native layout is
suppressed or moved based on these aggregate samples.

Microsoft documents the distinction in [DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush)
and the [compositor clock overview](https://learn.microsoft.com/en-us/windows/win32/directcomp/compositor-clock/compositor-clock).
The overview's handle/heartbeat return-value example is checked against the
real OS API in the presentation suite, together with the existing scheduler
shutdown and per-tab presentation checks.

The unchanged-refresh check exposed a heartbeat already queued when the prior
paint retired its animation. The render worker now ignores that stale idle
wake while continuing to process content, generation and foreground packets.
The check still requires an unchanged model to keep its existing GPU frame.

The logging path now records the actual sampled bubble opacity and its motion
age at drawing, keyed by foreground revision and frame. The portable sampler
records every distinct captured stack and a QPC timeline, so later captures can
be matched to the exact native spans without relying on buffered log timestamps.

The reduced drawing load addresses a demonstrated pacing defect. Its effect on
this slower PC, and the remaining native navigation delay, still require live
verification there.

## Verification

`build.bat -SourceFile 0.9.293.cpp` passed the complete repository, C++, native
menu and DLL-unload gate before publication. Unchanged refresh, real resizing,
busy-GPU retries, continuous native touch, foreground lifetime and scheduler
shutdown remain checked. The resize window fixture now dispatches the actual
compositor frame message through `OverlayProc`; previously its custom procedure
discarded that message. The readiness check now verifies idle signals do no GPU
work, then exercises busy retries with a genuinely changed model. Assertions
for preserved pixels and eventual completion remain in place.

An interim native-menu run contained an extra disabled Notepad++ extension
entry in the second popup. The menu-parity assertion was retained; all 36
displayed popup comparisons passed in the final complete run.

The updated portable sampler was compiled and validated against a private
waiting process: all 12 snapshots had QPC timeline entries and ranked stacks;
the process resumed and exited normally (0.554 ms total pause, 0.062 ms maximum).
These checks are separate from live Explorer displayed-frame validation.
