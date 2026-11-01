# Single drag preview

The supplied screenshots show Shell's automatic image above the mod's popup.
The GPU path used `SHDoDragDrop`, whose documented behavior is to generate a
generic drag image when the data object/window does not already provide one.
Supplying a custom `IDropSource` alone does not disable that behavior.
[Microsoft's SHDoDragDrop documentation](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shdodragdrop)
describes the automatic fallback.

When its popup exists, the shared source now invokes OLE `DoDragDrop` directly
with the same native Shell selection `IDataObject`, source, allowed effects and
existing destination bridge. This avoids the image-producing Shell wrapper for
mouse, touchpad and promoted-touch drags, including external targets. If popup
creation is unavailable, the Shell wrapper remains the fallback without a GPU
image. No global helper visibility, foreign window classes, polling, injected
input or changes to another application's drag feedback are involved.

Artwork starts at four native icon widths and follows larger displayed icons
up to eight, using monitor DPI and the existing bounded image tiers. Its rounded
tile, border and tile shadow are removed. Shared artwork/emoji geometry fits the
actual thumbnail rectangle and anchors the operation emoji to its trailing lower
corner in LTR/RTL. Canvas bounds stay stable while enrichment replaces the icon;
the current equal-width emoji, count badge and compositor motion remain.

The mouse hotspot uses the actual artwork's bottom center rather than a cursor
size offset applied to the whole canvas. Touch places the entire canvas above
the promoted contact point. Placement uses the shared aspect-aware geometry,
clamps to monitor work bounds (including negative origins), and recomputes in
the same feedback call when moving the popup changes its monitor DPI.
Artwork is bottom-aligned within its fixed square so background replacements
keep that hotspot stationary even when their aspect ratio changes.

Selections use at most five actual item images: the grabbed item is upright,
and successive rear images have stable, path-dependent signs and 3/6/9/12-degree
tilts. This follows the overlapping, rotated intent of Apple's
[pile formation](https://developer.apple.com/documentation/appkit/nsdraggingformation/pile).
Only cached selection identities and visible rows are consulted; the source
does not traverse the directory or resolve provider names. Each selected image
shares the same bounded thumbnail demand and independently retained GPU texture.
Rotated square bounds reserve space before optional artwork arrives.

The description comes directly from the native target's
[`DROPDESCRIPTION`](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-dropdescription)
feedback, including its localized message and destination insertion. No English
operation strings or private resource IDs are embedded. The small feedback
record is read on the source STA; file contents and provider metadata are not
requested. Native target changes/leave notifications clear old descriptions.
The immutable caption layout derives its face/style/size from the view's header
format, wraps to the monitor's available width, emphasizes the destination and
uses the existing tooltip palette, including high contrast.
The caption's bounded monitor-width allowance is reserved in the transparent
canvas from drag start, so asynchronous caption updates cannot move the icon
origin. Screen-edge placement clamps visible content rather than that unused
allowance. Identical pixels in several stack layers share one GPU bitmap.
Count and caption text reuse the existing text-policy resolver with a separate
preview monitor cache. System mode uses Windows subpixel rendering on these
opaque backgrounds; Smooth and Aliased use the same overrides as list text.
The list's glyph-cache serial is unaffected by a drag crossing monitors.
Rendering-setting broadcasts also refresh an active preview. Pixel readback
checks both text regions in all three modes, including RGB subpixel coverage
and opaque backing. Color emoji remain regular-weight artwork.
As in the existing text-quality suite, active MacType overrides are recognized
and their effective mode/pixels are checked rather than assuming raw Direct2D
requests cannot be customized by the installed font renderer.

The flattened prohibition edge came from requesting semibold Segoe UI Emoji.
The installed face supplies regular artwork: native font matching reported
`DWRITE_FONT_SIMULATIONS_BOLD` for weight 600 and no simulation for weight 400.
A native offscreen comparison reproduced the flat edge only with synthesized
bold. Operation emoji now request regular weight; text retains its own styling.
The regression reference independently requests the original regular artwork
at the actual fractional widths used at 96/144/192 DPI. Comparing the old cached
glyph with semibold direct drawing had reproduced the same defect in both paths
and was therefore insufficient. Exact-size popup renders are also compared with
larger reference surfaces to detect clipped texels. Nearest-neighbor sampling
inherited from stationary list rendering was removed from this compositor-scaled
preview to keep lift/settle edges smooth.

The existing image request path accepts a drag-thumbnail demand. It uses the
same per-item grid thumbnail keys, tab workers, cancellation and cache budget,
independently of Details/List thumbnail preferences. The displayed artwork is
shared immediately; actual thumbnails supersede icons even if their native
pixel dimensions are smaller. An icon cannot displace an accepted thumbnail.

Tests use a real private PNG and native Shell image factory to verify thumbnail
delivery with Details thumbnails disabled. Offscreen GPU readback verifies no
tile pixels in the transparent margins, thumbnail aspect ratio/atlas boundaries,
operation glyph widths and centering, DPI/RTL geometry and retained GPU textures.
Touch tests substitute the modal OLE boundary while retaining native recognition
and selection data. These establish component behavior; live Explorer visual
confirmation of the single preview remains separate.
