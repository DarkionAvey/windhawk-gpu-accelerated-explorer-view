# Building and testing

Requires Windows 11 x64 and a Windhawk installation containing its bundled
Compiler and x64 Engine import library. No Python, Codex runtime, fixed drive
letter or user-specific folder is required by the build or regression scripts.

## Release command

Run `build.bat` from the repository, or invoke it by its full path from any
working directory. It selects the newest numbered working source; if none
exists it copies the newest source into the next patch version. To start a
version before editing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/new-version.ps1
# Edit the new numbered source, then:
.\build.bat -SourceFile 0.9.197.cpp
```

The filename above is an example; use the version the starter actually creates.
The release command embeds root README.md in the working source, runs repository
checks, links the real Windhawk host DLL, and runs the full standalone and
DLL-unload suites. Only after success does it move root numbered
sources (and legacy mod.cpp, if present) into previous-versions and copy the
tested release to latest.cpp. It then keeps only the 15 newest source versions
in previous-versions, including the published version, ordered by numeric version.
The legacy mod.cpp baseline counts toward this limit; unrelated files and
subdirectories are left alone. The published release's archive and latest copy
are byte-identical.
Conflicting archives and downgrades fail without overwriting history. A failed
run retains the working source and temporary binaries, and leaves latest.cpp
unchanged, without pruning archives. Update CHANGELOG.md with the results after
testing.

## Toolchain discovery

Use `-WindhawkDirectory "path\to\Windhawk"` for either script, or set
`WINDHAWK_HOME` to the installation root (the directory containing Compiler
and Engine). Otherwise discovery checks registered installations, windhawk.exe
on PATH, and Windows' Program Files locations. Invalid explicit configuration
fails instead of silently falling back to another installation. Paths may
contain spaces or Unicode. Relative source paths are rooted at the repository.

## Focused or historical tests

`--touch` feeds calibrated contacts through the production input boundary into
the real Windows Interaction Context recognizer. A private synthetic digitizer
supplies calibration; it never injects input into the desktop. No gesture
outputs or Windows input messages are manufactured. Coverage includes taps,
six double-tap activation cycles (varied system-relative intervals, positions,
press durations and delayed owner dispatch), timed and delayed-delivery holds,
Shell drag identities, bulk-selection release races, vertical pan with
1/2/3/5 contacts, one-finger marquee commit/rollback and contact changes,
two-finger column scrolling followed by edge-only history navigation through
the owning Shell browser, reversal before release, staggered/unequal two-finger
swipes, spring-driven bubble/chevron stretch, threshold continuity and vertical
follow without changing a locked history action, radial pinch/expand at different
separations and orientations, exclusive
pinch anchoring across layouts/DPIs/RTL, contact replacement/rotation rejection,
vertical marquee in fitting directories, release-velocity flings, coalesced frames, capture
loss, divider fitting, animated scrollbar targets, persistent input ownership
and Explorer click/underline preferences. Scrollbar eligibility stays with the
original DOWN hit, including header/body entry, two-finger swipes, a remaining
contact, and simultaneous/staggered starts on the bars. Offscreen text checks also compare
underlined/plain mask variants with DirectWrite. Desktop feedback, modal
dragging, clipboard and focus are
substituted. DLL-unload tests also retire an actual native recognizer with a
hold deadline armed. OS message delivery and visual/device parity with live
Explorer require hardware validation; these are component tests.

Fling trajectories use frame-paced contacts with real QPC timestamps, native
release velocity and effective Windows deceleration. They run the real owner
message queue, compositor clock and presentation lane through the full stopping
distance, including renderer acknowledgement. A short fast flick must travel
farther than a longer slow drag. Successive flings add decaying momentum in both
vertical directions; new contact catches the list, and reversal/axis changes
discard unrelated momentum. Checks cover 96/192 DPI and LTR/RTL column scrolling.

Touch rendering checks also capture the real immutable presentation model and
paint it into a GPU target. They read marquee pixels in empty content starting
with focus on Explorer chrome, including perfectly vertical drags and foreground
loss. Gradual pinch checks require identical row/icon/text pixels at recognition
and fractional scaling only after further movement; list/grid and RTL are covered.
Scrollbar checks cover independent growth/reversal, large idle hit targets,
track leave, captured drags outside the track, capture loss and exact return to
idle pixels through the presentation model at 96/192 DPI in LTR/RTL.

Navigation panel checks keep both real-recognizer contacts inside 240-pixel-wide,
1,000-pixel-tall private windows at 96/192 DPI in LTR/RTL. They verify the one-third
commit cap, position continuity at activation, actual accent-colored GPU pixels,
single-opacity chevron joints in fractional entry and active poses,
zero color API calls during input/animation/paint, release-only navigation and
animation completion through the folder-reset path. Spring checks compare
different frame subdivisions, not a second implementation of the equation.
The existing style-event checks verify the accent update and translucent glyph
contrast. A private-fixture BMP records the active panel for visual inspection.

`--status-buttons` checks measured native/virtual controls, pending and warm
frame translations, actual Windows boundary feedback when enabled, stale resize
results and hidden/restored anchors. `--rtl-windows` also checks actual toggle
placement and measurement reuse through native moves in mirrored/LTR hosts.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-regressions.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-regressions.ps1 -SourceFile previous-versions/0.9.195.cpp -TestArguments --selection
powershell -NoProfile -ExecutionPolicy Bypass -File tests/repository-regression.ps1
```

