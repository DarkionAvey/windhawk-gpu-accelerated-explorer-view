# Paired UI/render capture and diagnostic wait fix (0.9.294)

The 11:20 ZIP contains both UI thread 1664 and presentation thread 6100 in
Explorer PID 6448. Their QPC sampling intervals overlap the attached
`wh_log.txt`. Each has 1,000 snapshots; total pause is 74.475/76.599 ms and
maximum individual pause is 0.547/0.632 ms. These files resolve the earlier
capture/interval mismatch.

## Rendering versus native navigation

- The renderer's most frequent stack is its `WaitForSingleObject` work wait:
  930 of 1,000 samples. Another 50 samples include Direct2D/graphics-driver
  work and nine include DXGI presentation. These are sampled stacks, not a
  measurement of CPU utilization or displayed frame rate.
- In the shared sample interval, 709 retained complete renderer frame bodies
  average 0.905 ms, maximum 4.725 ms. Frame summaries independently report
  continuing-animation intervals up to 18.27 ms. There is no recorded sustained
  30–50 ms renderer workload in this particular capture.
- Twenty-one complete native `0x700` handlers in that interval take
  112.919–170.274 ms (mean 133.091 ms). UI stacks include Shell view creation,
  ItemsView/DirectUI construction/layout, accessibility and mod host hooks.
  Time inside the native dispatcher can include nested mod callbacks; it must
  not all be attributed exclusively to Windows code.
- The 232 retained compositor-clock observations average 15.407 ms, maximum
  16.729 ms. The renderer is not generally waiting for a per-frame Shell/UI
  acknowledgement. New pointer processing still depends on the owner UI
  thread. Retained pointer-update/up dispatch ages reach 47/62 ms; the larger
  266 ms pointer-message age is a leave message, not a moving contact.

Committed fade revision `0x6A` provides a direct overlap example. Its origin is
QPC 274151543277. The native navigation handler runs from 274151598819 to
274152787043 (118.822 ms). During that handler the renderer records decreasing
opacity 0.7442, 0.5358, 0.4635, 0.3255 and 0.1185 at fade ages 20.461, 37.139,
42.921, 53.956 and 70.516 ms. Each has a following S_OK presentation; the zero
pose is also submitted before the native handler finishes. Thus released
foreground feedback does progress while this UI handler is busy. S_OK remains
submission evidence, not proof of physical display of every pose.

There are 48,892 retained pipeline records and 20,837 explicitly dropped
records. Later revisions' sparse opacity samples must not be interpreted as
proof that all intervening renderer frames were absent. This capture does not
establish the cause of every visible skipped frame.

## Confirmed avoidable wait

Renderer samples 55 and 226, at QPC 274152449170 and 274179430965, show:

```
NtWaitForSingleObject
WaitForSingleObjectEx
OutputDebugStringA / OutputDebugStringW
Windhawk InternalWh_Log
mod frame-summary output
```

A third sample shows the same logging path mapping its output buffer. The
source emits `[GPU frames]` synchronously in `EndFrameProfile`, after its
pipeline `frame.end` and measured frame-body duration. Consequently those
duration records do not include this logging transport wait. The two sampled
waits occur after their respective committed fade has already expired; they
demonstrate an avoidable renderer wait, not that it caused those fades to skip.

0.9.294 captures immutable frame-summary data and sends it through the existing
bounded per-tab trace queue. Sorting, formatting and Windhawk output run on
that tab's logging worker. Producer thread attribution and the summary fields
are preserved. Queue contention/fullness uses the existing explicit loss
accounting; there is no synchronous fallback, extra worker, new timer or Shell
COM call. Frame-only logging can also initialize/reuse that writer. Native
navigation and input ownership are unchanged.

## Validation

The new regression uses actual profiler frames and a private Windows named
pipe. Its log consumer deliberately leaves bytes unread, so the writer's
`FlushFileBuffers` waits for actual consumption
([Microsoft named-pipe documentation](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-operations)).
Further profiling completes while that I/O remains blocked; after consumption,
the original producer thread ID and eight-frame metrics are checked. Existing
bounded-overflow, idle-writer, per-tab isolation and reclamation tests also run.

The initial focused checks passed, followed by a sandbox denial during private
fixture cleanup. The required full release gate subsequently passed with
native integration access, including all C++ regressions, 36 native popup
comparisons and DLL-unload checks. The published source is 0.9.294. The pipe
fixture also joins its released logging callback before closing test handles.
The final focused pipeline/logging suite passed again after that cleanup change.
Offscreen checks validate the logging change and lifetimes, not live Explorer
scanout smoothness.
