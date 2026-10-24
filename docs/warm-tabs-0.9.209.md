# Warm tab presentation, 0.9.209

## Confirmed regression

The 0.9.207 memory fix posted `kMsgRetireGraphics` when a tab became inactive.
Its handler called `DropTarget`, destroying the swap chain, D2D/D3D/DComp
domain, glyph masks and uploaded icons. CPU directory data, selection, icon
pixels and shaped layouts survived, but an existing tab had no rendered image
to display. `PresentCachedTabSurface` recreated graphics and showed the window
before its first replacement paint. This explains both the blank/blink and
the repeated visible text/icon preparation reported after switching tabs.

## Change

Supported hidden GPU tabs enter warm standby. They retain the accepted tab
image, swap chain, graphics device, visible glyph masks and visible icon
bitmaps. Activation shows that image through the existing HWND presentation
path before requesting an updated frame. It does not enumerate the folder,
create a graphics domain or regenerate text before showing the tab.

An interrupted column scene first finishes through the ordinary cached
renderer. Only an accepted ordinary presentation permits retirement of the
temporary animation planes; backpressure continues on the existing frame
wake. Other obsolete glyph/icon textures retire on the owner STA with the
existing measured cleanup budget. CPU shapes and icon pixels stay cached.
A graphics paint serial independent of opacity-animation resets identifies
the last accepted frame's textures.

Merely releasing the scene objects is insufficient to keep warm tabs small.
The private allocation probe measured about 344 MiB for one hidden 4K tab
after logical scene retirement and driver trim. Releasing just its
DirectComposition domain reduced that to about 74 MiB without discarding
the swap chain, D2D device or cached text/icons. DComp keeps an animation
surface pool beyond the released scene. Standby now replaces that small
composition domain after settling a scene and reconnects the retained swap
chain while hidden. Ordinary hide/show cycles without an intervening scene
do not recreate it. Allocation failure before replacement preserves the
old working target; a failed target replacement attempts to restore it.

Scene destruction is committed after releasing its client resources.
Unused D2D resources and deferred driver state are then flushed/trimmed;
explicit retained textures and swap-chain contents remain intact. Production
does not wait for GPU/composition completion on Explorer's UI thread, and
uses no delay to assume that another operation has finished. Tabs still own
independent devices, workers and caches. Full teardown remains for native
fallback, closure/unload and genuine target/device failure.

API contracts: [DComp transactions](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components),
[ClearResources](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1device-clearresources),
[Trim](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim).

## Validation

`tests/run-regressions.ps1 -SourceFile 0.9.209.cpp -TestArguments --gpu-lifecycle`
uses 20 private offscreen 4K tabs, real glyph/icon uploads, cache pressure,
interrupted column scenes, stale hides, nested setup scopes and twelve
returns to a retained tab. It checks device, swap-chain, format and visible
texture identities; visible glyph/icon pixels; the ready frame before a
return paint; obsolete texture retirement; idle frame-wake removal; and full
close cleanup. It enforces measured warm/closed GPU memory limits as well as
logical resource accounting. Test-only composition completion fences make
measurement timing explicit; production never performs those waits.

The production attachment fixture also hides and resumes a supported tab
three times with new composition-device creation deliberately failing during
activation. It verifies the image can be shown without repaint or device/font
recreation and with native content still suppressed.
Pool-allocation failure preserves the old ready target and a later hide
retries cleanup. A synchronous activation during replacement-device creation
must preserve the resumed target instead of detaching it.

The focused resource run before the final pixel assertions measured 1,894.90
MiB peak and 1,473.66 MiB at standby for twenty warm devices, compared with
6,876.16 MiB when only logical resource release/driver trim was used. The
initial warm prototype without trim reached 9,911.70 MiB. These are this
private process's DXGI local-memory measurements, not total card memory or
predictions for every adapter. The fixture has one visible model row per tab
plus synthetic offscreen glyph pressure, not twenty populated Recycle Bins.
The retained images trade some steady memory for immediate presentation.
These checks do not establish displayed frame rates or live Explorer input
latency. Final release-gate results are recorded in the changelog.

The final full release gate passed repository workflow checks, Windhawk DLL
linking, the complete C++ suite and all 21 actual DLL-unload cycles. Its
20-tab run measured 1,904.76 MiB peak, 1,483.64 MiB warm standby and 9.86 MiB
after close. Three production activation calls peaked at 0.4619 ms CPU time
in the private offscreen attachment fixture. Critical compiled callback stack
frames peak at 1,944 bytes. All twelve previous archives and the public README
retain their original hashes, and `latest.cpp` is identical to the tested
`previous-versions/0.9.209.cpp`. The failed intermediate gate did not publish.
