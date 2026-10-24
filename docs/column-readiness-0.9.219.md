# Column readiness across the independent presentation bridge

The reported Recycle Bin tab was inspected read-only with the exact installed
0.9.218 source layout. The installed source hash matched the published archive;
the probe also checked the loaded DLL's version before reading ViewState.
`tests/live-columns-218.log` records one visible tab with 876 rows, Details mode,
loaded names, completed startup step 5, zero columns, dirty native columns,
no native column probe, and `deferColumnProviders=1`. An automatic resize fit
was pending with its resize flag set. Presentation sequence 771 had already
been accepted. No files, selection, focus, clipboard or process memory were
changed by the probe.

The resize request reached the overlay, but both native column acquisition and
automatic fitting waited on the stale provider-deferral flag. Directory/native
rebinds re-arm that flag. The dedicated renderer's completion consumer cleared
it only when the tab's lifetime first-content timestamp was still zero. Once a
tab had presented anything, a later rebind could leave the flag set forever.
The older direct renderer had separate first-content and provider-readiness
completion branches; that distinction was lost at the asynchronous boundary.

0.9.219 keeps the lifetime timestamp and releases provider deferral separately
for each accepted content generation. The renderer explicitly reports whether
the accepted frame contained visible names or a completed empty folder. Loading
frames cannot release providers or pin the native mode. Lifetime, view and scan
generation checks still reject departing/stale content. Settled presentation
also wakes the existing posted owner-STA column/startup continuation; provider
COM does not move into rendering or acknowledgement callbacks.

Testing exposed a second gap: an unchanged revalidation advances directoryToken
but could skip publishing that generation because no pixels changed. Receipts
from the old scan were then rejected indefinitely. Completed unchanged names
now invalidate the overlay for a coalesced model publication. The existing warm
device, swap chain and caches remain in use. Changed-content delivery keeps its
existing repaint path; no delay or periodic fitting timer is added.

The reproduction in `tests/column-bridge-negative-218.log` uses production
AttachView, ResetForNewDirectory, OverlayProc, host suppression, names workers
and the independent renderer. Native columns are initially unavailable. After
the first names image, the native view binds an empty destination and its column
provider becomes ready. On 0.9.218 the test records the same stuck state as the
live tab: loaded content, startup complete, zero columns, deferred providers,
dirty native columns and no native column probe.

`tests/column-bridge-smoke.h` covers:

- Loading versus accepted empty-content readiness, unchanged lifetime clocks,
  and stale view/scan acknowledgements.
- Cold/delayed column acquisition without a column toggle, empty/populated
  navigation and repeated unchanged revalidation with matching receipts.
- Five metadata columns plus Name; only the native frame and content host are
  resized. Wide/narrow/restored bounds must reach fitting and consume the
  available viewport without horizontal overflow where six minimum widths fit.
  Disabled automatic fitting must preserve native widths.
- Warm GPU device/swap-chain identity across unchanged generation publication.

These tests supply native Shell column responses, use private temporary folders
and offscreen windows, and substitute focus/clipboard APIs. They do not simulate
a maximize button click in the user's Explorer. The existing full option,
clipping, rendering, native Shell integration and DLL-unload suites supplement
these checks. Live displayed frame-rate validation is separate.

The read-only probe can be rerun with
`tests/run-column-probe.ps1 -SourceFile <matching source>`; it uses the discovered
Windhawk compiler and refuses layouts that do not match a loaded version. The
existing pipeline/UI-transition logger now includes `columns.providers.ready`
and `columns.autofit.resize` readiness events. Logging remains off by default.

Validation: the complete `build.bat -SourceFile 0.9.219.cpp` gate passed and
published the release; see `tests/build-0.9.219.log`. All 32 option contracts,
production attachment/resize cases, existing graphics/input tests and 21 actual
API-hook DLL-unload cycles pass. A pre-existing cleanup fixture now waits for
actual result consumption if the provider's icon lock was briefly busy, while
holding the cleanup lock to retain its synchronous-reclamation check. All 22
prior source archives retain their hashes. Published latest.cpp and the tested
0.9.219 archive have SHA-256
`95489E0E695F4BC49A7AA41B3318233788E0BD1A4C685DEC4EF94A686EA71A81`.
