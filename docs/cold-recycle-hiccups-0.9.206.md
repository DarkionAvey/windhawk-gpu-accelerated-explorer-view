# Remaining cold Recycle Bin hiccups

The post-0.9.205 capture contains 13.42 MB of pipeline/frame diagnostics. The
user reports that the severe lag is mostly gone, with remaining hiccups before
caches warm. This capture does record them:

- The active Recycle Bin has 825 rows on UI thread 34728. Its frame summaries
  report maximum intervals of 335.09 ms and 239.50 ms while paint work remains
  around 6 ms or less. These are submission intervals, not scan-out FPS.
- Several `DirectUIHWND` updates on that same thread take 36–59 ms each. They
  process bursts of 588–600 structure changes. The existing batching delivers
  one final notification in less than the diagnostic's 1 ms resolution.
- Opening the folder also includes 104 ms in the navigation tree's mouse
  dispatch and 225 ms in the native tab's navigation dispatch. The capture does
  not identify the internal functions responsible for every such call.
- The 203–407 ms timer warnings for HWND `0x306A0` concern `Shell_TrayWnd` on a
  separate UI thread, not the affected file view. They are not evidence of a
  Recycle Bin rendering stall.

Pipeline records must be ordered by QPC and thread/owner, not their printed
wall-clock prefix: independent asynchronous writers flush at different times.
Dropped records also prevent treating the event list as a complete timeline.
Background icon/provider jobs are not UI stalls merely because they take longer
than one display interval. The capture shows no large icon-upload spike.

## Additional live sample

The user reproduced a fresh-tab/scroll case. Their old window had closed, so
the sampler located the new Explorer window's UI thread, 47792, in the same
Explorer process. It captured 900 stack snapshots, accumulating 44.84 ms of
pause time with a 0.123 ms maximum pause. Thread resume precedes unwinding and
symbol work; no process writes, injection, input, selection or file operation
is used.

The sample contains DirectUI provider `DoInvoke`/`SendMessage` underneath this
mod's structure-event hook, confirming that root identification itself can
re-enter the native view and installed message-hook chain. Other native stacks
show row/property construction, desired-size/font measurement, and normal
navigation-tree work. GPU Present/driver waits and idle message waits are also
present; their inclusive sample counts are not cold-hitch durations or proof
of a driver defect. Mod symbols resolved only to nearby exports must not be
read as actual calls to those export names.

## Change

Reuse the exact, strongly retained fragment root already verified against the
owned suppressed native HWND for this actual dispatch. Repeated notifications
from that root do not call `get_FragmentRoot` again. The cache ends with the
dispatch, rather than waiting for a timer or persisting across navigation.

Child providers still resolve their root on each event. Their identity alone
does not prove they have not been reparented. Foreign providers/windows,
generation changes, reentrant replacement, fallback and unload retain the
existing checks and behavior. Singleton event/runtime-ID semantics and final
bulk accessibility notification are preserved.

Aggregate diagnostics now distinguish lookup count, exact-root reuse count,
lookup elapsed time and the native scope's elapsed time from final notification
delivery. `native.uia.resolve[queries,rootHits,lookupMs,scopeMs]` uses the existing
per-tab trace transport; frame-time batch logging includes the same fields.
Timing queries run only with pipeline or frame-time diagnostics enabled. All
logging defaults remain off.

## Validation limits

The private provider test replays a 600-event root burst, checks that just one
root/HWND lookup occurs, and confirms that child/foreign providers still take
their required ownership paths. It validates the aggregate timing/count record
as well as final bulk notification. The existing three-real-view, MTA client,
cached-data and unload tests continue to exercise the shared hook.

The first full gate passed the new UIA tests but failed the existing synthetic
Recycle Bin F5 check before its property epoch advanced. That test supplied no
modifier state; the real refresh shortcut deliberately rejects Alt+F5. It now
uses the same private-thread keyboard-table save/neutralize/restore pattern as
the other synthetic key tests. Acceptance still must advance the real property
epoch synchronously, and the asynchronous namespace refresh must actually
complete. No production refresh gate or assertion was relaxed.

This removes confirmed redundant work inside the native burst. It does not
establish that all native navigation, row/font construction or every reported
frame gap is eliminated. The new metrics separate that remaining native cost
from provider identification; live behavior after reloading this release must
be observed independently of offscreen component measurements. Final release
validation is recorded in `CHANGELOG.md`.
