# Selection and repeated UI Shell reads (0.9.255)

Nonempty small filesystem selections now share the raw-PIDL capture and
tab-owned selection worker already used for large/provider selections. The
normal import path no longer constructs their IShellItemArray or resolves
item/focused paths on the UI thread.
The worker binds its own folder and resolves identities; no live folder-view
interface crosses apartments. Empty selections still commit immediately.

The current selection count, raw snapshot and worker completion retain separate
consistency checks. Small filesystem results additionally compare a bounded
raw selected-PIDL set before acceptance: a silent replacement of one selected
item by another must not authorize an obsolete deferred command. Byte drift
retries conservatively, without doing provider canonical comparisons on the
UI. Existing namespace canonical resolution and bulk-selection paths remain.

Unchanged observational results retain the displayed selection, focus and
viewport without an extra repaint. New GPU input/navigation cancels the old
generation. File commands remain deferred until a current selection completes.

Two redundant reads were identified:

- The idle observer read the selection count, then CaptureNativeSelection
  read it again. Capture now owns that initial read; raw snapshot/completion
  reads remain because they straddle asynchronous work.
- An unchanged post-command mode observation read mode successfully, then
  forced another read because it had not changed. The first successful result
  now updates the mode cache once. The before/after comparison, failed-query
  fallback and periodic reconciliation remain. An unchanged ignored native
  mode cannot override a pinned GPU layout. Reentrant view changes reject the
  result and stop the superseded command continuation.

With pipeline logging enabled, `shell.view.mode`, `shell.view.sort` and
`shell.view.group` spans report operation count, duration, owning thread and
view token. `shell.selection.count.capture`, `.snapshot` and `.complete`
distinguish initial reads from consistency fences;
`shell.selection.identities.verify` records bounded result validation.
Existing `selection.resolve` spans show worker execution. These use the bounded,
nonblocking trace transport; disabled logging does not start a writer.

## Regression evidence

`--selection-ui` uses real private filesystem objects with substituted native
selection, focus and file commands. It checks 1/2/64 selections, a blocked
selection worker with an independent peer, same-count replacement without an
event, deferred Delete, immediate arrow-input supersession, navigation cancellation, immediate empty selection,
unchanged idle without invalidation, and worker-thread resolution records.
The idle probe performs one initial count plus snapshot/completion fences.
Mode checks cover successful reuse, pinned-mode authorization and reentrant
view-generation changes.
Provider navigation and modal selection use scoped modifier substitution
across Shell message pumps; focus restoration checks use the private fixture.
The startup capacity assertion now checks retained intent and eventual
submission, allowing the existing nonblocking queue's publisher contention.

Negative controls against preserved 0.9.254 fail on synchronous small selection
resolution and report `unchanged-command=2 grouping=1` before failing the
single-mode-read assertion. Archive sources are read-only.

`build.bat -SourceFile 0.9.255.cpp` passed the repository, Windhawk DLL link/stack,
full C++ pipeline/provider and DLL-unload suites and finalized 0.9.255. The
unchanged-command fixture reports one mode read versus two in 0.9.254; idle
selection uses one initial count plus the two asynchronous consistency fences.
The full suite also checks cached/pinned folder modes, actual user overrides,
modal Cancel followed by Up/Down, clipboard routing and native suppression.

These are component and offscreen provider tests, not evidence of complete
live Explorer event delivery or a guarantee that a single live-view COM call
cannot stall. Live PIDL/count capture, explicit native selection commits and
sort/group/mode reads still belong to the owner STA. Polling intervals and
recovery paths are retained. Rich selection diagnostics can themselves inspect
native state; the new query spans use the existing trace transport.