The default source is latest.cpp. `--icon-alpha` compares real Shell icon decoding
with independent native bitmap bytes and transparent PNG references, plus GPU edge pixels
on dark/light backgrounds, RTL/LTR, Cut opacity and 24..256px sizes. Set
`GPU_OVERLAY_ICON_ALPHA_PREVIEW` to a PNG output path for a comparison image.
See the [alpha investigation](../docs/icon-alpha-0.9.240.md).
`--icon-sizing` exercises a private native ICO through the actual image-list
worker while its image factory supplies a framed fallback. It checks list zoom
cache reuse, presentation handoff, GPU target recreation and offscreen grid/list
fitting. Manual read-only `icon-source-probe.cpp` and `icon-pipeline-probe.cpp`
accept an explicit source item and private output location; normal tests never
read user folders. See the [sizing investigation](../docs/icon-sizing-0.9.257.md).
`--shell-provider` compares a private folder shortcut's items with its Shell
provider, including native identities, metadata, selection, hidden and empty
targets, sorting, and target change watching. It also runs under `--filesystem`.
`--provider-navigation` exercises real private provider hierarchies, including
nested archives, deep and empty folders, Unicode, return navigation, native/GPU
toggles, refresh, column catalogs, PIDL watches, and stale selection cancellation.
Default menus are native; command invocation is captured. Deeper archive roots
use private materialized copies so no associated application is launched.

`--paste-focus` routes archive Copy and frame/GPU Paste through a private
clipboard stand-in backed by a real virtual Shell data object and stream.
It asserts one command for a queued key across repeated accelerator tables,
dispatch, WndProc and reentrant menu resolution/invocation. Release/repress in
one tick, dispatch-only input, pending command deduplication, independent tabs,
empty/loading destinations, selected folders and editor/modal/stale-view guards
are covered without changing the user's clipboard or focus. Native command
invocations are intercepted. The corrected 0.9.250 test fails on 0.9.249;
the old test incorrectly expected duplicate invocations for the same message.

`--profile-identity-probe` is an explicit read-only OS integration probe of an
already-open Explorer profile view, discovered through Windows APIs. It checks
the actual names worker against the native provider and performs no selection,
navigation, focus changes or commands in that view. It is excluded from the
default private-fixture suite. See the [binding investigation](../docs/profile-provider-0.9.271.md).

