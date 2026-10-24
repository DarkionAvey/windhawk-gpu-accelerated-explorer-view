# Paint layers and selection reconciliation, 0.9.210

## Drawing audit

The ordinary renderer previously painted column headers before the body and
interleaved each item's icon and text. A later item's thumbnail could therefore
cover an earlier label during overlapping reflow. Group headings in animated
column scenes were stored on the background surface, underneath item surfaces.
Only the pinned group band was explicitly reserved on those sources. Marquee
feedback preceded zebra striping; swipe feedback followed the scrollbars.

The final paint order is:

| Top to bottom | Contents |
| --- | --- |
| Scrollbars | Both axes, after navigation gesture feedback |
| Column headers | Captions, sort indicators, dividers and drag feedback |
| Group heading foreground | Current headings above retiring headings |
| Group heading feedback | All heading fills precede heading text |
| Item text | All labels/metadata, above all icons/thumbnails/drive bars |
| Item images | Ghost images before current images |
| Item feedback | Selection, hover, caret and marquee |
| Zebra | Document-anchored background slots |

The existing drawing/layout functions remain shared. A viewport-sized item
batch caches geometry and opacity once for the image and text passes. Column
headers paint late with a separate bounded glyph-preparation budget, preserving
cached-mask coverage during fades even when body preparation consumed its
budget. Headers and every visible group band reserve transparent pixels on
all body sources; they do not invent an opaque background over Explorer's
native theme. This includes narrow fitted-column sources and outgoing group
headings. The marquee retains its full viewport clip.

Animated mode/width scenes use the same hierarchy. A group-only surface is
allocated only for grouped scenes and is included in the existing surface
publication, memory accounting and teardown. Fitted column captions and edges
have a common header container above body/group layers. Existing DWM curves,
scrolling, RTL transforms and tab-owned resource lifetimes remain in use.
The final 0.9.211 refinement also orders the Name body source below metadata
text when Name is moved later in the column order. This uses a body-only visual
container and the existing Name/metadata sources, not another bitmap. Fitted
header visuals inherit the same horizontal viewport clip as their body.

The initial ordinary Details zoom pixel probe did not reproduce the reported
live column-header overlap. The audit found the concrete ordering weaknesses
above; the new contract explicitly protects column and group bands even if
item geometry grows outside its allocated cell. The preserved 0.9.209 fails
the new natural-group-band pixel assertion with oversized content. Tests also
cover real animated zoom in/out at fractional phases and scrolled partial rows,
and opaque label pixels overlapped by a neighboring reflow thumbnail. Existing
mode-slide/fade, column fitting, zebra, sticky heading and feedback tests remain
part of the gate. These are offscreen source/pixel tests, not monitor scan-out.

## End and Ctrl+A evidence

Read-only inspection of the user-specified Downloads folder found 236 raw
filesystem entries. The running GPU view held 235 rows; Explorer's native view
had 234. The GPU's last row was a hidden Office owner/lock file that the native
view omitted. The native view also omitted protected `desktop.ini`, which the
GPU correctly excluded. Global hidden-file visibility was enabled, so the
per-view difference cannot be inferred merely from that global setting.

`tests/inspect-shell-view.cpp` queries counts, identities, flags and command
support without invoking a command. `tests/inspect-gpu-view.cpp`, compiled
against the running published source, reads bounded state/last-row snapshots
using `ReadProcessMemory` and rechecks vector/lifetime stability. Neither sends
keys, selects items, changes focus, writes to another process, or modifies
user files. No live End/Ctrl+A or file operation was executed for this probe.
Fixture tests reproduce the reconciliation conditions privately.

An unverified GPU selection commit previously imported whatever selection
Explorer retained. That could reveal an unrelated earlier item and cancel
End's requested viewport. It now uses the existing safe recovery: block
unverified file commands, wait for input/operation/names completion, clear both
selections, and retain the viewport. It does not detach the overlay or hide the
Office file through a guessed filename rule.

Large native Select All previously used a count-only shortcut even when its
native universe differed from the GPU snapshot. The mismatch refreshed the
same filesystem names and eventually rejected a valid native selection.
Filtered all-selected sets now use the existing bounded raw-PIDL capture and
tab-owned worker to map actual native identities. They preserve scroll and do
not construct a large `IShellItemArray` on Explorer's UI thread. Identical
native/GPU counts keep the existing optimized path.

Ctrl+A also cancels older bulk/import/recovery generations before native
handling. Accelerator and dispatch observations carry the originating folder
token; an old message cannot adopt selection into a new folder using the same
HWND. An early posted observation retains an event/idle continuation instead
of treating old selection as command completion. The 0.9.212 completion check
also retains that continuation when `ItemCount` temporarily fails instead of
returning a count. Native-focused file commands
retain Explorer's command ordering; the GPU-focused fallback blocks commands
from its obsolete painted selection. An unchanged small-selection idle check
cannot skip verification while selection is marked unsafe.

## Validation

Focused commands use `tests/run-regressions.ps1 -SourceFile 0.9.210.cpp` with
`-TestArguments --graphics` or `--selection`. The graphics negative control
uses `previous-versions/0.9.209.cpp` without modifying it. Selection tests use
private temporary files and substituted native interfaces, keyboard state,
clipboard and focus APIs; they do not depend on the user's directory.

New selection cases exercise a filtered all-selected universe, generation
cancellation, stale folder messages, early command completion, unchanged unsafe
selection, no UI Shell arrays, retained viewport, actual End target resolution
against rejected native selection, blocked commands and eventual two-sided
reset. Final release-gate results are recorded in the changelog. Live Explorer
confirmation remains separate from these component/integration checks.

The complete 0.9.210 release gate passed repository workflow checks, Windhawk
DLL linking, the complete C++ suite and all 21 actual DLL-unload cycles. It
measured 1,483.52 MiB at twenty warm 4K tabs, 1,904.76 MiB peak and 9.86 MiB
after closure. Three cached activation calls peaked at 0.3995 ms CPU time in
the private attachment fixture. All thirteen earlier archives and README
retain their hashes. The final 0.9.211 gate also covers the moved-Name source
ordering contract; its final results are recorded in the changelog.

The final 0.9.212 release gate passed the complete repository, Windhawk DLL,
C++/graphics/input and 21-cycle DLL-unload checks. The injected transient-count
failure test fails on the unchanged 0.9.211 archive and passes in 0.9.212.
Final memory measurements remain 1,483.52 MiB warm, 1,904.76 MiB peak and
9.86 MiB after closure; cached activation peaks at 0.3989 ms CPU offscreen.
Fifteen earlier archives and README retain their hashes, and `latest.cpp`
equals the tested `previous-versions/0.9.212.cpp`. Live behavior after loading
the release still needs user observation; the diagnostic did not operate on
the user's selection or files.
