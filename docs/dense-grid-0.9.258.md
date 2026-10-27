# Dense grid drawing, 0.9.258

The supplied Recycle Bin trace contains 1,003 items and a maximized content
viewport of 4,751 by 1,958 pixels, on a display reported as 165 Hz. Across
584 completed drawing spans, CPU drawing takes a median 10.49 ms and p95
27.06 ms. EndDraw's median is about 0.06 ms. The trace also contains native
dispatch stalls, so it does not establish that drawing explains every delay
or that GPU execution itself is fast. No GPU timestamp data was supplied.

Two avoidable costs in the shared paths:

- Text rounded both screen-space edges before choosing layout/mask bounds.
  Fractional translation could change those bounds by one pixel. Filename
  selection also requested the unrounded bounds of the same shaped label,
  repeatedly changing wrapping and middle trimming before painting it.
- An icon size fallback allocated complete provider-key strings repeatedly
  while probing supported tiers, even when no caller needed the resolved key.
  This affects drawing, upload discovery, demand and presentation capture.

The fix rounds local text dimensions once for both selection and painting,
and snaps the drawing origin separately. Real size/font/monitor changes still
change the appropriate geometry or rendering generation. Icon lookup reuses
one local scratch string and only keeps a winning-key copy when requested.
Cache budgets, worker counts, image request sizes and frame pacing are unchanged.

Validation uses private generated models and GPU surfaces. It never opens or
modifies the user's Recycle Bin, clipboard, focus or files. The added
`--dense-grid` suite compares warm label reuse through subpixel scrolling at
96/144/192 DPI and profiles long-identity fallback lookup. Timings are offscreen CPU submission
measurements; they do not measure live Explorer or monitor scan-out.

One local comparison against 0.9.257, with 784 visible labels, a 129.6-pixel
row and 24 fractional scroll steps:

| Measurement | 0.9.257 | 0.9.258 |
| --- | ---: | ---: |
| Label mask hits / misses | 10,126 / 8,690 | 18,816 / 0 |
| Estimated glyph cache before / after | 49 / 56.625 MiB | 49 / 49 MiB |
| Mean offscreen CPU paint, LTR | 9.61 ms | 2.20 ms |
| 512 long-identity fallback lookups | 1.22 ms | 0.68 ms |

RTL also reused all 18,816 masks with zero misses and no cache growth,
averaging 2.38 ms per paint. Timings are observational, not test thresholds;
the regression assertions check reuse, bounded memory and fallback identity.
The published source fails the fractional-scroll assertion with the same
fixture. The modified source passes it.

The release gate passed: repository checks, Windhawk DLL linking, full C++
regressions and DLL unload checks. All six dense-grid DPI/direction cases had
zero warm mask misses and unchanged cache bytes. Published source copies match.
