# Remaining frame-budget pressure, 0.9.260

The third Recycle Bin trace reports a 165 Hz display (6.06 ms interval).
Across 434 completed drawing spans, CPU drawing has a 6.60 ms median and
14.73 ms p95. The new stages isolate item work (4.69 ms median) and labels
(2.01 ms median, 47.32 ms maximum during zoom). Snapshot capture is 3.35 ms
median. EndDraw and Present remain short CPU spans; no GPU timestamps or
scan-out measurements were supplied. Dropped records prevent deriving FPS
from raw event counts.

Private tests isolated the remaining long-identity opacity lookup cost.
Replacing its ordered map with a case-folded hash map was slower (6.03 ms
versus 2.78 ms per sampled viewport). The map and comparison semantics remain;
SSE2 now skips equal UTF-16 prefixes eight units at a time before the original
CRT case comparison. All loads stay inside both strings and stop at NULs.
SSE2 is guaranteed by the existing x64-only target.

Font-size-only changes now resize the existing DirectWrite layouts. Face,
direction, DPI and alignment changes retain invalidation. New-size glyph masks
still use the existing memory and preparation budgets, and stale masks still
retire after painting. Text is rasterized at its actual font size; cached text
is not stretched. Pixel tests compare reused and freshly created layouts,
including fractional sizes, mixed Arabic/Hebrew, wrapping, middle trimming and
opacity. A native metrics probe showed that removing trimming-state changes
would alter line/cluster metrics, so that shortcut was rejected.

One focused offscreen comparison, with the same private generated identities:

| CPU measurement | 0.9.259 | 0.9.260 |
| --- | ---: | ---: |
| Provider viewport paint | 5.76 ms | 2.95 ms |
| Isolated opacity tracking | 2.78 ms | 0.54 ms |
| Isolated icons | 1.25 ms | 1.20 ms |
| Isolated labels | 0.71 ms | 0.76 ms |
| 24-step dense wrapped-label zoom, p50 | 23.57 ms | 16.85 ms |
| Same zoom, p95 | 27.05 ms | 18.73 ms |

Warm private drawing fits the 165 Hz CPU interval. Cold font reflow still
exceeds it; these observations do not establish 165 displayed FPS in Explorer.
No resource budget, worker count, image quality, overscan, input or pacing
reduction is used. Fixtures do not operate the user's files, clipboard or focus.

The full release gate passed, including repository checks, Windhawk linking,
all C++ suites and 21 actual DLL unloads. Resized/fresh text pixels matched
exactly. During the loaded full suite, provider paint measured 4.36 ms and
opacity tracking 0.81 ms; zoom measured 24.73 ms p50 / 27.16 ms p95. Timing
variation and the remaining reflow cost are why these results are not a
guarantee of 165 displayed FPS. Published source copies are identical.
