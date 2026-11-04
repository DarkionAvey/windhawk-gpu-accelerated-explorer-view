# Text rendering recovery loop: 1.0.2

The affected-PC capture at 13:45:34 confirms a rendering-mode conflict:

- Requested and stored D2D AA: Aliased (3).
- Actual raster and frame target AA: Grayscale (2).
- Actual DirectWrite rendering mode: Aliased (1).
- Both A8 glyph raster and BGRA frame EndDraw return E_INVALIDARG.
- Device-removal status is S_OK, and hardware target recreation succeeds.

The mod requests Aliased but reads back Grayscale. The production code has no
separate assignment of Grayscale to these targets after their Aliased setup.
An injected font policy is a plausible explanation, but the capture does not
identify the component responsible for changing the effective setting.
Microsoft documents Grayscale D2D AA plus Aliased DirectWrite rendering as an
invalid combination:
https://learn.microsoft.com/en-us/windows/win32/direct2d/d1136

Use DirectWrite DEFAULT for the aliased option, allowing D2D to resolve rendering
from the effective target AA mode. The requested D2D mode remains Aliased and
monitor gamma/contrast and grid fitting are retained. The shared parameter path
also covers drag captions and tooltips.

The broader audit also found unvetted combinations in Smooth mode, native
monitor-parameter fallback after custom creation fails, opaque tooltip/drag AA
changes and drag-target state restoration. All production text parameter/AA
assignments now go through SetCompatibleTextRendering. It reads back the
applied pair and clears incompatible custom parameters, letting D2D choose
compatible defaults. No allocation or provider work is added to the draw path;
the guard reads the target state on its existing owning thread.

Frame EndDraw failures now retain their HRESULT and stage in presentation
receipts; previously these failures could arrive at the owner as S_OK.

The existing render-quality test gains real A8 and BGRA draws with the observed
grayscale override, plus the rendering-mode/AA compatibility matrix. Existing
pixel checks cover the ordinary aliased choice.
Temporary capture instrumentation has been removed. Build and regression
validation are recorded in `release-validation-1.0.2.md`. Confirmation on the
affected PC remains pending.
