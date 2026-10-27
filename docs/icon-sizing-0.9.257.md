# Icon source and cache handoff

The 0.9.256 change was withdrawn. Read-only probes of the affected items showed
that icon-only image-factory results contained a faint frame around small centered
artwork. Their alpha bounds covered the whole canvas, so transparent-border
cropping could not solve the reported sizing problem. The same items' native
image-list HICONs contained unframed artwork: Rufus 46x43 pixels and ZIP 44x34.

The worker now requests genuine thumbnails separately, then uses native HICONs
for ordinary icons. It reads the image list's actual canvas size before decoding
so a padded jumbo canvas is not reduced before its artwork is compacted. This
work and pixel ownership stay on the existing per-tab worker lanes. The renderer
magnifies the cached texture with GPU bilinear sampling; thumbnails retain their
own framing and decorations. The image factory remains a last fallback if native
icon extraction fails.

Missing list icons had a separate cache handoff mismatch: drawing accepted other
size tiers, while list uploads required an exact tier. Source-limited cache reuse
could therefore provide valid CPU pixels that were never uploaded. Both views
now use the same size-fallback lookup for uploads and drawing, including after
device recreation.

`--icon-sizing` covers native ICO extraction, a framed factory response, dynamic
image-list dimensions, cached list zoom, source ceilings, presentation handoff,
device recreation and GPU grid/list/RTL fitting. Manual read-only probes also ran
the affected Rufus executable and both missing ZIPs through real worker/cache/GPU
rendering. Those renders are offscreen component evidence, not live Explorer or
displayed frame-rate measurements.

The older alpha test's cross-provider equality check passed in isolation on both
0.9.255 and 0.9.257 but failed after the full suite's Shell requests. Its reference
now uses GDI bitmap bytes with independent integer premultiplication, retaining
the per-channel alpha and GPU comparisons without assuming provider artwork is
identical.
