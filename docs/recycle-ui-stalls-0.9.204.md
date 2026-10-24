# Recycle Bin UI stalls in the real file-management capture

Follow-up: the next multi-tab capture confirmed `0x0` suppression while stalls
persisted. The viewport repair below was insufficient for the main live issue.
See [the native UI Automation investigation](native-uia-stalls-0.9.205.md).

## Evidence and limits

The supplied 19.9 MB capture covers approximately 40 seconds. Explorer's folder
UI thread is 6224; the affected overlay is `353097A` and its native DirectUI
child is `EB0F8A`. The log contains 181 pipeline drop notices, so absence of an
operation's end record is not evidence that the operation did not finish.
Writer timestamps can lag and interleave; QPC and paired spans establish
operation durations where both records survived.

Eight independent slow-dispatch records identify the native DirectUI child's
`0x4C8` messages taking **531–828 ms**. Matching frame-queue records include
735, 828, 781 and 687 ms waits. Surviving pipeline spans independently show
578.45, 709.49, 738.68 and 777.92 ms native dispatches. The same native child
also has inexpensive dispatches, so this is intermittent work, rather than a
uniformly expensive wrapper around every message.

The important distinction is the shared STA: this hidden native control runs
on the same thread that processes the GPU view's input and submits frames.
Typical affected frame summaries spend roughly 0.5–1 ms drawing and 4–5 ms in
total CPU submission stages, mostly Present. Moving metadata/icon jobs to
workers has not removed native Explorer's hidden presentation work.

A header release at input `(1087,29)` takes 688.05 ms. QPC shows approximately
686.6 ms inside that handler before its directory request starts, consistent
with the existing synchronous native `SetSortColumns` call. The old capture
does not separately time that call. Another long right-button handler includes
an interactive context menu; its entire duration must not be described as a
CPU stall. Its separately logged Shell command takes 109 ms. Native navigation
tree callbacks also contribute 31–93 ms stalls. Tray timers on thread 13208 are
separate from the folder STA and must not be counted as this overlay's work.

The capture does not record the DirectUI child's viewport dimensions or
suppression ownership. It establishes the blocking component, but cannot alone
prove which internal DirectUI task ran. The code audit found a concrete
suppression gap, and the real Shell experiment below reproduced its expensive
deferred callbacks. The fix addresses that gap; it is not a claim that every
possible Shell extension or native command can be made asynchronous.

## Failure mechanism and repair

`SuppressNativeContent` acquired and hid the dedicated native host. On later
calls it returned immediately when that host was already owned. If DirectUI
was created afterward, the ordinary presentation/positioning pass never
acquired its viewport. A creation/show accessibility event was the only other
opportunity. Events can be delayed, skipped during an internal visibility
transaction, or absent for a child born beneath a hidden host.

Hiding the ancestor prevents visible overlap; it does not stop DirectUI from
realizing rows, templates, metadata and accessibility structures. An active
native viewport can therefore keep doing expensive work behind the GPU list.
The older real-browser regression only acquired presentation during
`OnViewCreated`; its child already existed and it did not exercise this gap.

0.9.204 keeps the existing ownership and geometry mechanisms and repairs three
entry points:

- An already acquired host still revisits its presentation children. Existing
  child acquisition is idempotent; late and replacement children get the same
  zero-viewport contract and retained restoration bounds.
- Before dispatching posted native work, the shared dispatcher acquires a
  previously missed DirectUI/ListView child beneath an **already owned** host.
  This uses HWND ownership on the owning thread; no Shell/provider call or
  active-tab inference is needed. The original message still runs normally.
- Header sorting repairs native child suppression before entering Shell's
  synchronous sort API. A retained view and lifetime/generation checks reject
  reentrant replacement results.

The existing per-tab model, selection, commands, watcher updates and workers
continue to run. No native message is discarded, delayed until a guessed
deadline, or moved across STA boundaries. The no-detach selection recovery
from 0.9.203 remains intact. Suppression is released with usable native bounds
before exposing the host on fallback/unload.

New `native.viewport.acquire[width,height]` and `native.sort` spans identify
acquisition and native sort cost. The existing frame-time slow-message record
now includes the target's client size and viewport ownership. If a native
provider still stalls with a correctly suppressed `0x0` viewport, a later
capture can establish that distinction without another high-volume pipeline
dump. All logging defaults remain off.

## Verification

The standalone **real Shell** probe creates private, offscreen
`IExplorerBrowser` instances. It compares normal native presentation, a hidden
host with an intentionally missed child, and the repaired child contract. It
uses a private 512-file directory and an intentional read-only Recycle Bin
probe containing 818 items during the focused run. It changes sort settings
only in those unsaved private browser views, never the user's Explorer view.
The Recycle Bin is never deleted from or otherwise modified.

Representative focused-run CPU timings, including eight sorts per browser:

| Namespace / presentation | Deferred native callbacks, total / maximum | Native sort calls, total / maximum |
| --- | ---: | ---: |
| Filesystem, missed child beneath hidden host | 683.31 / 126.34 ms | 957.59 / 156.99 ms |
| Filesystem, repaired child | 2.39 / 0.75 ms | 343.23 / 61.20 ms |
| Recycle Bin, missed child beneath hidden host | 1150.63 / 291.50 ms | 1458.96 / 275.61 ms |
| Recycle Bin, repaired child | 1.74 / 0.52 ms | 396.70 / 69.01 ms |

These are control-level CPU measurements, not a replay of the user's file
operations or displayed frame rates. Native sorting still has synchronous
owner-STA model costs; suppression removes unnecessary presentation work,
not Shell's need to execute a requested sort.

Regressions assert actual ownership/geometry rather than timing thresholds.
They cover children created after host suppression, a missed child reaching
its first native-work dispatch, sorting with a missed creation event,
reentrant view replacement during sorting, real namespace/filesystem model
counts, native mode/selection commands, native refresh, constrained resize and
positive restoration bounds. The full release gate also covers animation,
scrolling, selection recovery, clipboard/focus and DLL unload. See the
0.9.204 entry in `CHANGELOG.md` for the final gate result.

The first full gate caught an unrelated isolation error in the existing
pointer-sampling test. It replayed cursor positions but consulted the real
desktop's queued-input flags. Ambient input could correctly defer painting,
while the test incorrectly assumed its next direct frame call would run.
That test now uses the existing substituted input-queue session. The separate
pending-input regression still verifies frame yielding; production pointer
sampling and frame acknowledgement behavior are unchanged.

A subsequent gate reached the cold-attach test and caught the same class of
error in its focus check: it compared the user's foreground window before and
after its asynchronous retry observation. Cold-attach/background attachment
checks now use the existing focus fixture and assert that the mod made no
focus-setting calls. Actual iconic/enabled state remains checked through OS
APIs. User activity outside the private test windows is no longer treated as
an activation performed by the mod.
