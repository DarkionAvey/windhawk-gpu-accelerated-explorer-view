# Build export and lifetime review, 0.9.228

The build publishes `gpu-accelerated-explorer-view.wh.cpp` from the same staged
source as `latest.cpp`, after the complete release gate. The numbered archive
remains the tested source. Repository regressions check all three hashes and
ensure failed tests, changed source and archive conflicts leave both exports
unchanged. The extra filename does not participate in numbered-version discovery.

The dispatch-state observation was accurate: 0.9.227 allocated a map even for
unrelated/null dispatches, retaining every thread's map until unload. Allocation
now occurs only when an Explorer frame needs scan/diagnostic state. Each map has
a duplicated synchronization handle to its actual thread, so recycled thread IDs
cannot confuse ownership. The existing lifetime regulator polls registered
threads at most once per second while idle, removes only signaled/exited threads
and destroys retired maps outside the registry lock and off UI threads. No new
provider lane, worker or TLS destructor is introduced. Unload explicitly closes
remaining handles after API callbacks drain, including threads that outlive the DLL.

Frame startup now checks the stop flag while holding the frame lock. Shutdown
sets that flag and snapshots the thread under the same lock, releases the lock
to join, then clears/closes its handles under the lock. Animation registration
also rechecks admission and signals the event inside the lock. The worker uses
the wake handle passed at creation, which stays alive until the join completes.
The tab observer likewise uses its creation parameter rather than reading the
global handle that stop detaches before joining it.

The overlay class is named from `WH_MOD_ID` and belongs to the module containing
its class-name storage, discovered without increasing the module reference count.
Creation and unregistration use that same instance. A registration conflict fails
initialization; no existing class procedure is accepted or rebound. This follows
[Windhawk's window-class lifetime guidance](https://github.com/ramensoftware/windhawk/wiki/Global-objects-and-process-shutdown#7-raii-wrappers-that-call-teardown-apis)
and the [module-handle API contract](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw).

Private component tests hold animation/presentation startup during shutdown,
reject a new presentation after stop, exercise an observer with its global event
absent plus 32 start/stop cycles, and churn 128 registered threads while another
thread preserves its live cache. Null dispatches from 64 other threads allocate
no scan state. Real DLL tests retain trivial TLS pointers across actual unmapping,
reject conflicting DLL-owned classes, preserve a same-named EXE class, and verify
initialization failure releases only registrations owned by the DLL. Existing
rename, provider, input, native-host and offscreen graphics checks remain enabled.
These are component/unload checks; they do not measure live Explorer frame rates.

The focused `--lifetime` run and `build.bat -SourceFile 0.9.228.cpp` pass. The
full gate covers repository/export guards, installed Windhawk DLL linking,
all 33 option contracts, the complete C++/offscreen graphics/input suite and
actual DLL unmapping, including 21 API-hook cycles with surviving callback
threads and their trivial TLS pointers. Callback stacks remain below 8 KiB
(maximum 1,960 bytes). SHA-256 checks confirm that both published source files
match previous-versions/0.9.228.cpp, and all 31 prior archives are unchanged.
Local logs are `tests/lifetime-0.9.228-retry.log` and `tests/build-0.9.228.log`.
