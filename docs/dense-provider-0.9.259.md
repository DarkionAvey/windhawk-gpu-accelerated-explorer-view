# Remaining dense-grid costs, 0.9.259

The follow-up Recycle Bin trace confirms warm text-cache hits and stable glyph
memory after 0.9.258. Across 522 completed drawing spans, median CPU drawing
is 8.12 ms and p95 is 21.56 ms. Steady late frames show about 8 ms drawing and
7 ms owner-thread frame work, against a reported 165 Hz / 6.06 ms interval.
Median EndDraw is about 0.09 ms and Present about 0.07 ms. Dropped trace records
and absent GPU timestamps limit conclusions about actual GPU execution.

The remaining shared identity work was reproduced in private fixtures:

- Presentation image deduplication used ordered string comparisons, repeatedly
  walking long shared identity prefixes on the owner thread. It now uses hash
  membership, transferring already-created keys into the same temporary set.
- Item opacity and clipboard identity comparisons repeatedly case-folded those
  prefixes. Exact common characters are now skipped before the original CRT
  case-insensitive suffix comparison. Unicode, case and terminator semantics
  are retained; no second identity index is added.
- Icon size fallback constructed and hashed complete identities for each tier.
  Heterogeneous probes now reuse the base hash and stack-only tier prefixes.
  The same owning icon map, exact equality and source-quality ranking remain.

No image quality, overscan, text readiness, resource budget, worker count or
frame-pacing reduction is used. Existing per-tab ownership and lifecycle paths
are unchanged. Trace stages `presentation.capture`, `render.items` and
`render.labels` make these costs distinguishable in subsequent captures.

Before/after observations in one private 1,003-row model with 512 cached
32-pixel icons and generated 1,024-character identities:

| CPU measurement | 0.9.258 | 0.9.259 focused comparison |
| --- | ---: | ---: |
| Capture presentation snapshot | 16.68 ms | 4.31 ms |
| Warm offscreen painting | 11.32 ms | 5.63 ms |
| 512 size-fallback queries, 256-character identities | 0.668 ms | 0.319 ms |

Timings are observations, not pass thresholds or displayed frame rates. These
fixtures intentionally stress long shared prefixes; actual provider identities
were not included in the user's trace, so their length is not inferred from it.
Tests do not read or modify the user's Recycle Bin, files, clipboard or focus.
They assert preserved rows/images, no warm text misses or cache growth, exact
key behavior across rehash and nested prefixes, and case/Unicode comparison
equivalence. Existing full suites also cover thumbnails, zoom, selection,
clipboard animation, per-tab independence and DLL teardown.

The full release gate passed, including Windhawk linking, repository checks,
all C++ suites and 21 actual DLL unloads. During that full run the provider
fixture measured 5.05 ms capture and 7.97 ms paint, illustrating load-dependent
timing variation; its correctness and fixed-cache assertions still passed.
Live Explorer performance has not been remeasured after this release.