`--provider-selection` uses real offscreen ExplorerBrowser views of private
archives, delegated folders and ordinary directories. It checks selection and
focus in Details/List/grid, repeated echoes, external and bulk selection, and
GPU context-menu preparation with popup tracking intercepted. `--selection`
also checks mixed path/opaque provider keys and alternate canonical PIDLs.
`--drive-bars` checks remaining-space inline tracks, wide/narrow resizing,
intrinsic label/font updates, fitting at standard/custom DPI, canonical
source versus live column bounds, moved Name columns, LTR/RTL, reversal and the
settled handoff, plus offscreen GPU source pixels. See the
[storage-bar investigation](../docs/drive-bars-0.9.243.md) and
[remaining-space refinement](../docs/drive-bars-0.9.244.md).
`--smart-refresh` checks retained metadata with blocked providers, unchanged
results without repaint, changed and empty values, stale-result cancellation,
and independent This PC capacity updates. These are private component tests;
they do not operate a live Explorer context menu.
`--large-tiles` checks drawing backpressure at
one large icon per row, GPU sampling times and LTR/RTL Cut pixels; see the
[large tile report](../docs/large-tile-rendering-0.9.239.md). These are private
offscreen tests, not measurements of live Explorer or displayed frame rates.
`--dense-rows` publishes successive insertion scenes for a private 4,000-item
grid at 4,751 × 1,958 in LTR and RTL. It measures scene construction and DComp
commit separately, checks viewport culling and retargeted hit geometry, and
does not operate Explorer or measure displayed frame rates. It also runs as
part of `--dense-grid` and the full suite.

`--dense-grid` checks a private 1,003-item grid at the supplied log's
4,751 by 1,958 viewport size, including wrapped/trimmed names, selection,
fractional scrolling and LTR/RTL mask reuse without cache growth. It also
checks 96/144/192 DPI and measures cached icon lookup for long generated
provider identities. These are offscreen CPU submission and lookup
measurements, not displayed frame rates.
It also checks private provider frames with 1,024-character identities and
512 cached icons, exact size-tier keys, rehashing, reserved folder fallbacks,
and the original case/Unicode path comparison behavior.
Render-stage measurements separate opacity tracking, icons and labels, and
include a 24-step wrapped-label zoom. `--graphics` also compares resized native
layouts with fresh-layout pixels for fractional font sizes, wrapping,
middle trimming, opacity and RTL. See the
[165 Hz investigation](../docs/dense-grid-0.9.260.md).
`--animation-continuity` runs the grid-zoom and view-mode retarget checks; their
instantaneous geometry comparisons use a scoped test clock without changing
production animation timing.
`--options` checks all exposed settings,
clipping measurements and real asynchronous resize fitting; see the
[coverage notes](../docs/options-regression-0.9.218.md). Supported focused suites are --selection,
--graphics, --scrollbar-input, --navigation, --rtl-windows, --native-viewport, --live-sort and
--unload. Focused runs never publish or archive. The runner builds from a
selected-source header rather than shell-escaped macro strings, so source
selection works in Windows PowerShell 5.1 and PowerShell 7.

`--styles` checks native foreground precedence when theme/app backgrounds
disagree, repeated maintenance/settings refreshes, transient/invalid lookups,
high contrast, independent tab caches, malformed fonts, invalid sizes/DPI and
real offscreen text/chrome pixels. `--options` also checks failed accent and
wheel lookups. See the [foreground correction](../docs/style-fallbacks-0.9.252.md)
and [original style audit](../docs/style-fallbacks-0.9.251.md).

It also counts color-provider calls during six real private directory jobs and
48 maintenance passes, checks coalesced system/theme/accent/dark-mode and mod
settings events, and verifies tabs update with the toggle button disabled.
See [event-driven style resolution](../docs/style-events-0.9.253.md).

It also asserts no periodic Shell visibility queries, coalesced sent settings
broadcasts, deferred inactive/animating tab updates and This PC visibility
reconciliation. A due production capacity timer uses a blocked synthetic volume
to check that capacity work stays off the UI and does not request device
identities on subscribed views; drive events and missing-subscription recovery
still request them. Rename focus-restoration checks use substituted state and
do not depend on the desktop foreground window staying unchanged. See the
[polling and retry audit](../docs/polling-audit-0.9.254.md) for remaining findings.

`--column-bridge` checks cold/delayed native column acquisition, per-directory
content acknowledgements, unchanged scan publication and native frame/host
resizing through actual attachment. Published-model checks reject provisional
horizontal overflow while native column discovery/fitting settles; the vertical
geometry remains available. See the
[bridge investigation](../docs/column-readiness-0.9.219.md). The separate
`run-column-probe.ps1 -SourceFile <matching source>` performs an intentional
read-only inspection of running overlay column/readiness state. It requires a
matching loaded DLL version and never injects or sends input/window messages.

