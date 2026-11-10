# Changelog

## Unreleased
- **ADDED** Support for Explorer's file extension visibility settings, including live updates.
- **ADDED** Batch renaming of multiple selected files and folders, with native numbering and undo support.
- **ADDED** Click-to-rename behavior, matching native Explorer.
- **ADDED** Automatic renaming when creating new files or folders.
- **ADDED** Tiles view in every folder, showing each item's name, type and size.
- **ADDED** Content view, showing each item's name, type, date modified and size. Switching to or from List and Details animates.
- **ADDED** Support for Explorer's Compact view. List and Details rows shrink with an animation, like Ctrl+wheel zoom, and keep their text size.
- **ADDED** Libraries, Applications and other item lists such as Printers now open in the GPU view.
- **ADDED** Search results highlight the parts of names that match your search, like native Explorer.

- **IMPROVED** Drag files directly from their icon or filename without selecting first. Unselected list targets use the full row height; their blank row and grid padding remain available for rectangle selection. Selected items drag from the whole row or cell.
- **IMPROVED** Zebra stripes animate between snapshots of the visible bands, respecting group boundaries and empty-row stripes.
- **IMPROVED** Thumbnail overlays now match Explorer's sizes: shortcut arrows and app badges no longer cover large thumbnails. Video frames get film strips in Explorer's proportions, drawn over the image edges with see-through holes.
- **IMPROVED** Item tooltips now look like the modern tooltips in Explorer's Gallery: subtly rounded, no border and a soft centered shadow. Their text follows the view's text size, between the native tooltip size and twice it.
- **IMPROVED** Default text size now matches the Windows 11 right-click menu. The defaults follow that menu's sizes rather than usability testing.
- **IMPROVED** Rename field appearance to match the list's font, scaling, and text rendering.
- **IMPROVED** Build validation reliability with additional retries and diagnostics.
- **IMPROVED** View switch button and tooltip now use Explorer's own status bar button and tooltip styles.
- **IMPROVED** Thumbnails in the drag pile have rounded corners, sized for the pile like grid thumbnails are for the grid.
- **IMPROVED** The log now records sort, navigation and GPU device details that help diagnose a stuck or blank view.
- **IMPROVED** Sorting by Size orders folders by their size when another mod provides folder sizes.
- **IMPROVED** Folder sizes update when files are added, removed or changed inside them.
- **IMPROVED** Refining a search updates the results in place, animating added and removed items instead of reloading.
- **IMPROVED** This PC in Large icons is now a plain icon grid; its Tiles view keeps the drive usage bars.
- **IMPROVED** File hover tooltips now match Explorer's own tooltips: native colors, font and size, sharper text, and even spacing.
- **IMPROVED** In Tiles view, icons span all three text lines and the lines are evenly spaced.

- **FIXED** Deleting a striped row in a grouped list could animate a stripe beneath an unrelated collapsed group.
- **FIXED** A hidden native list could keep an old window size after resizing, leaving a strip where right-click menus did not open and restoring a cropped native view. Native child layout updates are also retained during parent resize and visibility changes.
- **FIXED** Rename field flickering, incorrect caret positioning, and visual artifacts.
- **FIXED** Selection and interaction issues when finishing a rename.
- **FIXED** View switch tooltip flickering and moving while shown.
- **FIXED** The drag item count now sits at the pile's top corner, not over a small first item.
- **FIXED** The drag operation symbol (🚫, 📋, 🔗) is always smoothly antialiased, whatever the text antialiasing option.
- **FIXED** Spacing that did not scale with display scaling: row and item insets, the gap after names, group headings, highlight corners, and the minimum text and icon sizes.
- **FIXED** Opening a drive from This PC could leave the view stuck on This PC or blank, when Explorer had saved that drive without a sort column.
- **FIXED** Double-clicking while a folder was still loading could jump back to the parent folder.
- **FIXED** The view could stay blank after being hidden and shown again while a folder was loading.
- **FIXED** File sizes showed as 0 bytes on USB and other removable drives. Sizes that cannot be read now show "--".
- **FIXED** Library folders that would not open on double-click, sizes shown as raw byte counts, and sorting that redrew the whole list.
- **FIXED** Apps with small icons appeared boxed and tiny in grid views; their icons now fill the icon space.
- **FIXED** The view switch button's tooltip could appear clipped the first time it showed after Explorer started.

