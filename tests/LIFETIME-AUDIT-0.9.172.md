# Explorer overlay lifetime audit — 0.9.172

The supplied toggle/recompile failure is an unload-lifetime defect. Windows
Application events report execute access violations in the **unloaded** overlay
DLL. Read-only inspection of the matching dumps and the installed DLL's COFF
symbols resolves both captured instruction pointers to `DispatchHook`:

| Dump | Exception | Unloaded image offset | Symbol |
| --- | --- | --- | --- |
| explorer.exe.23116.dmp | 0xc0000005, execute | 0xbb18 | DispatchHook + 0xfc8 |
| explorer.exe.19288.dmp | 0xc0000005, execute | 0xb6ac | DispatchHook + 0xb5c |

These identify execution in unmapped hook code. They are not a complete native
stack unwind and do not identify which Shell operation kept native dispatch
busy. `inspect-crash-dump.py` reads dump records and matching PE/COFF tables;
its raw stack-address candidates are explicitly labeled as **not unwound**.
Neither Explorer nor the dump files were modified to obtain this evidence.

0.9.171's existing cleanup waited for overlay/subclass callbacks and workers,
but not API hooks suspended inside the original Windows/Shell function. Removing
overlays therefore did not establish that the DLL was no longer on a thread's
return path. Sorting while the overlay was disabled is one way to expose this
timing; the fix applies to all six hooked APIs and all supported folders.

| Area | Hazard found | 0.9.172 change |
| --- | --- | --- |
| API detours | Dispatch/accelerator/visibility/position hooks could return from native code after unload. | One shared guard implementation tracks each API through its original call and all local cleanup. Final draining happens in `Wh_ModUninit`, after detour removal. Before-uninit stops new overlay work without waiting for API calls while detours still admit traffic. |
| Thread-local state | A nontrivial `thread_local unordered_map` registered destruction on Explorer-owned threads, which can outlive the mod. | Trivial TLS pointers refer to module-owned scan state. Tables retire after hook draining. Remaining TLS declarations are scalars or pointers, with no registered C++ destructor. |
| COM sources | Unadvise/removing a local reference does not prove that an external source has released its last callback object. | All five callback classes track object lifetime. All 34 COM entry methods participate in callback tracking. The DLL remains loaded until outstanding references/calls finish. |
| Reentrant windows | `WM_NCDESTROY` freed state and its resources while an outer hook, WndProc, or COM callback could still be using them. | The window detaches immediately and native suppression is restored. An intrusive, owner-thread retirement list keeps state/resources alive until the outermost mod callback returns. The lifetime guard also encloses profiling destructors. |
| UI thread isolation | Process-wide foreground/WinEvents can name a window owned by a different UI thread. Reading its mutable `ViewState` was unsafe. | The canonical `State(HWND)` resolver rejects foreign-thread access. Cross-thread coordination continues to use HWNDs/messages or the existing atomic diagnostics. |
| Initialization/class ownership | Partial initialization retained a class whose WndProc belonged to the DLL; accepting `ERROR_CLASS_ALREADY_EXISTS` could reuse a stale procedure. | Failure unwinds class/event/factory resources. Registration verifies/replaces an unused stale class and rejects a conflicting live class. Unload waits for owned class release. |
| Sibling controls | A rename EDIT can survive its overlay HWND. Deferred cleanup must still call its genuine procedure through NCDESTROY. A failed close must not retain a DLL WndProc. | The editor carries its retained session owner directly. Cleanup restores the native procedure/font and removes callbacks if physical close fails. The view-toggle subclass is explicitly removed before destroying its control. |
| Native input registrations | RevokeDragDrop needs the live target HWND; touchpad event admission must stop before that window disappears. | Drop-target revocation and manipulation-handler removal occur during NCDESTROY. COM/apartment/resource release remains deferred until outer callbacks return. |
| Discovery during unload | Cleanup limited to recognized Explorer frames could miss an owned overlay after reparenting. | Cleanup enumerates matching owned overlays under all process top-level windows. Normal discovery/settings retain their supported-Explorer scope. |

The audit also checked the dedicated worker inventory, private pool callbacks,
queue generations and immutable PIDLs, background reclamation, compositor thread,
cold-attach/visibility observers, posted message payloads, native suppression
properties, rename/toolbar subclasses and graphics-resource ownership. Existing
worker joins already wait for **actual** threadpool callback return, not only a
counter reaching zero, so that mechanism was retained. Provider work and queues
remain per tab. The additions do not create a worker, timer or cross-tab work
queue. The shared counters/retirement barrier serve module lifetime only.

Validation:

- The new isolated unload check fails on unchanged 0.9.171 with: `unload completed
  while a native hook retained a DLL return address`. The negative test releases
  its thread before unloading the historical DLL, avoiding an intentional crash.
- On 0.9.172, all six APIs are held inside a host-owned fake native function
  while before-uninit and uninit execute. Uninit waits; releasing native execution
  lets it complete. Three rounds exercise 18 real LoadLibrary/FreeLibrary cycles.
  VirtualQuery verifies that the image is unmapped **before** the callback thread
  exits, covering delayed TLS destruction as well as the return path.
- A retained real callback object prevents unloading until its final Release.
- A real nested DestroyWindow detaches the window but preserves its state and
  resources until the outer callback exits. A sibling native EDIT still receives
  its genuine NCDESTROY. A foreign-thread state lookup is rejected.
- WH_EDITING deliberately rejects hook installation. The resulting failed init
  repairs a stale class and releases its class, frame event and DirectWrite factory.
- The full runner includes these checks alongside the existing graphics, native
  suppression, navigation, selection, scrolling, worker, cache and live-sort suite.

Final 0.9.172 linked against the installed Windhawk host and passed the complete
runner, including the isolated unload executable and all pipeline regressions.

The isolated tests do not reproduce the user's full Explorer session or establish
that every third-party Shell extension is safe. The exact live toggle/recompile
sequence remains a separate user confirmation. This audit targets callback,
ownership and shutdown hazards; it is not a proof that arbitrary native memory
corruption or resource exhaustion cannot crash Explorer.

Normal performance flags are not required for lifetime stalls: `[GPU lifetime]
QUIESCE`, `DRAIN`, `INIT_FAILED`, `CLASS_CONFLICT` and `CLASS_RELEASE_WAIT` identify
the teardown stage. DRAIN reports the outstanding API, window callback and COM
object counts, at most once per second while waiting. Performance logging also
reports completed draining, retired states and scan-state cleanup. No per-frame
lifetime logging is added.

Preserved 0.9.171 SHA256:
`294A95F467C33202864EEEE3FAFB101C9AD501CA765F1510D8BDDEDE36CFB75C`.