`--selection-ui` checks 1/2/64-item native selection resolution on tab-owned
workers, blocked-tab independence, silent equal-count replacement, deferred
Delete, navigation cancellation, immediate empty selection and unchanged idle
imports without repaint. It also measures duplicate mode reads and checks
pinned layouts and reentrant view replacement. `--selection-ui-mode` runs the
mode checks alone.
`--selection-ui-navigation` follows these checks with real private provider
navigation to expose interactions between queues and fixtures.
See the [UI Shell call refinement](../docs/selection-ui-0.9.255.md).

Tests do not install the mod or inject into Explorer. They create private
temporary fixtures and offscreen windows, substitute clipboard/focus APIs, and
never implicitly inspect a local D: drive or user folder. OS icon/SysWOW64 and
Recycle Bin integration probes discover their real locations and are read-only.
Graphics availability can affect those checks; CPU submission timings are not
measurements of frames displayed by the monitor. Binaries are removed on success
and retained at the printed temporary path on failure.

The optional `inspect-crash-dump.py <dump> [matching-DLL-directory]` helper uses
Python 3's standard library. Python is not needed for the build or tests.
[Pipeline tracing](PIPELINE-TRACING.md), [UI diagnostics](UI-TRANSITION-DIAGNOSTICS.md)
and [release history](../CHANGELOG.md) contain investigation details.

## Coverage
The harness covers:

