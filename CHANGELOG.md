 # Changelog

## 1.1.11
- **ADDED** Theme XML can set Details header height, text size and color, and style vertical dividers and the bottom rule independently. Headers and their text can follow the list as you zoom.

- **IMPROVED** Details column headers keep their text size and trim vertical padding as Compact view animates.

## 1.1.9
- **ADDED** Date grouping now shows relative time just like native explorer behavior.

## 1.1.8
- **IMPROVED** The default scrollbar thumbs have distinct idle, hover, and pressed shades in light and dark mode. Pressing or dragging a thumb shows the native Explorer gray.

## 1.1.7
- **IMPROVED** Selection checkboxes match the size and placement of native Explorer checkboxes. They sit beside filenames in Details and Small icons and in the upper corner in larger icon views.

## 1.1.6
- **ADDED** Theme XML can set thumbnail corner radius in pixels or as a percentage of item height. It also controls shadow opacity and the border's width, color, and pattern.
- **ADDED** Percentage corner radii for scrollbars, storage bars, the drag-selection rectangle, file hover tooltips, selection checkboxes, and group selection badges.

## 1.1.5
- **FIXED** Hovering over items in single-click mode no longer changes the selection.

## 1.1.4
- **FIXED** The selection color follows the real Windows accent color, including an Automatic accent picked from the wallpaper. It no longer turns nearly black or uses an outdated title-bar color.

## 1.1.3
- **FIXED** Details, List and Small Icons now show each icon's own pixel-perfect small artwork instead of a shrunken large icon, at every display scaling. The current icon stays on screen until the sharper one is ready.

## 1.1.2
- **IMPROVED** Icon captions follow the Windows text size during zoom. Crossing an icon-size preset updates Explorer’s folder view.

## 1.1.1
- **ADDED** Theme XML setting with shared properties and optional light and dark overrides for each element. Works on a single line; see THEMING.md for instructions.
- **ADDED** Selection and hover backgrounds support solid colors, linear gradients with multiple color stops, and DPI-scaled borders aligned to whole pixels.
- **ADDED** File hover tooltips support themed backgrounds, borders, rounded corners and cached CSS-style shadows. Muted metadata text follows the custom background.
- **ADDED** Both scrollbars share themeable thumbs and tracks, with backgrounds, borders, rounded corners and hover and pressed styles.
- **ADDED** Drag-selection rectangles support themed backgrounds, borders and rounded corners, with light and dark overrides.
- **ADDED** Group selection badges support themed backgrounds, borders and rounded corners, with matching checkmark and count colors calculated from the background.
- **ADDED** Swipe navigation indicators support themed backgrounds and borders, with automatic chevron contrast and their existing animated shape.
- **ADDED** Selection checkboxes support themed backgrounds, borders and rounded corners for unchecked, checked, hovered and pressed states. Checkmarks adapt to the background.
- **ADDED** Theme borders support solid, dashed and dotted patterns, or no border.
- **ADDED** Storage bars support themed fills and tracks, usage-based colors and gradients, and full-track gradients revealed by used space.
- **ADDED** Storage fill and track heights can be themed independently within the item's text-size limit and available space. Both stay vertically centered, and zero hides just that part.
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
- **ADDED** Libraries, search results, Applications and Printers now open in the GPU view.
- **ADDED** Search results highlight the parts of names that match your search, like native Explorer.

