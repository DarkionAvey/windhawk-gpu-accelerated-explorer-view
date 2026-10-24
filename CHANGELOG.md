# Changelog

## 0.9.237

- Remove the arbitrary 512px request/decode ceiling. Image demand follows
  physical icon size, zoom destination, visible viewport, available memory and
  the current rendering device's bitmap limit.
- Use dynamic resolution tiers and cheaper nearby previews, reuse adequate
  cached imagery, and keep the displayed fallback until its replacement is ready.
- Bound per-tab reservations and WIC pixel bytes. Sample available RAM on existing
  image workers and reject stale large requests after memory pressure changes.
- Cancel obsolete queued/in-flight requests without terminating providers.
  Avoid repeated larger extractions only after multiple requests confirm that
  the provider cannot supply more detail; shared Shell image-list fallbacks
  remain limited to their available 256px artwork.

See the [adaptive image policy and validation](docs/adaptive-images-0.9.237.md).
Validation: the full release build, repository regressions, linked Windhawk DLL,
C++/graphics/input and DLL-unload suites passed. Automated DPI/5K viewport
models exercise requests through 6144px; real Shell-factory/WIC/GPU transfer
checks exceed 512px. Live Explorer behavior on a physical 5K display remains
separate. Published source/export/archive hashes match; 40 prior archives are
unchanged.

## 0.9.236

- Replace the Logging section and six separate diagnostic switches with one
  top-level **Enable logging** option. It enables every existing diagnostic
  category together and defaults to off.
- Bound Shell column counts and labels, validate selected-PIDL enumeration
  batches, reject ambiguous navigation paths and cap incremental search input.
- Ignore non-finite scroll input, reject invalid scrollbar geometry, validate
  pixel strides/buffer sizes and keep successful image decode status accurate.
- Cancel failed, stale and disallowed drag/drop targets. Detach old drag state
  before outgoing callbacks so reentrant activity retains its own session.

See the [unexpected-input audit and fault checks](docs/unexpected-input-audit-0.9.236.md).
`build.bat -SourceFile 0.9.236.cpp` passes repository/export checks, real Windhawk
DLL linking, all 31 option contracts, the complete C++/offscreen graphics/input
suite, six fault groups (including 2,000 mixed scrollbar cases), and real DLL
unload/process-exit checks. Each fault group detects a failure in unchanged
0.9.235. The final full run also verifies non-owned enumeration slots are never
freed. Both published sources match the tested archive; all 39 earlier archives
remain unchanged. Live Explorer behavior has not been manually verified.

## 0.9.235

- Preserve the selected item's canonical path while Explorer catches up with a
  completed rename. Names refresh continues to remap selection by path; an empty
  intermediate native selection can no longer erase that pending identity.
- Verify both native selection and focused identity before completing the
  existing asynchronous restore. Boundary arrow keys retain outstanding
  verification; selecting a different item still supersedes it.
- Add optional **Logging > Selection identity diagnostics** with timestamped GPU
  and live native selection identities around rename, refresh, recovery and
  keyboard input. Diagnostics are disabled by default.

See the [selection identity investigation](docs/selection-identity-0.9.235.md).
`build.bat -SourceFile 0.9.235.cpp` passes repository/export checks, Windhawk
DLL linking, all 36 option contracts, the complete C++/offscreen graphics/input
suite and real DLL-unload/process-exit checks. The new delayed-readiness test
fails against unchanged 0.9.234 and passes after this correction, including
formatted GPU/native trace assertions. Both published sources match the tested
archive; all 38 earlier archives remain unchanged. Live Explorer behavior has
not been manually verified.

## 0.9.234

- Unify the editor-to-list focus transition across creation and rename routes.
  Preserve the selected file and keyboard caret while editing, reconcile Shell's
  selected identity before returning focus, and invalidate older selection imports.
- Keep renamed paths searchable before directory refresh with a sparse index
  update, carried with history and retired on the tab's existing background lane.
- Share list-focus ownership/fallback rules while respecting address/search
  controls, modal dialogs, foreground windows and the active tab.

See the [focus ownership investigation](docs/focus-ownership-0.9.234.md).
`build.bat -SourceFile 0.9.234.cpp` passes repository/export checks, Windhawk
DLL linking, all 35 option contracts, the complete C++/offscreen graphics/input
suite and real DLL-unload/process-exit checks. Focused rename and selection
suites pass; the old 0.9.233 source reproduces the new ownership regression.
Both published sources match the tested archive; all 37 earlier archives remain
unchanged. Live Explorer behavior has not been manually verified.

## 0.9.233

- Close the rename field and submit its replacement label before calling Shell,
  rather than waiting for a directory notification. Keep the pending name through
  reentrant directory deliveries; restore the actual name if Shell rejects it.
- Hand off hidden native inline editors from keyboard, toolbar and Shell New
  commands to the same visible GPU editor, preserving file identity, text and
  caret. Native editor focus no longer cancels a pending New command.
- Repair editor visibility and sibling order even when its rectangle is unchanged.

See the [rename handoff investigation and checks](docs/rename-handoff-0.9.233.md).
`build.bat -SourceFile 0.9.233.cpp` passes repository/export checks, Windhawk
DLL linking, all 35 option contracts, the complete C++/offscreen graphics/input
suite and real DLL-unload/process-exit checks. The focused rename suite also
passes. Both published sources match the tested archive; all 36 earlier
archives remain unchanged. Live Explorer appearance was not manually verified.

## 0.9.232

- Address the seven-point LLM report against current functions: stop history
  snapshots owning directory watches; suppress repeated discovery for resolved
  unsupported tabs; invoke selected files' native default command and working
  directory; correct frame subscription ownership and unload admission/draining;
  remove accelerator interpretation of unrelated window data; reject NaN Shell
  sort values; and explicitly release a process-exit-safe worker registry.
- Retain asynchronous watch teardown and names validation. History without a
  live watch revalidates before rows appear; metadata remains reusable after
  fresh names match. Native same-tab folder navigation is preserved.

See the [seven-point decisions and validation](docs/llm-report-0.9.232.md).
`build.bat -SourceFile 0.9.232.cpp` passes repository/export checks, Windhawk
DLL linking, all 35 option contracts, the complete C++/offscreen graphics/input
suite, real DLL-unload checks and the new blocked-callback process-exit check.
The report and watch-release checks, selected-group default activation and
namespace-aware history/navigation checks pass. Both published sources match
the tested archive; all 35 earlier archives remain unchanged.

## 0.9.231

- Add **Show native view toggle button**, enabled by default under Scrolling
  and navigation. It applies to existing and newly opened tabs; Ctrl+Alt+F9
  continues switching views while the button is hidden.
- Show the shortcut on a second line in both native/GPU switch tooltips.

`build.bat -SourceFile 0.9.231.cpp` passes repository/export checks, Windhawk
DLL linking, all 35 option contracts, the complete C++/offscreen graphics/input
suite and real DLL-unload checks. Attachment checks cover live setting changes,
fresh tabs with the button disabled, both multiline hints, and Ctrl+Alt+F9 in
both directions with the button hidden, without changing focus or activation.
The focused navigation suite also passes. Both published sources match the
tested archive; all 34 earlier archives remain unchanged. These are private
offscreen checks; live Explorer appearance was not manually verified.

## 0.9.230

- Remove the sharper grayscale text option. Previously saved `sharp` selections
  fall back to Windows default; smooth grayscale and aliased text remain available.
- Repaint the switch button's complete status-chrome surface on every paint.
  Remove transparent sibling ordering and destination-pixel copying, which could
  retain old hover/glyph pixels when Explorer's frame did not paint its backdrop.
- Reset hover on captured moves outside, capture loss, cancellation and tab
  visibility changes. Explicitly invalidate newly shown/repositioned switches.

See the [toggle repaint investigation](docs/toggle-repaint-0.9.230.md).
`build.bat -SourceFile 0.9.230.cpp` passes repository/export checks, Windhawk
DLL linking, all 34 option contracts, the complete C++/offscreen graphics/input
suite and actual DLL-unload checks. The new repaint and native-button state
checks pass in light/dark palettes. Both published sources match the tested
archive; all 33 earlier archives remain unchanged. An initial intermittent
sorting assertion and its successful reruns are recorded in the investigation.
Live Explorer appearance was not manually verified.

## 0.9.229

- Preserve transparent Explorer backgrounds and use native per-monitor
  DirectWrite tuning for text. Add Windows default, smooth grayscale, sharper
  grayscale and aliased choices, with consistent direct/cached/animated sources
  and lazy raster invalidation on rendering, monitor and system-font changes.
- Snap rendering primitives in physical device space, including translated
  row/column sources. Keep continuous layout/input/animation geometry and avoid
  a second bilinear filter on composited text/icon surfaces.
- Request correctly sized List/Details icons at high DPI and zoom. Snap image
  edges, preserve exact unscaled texels and use high-quality scaled icon filtering.
- Remove the switch button's synthetic idle background; reconstruct its actual
  parent backdrop and retain hover/press/focus feedback and composited icon alpha.
- Skip invisible fills, text, strokes and icons through shared draw guards,
  before shaping, cache lookups and optional image work. Transparent clearing
  operations still execute because they remove old pixels.

See the [rendering quality investigation](docs/render-quality-0.9.229.md).
`build.bat -SourceFile 0.9.229.cpp` passes repository/export checks, Windhawk
DLL linking, all 34 option contracts, the complete C++/offscreen graphics/input
suite and real DLL-unload checks, including 21 API-hook cycles. Pixel readback
also passes with the desktop's existing MacType injection overriding native text
choices. Both published sources match the tested archive; all 32 earlier archives
remain unchanged. Live Explorer appearance/frame rates were not measured.

## 0.9.228

- Export `gpu-accelerated-explorer-view.wh.cpp` alongside `latest.cpp` after
  the release gate; both files contain the identical tested source.
- Allocate dispatch scan state only for relevant Explorer frames. Reclaim
  exited threads' maps/handles on the existing lifetime regulator, preserving
  live-thread caches and trivial TLS pointers through DLL unload.
- Coordinate frame-thread startup, stop admission and handle teardown with
  the frame lock. Pass immutable wake handles to the frame and tab observers.
- Give the overlay a mod-specific class name and module instance. Reject
  class conflicts instead of reusing or replacing another registration.
- Add export/failure-guard, worker-race, thread-churn and real DLL class-ownership
  checks. See the [lifetime investigation](docs/lifetime-and-export-0.9.228.md).

`build.bat -SourceFile 0.9.228.cpp` passes repository/export guards, Windhawk
DLL linking, all 33 option contracts, complete C++/offscreen graphics/input and
real DLL-unload suites, including the new lifetime/class checks and 21 API-hook
unload cycles. Both published source files match the tested archive byte for
byte; all 31 earlier archives remain unchanged. Live Explorer frame rates were
not measured.

## 0.9.227

- Replace the rename editor's manual window-procedure installation/restoration
  with `SetWindowSubclass`, `DefSubclassProc` and `RemoveWindowSubclass`.
  Store the tab state in subclass reference data instead of EDIT user data.
- Preserve native EDIT destruction before releasing the session/font. Detach
  the callback and select a stock font if the editor survives overlay disposal.
- Cover LTR/RTL text, undo, caret, Enter/Escape epochs, external destruction,
  nested retirement and native messages after actual DLL unmapping.

See the [rename subclass investigation](docs/rename-subclass-0.9.227.md).
`build.bat -SourceFile 0.9.227.cpp` passes repository checks, Windhawk DLL
linking, all 33 option contracts, the full C++/offscreen graphics/input suite,
21 API-hook DLL-unload cycles and the retained-editor unload check. The
published latest.cpp matches its tested archive; all 30 earlier archives are
unchanged. These checks do not measure live Explorer's displayed frame rate.

## 0.9.226

- Keep asynchronous status-button discovery retryable when its submission or
  completion queue is briefly contended; coalesce an owner-thread continuation
  instead of leaving a completed measurement unused.
- Bound chrome discovery when a native content viewport is zero-sized or
  partially initialized, so it cannot descend into the directory's UIA tree.
- Add deterministic queue-contention and partial-viewport regressions. Repeat
  the read-only production probe against the desktop's actual visible controls:
  two 41×41 buttons, zero native gap, and a matching non-overlapping placement.

`build.bat -SourceFile 0.9.226.cpp` passes repository checks, Windhawk DLL
linking, all 33 option contracts, complete C++/offscreen graphics/input and
21 real DLL-unload cycles. Contention and partial-viewport checks pass in the
full gate; callback stacks remain below 8 KiB (maximum 1,960 bytes).
latest.cpp matches the tested archive; all 29 earlier archives remain unchanged.
See the [status geometry investigation](docs/status-toggle-0.9.225.md).

## 0.9.225

- Change the Grid text size default to 42%; preserve the separate List/Details
  default and existing saved font preferences.
- Measure status-bar buttons exposed through HWNDs or virtual accessibility
  controls. Copy the nearest button's dimensions and vertical position and the
  actual peer gap, including zero-gap native buttons, with RTL/mirrored mapping.
- Query optional accessibility chrome on the existing tab-owned Columns lane;
  publish plain geometry snapshots with generation/lifetime guards. Cache
  unchanged geometry and invalidate it on frame/layout/DPI/theme/control changes.
- Use DPI-aware physical coordinates for accessibility proxies, and retain a
  bounded fallback for hidden/unavailable controls or pending discovery. Add
  source/bounds diagnostics under the existing Performance logging option.
- Cover non-HWND controls using real private toolbar accessibility, resized/
  superseded requests, hidden/restored buttons, DPI/RTL/spacing, and actual
  native HWND/mirrored attachment checks. The live production-code probe finds
  the desktop's real virtual controls; it does not exercise the fallback.

`build.bat -SourceFile 0.9.225.cpp` passed all repository, DLL-link, 33-option,
C++/offscreen graphics/input and 21 real DLL-unload checks before publication.
See the [measurement and implementation notes](docs/status-toggle-0.9.225.md).

## 0.9.224

- Separate List/Details and grid text-size percentages. List/Details keeps
  its existing saved setting and 52% default; grid captions and drive tiles
  have an independent setting defaulting to 32%. Both retain the 20–80% range.
- Select the percentage in shared metrics so zoom, layout switching, DPI and
  independent presentation use the current view's setting. Preserve the grid
  caption cap at very large image sizes.
- Extend option contracts and layout/DirectWrite consumers to cover separate
  percentages, repeated List/Details/icon/small-icon switches and large grid
  zoom at 96/144/192 DPI. Update native view-switch font checks for both values.
- Initialize graphics fixtures with matching metrics/formats, cover short and
  wrapped labels, and check drive-tile navigation against responsive packing.
  Isolate the This PC refresh fixture's keyboard modifiers and verify both
  Alt+F5 rejection and unmodified F5 refresh on the private test thread.

Focused option, graphics and drive-refresh suites pass, as does
`build.bat -SourceFile 0.9.224.cpp`: repository checks, Windhawk DLL link, all
33 option contracts, full C++/offscreen graphics/input and 21 real DLL-unload
cycles. Callback stacks remain below 8 KiB (maximum 1,960 bytes). The tested
release is published as latest.cpp and its identical 0.9.224 archive; all
27 earlier archives are unchanged. These checks do not measure live Explorer's
displayed frame rate. Detailed local logs are in `tests/options-0.9.224.log`,
`tests/graphics-0.9.224.log`, `tests/drive-refresh-0.9.224.log` and
`tests/build-0.9.224.log`; the failed fixture runs are also retained.

## 0.9.223

- Fix the intermittent offscreen group regression: wait for the current
  renderer intent's explicit completion instead of treating a nearly-final
  document height as proof that animation has finished. Retain the final
  native/input geometry assertion.
- Share group advancement and raw-progress completion between presentation
  and input. Acknowledge the renderer's actual geometry sample so delayed paint
  or receipt delivery cannot advance input geometry ahead of the image.
- Add deterministic collapse/expansion boundary, delayed receipt and reversed
  intent checks for LTR/RTL Details/grid, alongside the offscreen GPU test.

See the [failure investigation and validation](docs/group-animation-completion-0.9.223.md).
The focused presentation regression and `build.bat -SourceFile 0.9.223.cpp`
pass: repository checks, Windhawk DLL linking, all 32 option contracts, the
complete C++/offscreen graphics/input suite and 21 real DLL unload cycles.
Critical callback stacks remain below 8 KiB (maximum 1,960 bytes). The build
published 0.9.223; latest.cpp matches its archive, and all 26 older archives
remain unchanged. No live Explorer frame-rate claim is made.

## 0.9.222

- Commit namespace membership through the same snapshot preparation, identity
  matching, selection/index reconciliation and structural animation path as
  filesystem changes. This PC drive/device additions, removals and replacements
  retain surviving items instead of recreating their rows; empty results use
  normal exit/entrance motion. No hardware-specific animation trigger is added.
- Preserve headers and use the shared 2D transition for structural changes in
  an already-grouped Details view. Include identity in order fingerprints so
  equal-caption, equal-count replacements are still reconciled.
- Request fresh backend identities directly from drive/media membership hints;
  remove fixed-delay native snapshot retries. Keep capacity requests independent
  of names completion, and retain committed rows after failed provider refreshes.
- Add backend-to-presentation tests for LTR/RTL Details/grid, grouping, sorting,
  surviving/removed identities, partial/failed/unchanged/empty snapshots,
  selection, capacity enrichment, upstream hints and an unpumped owner.
  Cover ordinary grouped filesystem insertion/removal through the same pipeline.

See the [log investigation and shared snapshot changes](docs/namespace-mutations-0.9.222.md).
The archived 0.9.221 regression reproduces surviving items being recreated as
deleted ghosts. The working release passes the focused mutation regression.
`build.bat -SourceFile 0.9.222.cpp` passed the complete repository, Windhawk DLL
linking, all 32 option contracts, C++/offscreen graphics/input and actual DLL
unload gates, then published the tested release. The gate caught and prevented
publication of a cold namespace startup-order regression; explicit names-only
commit permission fixes it. All 21 API-hook unload cycles pass; critical callback
stacks stay below 8 KiB (maximum 1,960 bytes). All 25 prior source archives are
unchanged; latest.cpp matches the tested 0.9.222 archive. Live USB insertion/
ejection was not performed; private snapshots cover both membership directions.

## 0.9.221

