# Hold drag handoff (working candidate)

The hardware Windhawk log `123603.215 6716 explorer.exe [WH].txt` establishes
the failure without a separate debugger. Five held contacts (61–65) all reach
native HOLD, held manipulation and a prepared Shell selection. USER promotion
returns TRUE, but `GetKeyState(VK_LBUTTON)` still reports `0x0000` or `0x0001`:
neither has its down bit set. The first OLE `QueryContinueDrag` receives zero
button flags while the mod still records the finger down. Our ordinary source
returns `DRAGDROP_S_DROP`. OLE returns `0x00040100` with no effect after 15–31 ms.
The preview disappears because the source requests a drop immediately, rather
than because the hold failed or Explorer canceled an established drag.

`ConvertPrimaryPointerToMouseDrag` requests system mouse delivery; its success
does not mean the owner thread has consumed the resulting button-down message.
The previous posted continuation entered OLE immediately and the overlay
discarded promoted mouse messages unconditionally. Windows documents both the
queue-dependent button state and entry into OLE from system mouse messages:

- [Pointer promotion](https://learn.microsoft.com/en-us/windows/win32/winmsg/winuser/nf-winuser-convertprimarypointertomousedrag)
- [GetKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeystate)
- [OLE input requirements](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-dodragdrop)

The continuation now requests promotion only. The actual touch-sourced
`WM_LBUTTONDOWN` or `WM_MOUSEMOVE` carrying `MK_LBUTTON` enters the existing
shared Shell drag source before the ordinary touch-mouse suppression. Selection,
item identity, epoch and ownership are checked again. Pending promotion cannot
post another conversion. Release, cancellation, Escape, another contact or
navigation invalidates a handoff that has not entered OLE. Lifetime/generation
checks protect both the conversion and the reentrant modal return. Native OLE
still owns drag movement, release/drop, modifiers and all target file operations.
There are no injected messages, button-state overrides, polling or delays.

0.9.333 added a raw displacement check using `SM_CXDRAG`/`SM_CYDRAG` before
recognition. That ineffective workaround is removed; the log confirms the native
HOLD-to-manipulation path already reaches promotion. That path and conversion
were present in 0.9.332.

The component fixture passes calibrated packets to real Ninput but substitutes
promotion and OLE. It cannot test the OS queue transition without hardware.
Its revised expectations require one promotion request and no OLE entry before
real native mouse delivery; contact release cancels the waiting handoff. They
are not evidence of an end-to-end hardware fix.

## Hardware check using Windhawk only

Use the updated `0.9.341.cpp` working source in Windhawk. Try holding and dragging
an item. If it still exits early, enable **Enable logging**, reproduce once and
save the Windhawk log. The source logs native hold/promotion, the real promoted
mouse message and button state, OLE entry and release decisions. Bounded owner
stacks include native module paths, build timestamps, sizes and offsets; the OLE
release decision captures the native OLE caller stack directly. No WinDbg,
PowerShell preparer or separate capture application is required. Symbol downloads
and resolution do not run in the Explorer input handler.

The user confirmed the touch handoff, follow-up visuals, and This PC repair on
hardware, and authorized serial regressions with `-Jobs 1`. Publication remains
a separate step.

## Drag preview follow-up

The caption's bottom edge is anchored above the rotated pile: additional lines
expand its top edge upward, and fewer lines let it shrink back down. Caption
space is reserved above the pile from source creation, using the same
native-derived font and three measured lines with ellipsis for longer target
descriptions. The canvas and monitor-edge clamp remain the same with no caption,
a single line or wrapped text. This prevents both owner-window positioning and
asynchronous surface replacement from shifting the pile. Captions and item
counts retain the mod's text antialiasing policy and cached native theme colors.

Only initial entry may animate scale. Operation, caption, theme and image updates
do not restart a scale curve. Each icon has an alpha-derived Direct2D Gaussian
shadow with padding for its full blur radius and stack rotation. The mask uses
the actual fitted artwork, excluding the thumbnail atlas decoration band.
Per-tab GPU shadows are regenerated only for new artwork or a changed render
size, shared for identical icons, and retired with the drag/device. Moving the
pointer or changing operations performs no shadow generation or CPU readback.

An offscreen rendering of the production drawing functions exercised Windows
folder artwork and the shadow effect in dark/light palettes with absent,
single-line and wrapped captions. All six renders retained the same local
artwork coordinates and 263×322 canvas at the sample size. The wrapped caption
grew upward with its bottom fixed. This checks component geometry and pixels,
not live Explorer timing, physical touch or displayed frame rates. Regression
suites and publication were deferred at that stage for the user's visual check.

## This PC follow-up and count badge

The supplied This PC capture (`Pasted text.txt`, 13:20:11–13:20:26) shows three
rows in Details mode. Held contacts 492 and 494 reach native HOLD with `row=-1`
from the initial DOWN. Neither enters held-item manipulation or promotion.
The capture contains no initial screen/client contact coordinates or hit-test
geometry, so it does not establish why these presses miss the displayed items.
This is an earlier failure than the already corrected OLE button handoff.

The working candidate added `[GPU touch hit]` records at DOWN and HOLD, including
mapped coordinates, viewport and cached item/group bounds, layout mode, DPI,
row count versus entry count and transition state. It queries no Shell provider,
and bounds row geometry logging to three entries per event. It deliberately
adds no This PC exception, fabricated hit or alternate drag route. Other special
folders are not declared broken by this capture.

The follow-up `thispclog.txt` establishes that held contacts 671/672 correctly
hit row 1. Their native selection remains marked unsafe. Selection imports
repeatedly return `E_FAIL` with the observed and expected counts both 3 and no
dirty-selection signal; no promotion is attempted. The initial row=-1 contact
670 was below the actual item bounds and was not the held-item failure.

Complete native-row projections now publish the same immutable path-to-child
index as namespace directory snapshots. This is constructed on the native-row
worker, adopted with that row generation and retired through the existing
background lane. This PC's selection imports can use `NamespaceSelectionKey`'s
existing raw-child/canonical matching against the displayed identities, avoiding
the failing provider-name translation. Selection verification, cancellation,
snapshot identity checks and all normal drag eligibility guards remain intact.
This repairs identity import for native projections without a touch/drive
exception or a second file-operation implementation. The user subsequently
confirmed this candidate and the earlier OLE handoff on hardware.

Count badges now consume the exact packed palette already used by the history
navigation bubble, including its contrast-selected foreground and opacity.
Feedback republishes palette changes along with existing theme/effect updates.
The immutable drag packet has an internal `dragBadgeScaling=0.75f`; diameter,
padding, width and font size derive from that single factor. The operation
emoji retains its size, and text antialiasing policy is unchanged. Count layouts
reuse the same resolved mod font format as captions, including family, face,
collection, style and stretch. Only the count layout's own size/wrapping/trimming
are changed; immutable shared formats are not modified during rendering.

A read-only local OS probe ran the production native-row publisher against This
PC, then resolved fresh children from a second Shell enumeration through the
published index: 2/2 identities matched. An offscreen render used `Consolas`
through the actual mod font setting/format resolver and showed the 0.75-scale
count using the configured face in both palettes. These are OS integration and
component checks, not a hardware touch test or the complete regression suite.

The subsequently authorized complete serial regression gate passed on
2026-11-02, including all 12 groups, normal/minified DLL builds and unload.
See [the regression investigation](regressions-0.9.341.md) for corrected fixture
assumptions, explicit prerequisites and retained validation logs. The source
remains identical to the hardware-approved candidate; publication is separate.
