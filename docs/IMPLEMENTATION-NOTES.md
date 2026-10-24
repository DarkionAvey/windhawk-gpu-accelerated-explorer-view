# Implementation notes

This overview was retained from the 0.9.195 documentation. Release changes and validation results are in [CHANGELOG.md](../CHANGELOG.md).

The overlay supports filesystem folders on local drives and network shares.
Virtual Shell folders except This PC and Recycle Bin remain native. The original Shell view stays
alive underneath. Header sorting, column visibility and divider resizing
are committed to Explorer's native folder-view settings. File operations
are performed through the native Shell interfaces.

Ctrl+Alt+F9: show/hide the custom list in the focused Explorer window.
Escape cancels a pending Cut through the clipboard. A persistent switch-arrow
icon in Explorer's bottom status area follows the current tab and theme.
Its English tooltip describes the action. It disappears when the mod is unloaded.

Current features: GPU-capable Direct2D/DirectWrite rendering, visible-row-only
painting, worker-thread names-first directory enumeration and sorting,
visible-row-only background metadata and lazy background Shell icon loading,
configurable row height, VSync-oriented smooth scrolling, draggable scrollbar,
Explorer-managed dynamic Details columns, filename-only selection and custom opacity/radius,
group selection (Ctrl/Shift/marquee) with verified native Shell selection
synchronization and native clipboard Cut dimming, accent state palettes, Ctrl+wheel proportional text scaling,
in-tab Shell folder navigation with parent-folder selection restore, ShellExecute-based file opening, and native
Shell item/background context menus (file rows versus both edge margins).
Some uncommon Shell accelerators are not yet implemented. File drag-and-drop
uses Explorer's native Shell IDataObject and IDropTarget implementations for
single and multi-item drags, including external apps and folder targets.
New folders initially display filesystem enumeration order with
no sort chevron; clicking a column header enables sorting and also updates the
native Shell sort order. Use the instant native-view
fallback for unsupported Shell accelerators.

Because this is an injected prototype, test with unsaved work closed. Explorer
and namespace providers can differ in behavior across Windows builds.

Rendering uses a DirectComposition premultiplied-alpha swap chain. Each
Explorer tab owns a separate GPU window, swap chain, view state and cache. Each distinct
overlay remains a child of the stable Explorer frame, with tab-specific Shell
browser identity and explicit per-tab visibility. A tab switch restores the
existing GPU surface without rebinding another tab's pixels or view state. While active
it hides the dedicated native folder-content host (CtrlNotifySink above
SHELLDLL_DefView). ShowWindow/ShowWindowAsync and direct/deferred positioning
requests cannot make that host visible. A host subclass constrains visibility
styles before layout. The whole native file-list subtree stays absent, including
loading and empty-folder content; no post-paint repair is required. The Shell
COM model stays alive and the navigation tree is unaffected. The latest Shell
visibility intent is restored for Native view and on unload.
Each tab's transparent overlay remains alive across folder navigation;
new native hosts are suppressed as soon as their Shell view is announced.
Native view is always restored when the overlay is disabled or the tab
leaves the the Windows installation directory test scope.

Touchpad pan and pinch are handled through the per-overlay DirectManipulation
viewport. Scale and translation are classified as distinct gestures, as in
Chromium's DirectManipulation handling, to keep pinch movement from scrolling.
Scrolling follows Windows' DirectManipulation translation inertia after finger
release, without a second easing curve or custom friction settings.
Optional two-finger horizontal swipes navigate after release if their deliberate
threshold is reached (without waiting for DirectManipulation inertia). The tab's
IShellBrowser remains responsible for history navigation. Normal mouse wheel and
Ctrl+wheel scaling are preserved. Only dedicated native content hosts are subclassed for the visibility barrier.
The active Shell view supplies its real SVGIO_BACKGROUND context menu for
right-clicks in either edge margin or below the final item. There are no
hardcoded New/Paste/Refresh labels or invented Shell commands.
The marquee can cross both edge margins and autoscroll at the vertical viewport edges.
Edge autoscroll is driven by the same compositor-paced frame worker as the
wheel animation (no fixed 16ms timer). High-resolution elapsed time controls
velocity independently of refresh rate, with a cap against stalled-UI jumps.
Native Shell selection synchronization is deferred to a coalesced message
rather than being invoked directly from each animation frame.
The configurable content edge margin is symmetric at the logical start and
end of the list. Full-row zebra and selection fills stop at these margins when
rounded, but reach the paintable viewport edges when selectionRadius is zero.
Item text/icons, columns, and item hit targets always stay within the margins.
Clicking either margin clears selection; right-clicking there opens the native
folder context menu regardless of whether a highlight is painted underneath.
The separate scrollbar track keeps its normal input handling. Geometry follows
Shell UI reading order (RTL for Arabic/Hebrew Explorer, LTR otherwise).
Native Shell focused-item and selection markers follow overlay selection so
preview tools can find the intended file via standard Shell view interfaces.
Ctrl+wheel scales real icons as well as row height, font, and text indentation.
Grid images can grow beyond Explorer's largest icon size; captions stop at the
existing maximum font size. Details/List keep their original zoom limits.
Interactive zoom persists across folder navigation in the same tab, with
independent levels for Details/List and icon grids. New tabs start from the
mod settings. All zoom overrides are RAM-only.
This is a best-effort compatibility layer, not universal third-party UIA emulation.

