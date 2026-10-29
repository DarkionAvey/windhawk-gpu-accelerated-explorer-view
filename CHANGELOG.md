# Changelog

## 0.9.284

- Keep scrollbar track presses captured so the same held press can drag the
  thumb after scrolling to the pointer, on both axes.
- Limit touch scrollbar menus to a stationary hold starting on the thumb,
  using Windows gesture recognition to cancel menus during scrolling.

## 0.9.283

- Draw Back/Forward swipe bubbles and glyphs above both scrollbars.

## 0.9.282

- Match the horizontal thumb's end padding and rounded caps to its track.
- Make scrollbar clearance scroll with the end of the list, letting items draw
  behind the horizontal bar until the final rows scroll above its touch target.

## 0.9.281

- Reserve bottom space above the horizontal scrollbar's full touch hit area,
  keeping the last item reachable while the bar is idle or enlarged.

## 0.9.280

- Pad the vertical scrollbar thumb's top and bottom to match its side inset,
  aligning its rounded caps with the track without changing scrolling behavior.

## 0.9.279

- Hide inline storage bars in This PC when there is not enough room for full
  drive labels and a bar, preventing bars from returning as labels ellipsize.

## 0.9.278

- Join the navigation chevron into one continuous stroke, removing the gap and
  uneven opacity where its segments meet during stretching and scaling.

## 0.9.277

- Replace the outlined swipe arrows with an accent-colored bubble and flexible
  chevron using Android's spring/stretch behavior, with distinct commit and
  cancel motion. Cache translucent contrasting glyph colors on accent changes.
- Cap navigation distance at one third of the tab width and retain column-first
  scrolling. Honor the touch recognizer's horizontal intent without a second
  direction test, and let the commit animation finish through folder changes.
- Restore cached visible properties before trying the worker queue lock,
  preventing temporary blank cells when metadata work is busy.

## 0.9.276

- Drive touch flings from Windows' measured release velocity and system
  deceleration, independently of drag distance and animation frame cadence.
- Add remaining momentum to successive flings in the same direction. A new
  touch catches the list immediately; reversal or an axis change starts fresh.

## 0.9.275

- Let swipe arrows track horizontal drag 1:1 through the navigation threshold,
  then keep traveling with increasing logarithmic resistance and no distance cap.
- Use constant linear resistance for vertical arrow movement.
- Wake a touch scrollbar only when the gesture's original DOWN starts on it.
  Multi-finger gestures and their remaining contact cannot acquire a scrollbar.

## 0.9.274

- Give Back/Forward swipe arrows a 1:1 start and progressive logarithmic
  resistance, stopping their travel at the navigation threshold.
- Let arrows follow vertical finger movement within half a circle height
  above or below center, without affecting an established horizontal swipe.

## 0.9.273

- Keep the accepted list image visible through resizing and cold navigation
  until its replacement is ready; confirmed empty folders still update normally.
- Retain icons, thumbnails and property text during Shell refreshes and failed
  provider reads. Replace GPU artwork only after its new texture uploads.
- Preserve usable fonts and rendering resources when replacement allocation
  fails, and retry through the existing presentation path.

## 0.9.272

- Automatically refit columns when switching from grid or List view to Details,
  using the existing readiness and clipping checks.

## 0.9.271

- Fix folders failing to open and selections being cleared in user profile
  directories when Windows exposes different Shell providers for the same path.
- Preserve the active Explorer folder's identity through background refresh,
  change watching and cached returns.

## 0.9.270

- Keep the view-toggle button aligned with native status controls during touch
  overscroll and ordinary window moves, without repeatedly falling back to an
  estimated position while native control measurements reload.

## 0.9.269

- Keep idle scrollbars small, retaining the full touch hit area. Enlarge only
  the bar a finger engages, animate back on release/leave, and preserve thumb
  capture outside the track. Prevent a first-touch scroll jump at high DPI.
- Defer new horizontal scrollbars until column discovery, fitting and layout
  settle, avoiding temporary overflow flashes. Keep vertical scrolling immediate
  and reveal horizontal scrolling when actively needed.

## 0.9.268

- Give touch marquee selection the same list-focus handoff as mouse input,
  so its outline appears without first clicking an item. Draw a visible line
  for perfectly vertical or horizontal marquee drags.
- Start pinch scaling from the contacts' span when recognition commits,
  preventing the initial zoom jump. Subsequent scaling follows the measured
  span without inheriting old scale from replaced contacts.

