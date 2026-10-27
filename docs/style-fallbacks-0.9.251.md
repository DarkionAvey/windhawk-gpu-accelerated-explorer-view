# Resolved style fallbacks (0.9.251)

The foreground contrast policy described below caused a regression and is
superseded by [0.9.252](style-fallbacks-0.9.252.md). Theme/app background values
cannot establish the actual inherited Explorer surface and must not reject
valid native foregrounds. The original tests missed that disagreement.

The reported photo shows drive icons and blue capacity fills while labels and
other foreground elements are absent. This is consistent with a bad common
foreground, but does not establish that PC's theme values or font configuration.

The COLORREF-to-Direct2D RGB conversion was already correct. The old palette
accepted any successful GetThemeColor result, including invalid RGB values and
black on a dark surface. Its final COLOR_WINDOWTEXT fallback can also be black
in dark Explorer. Missing font names were passed through without confirming
installation, and native font checks accepted extreme heights and incomplete
names. Switching from custom selection color to accent could retain the custom
color if DWM lookup failed.

| Input | Validation and fallback |
| --- | --- |
| Native text/header colors | Require a 24-bit COLORREF. Try attached theme, ItemsView defaults and normal items, app foreground, system foreground, then the more contrasting black/white. Headers fall back to body color. |
| Background for validation | Prefer a valid native theme fill, then opaque Windows app background, then system window background, then white. The overlay continues to be transparent. |
| Readability | Reject base foregrounds below 2:1 contrast against the resolved surface. This guards against effectively invisible values; it is not an accessibility guarantee. Intentional secondary/animation opacity remains unchanged. |
| High contrast | Preserve valid Windows system colors exactly, without the contrast guard. |
| App palette/status button | Reject nonopaque WinRT entries; validate foreground against the resulting button surface. Cache copied values per tab and refresh on theme/settings changes, avoiding periodic COM activation. |
| Selection color | Trim outer whitespace; require six hex digits or use accent. Failed/transparent DWM color falls back to COLOR_HIGHLIGHT, then the default blue. Never retain an old custom color. |
| Font names | Bound length, reject control characters, require installed family/face for custom fonts; unknown names use native font. Invalidate font cache on WM_FONTCHANGE. |
| Native font metrics | Require a terminated, nonempty face, 4..96 logical-pixel height and valid weight. Try theme, per-DPI message font, legacy message font, stock GUI font, then Segoe UI at 12 logical pixels. |
| Text formats | Validate finite positive body size; unusable values use 17 logical pixels. Retry native, Segoe UI and Arial families. Commit complete replacements only; sorted format can reuse the regular header on failure. |
| DPI/wheel/timing | Window and copied DPI use 96 outside 72..768. Wheel lookup failure/unreasonable values use three lines, preserving zero and page-scroll. Clamp refresh rate before integer conversion; require positive QPC frequency. |
| Existing settings | Row height, font percentages, margins, radii and opacities are bounded. Unknown enum values have defaults. Zero opacity/radius/margins remain valid user choices. |
| Existing Shell geometry | View modes are range-checked; unavailable modes defer or restore native view. Icon size uses mode-specific defaults outside 16..512; column widths use native default or bounded conventional widths. |

`--styles` injects theme/API failures and successful-but-invalid returns into
the real selected source. It covers dark/light/custom palettes, high contrast,
independent tab caches, invalid native/custom fonts, sizes and DPI, and reads
real offscreen GPU glyph/chrome pixels against a dark surface. `--options`
also covers accent failure, whitespace and wheel values. Fixtures use private
windows and substituted APIs, without changing Windows settings, focus or the
clipboard. These are component tests, not a reproduction on the reporter's PC
or a measurement of live Explorer frame rates.

Paint-only modifications made by other Explorer mods cannot be discovered
reliably from theme resources. If they report a different background from the
one they actually paint, contrast validation can only use the reported surface.
With logging enabled, `[GPU style]` records source and resolved colors when
the palette changes, to help diagnose that case.

Validation: `build.bat -SourceFile 0.9.251.cpp` passed the repository checks,
Windhawk DLL link/stack checks, full C++ pipeline suite (including `--styles`
and all 31 options), and actual DLL-unload suite. The release gate finalized
the tested source as 0.9.251.