When returning from a nested folder to any ancestor, the overlay centers and
selects the immediate child folder on the previous path. A RAM-budgeted,
per-tab cache retains directory snapshots (including optional user sorting)
across visited folders without a fixed three-folder history limit. Cache hits
restore the old GPU rows without enumeration or sorting. The worker revalidates
the cached view asynchronously. On a cache miss, the background worker locates
the child without a UI-thread filesystem or native-index scan.

Each tab's frame-child GPU surface remains bound to its own Shell browser;
the overlay HWND and DirectComposition resources are reused across folders.
Shell-view lifecycle events schedule an asynchronous rebind even
without mouse or keyboard input. Explorer still owns Shell navigation; an
unmeasured single native first frame on first activation cannot be ruled out.

The column manager supplies the visible Shell column keys, localized names,
order and sizes; no fixed Date/Type/Size column settings remain. Uncommon
properties are resolved only for visible rows by background providers.
Details headers support drag-to-reorder with animated neighboring columns;
shaking a grabbed header left/right in its original slot refits visible columns;
the final order is committed to the native Explorer column manager.
The Details header opens a Shell-backed column chooser and resizing commits
to Explorer. Column overflow exposes a horizontal scrollbar. Clicking its
track animates on the display-paced frame worker; dragging the thumb remains
immediate. The Shell column chooser only restores widths differing from Explorer's
reported defaults (or explicitly resized in this overlay), avoiding needless
synchronous SetColumnInfo calls. Genuine custom widths are restored one at a
time after idle, keeping these Shell STA calls out of the menu's critical path.
New GPU headers update from cached labels without rereading all column
metadata; visible-row values load in the worker.
Column labels resolve on a dedicated per-tab worker and cache per Shell folder.
A cold chooser returns to input/paint dispatch and opens when its labels arrive;
new overlay input or a view/tab change cancels that pending menu. Metadata
jobs for obsolete column sets are invalidated. List mode uses name-only GPU
rows while other native view modes are passed through.