- Omit Office owner/lock files (non-folder names beginning with `~$`) in the
  existing filesystem enumeration filter, including when hidden files are shown
  or the owner file has not received its hidden attribute yet. Exclude them
  before previews, layout, row indices, caches and selection are constructed.
- Use two leading-character comparisons on the already-enumerated name, with
  no added filesystem/Shell queries, allocations, scan or paint/input work.
  Preserve similarly named directories and normal hidden/temporary files.
- Add asynchronous enumeration, hidden/protected policy, contiguous identity,
  End, native bulk selection, lock creation/removal and cache regressions, plus
  an isolated 100,000-record CPU comparison for the filename predicate.

The archived 0.9.220 negative control reproduces the unwanted rows; the working
release passes the focused regression. See the
[filter and performance notes](docs/office-lock-filter-0.9.221.md).
`build.bat -SourceFile 0.9.221.cpp` passed the complete repository, Windhawk DLL
linking, all 32 option contracts, C++/offscreen graphics/input and actual DLL
unload gates, then published the tested release. The optimized CPU-only profile
measured 0.166 ms without the name check and 0.1731 ms with it over 100,000 cached
enumeration records (0.0071 ms difference in this run, not live Explorer latency).
All 21 API-hook unload cycles pass; critical callback stacks stay below 8 KiB
(maximum 1,960 bytes). All 24 prior source archives are unchanged; latest.cpp
matches the tested 0.9.221 archive.

## 0.9.220

- Give fitted columns readable minimum widths: 180 DIP for Name and 112 DIP
  for metadata, increased for the actual font, icon/text insets and ordinary
  header captions. Keep the existing horizontal scrollbar once those minima
  no longer fit; distribute available space again when the window widens.
- Preserve these floors through last-column scrollbar-clearance adjustment
  and integer rounding. Keep deliberate manual divider sizing available.
- Add DPI/font/LTR/RTL regressions for readable text/header room, narrow/wide
  layouts, empty folders, long captions, scrollbar thresholds and access to
  the last column. Update the real native-frame resize bridge regression to
  check readable overflow as well as wide-window fitting.

See the [width policy and validation](docs/column-readability-0.9.220.md).
The archived 0.9.219 negative control reproduces the collapse; the new focused
regression passes all eighteen density/font/direction combinations on 0.9.220.
`build.bat -SourceFile 0.9.220.cpp` passed the complete repository, Windhawk DLL
linking, all 32 option contracts, C++/offscreen graphics/input and actual
DLL-unload gates, then published the tested release. All 21 API-hook unload
cycles pass; callback stacks stay below 8 KiB (maximum 1,960 bytes). All 23 prior
source archives are unchanged; latest.cpp matches the tested 0.9.220 archive.
These checks use private/offscreen fixtures and are separate from live Explorer
display validation.

## 0.9.219

- Fix missing cold-tab headers/metadata and automatic fitting blocked after a
  directory/native rebind. Release column providers on each current content
  acknowledgement independently of the tab's first-ever presentation timestamp.
  Keep loading frames and stale view/scan receipts from licensing provider work.
- Publish completed unchanged scan generations to the independent renderer, so
  cancellation checks do not reject its acknowledgements indefinitely. Resume
  the existing owner-STA startup/column continuation on presentation settlement.
- Add production-attachment regressions for delayed native columns, empty and
  populated navigation, unchanged revalidation and six-column narrow/wide native
  frame resizing with automatic fitting enabled and disabled. Preserve warm GPU
  identities and add readiness events to the existing optional logger.

The [read-only live inspection and reproduction](docs/column-readiness-0.9.219.md)
identify the blocked flag in the user's 0.9.218 Recycle Bin tab and reproduce it
on the archived source. `build.bat -SourceFile 0.9.219.cpp` passed the complete
repository, Windhawk linking, all 32 option contracts, C++/offscreen graphics and
input suites, and actual DLL-unload gates, then published the tested release.
All 21 API-hook unload cycles pass; callback stacks remain below 8 KiB (maximum
1,960 bytes). The existing thumbnail cleanup test now tolerates a legitimate
nonblocking icon-lock deferral before checking cleanup ownership. All 22 prior
source archives are unchanged; latest.cpp matches the tested 0.9.219 archive.
The running 0.9.218 tab was inspected without mutation; live validation after
loading the new release remains separate.

## 0.9.218

- Restore automatic column fitting after window resizing, including changes
  smaller than 12 pixels. Resize requests redistribute the new viewport through
  the existing fitter; interactive resizing waits for its actual completion.
  Keep unchanged-size echoes inert and preserve readable columns on navigation.
- Measure sorted headers with their actual bold format in both fitting and the
  clipping preflight. Check warm ellipsized cells against intrinsic DirectWrite
  measurements in LTR/RTL.
- Add settings-host regression coverage for all 32 user options: exact exposed
  defaults/runtime keys, toggle directions, ranges/clamping, enum choices and
  fallbacks, font/color parsing and string ownership. Check shared consumers
  and real WM_SIZE-to-presentation completion, including small resizes with
  multiple ellipsized columns, enabled/disabled fitting and native size echoes.
  Schema changes now require a matching coverage update.

See the [investigation and coverage notes](docs/options-regression-0.9.218.md).
Validation: `build.bat -SourceFile 0.9.218.cpp` passed the complete repository,
Windhawk DLL linking, C++/offscreen graphics/input and actual DLL-unload suites
and published the tested release. The 0.9.217 negative control reproduces the
resize failure; the new option, clipping and asynchronous resize tests pass.
An attachment fixture now awaits an accepted image after a rapid native/GPU
toggle before asserting warm standby. Callback stacks remain below 8 KiB
(maximum 1,960 bytes); all 21 API-hook unload cycles pass. All 21 prior archived
sources retain their original hashes and latest.cpp matches the tested 0.9.218
archive. These are private/offscreen checks, not live Explorer scan-out tests.

## 0.9.217

- Separate each tab's Explorer/Shell bridge and authoritative model from a
  dedicated GPU presentation owner. Render and advance shared overlay
  animations without waiting for Explorer's UI dispatch, using immutable
  versioned snapshots and checked geometry acknowledgements.
- Share complete names models, image pixels, clipboard contents and selection
  pages; coalesce pending frames and bound viewport enrichment and image
  residency. Preserve warm tab images and existing provider/lifetime lanes.
- Synchronize completed zooms, row/column intent and compositor clocks,
  offscreen collapsed groups, fonts and filesystem-to-presentation telemetry.
  Publish logical feedback before native selection/file-operation calls.
- Fix frame-pulse cancellation losing its stop signal in the cadence wait;
  drain presentation workers before DLL unload and stamp worker failures with
  their overlay lifetime. Separate visibility completion from names refreshes
  and preserve cold-start capability before the first image exists.

See the [ownership and validation notes](docs/presentation-owner-0.9.217.md),
including the remaining native input/model-delivery boundary.

Validation: `build.bat -SourceFile 0.9.217.cpp` passed the complete repository,
Windhawk DLL, C++/graphics/input and actual DLL-unload gates and published the
tested source. The native-barrier DLL case now disables a live presentation
worker with queued motion before verifying DLL unmapping; all 21 API-hook
unload cycles also pass. Callback stack use remains below 8 KiB (maximum
1,960 bytes).

New private GPU-owner checks cover an unpumped UI thread, a blocked tab while
another renders, immutable 30,000-item models, independent scroll/zoom,
compositor-clock acknowledgements, LTR/RTL Details/grid/grouped delete/restore,
offscreen collapse, stale packets, cold hide/resume and names refresh during
standby. Native attachment, rapid navigation/mode reversal, selection and
no-detach file-command recovery pass. Preserved 0.9.216 fails the new ownership
contract with "presentation still runs on Explorer's owner thread".

Twenty private 4K presentation owners peak at 1,490.33 MiB local GPU memory and
return to 9.86 MiB after closure. The existing twenty-tab scene-stress suite
peaks at 2,054.76 MiB, retains 1,483.52 MiB in standby and also returns to
9.86 MiB. These are offscreen component/resource measurements, not live
Explorer frame rates. All twenty older archives and README retain their hashes;
published source and the tested archive match. Full output:
`tests/build-0.9.217.log`.

## 0.9.216

- Present ordinary delete/restore row movement, collapse and fades through
  DirectComposition, so an already-committed item animation continues while
  Explorer's owner STA is inside a native call. Preserve the existing input,
  selection, identity reconciliation and transition geometry.
- Pack feedback, icons and text into separate bounded sources, retain group
  and column header ordering, and reuse storage through streamed updates and
  scrolling. Sample current compositor progress for subsequent captures.
- Include the feedback atlas in GPU memory profiling and settle/release row
  scenes through the existing warm-tab retirement path.

See the [investigation and test scope](docs/row-compositor-0.9.216.md).

Validation: `build.bat -SourceFile 0.9.216.cpp` passed the complete repository,
Windhawk DLL, C++/graphics/input and 21 real DLL-unload gates and published the
tested source. New actual-GPU checks pass in all eight LTR/RTL Details/grid
and grouped combinations, including an unpumped owner, delayed replacement
commit, deleting a still-entering item, concurrent scrolling and warm standby.
The same new contract test fails on preserved 0.9.215 with "structural animation
still depends on owner-thread presentation". Twenty warm 4K tabs, ten retiring
live row scenes, peak at 2,054.76 MiB local GPU memory, retain 1,483.64 MiB in
standby and return to 9.86 MiB after closure. These are private component and
compositor-contract measurements, not displayed Explorer frame rates. All
nineteen older archives and README retain their hashes; published source and
the tested archive match. Full output: `tests/build-0.9.216.log`.

## 0.9.215

- Continue an already-started row animation at its current clock through
  streamed restore/delete updates. Multiple names commits between frames no
  longer pin row motion and fades to zero until the operation stops.
- Preserve outgoing grid/list rows across subsequent deletion batches,
  including deletion of the final grid item. Share indexed viewport matching
  between the transition paths to reduce animation setup work on the UI thread.
- Group all five logging switches under **Logging**, with matching nested
  runtime keys and every default still off. Keep the responsive filesystem
  refresh and input-completion frame paths from 0.9.213–0.9.214.

Validation: `build.bat -SourceFile 0.9.215.cpp` passed repository workflow,
Windhawk DLL linking, all C++/graphics/input checks and all 21 actual DLL
unloads. Ten streamed insert/delete commits pass in LTR/RTL list/grid; the same
test fails on preserved 0.9.214 with "insert burst reset its started animation
to unpainted". GPU overlapping-motion, native selection, filesystem/cache and
warm-tab lifecycle checks passed. Twenty warm 4K tabs retain 1,483.52 MiB local
GPU memory and return to 9.86 MiB after closure; these are private component
measurements, not live Explorer frame rates. All eighteen older archives and
README retain their hashes, and published source matches the tested archive.
Full output: `tests/build-0.9.215.log`. See the
[investigation](docs/streamed-row-motion-0.9.215.md) for both supplied traces,
the animation failure and the remaining native-dispatch limits.

## 0.9.214

- Resume a paced frame at actual input completion after it yields to queued
  keyboard/mouse/raw/touch/pointer input. Native restore posts can no longer
  keep that frame waiting for queue-idle WM_PAINT. Preserve input-first
  dispatch, one outstanding frame, view/lifetime guards and ordinary painting.
- Share the completion between dispatched input and consumed accelerators;
  unhandled accelerators leave it for actual input dispatch. Add
  `frame.resume.input[deferredMs]` under the existing pipelineTracing toggle.
- Retain the responsive filesystem update and validated-history behavior from
  0.9.213. See the [investigation](docs/restore-frame-starvation-0.9.214.md) for
  the recorded 951 ms scheduling gap and the separate native-call limitations.

Validation: `build.bat -SourceFile 0.9.214.cpp` passed repository workflow,
Windhawk DLL linking, the complete C++/graphics/input suite and all 21 real
DLL-unload cycles. Tests verify actual accelerator/dispatch completion and a
real GPU frame presenting with 128 native-style posts still queued for every
input mask; unhandled accelerators, obsolete view tokens and duplicate
completion remain guarded. Filesystem/cache response, concurrent motion and
warm-tab memory tests pass. All seventeen older archives and README retain
their hashes, and published source matches the tested archive. These are
private component/integration checks, not measured live Explorer frame rates.
Full output: `tests/build-0.9.214.log`.

## 0.9.213

- Reject changed filesystem history snapshots before displaying cached rows.
  Retain change handles/revisions so a folder emptied by a move or deletion
  cannot flash its former contents on return. Unchanged verified folders and
  warm tab surfaces retain their immediate restoration paths.
- Wake the existing tab watch lane on filesystem events. Start names refresh
  on the first notification, coalesce subsequent hints behind actual scan
  completion, and publish completed results without a quiet-period timer.
- Add correlated command, filesystem observer, scan, model commit and accepted
  presentation timings under the existing pipelineTracing toggle. No logging
  defaults change. See the [investigation](docs/file-operation-latency-0.9.213.md)
  for the supplied trace evidence and its limits.

Validation: `build.bat -SourceFile 0.9.213.cpp` passed repository workflow,
Windhawk DLL linking, the complete C++/graphics/input suite and all 21 real
DLL-unload cycles. Private filesystem tests cover direct notification delivery,
scan-completion coalescing, hidden-tab deferral, changed/unverified snapshot
rejection and empty-folder publication. Grouped and interrupted cached navigation,
selection recovery and warm-tab memory checks passed. Twenty warm 4K tabs retain
1,483.52 MiB local GPU memory and return to 9.86 MiB after close; cached attachment
peaks at 0.44 ms CPU in the offscreen fixture. These are component measurements,
not live Explorer operation latency or displayed frame rates. All sixteen older
archives and README retain their hashes; published source matches the tested
archive. Full output: `tests/build-0.9.213.log`.

## 0.9.212

- Retain the existing selection-completion retry when Explorer temporarily
  cannot report a Ctrl+A selection count. Includes all layering, End and
  large/filtered Ctrl+A fixes from 0.9.210–0.9.211.

Validation: `build.bat -SourceFile 0.9.212.cpp` passed repository workflow,
Windhawk DLL linking, the complete C++/graphics/input suite and all 21 real
DLL-unload cycles. The new transient-count test fails on preserved 0.9.211 and
passes here. Twenty warm 4K tabs retain 1,483.52 MiB local GPU memory, returning
to 9.86 MiB after closure; offscreen cached activation peaks at 0.40 ms CPU.
All fifteen older archives and README retain their hashes; published source
matches the tested archive. These measurements do not establish live Explorer
latency or displayed frame rates. See the
[investigation](docs/layering-and-selection-0.9.210.md).

## 0.9.211

- Complete the layering audit for reordered columns: keep the Name image/text
  source below metadata text regardless of its column index, in both mode and
  auto-fit scenes. Body containers preserve the background/group/header/chrome
  hierarchy without allocating another texture. Clip fitted headers to the same
  horizontal viewport as the body.
- Includes the zoom/header, End recovery and large/filtered Ctrl+A fixes from
  0.9.210. See the [investigation](docs/layering-and-selection-0.9.210.md).

Validation: `build.bat -SourceFile 0.9.211.cpp` passed the complete repository,
DLL, C++ and 21-cycle unload gate, including moved-Name body ordering,
zoom/header pixel protection, neighboring thumbnail overlap, End recovery,
filtered Ctrl+A and retained warm-tab memory. Fourteen older archives and README
remain unchanged; publication matches the tested archive.

## 0.9.210

- Enforce the shared paint order: scrollbars, column headers, group headings
  and their feedback, item text, icons/thumbnails, item feedback, zebra.
  Reserve transparent header bands during zoom/reflow and align compositor
  scene order; preserve existing scrolling and animation curves.
- Preserve End's viewport when native selection cannot verify a GPU-only item.
  Use the existing two-sided selection reset instead of importing an unrelated
  native selection. Keep the overlay attached and file commands guarded.
- Import filtered native Select All by identity on the existing worker lane.
  Cancel older selection work before Ctrl+A, reject stale folder observations,
  and retain a continuation when the native command has not completed yet.
- Add GPU overlap/zoom pixel checks, filtered/bulk selection and End recovery
  regressions, plus opt-in read-only diagnostics. See the
  [investigation](docs/layering-and-selection-0.9.210.md) for evidence and limits.

Validation: `build.bat -SourceFile 0.9.210.cpp` passed repository workflow,
Windhawk DLL linking, the complete C++ suite and all 21 real DLL-unload cycles.
The new animated zoom/oversized-header and overlapping-thumbnail pixel cases
pass in LTR/RTL, as do filtered Ctrl+A, canceled generations, unsafe retries and
actual End recovery. Twenty warm 4K tabs retain 1,483.52 MiB local GPU memory,
returning to 9.86 MiB after closure; cached activation peaks at 0.40 ms CPU in
the private offscreen fixture. All thirteen older archives and README remain
unchanged; publication matches the tested archive. These are component tests,
not live Explorer acceptance or displayed frame-rate measurements.

## 0.9.209

- Keep supported inactive tabs' accepted image, visible text/icons and graphics
  device warm. Switching back shows the ready tab before repainting, without
  rebuilding its graphics domain or reloading its directory.
- Retire obsolete textures and interrupted animation planes while hidden.
  Recycle DirectComposition's measured retained animation-surface pool while
  preserving the swap chain and text/icon resources; ordinary returns reuse it.
- Add warm-frame/texture pixel checks, repeated production tab activation with
  graphics creation disabled, and measured multi-tab/close GPU memory bounds.
  See the [investigation](docs/warm-tabs-0.9.209.md) for evidence and limits.

Validation: `build.bat -SourceFile 0.9.209.cpp` passed repository workflow checks,
Windhawk DLL linking, the complete C++ suite and all 21 real DLL-unload cycles.
Twenty private offscreen 4K tabs retain twenty warm devices at 1,483.64 MiB
local GPU usage (1,904.76 MiB peak), returning to 9.86 MiB after closing.
Visible glyph/icon and per-return frame pixels survive cleanup. Three actual
attachment-handler returns with graphics creation disabled peak at 0.46 ms CPU
time; this is an offscreen component measurement, not live/displayed latency.
Failed pool replacement and activation during replacement retain usable
targets. Twelve older archives and README remain byte-identical; publication
matches the tested archive. See the investigation for workload limitations.

## 0.9.208

- Fix the live-profiled multi-tab CPU spin in the diagnostic trace writer:
  reset its manual-reset wake under the queue lock when drained, preserving
  enqueue, settings-transition and stop delivery without polling.
