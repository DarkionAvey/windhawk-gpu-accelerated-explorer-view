# Adaptive icon and thumbnail resolution (0.9.237)

The old 512px maximum existed in requests, WIC decoding, fallback lookup and
presentation-packet construction. Changing only the request size would still
downsample larger output and lose it during rendering handoff.

## Demand and boundaries

The arbitrary 512px ceiling is removed. `IconRequestSize` chooses from a generated
sequence of whole/half powers of two: 32, 48, 64, 96, 128, 192, 256, 384, 512,
768, 1024, 1536, 2048 and onward. Quantization reduces extraction churn during
continuous zoom; it does not end at a chosen desktop icon size.

The input is the physical size of the drawn icon, including the destination of
an active zoom. A 5K screen does not cause every small icon to request a 5K image.
Only imagery that actually occupies more physical pixels needs more detail.
Requests are bounded by:

- The physical viewport: zooming a mostly offscreen giant cell does not extract
  an enormous bitmap just because the logical cell has grown.
- A per-tab memory allowance, sampled from available physical RAM on an existing
  provider lane at most once per two seconds. The allowance is one sixty-fourth
  of available RAM, with a 4 MiB minimum and a 64 MiB initial fallback. Ordinary
  cache retention uses the existing 8–64 MiB history budget; larger viewports can
  justify more working memory, up to their pixel area times 12 bytes and the
  current RAM allowance.
- A share for the visible cell count plus two replacement/scratch slots, using
  eight bytes per square pixel for decoded/GPU imagery. Pending requests count
  against the cache budget; the number of wanted items cannot inflate it.
- The rendering device's reported bitmap limit, returned to the owner through
  presentation receipts. D3D11's platform texture bound is the initial fallback.
  These hardware/allocation boundaries remain necessary even without a fixed
  mod-specific resolution ceiling.

Nearby offscreen cells request at most 256px previews. Their visible request can
upgrade when scrolling brings them onscreen. Shared Shell image-list association
icons also stop at 256px: requesting a larger bitmap there would just enlarge
the same artwork. Per-item image factories, thumbnails and customized icons
use the adaptive path.

## Work, ownership and memory

All extraction, WIC scaling and RAM sampling remain on the tab's existing image
lanes. No new worker pool, global rendering lock, UI-thread provider query or
blocking wait is added. WIC receives the actual requested dimension and an
explicit byte budget, scales down without upscaling, and checks allocation
failure. Oversized thumbnail adornments are omitted while preserving the useful
image if they would exceed the texture/byte bounds.

A queued request can become too expensive before a provider starts. Worker
preflight checks the refreshed allowance and reports a retryable budget change
without extracting the stale large bitmap. Result delivery updates the owner's
allowance, so it requests a smaller tier. This does not count as a broken image
handler and does not prevent quality recovering when memory becomes available.

GPU uploads retain the existing per-frame byte/time pacing. One large upload can
make progress in a frame; subsequent uploads respect the batch budget. Normal
Explorer uses its independent per-tab presentation lane, so the transfer does
not run on Explorer's input apartment. Decoded pixels are shared immutably across
the handoff rather than copied again. Renderer cache retirement remains paced.

Bounded pending-request tokens cancel obsolete queued and in-flight work when
scroll/zoom demand changes. A provider already executing is allowed to return;
its obsolete bitmap is discarded on the worker before further decode/publication.
No provider thread is terminated. Existing folder generations and request IDs
still reject old completions after navigation, invalidation and cache reuse.

## Reuse and source quality

Fallback lookup knows all generated tiers without scanning the entire cache.
It prefers the nearest adequate actual pixel source and otherwise the most
detailed smaller one. A large request label cannot displace a sharper bitmap
when the provider returned fewer pixels than requested.
A nearby adequate cached image avoids another extraction; an excessively large
cached image can give way to a cheaper request. A cached bitmap incompatible
with a replacement device cannot suppress a request that the device can accept.

Shared image identities are resolved only once per packet, even when hundreds
of rows use the same file-type icon. Presentation packets carry the desired
image and one available alternate per
identity, rather than every historical size. The displayed GPU fallback remains
usable while a replacement is still CPU-only or waiting for its upload.

A single undersized result is not treated as a permanent source ceiling: it may
be a cold provider/cache response. Only a larger successful request that returns
the same detail as another successful tier marks that artwork as limited. A
decrease can be transient and does not establish a permanent ceiling.
Further enlargement can then reuse the available pixels. Productive upgrades
remain allowed, and normal image/association invalidation clears those records.
Item invalidation visits the same generated tiers as lookup, so file changes
also discard larger cached artwork and its learned source limit.
Windows/providers cannot create detail absent from the source image.

Relevant API contracts:
[`IShellItemImageFactory::GetImage`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage),
[`SHGetImageList`](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetimagelist),
[`GetMaximumBitmapSize`](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1rendertarget-getmaximumbitmapsize).

## Checks

The new `tests/adaptive-image-smoke.h` checks physical DPI/5K viewport models,
small icons on large displays, generated requests through 6144px, cheap previews,
memory/device limits, viewport clipping, destination prefetch and dynamic-tier
fallback. WIC checks decode a 2048×1024 source above 512px and enforce explicit
pixel-byte budgets.

A private substituted Shell image factory records the actual request size and
returns real HBITMAP pixels. Its checks cover large WIC/GPU transfers, sparse
presentation handoff, cache reuse, replacement-device demand, a blocked obsolete
provider, reservations, memory pressure after queueing and subsequent recovery,
confirmed limited artwork and providers that produce better output after a cold
undersized response. All windows are offscreen; no real clipboard, desktop focus,
user folder or file command is changed.

The 6144px case is a demand calculation, not a 6144px live Explorer rendering
measurement. The real transfer checks are separate from displayed frame rates.
The first full gate passed the adaptive-image checks but caught an existing
plain-click selection assertion. The same retained binary passes its isolated
selection suite. That fixture now uses the existing scoped neutral modifier
substitution through nested Shell pumps, preserving all original assertions.
A subsequent gate exposed a tie between equally detailed cached images that
discarded the newer entry's confirmed source ceiling. Lookup now preserves
that metadata, and the focused adaptive suite passes. Item-update coverage also
checks that invalidation clears the new high-resolution tiers.
The final `build.bat -SourceFile 0.9.237.cpp` gate passed, including repository
regressions, the linked Windhawk DLL, full C++/graphics/input checks and DLL
unload/process-exit coverage. The retained log is
`tests/build-0.9.237-release.log`. Published `latest.cpp`,
`gpu-accelerated-explorer-view.wh.cpp` and `previous-versions/0.9.237.cpp` have
identical SHA-256 hashes; the 40 earlier archived sources retain their original
hashes. No live Explorer or physical 5K display validation is claimed.
