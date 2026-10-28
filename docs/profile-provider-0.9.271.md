# Profile folder binding regression (0.9.271)

The 0.9.240–0.9.252 comparison identifies the 0.9.246 provider-enumeration
change as the introduction. `BindDirectoryProvider` reparsed a path with
`SHCreateItemFromParsingName` instead of retaining the active native folder's
PIDL. 0.9.248 then routed provider folders through the selected native default
command: the incompatible selection IDs now also prevented directory opening.
0.9.252 was recovered read-only from Git commit `8f661a1`; retained historical
sources were not edited.

The supplied other-device trace repeatedly records `COMMIT wanted=1
verified=0 fallback=1 hr=0x00000000`, followed by selection recovery. Pictures
and Downloads never become selected in the native view. Native Select All
later reports 13 items, but none map to the GPU provider snapshot; recovery
clears the selection. This explains both blocked default commands and delayed
deselection. The trace alone does not identify the two providers.

A read-only probe of the affected local Explorer view establishes the cause:

| Binding of the same profile path | Provider class |
| --- | --- |
| Active native view | `{F3364BA0-65B9-11CE-A9BA-00AA004AE837}` (filesystem) |
| Reparsed path | `{59031A47-3F72-44A7-89C5-5595FE6B30EE}` (User Files) |
| Rebound native PIDL | Original filesystem class |

The path-bound provider returns wrapped or known-folder child PIDLs; native
Explorer has ordinary filesystem children. A child PIDL belongs to its parent
provider, even when another provider reports the same filesystem path. See
[Microsoft's IShellFolder reference](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellfolder).
The difference exists in a separate test process, independently of the mod
DLL loaded in Explorer. Stale DLLs are not required to produce this bug. A
restart changing how Explorer binds the profile would be consistent with the
reported temporary recovery; that restart effect was not reproduced here.

The fix captures the native PIDL once per confirmed binding and carries it
through names, sort-overflow verification, watches and selection preparation.
Providers are rebound on their existing tab-owned worker apartments. A failed
identity capture does not permit a different path-based binding. Speculative
navigation still owns the departing folder object, so it must not use that
object as the destination's identity. Cached snapshots retain their provider
identity and cannot restore command identities into a different native binding.
Confirmation replaces any path-only preview job, and a native view replacement
with a different root identity resets the binding even if its path is unchanged.
Filesystem fast enumeration and genuine delegated/virtual providers remain.

`--profile-identity-probe` is an explicit, read-only OS integration probe,
excluded from the default suite. It discovers the profile through
`SHGetKnownFolderPath`, reads an already-open native view and runs the actual
names worker. It never selects, navigates, invokes verbs or focuses that view.
On preserved 0.9.270 it fails with `native-filesystem=1 model-provider=1`;
0.9.271 passes with `native-filesystem=1 model-provider=0` and retains the native
root. This checks worker binding against live Explorer; it does not claim a
live double-click test of an installed replacement DLL.

The private provider-selection suite also checks that ordinary, delegated and
archive views retain the exact native folder PIDL before exercising selection,
focus, menus and asynchronous echoes. Existing provider-navigation and full
release suites cover confirmed/speculative transitions and DLL unload.

Validation: the final compiled 0.9.271 worker passed the read-only probe against
the still-affected native view. `build.bat -SourceFile 0.9.271.cpp` passed the
full C++ and DLL-unload suites and finalized the release. An earlier candidate
passed its suites but was held by the source-change publication guard while
confirmation/rebind handling was completed; the final gate was rerun in full.