- Thirty independent directory workers while ninety layout, property and icon workers are deliberately blocked. The test also fills more icon jobs than the former global queue allowed.
- Generation cancellation, preview coalescing, complete-result delivery, and asynchronous thread/context reclamation.
- Early names for a grouped/sorted folder, and navigation past a blocked layout provider.
- Real sorting/grouping, cached row/index restoration, and background filesystem watch installation and delivery.
- Repeated Name/Date/Type/Size sorts while the property lane is blocked: previously resolved cells remain available before paint, including known empty values, offscreen rows and folder-cache returns, without duplicate provider requests. Column generations reuse existing keys; a newly added property requests only that key. Changed file metadata and Shell UPDATEITEM hints still invalidate the affected row.
- Baseline native host suppression through 100 unpumped show/style/resize cycles, visibility restoration and ownership cleanup.
- Separate graphics devices and composition targets, font refresh without target recreation, and recovery isolated to one overlay. This check reports a skip if graphics initialization is unavailable.
- Page Up/Down and Home/End scroll through intermediate frames and finish within the shared 180 ms duration, including native selection echoes. GPU pixel readback verifies persistent hover as keyboard selection arrives and leaves, identical mouse/keyboard overlap, and combined opacity above or below selection opacity.
- Actual SetCapture/ReleaseCapture delivery through the window procedure plus GPU pixel readback: hovered selection remains visually identical before press and after repeated releases at both combined-opacity settings. A running hover transition retains its clock. Release outside, actual capture theft, and WM_MOUSELEAVE still remove hover.
- Ctrl+wheel and pinch use the same 180 ms zoom transition. Checks cover fractional row/font/icon geometry, rendering, grouped and ungrouped center anchoring, hit testing, rapid retargeting and reversal, saved tab destinations, independent overlays, synthetic wheel suppression during pinch, and cancellation on navigation or a metrics refresh. Header formats and graphics devices are reused.
- GPU pixel readback compares the zebra background before and during deletion/insertion. Bands stay fixed while contents move. With empty rows disabled, only the trailing height animates; cancelling foreground motion leaves that animation intact, and overlapping changes continue from the last submitted height.
- Marquee selection scrolls upward and downward, releases outside the viewport, and survives native capture/selection reentrancy. Tests verify complete native groups over 64 items, first-item-only Shell batch fallback, additive batch recovery in at most five native calls, canonical identity despite changed cached metadata, rejection of a different set with the same count, unchanged viewport on selection echoes, obsolete-job cancellation, truthful reconciliation after rejected native commits, lost-capture rollback, independent selection lanes, and coalescing 10,000 Shell events into one pending notification. The fixture has 512 real files. The cached-metadata test writes only to the suite's temporary fixture.
- Safe provisional double-clicks, stable input across appended prefixes, and Escape retaining GPU view while enumeration is incomplete.
- Real native rename EDIT controls grow and shrink from live text in LTR/RTL, respect column bounds even below the normal minimum width, retain caret/selection, and resize after replacement, Unicode text and undo. The original filename stays unchanged until commit. Repeated pointer clicks leave a current fade's clock and the native selection transaction untouched; a multi-selection still collapses, and a known native mismatch still receives repair.
- Escape clears a Cut clipboard through EmptyClipboard, including during preview, optimistic navigation and This PC loading. Clipboard updates restore another tab's normal opacity. Copy data is preserved. Busy/failed clipboard cancellation retries without erasing newer data; deferred Cut commands are removed while other deferred commands remain.
- Copy and Cut share icon wiggle and viewport pacing in LTR/RTL, including association fallback icons; only Cut fades content. External clipboard updates cover same-path operation changes, replacement/clear/non-file data, plain CF_HDROP and atomic 9,000-item captures interrupted by newer contents. Private clipboard fixtures preserve the user's clipboard. List/grid paints verify same-path Cut-to-Copy opacity restoration.
- Vertical thumb dragging honors instant item-step/smooth scrubbing. Checks cover item thresholds in Details/List/grid, sticky-heading offsets, collapsed groups, monotonic progress and final partial rows. Horizontal dragging stays direct in LTR/RTL with either setting; track clicks and wheel input retain smoothing. Captured-pointer checks cover withheld/stale messages, endpoints, release and independent tabs. The scrollbar-input suite replays rapid track-to-thumb presses (DOWN and DBLCLK), track double-clicks, provisional names/This PC/optimistic loading, captured releases outside the bar and selection isolation in a private offscreen window with substituted pointer/focus APIs. Filesystem scope checks use synthetic identities without accessing assumed drive letters/shares; ordinary attachment tests run without a scope override. Native-focus guard and rejection checks always exercise the enabled policy.
- Filesystem names from PIDLs without label/property providers, progressive rows before a blocked virtual-name resolver, identity-based command construction, cached metadata projection, and cancellation across navigation.
- Optional overlay services remain uninitialized until the first content submission; visible icon requests can start with the first names. The standalone GPU test prints cold and next-device setup times; these are component measurements, not Explorer tab latency.
- Synthetic Shell icon paths block individual providers while real HICON/WIC conversion, dispatch and GPU uploads remain active. Checks cover peer progress, associations despite a fully blocked extraction pool, other tabs, current-viewport priority, stale queue cancellation, navigation reuse, one retry for transient empty responses, a permanent-failure retry bound, and asynchronous pool retirement. Real read-only Windows EXEs are profiled with an empty mod cache and uncontrolled Shell/OS caches. GPU readback verifies association pixels until the per-file icon replaces them, batched uploads, and progress during structural animation.
- Synthetic property paths delay one row or volume while the real worker dispatch and UI merge paths run. Other rows complete from the same batch, volumes publish individually, and watch/reclamation/other-tab work progress. Checks cover distinct private pool handles, final completion followed by a late partial result, and callback retirement.
- Production `AttachView` and `OverlayProc` together: initial filename bounds without native columns, lifecycle hooks before first content, nested graphics messages, host-region reset on activation, temporary zero-size host layout, native/GPU mode changes, namespace enumeration while the native model remains empty and the maintenance timer is disabled, and This PC to the temporary fixture's filesystem root. Shell-view COM responses are substituted; the renderer and filesystem work are real.

Each version remains one installable Windhawk source file. Each overlay owns lazily started enumeration, layout, native names, selection identities, column catalogs, association icons, watch setup and cache-reclamation lanes. Three private Windows thread pools independently allow up to four per-file icon, property and capacity callbacks each. Pools have their own callback environments, work objects and queues; they never use the default process pool or another tab's capacity. Existing workers accept jobs without taking the global lifetime lock. Pool initialization starts OS threads under only the owning tab's gate. The shared regulator only retains, stops, joins and reclaims worker groups; it never schedules directory or provider work between tabs. A closed tab can remain retained while an uninterruptible Shell provider is inside its call. No thread is forcibly terminated, and DLL unload joins those calls before code is unloaded.

