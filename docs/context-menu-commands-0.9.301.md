# Explorer menu command routing: 0.9.301

The native menu provider sends its selected View, Sort by and Group by commands
directly to `SHELLDLL_DefView`. A private ExplorerBrowser probe invoked Windows'
own `SVGIO_BACKGROUND` command provider and observed `WM_COMMAND` on that HWND,
with the native mode/sort changing successfully. The frame does not receive
those commands. The GPU's frame/dispatch observers therefore missed them.
Observing `WM_CONTEXTMENU` only covers opening the menu; an asynchronous
presenter can execute its choice after that call has returned.

There was a second independent gate: `RefreshSortFromExplorer` refused to read
sorting until a GPU header had first enabled `trackNativeSort`, even on the
explicit command path. Menus could change the native sort while GPU rows kept
their previous order.

The native view now has an owner-STA command subclass for its existing overlay
binding. It wraps actual native menu commands in the existing `NativeViewInput`
transaction. The existing nesting guard defers observation during reentrant
Shell pumps; the existing coalesced queue samples the committed mode, grouping
and sort after dispatch. Only an actual native command can accept an untracked
sort. Merely opening/dismissing a menu leaves the initial natural order intact.
This PC uses the existing native-row projection for accepted sort changes.

The binding is removed on view replacement and overlay disposal, and the
callback participates in the existing DLL callback-draining guard. Native
suppression and unsupported-layout fallback keep their existing lifecycle.

Refresh also needs to invalidate cached detail values even when names, sizes
and timestamps are unchanged. The existing folder automation subscription now
coalesces `EnumDone` with its selection notification and runs `ForceRefresh`
on the owner queue, validating the native HWND and folder epoch. Where no
`IWebBrowser2.Document` is available, it binds the native view's own background
automation object after first content. No command IDs or polling intervals are
used to recognize Refresh.

`--native-context-menu` retains its 36 real popup comparisons and adds actual
Shell-provider command invocations for private disk, ZIP and delegated folders.
It discovers command offsets by canonical verb and property labels through
`IColumnManager`, and invokes `IContextMenu::InvokeCommand`. Windows itself
delivers the command to DefView; the test sends no fabricated `WM_COMMAND`.
Coverage includes fresh sorting without a header click, keys/direction in
Details/List/grid, pinned mode changes, Large/Medium/Small native icon sizes,
queued choices, grouping and Refresh in all three layout families. The Refresh
checks require an actual native enumeration-complete event, a new GPU detail
refresh epoch and preservation of unchanged content/order. Expected modes/sizes
are read from the real native view. The DLL
suite additionally checks removal of the command subclass during disable.

The original 0.9.301 working source from Git is a negative control: the new
suite fails at `menu direction did not reach GPU sorting without a header click`.
The corrected command suite passes for all three private providers,
alongside the 36 displayed popup comparisons. Content/Tiles fallback and return
are also covered by the final suite. This repository's newer rename tests do
not compile against 0.9.300, so that version is not claimed as a runnable
negative control.

Final validation: `build.bat -SourceFile 0.9.301.cpp` passed repository checks,
the installed Windhawk DLL link, callback stack checks, the complete C++ suite
and real DLL-unload tests. The tested source was published as 0.9.301. The unload
test also removes the native ownership property before disable and requires the
view's original window procedure to be restored before DLL retirement.

These are private Shell/GPU component checks. They do not demonstrate a mouse
selection inside Explorer's modern presenter or a displayed frame rate.

Public API contracts:

- [IShellView::GetItemObject](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellview-getitemobject)
- [IContextMenu::InvokeCommand](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-invokecommand)
- [IFolderView2::SetSortColumns](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-setsortcolumns)
- [ShellFolderViewOC events, including EnumDone](https://learn.microsoft.com/en-us/windows/win32/shell/shellfolderviewoc-object)