- **IMPROVED** Drag files directly from their icon or filename without selecting first. Unselected list targets use the full row height; their blank row and grid padding remain available for rectangle selection. Selected items drag from the whole row or cell.
- **IMPROVED** Themed selection and hover gradients use cached dithering for smoother color transitions without an extra drawing pass.
- **IMPROVED** File tooltip text uses system ClearType on opaque backgrounds and grayscale smoothing on translucent backgrounds, respecting the selected text antialiasing mode.
- **IMPROVED** Details column dividers use a subtler transparent theme color to match the native pane divider more closely.
- **IMPROVED** Theme loading keeps readable properties when part of the XML is malformed and merges repeated definitions using the last complete value.
- **IMPROVED** Focus and animation checks use the actual input state and a held animation clock.
- **IMPROVED** Zebra stripes animate between snapshots of the visible bands, respecting group boundaries and empty-row stripes.
- **IMPROVED** Thumbnail overlays now match Explorer's sizes: shortcut arrows and app badges no longer cover large thumbnails. Video frames get film strips in Explorer's proportions, drawn over the image edges with see-through holes.
- **IMPROVED** Item tooltips now look like the modern tooltips in Explorer's Gallery: subtly rounded, no border and a soft centered shadow. Their text follows the view's text size, between the native tooltip size and twice it.
- **IMPROVED** Default text size now matches the Windows 11 right-click menu. The defaults follow that menu's sizes rather than usability testing.
- **IMPROVED** Rename field appearance to match the list's font, scaling, and text rendering.
- **IMPROVED** Build validation reliability with additional retries and diagnostics.
- **IMPROVED** View switch button and tooltip now use Explorer's own status bar button and tooltip styles.
- **IMPROVED** Thumbnails in the drag pile have rounded corners, sized for the pile like grid thumbnails are for the grid.
- **IMPROVED** When logging is enabled, the log records sort, navigation and GPU device details that help diagnose a stuck or blank view.
- **IMPROVED** Sorting by Size orders folders by their size when another mod provides folder sizes.
- **IMPROVED** Folder sizes update when files are added, removed or changed inside them.
- **IMPROVED** Refining a search updates the results in place, animating added and removed items instead of reloading.
- **IMPROVED** This PC in Large icons is now a plain icon grid; its Tiles view keeps the drive usage bars.
- **IMPROVED** File hover tooltips now match Explorer's own tooltips: native colors, font and size, sharper text, and even spacing.
- **IMPROVED** In Tiles view, icons span all three text lines and the lines are evenly spaced.
- **IMPROVED** Cloud sync icon updates (such as OneDrive) no longer reload every file's details, only the status icons that show them.
- **IMPROVED** Activity inside subfolders (builds, installs, downloads) refreshes folder sizes only when sizes are shown or used for sorting.
- **IMPROVED** Leaving a folder stops pending folder-size calculations sooner.
- **IMPROVED** Drawing unthemed checkboxes, group badges and swipe indicators no longer rebuilds theme resources for every item.
- **IMPROVED** Alternating row stripes, search-result highlights and Content view lines no longer allocate memory for every item on every frame while scrolling.
- **IMPROVED** Regression tests run as many tests at once as the computer's processor cores allow, instead of a fixed number.

- **FIXED** Item names show the single-click underline when pressed with touch.
- **FIXED** Sorting by Name in This PC orders drives by drive letter.
- **FIXED** Newly visible file icons stay ahead of offscreen requests after scrolling, including when a busy provider briefly defers queue updates.
- **FIXED** Three-digit theme colors such as `#39f` now expand to the correct color.
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
- **FIXED** OneDrive folder Status columns showed hexadecimal data instead of sync icons. Status icons now follow Windows' property definitions and provider custom states, with text kept when icons cannot be loaded (PR by @dariy-vel).
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
- **FIXED** Content view in several Explorer windows at once could crash Explorer. The "Date modified" and "Size" labels are now read once at startup.
- **FIXED** Folders such as Control Panel, Network and devices could open in the GPU view, and opening some folders could pause Explorer while it checked them. Only libraries, search results, Applications and Printers are added.
- **FIXED** Folder names, paths and extra Shell queries were written to the log even with logging turned off.
- **FIXED** Refining a search could carry an open rename, a pending click-to-rename or a drag over to different items in the new results.
- **FIXED** Typing a name with an input method editor (for example Chinese, Japanese or Korean) hid the composition while renaming.
- **FIXED** Automatic renaming of new items could misbehave when Explorer created them from another thread.
- **FIXED** Changing display scaling, theme or colors while renaming could briefly show old pixels where the rename field had been.
- **FIXED** Renaming several selected items could be abandoned when Explorer's editor appeared before receiving keyboard focus.
- **FIXED** With single-click and "underline when I point", a touched item now stays underlined after lifting the finger, like native Explorer.
- **FIXED** The message for a failed rename of several items could crash Explorer on a Windows version whose text differs from the expected one. Unexpected text is now skipped and only logged.
- **FIXED** Turning off or updating the mod while Explorer showed a New-item permission prompt, or while renaming, could crash Explorer.
- **FIXED** A failed multiple-item rename showed an English-only message that could freeze or break the view while open. It now uses Windows' own localized "Cannot rename" message, shown after the rename finishes.

- **REMOVED** The option to disable Explorer's saved folder sorting; the mod now always follows it.
- **REMOVED** The accent-colored background behind a dragged Details column header.

