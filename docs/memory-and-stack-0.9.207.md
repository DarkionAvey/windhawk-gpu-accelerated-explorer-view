# Explorer crash and graphics memory investigation (0.9.207)

## Confirmed crash

The latest captured dump, `explorer.exe.11208.dmp` (23 October 2026,
11:29:47 local time), and its Application Error event identify exception
`0xc00000fd`, a stack overflow in the installed 0.9.206 mod. Its fault RVA
`0xded62` is `___chkstk_ms+0x16`. The saved return address is
`DispatchHook+0x16`; the actual DLL's prologue requests `0x10428` (66,600)
bytes. This is independent of a guess about video memory exhaustion.

`DispatchHook` contains a 32,768-character local path buffer for Alt+Up.
The compiler reserves that frame even when handling unrelated messages.
Offline unwinding through matching PE exception tables establishes six
existing DispatchHook frames inside native COM/RPC message dispatch, followed
by the attempted seventh allocation. Those frames alone retain about 400 KiB.
The older `explorer.exe.37576.dmp` event has the same exception and fault RVA.
Other earlier access violations have different signatures and are not explained
by this finding.

All eight large callback/helper path buffers now allocate only inside their
actual branch, on the heap. The compiler gate rejects critical message/COM
callbacks above 8 KiB. A separate actual-DLL regression calls the real dispatch
hook recursively 32 times on a thread with a 256 KiB stack reservation.

`tests/inspect-crash-dump.py` resolves captured addresses against matching
module files. Saved-stack address candidates are not unwind frames.
`tests/unwind-crash-dump.cpp` reads dump memory and the matching modules as
data and uses their PE exception tables. Its optional `--stack-probe` mode
validates the actual prologue CALL and probe pushes before restoring the
caller context. No injection or live-process execution is involved. Export-only
nearest symbols must not be mistaken for actual C++ function names.

## Reproduced resource growth

Private standalone targets use the production rendering paths at 3840 by 2160.
They do not request Shell providers or open user folders. DXGI numbers below
describe the test process, not Explorer or total card memory.

| Probe | Measured local GPU memory | Private process RAM |
| --- | ---: | ---: |
| Four empty targets, original cleanup | 167.64 MiB | 145.63 MiB |
| Two warmed targets with interrupted column scenes, original cleanup | 792.01 MiB | 489.39 MiB |
| Immediately after releasing those original targets | 408.04 MiB | 474.47 MiB |
| Same warmed-scene probe with explicit release/trim | 795.63 MiB | 491.76 MiB |
| Immediately after explicit release/trim | 9.93 MiB | 88.05 MiB |

These are individual observations, not exact hardware-independent limits.
In particular, immediate DXGI readings do not establish a permanent leak:
driver/compositor destruction can be deferred. The changed cleanup markedly
improves that observed release, while active scene storage remains substantial.

The code retained an entire independent graphics domain in each inactive tab.
Hiding stopped paints, so an interrupted scene could retain full-viewport D2D
bitmaps plus DirectComposition surfaces until the tab was visited again.
Glyph masks also charged only their small A8 pixel payload: 1,024 labels had
about 3.67 MiB of pixels, while DXGI rose in roughly 64 KiB increments per
small texture. Neither quantity is a reliable measurement of the other.

The fix retires inactive graphics on the owning STA after creation/painting
scopes finish. A lifetime-checked posted message rechecks current ownership
and visibility; a tab shown again cancels the release. Busy scopes schedule
their continuation on exit, without a guessed completion timer. Hidden paints
cannot recreate devices. Retirement stops animation wakeups while retaining
the existing tab-handoff ownership probe. A selected tab awaiting native layout
keeps its resources rather than repeatedly creating and dropping them.
Native suppression, names, model generations,
selection, CPU icon data and DirectWrite shaping survive. Activation/restore
rebuilds graphics immediately through the existing target path, before waiting
for a paint or animation to complete, without restarting enumeration.

