# Changelog

## 0.9.254

- Replace periodic hidden/protected-item settings checks with coalesced settings
  and native-view notifications, including sent Explorer frame broadcasts.
- Keep periodic This PC capacity updates on the background volume lane. Device
  identities use drive/media and native reorder events; retain discovery
  recovery only when the native reorder subscription is unavailable.

## 0.9.253

- Refresh colors at attachment and on relevant mod settings, theme, system
  color, dark-mode and accent events. Remove periodic color checks while browsing.
- Coalesce related notifications per tab and update tabs even when the native
  view toggle button is disabled. Font-only and unrelated settings skip color checks.

## 0.9.252

- Fix 0.9.251 changing correct text and control colors when theme background
  resources disagree with Explorer's visible background.
- Preserve valid native foreground colors and retain them through temporary
  lookup failures. Apply fallbacks only when native colors are unavailable.

## 0.9.251

- Keep text and matching controls visible when theme colors are invalid or
  indistinguishable from the resolved background. Preserve usable theme colors
  and Windows high-contrast colors.
- Fall back safely for missing fonts, malformed font metrics and invalid text
  sizes; keep existing text formats if replacement creation fails.
- Restore a system accent when accent lookup fails, and validate DPI, scrolling
  and display timing values.

## 0.9.250

- Prevent repeated Paste commands when Ctrl+V is handled with Explorer frame
  focus. Repeated accelerator checks and dispatch share one command per keypress.
- Apply the same input ownership to recovered Cut, Copy and Delete shortcuts.

## 0.9.249

- Restore Ctrl+V when the Explorer frame or GPU list owns focus, including
  empty destinations and files copied from archives or other Shell providers.
- Paste into the active folder without requiring an item selection or changing focus.

## 0.9.248

- Use the native default action for Shell-provider items, including nested
  archives and folders inside containers.
- Keep the GPU view available in provider subfolders without filesystem paths,
  including after switching to native view and back.
- Preserve provider folder identities through refresh, columns and change watching.

## 0.9.247

- Keep selection and focus stable in ZIP and other Shell-provider folders
  when Explorer reports the selected items back to the GPU view.
- Restore item context menus by matching provider identities instead of
  assuming every item has a filesystem path.

## 0.9.246

- Show the Shell provider's contents for folders that delegate their view,
  instead of listing the physical wrapper directory's files.
- Preserve native item identities and watch the delegated target for changes.

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