## 1.0.4 — Public release
- **FIXED** ARM64 compatibility.

## 1.0.3
- **FIXED** File and folder opening, selection handling, and navigation reliability.
- **FIXED** Touch drag-and-drop issues.

## 1.0.2
- **IMPROVED** New-tab loading and animation responsiveness.

- **FIXED** Text rendering inconsistencies across views.
- **FIXED** Renaming items inside collapsed groups.

## 1.0.1
- **IMPROVED** Type-to-select performance and Unicode support.
- **IMPROVED** Keyboard navigation and group selection behavior.

- **FIXED** Relative date calculations in tooltips.
- **FIXED** Thumbnail overlay scaling at large icon sizes.

## 1.0.0
- **ADDED** Localized relative dates to file tooltips.
- **ADDED** Keyboard navigation and expand/collapse controls for group headers.

- **IMPROVED** Type-to-select with flexible matching and Backspace support.
- **IMPROVED** Group animations, drag previews, and selection visuals.

- **FIXED** View toggle behavior when GPU rendering is unavailable.
- **FIXED** Compatibility with native Explorer tooltips and accessibility settings.

## 0.9.347
- **ADDED** Selection counters on collapsed groups, with click-to-reveal functionality.

## 0.9.346
- **IMPROVED** Responsiveness when sorting, switching views, refreshing, and resizing.

## 0.9.345
- **ADDED** Smooth icon rearrangement animations when resizing or zooming.

- **FIXED** Touchscreen pinch-zoom flickering and clipping.

## 0.9.344
- **ADDED** Double-tap on empty space to navigate up a folder.

- **IMPROVED** Visual sharpness at non-default display scaling.

## 0.9.343
- **IMPROVED** Icon overlay and thumbnail badge scaling.

- **FIXED** Scrollbar flickering during column resizing.

## 0.9.342
- **ADDED** Cloud sync, shortcut, and sharing overlays to icons and thumbnails.

## 0.9.341
- **IMPROVED** Drag-and-drop previews, animations, and touch compatibility.

- **FIXED** Select All occasionally clearing selections.
- **FIXED** Text rendering inconsistencies and rename-field alignment.

## 0.9.340
- **FIXED** Empty cloud and virtual folders incorrectly appearing empty.
- **FIXED** Missing system-marked items.

## 0.9.339
- **IMPROVED** Zoom smoothness and responsiveness in large folders.

## 0.9.338
- **ADDED** Detailed file tooltips with additional metadata.

- **IMPROVED** Tooltip localization, formatting, and appearance.

## 0.9.337
- **ADDED** Custom drag-and-drop previews with stacked icons, thumbnails, and operation indicators.

## 0.9.336
- **ADDED** File information tooltips with names, sizes, types, and dates.

## 0.9.335
- **IMPROVED** Memory usage in large folders.

## 0.9.334
- **IMPROVED** Folder switching performance through better text caching.

## 0.9.333
- **IMPROVED** Touch drag-and-drop and gesture reliability.
- **IMPROVED** Drag preview appearance and animations.

## 0.9.332
- **IMPROVED** Group header hover effects and animations.

## 0.9.330
- **FIXED** Selections being lost when collapsing or expanding groups.

## 0.9.329
- **FIXED** Flickering during window resizing and file renaming.

## 0.9.328
- **IMPROVED** Window resizing performance.

## 0.9.327
- **IMPROVED** GPU memory efficiency and scrolling performance.

## 0.9.326
- **IMPROVED** GPU efficiency during cut-and-copy animations.

## 0.9.325
- **IMPROVED** Scrolling and animation responsiveness.

