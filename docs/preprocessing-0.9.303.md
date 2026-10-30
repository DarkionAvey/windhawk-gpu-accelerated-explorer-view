# Preprocessing reference: 0.9.303

Recorded October 30, 2026 on Windows 11 build 26100, AMD Ryzen 9 9950X3D
(32 logical processors), local NTFS storage. Windhawk 1.7.3's Clang 20.1.3
built the x64 standalone harness with C++23, `-O2` and the repository's usual
static regression configuration.

The harness creates private real directories with 1,000, 10,000 and 50,000
entries, including folders, Unicode/numeric names, four extensions, sizes and
timestamps. It uses the production tab-owned workers, native filesystem APIs,
comparison code and publication pipeline. It checks counts, ordering, complete
path indexes, fingerprints and the opt-out path's native enumeration order.
It does not synthesize input or notification messages.

Five samples per case; durations below are milliseconds from request to the
worker's complete-snapshot publication marker, before insertion into the
delivery queue. Creation, observer polling, verification and cleanup are excluded.
The first samples include worker/provider startup.
Filesystem caches are warm from fixture creation; no cache flushing is attempted.

| Sort | 1,000 median | 10,000 median | 50,000 median | 50,000 maximum |
| --- | ---: | ---: | ---: | ---: |
| Unsorted | 4.36 | 29.69 | 143.30 | 254.88 |
| Name ascending | 5.06 | 39.55 | 213.72 | 234.67 |
| Name descending | 4.65 | 44.43 | 224.47 | 230.79 |
| Modified descending | 4.11 | 47.83 | 231.06 | 271.80 |
| Size descending | 4.84 | 39.27 | 209.65 | 222.71 |
| Extension ascending | 6.18 | 54.43 | 272.52 | 283.57 |
| Type ascending | 6.33 | 59.62 | 294.98 | 299.59 |

[Raw reference samples](preprocessing-0.9.303.csv) include provider binding,
enumeration, sorting, sort-baseline preparation and snapshot preparation times.
Unlisted startup, queueing, handoff and bookkeeping also contribute to total time.

During development, the worker calculated the same order/metadata fingerprints
for its sort baseline and again for the published snapshot. The final code reuses
the completed fingerprints. In the sequential reference runs, 50,000-item Name
ascending snapshot preparation fell from a median of 86.44 to 39.13 ms, and total
preprocessing from 280.16 to 213.72 ms. The
[before-reuse samples](preprocessing-0.9.303-before.csv) are from the same
development version before that optimization. Whole-run differences also include
scheduling and cache variation; these figures are references, not timing gates.

To reproduce the archived baseline:

```powershell
.\tests\run-regressions.ps1 -SourceFile previous-versions/0.9.303.cpp -TestArguments --preprocessing
```

Use a numbered working source for a candidate change, or `latest.cpp` for the
current release. Preserve raw samples, environment and toolchain details when
comparing. The release gate also runs the 1,000/10,000-item cases three times.
There are no machine-dependent timing thresholds.

These are component preprocessing measurements, not live Explorer navigation
latency, rendered/displayed frame rates, cold-cache disk benchmarks, or coverage
of network storage and third-party property providers. Native sort conformance
is tested separately with a private ExplorerBrowser in `--initial-sort`, while
the existing blocked-provider and multi-tab suites check lane independence.
