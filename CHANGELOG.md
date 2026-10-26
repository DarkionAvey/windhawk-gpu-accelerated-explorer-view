# Changelog

## 0.9.242

- Keep existing metadata visible during F5, Ctrl+R and right-click Refresh
  while checking for changes in the background. Identical property results
  no longer request a repaint; changed values replace their previous text
  directly, including properties that become empty.

## 0.9.240

- Correct Shell image-factory bitmap transparency before GPU upload, removing
  bright edge fringes and jagged-looking outlines on icons and transparent
  thumbnails.
- Preserve the existing icon sizes, zoom, filtering and large-tile drawing
  optimizations; alpha conversion remains on the image worker.

## 0.9.239

- Skip ordinary tile drawing while a tab's swap chain is busy, retaining the
  latest input/model without blocking Explorer or another tab's rendering.
- Use cheaper sampling for large magnified icons while preserving native-size
  sampling, high-quality downscaling, image resolution and pinch/zoom behavior.
- Keep deferred frame acknowledgments, transition handoff, resize and GPU handle
  cleanup consistent with the new readiness check.