## 0.9.324
- **IMPROVED** File size loading and large-folder responsiveness.

- **FIXED** Dragging with swapped mouse buttons.

## 0.9.323
- **FIXED** Text clipping while horizontally scrolling Details view.

## 0.9.322
- **FIXED** Filename highlight sizing after sorting, renaming, or changing fonts.

## 0.9.321
- **ADDED** Compatibility with Better file sizes in Explorer for folder size display.

## 0.9.320
- **FIXED** Rename-field and view-button flickering.

## 0.9.319
- **IMPROVED** Rename fields in List and Details views.

## 0.9.318
- **IMPROVED** Rename fields for long filenames and zoom changes.

## 0.9.317
- **FIXED** Rename-field alignment and selection backgrounds.

## 0.9.316
- **FIXED** Selection flickering when opening context menus.

## 0.9.315
- **FIXED** Selection handling in provider-managed folders.

## 0.9.314
- **IMPROVED** Checkbox hit targets for mouse and touch.

## 0.9.313
- **IMPROVED** New-tab loading and switching performance.

## 0.9.312
- **FIXED** Middle-clicking folders sometimes failing to open new tabs.

## 0.9.311
- **FIXED** Middle-clicking folders opening separate windows instead of tabs.

## 0.9.310
- **ADDED** Double-click empty space to navigate up.
- **ADDED** Middle-click folders to open new tabs.
- **ADDED** Two-finger tap to open context menus.

## 0.9.309
- **IMPROVED** Checkbox appearance and responsiveness.

## 0.9.308
- **FIXED** Checked boxes disappearing after hover or input changes.

## 0.9.307
- **ADDED** Modern selection checkboxes with touch support.
- **ADDED** Select All checkbox in the Name column header.

## 0.9.305
- **IMPROVED** Sorting behavior by removing unnecessary item animations.

## 0.9.304
- **IMPROVED** Sorting performance in large folders.

## 0.9.303
- **IMPROVED** Compatibility with Explorer's native sorting preferences.
- **IMPROVED** Large-folder sorting and keyboard selection behavior.

## 0.9.302
- **FIXED** Missing animations when changing folder grouping.

## 0.9.301
- **IMPROVED** Rename fields and selection marquee rendering.

- **FIXED** Native sorting, view switching, and refresh behavior.

## 0.9.300
- **FIXED** Back/Forward gesture indicators sometimes disappearing.

## 0.9.298
- **IMPROVED** Folder navigation transitions.

## 0.9.297
- **FIXED** Scroll position briefly jumping during Back navigation.

## 0.9.296
- **FIXED** Old gesture animations replaying after switching tabs.

## 0.9.295
- **IMPROVED** Navigation animation smoothness and rendering efficiency.

## 0.9.293
- **IMPROVED** Animation frame pacing.

## 0.9.292
- **FIXED** Swipe animations being interrupted during folder navigation.

## 0.9.291
- **IMPROVED** Touchscreen Back/Forward gesture animations.

## 0.9.290
- **FIXED** Selection highlights disappearing between context menus.

## 0.9.289
- **FIXED** Selection highlights disappearing while context menus are open.

## 0.9.288
- **IMPROVED** Compatibility with Explorer's native context menus.

## 0.9.287
- **FIXED** Context menus and Rename commands in native Explorer integration.

## 0.9.286
- **IMPROVED** Two-finger swipe and pinch gesture recognition.

## 0.9.285
- **IMPROVED** Pinch-to-zoom recognition with uneven finger movement.

## 0.9.284
- **FIXED** Scrollbar dragging and accidental touch context menus.

## 0.9.283
- **FIXED** Swipe indicators being obscured by scrollbars.

## 0.9.282
- **IMPROVED** Horizontal scrollbar appearance and layout.

## 0.9.281
- **FIXED** Last items being obscured by the horizontal scrollbar.

## 0.9.280
- **IMPROVED** Vertical scrollbar appearance.