- Add an actual 25-writer idle CPU regression with enabled/disabled tracing,
  re-enable delivery and worker retirement. Record the matching live thread
  IDs, installed DLL symbols and separate scrolling hiccup evidence in the
  [CPU investigation](docs/cpu-trace-writer-0.9.208.md).

Validation: the new CPU test fails against the unchanged 0.9.207 archive
(1,718.75 ms aggregate writer CPU over 422 ms enabled; 2,015.62 ms over
625 ms disabled). The final 0.9.208 gate measures 0 ms writer CPU over 406 ms
enabled and 391 ms disabled for 25 actual writers, then verifies resumed
delivery and teardown. `build.bat -SourceFile 0.9.208.cpp` passed the full
repository, Windhawk DLL, C++ and 21-cycle DLL-unload gate, including GPU
memory/lifetime, restore, input and native-model checks. Eleven old archives
and README remain unchanged; publication matches the tested archive.
Live CPU samples prove the logger spin; remaining native/GPU hiccup timing
needs observation after loading the fix. See the investigation for capture
overhead, negative-control limits and the isolated GPU assertion retry.

## 0.9.207

- Fix the latest dump's confirmed stack overflow: move large Shell path buffers
  out of reentrant callback stacks and gate compiled callback frame sizes.
- Release inactive tabs' GPU domains, including interrupted animation scenes,
  after owner-thread rendering/setup completes. Preserve native suppression,
  model data and selection; rebuild graphics on resume without re-enumerating.
- Flush/trim released graphics, bound glyph allocation estimates including
  pending cleanup, and limit optional per-tab folder history.
- Add optional **GPU memory profiling**, disabled by default: measured process
  GPU usage/budgets and private RAM, plus per-tab estimated resource categories.
  See the [crash and memory investigation](docs/memory-and-stack-0.9.207.md)
  for evidence, measurement limits and capture instructions.
- Add actual-DLL small-stack recursion and 20-tab graphics lifecycle coverage.

Validation: `build.bat -SourceFile 0.9.207.cpp` passed repository workflow checks,
actual Windhawk DLL linking, the full standalone C++ suite and all 21 real
DLL-unload cycles. Critical compiled message/COM frames peak at 1,944 bytes;
32 nested actual-DLL dispatch hooks return on a 256 KiB thread stack. The
20-tab offscreen lifecycle test peaks at 421.52 MiB local GPU usage and returns
to 9.86 MiB with no retained tab devices. Hide/show races, nested setup scopes,
pending native layout, hidden paints, selection/shaping reuse and minimized
production attachment/restore pass. Real native namespace counts now wait for
complete enumeration and selection API acceptance instead of assuming a cold
view finishes in two seconds. All ten older archives and the public README
remain unchanged; `latest.cpp` equals the tested 0.9.207 archive. These are
private component/integration measurements, not live Explorer acceptance or
displayed frame rates; the exact reported 12 GiB workload remains unverified.

## 0.9.206

- Reduce repeated native provider entry during an accessibility burst: reuse
  the exact fragment root already retained and HWND-verified for the current
  dispatch. Child providers still resolve ownership; no cross-dispatch cache,
  timer or accessibility notification is introduced or removed.
- Separate provider lookup/reuse counts and elapsed time from native scope and
  final notification delivery in existing optional trace/frame diagnostics.
- Add a 600-event root-burst and timing-correlation regression; retain real
  Windows provider/client cached-data delivery and unload coverage. Record the
  post-fix capture and additional live sample in the
  [cold Recycle Bin investigation](docs/cold-recycle-hiccups-0.9.206.md).

Validation: `build.bat -SourceFile 0.9.206.cpp` passed repository workflow checks,
actual Windhawk DLL linking, the full standalone suite and all 21 DLL-unload
cycles. A 600-event exact-root burst requires one root lookup; child and foreign
providers still prove ownership. The real three-view UIA client test reports
84 lookups plus 36 verified-root reuses for 120 incoming events, with 12 final
notifications and usable client cache data. Recycle Bin F5 acceptance and
completed namespace revalidation pass after isolating the synthetic key's
modifier state; no failed gate published a release. All nine older archives
and the public README remain unchanged; `latest.cpp` equals the tested
0.9.206 archive. Logs and additional live stacks establish the redundant work;
the new release's live cold-cache behavior still needs observation after reload,
and native row/font/navigation costs remain outside this optimization.

## 0.9.205

- Address the live multi-tab stall identified in native DirectUI/UI Automation
  stacks. Coalesce a suppressed list's structure-event burst at actual native
  dispatch completion into one root invalidation, preserving accessibility
  notification and normal Shell operations rather than disabling either.
- Validate native root/host-provider HWND ownership, keep exact singleton
  events/runtime IDs, isolate nested foreign controls and reject obsolete
  navigation/teardown batches. Include the new API hook in callback draining.
- Add a real three-view UIA subtree-cache client test, focused provider/lifetime
  contracts and manual read-only live stack/symbol tools. Document why the
  previous viewport probe did not establish a live fix in the
  [native UIA investigation](docs/native-uia-stalls-0.9.205.md).

Validation: `build.bat -SourceFile 0.9.205.cpp` passed repository workflow checks,
actual Windhawk DLL linking, the full standalone suite and all 21 DLL-unload
cycles. The real three-view/MTA UIA client test delivered usable cached data
and reduced 120 native notifications to 12. In the final gate, its 12 native
mode changes took 197 ms with batching versus 699 ms without; these are
offscreen component elapsed times, not live Explorer latency or display FPS.
Failed gates exposed native host-provider delegation and desktop-wide legacy
WinEvent test dependencies; both are addressed in the linked investigation,
and no failed gate published a release. All eight prior source archives remain
unchanged; `latest.cpp` is identical to the tested 0.9.205 archive. The affected
live tab supplied the diagnostic stacks; interactive validation of this release
still requires a mod reload.

## 0.9.204

- Repair native presentation children created after their host was already
  suppressed. Revisit known hosts and acquire missed children before their
  first posted native work, closing the gap that left expensive hidden
  DirectUI layout running on Explorer's input/render thread.
- Repair suppression before native header sorting and reject reentrant sort
  results from replaced views. Preserve native model/selection/refresh work,
  no-detach recovery and native restoration on fallback/unload.
- Add acquisition/sort spans and viewport ownership/size to existing stall
  diagnostics. Analyze the real capture's 531–828 ms native stalls and the
  real read-only Shell comparison in the
  [Recycle Bin investigation](docs/recycle-ui-stalls-0.9.204.md).
- Isolate replayed scrollbar samples and background attachment focus checks
  from desktop input/activation using the existing fixtures; retain frame-yield
  checks, explicit focus-setting assertions and native iconic/enabled checks.

Validation: `build.bat -SourceFile 0.9.204.cpp` passed repository workflow checks,
real Windhawk linking, the full standalone suite and all 18 DLL-unload cycles.
Real offscreen Shell probes preserve counts, selection, modes, refresh and
restoration. In the final run, the Recycle Bin's eight-sort comparison reduced
deferred native callback CPU from 557.01 ms total (82.37 ms maximum) with a
missed child to 0.68 ms total (0.34 ms maximum) with repair. These measurements
are private browser controls, not live user-file management or display FPS.
The earlier failed gates exposed input/focus test-isolation defects documented
in the investigation; neither failure published a release. All seven prior
source archives remain unchanged, and `latest.cpp` is identical to the tested
0.9.204 archive.

## 0.9.203

- Keep the GPU overlay active when selection identities/counts cannot be
  reconciled or a revalidation fails. Retain usable rows, block file commands
  against unverified selections, and recover by clearing both selections through
  the existing verified native deselection API. Failed resets remain retryable;
  a fresh verified selection supersedes an obsolete recovery.
- Defer recovery through actual Shell command/rename/drag-drop calls, modal
  owners, directory enumeration and watcher batches. Delete and other Shell
  operations are never repeated or cancelled by recovery. Late resets cannot
  affect a replacement tab/view after reentrant navigation.
- Fix Recycle Bin selection imports that compared serialized absolute PIDLs.
  Use raw relative capture and the existing per-tab background selection lane
  to match canonical identities and focus against immutable published rows,
  preserving duplicate names, large zoom, cached returns and stale-result checks.
- Record the supplied log evidence and recovery behavior in the
  [selection investigation](docs/selection-recovery-0.9.203.md).

Validation: `build.bat -SourceFile 0.9.203.cpp` passed repository workflow checks,
real Windhawk linking, the full standalone suite and all 18 DLL-unload cycles.
New tests cover large grid zoom with binary-different canonical selection/focus
IDs, duplicate labels, cached returns, stale snapshots, Delete command lifetime,
names/watch completion, failed resets and revalidation, blocked unsafe commands,
newer selection and navigation during clearing. Existing scrolling, rendering,
clipboard, focus, selection and tab-isolation checks also passed. These are
private offscreen component tests with captured verbs, not live user-file Delete
validation. All six prior source archives remain unchanged, and `latest.cpp`
is identical to the tested 0.9.203 archive.

## 0.9.202

- Trigger automatic column fitting after pinch and Ctrl+wheel zoom through the
  shared zoom and column-fitting paths. Coalesce input with a per-tab 200 ms
  debounce, including fractional samples, while still waiting for actual gesture,
  animation and visible-property completion.
- Ignore clamped/no-op zooms, respect disabled autofit and cancel obsolete zoom
  requests through the existing navigation, mode, manual-column and teardown
  paths. Queued old timers/fit messages cannot bypass a newer zoom deadline.

Validation: `build.bat -SourceFile 0.9.202.cpp` passed repository workflow checks,
real Windhawk linking, the full standalone suite and all 18 DLL-unload cycles.
Zoom tests exercise real wheel/pinch handlers with smoothing on/off, rapid and
fractional input, stale fit/timer messages, actual pinch READY, delayed metadata,
disabled autofit, clamped zoom, cancellation and preserved property generations.
Existing LTR/RTL, animation, scrolling and tab-isolation checks also passed.
These are private offscreen component checks, not live Explorer validation.

## 0.9.201

- Fix the directory-switch autofit slide's dependency on UI-thread paints by
  extending the existing DirectComposition column path. Positions, changing
  width clips and alpha now share the same absolute clock as CPU hit geometry.
  Per-column sources isolate glyphs; dividers and sort chevrons keep their edge
  anchors. Preserve scrolling, metadata updates and accepted-frame replacement.
- Bound compositor sources and retain the ordinary renderer for incompatible
  gestures/layouts. Remember setup rejection for the transition/viewport, so a
  resource failure cannot cause repeated per-frame allocation attempts.
- Add scene-kind/fallback diagnostics and animation activity flags to existing
  traces/frame summaries. All logging settings still default to off.
- Analyze the supplied native-dispatch stalls and audit the other animations'
  exposure in [the animation investigation](docs/animation-stalls-0.9.201.md).
  Smooth scrolling, grid reflow/zoom, row/group motion and paint-time feedback
  still depend on owner-thread updates; this release does not claim those
  effects are independent compositor animations.

Validation: `build.bat -SourceFile 0.9.201.cpp` passed repository workflow checks,
real Windhawk linking, the full standalone suite and all 18 DLL-unload cycles.
New LTR/RTL checks cover real source pixels, neighbor isolation, a deliberately
unpumped 65 ms owner pause, shared-clock catch-up, retargeting, concurrent scroll,
automatic-fit publication, bounded fallback and plane retirement. Existing
mode/navigation, clipboard, focus, selection and tab-isolation suites passed.
These verify offscreen rendering and the compositor contract, not live Explorer
scan-out or displayed FPS. All four preexisting archived source hashes remain
unchanged; published `latest.cpp` is identical to the tested 0.9.201 archive.

## 0.9.200

- Replace `autoRefitDelayMs` with `autoRefitColumns`, a boolean enabled by
  default. Automatic fitting now follows final names, visible property data,
  native column definitions, submitted layout and actual input/resize completion.
  Reuse the existing fitter and clipping preflight; collapsed hidden rows no
  longer create metadata dependencies that can never complete.
- Keep cold attachment, navigation rebind, Shell subscriptions and returned-child
  selection pending beyond their former time/retry limits. Transient not-ready
  results remain distinct from explicit unsupported capabilities. A successful
  render and subsequent compositor cycle replace the post-paint focus timer.
- Keep delayed New-item rename intent through enumeration and same-context
  watcher setup, with explicit context/input/failure cancellation. Use actual
  pointer release and DirectManipulation status instead of grace/inactivity
  deadlines. Settled column presentation must be accepted before Shell setup.
- Document the full timing inventory, completion dependencies and timers retained
  for animation, batching, freshness and retry pacing in the
  [timing audit](docs/timing-audit-0.9.200.md).

Validation: `build.bat -SourceFile 0.9.200.cpp` passed repository workflow checks,
real Windhawk linking, the complete standalone suite and all 18 DLL-unload
cycles. Adversarial tests hold data incomplete, deliver stale/partial results,
exceed former retry limits, acknowledge presentation explicitly, and cancel
obsolete work through later input or reentrant navigation. Earlier gate runs
exposed column fixtures that skipped accepted presentation and a metadata
fixture that assumed synchronous insertion through a nonblocking queue; those
fixtures now service the real completion/continuation paths. The complete gate
then passed. These are private offscreen checks with substituted clipboard/focus
APIs, not live Explorer validation on a slow physical PC or displayed frame-rate
measurements. All three preexisting archived source hashes were preserved.

## 0.9.199

- Correct `instantScrollbarScrub` to affect only vertical thumb dragging. With
  it enabled, the viewport moves immediately in whole item-line steps, like a
  fast-scroll thumb; grid views advance by a row of cells. The thumb still maps
  proportionally across the folder and can reach the final partial row.
- Grouped views now snap using existing item/group geometry, including sticky
  heading offsets and collapsed sections. No row scan or provider call is added.
- Restore direct horizontal thumb dragging regardless of this setting. Track
  clicks, wheel scrolling and touchpad paths retain their existing behavior.
  The setting description now states its vertical-only scope.

Validation: `build.bat -SourceFile 0.9.199.cpp` passed real Windhawk linking,
repository workflow checks, the complete standalone suite and all 18 real
DLL-unload cycles. Captured-thumb checks verify whole-item thresholds in
Details/List/grid, sticky-heading offsets, collapsed groups, monotonic progress,
final partial rows, and direct LTR/RTL horizontal dragging with either option
value. Track clicks and wheel input still animate with instant scrubbing on.
These are offscreen component checks; live Explorer validation remains separate.

## 0.9.198

- Make `instantScrollbarScrub` apply to horizontal as well as vertical thumb
  dragging. Smooth horizontal drags reuse the existing track animation and
  consume elapsed motion before retargeting, preserving progress under rapid
  mouse input. Stationary samples keep the same clock; touchpad pans stay direct.
- Enable every filesystem folder, including network shares, without the
  `allFilesystemFolders` setting or Windows-directory scope. Supported Shell
  roots and native fallback still use the existing capability checks.
- Remove `nativeShellFocus` and its Compatibility group. Native Shell focus,
  keyboard routing and preview integration are always enabled, with the
  existing unavailable/rejected-native-focus fallback.
- Apply the requested appearance, selection, hover and column defaults in both
  the Windhawk settings and runtime initializers. The default font is Explorer's
  font, auto-refit waits 250 ms, and all four logging switches default to off.

Validation: `build.bat -SourceFile 0.9.198.cpp` passed real Windhawk linking,
repository workflow checks, the complete standalone suite and all 18 real
DLL-unload cycles. Captured-pointer tests cover both scrollbar modes, actual
viewport positions, rapid horizontal retargeting, stationary clocks, LTR/RTL
endpoints, and disabled master smooth scrolling. The always-enabled native-focus
matrix, guarded focus fallback, and ordinary filesystem/namespace attachment
checks passed. All 31 setting defaults and the changed runtime initializers were
checked. The first run exposed a zero-margin assumption in the narrow-column
rename fixture; its document/viewport coordinate conversion was corrected and
the complete release gate reran successfully. Live Explorer validation remains
separate from these offscreen component checks.

## 0.9.197

- Copied files now share the existing pending-transaction icon wiggle. Only Cut
  adds fading; normal hidden/dotfile dimming still applies to copied files.
- The tab-owned snapshot follows `WM_CLIPBOARDUPDATE`, `CF_HDROP` identities and
  the preferred drop effect, including external Copy/Cut, same-path operation
  changes and replacement with non-file data. Plain file-drop data means Copy.
- Reuse bounded, sequence-checked clipboard capture, cache retirement, shared
  icon drawing and frame pacing. Escape cancels only Cut and preserves Copy.

Validation: `build.bat -SourceFile 0.9.197.cpp` passed real Windhawk linking,
repository workflow checks, the complete standalone regression suite and all
18 DLL-unload cycles. Private clipboard fixtures cover external replacements
across tabs, same-path effect changes, plain file drops, interrupted 9,000-item
captures and Escape preserving newer Copy contents. Offscreen GPU readback and
list/grid paints verify shared wiggle, Cut-only fading and opacity restoration.
Initial sandboxed runs hit temporary-file/fixture-cleanup restrictions; the
complete release run passed outside the sandbox. Live Explorer validation is
separate. Preserved 0.9.196 SHA256:
`A9F1BE5DA9BCB3078BABA9C9B2A1F9EE2E312EDD0BCEB33179F924FA43938915`.

## 0.9.196

- Add `build.bat`: sync the public README into the working mod, run repository checks and the full regression/unload suites, then archive numbered sources and publish an identical `latest.cpp`.
- Add a version starter and portable Windhawk discovery through an explicit path, `WINDHAWK_HOME`, installed-app registration, PATH or Windows Program Files directories.
- Remove implicit local-drive tests and discover the Windows installation directory for the default overlay scope. Keep synthetic test identities independent of actual disks.
- Keep the public README and agent instructions concise; retain implementation notes separately and put release history here.

Validation: `build.bat -SourceFile 0.9.196.cpp` passed real Windhawk linking,
the full standalone regression suite and all 18 DLL-unload cycles. Repository
workflow checks passed in Windows PowerShell 5.1 and PowerShell 7, covering
README embedding, archive conflicts, failed-test/source-change publication
guards, and paths containing spaces, brackets and Arabic characters. A copied
checkout also linked against the discovered Windhawk installation and passed
the selection suite from an unrelated working directory. `latest.cpp` and
archived 0.9.196 are byte-identical; available baseline hashes for 0.9.192
through 0.9.195 were verified unchanged. No build/test dependency on the local
username, repository drive or private runtime remains. These checks were run
on this Windows machine; live Explorer behavior on another PC remains separate.

