# Multi-tab CPU overload and remaining hiccup evidence (0.9.208)

## Live profiling before log analysis

The user reported stable VRAM after 0.9.207 but about 80% Explorer CPU with
20 Recycle Bin tabs. The live capture first measured Explorer's thread CPU
deltas, then sampled the hottest threads before reading the submitted log.

On the 32-logical-processor machine, Explorer accumulated 51,609.375 ms of
CPU in 2,054.963 ms of elapsed time: **78.48% of the machine's CPU capacity**.
The hottest threads each accumulated about 2,047 ms and were running.
Eight hot threads received 500 bounded stack snapshots each. They all resolve
to the same worker, `PipelineWriterProc`, using the actual installed
`local@gpu-accelerated-explorer-view_0.9.207_867512.dll` COFF symbols
(PE timestamp `0x6adb365a`).

The wait return address is RVA `0x4c50a`, `PipelineWriterProc+0x55a`; its caller
is `TabWorkerEntry+0xb` at `0x4c87b`. The sampler's nearest exported symbol
label, `Wh_ModUninit+0x45f2a`, is not an unload operation. Eight sampled TIDs
(32212, 35156, 20988, 38980, 43628, 35016, 33592, 22664) also appear as trace
writer IDs in the submitted log. That log starts **25 independent writers**.
This agrees with the measured consumption of approximately 25 cores.

Most hot stacks are inside `NtWaitForSingleObject`. Those samples alone would
not establish busy CPU; the simultaneous thread CPU deltas do. The wait is
returning immediately because its event remains signalled.

## Confirmed cause and fix

`StartTabWorker` creates a manual-reset wake event. The trace writer drained
its queue and called `WaitForSingleObject(INFINITE)` without resetting that
event. After the first enqueue, an empty writer repeatedly drained, locked,
checked and waited without sleeping. Disabling trace production left already
started writers in the same loop.

The existing `PipelineTrace::Drain` now resets the worker's wake **under the
queue lock when the queue becomes empty**. Producers retain their existing
bounded, nonblocking enqueue path. Resetting while holding that lock prevents
an enqueue from being lost between an empty check and a reset. A delayed
producer signal can cause one extra wake, which the next empty drain clears.

The writer supplies its worker handle, initialized before thread creation,
rather than reading the producer-facing pointer during startup publication.
Stop still sets its atomic flag and signals the inventory; the writer checks
stop after draining/resetting and before sleeping. Enable/disable transitions
keep their existing explicit wake. No new polling delay, shared writer,
cross-tab work serialization or logging setting is added.

Other worker waits were audited: manual-reset job lanes already reset empty
queues under their queue locks; the frame, tab-watch and lifetime-reaper
notifications use auto-reset events. The bootstrap event is a stop event.

All logging defaults, including GPU memory profiling, remain disabled.

## Hiccups recorded and live scrolling capture

The submitted 18 MB log spans about 24 seconds. It contains 149 stall reports,
including delayed frame delivery and native DirectUI work across several
views. Thirty frame-queue reports reach 672 ms. Ninety-two DirectUI `0x4c8`
stall reports total 3,829 ms; the trace reports 84,409 dropped records.
Completed nested dispatch spans reach 893 ms inside native dispatch
and 922 ms for total dispatch. Nested spans overlap and must not be summed
as independent CPU time. The log also reports dropped trace records, so it
does not provide a complete timeline.

After the initial idle UI sample, the user reproduced scrolling while a second
UI-thread capture collected 1,600 samples. It caught:

- GPU presentation/driver waits beneath `Paint`. The installed call site's
  return RVA `0x37264` resolves to `Paint+0x7ee4`. Its actual instructions pass
  sync interval 0 and flag 8 to swap-chain Present, confirming the existing
  `DXGI_PRESENT_DO_NOT_WAIT` path. Nearest exported NVIDIA/DXGI labels do not
  identify a disk-cache or encoder operation.
- Native DirectUI desired-size/font layout in `UIItemsView::PreProcessEventQueue`.
- Native UIA child-event provider calls through the mod's ownership checks.

These establish additional work on the UI thread during scrolling. They do
not prove why the driver waits or how much of the timing is secondary to the
25 busy trace writers. Removing the proven CPU spin is necessary before
attributing a remaining hitch to one of those paths. This release does not
claim that every hiccup has been eliminated or change the GPU presentation
strategy without that evidence.

Sampling overhead is recorded rather than hidden: the eight worker captures
pause their respective thread at most 0.511 ms per snapshot. The scrolling
capture's total UI pause is 153.831 ms over 1,600 snapshots, with one maximum
pause of 33.618 ms under load. It can contribute to observed timing. The
submitted stall log predates that capture and independently records hiccups.
No injection, process memory writes, file commands, focus changes or thread
termination were used. All sampling helpers exited after their bounded runs.

## Validation and next observation

`tests/run-regressions.ps1 -SourceFile 0.9.208.cpp -TestArguments --trace-cpu`
runs 25 actual private trace writers after their initial context records have
been delivered. It observes aggregate thread CPU with tracing enabled and
disabled, then checks re-enable/enqueue delivery and full worker retirement.
The observation interval is a CPU measurement, not a startup completion timer.

The initial focused fixed-source run measured **0 ms aggregate thread CPU**
over 407 ms for 25 idle writers. An unchanged archived 0.9.207 source is also
run as a negative control; focused tests cannot publish or edit that archive.
The release gate runs the complete C++/DLL-unload and repository workflow
suites before publishing.

The negative control fails the CPU assertion as intended:

| Source / state | Observation time | Aggregate writer CPU |
| --- | ---: | ---: |
| Archived 0.9.207, enabled | 422 ms | 1,718.75 ms |
| Archived 0.9.207, disabled | 625 ms | 2,015.62 ms |
| Final 0.9.208 gate, enabled | 406 ms | 0 ms measured |
| Final 0.9.208 gate, disabled | 391 ms | 0 ms measured |

Thread CPU counters have measurement granularity; measured zero is not a claim
that wake handling executes no instructions. The private negative-control
threads compete with the still-loaded live release, so its CPU totals are not
a standalone maximum-throughput benchmark.

The final complete gate passed repository checks, actual Windhawk DLL linking,
the standalone C++ suite, 32 nested actual-DLL hooks on a 256 KiB stack, all
21 real DLL-unload cycles, GPU lifecycle/restore and selection/input checks.
The 20-tab offscreen GPU test retained the previous memory result: 421.52 MiB
peak and 9.86 MiB after hiding, with no retained tab devices. An initial full
run stopped at an existing GPU lifecycle assertion. The isolated original
binary passed on retry; a subsequent full gate passed after adding failure
state diagnostics, without weakening the assertion or changing GPU runtime
code. That one assertion failure's cause was not established. Failed gates
did not publish. All eleven previous archives and the README retain their
original hashes; `latest.cpp` equals the tested 0.9.208 archive.

Reload the new mod to replace the existing spinning workers. For a subsequent
capture, frame-time and GPU memory logging can remain independently enabled;
full pipeline tracing is useful only for the short reproduction. If hiccups
remain after CPU returns to normal, repeat a live UI capture on that workload
to separate native layout/provider stalls from GPU presentation waits.
