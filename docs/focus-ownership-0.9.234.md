# Explorer focus and selection ownership (0.9.234)

The report concerns the shared input handoff, rather than the Ctrl+Shift+N
binding or a specific New-menu file type. No creation-command-specific restore
timer or hardcoded selection row is added.

## Ownership before the change

Explorer's UI apartment has one HWND keyboard-focus owner. The active GPU tab
nevertheless maintains several distinct identities:

| State | Normal owner | Purpose |
| --- | --- | --- |
| Foreground frame/active tab | Explorer | Grants permission to handle list input or return focus. |
| HWND focus | Hidden native Shell view, GPU fallback, or an explicit control | Routes keyboard messages and lets native Shell/preview extensions recognize the active view. |
| Native selected/focused PIDLs | `IFolderView2` | Supplies native commands, selection counts and Shell's caret. |
| GPU selection and `selected`/`anchor` | Active tab | Draws selection and determines arrow-key origins. |
| Rename editor | Frame-child EDIT owned by the active tab | Owns text/caret input until its session ends. |
| Search/address/ribbon/modal control | The explicit control | Retains its own input; list focus must not override it. |

The GPU HWND normally forwards focus to the native Shell HWND for native
integration; native/frame keyboard messages reach the active GPU tab through
the existing shared accelerator/dispatch resolver. This design requires the
native focused identity and the GPU caret to agree when an editor gives input
back to the list. A successful `SetFocus` alone does not establish that agreement.

## Root causes and reproduction

The keyboard/ribbon route can first open Shell's inline editor. Adoption cancels
that hidden editor, which can emit empty or intermediate native-selection
callbacks. The old adoption code then selected the GPU row without reconciling
the Shell selected/focused PIDL. Ordinary selection messages were gated while
editing, but direct `ApplyImportedNativeSelection` calls (including names/worker
completion) and direct captures were not. An older result could erase selection
even while the visible editor still owned input.

The new offscreen test run against unchanged **0.9.233** fails at
`stale empty native import erased editor-owned selection`. The fixture changes
no real desktop focus. It also models native cancellation callbacks and checks
the transition back to list input; this establishes a code-level ownership bug,
not a measurement of a live Explorer focus sequence.

`FinishRename` additionally returned HWND focus to Shell before reconciling its
new PIDL. Its immediate path mutation left the worker-built path hash index at
the old name until refresh, so a selected new identity could temporarily appear
absent. These are shared transition/index invariants, independent of the New
command's origin.

## Shared correction

- `InputFocusOwner` classifies the active frame, native/GPU lists, native/owned
  editors and other controls under the same active-tab/foreground checks.
  `ReturnListInputFocus` implements one guarded native-focus return with GPU
  fallback, including recursion protection for the GPU HWND's focus forwarding.
- The existing native/frame keyboard resolver uses the same input-owner guard.
  A queued native key cannot bypass a focused editor or take input from the
  address/search control; list arrows retain their ordinary routing afterward.
- Starting an editor invalidates older selection preparation/import/recovery and
  establishes its selected identity through the existing verified native-selection
  path. Native-editor cancellation and the complete rename operation use the
  existing native-command lifetime scope to defer reentrant selection imports.
  The editor rechecks its owner generation after Shell calls; navigation during
  a callback cannot open a textbox for the departed input context.
- Both direct capture and import respect editor ownership. Finishing an editor
  reconciles the selected/focused PIDL before returning HWND focus, including
  unchanged names, Escape, rename failure and successful canonicalized names.
  Other controls, other tabs, modal dialogs and background frames retain input.
- Local path mutations add sparse hash-to-row changes rather than rebuilding or
  shifting the complete directory index on the UI thread. The changes follow the
  cached snapshot, are replaced by the next worker index, and use the existing
  tab-owned retirement lane for cleanup.

## Checks

`TestEditorSelectionOwnership` covers files and folders created through the
context-menu intent, shortcut intent and native-editor-only toolbar routes in
LTR/RTL list/grid layouts. It uses a selected row away from the top, native
cancellation echoes, a stale direct import while editing, real private Shell
rename, a reordered names publication, a subsequent native selection read, and
Up/Down messages routed from the native focus HWND through the shared resolver.
Additional checks protect address/search input, disabled/modal and background
frames, queued native keys during editing/search, and a native host that rejects
focus. Existing rename sizing, text/caret
handoff, immediate label submission and rollback regressions remain enabled.
The new reentrant-parse check changes the owner generation during selection
reconciliation and verifies that no obsolete editor opens.

Standalone graphics fixtures now declare their actual non-tabbed frame as the
active-tab owner. The capacity-refresh fixture uses scoped simulated modifiers
so nested message pumps cannot replace its Alt/F5 state. The mixed-selection
metadata check chooses a selected regular file rather than assuming the first
selected item is writable as a file. All existing pixel/selection assertions
remain enabled.

Validation: `build.bat -SourceFile 0.9.234.cpp` passes repository/export checks,
Windhawk DLL linking, all 35 option contracts, the complete C++/offscreen
graphics/input suite and real DLL-unload/process-exit checks. The final full run
includes the expanded ownership, queued-input and reentrant-generation checks
and native-selection metadata validation with 543 mixed filesystem items.
Focused rename and selection suites pass. Both published sources match the
tested archive; all 37 earlier archives remain unchanged.

These private offscreen checks do not establish live Explorer appearance; the
reported keyboard/ribbon/context-menu reproduction should be retried after
installation.