## Earlier releases

The notes below preserve the earlier implementation and validation history. Some old source versions were removed before this repository was prepared for publication; only files actually present in `previous-versions` are available for comparison.

The preserved 0.9.118 restarts from the preserved 0.9.117 baseline. The previously
rejected 0.9.118 through 0.9.120 sources were discarded at the user's request.

0.9.119 continues from 0.9.118 to repair continuous input and concurrent scrolling.

0.9.120 preserves elapsed scroll progress when input retargets between paints.

0.9.121 removes guessed body widths from populated-view column fitting so
first-time column additions do not overshoot and shrink back on metadata arrival.

0.9.122 draws zebra stripes as an independent document background. Insertions
and deletions move contents over fixed row slots; with showEmptyRows disabled,
only the background's trailing height animates, independently of item motion.

0.9.123 preserves resolved property cells by file identity across sorting and
same-folder snapshots. Metadata travels with the folder cache and is restored
for visible rows before queuing only missing or genuinely invalidated values.

0.9.124 sizes the rename EDIT from live text within the Name column, keeps
repeated clicks on the same sole selection from restarting its animation/native
transaction, and makes Escape cancel Cut through the clipboard. Escape no longer
switches to native view, including while names/navigation are pending.

0.9.125 fixes the remaining repeated-click blink: normal capture release no
longer clears hover, which changed the composited selection background despite
unchanged selection strength. Shared pointer completion reconciles the release
position and preserves hover through WM_CAPTURECHANGED; real capture loss and
pointer leave still clear it.

0.9.126 resolves column catalogs on a dedicated tab-owned worker, shares
DirectWrite layouts and intrinsic measurements between fitting and drawing,
uses the prepared path index for selection restoration, and avoids full
large-selection verification during unchanged idle polls. Shell item hints
copy PIDLs before unlocking and use path conversion without Shell item creation.
Metadata result bursts share one viewport follow-up; capacity strings format
on the probe worker. Slow UI stages publish names through the existing trace.

0.9.127 prioritizes visible icons before optional startup and metadata, uses
independent per-tab private Windows pools for per-file icons, properties and
capacity probes, and streams individual volumes and metadata rows. Shell
association icons have an independent lane and survive navigation. Empty Shell
icon responses use an icon-only image-factory fallback and one bounded retry.
Ready textures upload in time/byte batches, with tighter budgets during motion.
Watch setup and discarded-cache cleanup have separate lanes. One canonical
worker inventory handles stop/reap/unload, including selection and catalogs.

0.9.128 prevents cold provider completions from interrupting scrolling. Icon,
property and capacity notifications hold their wake latch until a paced frame;
names/catalog/watch delivery remains independent. Frames merge bounded data,
submit one visible-demand follow-up and use the motion upload budget. Metadata
prioritizes visible rows, preserves cache restoration overhang and cancels
departed queued requests without stranding their pending flags. Column fitting
also respects direct scrollbar, manipulation and marquee input. Scroll easing,
per-tab pools and native suppression retain their existing ownership.

0.9.129 extends the UI feedback audit to wheel/touchpad/marquee demand, idle
Shell polling, selection observations, optional startup, This PC projection and
unchanged directory results. Input marks one paced data follow-up. One shared
motion guard keeps live Shell maintenance out of active animation/input;
deferred startup/selection observations resume on settlement or idle. Capacity
updates project cached This PC identities on the frame and preserve pending
extra properties at stable row identities/positions. Genuine identity changes
keep their immediate capture wake. Ignored previews and unchanged revalidations
report no content change; background refreshes do not force native presentation.
Post-content touchpad/watch, clipboard and drag/drop setup run before the
provider-binding steps, so deferring optional work cannot strand initial input.

0.9.130 preserves the selected native host's logical visibility across
minimized unload and accepts an otherwise visible Shell view whose dedicated
host was left hidden while minimized. Inactive/departed views stay excluded.
One shared SetWindowPos visibility path uses NOACTIVATE/NOOWNERZORDER for native
suppression, restoration and overlay/editor presentation. Focus returns only
to an enabled, restored foreground frame. Rendering, optional startup and idle
Shell/selection maintenance pause while minimized. The existing per-frame
lifecycle ownership also owns a frame subclass; only minimize-state changes
post a per-tab resume, outside USER's restore query/activation transaction.
The final overlay removes the subclass before DLL unload. Restore-query and
enabled-state diagnostics report native results without overriding them.

0.9.131 makes startup attachment independent of Explorer dispatching the
private registered wake message or receiving mouse/paint input. The existing
cold observer installs one process-local WH_GETMESSAGE subscription per UI
thread before posting; only removal of that specific message runs the shared
owner-thread attach handler and clears its latch. PM_NOREMOVE is observational.
Thread handles detect reused thread IDs; the observer owns and removes every
subscription on stop. Startup begins in Wh_ModAfterInit, after Windhawk has
applied hooks. The DispatchMessage fallback uses the same handler. Existing
minimized presentation, tab-owned data pipelines and native suppression remain.

0.9.132 exposes the active Explorer type-to-select prefix in matching visible
names using a compact yellow fill and outline, so rows visibly reflect the
current prefix while the existing selection/scroll match runs. Prefix width is
measured through the per-tab DirectWrite cache; matching is limited to visible
rows and uses the existing loaded names. A per-overlay timer clears the marker
after the existing 1.2-second search timeout and on navigation/teardown.

0.9.133 reuses the column transition for Details/List mode changes. A fractional
header extent collapses/expands with secondary-column slides on the existing
180 ms frame pacing; header captions retain their natural layout height under
the moving clip. Logical mode changes immediately while outgoing columns remain
paint-only. Names, selection geometry and the scrollbar reserve follow the
animated geometry. Reversal starts from elapsed progress; metrics/row delivery
and same-tab navigation preserve it. Native descriptors remain deferred until
settlement; a List-first tab uses the ordinary column-add entrance when they
arrive. Native host suppression remains in force throughout.

0.9.134 adds Small/Medium/Large/Extra Large Icons to the GPU view gate. Shared
document/item geometry packs cells and resets rows under group headings;
visible-demand, hit testing, selection/hover, keyboard, rename, marquee,
scrolling and 2D item transitions consume it. Large labels use the existing
DirectWrite cache with centered wrapping; type-search markers use the shaped
text's hit-test geometry. Icon jobs/caches carry size tiers on the same tab-owned
workers, using Shell image factories/image lists for larger icons and thumbnails;
ready images at other sizes remain available as interim content. Thumbnails keep
their aspect ratio. Cache capacity follows visible demand and changed-item hints
invalidate every size variant. Metadata remains Details-only. Tiles/Content
and other unsupported namespaces retain native presentation.

0.9.135 keeps the displayed icon size until a replacement GPU bitmap exists,
protects visible fallback records from eviction, and requests the zoom's final
resolution rather than every intermediate size. Grid zoom reuses sparse item
motion for old/new viewport cells and widths, with elapsed retargeting and the
same 180 ms clock. Destination packing stays fixed during the transition;
viewport anchoring interpolates with it. Mode commands/native layout hints use
the existing queued probe to bypass the idle mode cache without provider work.
Supported mode/size changes reveal the existing selection once. The existing
selectionRadius setting controls List/Details; gridSelectionRadius controls
icon grids. Native suppression and per-tab worker ownership remain unchanged.

0.9.136 supports the Recycle Bin root in Details, List and icon grids. Explicit
known-folder identity checks admit it independently of allFilesystemFolders.
Its names reuse the namespace enumerator on the tab's directory lane, streaming
previews into the ordinary background layout/index/diff/cache publication path;
This PC's small capacity projection remains separate. Opaque PIDL keys distinguish
duplicate original names/paths. Shell PIDLs also carry icon/thumbnail, property,
selection and command identity; default opening invokes the namespace's native
verb. Native column descriptors include Original location/Date deleted; sorting
and grouping resolve requested Shell properties on workers. Scoped notifications
feed the existing change batcher for restore/delete/add/empty operations. Tests
only read the real Recycle Bin and use private files for duplicate-name fixtures.

0.9.137 preserves viewport ownership after wheel/thumb scrolling and treats
ordinary native selection/focus/reorder signals as observations, so late image
or metadata completion cannot re-reveal an unchanged selected item. Genuine
new native selections and explicit navigation reveals retain their reveal path.
The shared maintenance guard includes hover and recent pointer motion; queued
mode/settings hints coalesce through motion and resume at idle. Explicit view
commands bypass that deferral. Recycle Bin mode/column/grouping snapshots are
reused until commands invalidate them; its idle timer no longer polls live
selection/settings or rescans a valid binding. Its existing Shell watch avoids
the missing-Win32-watch fallback. Immutable folder identity is captured once
per binding and reused by enumeration, catalogs and focused-item conversion.
Regression checks cover 800-item late echoes in filesystem/Recycle Bin folders,
new external selection reveals, hover/reorder bursts, zero live namespace view
queries over forced idle ticks, immediate view commands and previous pipelines.

0.9.138 addresses the repeated Recycle Bin enumerations observed after 0.9.137.
Ambiguous root UPDATEDIR notifications and watch-installation checks compare
raw namespace identities on the existing names lane before starting a refresh;
unchanged checks preserve directory/detail generations, cached cells and icons.
The shared enumerator reuses its worker-owned parent for child Shell items.
Metadata-notification overflow invalidates viewport enrichment rather than
promoting UPDATEITEM to a names refresh. Nested notifications are scoped to
the displayed root. Shell workers initialize their own STA and pump provider
messages between jobs/while idle, with COM teardown before pool callbacks
return; per-tab pools and the existing lifetime regulator remain independent.
Queued inactive-tab feature/settings/refit work cannot perform maintenance,
and the DispatchMessage recovery scan respects motion and valid Recycle Bin
bindings. Identity sets retire through the existing background cleanup lane.
The 0.9.137 selection/viewport fix is retained. HINT/CHECK/REFRESH traces expose
remaining refresh causes; component tests do not establish live Explorer FPS.

0.9.139 replaces the fixed icon-count eviction cliff with per-tab accounting
of decoded pixels, estimated GPU copies, keys and pending request reservations
under the existing adaptive cache budget. Each viewport shares one LRU victim
scan; pressure cleanup is time bounded and visible fallback bitmaps remain
protected. Icon delivery has a time/count budget. Cold data delivery/demand use
nonblocking worker-queue access and retry through the existing paced frame;
metadata remainders remain UI-owned rather than waiting to return them to a
worker queue. Stale thumbnail pixels retire on the existing cleanup lane,
including asynchronous cleanup-lock contention and teardown. Metadata cache
records retain namespace names/PIDLs needed for identity validation.
The separate frameTimeLogging option defaults off and is independent of
perfLogging. Per-tab GPU frames summaries record active submission cadence,
interval percentiles/max, late intervals and CPU delivery/demand/upload/draw/
present stages. Intentional idle gaps, including thumbnail arrivals during a
paused grab, are excluded; these are submission timings, not scan-out FPS.
Tests cover 790 per-item icons, contended queues, stale pixels, namespace cell
cache reuse and profiler enable/disable/idle/stall behavior alongside the full
existing suite. The live Recycle Bin hitch still requires Explorer verification.

0.9.140 addresses the shared renderer after live frame logs isolated roughly
13.5 ms of cached Recycle Bin drawing with zero enrichment work. The existing
per-tab text-layout LRU now also retains A8 GPU glyph masks by geometry.
FillOpacityMask applies text gamma and live color/opacity without rebuilding
coverage, including during fades. Cold preparation
is limited to 1 ms per paint with immediate ordinary text fallback. Intermediate
zoom/column/group geometry avoids new rasters. Font changes transfer old cache
ownership and reclaim resources after presentation in 0.25 ms slices; device
loss clears device-bound resources while retaining current glyph shaping.
The compositor callback completes one coalesced paint instead of waiting for
low-priority WM_PAINT delivery under a busy message queue. Invalidations made
by the frame's data merge are consumed before its full paint, so enrichment
cannot leave a duplicate paint behind that same frame. Provider completions
remain paced. Frame logging separates drawing, EndDraw flush and cleanup,
counts text hits/misses and reports slow dispatch/overlay messages of every
type plus frame queue waits. Namespace verification queue contention retries
without blocking the UI. Native suppression and per-tab ownership remain.
Offscreen rendering/pixel checks do not establish live Explorer FPS or prove
the source of the separate 327 ms gap in the supplied log.

0.9.141 follows cold-scroll logs to same-owner-thread native DirectUI dispatch,
then reproduces it in real offscreen ExplorerBrowser controls. Hidden native
presentation children retain an active virtual viewport and still realize rows,
layout and accessibility structure during provider updates. The shared native
suppression path now holds only those owned DirectUIHWND/SysListView32 children
at zero size while preserving their Shell model, mode and column preferences.
The existing native subclass intercepts requested geometry before layout,
records its latest bounds per tab and restores them before revealing the host.
Direct/deferred positioning cannot bypass the interceptor; replacement children
join through the existing coalesced owner-thread presentation message. Native
messages and model notifications still run normally. All native fallback,
unload and minimized lifecycle paths use the existing restoration path.
The shared text cache canonicalizes fully fitting single-line glyph masks to
intrinsic width, allowing reuse and bounded preparation during column fitting;
trimmed/wrapped/changing-font geometry retains its exact bounds and fallback.
The linked Windhawk build and full regressions passed. Real read-only native
controls retained 793 Recycle Bin rows and selection/view commands: their worst
initial callback measured 150.778 ms normally and 0.211 ms suppressed. The
512-item filesystem control measured 19.920 ms and 0.531 ms respectively.
These component measurements do not establish live Explorer FPS.

0.9.142 follows cold Recycle Bin logs to uncached metadata drawing rather than
provider delivery: the former averaged 12.5 ms while delivery averaged 0.06 ms.
The shared text preparation budget previously fell back to ordinary DirectWrite
drawing for every unprepared cell, leaving the expensive work on the UI thread.
Text now accepts a deferred-readiness policy for metadata in all folder views.
Once the preparation budget is spent, new cells wait for an existing paced frame,
including their first shaping, rather than performing that unbounded fallback.
Ready masks still draw before the budget check, and filenames remain immediate.
Changing font/column/horizontal geometry retains immediate text drawing to avoid
removing previously visible text during those animations. The normal per-tab
textRasterPending latch resumes preparation, including while a thumb is held.
Frame summaries add text-wait counts under the existing frameTimeLogging flag.
No additional worker, cross-tab queue, native/model changes or scrolling easing
changes are introduced; 0.9.141 native viewport suppression remains intact.
A 793-row six-column cold drawing workload, with values already populated and
Shell/provider work excluded, measured 9.561 ms median / 10.496 ms p95 on the
preserved 0.9.141 and 1.650 ms / 1.821 ms on final 0.9.142. Warm median was
0.178 ms. The linked Windhawk build and full suite passed, including pixel checks
for immediate filenames, deferred new values, ready cells surviving an exhausted
budget and eventual metadata completion with a held scrollbar. Measurements
are offscreen rendering CPU times, not live Explorer display frame rates.

0.9.143 is a diagnostic release for remaining Recycle Bin rubber banding.
The independent pipelineTracing option defaults off. When enabled, each tab
lazily allocates its own bounded event ring and starts one writer through the
canonical worker inventory/regulator. Producers try rather than wait for the
buffer; overflow/contention is explicitly counted and logged. QPC timestamps,
producer thread, input/frame/span IDs preserve event timing despite asynchronous
output. Disabling/re-enabling reuses the writer; teardown drains and joins it.
Trace layers cover input age, thumb conversion, scroll retarget/advance/stop/
clamp, compositor cycle sequence/DwmFlush duration/queue delay, every paint,
each frame stage, physical cursor versus target, text readiness, EndDraw and
Present HRESULTs, dispatch/native callbacks, provider jobs/publication, queue
contention, metadata application and native selection/column/presentation work.
Nested frames retain parent identities; frame end also records UI CPU cycles.
Detailed traces work without perfLogging/frameTimeLogging. No scroll easing,
metadata policy or native suppression changes are made. The linked build and
full suite passed, including concurrent/contended/overflow transport, off-thread
independent writers, nested frames, settings reuse and regulator reclamation.
Disabled-trace cold drawing measured 1.634 ms median / 1.838 ms p95, consistent
with 0.9.142 component timings. Live rubber banding requires a new capture;
this release does not establish its remaining cause or claim it fixed.
See tests/PIPELINE-TRACING.md for capture instructions, schema and layer/flag maps.

0.9.144 follows detailed live traces to delayed captured-pointer delivery:
24 successful presents in a 146 ms interval kept input ID 217 and an unchanged
scroll destination while physical cursor samples advanced by 4,866 document
pixels. The existing tab-owned frame path now samples captured GPU gestures
before painting/demand; mouse messages and frame samples share one handler for
both scrollbars, column drag/resize and marquee. Queued stale movement uses the
current physical point, unchanged samples do not restart easing or redraw, and
capture sampling retains paced wakes even when there is no animation/data work.
Stationary sampling does not require a GPU paint. Button release pauses frame
sampling until the ordinary release/commit; capture loss uses existing rollback.
No per-tab worker, cross-tab queue, provider policy or easing-duration change is
introduced. Tracing adds pointer.sample events to distinguish sampled from
message-driven motion. The linked Windhawk build and full regressions passed,
including withheld/stale mouse messages, smooth/instant and horizontal/RTL
scrolling, stationary clocks, release/capture rollback, marquee and tab isolation.
The supplied capture has explicit dropped records; findings use QPC timestamps,
input counters and the observed presents, not wall-clock output gaps. See
tests/TRACE-FINDINGS-0.9.144.md. Live Explorer feel still needs user verification.

