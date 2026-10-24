# Rename editor subclassing, 0.9.227

The two `GWLP_WNDPROC` sites in 0.9.226 install `RenameEditProc` in
`BeginRename` and restore the saved EDIT procedure in `DisposeOverlayState`.
They belong to the frame-child file rename editor. The renderer is unaffected.

The editor now uses the six-argument `SUBCLASSPROC` callback with its `ViewState`
in reference data. `DefSubclassProc` preserves the remaining subclass chain and
the native EDIT implementation. On `WM_NCDESTROY`, remove this subclass, forward
the message, then finalize the session and release its font. If the editor
survives overlay disposal, remove the subclass before freeing the state and
replace its font with a stock font before deleting the session font.

`BeginRename` creates the EDIT on the overlay's UI thread. Overlay destruction
retires state through `RetireOverlayState`/`LeaveCallbackScope`, so disposal runs
after the outermost callback on that same thread. Unload requests disposal via
the overlay's owner-thread message and drains callbacks before returning.
Consequently the ordinary common-controls subclass APIs are appropriate here.
The defensive foreign-thread close branch keeps the editor/session alive and
posts `WM_CLOSE`; it does not transfer state ownership to the calling thread.

[Microsoft documents the owner-thread restriction](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass).
Windhawk's installed utility header was also inspected: its any-thread helpers
use a five-argument callback and their own wrapper, so their removal function
cannot be mixed with a subclass installed directly by `SetWindowSubclass`.

Private offscreen regressions exercise LTR/RTL native text editing, undo,
caret/selection, independent EDIT user data, Enter/Escape decisions and session
epochs, cancellation and external destruction. The DLL fixture checks the
native destruction chain while the session font is alive. A second fixture
deliberately retains an editor through overlay retirement, verifies callback
removal and stock-font substitution, then sends native text messages and
destroys it after verified DLL unmapping. These are component and unload tests;
they do not measure live Explorer behavior or displayed frame rate.

`build.bat -SourceFile 0.9.227.cpp` passed repository regressions, Windhawk host
DLL linking, the 8 KiB callback stack budget (maximum 1,960 bytes), all 33
option contracts, the complete C++/offscreen graphics/input suite and the
DLL-unload suite, including 21 API-hook cycles and the retained rename editor.
The published latest.cpp matches previous-versions/0.9.227.cpp byte for byte;
SHA-256 verification confirms that all 30 earlier archives are unchanged.
The complete successful local log is `tests/build-0.9.227-retry.log`.

Initial attempts exposed sandbox temporary-file/fixture access restrictions
and a test-only import of `GetWindowSubclass`, which the standalone host's
legacy comctl32 does not export by name. The latter check now verifies the
restored native class procedure. The successful full gate ran outside the
sandbox with the installed toolchain; no production code or gate was bypassed.
