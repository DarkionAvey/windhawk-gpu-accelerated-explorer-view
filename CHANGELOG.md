 # Changelog

## Since 1.0.5 — Unreleased
- **ADDED** Dragging files near any edge of the view scrolls toward offscreen destinations, with speed controlled by the pointer's distance into the edge.
- **ADDED** Internal animation duration scale for debugging, set to 1.0 by default.
- **ADDED** Double-click `test-mod.bat` to test a chosen mod source quietly, run tests individually and collect failure logs without publishing a release.
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
- **IMPROVED** Focus and animation checks use the actual input state and a held animation clock.
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

- **FIXED** Selection highlights morph from their displayed shape when switching views, including filename-only grid selections and full-row list selections.
- **FIXED** The first item and group heading stay visible when a grouped folder opens in a new Explorer window.
- **FIXED** Cold folder previews begin loading names when the destination is known.
- **FIXED** Filenames move from their displayed text anchors during view changes instead of sliding in from the center of the list. Interrupted transitions retain the current alignment.
- **FIXED** Recycle Bin items show their filenames instead of repeating the original full path, while retaining their Shell identities.
- **FIXED** Gallery keeps its native image view instead of receiving the GPU overlay.
- **FIXED** File hover tooltip shadows now fall below the tooltip with a tighter blur instead of forming a glow around it.
- **FIXED** File hover tooltips now use the list's actual font and weight, including custom fonts, while keeping the existing minimum and maximum text sizes.
- **FIXED** Arrow keys now start from the hovered item in every view when there is no selection or saved keyboard position. Left and Right move between items across groups, and collapse or expand a group only when its heading has keyboard focus. Space toggles the focused heading.
- **FIXED** Rapid repeated clicks on a column header now apply every sort toggle, including Windows' double-click sequence.
- **FIXED** Date sorting stays consistent when Shell column reads fall back to item properties. Changes to cached item dates and sizes are checked using refreshed Shell identities.
- **FIXED** Icons leaving Tiles for Content or Details could animate from the wrong position. View changes and interrupted layout animations now retain the outgoing icon and filename positions.
- **FIXED** Cloud folder Status columns showed hexadecimal data instead of sync icons. Status icons now follow Windows' property definitions and provider custom states, with text kept when icons cannot be loaded.
- **FIXED** Switching from Details to an icon grid could send icons sideways before settling. Icon and filename motion now uses destination cell bounds, and folder updates retain an interrupted view transition's geometry.
- **FIXED** View changes, selection and folder events could stop being observed after navigation. Event subscriptions now follow the current native view and folder generation, including when returning to This PC.
- **FIXED** Collapsing a sticky group now scrolls to that group's own heading, keeping the collapsed group visible instead of skipping ahead.
- **FIXED** Touchscreen pinch zoom in Content and Tiles could resize icons in visible steps. Taking over a wheel or trackpad zoom with touch now uses the current zoom scale instead of estimating it from the row or icon size.
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

- **REMOVED** The option to disable Explorer's saved folder sorting; the mod now always follows it.