The user confirmed smooth scrolling on 0.9.144. 0.9.145 repairs disappearing
metadata during window resizing. The supplied trace showed 152 warm text hits
turning into zero hits/152 misses/102 deferred cells on resize, without provider
updates. Geometry-only resize now preserves the body/header formats and glyph
caches. The existing body-only format refresh reuses identical size/direction;
actual body-size and DPI changes still rebuild the appropriate formats.
Text readiness is recorded per shaped value/font generation after either mask
or direct drawing. Changed clipping bounds cannot defer a previously drawn
value back to blank; genuinely new values retain the cold preparation budget.
An unpaced paint that leaves glyphs or retired caches pending starts the existing
tab-owned frame continuation instead of depending on further pointer/provider
activity. No worker, metadata-fetch policy or scroll-easing changes are made.
The new pixel regression fails on preserved 0.9.144 and passes on 0.9.145.
The linked Windhawk build and full suite pass, including exact metadata pixels
across repeated LTR/RTL resizes, real font-size refresh, changed trimming bounds
after direct drawing, input-independent deferred completion and 0.9.144's
captured-pointer regressions. The resize readback occurs before Present rotates
the newly resized swap chain. Live Explorer resize confirmation remains separate.

The user confirmed the 0.9.145 resize fix. 0.9.146 animates both scrollbar axes'
hover/press thickness and opacity on the existing 80 ms QPC/compositor clock.
Reversals consume elapsed progress; gesture reset, unusable bars and device
teardown clear their tab-owned motion. Hit testing and scrolling remain immediate.
One caption-bounds method supplies fixed/floating headers and clipping checks;
16 px padding (scaled for DPI) is also included in header fitting. Name remains
aligned to filenames. Unsorted captions use 60% of the existing native/theme
text color; the sorted caption retains full opacity and existing bold weight.
Filesystem enumeration stores raw attributes; namespace resolution includes
SFGAO_HIDDEN in its existing worker query. Hidden/dotfile/Cut content shares one
48% alpha, so overlapping reasons never compound. Appearance.dimDotFiles defaults
true and controls dot-prefixed names only; Windows-hidden items always dim.
Hidden-only changes update the snapshot digest without invalidating unchanged
property cells. Painting adds no attribute I/O or Shell calls, and one shared
content pass applies to Details/List/grid icons, labels and metadata.
The linked Windhawk build and full suite passed, including scrollbar pixels,
press/release/reversal/cleanup, themed caption opacity and padding, shared alpha,
real private hidden-file enumeration and Shell attributes, existing resize
pixels, continuous input, cached cells and native suppression. The older
synthetic thumb-retarget test now uses the existing private pointer fixture so
real mouse activity cannot override its replayed positions. Live appearance
confirmation remains separate; no new worker, queue or timer is introduced.

0.9.147 repairs the scrollbar appearance regression in 0.9.146. An absent
horizontal bar returned default vertical geometry, so its Paint call cleared
the vertical bar's animation on every frame. Unusable horizontal geometry now
retains its axis. Both bars keep their existing independent 80 ms animation,
hit testing, capture and continuous scrolling. ColumnTextBounds adapts the
caption geometry for body cells: secondary captions and values share the same
16 px DPI-scaled leading inset. Painting and clipping checks use those bounds;
fitting includes the new body inset. Paint supplies its existing viewport and
one DPI snapshot, avoiding new per-cell window/viewport queries. Name alignment
and hidden/dotfile/Cut behavior remain. Full-paint input regressions cover
Details/List, horizontal overflow, LTR/RTL hover/press/drag/release/leave and the
absent horizontal bar alongside the existing pixel, resize and pipeline suite.
The final Windhawk DLL linked and full suite passed. One earlier run hit the
existing cached-sort assertion before graphics; the unchanged binary passed on
repeat. Live Explorer appearance still requires user confirmation. 0.9.146's
SHA256 remains 9EAD8388241A5274F32BC83C616E8713DDF966F77A443C85A061FE566D49E2F8.

0.9.148 retains displayed group geometry when AttachView replaces the native
Shell HWND. Cached optimistic navigation has already restored the destination's
rows and headers; clearing sections/rowGroup on confirmation erased them, and
the unchanged-directory fast path correctly skipped a redundant rebuild.
Directory reset and row commits remain the owners of group layout changes.
The regression uses a private grouped child and ungrouped parent through the
production cache/navigation/AttachView/worker paths. It checks repeated returns,
unchanged validation, same-folder native replacement, collapsed groups and real
Group by None/re-enable changes. Preserved 0.9.147 fails at confirmation with
"native view confirmation erased cached group headers before revalidation".
No delay, extra provider call, worker or timer is introduced. 0.9.147 remains
unchanged (SHA256 03C8A10DAE493BF570D79A359D897D4AB5D53046330008DE86EF5BA6E3300DFF).
The linked Windhawk build and full suite passed on final 0.9.148. The first
fixed run passed every assertion but failed removing the temporary nested
fixture; cleanup now retries only that already-verified private path until
Shell deletion settles. Live Explorer confirmation remains separate.

0.9.149 restores list keyboard focus after an owned modal dialog (including
Shift+Delete confirmation). The frame subclass captures list ownership before
owner disable and posts completion through the existing frame-state message on
enable/activation. The tab owns the native-HWND/directory-token record; stale,
inactive, native-only, renamed or minimized contexts cannot reclaim focus.
An owned foreground popup defers completion until frame activation. Editors and
unrelated controls retain their focus; the current Shell/GPU focus preference
chooses the return target. Selection and the post-deletion caret are untouched.
There is no timer, worker, activation override or foreground-window change.
perfLogging includes GPU focus MODAL_RETURN. The isolated focus test substitutes
focus/foreground/owner-enabled APIs so user input and activation stay untouched;
real frame messages and production posting/routing/caret logic are exercised.
Preserved 0.9.148 fails at "modal return left keyboard focus outside the file list".
Its SHA256 remains 67286E0CCD639C3F35BC97CD00566A983FE0F9E77CE5CCF0B8140B02B44EBA4A.
The final Windhawk DLL link and full suite passed, including modal confirm/cancel
focus/key routing, deletion-caret targets, actual user-focus isolation, cached
grouping, minimized lifecycle, continuous input and native suppression. This
is an isolated lifecycle regression, not a live Shell confirmation-dialog run.

0.9.150 replaces the ineffective 0.9.149 modal focus-return workaround after a
read-only live capture showed the selected indices/flags survived cancellation,
the Explorer frame never disabled, and keyboard focus returned to CabinetWClass.
The old native-ancestor router missed frame-targeted arrows. A shared target
resolver handles list movement in accelerator translation and normal dispatch
for the enabled foreground frame's active visible GPU tab with actual frame
focus. It does not move focus or intercept editor/dialog/native-only input.
Separately, empty native selection imports retain the existing keyboard identity
until names establish removal, using the existing deletion/Undo caret. Disabled
owner selection observations defer through the normal retry path. No worker,
timer or Shell query is added to keyboard target resolution. Optional performance
logs expose FRAME_KEY, EMPTY_IMPORT and KEY_TARGET; detailed tracing adds
selection.import[index,next,restored,vacant]. The private regression reproduces
the captured enabled-frame/intact-selection failure using production hooks and
real PIDL selection commits. Preserved 0.9.149 fails at "enabled frame kept
selection but swallowed Up after confirmation Cancel"; its SHA256 remains
E164D3221D7CD1784D4D931B915510835FBCF4688E1AE35CC6F43F65876F9867.
Final 0.9.150 linked against the installed Windhawk host and passed the full
suite, including cancellation/deletion ordering, input guards, grouping, native
suppression, minimized lifecycle and marquee. The actual final patch still
requires Explorer confirmation; the supplied live capture used 0.9.149.

0.9.151 follows the user's directory-change versus same-directory mode traces.
The former has 33–55 ms frame gaps overlapping 46–49 ms shared-tab DirectUI
paints/native OLE dispatch. A read-only HWND probe confirmed that shared canvas
also hosts navigation controls; it cannot be suppressed as an owned file list.
The existing Details/List/grid header/column mode transition now publishes
per-tab DirectComposition planes using its same 180 ms cubic and absolute QPC
origin. DWM clips/moves the header, body and columns through owner-thread stalls;
CPU geometry still owns input, selection, viewport state and interruption.
Background, names/icons, metadata and scrollbar chrome use separate temporary
planes, with one header of vertical overscan. Pixels and transforms commit
atomically without presenting canonical pixels through the ordinary swap chain,
also removing that Present wait during mode animation. Caption and trailing
divider/sort glyph follow their corresponding edges. Canonical text geometry
reuses the normal shaping/mask budget. Paint restores geometry and target before
returning, with the existing rendering latch preventing nested draw transactions.
The normal visual root and temporary surfaces retire on settlement, resize,
device loss and teardown; reversal/rebuild retains the last committed scene until
replacement. No worker, cross-tab queue, timer, Shell paint filtering, extra
duration or selection/native-suppression change is introduced. The private tests
exercise plane pixels/overscan, no swap-chain Present during mode frames,
LTR/RTL reversal, resize/root recovery and cached cross-directory Details/grid
bindings with a 65 ms owner pause. They verify the submitted graph/clock contract,
not live scan-out FPS. Frame summaries continue measuring CPU submissions;
GPU composition MODE logs and pipeline ready/upload/commit records expose the
independent scene. See tests/TRACE-FINDINGS-0.9.151.md for evidence and limits.
Preserved 0.9.150 SHA256:
E34972CC3A1C2B51C9EE78DB33047EFECBD896D7683B776F9B3034DC35DFFD13.
The final 0.9.151 Windhawk host DLL linked and full suite passed, including
populated plane/overscan pixels, unchanged swap-chain presentation count during
mode frames, 65 ms owner pauses across real attachment/cache paths, resize/root
recovery and the preserved selection, suppression and input regressions. The
cold six-column renderer retained 1.591 ms median / 1.743 ms p95 CPU time in
the final run; these offscreen timings do not establish live display FPS.

0.9.152 keeps a committed mode scene visible through render-target rebuilds.
ResizeTarget previously reconnected the ordinary swap-chain root before new
pixels existed, exposing an old header/body offset during navigation layout.
Unchanged dimensions now leave the buffer, planes and absolute animation origin
intact. A real resize or failed scene rebuild releases preparation resources
without replacing the displayed root; Paint owns the existing ready-frame
replacement transaction. No new animation, delay, worker or shared state is added.
Tests cover unchanged-size preservation and real resize in LTR/RTL, eight cached
Details/grid navigations without intermediate settlement, late WM_SIZE/repaint
at 45/165 ms, and final normal-root retirement. The new assertion fails on
preserved 0.9.151: "unchanged size exposed the stale ordinary frame or restarted
the compositor scene". Preserved 0.9.151 SHA256:
7207BB90CDB251D8C911CDDB03894EB8023A198486520C58C078E586122C1C30.
Offscreen checks validate scene ownership/geometry; live visual confirmation
remains separate.
Final 0.9.152 linked against the installed Windhawk host and passed the full
suite, including eight interrupted navigation/repaint cycles and the existing
input, grouping, suppression, selection, minimized lifecycle and scroll checks.

0.9.153 subscribes to SHCNE_FREESPACE using each tab's existing system Shell
registration. Hints coalesce on its maintenance timer, ahead of inactive-tab
and animation guards, and submit only background capacity work. Submission
uses a try-lock and retains retry/follow-up intent; bursts are limited to one
probe batch per second with the existing 100 ms coalescing. Names/providers do
not run in the notification handler. Explicit refresh bypasses that throttle.
Capacity jobs now have their own tab-owned cancellation generation, preserving
in-flight names and property requests. The existing private capacity pool,
paced result merge and worker regulator are reused. Mounted local drives take
fresh GetDiskFreeSpaceEx values instead of treating cached Shell space values
as authoritative; virtual/network storage retains provider queries.
One ForceRefresh path serves F5, Ctrl+R and the native background Refresh verb
in every supported namespace/mode, including incomplete previews. It advances
property/job generations, retires the property cache off-thread and replaces
names/capacity requests while keeping selection and viewport. Offscreen values
invalidate lazily. This PC projection cannot revive old extra properties or
hide fresh seeded capacity cells after invalidation. Normal directory snapshot
merges carry propertyEpoch with retained cells so later sorting keeps caching.
The private synthetic volume deliberately returns stale Shell values and a
blocked/fresh volume result; checks cover bursts, queue contention, follow-up
intent, obsolete results, independent metadata jobs, F5 and zero UI provider/
volume calls. Filesystem Details/grid/List F5/Ctrl+R checks retain selection,
refetch unchanged-file properties and verify caching on subsequent sorts.
The real Recycle Bin integration checks F5 during its partial namespace load.
The first run exposed the preview gate swallowing refresh; routing now occurs
before that guard. The tests never mutate a real volume or broadcast fake
Shell events. Preserved 0.9.152 SHA256:
51BA943A9BEDB0F2B5DDE6B2CF431869C03D864306016DEFFB94FDA73CE74137.
Final 0.9.153 linked against the installed Windhawk host and passed the full
suite after the partial-preview refresh fix. The new drive checks observed
zero UI volume/property-store calls, and existing animation, suppression,
selection, input, tab lifetime and scrolling checks passed. The cold six-column
renderer retained 1.6132 ms median / 1.8139 ms p95 CPU time; these offscreen
checks do not replace live Explorer notification/appearance confirmation.

0.9.154 replaces the WinUI command-bar toggle with a persistent frame-child
Native view / GPU overlay button owned by each tab's ViewState. WinUI startup
discovery depended on a focused XAML element; being in PrimaryCommands also
did not guarantee an on-screen command. The existing active-tab/state refresh
path now presents a native button in the bottom status area, reserving the
native view controls' trailing space; if no footer exists it stays at the lower
viewport edge. No list/header extent or animation is changed. The same SetActive
path serves this button and Ctrl+Alt+F9. A posted click rechecks active-tab
ownership. Inactive/unsupported tabs hide their own button; native preference
keeps the switch back available. Themed native controls supply pointer/keyboard
behavior, tooltip and DPI font. Frame size/DPI/theme notifications coalesce an
owner-queue layout update, avoiding presentation/focus work inside restore.
The renderer keeps the visible switch above it through normal z-order repair.
Control, tooltip and font cleanup share ViewState's release path and run before
overlay disposal; the destructor covers standalone/failed-start ownership too.
The obsolete WinUI tree scans, command-vector delegates, symbol/module hooks,
cross-thread XAML dispatch/drain and WinRT projection includes were removed.
No timer, worker or cross-tab operation queue is introduced. Final 0.9.154
linked against the installed Windhawk host and passed the full suite. New
attachment checks use a frame with no WinUI toolbar: presence before first
content, native/GPU button notifications, retained HWND/tooltip and suppression,
320/640/960 pixel LTR/RTL resizing, inactive/unsupported hiding, renderer
z-order, unchanged real focus/foreground and disposal cleanup. Existing minimized
reattachment, scrolling, animation and selection checks passed. These offscreen
checks do not replace live Explorer placement/appearance confirmation.
Preserved 0.9.153 SHA256:
D7222724F45BDF33B9C8D4F0C8AE8462E4F601F3FA54450A3B5DBBA6BF1611F6.

0.9.155 replaces the persistent button caption with the previous Fluent Switch
arrow (E8AB) and keeps the tab-owned native button's interaction/lifetime.
English tooltip/accessibility text explicitly names the destination view:
Switch to native Explorer view / Switch to GPU overlay view. Windows UISettings
supplies cached colors on creation/theme changes; the status surface derives
from that palette, while high contrast/API failure uses system button colors.
Hover/pressed/disabled feedback and keyboard focus share that palette. Buffered
painting publishes opaque alpha for composited frames; the font is cached before
requesting repaint. Neighboring native status buttons supply exact bounds where
available, with a 32-DIP fallback for virtualized controls. The neighbor lookup
runs once per tab, outside paint/input. Tooltip classes are initialized explicitly
and registration is checked. Final 0.9.155 linked against the installed Windhawk
host and passed the full suite, including alpha and actual BUTTON paint readback,
light/dark interaction states, native size matching, tooltip, both toggle
directions, tab ownership and minimized dispose/reattach/restore. Live Explorer
appearance still needs user confirmation. Preserved 0.9.154 SHA256:
A7B2A13694D061E60C825089626BE4D17650C6767BABD328D9631A2E80400DFF.

0.9.156 moves switch painting into its own button subclass for WM_PAINT and
print/capture requests, using the existing shared painter. The frame no longer
owns its WM_DRAWITEM handling. A standard BS_ICON push button retains pointer,
keyboard, image binding and accessibility behavior. Cached normal/disabled
32-bit premultiplied BGRA HICONs replace live font painting. E8AB is rasterized
only on DPI/palette changes; missing coverage has a vector-arrow fallback.
Transparency masks are initialized and icon source bitmaps are deselected before
CreateIconIndirect. Replaced icons and temporary GDI objects are reclaimed.
Size, theme, tooltip, tab ownership and native suppression remain as in 0.9.155.
The attachment check verifies BM_GETIMAGE and 32-bit color, blocks the parent's
entire WM_DRAWITEM route during actual button painting, and rejects missing or
solid-tile output. Final 0.9.156 linked against Windhawk and passed the full
suite. A previous run had a transient unchanged composition-readiness failure;
no renderer changes were made for it. The read-only live check found the expected
font and glyph, but Explorer was minimized, so live restored appearance still
needs user confirmation. Preserved 0.9.155 SHA256:
847A3CA1DF7CD91F16B5C54D02458EE1455B998563533F3C3BCFBC2998AE8209.

0.9.157 makes unmodified Up/Down resolve from the first/last displayed member
of a multi-selection rather than its last-clicked caret. The existing target
resolver accepts this policy and reuses list/grid row steps and group skipping.
The input-only flag scan stops after two displayed matches; it adds no Shell
queries, worker or per-frame work. Ctrl/Shift and single-selection caret origins
remain unchanged. The shared Select no-op guard requires a sole selection even
for keyboard input, allowing a bulk to collapse at a boundary where the focused
index does not move. Missing destinations past collapsed end groups retain the
bulk's displayed edge. Regression cases cover forward/reverse ranges, disjoint
selections, repeated keys, bounds, collapsed groups, grid steps and modifiers,
checking actual Shell selection/focus identities. The selection fixture emits
selection callbacks for selection transactions, excluding focus-only commands.
The existing composition test now waits up to one second for budgeted metadata
readiness instead of assuming exactly two paints; production rendering is
unchanged. Preserved 0.9.156 SHA256:
5BB5A0AC05B3DCEC6386161AE6E72565E27B7DACA595529CBF228EED18893A74.
Final 0.9.157 linked against the installed Windhawk host and passed the full
regression suite, including the new bulk-exit and native PIDL/focus checks.

