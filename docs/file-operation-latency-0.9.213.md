# Folder history and filesystem update latency, 0.9.213

## Evidence and limits

The supplied trace covers GPU window 0x170AFA, including directory requests
for Downloads. Its QPC values are the event clock; the log writer's wall-clock
prefixes are buffered delivery times and must not be used to time operations.

The 587-row result for generation 0x4F7 was published at QPC 106890564124
and received at 106890593656: about 2.95 ms in the UI queue. Its delivery scope
took 0.4359 ms. Generation 0x50B was published at 106890620272 and received
at 106891160693: about 54.04 ms in the UI queue; delivery took 0.0741 ms.
An overlapping native message 0x4C8 took 40.93 ms, including a recorded native
UIA resolution scope. The 111.60 ms native message 0xA1 is nonclient mouse
input, not evidence of a slow Delete/Paste implementation. The trace does not
identify a filesystem-notification timestamp or an entire destructive-command
transaction. It cannot establish the user's complete operation-to-visible-change
delay or attribute every native dispatch stall to this mod.

Code inspection establishes two independent problems:

- A saved folder vector could be moved directly into the displayed model and
  painted before async enumeration discovered changes made while away.
- The active filesystem handle was observed on the maintenance timer, normally
  250 ms. A further 90 ms quiet period gated refresh. Notifications during a
  names scan could cause the completed result to be discarded. Continuous
  operations could therefore keep successfully enumerated changes invisible.

## Change handling

The existing per-tab watch lane now waits on both its work wake and the active
filesystem notification handle, while still pumping its own COM apartment.
The first signal posts a generation-tagged owner message. The owner rearms the
notification and starts the existing names/layout pipeline. Shell structural
hints use one coalesced owner message through the same path. No additional
thread, callback pool, recursive whole-drive watcher or high-frequency poller
is introduced. The maintenance timer remains a notification-delivery fallback.
Warm hidden tabs latch their hints without starting scans. Activation posts
the same flush without doing Shell/provider work on the activation stack, so
twenty background tabs do not each re-enumerate on a foreground operation.

One names request runs at a time. Notifications during it remain latched; its
actual completion publishes the result and releases a follow-up scan. They do
not cancel it, discard it while waiting for quiet, or advance work on an assumed
deadline. Active rename editors retain their existing deferral. Selection,
viewport, row diff/animation, native suppression and enrichment use existing
functions. Last-access notifications are excluded from the directory handle so
that merely reading files cannot create a refresh feedback loop.

Filesystem snapshots retain a shared change handle plus the watch revision
captured by the names request that produced their rows. Returning checks the
latched event with a zero-time wait and compares revisions. Changed, unobserved
or unverified snapshots cannot paint old rows; their normal async names request
supplies the new list, including an actual empty result. A consumed event cannot
authorize an older snapshot because its revision differs. Unchanged verified
snapshots retain immediate vector-move restoration. Ordinary warm **tab**
activation continues to retain its accepted surface, text and icons.

Only the active folder occupies the existing watch worker. Cached handles stay
latched without a worker or Shell subscription for each history entry. The
snapshot RAM budget also charges 4 KiB per retained watch to account for its
kernel resources. Cache eviction, close and unload release ownership normally;
the worker holds its own reference while waiting. A stale signal racing
navigation cannot terminate that tab's watch lane. A failed rearm recreates
the watcher through its existing background creation path.

Recycle Bin continues to use its Shell namespace notification/identity path;
it is not treated as a physical filesystem folder. These changes do not claim
to observe a provider's physical storage commits or create a new namespace
cache validation mechanism.

## Capturing the remaining delay

Enable the existing **pipelineTracing** setting. All logging defaults remain
off. The new records share the existing tab trace identity, QPC clock, sequence
numbers and bounded async writer:

| Record | Meaning |
| --- | --- |
| `file.native.key` | Native keyboard command dispatch scope; `a` is the key. |
| `file.selection.key` | GPU/frame selection shortcut scope, including menu acquisition. |
| `file.selection.contextMenu` / `file.selection.invoke` | Separate Shell context acquisition and verb invocation spans. |
| `file.menu.invoke` | Existing item/background menu invocation scope. |
| `tree.filesystem.signal` | Watch worker first observes the kernel signal; `a` is its watch generation. |
| `tree.observe[source,verify]` | Owner consumes/coalesces a notification; `a` is its observer batch and `b` its watch generation. Source 0 is filesystem, 1 Shell. |
| `tree.refresh[observerMs]` | Names request queued; `a` observer batch, `b` directory generation; elapsed since observation. |
| `tree.scan` / `tree.layout` | Actual worker scan/layout spans, carrying observer batch and directory generation. |
| `tree.commit[observerMs,rows]` | Names result enters model commit, with elapsed observation time and row count. |
| `tree.present[observerMs,commitMs,rows]` | A presentation containing that model was accepted, with elapsed observation/commit times. |
| `tree.cache.rejected` | A saved filesystem list was rejected before restoration. |
| `tree.watch.rearmFailed` | Watch recreation was required; `a` contains the Win32 error. |

Kernel directory notifications can combine multiple changes. Their first
observation is an OS notification timestamp, not the physical disk-write time.
An accepted Present/composition submission is not proof of when pixels reached
the monitor. Existing frame/compositor records supply the subsequent pacing
context. Native dispatch spans expose remaining UI queue stalls without guessing
which command or provider caused them.

## Validation

`tests/filesystem-mutation-smoke.h` uses a private temporary directory and real
kernel change notifications. It dispatches no maintenance timer. It verifies
direct worker wakes, first-hint refresh, names-completion coalescing, preserved
unchanged restore, move/delete while away, empty publication, consumed-event
revision rejection, later watch rearming and obsolete-generation messages.
It also rejects snapshots whose initial scan preceded watch installation and
checks that a hidden warm tab defers its scan until its activation flush.
Existing cache tests explicitly establish a real watch for their manually
constructed snapshots. Sort/namespace tests no longer advance a fake quiet
deadline.
The grouped navigation fixture awaits a watch-covered scan before testing
immediate cached restoration; merely observing names readiness does not imply
that asynchronous watcher installation has completed.

Focused `--filesystem` and `--navigation` validation passed. The final full
release gate also passed: repository workflow, installed Windhawk DLL linking,
all C++/graphics/input checks and 21 actual DLL unloads. The maximum compiled
critical callback stack frame remains 1,928 bytes. Twenty warm 4K devices retain
1,483.52 MiB local GPU memory and return to 9.86 MiB after close; three cached
attachment returns peak at 0.4353 ms CPU. All sixteen older archives and README
retain their hashes. The published source and 0.9.213 archive share SHA256
`C1E0060DABB3098C79E7B0DCCD90F225EB89799D22AB8DC71A791D03E6244173`.
See `tests/build-0.9.213.log` and CHANGELOG.md.
These are private component and integration tests, not live Explorer operation
latency or displayed-frame-rate measurements. No user file operations,
clipboard changes or focus changes were performed.
