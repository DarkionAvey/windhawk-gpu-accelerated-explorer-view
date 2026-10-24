# Readable automatic column fitting (0.9.220)

The screenshot's six columns were all being compressed toward a universal
55-DIP floor. That includes the Name icon and its insets, leaving only a few
characters of text. The fitter only exposed horizontal scrolling once even
those very small widths no longer fit.

The documented [CM_COLUMNINFO descriptor](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ns-shobjidl_core-cm_columninfo)
provides current, default and ideal widths, without a minimum-width field.
Default and ideal widths are not readability limits. This change therefore
uses a bounded, font- and density-aware policy in the existing fitter:

- Name starts at 180 DIP; other columns start at 112 DIP.
- Increase that allowance to accommodate twelve numeric-character advances
  for Name or ten for metadata, measured with the actual body font. Include
  the existing icon, text origins and trailing insets. This also corrects the
  Name fitter's previously omitted eight-pixel trailing text inset.
- Include ordinary header text and its sort-arrow allowance, measured with
  the actual normal/bold header font. Bound unusually long captions at twice
  the column's body allowance so a provider cannot impose enormous minima.
- Compress only space above each column's own integer minimum. If the sum of
  those minima exceeds the viewport, retain them and use the existing
  horizontal scrollbar. When space returns, distribute it through the normal
  preferred-width fitter. Account for each trailing minimum during rounding.
- Preserve the last fitted floor when the existing layout code absorbs a
  vertical-scrollbar clearance sliver. Otherwise that post-fit adjustment
  could quietly violate the floor. Explicit divider drags clear this fitted
  constraint and continue using the existing manual sizing limits.

All shaping uses the existing per-tab cache. One bounded cached numeric string
supplies the font measurement; no new Shell/provider calls, timers, background
lanes or settings are introduced. Immutable column snapshots carry the floor
through the existing presentation owner. Layout, horizontal scrolling and
animations continue using the shared geometry.

## Validation

`tests/column-minimum-smoke.h` exercises production fitting and scrollbar
geometry with real DirectWrite fonts at 96/144/192 DPI, font settings
20/52/80 percent, and both LTR and RTL. It checks meaningful text room after
icon/insets using independently created text layouts, complete ordinary
headers, empty/populated folders, long caption bounds, near-threshold integer
rounding, vertical-bar clearance, access to the final column, wide-window
redistribution and deliberate manual sizing. The archived 0.9.219 source fails
the readable-width assertion (`tests/column-minimum-219.log`); 0.9.220 passes
all eighteen combinations (`tests/column-minimum-220.log`). At 96 DPI and the
default font setting, the six-column fixture retains Name at 180 pixels and
Size at 114 pixels, exposing 241 pixels of horizontal scrolling in a 560-pixel
window instead of compressing every column to fit.

The existing production attachment test also checks the actual native-frame
resize -> host layout -> overlay -> asynchronous fit -> presentation receipt
chain, now expecting readable overflow in its narrow six-column window and
viewport fitting in its wide windows. Disabled automatic fitting continues to
preserve native widths. These are private component/offscreen checks; they do
not measure live Explorer display frame rates.

The complete release gate passed and published 0.9.220; see
[`tests/build-0.9.220.log`](../tests/build-0.9.220.log). This includes all 32
option contracts, existing animation/clipboard/selection/independent-owner
regressions, and 21 real API-hook DLL unload cycles. Critical callback stacks
remain below 8 KiB (maximum 1,960 bytes). All 23 earlier source archives retain
their recorded hashes and the published source matches its 0.9.220 archive.