0.9.158 seeds the first Up/Down from a displayed hovered item when no selected,
restored or post-deletion keyboard reference exists. The first key selects the
seed itself; later keys use ordinary navigation. Without a valid hover it uses
the first displayed item. Fresh keyboard input reuses RefreshHoverUnderCursor
before resolving the target, covering a stationary mouse after navigation
without a timer, worker or Shell query. Existing references, bulk-edge movement,
Home/End and Page keys retain their prior behavior. The existing selection
fixture uses offscreen windows and private cursor/foreground samples to check
list/grid seeding without mouse movement, top fallback, continued movement,
invalid hover, reference priority and native Shell identities. Final 0.9.158
linked against Windhawk and passed the full suite. Preserved 0.9.157 SHA256:
D24F1F3BEB38391AFFC7A491EE1C6BC56F5B22A668751F112BF4AC6DA4AEB0FB.

0.9.159 keeps the selected item visible through wheel/pinch zoom in both
directions. Navigation and zoom share a pure reveal-target calculation using
existing list/grid/group geometry. The center anchor changes only as needed
to reveal the selected cell; an unselected caret does not take the viewport.
Grid scroll follows the same 180 ms interpolation as cell reflow, including
the destination viewport used to prepare sparse motion. The scroll limit also
retains the outgoing viewport until reflow settles, so zoom-out cannot clamp
moving cells to the shorter destination document prematurely. No Shell queries,
workers, selection commits or separate scroll animation are added. Oversized
cells retain their leading edge. Existing zoom tests cover intermediate and
settled visibility, grouped/ungrouped LTR/RTL grids, zoom in/out, shared pinch,
unchanged no-selection anchors and tab isolation. Preserved 0.9.158 SHA256:
9BCF96A45017C2762BD26FA7F2D9ABE0934E4B0C49FEAFF908DE7D7B6C70CC8C.
Final 0.9.159 linked against the installed Windhawk host and passed the full
offscreen regression suite. The initial visibility test exposed the premature
zoom-out scroll clamp; the final run includes its correction. Live Explorer
appearance remains for user confirmation.

0.9.160 stores two zoom overrides in each tab's existing presentation state:
Details/List and icon grids. Shared metrics and wheel/pinch input index the
active family, so neither copies its scale into the other. Host rebinding
restores both values; settings reset both to the configured default. A family
switch clears partial wheel/pinch input and refreshes body text only when
the restored font size changes. Header formats and per-tab worker ownership
remain unchanged. Attachment checks exercise actual wheel/pinch messages,
switching during active zoom, repeated List/Details/grid changes, text sizes,
saved preferences and native suppression. The existing zoom/reflow/visibility
and tab-isolation checks also pass. Final 0.9.160 linked against the installed
Windhawk host and passed the full offscreen regression suite. Preserved
0.9.159 SHA256:
80DC0E420C35ED692CACBE464AD5E4A2A2AAC30BB833051436BC8F1C3D6FC3E3.

0.9.161 consumes Windows DirectManipulation translation inertia, already enabled
in the existing viewport configuration but previously discarded by the blanket
finger-release pan guard. Normal pan deltas continue unchanged until READY;
contact distance and history classification freeze at finger-up. Horizontal
history swipes cannot extend, repeat or revive their feedback during inertia.
Pinch translation remains excluded. The same compositor-paced update manager,
immediate scroll path and batched data demand handle contact and inertia; no
custom friction, second easing, timer, worker or setting is introduced.
StopTouchpad accepts an optional resource-retention policy for interruption;
Stop returns the tab-owned viewport to READY and queued private gesture messages
are discarded. Explicit input, navigation, inactive tabs and minimize cancel
departing motion without recreating the manager. Existing full teardown remains
the default. The feedback fixture exercises the production handler's status
callbacks and posted messages, continued/unmodified inertial samples, contact
interruption/reversal, READY stale samples, one-shot/short history gestures,
pinch exclusion, cancellation and paced provider demand. These checks verify
routing, not physical touchpad velocity or perceived feel. Final 0.9.161 linked
against Windhawk and passed the full offscreen suite. Preserved 0.9.160 SHA256:
92064CF31BE3E1267BDF4139E40B20F944300C819776BD0575CDD091CBA192A4.

0.9.162 preserves displayed text readiness across font/zoom changes. The
shared text LRU now retains values and tracks a font/alignment generation;
ResolveTextLayout lazily replaces requested shapes while carrying their
displayed flag. Old glyph resources retire individually through the existing
0.25 ms post-presentation cleanup budget. Font changes no longer discard the
entire cache and make populated metadata cold at the first settled zoom frame.
Previously shown values keep drawing while replacement masks prepare; genuinely
new or only measured values still respect the 1 ms preparation budget. There
is no metadata invalidation/refetch, extra cache, worker or settling timer.
Pixel checks cover multiple visible values after a filename adopts a new font,
LTR/RTL and exhausted preparation budgets while preserving cold-value deferral.
Populated-view checks run wheel and pinch through intermediate, settled and
READY paints and verify unchanged property/column epochs and values. Final
0.9.162 linked against Windhawk and passed the full offscreen suite. Preserved
0.9.161 SHA256:
6A9306F9500C015113E0FFAA910DF3286D4766958D62DAA98C27E1A4D834CE0A.

0.9.163 adds Cut-only icon rotation to the shared per-item/association draw
path. A tab-owned QPC clock uses a 360 ms sinusoidal cycle; amplitude follows
logical icon size from up to 5 degrees on compact icons to 0.6 degrees on large
thumbnails. D2D rotates each bitmap around its center and restores the previous
transform before drawing other content. Existing hidden/dotfile/Cut alpha is
unchanged, and hidden-only icons stay still. Clipboard snapshot changes clear
the angle, visible latch and clock immediately; Escape uses the existing real
clipboard cancellation path. Paint samples angle once and only ready cut icons
intersecting the content viewport retain the existing compositor pacer. Hidden,
minimized, native or unrelated-directory views do not retain Cut repaint work.
The continuous paint flag is separate from layout/input motion guards so it
cannot indefinitely defer Shell maintenance. No timer, worker or option is
added. GPU checks cover LTR/RTL, size, phase, alpha, fallback icons, transform
isolation and cancellation; full Paint checks clock/pacer startup and hidden
views, and clipboard checks stop both tabs on Escape/update. Final 0.9.163
linked against Windhawk and passed the full offscreen suite. The initial run
passed Cut checks but failed the existing Recycle Bin F5 check once; the final
run passed it without changing refresh implementation. Failure diagnostics were
added to that check. Preserved 0.9.162 SHA256:
DAF8A71180657ADE2DE96491303AC7343711755ABEEE026D36D36B1DD3BB726F.

0.9.164 strengthens Cut rotation to up to 8 degrees for compact icons, with
a 1.2-degree minimum for large thumbnails. The same waveform accepts an
identity seed and delays each icon's start by a stable pseudorandom 0–120 ms.
It starts from neutral and retains its phase across sorting, scrolling and
resolution changes. The existing folded path hash supplies the seed without
random generators, allocations or additional per-item timers/state. Paint
samples elapsed time and logical size once; the shared icon path evaluates
the per-item angle. Existing Cut cancellation and viewport-only pacing remain.
GPU checks verify stronger amplitude, neutral delayed starts, distinct phases
for two cut items and unchanged phases after reordering, alongside prior
fallback/RTL/alpha/clipboard/pacing checks. Final 0.9.164 linked against Windhawk
and passed the full offscreen regression suite. Preserved 0.9.163 SHA256:
78852FFECBA0D541612B3F74894D505297F42EF5C1687AA2FA7AEE60E2BE733B.

0.9.165 distinguishes real thumbnails from icons with the existing Shell image
factory on tab-owned workers. Registered Treatment/TypeOverlay and Explorer's
ShowTypeOverlay preference control film rails and associated-app badges; empty
TypeOverlay suppresses a badge. Bounded worker-local style/pixel caches follow
the existing association epoch. Film rails rasterize once, and a badge occupies
an isolated band of the same texture, retaining the shared upload/accounting/
fallback/Cut/device-recovery paths. Images needing no wider atlas retain their
WIC allocation. One reusable bitmap brush rounds the complete thumbnail,
including rails; the independent global g_thumbnailCornerRadius96 starts at
8.f and scales with DPI. Three translucent fills approximate a shadow without
a blur effect or extra surface. The badge uses the trailing corner in RTL and
is not mirrored. Ordinary icons keep their existing draw path. The brush is
unbound after each draw and released with the target. No UI provider/registry/
resource calls, new worker, cross-tab queue or setting are added. Detailed
tracing includes worker.thumbnail.decorate. New GPU checks cover actual Shell
thumbnail identification, film/badge assembly, clipping/shadow at 96/192 DPI,
LTR/RTL, shared Cut opacity/transform, atlas-free list fallback, plain icons and
resolution replacement. A first assertion sampled the RTL badge rather than
the rail; moving the sample above the badge corrected that test. Final 0.9.165
linked against Windhawk and passed the full offscreen suite. The 36-image
cached decorated workload measured 0.3232/0.3233 ms median and 0.4587/0.417 ms
p95 for LTR/RTL CPU submission plus EndDraw, compared with 0.011 ms plain
images. These are synthetic offscreen CPU timings, not Explorer display FPS.
Native provider availability and live visual appearance remain separate.
Preserved 0.9.164 SHA256:
2965D38FD08D0710A748310A8357C55C6DF80A71EB1C54B1042130CC2852C1B7.

0.9.166 applies filenameOnlySelection to icon grids through the shared
SelectionRect/FillSelectionRect path. Cached DirectWrite line/run metrics
produce per-line label backgrounds, excluding trailing whitespace, instead
of highlighting the icon cell or the entire rectangular label block. Small
Icons uses its clipped single line. Eight bounded rectangles travel in
allocation-free paint/input snapshots; font/width/height changes invalidate
the label geometry with the existing text layout, while translation reuses it.
Physical text origins and logical RTL fills agree. Selection, hover, keyboard
movement, focus outlines and deleted ghosts consume the same shape. Pointer
leave retains the outgoing label through its existing fade. Existing colors,
opacities, radii, durations, input targets and disabled-option behavior remain.
No provider call, worker, timer, cross-tab state or user option is added.
Full-paint GPU checks cover wrapped/trimmed labels in LTR/RTL, per-line widths,
empty icon areas, selection/hover composition, leave/keyboard animation,
translated cache reuse and restoring full-cell selection. The rendered preview
was visually inspected. Final 0.9.166 linked against Windhawk and passed the
full offscreen suite. An earlier run stopped at the existing cached-property
assertion; an unchanged binary passed it. Two new fixture checks were corrected
to use a name that actually wraps and to sample background rather than letter
ink. Live Explorer appearance remains separate. Preserved 0.9.165 SHA256:
F31B3CEED4BECC9875C0A14D01D16245F44E1E739F6E27FA4C853C63D0AB2CA0.

0.9.167 adds 2 DIP outward padding to the existing grid filename-only
selection shape, using cached font DPI. The shared geometry applies it to
selection, hover, keyboard motion, outlines and deletion ghosts, bounded by
the item cell. Adjacent wrapped lines divide their padding overlap at a shared
boundary, preventing double-opacity seams. The existing text layout cache,
colors, timing, and other selection modes remain unchanged. The Windhawk DLL
linked, syntax passed, and all grid/rendering checks passed, including LTR/RTL
labels and the disabled setting. The padded GPU preview was visually inspected.
The full suite later stopped at the existing Recycle Bin F5 timing assertion
(epoch 1->1, loaded=0, pending=1); one unchanged-binary rerun stopped earlier at
the existing cached-sort assertion. No implementation changes or skipped
assertions were made for those failures; this release does not claim a full
suite pass. Preserved 0.9.166 SHA256:
5508A5BBFA3A29FE79B5A99E24B0758BDAA7A48DAD522F8E78BD33BA1AB20899.

0.9.168 makes view mode part of the per-tab cached folder snapshot. The shared
ApplyViewLayout transaction chooses mode/icon size before cached rows restore
or incoming names publish; startup, confirmed navigation and explicit changes
use the same geometry/font/column transition path. An optimistic destination
never reads its departing Shell view. Cached mode is authoritative immediately;
otherwise the first successful populated or empty content submission pins the
layout. Later native mode observations/reorders/rebinds cannot change it, even
when mode restoration is slow. No names delay, worker, timer or cross-tab
state is added. Existing column/header DWM transitions and suppression remain.
NativeViewInput separates actual user actions from automatic WM_COMMAND and
provider hints. Its queued baseline belongs to one overlay/folder token and
native view; navigation discards it. Unchanged unrelated input cannot adopt a
previously ignored native mode. Canonical View commands, Ctrl+Shift view keys
and the already-known status view buttons also permit explicit requests that
match native mode while the GPU differs. Explicit commands can follow a
same-folder native-HWND replacement. Returning from the user's native view
reconsiders its current preference. Automatic pinned-mode probes avoid COM.
Tracing adds view.mode.choose, view.mode.commit and view.mode.input records.
Regression coverage now includes cached destination geometry before rows,
old native modes across rebinds, repeated late signals, unrelated input, cold
first-content and empty-folder pinning, explicit requests matching native,
rapid reversals and preserved DWM clocks. Existing mode simulations now use
explicit user intent. The runner accepts -TestArguments '--navigation' for
focused suppression/attachment/input checks; the default full suite retains
all assertions. Final 0.9.168 linked against Windhawk and passed both focused
and full suites. Earlier full attempts stopped at existing cached-property
and real native-viewport assertions; no unrelated assertions were removed.
Laptop swipe appearance/scan-out still requires live confirmation.
Preserved 0.9.167 SHA256:
B7932205679575B05C52FEF02F19F32D904D30C9714604D960ED3B1BFB80857C.

0.9.169 supports Tile mode in This PC only. All This PC icon-grid modes use
horizontal drive tiles on the existing grid packing, zoom, hit testing,
selection, grouping and RTL paths. Item geometry reserves an icon beside a
single-line name, themed usage track and muted capacity caption. Unknown
capacity leaves that geometry intact; percentage-only devices retain a bar.
Details/List retain their inline Name-column tracks. Tile elsewhere and Content
remain native, with the existing exclusive suppression/restoration paths.
The existing capacity worker formats fresh free/total bytes through Windows'
System.Computer.DecoratedFreeSpace schema, using the shared property formatter.
The caption is a seeded property cell, updated by ordinary disk notifications
and F5; no volume/provider call enters drawing. No worker, timer, cross-tab
state or artificial readiness wait is added. Grid content submissions pin
layout independently of the outgoing Details column width.
Regression coverage includes fresh real read-only This PC Tile attachment,
mode switches, native-only filesystem/Recycle Bin Tile and This PC Content,
LTR/RTL bar pixels, capacity/percentage-only/unknown states, stable asynchronous
enrichment, grid keyboard/hit geometry, zoom and preserved inline bars.
The Windhawk DLL linked and full suite passed on the final production source.
One attempt hit the existing intermittent cached-property assertion before
rendering; the unchanged binary passed on repeat. The optional
GPU_OVERLAY_DRIVE_TILE_PREVIEW fixture exports a GPU preview. Live Explorer
appearance and laptop behavior remain for user confirmation.
Preserved 0.9.168 SHA256:
7D66F1F2214DCC0876744D09173D4E6B2AF4A81AD9391D3B5ED7C19C55F6DED6.

0.9.170 replaces filenameOnlySelection with Grid only / Always on / Always off,
default Grid only, and adds the same policy for middle filename ellipsis under
Appearance.middleEllipsis. One scope predicate controls ordinary, hover, moving
and deleted-row selection. Text layout/raster/selection retain original UTF-16
indices. Single-line trimming uses DirectWrite's delimiter and preserves an
extension or up to eight trailing characters. Wrapped labels use the standard
inline ellipsis fitted to existing shaped clusters/line breaks once per changed
geometry; native delimiter trimming was observed failing on wrapped lines.
No repeated substring shaping, metadata/header trimming or rename change is
introduced. The existing per-tab LRU, raster readiness and retirement remain.
Appearance.listThumbnails defaults false and reuses per-file icon workers for
List/Details thumbnails at the requested icon-size tier, including 32px image
factory requests. Small thumbnail keys cannot alias the old per-file icon;
matching larger grid thumbnails can be reused unchanged. Ready icons remain
until replacement bitmaps exist. Existing thumbnail parsing/film strips/badges,
rounded corners and cheap shadows are shared; the list badge scales with its
small icon. Grid image data/decorations retain their existing behavior.
Settings queue one paced visible-demand follow-up. No worker, cross-tab queue,
timer or UI-thread file/provider query is added. Tests exercise both dropdown
scopes in actual selection, visible suffixes for wrapped/unwrapped LTR/RTL and
extensionless/leading-dot names, warm cached rendering, icon-sized rounding and
shadow pixels, real cold BMP thumbnails, normal-icon restoration and retained
grid images. Final 0.9.170 linked against Windhawk and passed the full suite.
Explorer appearance remains for live confirmation.
Preserved 0.9.169 SHA256:
817267E276739474009035F5AEF8B2A930779DF5E0C26943B889625E1D24B08D.

0.9.171 applies live edits to the current explicit sort or implicit enumeration
order through the existing directory/diff/180 ms row animation path. Item hints
observe bounded active-key snapshots on the tab's existing layout worker;
unchanged values avoid names replacement. Shell watch registrations include
child-file hints, and callbacks scope structural changes to either direct rename
endpoint. Local/external renames carry the destination identity and old index.
Worker-owned sort snapshots detect genuine moved keys even for pathless Win32
watch hints; cleanup executes on the worker. Such moves use the shared animated
visibility/scroll path, retaining selection and respecting intervening scroll or
captured pointer/manipulation ownership. Ordinary enrichment and unchanged keys
do not re-reveal a selection. Implicit-order folders are included and keep their
existing enumeration order. Cached This PC capacities reuse the same comparator,
row transitions and reveal calculation; stable-rank updates retain animation
clocks. There is no new worker, timer, cross-tab queue or UI property/volume I/O.
The focused --live-sort run and final full suite passed, including real private
file edits, explicit/implicit renames, actual Shell notification delivery and
Win32-watch settlement, animated rank/scroll follow, unchanged/overflow hints,
queue contention, selection and capacity updates. The final Windhawk DLL linked.
Earlier fixture attempts lacked a real Shell message receiver or assumed one
watch hint; both were repaired before the passing run. Live Explorer validation
remains separate. Renaming does not synthesize a changed modified timestamp.
Preserved 0.9.170 SHA256:
6657C430E9F33F10A5F268AF5562D75C9501AF3B5EEFD9D9E0F5D96E4CE50AE9.

