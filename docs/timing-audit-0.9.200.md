# Completion and timing audit: 0.9.200

## Scope and method

Reviewed the current mod's timers, tick/QPC comparisons, delayed messages,
retry counters, worker waits and readiness flags. Traced the surrounding
startup, navigation, names/property/icon delivery, layout, presentation,
selection, creation/rename, input and shutdown paths. The question was whether
elapsed time or a retry count could authorize dependent work, discard a still
valid request, or leave a deferred request with no future wake.

This is a source audit with adversarial offscreen component regressions. It
is not a measurement of a slow physical PC, live Explorer, displayed frame
rate, or Windows/Shell provider latency.

## Fixed dependencies

| Path | Fragile behavior | Completion and cancellation now used |
| --- | --- | --- |
| Automatic column fit | User-selected millisecond delay and resize debounce; an initial fit could precede properties and rely on a second fit. | Boolean `Columns.autoRefitColumns`, default on. A pending request waits for final names, completed enumeration, native column definitions, current sampled property values, accepted initial content, and idle layout/input. Names/property/column delivery, submitted layout and input completion queue one owner-thread continuation. |
| Resize fit | A quiet interval guessed when interactive resizing had ended. | `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE` delimit an interactive resize. Programmatic size changes coalesce through the same continuation; the existing spatial change threshold remains. |
| Fit sample | Sampling the integer range between the first and last visible item included hidden rows in collapsed groups. Their metadata was never requested, so a metadata follow-up could remain blocked forever. | Both measurement and readiness use the same bounded sample of actual visible item identities. Current property and column generations are required. This PC's seeded columns do not wait for an unnecessary property request. |
| Column presentation → Shell setup | The animation clock could expire before a settled draw was accepted. Native descriptors/optional setup could then block the UI before that draw. | A separate pending-presentation flag remains until rendering accepts the settled column geometry. The existing compositor pacer retries presentation; only then can descriptor capture, feature setup and fitting continue. |
| Cold window attachment | The observer stopped posting retries after roughly 1,050 ms. A slow Shell view without another show/input event could never attach. | Retain each unresolved visible frame with capped retry backoff. Stop on attachment, an explicitly unsupported view, hidden/destroyed frame, or observer shutdown. Minimized frames avoid expensive discovery and remain eligible after restoration. |
| Navigation rebind | After the fast retry window, code could clear a pending incoming view or inspect/reuse the departing view. A transient folder/PIDL read failure could also be mistaken for an unsupported folder. | The fast interval only controls probe frequency. A live incoming view stays pending until actual binding/cancellation/destruction. Cached and full discovery reject a departing HWND; reused HWNDs require actual folder identity. Resolved unsupported identities are distinguished from unresolved reads. |
| Shell subscriptions | Selection document binding had two attempts. Transient destination subscription errors were cached as permanent negatives. Selection retry also depended on navigation-event subscription success. | Keep transient failures pending with capped backoff on the owning tab's existing maintenance lane. Retain the browser independently of subscription success. Cache only a successful subscription or an explicit missing interface/connection point. Validate view generations around COM calls and unadvise a subscription that became stale during `Advise`. |
| Returned-child native selection | A fixed post-paint delay and eight attempts guessed when native selection/focus would be ready. The old paint tail could schedule selection even after rejected rendering. | An accepted render submission arms a barrier for a successful compositor cycle that starts afterward. Native retries continue until exact selection/focus verification succeeds; their count only adjusts pacing. Navigation and later explicit user selection/input cancel the old request. No waiting is added to the UI thread. |
| New-item rename | Three/four-second eligibility windows, eight refresh attempts and a 200 ms focus guard could expire during slow creation/enumeration or misclassify the initiating mouse press. | Explicit New intent is scoped to native view and folder generation. The accepted created pathname remains pending across enumeration delay. Watcher recreation in that same context preserves it. New input, context/focus loss, failed command or teardown cancels it. The initiating press is tracked until actual release; later clicks can commit the editor without a guessed grace period. |
| Touchpad completion | 1,800 ms without activity forced an active gesture to end, even if DirectManipulation was delayed. | Continue RUNNING/INERTIA. Recover a missed notification using the actual viewport status: READY completes, disabled/suspended cancels. Existing native callbacks still delimit contact, inertia and completion. |

