# Polling and retry audit (0.9.254)

Scope: production source, timers, worker waits, elapsed-time gates and the
notifications/queues that feed them. Rates below are configured thresholds,
not measured wake rates: UI gates run when the maintenance timer is serviced.
This is a source audit and private component verification, not a live Explorer
latency or event-delivery trace.

## Removed work

| Path | Finding and change |
| --- | --- |
| `UpdateDirectory` color/accent checks, 2.5 s | Removed in 0.9.253. Resolved palettes now persist until attachment or a relevant settings/style event. No replacement polling interval. |
| `UpdateDirectory` visibility settings, 1.2 s | Removed in 0.9.254. The old dispatcher only saw posted settings messages; the frame subclass now forwards sent `WM_SETTINGCHANGE` broadcasts through `PostShellVisibilityProbe`. Bursts coalesce; inactive/animating tabs retain pending intent. Startup/navigation still capture a visibility snapshot. |
| This PC capacity timer, 60 s | Previously also requested fresh Shell item identities and rebuilt rows unconditionally. Subscribed views now submit only background capacity work. Native reorder, drive/media notifications, initial capture and explicit refresh retain membership updates. A missing native reorder subscription retains a coalesced discovery hint, since portable devices need not have a drive/media signal. Visibility is reconciled before the This PC notification handler requests replacement rows. |

Windows normally delivers [settings broadcasts](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-settingchange)
to top-level windows; a posted-message hook alone misses sent broadcasts.
Settings areas can be null or vary by subsystem, so visibility handling does
not invent an exclusive area-name whitelist. It reuses the existing coalesced
owner queue. The callback retains no borrowed broadcast string.

## Remaining design debt, ranked

| Priority | Location and present behavior | Assessment / evidence required before removal |
| --- | --- | --- |
| High | `OverlayProc(WM_TIMER)` / `RefreshSortFromExplorer`: eligible active filesystem tabs reconcile sort/group at the normal 250 ms maintenance cadence; This PC calls `ReconcileShellGrouping` there too. | Recurring owning-STA Shell COM, even when no setting changed. The old comment claiming a small sort descriptor makes the call quick was unjustified and is corrected. Native commands and reorder hints already exist, but their sort/group coverage must be completed and verified for programmatic changes before deleting reconciliation. |
| High | `RefreshNativeViewMode`: ordinary views use a 600 ms minimum cache age, then re-read native mode through presentation maintenance. Recycle Bin retains its cached mode outside explicit hints. | Another recurring UI COM query. Explicit command snapshots and native hints already bypass the cache. Establish coverage for external/programmatic view changes, then make cache invalidation authoritative rather than replacing 600 with a larger number. |
| High | `OverlayProc(WM_TIMER)` / `ScanShellWindow`: normal available views rescan at 4.5 s; unavailable/out-of-scope views at 800 ms, with faster navigation recovery. | Repeated Shell discovery can block the STA. Limit recovery to unresolved navigation or missing subscriptions once actual live subscription availability is known. A `*Probed` flag can mean unsupported; it cannot prove that notifications are connected. |
| Medium | `TabVisibilityObserverProc`: HWND inspection every 50 ms while Explorer is foreground, 350 ms otherwise; 25 ms during diagnostic input tracing. | Permanent observation overlaps frame/tab WinEvents and dispatcher identity checks. It avoids Shell COM but the intervals are empirical. A live tab-switch trace is needed to identify the missing signal before consolidating observers; removing it blindly risks leaving the wrong tab visible. |
| Medium | `ProbeNativeSelectionOnIdle`: 240 ms warm-start threshold, otherwise 850 ms for a nonempty selection or 1.2 s for empty; several interaction/provider guards. | Source explicitly retains this for programmatic selections without a native notification. Nonempty selections can trigger identity reads, not just a count check. Test external Show in Folder, equal-count replacement, delayed restoration, and absent automation subscriptions on live Explorer before making this notification-only. |
| Medium | Main overlay timer: normally 250 ms active, 1.25 s inactive. | Mixes unconditional discovery/reconciliation with pending-result delivery, watcher handling, subscription recovery and layout readiness. A larger global interval would hide costs and slow valid work. Future work should wake each pending category from its owner event and retain recovery only where a signal is unavailable. |