0.9.172 repairs DLL lifetime after real Explorer dumps resolve execute faults
to DispatchHook in an unloaded image. All six API hooks track complete calls,
and Wh_ModUninit drains them after detour removal. Nontrivial thread-local scan
storage becomes module-owned, with trivial TLS pointers and explicit retirement.
All five callback COM classes track retained object lifetime; 34 COM methods,
WinEvents and the rename procedure share window-callback tracking. Owner-thread
retirement defers destroyed overlay state/resources until outer callbacks return;
the HWND detaches immediately and native suppression is restored. State(HWND)
rejects foreign UI threads. Startup failure rolls back factory/event/class state;
stale class procedures are replaced only when unused, and unload waits for class
release. Rename EDIT ownership survives overlay detachment through genuine native
NCDESTROY, with callback removal on failed close. OLE/touchpad admission closes
while the HWND is valid; apartment/resources retire afterwards. View-toggle
subclasses unlink explicitly. Cleanup also finds reparented owned overlays.
See tests/LIFETIME-AUDIT-0.9.172.md for evidence, audited areas and limitations.
Preserved 0.9.171 fails the new isolated hook-return lifetime test. Final 0.9.172
linked against Windhawk and passed the full suite, including 18 actual DLL unloads
before callback-thread exit with verified image unmapping, retained COM references,
foreign-thread rejection, nested window/editor destruction and failed-init cleanup.
The user's exact Explorer toggle/recompile sequence still needs live confirmation.
No worker, timer or cross-tab work queue is added. Lifecycle waits log outstanding
APIs/window calls/COM objects without frame-log traffic.
Preserved 0.9.171 SHA256:
294A95F467C33202864EEEE3FAFB101C9AD501CA765F1510D8BDDEDE36CFB75C.

0.9.173 follows navigation/keyboard traces showing GPU HWND visibility loss
during This PC handoffs and otherwise warm roughly 165 Hz keyboard submissions.
Transient native-host suppression/geometry failures retain the active tab's
committed surface with existing suppression and paced retries. Same-namespace
This PC native-HWND replacement retains rows, capacities, selection and motion
while the names worker revalidates. Actual native/unsupported/inactive paths
still own hiding. Keyboard retargeting carries elapsed position and velocity
through a monotone cubic Hermite using the existing 180 ms duration; fresh
movements retain ease-out cubic and shortened/reversed targets cannot overshoot.
Single selected items combine native focus/selection mark with the verified
selection transaction; multi-selection recovery remains. Optional pipeline
tracing adds highlight trajectories/stops, pending presentation causes, actual
GPU visibility requests and installed version. No new worker, timer, artificial
readiness wait or cross-tab queue is added. See tests/TRACE-FINDINGS-0.9.173.md.
Preserved 0.9.172 fails the new transient-host regression. Final 0.9.173 linked
against Windhawk and passed --navigation and the full suite, including 18 real
DLL unloads, repeated/reversed monotone selection arrival, native focus identity,
same-namespace replacement and all prior suppression/input/cache/lifetime checks.
The initial full run reached an old zero-geometry test that required GPU hiding;
its expectation now requires retained visibility and pending retry instead.
Live Explorer flicker/motion confirmation remains separate. The old capture did
not log highlight velocity or the caller requesting a visibility change.
Preserved 0.9.172 SHA256:
8267F0068027D353F6E33D99C385A30EE8C5797C1FB59D48B248896EDC143703.

0.9.174 follows the remaining arrow-selection capture to message-queue priority
inversion, rather than another easing change. Frames continue near 165 Hz while
eleven accepted arrow messages wait at least 100 ms (max 172 ms); selection
settles before delayed repeats arrive. The shared paced-frame paint now checks
the high word of GetQueueStatus(QS_INPUT). Pending old/new input leaves the
ordinary paint and its one outstanding wake held, preventing another posted
frame from outranking input. Normal Explorer accelerator/dispatch then precedes
painting. With no input, synchronous paced painting still prevents provider
traffic from starving rendering. Actual hides release the deferred paint latch
so tab/native switches resume; existing minimize/unload paths remain. Easing,
180 ms duration, pointer sampling, suppression and tab-owned workers retain
their behavior. No timer, sleep, message pump, API hook or worker is added.
The existing opt-in trace adds frame.yield.input. See
tests/TRACE-FINDINGS-0.9.174.md for QPC/record evidence, Windows queue semantics
and limits. Preserved 0.9.173 fails "paced frame painted ahead of pending
hardware input". Final 0.9.174 links against Windhawk and passes the full suite,
including old-input hints for keyboard/mouse/raw/touch/pointer, retained frame
and invalidation, key routing, duplicate prevention, one paint/ack, hide/show
recovery, pointer sampling, navigation, native selection and 18 real DLL unloads.
Offscreen tests use explicit retained-paint delivery and private queue hints;
they do not inject desktop input or establish live Explorer latency. Initial
fixture compilation/delivery and focused-graphics prerequisites were corrected
before the passing final run. The runner accepts --graphics for renderer/input
checks. Live arrow-motion confirmation remains with the user.
Preserved 0.9.173 SHA256:
925EFF50AA0CA48342B138CD87EB559D952DB764C7B9888E00E4A1BF47F38C6C.

0.9.175 follows external-gesture This PC/F:\ navigation with a native loading
placeholder flash despite retained GPU visibility. Queued WinEvents cannot be
a pre-paint barrier during native dispatch. The existing per-frame hook owner
now also owns a process/thread-scoped in-context presentation subscription,
registered with its callback's real module handle. A shared HWND-only helper
suppresses qualifying SHELLDLL_DefView/dedicated host creation, showing and
reparenting, and constrains owned DirectUI/ListView viewports. The queued
lifecycle path reuses that helper; discovery, COM, tab and selection work stay
asynchronous. Native mode, unsupported/inactive tabs and shared chrome are
exempt. An internal-visibility guard prevents intentional restoration from
reacquiring hosts. Callback tracking/owner-thread retirement and both ordinary
and emergency hook cleanup include the new subscription. No timer, worker,
file/provider lookup, text matching, GPU paint or navigation delay is added.
Optional tracing records native.suppress.event. See
tests/TRACE-FINDINGS-0.9.175.md for evidence, Windows event semantics and limits.
Preserved 0.9.174 fails "new native loading placeholder escaped before queued
lifecycle delivery". Final 0.9.175 links against Windhawk and passes focused
--navigation and the full suite, including twelve unpumped creation/reparent
cycles, immediate viewport suppression, scope/focus guards, prior input pacing
and selection tests, 18 API-return DLL unloads and a new actual-DLL subscription
test verifying suppression, host restoration, hook retirement and image unmapping.
The new DLL fixture initially omitted ViewState HWND initialization; that was
corrected before passing. Live external gesture behavior still needs user
confirmation. The captured log itself does not contain loading-label pixels.
Preserved 0.9.174 SHA256:
707D18AEBD999822FD98F51329056FBDF5FA5773BA1A9435D834B23870B9C3C8.

0.9.176 animates changes in the shared Hidden/dotfile/Cut content opacity on
the existing 180 ms cubic/compositor clock. Per-tab history retains only painted
viewport identities; new rows, directory tokens, returning offscreen items and
hidden/minimized surfaces establish their final opacity without an entrance
fade. An initial clipboard snapshot is also a baseline when optional startup
attaches after names are painted. Cut replacement fades old and new sets in the
same frame; overlapping dim reasons never compound. Reversals sample elapsed
opacity, and same-directory reorders preserve the motion by identity. Icons,
names, properties and drive capacity content share the same alpha. Opacity
motion retains paced paints independently of layout/scroll animations. There
is no new worker, timer, provider query or cross-tab state. Final 0.9.176 links
against Windhawk and passes focused graphics and the full regression suite,
including initial/late clipboard baselines, all three layouts, hidden toggles,
reversal, Cut replacement/cancel, overlap, reorder, navigation/hide resets,
viewport-bounded history, existing input/native suppression and real DLL unload
checks. Tests use private clipboard data and offscreen windows; live Explorer
appearance remains for user confirmation.
Preserved 0.9.175 SHA256:
8A30F0DD83B00A69A70EC29E280ED6928EA2C24427A00432384F95DC6D3E9A57.

0.9.177 is diagnostic only for the abrupt icon/label opacity jump on returning
from a grid child to a Details parent. The cause is unconfirmed; the isolated
parent-return check retains full row/content opacity. No speculative rendering
or animation change is made. The independent uiTransitionLogging option,
"UI transition diagnostics", defaults off and reuses the existing bounded
per-tab trace ring/writer. Full pipeline tracing remains optional; UI-tagged
events do not enable unrelated pipeline traffic. It records navigation/cache/
row publication/native rebind, returned-child selection, layout/column bounds,
palette, suppression/visibility, scene construction/retirement, actual sampled
draw opacities/coordinates, icon keys/bitmaps/fallbacks, text mask/direct paths,
EndDraw and commit/Present results. Existing paints sample the selected item
and first drawn row at most once per 8 ms for one second after relevant work.
This creates no paint, timer, Shell/provider query, GPU readback or cross-tab
queue. See tests/UI-TRANSITION-DIAGNOSTICS.md for capture/schema/limits.
The full suite passes, including UI-only routing, asynchronous writer reuse,
disable/re-enable/retirement, actual draw records, existing navigation/input
and DLL unload checks. The final source also links and passes focused graphics
after adding precise draw-coordinate records. Logs describe submitted state,
not screen pixels or scan-out; live reproduction/logs remain necessary.
Preserved 0.9.176 SHA256:
65F75A7B7CEFBFE309841C216F9B1404E0961D210052C49A126142855E3ECA3D.

0.9.178 follows the user's selectionOpacity=0 isolation to a reversed temporary
DirectComposition sibling order. AddVisual(child, TRUE, nullptr) inserts beneath
all siblings, so constructing background first left it ABOVE names/icons.
Both layer/header insertions now use one shared AppendCompositionVisual with
FALSE/null, matching ordinary back-to-front painting. Selection, hover, zebra
and drop/focus feedback stay behind names/icons/metadata. The ordinary marquee
fill/outline also moves before content while retaining full-width clipping,
document anchoring, fade and input behavior. Scrollbars and navigation gesture
feedback use the existing foreground chrome plane with logical viewport bounds.
There is no new surface, effect, worker, timer or animation clock; Cut/hidden
fades and the 180 ms mode transition remain. Diagnostics from 0.9.177 are retained.
See tests/LAYERING-0.9.178.md for Microsoft's AddVisual contract and test limits.
Preserved 0.9.177 fails the new real-GPU foreground pixel check. Final 0.9.178
links against Windhawk and passes the full standalone suite, including LTR/RTL
Details/List/grid selection/hover/overlap/drop/caret/marquee/fading marquee
pixels and prepared-plane replay through the production append operation with
a model of the documented sibling contract. Existing input, opacity, navigation,
suppression and 18 actual DLL unload checks pass. The first full run stopped at
an existing live Recycle Bin model-count check; the unchanged executable passed
the full suite on repeat. A manual restricted graphics run passed the new
layering checks but could not complete its private rename fixture; the full
rerun used the runner's normal execution permissions. The plane replay checks
submitted ordering, not live DWM pixels; Explorer confirmation remains separate.
Preserved 0.9.177 SHA256:
67CFA46C08688A5A9E4680C0B2C5E830871F63E94C5137D7BCE0EAC5619CF4CA.

0.9.179 moves the entire Details header vertically during Details/List/grid
mode changes, including cached cross-directory navigation. The existing 180 ms
absolute compositor curve now translates one full-width header child inside
the animated screen-space clip. Captions, dividers and sort glyphs keep their
Details X geometry and normal theme opacity. Per-column header X transforms
and the whole-header fade are removed; body layout and individual column
add/remove/reorder animations retain their existing paths. ColumnBounds shares
header geometry with caption layout and hit testing; reversals retain those
bounds instead of borrowing in-flight body positions. Grid icons do not change
the Name caption's retained list inset. This reduces header visuals and adds
no surface, effect, worker, timer, provider call or cross-tab queue.
Preserved 0.9.178 SHA256:
BB852FE2B2ABE5B3F1BD804D0393199D56E1FE159545C3A037E07719A4B30228.
Final 0.9.179 linked against the installed Windhawk host and passed the full
standalone suite, including translated header pixels in LTR/RTL Details/List/
grid, canonical source pixels, rapid reversals, normal column entrances,
cached cross-directory scenes through 65 ms owner-thread pauses, selection
layering, native suppression and 18 actual DLL unloads. Source-pixel and
submitted-scene checks do not measure live DWM scan-out; Explorer appearance
remains for user confirmation.

0.9.180 makes Group by headings sticky below the current Details header (or
at the body top in List/grid). LayoutGroupHeader replaces the separate capture/
paint interpolation and supplies shared position, height and opacity; the next
heading pushes the current one away. Hover, cursor and clicks use those visual
bounds; file hit testing excludes the pinned strip. All body planes clip below
that strip, preserving transparent/native-themed backgrounds without layering
an opaque fill over icons or names. The temporary compositor foreground planes
also receive one transparent COPY rectangle per plane after body drawing;
pixel checks exposed retained source pixels despite the surrounding body clips.
The blend/antialias states are restored, and no per-item clip is added.
Ungrouped paints keep the original clip count. Group captures include the pinned identity
when its natural heading is beyond overscan. The shared section layout adds one
empty list-row gap after each expanded group; grids use a compact heading-sized
gap. Gaps shrink with collapse and never enter entry counts, Shell selection or
hit testing. Packing/scroll extents use the same spacing. Item reveals reserve
room for the sticky heading, and marquee edges and rename placement use the
visible item viewport. No worker, timer, provider query, surface or cross-tab
state is introduced.
Preserved 0.9.179 SHA256:
017AA775412C6300AB87E47CAE3EB9877106360B28C455D4D501B8A80F2F7B35.
Final 0.9.180 linked against the installed Windhawk host and passed the full
standalone suite, including real-GPU sticky header pixels/foreground-plane
masking in LTR/RTL Details/List/grid, push/handoff and hit ownership, separator
bounds/collapse/capture/reveal, cached grouping/navigation, existing selection,
marquee autoscroll, native suppression and 18 actual DLL unloads. These are
offscreen pixels and submitted-scene checks, not live Explorer scan-out.

0.9.181 excludes group headings and section spacers from the zebra pass.
Visible natural/sticky/animated/retiring headings and fraction-scaled spacers
supply clipped document intervals; sorted intervals leave the same global
stripe slots in the remaining body. No stripe parity reset, item-driven stripe
motion, opaque fill or clearing of selection/marquee/content is introduced.
The ungrouped path retains its normal single stripe clip. Interval gathering
is viewport/overscan bounded, with no provider query, worker, timer or shared
state. Existing sticky header pixel tests now cover blank normal headings and
spacers over odd stripe slots during collapse, LTR/RTL Details/List, with
showEmptyRows enabled and disabled. Text readiness is warmed before comparing
caption pixels so direct/masked glyph readiness cannot mimic background changes.
Preserved 0.9.180 SHA256:
06FE767B29C6BDB27330038BF74CAF9A3F6E52D44F393D539A5AC0EEB7C12ACC.
The cold-scroll fixture now uses the existing queued-input and physical-pointer
fixtures while asserting immediate paced painting. Two prior runs reached its
no-extra-paint assertion with uncontrolled real input; that assertion must not
exercise the intentionally deferred input path. Real provider work/rendering
and the assertion remain, while separate input-deferral regressions still test
queued input explicitly. No production scheduling/input policy was changed.
Final 0.9.181 linked against Windhawk and passed the full standalone suite,
including unstriped header/spacer pixels, fixed stripe delete/insert behavior,
sticky/compositor headers, controlled cold scrolling, queued-input deferral,
selection, grouped navigation, native suppression and 18 actual DLL unloads.
The source-pixel checks do not measure live Explorer scan-out.

0.9.182 replaces offscreen/width-dependent column entrances and exits with a
single kColumnSlideDistance96 of 16 DIP, scaled through the existing DPI path.
Individual column captions, dividers and cached metadata fade while sliding;
removed columns remain paint-only on the existing 180 ms column clock. Rapid
re-additions retarget elapsed geometry/alpha, and horizontal-scroll clamping
preserves their displayed starting coordinates. Active interaction, provider
requests and cached column sets exclude these retiring records. Settled/cache
layouts reset presentation alpha. The whole Details header retains its vertical
container motion, now using the same short distance plus alpha; body spacing
still collapses/expands over its full extent. Ordinary and DComp rendering share
the fixed-distance contract and absolute clock, including grid mode/reversals.
No worker, timer, provider query or extra surface is introduced.
Preserved 0.9.181 SHA256:
7B7AC52E4E47E75D97BDED10E184A7DAD69C593C798198A18D374D0D8BB0EAD6.
Final 0.9.182 linked against Windhawk and passed the graphics and full standalone
suites, including header fade/source pixels in LTR/RTL, mode reversal, column
removal/re-addition continuity, cached navigation, grouping, native suppression
and 18 actual DLL unloads. The font-continuity fixture now establishes each
sample as displayed with its own preparation budget; one initial run exhausted
the shared setup budget before displaying the second sample. Production text
readiness policy is unchanged. These are isolated pixels/scene/lifecycle checks;
live Explorer appearance remains separate.

0.9.183 limits thumbnail radius to min(8 DIP, 20% of the displayed shorter
edge), through the existing global maximum and shared list/grid icon draw path.
Small thumbnails keep visibly rounded corners with straight edge sections;
zoom/aspect/DPI changes follow actual displayed dimensions without new work or
resources. The cheap shadow shares the adjusted radius, and film rails remain
inside the same rounded bounds. Existing thumbnail pixels now cover 16/32 DIP
list images at 96/192 DPI in LTR/RTL, including corners and non-pill top edges.
The Windhawk host DLL linked and the graphics suite passed. An initial run
passed all thumbnail checks but failed a captured-pointer destination assertion;
the unchanged binary passed the complete graphics suite on repeat. No input or
scrolling production/fixture code changed, and the full non-graphics suite was
not rerun for this drawing-only change. Preserved 0.9.182 SHA256:
8E8C6598A0B150CD816E9302B7416DDFBA354350AB4BAF8ECC8095396C7C7609.