Cleanup releases targets and composition references, clears D2D resources,
clears and flushes the tab's immediate D3D context, and calls
[IDXGIDevice3::Trim](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim).
It does not wait for GPU completion or move single-threaded D2D objects across
apartments. Teardown uses the same cleanup. Devices and queues remain tab-owned.

All four glyph caches together have a 64 MiB estimated allocation budget,
including textures pending paced destruction. Each standalone mask charges a
conservative 64 KiB-rounded estimate, rather than pixels alone. Immediate names
can still draw through DirectWrite when allocation is deferred. Optional folder
history is bounded to 8–64 MiB per tab, instead of up to 1 GiB per tab on a
large-memory PC. Icon accounting also includes a rounded texture estimate for
small icons, alongside their retained CPU pixels. D2D's internal cache target
is 16 MiB; this API does not impose
a hard limit on explicit surfaces or a complex frame.

The exact reported 12 GiB Explorer workload has not been reproduced. The crash
has a proven stack cause and the resource probe proves retention/amplification;
it does not prove that all of the user's 12 GiB belonged to these resources.
Live validation after loading this release remains necessary.

## Next capture

Enable **GPU memory profiling** (`gpuMemoryLogging`) in the mod settings and
capture Windhawk's log while opening tabs, switching tabs, zooming and closing
tabs. All logging remains disabled by default. Pipeline tracing is optional.
The memory log samples active rendering at most once per second, and logs
target creation and release immediately. Disabled profiling performs no DXGI
queries or cache walks.

Each `[GPU memory]` record identifies the overlay, tab, lifetime, visibility,
suspension and live mod-device count. All sizes are bytes:

- `processLocal`, `processShared`, `localBudget`, `sharedBudget`: measured
  **process-wide usage/budgets on that adapter**, using
  [QueryVideoMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo).
  They include other GPU users inside Explorer; do not sum them across tabs.
  HRESULT fields distinguish unavailable results from measured zero.
- `processPrivate`: process private CPU memory, with `cpuOk` indicating success.
- `estimateSwapchain`: this tab's double-buffer pixel storage.
- `estimateScene`: animation-plane bitmap plus composition-surface pixel storage.
- `estimateGlyphs`, `estimateRetiredGlyphs`: active and pending glyph allocation
  estimates. Counts include cached shapes and retired layouts.
- `estimateIconCpuAndGpu`, `iconRecords`: existing combined CPU/icon cache
  allocation accounting, not measured icon VRAM. Driver metadata, staging allocations,
  alignment and compositor overhead are outside pixel estimates.

Use process deltas alongside categories and release records. Categories are
estimates, not a complete sum that must equal DXGI. No per-frame cache sweep or
cross-tab graphics lock is added for profiling.

## Validation

Focused commands are `tests/run-regressions.ps1 -SourceFile 0.9.207.cpp
-TestArguments --gpu-memory` and `--gpu-lifecycle`. The latter exercises 20
private warm 4K tabs, interrupted scenes, a stale hide followed by show,
reentrant initialization, hidden paint rejection and cached shaping/selection
on resume. These are offscreen resource/lifetime tests, not displayed frame
rate or live Explorer acceptance tests.

The final complete release gate passed repository workflow checks, Windhawk
DLL linking, the standalone C++ suite and all 21 actual DLL-unload cycles.
Compiled critical callback frames peak at 1,944 bytes. The actual-DLL
32-level/256 KiB stack test passes. In the full-suite 20-tab lifecycle run,
local GPU memory peaks at 421.52 MiB and returns to 9.86 MiB with no retained
tab devices. Production minimized attachment/restore passes alongside the
private lifecycle test. Ten previous archives and the public README retain
their original hashes; published `latest.cpp` is byte-identical to the tested
0.9.207 archive. No failed gate published a source.

The read-only native viewport integration probe also stopped treating a fixed
two-second wait as a completed cold enumeration. It obtains a complete Shell
enumeration and checks that the native model reaches that count after its
navigation event. Selection acceptance after native view-mode changes is checked
through the actual selection API result. Test deadlines report failure; they
do not authorize the next step. Simulated paint fixtures provide valid active
tab ownership now that production ignores inactive paints.
