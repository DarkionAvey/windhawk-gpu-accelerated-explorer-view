# RTL/LTR coordinate audit — 0.9.192

Earlier RTL tests mostly set `rtlLayout` on LTR windows. They did not reproduce inherited Win32 mirroring in Arabic Explorer. The new fixture creates real `WS_EX_LAYOUTRTL` frame/native hierarchies and uses production attachment, drawing, pointer handling and rename controls.

## Findings and fixes

1. **Inherited client mirroring conflicted with GPU mirroring.** The renderer already converts logical start/end geometry into physical pixels. An inherited mirrored HWND gave input another coordinate convention. The owned overlay now clears `WS_EX_LAYOUTRTL` during `WM_NCCREATE` and sets `WS_EX_NOINHERITLAYOUT`. Explorer and its native children retain their styles. This follows Microsoft's documented method for [opting an individual child out of inherited mirroring](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#window-layout-and-mirroring).
2. **Screen/client mapping was inconsistent.** Captured pointer sampling, hover recovery, cursor selection, OLE drop hit testing, popup anchors and tracing now share `MapUiPoint`/`MapWindowPoints`. A successful zero translation is accepted. Existing two-point RECT mappings retain mirrored edge swapping.
3. **Rename editors inherited geometric mirroring.** The sibling EDIT now has physical client coordinates while retaining RTL reading order/right alignment. Its bounds come from the filename's painted geometry and are mapped once into the frame's placement coordinates. Existing live sizing and caret behavior remain.
4. **Status-bar placement mirrored twice.** Fallback positioning, native-neighbor discovery and adjacency now account for both UI direction and the frame's coordinate layout. The owned button/tooltip opt out of geometric mirroring; the custom GDI painter uses physical pixels. Native controls remain unchanged.
5. **Touchpad X deltas and history feedback assumed LTR.** Physical horizontal movement now enters the logical scrolling/history model once. History feedback uses the matching physical edge and arrow direction. OS inertia, release thresholds, cancellation and durations remain unchanged.
6. **Popup direction differed between paths.** Column, background and selected-item menus share `ShellMenuFlags`, including RTL layout/right alignment. Background menus retain notifications required by Shell extensions and New.

## Coordinate boundaries

| Boundary | Convention |
| --- | --- |
| Item, column and scroll model | Logical start to end |
| GPU target and owned HWND client input | Physical left-to-right pixel coordinates |
| Text | DirectWrite/native EDIT reading order, independently of geometry |
| Screen cursor and OLE points | Physical screen coordinates, mapped into the client once |
| Explorer frame child placement | Parent's actual Win32 coordinates; mapped RECTs or explicit frame-mirroring accounting |
| Native viewport restoration | Native parent's coordinate convention and existing ownership checks |

Reviewed drawing includes names/metadata, glyph masks, selection/hover, group headings, grids, thumbnail decorations, scrollbars, marquee, clipping and temporary DirectComposition planes. Existing logical-to-physical drawing remains authoritative. No bitmap/glyph mirroring, process-wide layout change, worker, timer, Shell input query or shared queue was added.

## Validation

`rtl-window-smoke.h` covers genuine mirrored RTL hosts, unmirrored RTL hosts and LTR hosts. It checks overlay/native bounds, pointer messages and sampled hover, Details/List/grid hitboxes, headings, both scrollbars, horizontal touchpad movement, marquee, mixed Arabic/Latin rename text and its physical anchor, native-neighbor/fallback status placement, history direction/release and native viewport restoration. Windows remain off screen and focus/cursor fixtures avoid desktop input injection.

Preserved 0.9.191 fails the new regression at `RTL touchpad pan moved opposite to physical content movement`. Focused 0.9.192 checks pass. The first full run also exposed an older synthetic English-status-bar fixture depending on the desktop's Arabic UI fallback. The harness now makes its fallback language explicit and separately checks Arabic and English fallback detection; real HWND mirroring and native Shell controls remain real.

Live Arabic Explorer confirmation and monitor scan-out are separate from these checks. The harness never changes Windows' language.

The final 0.9.192 linked against the installed Windhawk host and passed the full regression suite, including the mirrored-window matrix, existing rendering/animation/input checks, 18 actual DLL unload cycles and native-view restoration on disable.

Preserved 0.9.191 SHA256: `632272D4F94F3839AA266B36971A9D6310D18A5D063DB4F85BB0B70D946000B4`.
