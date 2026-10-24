# Transparent text, pixel alignment and switch background, 0.9.229

The user chose to preserve transparent/custom Explorer backgrounds and accepted
grayscale antialiasing instead of requiring RGB subpixel text. ClearType needs a
known opaque background; enabling it on the current transparent swapchain or A8
glyph mask would be incorrect and cannot preserve arbitrary native backdrops.
See [Microsoft's alpha-mode guidance](https://learn.microsoft.com/en-us/windows/win32/api/dcommon/ne-dcommon-d2d1_alpha_mode#cleartype-and-alpha-modes).

## Native text rendering

Keep DirectWrite shaping, fallback, wrapping, trimming, RTL and the existing A8
mask cache. Explicitly obtain native monitor rendering parameters with
`CreateMonitorRenderingParams`, falling back to native defaults. Use those
parameters in both direct rendering and mask generation/composition. The Windows
choice also respects system font smoothing being disabled. The dropdown adds
smooth downsampled natural grayscale without grid fitting, sharper natural
grayscale with grid fitting, and aliased text. Custom choices retain native
gamma/contrast rather than inventing a font engine or copying a browser renderer.
The native APIs are documented by
[DirectWrite](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefactory-createmonitorrenderingparams)
and [its rendering modes](https://learn.microsoft.com/en-us/windows/win32/api/dwrite_3/ne-dwrite_3-dwrite_rendering_mode1).

MacType's [DirectWrite implementation](https://github.com/snowie2000/mactype/blob/directwrite/directwrite.cpp)
intercepts the standard rendering-parameter/antialias APIs used here. Compatibility
therefore follows that integration; a GDI-only profile does not replace GPU text.
The private test process already had the desktop's MacType64 components injected.
Its profile overrode the aliased request with grayscale and replaced the supplied
rendering parameters. Tests explicitly verify matching effective policies on the
main and mask contexts, transparent output and stable cached pixels under that
override. MacType retains control; the dropdown describes that font customizers
can override its choices. No MacType configuration was changed, and other profiles
or live Explorer injection/appearance were not tested.

Raster variants include a rendering serial. Changes to mode, monitor or Windows
font settings invalidate masks lazily through the existing bounded per-tab LRU;
there is no whole-directory shaping/sweep, new worker or global rendering lock.
Every source context uses the same policy, including row/column animation planes.

## Pixel alignment and icons

The old text code rounded local coordinates before fractional source-plane
translations. The icon path did not snap and always used linear interpolation.
List/Details image demand also stayed at 32 pixels even above that physical size.

Shared draw helpers now map positions through the current axis-aligned transform
and DPI, snap in device pixels, and map back. Filled edges and stroked centers use
their appropriate pixel phases. Bitmap destinations snap their edges; an exact
1:1 icon uses nearest-neighbor sampling and scaled icons use high-quality cubic
filtering. Thumbnail frames/brush placement follow snapped geometry. List and
Details use the same image-demand tiers as grid views, on their existing workers.
Shell providers can still return limited-resolution artwork at extreme zoom.

All composition visuals use nearest-neighbor bitmap sampling to avoid a second
blur when already-rasterized pixels are translated. Logical layout, hit testing,
shared animation curves and concurrent scrolling remain continuous. Deliberate
icon rotation, antialiased curves and soft thumbnail shadows retain those effects.

## Switch background and invisible draws

The native child switch previously filled its entire rectangle with a palette
color. It now uses transparent-child paint ordering and paints the actual parent
backdrop. Buffered output retains correct alpha for native composited icon pixels;
only hover/press/focus feedback adds its own visible decoration.

`VisibleOpacity` is shared by the reusable fill/text/icon/stroke paths. Invisible
source-over draws return before font shaping, icon-key/cache lookup or GPU work;
new opacity settings should feed those same primitives. Explicit transparent
COPY/clear operations remain necessary to erase stale source pixels and are not
treated as invisible decoration.

Private GPU/GDI tests cover native parameter selection, real glyph coverage for
all choices, transparent corners/no RGB fringes, warm-cache stability, exact 1:1
icon texels in LTR/RTL and fractional source transforms, DPI mapping, resolution
tiers, invisible-work elision and an actual patterned parent behind the switch.
Existing text/layout, independent renderer, animation/input and unload suites
remain enabled. These tests are offscreen component checks, not a claim about
live Explorer appearance or displayed frame rates.

The focused `--render-quality` suite and `build.bat -SourceFile 0.9.229.cpp` pass.
The full gate includes repository/export guards, installed Windhawk DLL linking,
all 34 option contracts, independent text/clipping/selection/layout comparisons,
all offscreen GPU/input/animation suites and actual DLL unmapping, including
21 API-hook cycles. Callback stacks remain within 8 KiB (maximum 1,960 bytes).
The existing MacType profile supplied the effective grayscale policy in this run;
this is not evidence of aliased output while that external override is active.

Offscreen CPU submission profiles report 80 translated text cells at 0.1014 ms
median and the warm 793-row/six-column fixture at 0.3244 ms median. These are
component CPU measurements, not displayed latency or live Explorer frame rates.
SHA-256 checks confirm latest.cpp, gpu-accelerated-explorer-view.wh.cpp and
previous-versions/0.9.229.cpp are identical; all 32 earlier archives are unchanged.
Local logs are `tests/render-quality-0.9.229-retry.log` and
`tests/build-0.9.229.log`; failed diagnostic runs are retained separately.