The 64-item threshold limits direct UI-thread parsing, not selection size. Larger groups resolve relative PIDLs on their overlay's selection lane, then use the same native commit and verifier as small selections. Old identities retire off the UI thread. Preparation is cancelled by a newer selection or navigation. Native selection is cleared during preparation so commands cannot act on the preceding group; pending Delete/Cut/Copy shortcuts and context menus resume after commit while their tab remains active. Commands use native canonical verbs with captured modifier flags; deferred menus retain the selected identities even if the viewport moved. Selected-PIDL enumeration uses bounded reads and a hash index, without walking unrelated native rows. [SHGetDataFromIDList](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetdatafromidlistw) reads cached PIDL information; [canonical CompareIDs](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-compareids) distinguishes identity from stale size/time data. Capture and autoscroll end before native COM calls. Owned selection echoes preserve the viewport; an intentional scroll clears that ownership so external reveals can work again.

Cold loads publish names at the first available entry and grow the provisional prefix to the captured viewport (up to 256 rows). Native column-provider queries wait for the first content submission. Sorting and grouping can replace provisional order later. Folder navigation can use a confirmed prefix; native selection and file mutations require a complete snapshot. Cached returns move vectors and use the existing path index for ancestor selection. Failed navigation rolls back through one reset path; a slow request is not rolled back merely because two seconds elapsed.

New-tab startup has an explicit first-content boundary shared by filesystem and native-backed views. Lifecycle hooks and native suppression are prerequisites; they are installed before optional setup and remain available for an initial native-only view mode. Names use viewport bounds until native columns become available. The first-content marker requires drawable content, rather than counting an invisible name snapshot. Visible icons can start with names. Clipboard, drag/drop, provider/event subscriptions, touchpad setup and drive-capacity probes follow in separately posted stages. Transient view-mode, geometry, suppression or item-readiness failures retry through composition pacing without changing the user's GPU preference. Graphics initialization rejects nested paint/resize transactions. Native host visibility is held independently of rendering. ShowWindow and ShowWindowAsync calls are constrained; direct and deferred positioning force HIDEWINDOW before USER applies them. A host subclass rejects WS_VISIBLE changes before layout. Region resets cannot expose the host. Overlay geometry follows the dedicated content host rather than requiring its hidden child list to finish layout. Unload removes host subclasses and drains active window callbacks before releasing DLL code. Newest-first Shell discovery has no 64-tab limit; discovery messages coalesce and a frame guard prevents duplicate overlays during nested Shell message loops.

This PC is enumerated through [IShellFolder::EnumObjects](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-enumobjects) on its own worker. There is no ItemCount/Item/view-cache readiness fence. Fast filesystem rows are published from their PIDL paths before virtual-name resolution; volume labels and capacity are background metadata. Rows retain both absolute and relative PIDLs, and selection/caret/menu/drop paths share one child-identity conversion. Opening a known drive does not require native row numbers. No logical-drive shortcut or cross-tab snapshot cache is introduced.

The supplied 0.9.113 log showed graphics ready in 16–31 ms, with first content at 125, 250 and 391 ms after attachment. The new `[GPU names] ENUM` and `RECEIVE` logs distinguish namespace execution, queue/delivery delay and first presentation. These stages support live diagnosis; standalone checks do not establish an Explorer latency improvement.

The harness verifies behavior and isolation, not Explorer's end-to-end latency or freedom from every visual artifact. Validate the mod in Explorer with repeated Ctrl+T into This PC, rapid Back/Forward, slow network/provider paths, multiple tabs, view toggles, and unload. Logs under `[GPU startup]` report `CORE_READY` (including GPU setup time), `FIRST_CONTENT` (attach-to-submit), and each deferred `FEATURE_STEP`; existing first-populated-paint logs track navigation separately. Use ETW/WPA for actual presentation and Shell timings. Nonblocking submission follows the [DXGI presentation contract](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present); driver work and filesystem/provider latency still require measurement.

The rebuilt 0.9.118 starts from 0.9.117; the previously rejected versions were discarded. It reuses per-tab DirectWrite layouts, starts structural transitions at first successful submission, and rebases overlapping directory snapshots onto submitted row geometry. Ghosts remain until the final layout submits successfully. Idle Shell probes and column fitting wait for structural movement to finish.