0.9.184 uses the existing MoveScroll/180 ms clock for late native selection
imports, including Explorer Back/Forward history restoration. The former
CenterRowImmediately becomes the shared CenterRow with an animation parameter;
selection identity, centered destination and bounded clamping are preserved.
Unchanged native echoes retain the current animation, and later manual scrolling
retains viewport ownership. Hidden positioning and the disabled-smooth option
still settle immediately. Ancestor-return/resort setup and view-mode boundary
anchoring preserve their prior immediate geometry; the latter already owns its
mode animation and must not start a second viewport animation. No new polling,
delay, worker, timer or provider operation is added. Checks cover delayed native
imports, actual intermediate scroll positions, 180 ms settlement, unchanged
echoes, manual scrolling, hidden/instant paths and existing layout anchoring.
Preserved 0.9.183 SHA256:
BD9CC21DE4305152AC55A4C4B1E6881ECF1C1DAE81B2B9BABF6D70C5AE0E303C.
Final 0.9.184 linked against Windhawk and passed the complete standalone suite,
including late-selection scroll trajectories/echo ownership, cached navigation,
view-mode anchoring, native suppression, marquee and 18 actual DLL unloads.
These are isolated production-path checks; live Explorer confirmation remains
separate. The first full run caught view-mode anchoring using the new animated
reveal; that boundary now explicitly retains its existing geometry path.

0.9.185 gates item selection/hover, moving/fading/retiring selection, keyboard
caret, marquee, group hover and type-search feedback on actual input ownership.
One predicate recognizes GPU focus, its suppressed native list/children and the
existing active-frame keyboard recovery after modal commands. Address/search,
rename/editor controls and another foreground frame suppress feedback without
clearing logical flags, caret, hover or native selection. Shell drop-target
feedback remains independent of keyboard focus. The per-paint snapshot is
shared by ordinary and temporary DComp planes; focus changes redraw those
planes without restarting their absolute animation clock. Existing native,
overlay, frame activation/enabling and accessibility focus callbacks enqueue one
owner-thread, lifetime-tagged repaint per tab. No new hook, timer, worker,
provider query, polling or focus transfer is introduced.
Existing focus fixtures now supply logical frame visibility for offscreen
rendering and restore nested owner/enabled state. Real desktop focus is never
changed. Pixel checks cover inactive/active Details/List/grid in LTR/RTL,
selection/hover/caret/marquee, editors/sibling controls, preserved logical state,
focus return, deferred/coalesced updates, stale lifetime rejection and cached
composition planes. The invalidation check uses a visible offscreen NOACTIVATE
popup so USER retains a real update region; hidden child windows do not provide
that contract. Final 0.9.185 linked against Windhawk and passed graphics and the
full standalone suite, including modal keyboard recovery, cached navigation,
native suppression and 18 actual DLL unloads. These isolated checks do not
substitute for live Explorer appearance confirmation. Preserved 0.9.184 SHA256:
A7A25309D327F623F4C1564213E1C975BA1A2F95FA2FCB47D6D11D6BEE2F4B01.

0.9.186 fixes the confirmed title-bar focus discrepancy in 0.9.185. Bare
CabinetWClass focus qualified for highlights and arrow recovery, but Delete
was never routed. Frame/GPU Delete, Shift+Delete, Ctrl+C and Ctrl+X now reach
the selected native Shell group without changing keyboard focus. The former
DeferNativeSelectionKey becomes HandleNativeSelectionKey, sharing classification,
modifier capture, repeat suppression and its pending-selection queue with
immediate commands. The existing deferred InvokeCommand block is extracted into
one shared invocation path. Normal native-list shortcuts stay native. Native
HWND, overlay lifetime and view-token checks reject stale bindings before a
verb executes. No directory scan, worker, timer or polling is introduced.
Tests use captured IContextMenu/IShellView commands with real selection/PIDL
plumbing: title-bar Delete/Shift+Delete/Cut/Copy through accelerator/dispatch,
GPU Delete, modifiers, repeat suppression, editor/foreground guards and a view
change during menu resolution. They never execute real file commands. The
navigation checks and final full standalone suite passed, including pending
large selections, focus/selection preservation, native suppression and 18
actual DLL unloads. Final source linked against Windhawk; the live Explorer
sequence remains for user confirmation. Preserved 0.9.185 SHA256:
3A2D1694843DAF04D9AFC451D3B333BF29AF21E46747C07BEBA8B582453B9E28.

0.9.187 separates grid image scale from caption metrics. Wheel and pinch reuse
ApplyRowHeight for fractional input normalization and the existing 180 ms
transition. Grid zoom is bounded only by integer preference storage; Details/List
retain 24–96. Grid captions stop at the previous 96-height font metrics while
displayed images can exceed 512px. Shell image tiers and decoded/GPU cache budgets
stay bounded; enlarging an existing tier does not discard its bitmap or allocate
an image of the display size. Grid DPI/size arithmetic avoids integer overflow;
pinch accumulation uses double precision and retains sub-integer changes.
Regression checks cover LTR/RTL wheel/pinch beyond the old limits, intermediate
motion, capped font-format reuse, bounded image keys/requests, oversized painting,
zoom-out, list limits and separate preferences across native view switches.
Preserved 0.9.186 SHA256:
2CA487B05F4B4C3E8D8444488F483DD08504B661126F220EF268475550D6FBC2.

0.9.188 restarts the zebra background's phase at each section's blank heading,
so its first item is always striped regardless of preceding section lengths.
The shared band painter keeps document slots within each section, independent
of file identity and item motion. Sticky/retiring header exclusions clip those
slots without resetting their phase. Ungrouped striping, blank headers/spacers
and the existing trailing-height animation retain their paths. Pixel checks
sample alternating rows in LTR/RTL Details/List after odd-length, fractional
and collapsed predecessors, at natural/scrolled origins and with empty rows
both enabled and disabled. Final source linked against Windhawk and passed
graphics checks, 18 actual DLL unloads and the full standalone suite. The first
full run stopped at the existing live Recycle Bin count comparison; the unchanged
standalone binary passed on repeat (721 rows in both native views). No assertion
or native-model code was changed. Live Explorer appearance remains separate.
Preserved 0.9.187 SHA256:
7C3C03B92FDA25153672B259FCFF1492193BDA0F38C8837E410A74B30709B265.

0.9.189 audits runtime ownership/performance. Dedicated worker creation now
uses the per-tab submission/Stop gate after brief lifetime registration, matching
private-pool startup; another tab cannot wait on its OS thread creation under
the registry. Pixel retirement is shared by cancellation, per-item/global
invalidation, receipt and eviction, deferring large CPU buffer destruction to
the existing cleanup lane. Targeted cancellation preserves other request IDs
and generations. Snapshot/property accounting shares EntryPayloadBytes, including
property strings/PIDLs, with at most 24 distributed snapshot samples. Text LRU,
raster-budget and geometry-variant evictions use existing after-paint retirement.
Early GPU initialization failure clears its partial graph before retry.
tests/RUNTIME-AUDIT-0.9.189.md records the findings, retained ownership and limits.
The final Windhawk link and full suite passed, including blocked-start tab
isolation, exact pixel ownership on cancellation/invalidation, metadata-heavy
budget rejection, glyph eviction retirement, injected GPU failure/recovery and
18 actual DLL unloads. An earlier graphics attempt reused an HWND already owning
a composition target; only that fixture was corrected to use a private HWND.
Preserved 0.9.188 SHA256:
2D45AE139E7E207A3227E39ACA91B226A49549C62CFDCF1C62ED543E9FBC78C1.

0.9.190 repairs required Details columns being stranded behind optional startup,
header transitions or idle/input activity. NativeColumnSnapshotReady centralizes
the dependency: active supported GPU scope, names submitted, input services ready,
no speculative binding/header transition, dirty native columns and a bounded
provider retry interval. The existing coalesced, lifetime-tagged feature message
now also serves required column capture after optional startup has finished.
First content and both natural/forced header settlement wake it. Required column
reads run in the posted owner-thread handler before optional automation, with a
paced paint boundary before optional startup resumes. Provider failures retry
from the existing idle timer even while recent pointer activity defers general
maintenance. Native-only/inactive/out-of-scope contexts retain their guards.
RefreshExplorerColumns retains the view/manager and rejects navigation/rebinding
during native calls; genuinely dirty columns bypass unchanged-probe throttling.
perfLogging adds GPU columns SNAPSHOT with token/native/key count/startup step.
The focused fixture disables auto-fit and exercises names-first paint, pending
step-3 optional services, completed startup, natural and input-forced header
settlement, a temporarily unavailable provider, recent pointer activity, stale
capture rejection and eventual metadata/raster readiness without list input.
The final Windhawk DLL linked and the complete regression suite passed twice,
including 18 actual DLL unloads per run. Live Explorer confirmation remains
separate from the offscreen scheduling and rendering checks.
Preserved 0.9.189 SHA256:
0718306C161DBB61556E01A4A442367CAFF090CD1F96EB84B090985B9BCECD08.

0.9.191 repairs empty native lists after disabling the mod. A read-only live
inspection found the host and Shell view visible, their suppression properties
removed, but the DirectUI list child still 0x0. Native resize requests can echo
the suppressed physical viewport into the saved restoration bounds. The common
viewport interceptor now retains its latest nonempty layout. A direct Shell
presentation child captured before its first usable layout recovers from its
live parent's client area; nested/shared controls do not inherit guessed bounds.
The common suppression entry points reject restoration, disposal and unloading,
so reentrant callbacks cannot acquire fresh ownership during the handoff. Native
host suppression still precedes viewport layout; restoration still returns child
geometry before showing its host and preserves inactive/minimized visibility.
Performance logging records VIEWPORT_RESTORED and RESTORED with requested/actual
geometry or visibility and Win32 outcomes. Failed positioning/visibility calls
and rejected nonempty geometry log even when performance logging is disabled.
The new zero-size echo regression fails on preserved 0.9.190. Focused tests cover
real read-only namespace/filesystem controls, pre-layout fallback, reentry and
inactive/minimized ownership. The DLL fixture calls the actual disable lifecycle
and verifies nonempty native bounds before image unmapping. Live Explorer
confirmation of the patch remains separate.
The final Windhawk DLL linked, the complete standalone suite passed, and the
unload suite passed with 18 API-return lifetime cycles plus the actual-disable
geometry check. The first DLL fixture did not pump its UI apartment after
WinEvent unsubscription, leaving Windows' queued module retirement pending;
draining that fixture's messages while still mapped allowed verified unmapping.
No production unload barrier was relaxed.
Preserved 0.9.190 SHA256:
D1775266E151577D2B12D3A03B4F924AB96E67BEB7DCA270A8493BA06D09908A.

0.9.192 audits real Win32 RTL/LTR coordinate boundaries. Owned GPU HWNDs clear
inherited WS_EX_LAYOUTRTL during WM_NCCREATE and set WS_EX_NOINHERITLAYOUT,
retaining physical client pixels while shared GPU geometry and DirectWrite own
UI/text direction. Sibling EDITs and the custom status control use the same
physical convention. MapUiPoint replaces point ScreenToClient/ClientToScreen
paths for sampling, hover, cursor/drop hit testing, menus and diagnostics; RECTs
keep two-point MapWindowPoints semantics. Status placement/search accounts for
the frame's own mirroring, including fallback and native-button adjacency.
Touchpad X enters logical scroll/history space once; feedback edge/arrow and
release follow it. ShellMenuFlags standardizes popup RTL layout/alignment while
preserving background notifications. Existing drawing, native suppression and
per-tab lifetime/worker ownership remain; no timer/worker or global layout change.
The new genuine mirrored/LTR HWND matrix covers production attachment, pointer
messages/sampling, Details/List/grid hits, headings, scrollbars, horizontal pan,
grid marquee, mixed-script rename placement, status neighbor/fallback, history
release and physical native restoration. Preserved 0.9.191 fails its RTL pan
check. tests/RTL-AUDIT-0.9.192.md records the audit. Synthetic English fixtures
now declare their fallback UI language instead of depending on the desktop's
Arabic setting; explicit Arabic/English fallback checks and real mirroring stay.
Live Arabic Explorer confirmation remains separate.
The final Windhawk host DLL linked and the complete suite passed, including the
new actual-mirroring matrix, existing drawing/input/animation regressions, 18
actual DLL unload cycles and native restoration on disable.
Preserved 0.9.191 SHA256:
632272D4F94F3839AA266B36971A9D6310D18A5D063DB4F85BB0B70D946000B4.

0.9.193 fixes Cut opacity settlement across Details/grid changes and returns
list focus after the native status-bar view buttons. Clipboard updates compare
the bounded displayed-opacity history against the same Cut identity set used
by icon wiggle. A changed displayed Cut state retains the existing frame wake
until paint can start its inverse fade, so removing the last wiggle cannot
strand label/image opacity. Initial Cut/hidden baselines and hidden overlap stay.
NativeViewInput carries the recognized status control through its existing
dispatch transaction into the coalesced lifetime-tagged input-focus message.
Only the same active, visible, enabled foreground tab/directory can return
focus, and only while the source control or its child still owns it (or focus
is vacant). A later editor/control/tab/navigation/foreground change wins.
The preferred GPU/native list gets focus and hover reconciles under the cursor;
genuine focus loss still hides feedback. No new timer, worker or Shell query.
Graphics checks include actual compositor view switches followed by Escape at
the settled-wiggle wake boundary; preserved 0.9.192 fails the new pending-fade
wake assertion. The genuine mirrored/LTR HWND matrix exercises status focus
return, GPU/native preference, queued-message coalescing and stale/editor/tab/
foreground guards without moving real user focus or changing the clipboard.
Final Windhawk linking and 18 real DLL unload cycles passed. The first full
run stopped at the existing native-creation foreground/focus assertion; the
focused navigation test and full unchanged binary then passed. Live Explorer
confirmation remains separate.
Preserved 0.9.192 SHA256:
4457018B8F7F68E2870AE777706FA802B26A11EB11CC97052AAFAB729E747A52.

0.9.194 replaces the incomplete 0.9.193 status-button focus return. Status
actions can be addressed to a Button, a shared status/frame HWND, or delivered
as sent BN_CLICKED commands. NativeViewInput recognizes these through the
existing status neighbor/physical switch bounds and frame subclass, without
localized labels or UIA/provider discovery. Its per-tab, lifetime-checked input
depth defers reentrant mode probes until native dispatch finishes. The pending
focus intent retains the post-dispatch focus, directory and tab identity.
Earlier focus notifications and Shell message pumping cannot consume it before
the mode/presentation transaction finishes; the same coalesced focus message
resumes after commit. Native focus rejection falls back to the visible GPU
window. A newer control/editor, tab, directory or foreground change still wins.
Bare-frame recovery now routes every recognized list-owned key, including
Escape, consistently with its accepted highlight/focus ownership. Logical
selection remains intact; genuine focus loss still hides selection/hover.
There is no new timer, worker, foreground override or directory scan.
Performance logging adds GPU focus VIEW_COMMAND/VIEW_COMMIT/VIEW_RETURN/
VIEW_RETURN_CANCEL records with actual focus/source/native handles and context.
The expanded genuine mirrored/LTR fixture reproduces premature consumption on
preserved 0.9.193, and checks actual layout commits, nested mode-probe message
pumping, direct/shared/frame/sent routes, rejected native focus and stale guards.
Private clipboard checks cover bare-frame Escape in accelerator and dispatch
routes. The final Windhawk DLL linked and the full standalone suite passed,
including 18 actual DLL unload cycles and native restoration. Live Explorer
confirmation remains separate. The initial expanded fixture needed its fake
rename guard applied after mode commit, because normal layout work correctly
rejects an invalid synthetic rename session; no production guard was relaxed.
Preserved 0.9.193 SHA256:
566919492D29D4E91025E045AD3315330D837A1BD7DD2368CF4C81C3E6B76F21.

0.9.195 follows the Delete/Undo capture to thirteen explicit native-selection
fallbacks for 82 selected items out of 85. Large external partial selections
now share the existing prepared-selection lane: the owner STA copies raw
relative PIDLs in bounded/time-limited chunks, then a fresh worker-owned folder
resolves identities. No live view/enumerator crosses apartments. Results check
generation, native HWND, active scope, count and intervening selection hints.
Changing snapshots retry without detaching; a newer GPU click cancels imports.
The authoritative set is applied without selecting it again in Explorer. File
commands/context menus cannot act on an unsafe or unresolved imported set.
Selection capture/resolve/ready/retry pipeline records expose this handoff.
The 128-item failure-recovery cap, large Invert Selection handoff and filtered
Ctrl+A fallback are removed; transient menu/count mismatches reconcile through
the same path. The 64-item threshold remains a UI-work threshold, not a total
supported-selection size. This PC's direct identity lookup no longer assumes
at most 64 devices. The audit also removes the 8192-path Cut-visual cutoff,
128-visible/1024-available column limits, 256-character search-prefix reset and
2048-folder history ceiling. Clipboard reads release ownership between short,
sequence-checked batches and publish atomically; discarded path sets reuse the
cleanup lane. Column mapping ends at the provider/API boundary with cancellation.
Folder retention uses its byte budget, charging map node/key overhead; pressure
victims are ranked once instead of rescanning all history for every eviction.
Per-tab workers, memory/texture budgets, native suppression and animation easing
remain. No new worker lane/timer or cross-tab operation queue was added.
tests/LIMIT-AUDIT-0.9.195.md records all findings, retained limits and trace
boundaries. The capture also has ~1-second native SysTreeView32 dispatch spans;
ordinary drawing is around 0.4-0.5 ms. The inclusive right-button span includes
popup tracking. This patch does not establish that all native Undo stalls are
eliminated; no native navigation-tree suppression or timing change was made.
Private checks cover external 82-item imports, same-count retry, cancellation,
large invert, 9000 Cut paths, 1100 available/200 visible columns, 2100 cached
folders under budget, LRU pressure and the existing independent-tab/lifecycle
suite. Final Windhawk linking, the complete suite and 18 actual DLL unloads
passed. Live Explorer confirmation remains separate.
Preserved 0.9.194 SHA256:
71BE7703979858291994C240979DE453B07B1852459A9C20D04A447385018E8F.