## 0.9.267

- Distinguish two-finger scrolling from pinch using both contacts' movement
  relative to their initial center and radius. Allow staggered fingers,
  unequal swipe paths and spacing variation without switching into zoom.
- Commit touch gestures to scrolling or zooming. Pinch keeps its initial
  anchor; contact changes cannot reopen a committed scroll as pinch.
- Use one-finger vertical movement for marquee selection when the whole
  directory fits vertically; retain scrolling and overscroll in longer views.

## 0.9.266

- Use vertical touch movement for scrolling with any finger count, one-finger
  horizontal movement for marquee selection, and two-finger horizontal movement
  for column scrolling.
- Start Back/Forward previews only after available column scrolling is consumed.
  Reverse through the preview before returning to columns, commit on release,
  and clear canceled previews from the rendered view.
- Pair taps using Windows' configured double-click speed and item identity,
  including varied contact positions and delayed dispatch.
- Keep touch ownership between contacts so a resting mouse cannot steal
  hover or selection. Resume mouse/pen input on deliberate movement or clicks.
- Resolve pinch and pan intent separately, keeping spacing variation from
  turning two-finger scrolling into zoom.
- Preserve hold-to-drag when movement arrives before a delayed hold timer.
- Honor Explorer's single-click activation, point-to-select and both title
  underline preferences without adding mod settings.

## 0.9.265

- Replace the blocked touch gesture path with direct pointer input and Windows'
  native recognizer for scrolling, inertia, pinch, taps and hold menus.
- Add hold-then-move Shell file dragging, preserving multi-selection and
  canceling deferred drags when the finger lifts or the tab changes.
- Support touch scrollbar dragging and divider double-tap fitting; preserve
  animated touch sizing, boundary feedback and optional Back/Forward swipes.

## 0.9.264

- Handle touchscreen swipes as scrolling, with Windows gesture recognition,
  inertia and boundary feedback. Preserve tap selection, double-tap activation
  and press-and-hold Shell menus.
- Follow pinch distance directly, keeping the touched item anchored through
  list/grid resizing. Support the existing optional Back/Forward swipes on touch.
- Animate larger touch scrollbar targets without reflowing file layouts;
  restore mouse sizing on mouse or touchpad input.

## 0.9.263

- Also refresh the active tab when returning to an Explorer window, including
  side-by-side windows. Coalesce window activation with tab resume, and skip
  control-only focus changes and departed windows.

## 0.9.262

- Refresh a background tab when returning to it, using the same asynchronous
  refresh as F5 and the context menu after showing its cached contents.
- Keep unchanged names, values, selection and viewport in place; avoid content
  repaint for identical refresh results, including This PC devices and capacity.

## 0.9.261

- Reduce compositor work during streamed file insertions, deletions and grid
  reflow by sharing identical motion across layers and items.
- Avoid animation objects for stationary properties and opacity effects for
  fully opaque items. Preserve animation timing, layer order and image quality.

## 0.9.260

- Speed up file feedback in dense views by comparing long shared identity
  prefixes in bounded vector blocks, preserving native case comparison.
- Reuse native text layouts during font-size-only zoom to reduce layout
  allocation and preparation while retaining full-resolution text and trimming.

## 0.9.259

- Reduce frame preparation and drawing overhead for long Shell-provider
  identities, including dense Recycle Bin grids, without raising cache budgets.
- Avoid repeated identity copies and comparisons during icon lookup and
  snapshot capture. Preserve image quality and case-insensitive file feedback.

## 0.9.258

- Reuse grid label shapes and cached text while scrolling at fractional row
  sizes, avoiding extra glyph surfaces caused by screen-position rounding.
- Reduce temporary allocation work when finding cached icons at another zoom
  size, including long Shell-provider identities. Keep existing cache budgets.

## 0.9.257

- Replace the withdrawn 0.9.256 sizing change. Load unframed Shell icons and
  fit their artwork to grid/list slots, using cached GPU scaling when zooming.
- Fix missing list icons when reusing a cached image at another zoom size,
  including after the GPU target is recreated. Preserve thumbnail framing.

## 0.9.255

- Resolve small native selections on the existing per-tab selection worker,
  with identity checks before applying results or continuing file commands.
- Avoid an extra initial selection-count query and duplicate successful
  post-command view-mode reads. Keep empty selection immediate and unchanged
  idle selections free of extra repaint.

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