Automatic fitting still uses the existing visible-row fitter and clipping
preflight. It does not enumerate every file or wait for offscreen metadata and
icons. An explicitly invoked manual fit retains its immediate result plus its
existing one-time metadata refinement. A permanently unresponsive provider
keeps dependent work pending; waiting longer is not treated as success.

## Timing intentionally retained

| Timing category | Why it remains safe for sequencing |
| --- | --- |
| Scroll, zoom, hover, selection, grouping, row/column motion, fades and clipboard wiggle | Clocks define interpolation or an intentional visual duration. They do not certify provider completion. Settled column handoffs now require accepted rendering; existing row transitions already retain ghosts through an accepted final submission. |
| Type-search expiry, system double-click windows, header-wiggle gesture history | Time is part of the user gesture's meaning, rather than an estimate of asynchronous operation completion. |
| Directory/Shell change quiet intervals and burst limits | They batch hints. Enumeration/property results and generation validation determine the data that can be committed. A later hint remains pending or schedules a follow-up. A quiet interval does not mean the filesystem can never change again. |
| Native column polling/restoration cooldown, selection polling, clipboard contention retry and subscription/cold-attach backoff | These regulate repeated work. Dirty flags, pending identities and requests survive a late tick or transient not-ready result. Native APIs still supply success and identity. |
| Startup warmup, navigation fast interval, bounded compositor tab-switch probe | These select a faster observation rate, not a correctness deadline. Ordinary maintenance, lifecycle/identity events and the independent HWND observer continue after the fast phase. An unresolved navigation/attachment is no longer discarded when that phase ends. |
| Drive-capacity freshness and accelerated device-change probes | Periodic refresh is a freshness policy. Actual worker/native snapshots provide completion. Normal periodic capture continues after the accelerated probes end. |
| Per-frame CPU/time/byte budgets | Yielding preserves unfinished capture/upload work and its existing continuation. Budget exhaustion is not publication of a completed result. |
| Worker wake/reaper cadence, unload retries and callback drain sleeps | Teardown checks actual thread handles, outstanding COM callbacks and owned objects. It does not unload merely because a grace interval elapsed, and does not terminate provider threads. |
| Diagnostic thresholds, observation windows and responsiveness probes | These control logging/sampling only and do not license file commands, visibility, metadata commits or teardown. |

## Regression coverage

`tests/timing-readiness-smoke.h` runs through real owner-thread handlers using
private offscreen windows/files and substituted focus/clipboard APIs. It checks:

- Final names, native columns and partial properties arriving in different
  orders; stale property generations; no refit after repeated not-ready checks;
  exactly one continuation after the final completion.
- A real capture-change message releasing the fit while the generic recent-input
  clock is still fresh; resize completion; boolean disable/cancellation.
- A collapsed group containing 900 unresolved hidden rows, between visible rows,
  without waiting for those hidden rows.
- Settled column presentation and existing deferred native-column capture.
- A very old touchpad activity timestamp without a terminal status, followed
  by an actual READY message.
- An unsatisfied compositor barrier, its explicit acknowledgement, twelve
  rejected native selection attempts, then successful identity verification;
  a Shell query that changes the view generation before returning cannot
  apply an obsolete selection or clear its newer replacement.
- A live incoming view after expiry of the old navigation fast interval.
- More than the former selection-subscription attempt limit, independent
  transient navigation/destination subscription failures, and actual terminal
  missing-interface results.
- A created item older than the former expiry and retry limits, then completed
  enumeration, same-generation watcher recreation, and cancellation by Escape.

`tests/attachment-smoke.h` also consumes bootstrap wake messages after the
former cold-attach retry horizon, including observer shutdown/restart. Existing
rendering, rapid input, clipboard, focus, native suppression, multi-tab worker,
RTL, cancellation and DLL-unload suites remain part of the full release gate.
Existing column-animation fixtures now submit settled geometry before expecting
dependent work. The metadata-refresh fixture also advances the existing frame
continuation before inspecting queued demand: a contended nonblocking lock
does not promise synchronous insertion, even on a fast PC.

Focused command (does not publish):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-regressions.ps1 -SourceFile 0.9.200.cpp -TestArguments --timing
```

After publication, select `latest.cpp` or the archived source instead. Release
validation and any limitations are recorded in `CHANGELOG.md`.