## 0.9.279
- **FIXED** Storage bars overlapping truncated drive labels.

## 0.9.278
- **IMPROVED** Swipe navigation indicator appearance.

## 0.9.277
- **ADDED** Redesigned, animated Back/Forward swipe indicators.

- **IMPROVED** Gesture feedback and navigation behavior.

## 0.9.276
- **IMPROVED** Touch scrolling momentum and inertia.

## 0.9.275
- **IMPROVED** Swipe navigation responsiveness.

## 0.9.274
- **IMPROVED** Swipe indicator movement and gesture feedback.

## 0.9.273
- **FIXED** Blank content flashes during navigation, resizing, and refreshes.

## 0.9.272
- **IMPROVED** Automatic column fitting when entering Details view.

## 0.9.271
- **FIXED** Folders failing to open and selections being cleared in certain locations.

## 0.9.270
- **FIXED** View toggle button alignment during window movement and touch scrolling.

## 0.9.269
- **IMPROVED** Touch-friendly scrollbar animations.

- **FIXED** Temporary scrollbar flashes and initial touch scroll jumps.

## 0.9.268
- **FIXED** Touch selection and pinch-zoom jumping.

## 0.9.267
- **IMPROVED** Touch gesture detection and selection behavior.

## 0.9.266
- **ADDED** Support for Explorer's single-click activation preferences.

- **IMPROVED** Touch scrolling, selection, and swipe navigation.

## 0.9.265
- **ADDED** Native touchscreen gesture handling and hold-to-drag functionality.

- **IMPROVED** Touch scrollbars and pinch-to-zoom.

## 0.9.264
- **ADDED** Touchscreen scrolling, pinch-to-zoom, and gesture navigation.
- **ADDED** Touch-friendly scrollbar targets.

## 0.9.263
- **IMPROVED** Automatic refresh when returning to Explorer windows.

## 0.9.262
- **ADDED** Background refresh when returning to inactive tabs.

- **IMPROVED** Refresh behavior without unnecessary flickering.

## 0.9.261
- **IMPROVED** Animation efficiency during folder content changes.

## 0.9.260
- **IMPROVED** Large-folder responsiveness and text zoom performance.

## 0.9.259
- **IMPROVED** Rendering performance in large virtual folders.

## 0.9.258
- **IMPROVED** Text and icon caching during scrolling and zooming.

## 0.9.257
- **IMPROVED** Icon scaling and image quality.

- **FIXED** Missing icons when zooming or recreating the GPU view.

## 0.9.255
- **IMPROVED** Selection responsiveness and reduced unnecessary repaints.

## 0.9.254
- **IMPROVED** Settings synchronization and This PC drive updates.

## 0.9.253
- **IMPROVED** Theme and accent color updates without periodic checks.

## 0.9.252
- **FIXED** Incorrect text and control colors in certain themes.

## 0.9.251
- **FIXED** Invisible text and controls caused by invalid theme colors or font settings.

## 0.9.250
- **FIXED** Ctrl+V occasionally pasting multiple times.

## 0.9.249
- **FIXED** Ctrl+V not working in some folders or focus states.

## 0.9.248
- **IMPROVED** Support for ZIP archives and Shell-provider folders.

## 0.9.247
- **FIXED** Selection and context menus inside ZIP and provider folders.

## 0.9.246
- **FIXED** Incorrect contents appearing in delegated Shell folders.

## 0.9.245
- **FIXED** Scrollbar dragging becoming unresponsive during folder loading.

## 0.9.244
- **IMPROVED** Drive storage bar sizing in This PC.

## 0.9.243
- **IMPROVED** Drive storage bar alignment and column transitions.

## 0.9.242
- **IMPROVED** Refresh behavior by keeping existing file details visible while updates load.

## 0.9.240 — Public release
- **FIXED** Bright edges and jagged outlines on transparent icons and thumbnails.

## 0.9.239
- **IMPROVED** GPU rendering responsiveness and large-icon scaling performance.
