# Successive native menu focus handoff

Live mouse testing of 0.9.289 reproduced the user's consecutive-item failure.
Clearing selection first avoided it. Read-only WinEvent and process-state
observations showed the missing transition: the second `WM_CONTEXTMENU`
returned with frame focus, then Explorer transferred focus to its WinUI input
host and showed the replacement popup. Binding feedback to focus immediately
on return captured the frame and rejected the later, legitimate handoff.

0.9.290 leaves feedback pending until the native invocation has a visible
owned popup and delegated focus. Existing focus and popup events complete the
binding; no delay or timeout is used. A subsequent input interaction cancels an
uncompleted request, and tab/folder/foreground changes invalidate it. Once
bound, normal focus ownership still governs dismissal. Native menu invocation,
logical selection, keyboard ownership and menu contents are unchanged.

The GPU component regression now includes frame focus after return, delegated
focus before popup visibility, and completion when a real USER popup is shown,
in Details/List/Icon and LTR/RTL. These checks substitute focus; they do not
establish live WinUI correctness. An isolated negative control restoring
0.9.289's binding function fails at asynchronous popup completion.

Live Explorer verification on 2026-10-29 used the loaded 0.9.290 candidate and
real mouse input. Five alternating file/folder right-clicks, with the preceding
menu still open and no intervening selection clearing, retained the background
on the selected row. Reopening on another item in a two-item selection retained
both backgrounds. Show more options, file-menu dismissal, address-bar focus
with autocomplete, and a folder-background menu were checked separately.
Consecutive menus on two This PC drives also retained the selected drive's
background. No file or drive operation and no clipboard operation was invoked.

`build.bat -SourceFile 0.9.290.cpp` passed the full release gate, including the
graphics checks, 36 native popup comparisons and 21 actual DLL unloads. The
published source copies and archive are byte-identical; retained archives are
unchanged. Details are in `tests/build-0.9.290.log`.
