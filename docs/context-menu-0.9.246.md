# Native item context menus (0.9.246)

The old item-menu path constructed `IContextMenu` from the folder, queried it
with `CMF_NORMAL`, tracked an HMENU owned by the GPU overlay, and interpreted
some chosen verbs itself. Large selections used a second menu implementation.
Neither path entered Explorer's own menu-opening handler. Item menus also
lacked the message bridge used by the background menu for dynamic/owner-drawn
extensions. This explains both missing native capabilities and a different
context for menu customizers. Rename specifically requires the caller to
support the Shell's rename contract; adding that flag alone would leave the
other integration differences.

The replacement verifies the active folder and selected identities, then sends
`WM_CONTEXTMENU` to that tab's native Shell view. Explorer constructs and hosts
the menu, chooses its flags, supplies site services, forwards submenu messages
and invokes the result. The mod does not add, remove or interpret item verbs.
Small filesystem selections retain their existing verified commit. This PC's
best-effort commit gets an additional identity check. Prepared large selections
retain their native selection without reparsing all filenames on the UI thread.

A private ExplorerBrowser investigation found that a mouse-coordinate request
fails with the zero-size suppressed native viewport. A selection/keyboard
request (`lParam == -1`) still works. This is the documented
[WM_CONTEXTMENU selection request](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu).
It leaves the native host hidden and the native viewport empty. A scoped,
thread-local anchor changes only the first popup's screen position to the GPU
click/keyboard anchor. The native menu, owner, flags and tracking return value
remain unchanged. Other HWNDs, threads and later extension popups pass through.
Both popup API callbacks participate in the existing DLL-unload drain.

Rename uses the existing native-editor handoff, shared with ribbon and
extension-created editors. There is no item-menu-specific rename entry or
canonical-verb dispatch. Background and column menu implementations are outside
this change.

`--context-menu` uses private temporary files and a real offscreen Shell view.
Popup display and focus changes are substituted in the test process. It compares
native and overlay top-level commands, submenu entries and flags for files/folders
and 1, 3 and 128 items,
with/without Shift, and checks native ownership, screen anchors, suppression and
stale-tab cancellation. Lazy submenu children can change asynchronously (the first
Send To submenu initially contained a placeholder), so they are not compared
before activation. The real native Rename command is entered and cancelled
on a private item through the shared editor, without changing its name. The
unload harness blocks each popup API during actual DLL removal as well as the
seven existing callback types.

These are component checks. They do not establish visual parity with a running
Explorer window, Windows 11's live menu presentation or the user's particular
menu-customizer configuration. Live validation should compare the same selected
items in GPU/native views, including nested extension menus and Rename, then
switch tabs and unload while a menu is open.
Validation: `build.bat -SourceFile 0.9.246.cpp` passed the full repository,
C++ and DLL-unload gate. The latter covers nine API callbacks and 27 DLL
unloads. Published source copies and the archive are byte-identical; retained
0.9.242–0.9.245 archives keep their prior hashes. An earlier full run hit an
intermittent metadata-retention assertion; isolated checks passed on both the
unchanged 0.9.245 source and the new source, and the final full gate passed
without metadata implementation changes. Failure diagnostics remain in that
test for future investigation.
