# Rename editor handoff (0.9.233)

The supplied trace contains two `EDIT_CLOSED` events, both with
`physically-present=0`. It does not prove that an HWND survived commit, nor does
it include a captured New-command handoff. The old label/GDI residue is a visual
symptom; code inspection identifies a missing presentation transaction:
`FinalizeDestroyedRenameEditor` invalidated the old row, while `FinishRename`
only changed its label after `SetNameOf` returned and did not submit that change
until a later directory delivery.

Commit now publishes the entered name as the real field is destroyed and submits
that frame before querying/calling Shell. A token/path-bound preview also covers
reentrant old directory packets while Shell is busy. The actual filesystem
identity remains unchanged until success. The returned Shell PIDL supplies the
canonical display name and parsing path; rejection restores the actual name and
reports the error. Navigation/unload or tab destruction invalidates completion.
This makes the visual handoff immediate; filesystem/provider completion still
takes whatever time Shell requires. No synchronous compositor wait is added.

The creation paths were inconsistent because only keyboard/context-menu intent
was recorded, and focus in the suppressed native EDIT was explicitly excluded
by list-input focus checks. Toolbar New can open that native EDIT directly.
Native EDIT create/show/focus events now queue an owner-thread handoff. The
existing timer and keyboard-dispatch paths use the same helper. It resolves the
native focused PIDL against the current GPU rows, copies text and selection,
cancels Shell's duplicate editing transaction, and calls the existing
`BeginRename`. Address/search controls and inactive tabs cannot qualify. If names
are still loading, the native text is retained and the existing names lane is
retried; the field transfers when the exact row is available.

The frame-sibling arrangement remains intentional: the EDIT sits above the
DirectComposition HWND. `RepositionRenameEdit` now checks visibility and sibling
order as well as coordinates, and sticky creation refocus repairs both before
returning keyboard input. Ordinary pointer focus loss commits the current rename.

`tests/rename-handoff-smoke.h` uses private temporary Shell folders and offscreen
windows, with substituted focus, mouse state and error dialogs. It covers the
three intent patterns (shortcut, context menu, toolbar), native editing before
names complete, text/caret retention, sibling-order/visibility repair, blank-area
click commit, inspection of the submitted new-name frame at Shell-call entry,
reentrant old names, successful real private-folder renaming and rejected rename
rollback, in LTR/RTL list/grid layouts. Existing native EDIT sizing, undo and
session-epoch checks remain in the focused `--rename` suite.

Validation: the focused `--rename` suite passes, and
`build.bat -SourceFile 0.9.233.cpp` passes repository/export checks, Windhawk DLL
linking, all 35 option contracts, the full C++ offscreen graphics/input suite,
and real DLL-unload/process-exit checks. The final full run also executes the
expanded handoff suite, including first-character routing. Both published
sources match the tested archive; all 36 earlier archives remain unchanged.
These component checks do not establish live Explorer frame timing or
appearance; the supplied live reproduction should be retried after installing
the release.
