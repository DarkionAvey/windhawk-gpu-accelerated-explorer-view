# Replacement lifetime audit (0.9.273)

The supplied trace has 714 overlay size messages and 746 successful presentation
records, with no nonzero Present result and no text-miss/wait record. Text remains
cached, and the sampled native-content state stays suppressed.
The status-button messages containing `source=fallback` concern button placement,
not a fallback to the native file list.

## Confirmed problems and repairs

| Path | Premature disposal | Replacement boundary |
| --- | --- | --- |
| Window resize | ResizeBuffers reallocates the chain still attached to DWM before the new frame is drawn | Build separate buffers on the tab's GPU lane; attach them after nonblocking Present succeeds, in one composition transaction |
| Cold navigation | An active packet with no names can clear the previous image | Acknowledge loading as control information and keep the accepted model/image until preview names or a confirmed empty directory arrive |
| Shell icon invalidation | Item/global notifications erase all usable artwork | Invalidate request freshness while retaining the cache as fallback; cancellation and unique request IDs still reject departed results |
| CPU/GPU icon handoff | CPU delivery removes the old bitmap before the paced upload, including failed/empty results | Keep the bitmap and its source geometry until a replacement texture exists; account for both generations during handoff |
| File/association property refresh | Invalidation clears visible cells while providers are still running | Keep displayed values, invalidate demand freshness separately, and merge only successfully read properties (including authoritative empty values) |
| Font/DPI/style changes | Callers reset formats before the transactional factory can succeed | Keep the old complete set and retain a dirty flag until a complete replacement succeeds; preserve monitor rendering parameters on failed queries |

Resize allocation failures keep the accepted image and retry on the existing
presentation pulse. A genuine removed device still follows native recovery.
Retained chains, textures and CPU pixels remain tab-owned and are released by
the existing teardown/cleanup paths; memory estimates include overlapping images.
The handoff uses DirectComposition's documented
[atomic commit](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components)
behavior. The old displayed chain is retained rather than passing its storage to
[ResizeBuffers](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).

## Other paths checked

- Directory revalidation already rejects failed/partial replacement snapshots
  for a loaded view and preserves surviving visible properties on its worker.
- Native namespace/This PC membership results already reject incomplete and
  failed revalidation before replacing committed rows.
- Column capture builds a complete vector and checks its owning view/generation
  before swapping it. Reorder/drag rejection restores the previous layout.
- Text layouts and raster variants prepare their replacement before retiring
  cache entries; unsuccessful raster preparation retains the direct-text path.
- Composition scenes keep their committed root until ordinary replacement pixels
  are accepted. Standby recycling preserves the accepted chain rather than an
  unaccepted resize target.
- Hidden/out-of-scope views, native-mode changes, actual device loss and DLL
  unload intentionally restore native ownership or release their graphics;
  retaining visible GPU content across those ownership boundaries is unsuitable.

## Verification

The new resize regression fails against the original implementation with
`resize destroyed the displayed buffers before replacement presentation`.
It uses SetWindowPos and USER's default message delivery, with LTR/RTL resize
bursts, a return to the original dimensions and presentation backpressure.
The existing metadata pixel comparison also uses native resize notifications.

Additional checks cover failed font creation, real private-folder cold/empty
navigation, native private-ICO worker refresh and texture handoff, cancellation
and cache retirement. Property formatting distinguishes a missing/failed store
from a successful empty property. These are offscreen component/API checks;
they do not measure displayed Explorer frames or prove live visual smoothness.

`build.bat -SourceFile 0.9.273.cpp` passed the repository checks, real Windhawk
DLL link, complete C++ regressions and actual DLL-unload suite. The successful
gate archived 0.9.273 and finalized the two published source copies. Test-only
loading/icon expectations now distinguish invalidation from disposal, queue
checks observe the exact provider notification, and fixture teardown drains the
new presentation scheduler. Native-input tests required the unrestricted Windows
test environment; the sandbox did not expose the private calibration digitizer.
