# Frame-focused Paste (0.9.249)

The frame/GPU shortcut recovery recognized Cut, Copy and Delete but omitted
Ctrl+V. Clicking the destination list returned input to the native Shell view,
whose accelerator then performed Paste. Frame focus left the shortcut without
that native-list route.

The shared file-command path now recognizes Paste and invokes the active
view's background `paste` verb. It uses the same native-window, tab, focus,
generation and command-lifetime guards as the selected-item commands. Paste
does not require selected items, loaded names, or completion of a selection
import. It never targets a selected subfolder or waits in that selection's
command queue. Focused native lists, text editors, modal owners, inactive tabs
and provisional destinations retain their existing input ownership.

The Shell consumes the original clipboard data. The mod does not inspect or
convert clipboard formats, materialize archive files, change focus, or inject
keystrokes. Canonical verb invocation is supported by
[IContextMenu::InvokeCommand](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-invokecommand).

`--paste-focus` reproduces the original failure before the fix. It uses a real
private archive's virtual `IDataObject` and reads its deferred stream through a
private clipboard stand-in. Shell Copy/Paste invocations are intercepted, so
the user's clipboard and files are untouched. Coverage includes accelerator
and dispatch routing, frame/GPU focus, an empty/loading destination, a selected
folder, pending/unsafe selection imports, repeat suppression, editor/modal/tab
guards, stale menu resolution, and unchanged real foreground/focus. These are
offscreen integration checks rather than live Explorer paste operations.
