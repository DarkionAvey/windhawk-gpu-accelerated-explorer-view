# File-change animation presentation

The supplied `351c386e` trace still presents row movement through the owner's
swap chain: `ui.submit` reports scene=0 while `rows.retarget` is active.
Explorer's native `0xB04` dispatch takes 125.28 and 174.94 ms in this capture.
Tree commits can also occur 14–24 ms apart. The previous patch corrected the
retarget clock, but an accurate clock cannot present intermediate frames while
the same STA is inside a native call.

## Change

Ordinary file-change transitions now use the existing DirectComposition
device/target owned by each tab, as column fits already do. The existing Paint
functions draw canonical item pixels into packed, viewport-bounded atlases.
Separate feedback, icon and text layers preserve the foreground ordering;
section headings and column headers/scrollbars retain their higher layers.
Each surviving, entering and outgoing item receives compositor translation,
height clipping and opacity curves with the shared absolute QPC origin and
180 ms cubic. DWM can sample those curves without dispatching another owner
message or calling Paint.

The first origin is assigned after successful source drawing/upload, immediately
before committing the scene. Enrichment repaints update those same surfaces
without restarting the committed curves. Names commits use the existing
identity/geometry path. At the actual replacement commit, surviving identities
are sampled again from the previous committed compositor scene, and both its
replacement curves and owner geometry receive that position and a common new
origin. Time spent waiting to paint cannot turn a snapshot-time position into
a presentation jump. Viewport/hit/header captures also sample the current
compositor clock rather than the last owner-submitted progress.

Packing only includes items whose source-to-destination interval intersects
the viewport. Storage is bounded independently of directory size, retains
small allocation headroom, and is reused across names commits and scrolling.
No viewport texture is allocated per item. Existing GPU memory estimates now
include the feedback atlas. The accepted scene stays attached until a regular
settled frame replaces it. Warm-tab retirement bakes that frame and uses the
existing composition-domain recycling to reclaim temporary surfaces without
discarding the warm device or cached tab image.

Shell objects, caches and pixel preparation remain on their existing owner
lanes. This adds no provider thread, polling timer, UI wait, shared tab lock,
or independent logical selection model. Native Explorer can still delay the
arrival of a new model or the beginning of an animation. Active zoom/layout
gestures, column transitions and actual composition allocation/device failure
retain their existing render paths; this change does not make those paths or
all native Explorer UI independent of its STA.

## Validation

`tests/row-composition-smoke.h` exercises actual production transitions and
DirectComposition resources in private LTR/RTL Details/grid targets, including
grouped views, deletion ghosts and restored entrances. It checks nonempty
feedback/icon/text/heading pixel sources, bounded storage, source and origin
stability, scrolling, a delayed names-commit handoff, regular-frame handoff and
warm standby cleanup. During
an intentionally unpumped 80 ms owner interval, DWM's frame clock advances
while the paint serial, model progress, scene root and source pixels remain
unchanged. A second unpumped interval exceeds the entire animation duration.
These are compositor-contract and offscreen pixel checks, not a measurement
of displayed Explorer frames or monitor scan-out.

The full release gate also covers the existing input, selection, filesystem,
layering, concurrent-scroll, 4K tab-memory and DLL unload tests. Half of the
twenty private 4K tabs retire a live row scene in the memory lifecycle test.
Release results
and output are recorded in CHANGELOG.md and `tests/build-0.9.216.log`.
