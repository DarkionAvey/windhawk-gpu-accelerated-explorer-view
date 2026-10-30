# Grouping animation: 0.9.302

A native Group by choice can also change the Shell sort key. After 0.9.301
started observing that sort correctly, `CaptureItemSnapshot` rejected the old
viewport because displayed and requested sorts differed. The layout worker
therefore received neither old item positions nor section headings, and
`StartGroupingTransition` rejected the missing row metrics. The new layout
appeared immediately.

Capture now preserves the bounded displayed viewport even when the requested
sort changes. The existing worker maps file identities to the new order and
the existing commit/compositor paths animate items and headings. The same
capture supports Details, List, grids, regrouping and Group by None; no new
animation path, polling or provider work was added to the UI thread.

The native-menu regression invokes Windows' own background `IContextMenu`
provider for private filesystem, ZIP and delegated folders on an isolated
desktop. Across Details, List and Medium icons it groups by Size, reverses
direction, changes to Type and chooses None. Commands use native offsets and
the column catalog; None is the unique non-property choice because Windows
does not supply it with a canonical verb. No fabricated command messages or
user folders/focus/clipboard are used.

Checks cover old positions and opacity for surviving identities, intermediate
item/header geometry, outgoing headings on a property change and None, and
the immutable renderer model. Actual GPU row scenes must publish item/heading
visuals and nonempty heading pixels; None must retain outgoing heading
visuals. Only the mod's clock is controlled to verify animation completion at
its shared duration. Deferred glyph-cache work may remain pending during a
transition while DirectWrite supplies immediate drawing.

The new native command test fails on published 0.9.301 with
`native grouping change skipped the shared item/header animation`.
The corrected focused native-menu suite passes all 36 grouping transitions
and 36 displayed popup comparisons.

The first two full builds stopped at the existing GPU standby retirement
check, before publication. The same assertion appears in earlier 0.9.217/0.9.279
release logs. Both retained 0.9.301 and 0.9.302 binaries passed
`--gpu-lifecycle` in isolation; this fixture does not call the changed snapshot
capture. The complete graphics fixture followed by lifecycle also passed.
Lifecycle diagnostics now record the failed phase and the last 64 actual
callbacks without changing its assertions. The standby failure's cause remains
unresolved; no standby production change is claimed here.

Final validation: `build.bat -SourceFile 0.9.302.cpp` passed repository checks,
the installed Windhawk DLL link, callback stack checks, the complete C++ suite
and real DLL-unload tests, including standby retirement. The tested source was
published locally as 0.9.302. The successful full log is
`tests/group-animation-302-release-diagnostic.log`; the two earlier full failures
remain in `tests/group-animation-302-release.log` and
`tests/group-animation-302-release-retry.log`.

These are private Shell/GPU integration checks, not live Explorer captures or
measurements of displayed frame rate.
