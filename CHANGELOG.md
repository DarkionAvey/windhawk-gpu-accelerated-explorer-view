# Changelog

## 0.9.245

- Fix rapid scrollbar track clicks followed by thumb dragging, including
  presses that Windows reports as double-clicks.
- Keep scrollbar presses and captured releases responsive while directory
  names are loading.

## 0.9.244

- Size the shared storage-bar label area from the visible labels instead of
  the window width. Bars fill the remaining Name-column space while keeping
  their alignment, fixed gaps and smooth column transitions.

## 0.9.243

- Keep storage bars aligned across This PC Details rows, using shared label
  and track space regardless of drive-label length.
- Preserve the current column bounds for width-dependent contents during
  navigation and view transitions, avoiding a size jump when animation settles.

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