These remain audit findings, not claims that notification coverage has been
proven. Existing substituted-COM tests cannot establish which events a real
Explorer build or third-party provider emits.

## Conditional work retained

| Mechanism | Why it remains / limit |
| --- | --- |
| This PC capacity, 60 s | Runs off the UI thread. Arbitrary disk use need not produce a Shell free-space hint. The rate is a freshness policy, not a Windows requirement; changing it needs a freshness decision. `SHCNE_FREESPACE` already has a separate coalesced capacity path, with 100 ms burst coalescing and 1 s minimum spacing. Discovery recovery remains conditional on a missing native reorder subscription, rather than piggybacking on every capacity update. |
| Directory fallback, 5 s | Applies when the filesystem watcher is unavailable and no namespace notification registration exists; ordinary watched folders use notifications. Initial loading also queues when no work is pending. |
| Navigation readiness, 75/100 ms fast recovery and 700 ms fast window | Begins with navigation signals; the window changes retry cadence, never rolls back a slow provider on a timeout. Explicit completion/failure owns the outcome. |
| Cold attachment, 16..1000 ms backoff | Only unresolved startup HWNDs are retried; no pending work means an infinite event wait. Attachment, an unsupported resolved target or HWND destruction ends the request. Backoff remains empirical recovery policy. |
| Native columns, 350 ms; width restoration, 650 ms | Gated by dirty/unavailable columns or pending restoration; success clears work. Cooldowns are readiness/retry policy, not background polling of settled columns. |
| Shell subscriptions, 1 s plus binding backoff | Only unresolved subscription/binding paths call providers again; terminal unsupported and successful states are distinguished in the relevant adapter. |
| Selection restoration and new-item editor recovery | Pending user intent only; bounded direct reads, cancellation generations and owner/focus guards must survive any scheduling refactor. No elapsed timeout may override a legitimate slow operation. |
| Background reaper, 50 ms | Only while stopped workers remain unretired; otherwise waits indefinitely. A worker's last wake can precede its actual OS thread exit, so deleting the retry can lose completion. Thread-completion signaling is a possible replacement, with callback draining/DLL lifetime validation. |
| DWM/animation waits | Normal pacing uses compositor/refresh and pending frames. The 8 ms sleep is an error yield after failed `DwmFlush`; the 1 ms wait prevents a suspended compositor busy loop. These are not property refresh policies. |
| Shutdown drain waits, 1/25 ms | Unload waits for actual callbacks/owner-STA cleanup rather than terminating providers. Completion events could improve waiting; removing waits would permit executing unloaded code. |
| Input timeouts, debounce and frame budgets | Type-search expiry, zoom fitting, animation durations and bounded delivery slices are interaction/scheduling policy. They do not independently sample system properties. |

Shell [change notifications](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify)
distinguish free-space, membership and item changes; a capacity sample is not a
device-discovery event. The audit follows those distinct existing queues.

## Verification

`--styles` counts actual style and Shell visibility API calls across six private
directory jobs and 48 maintenance passes. Sent frame broadcasts coalesce,
inactive tabs replay settings intent, and This PC updates visibility before
requesting rows. A forced-due production `WM_TIMER` blocks a substituted volume
probe on its background lane while asserting no native identity capture is
requested on a subscribed view; a drive-add hint and missing-subscription
recovery still request identities independently. No real drive, clipboard,
focus or Windows settings are changed. Existing rename tests now verify that
the substituted focus state is restored, replacing an assertion that depended
on the user's desktop foreground window staying unchanged during the test.

Validation: `build.bat -SourceFile 0.9.254.cpp` passed repository checks, the
Windhawk host DLL link/stack checks, the full C++ pipeline and DLL-unload suites,
and finalized 0.9.254. Offscreen checks do not establish live Explorer
responsiveness or notification completeness.