Each tab has independent enumeration, layout, watch, reclamation, native-name,
selection and column-catalog lanes, queues and cancellation generations.
Three private Windows thread pools each allow four concurrent providers for
per-file icons, visible-row properties and drive capacities. No provider pool
or queue is shared between tabs. Association icons use their own fast lane,
so executable extraction cannot delay a genuine Shell file-type fallback.
Specific per-file icons replace those associations as they arrive. Icon jobs
start with visible names, before optional startup services, and prioritize
the current viewport. Small ready textures upload in measured time/byte batches;
animations tighten the upload budget instead of blocking icons for 180 ms.
Each volume publishes capacity separately. Watch installation and discarded
snapshot cleanup cannot wait behind a metadata or storage provider.
Icon, property and capacity completions hold one notification until a paced
frame consumes their results. They never force a paint or reread native view
presentation per completion. Property delivery is bounded by count and time;
one viewport follow-up serves each frame. Visible rows precede metadata
overhang and queued requests outside the current viewport/overhang are
cancelled. Data continues loading during scrollbar grabs, while width fitting
waits for direct input to finish and uploads use the tighter motion budget.
Wheel, touchpad and marquee input only mark visible demand; the frame prepares
it once. Live Shell polling, automatic selection observations and optional
provider binding wait through active input/animation. Touchpad, clipboard and
drag/drop setup run first after content submission. This PC capacity changes
project the existing identities on that same frame without native view probes
or a forced paint, and retain extra-property requests already in flight.
Unchanged revalidation and ignored previews do not trigger presentation work.
A cold directory publishes names from its
first available row, then grows that provisional prefix to the viewport. Sorts
and groups are prepared separately and can reorder that prefix when complete.
Path-based folder double-clicks work in a confirmed names preview; native
selection and file operations wait for the authoritative snapshot.
New overlays submit content before clipboard, drag/drop, automation and
touchpad initialization. The tab's namespace worker enumerates This PC through
IShellFolder independently of the hidden native list. Filesystem paths seed
immediate rows; volume labels, capacity and icons follow as background data.
Virtual names resolve after known filesystem identities have been published.
Rows own absolute and relative PIDLs, so opening a drive and constructing a
Shell command do not depend on native row indices or native list readiness.
Date/type/size resolve for visible rows on demand. Cached rows remain displayed until
revalidation finishes. Unchanged order is reconciled without resetting the
scroll position or selection. When a new sort arrives, the old rows remain
visible until the worker publishes the new order, preserving the viewport's
top visible item and pixel offset when possible. Native selection is preserved.
Changing sort order reuses known property values; only missing or invalidated
properties load again.
Slow filesystem and type-provider work never enters WM_PAINT.
Group by reads Explorer's active Shell grouping property, drawing scrolling,
collapsible GPU section headers with the file-row font (including zoom) and
the usual themed header color.
This PC sections are built from its native Shell items. Group collapse stays
in the GPU view and does not modify Explorer's underlying collapse state.
The GPU overlay renders native Shell Details, List, and Small/Medium/Large/
Extra Large Icons views. Icon grids share the item geometry, selection,
marquee, keyboard, rename, scrolling, cache and per-tab worker pipelines.
Large labels wrap below centered icons; Small Icons uses compact cells.
Sized Shell icons/thumbnails load on the existing workers and retain separate cache
entries, while cached smaller icons remain usable during enrichment. This includes
This PC using a Shell-namespace GPU row snapshot and asynchronously
retrieved drive capacity bars beside drive names. Other virtual locations stay native. Switching to
Tiles or Content restores native Explorer immediately; returning to a GPU mode
imports Explorer's selection and focused row into the GPU surface. Only F2
or the Shell context-menu Rename command starts a tab-scoped native EDIT
sibling above the GPU visual. A mere click never arms a rename timer.
Rename is exclusive to the GPU view
and commits only through the active IShellFolder (not a hidden native edit).
Sort indicators appear only after the user chooses a GPU header sort;
column captions follow Windows native header font metrics.
Unsorted captions use a muted theme color; the active sort caption stays bright.
Explorer's native sort preferences are not imposed on an unsorted GPU folder. Header clicks
set the same native sort state. After an explicit header sort, changes from
Explorer's Sort menu can be sampled without directory I/O on the UI thread.
The directory worker sorts by the chosen key, with a background property-store
fallback for uncommon Shell sort columns.
The display-paced frame worker wakes for wheel animation, marquee edge autoscroll, touchpad gestures and fading feedback.
Navigation rebinding coalesces Shell view SHOW/WM_SIZE hints and timer retries:
WM_WINDOWPOSCHANGED is not a navigation trigger; the same native view is not
requeried on every size event. Incoming views get an immediate attempt and
bounded retries only until they bind; duplicate scans within one clock tick
are coalesced. Native host visibility is constrained before USER applies layout;
resetting a host region cannot expose it. The slow recovery
probe remains as fallback. Each overlay owns its graphics device and composition
target. Presentation uses nonblocking swap-chain submission with paced retries.
Closing a tab cancels its workers asynchronously; one lifetime regulator reclaims
finished groups. DLL unload waits for outstanding providers, host subclass removal and active window callbacks.
Metrics printed to Windhawk's log are CPU paint-call timings, NOT measured GPU
presentation latencies or a substitute for ETW/WPA profiling.
The separate Frame time logging option defaults off. It reports per-tab active
submission cadence, interval percentiles/max and CPU delivery/demand/upload/
draw/present stages, independently of Performance logging. Idle gaps are excluded;
submission rate is not a measurement of frames physically displayed by the monitor.
