# Preprocessing optimization: 0.9.304

Recorded October 31, 2026, using the same private real-directory fixture,
toolchain and machine as [0.9.303](preprocessing-0.9.303.md): Windows 11 build
26100, AMD Ryzen 9 9950X3D, local NTFS storage, Windhawk 1.7.3, Clang 20.1.3,
x64 C++23 `-O2` static regression configuration. These are warm component CPU/IO
measurements through complete-snapshot publication, not displayed Explorer
navigation latency or frame rates. Fixture creation, copies for algorithm
comparisons, verification and cleanup are outside the timed intervals.

## Results

Five pipeline samples per case, in milliseconds. First samples include worker
and provider startup; subsequent unchanged requests can reuse their baseline.
The complete pipeline samples run before the separate algorithm experiments.

| Sort | 1,000 median | 10,000 median | 50,000 median | 50,000 maximum |
| --- | ---: | ---: | ---: | ---: |
| Unsorted | 3.08 | 10.43 | 51.95 | 95.93 |
| Name ascending | 3.36 | 17.20 | 96.50 | 107.47 |
| Name descending | 3.60 | 18.96 | 86.79 | 98.67 |
| Modified descending | 3.14 | 14.57 | 62.52 | 80.16 |
| Size descending | 3.41 | 14.19 | 65.08 | 77.58 |
| Extension ascending | 4.06 | 23.24 | 114.59 | 126.57 |
| Type ascending | 4.28 | 23.66 | 123.76 | 207.07 |

The 50,000-item Name ascending median fell from 213.72 ms in published 0.9.303
to 96.50 ms, a 55% reduction. Against the earlier 280.16 ms development
reference before fingerprint reuse, the reduction is 66%. Modified fell from
231.06 to 62.52 ms; Type from 294.98 to 123.76 ms. These sequential runs include
scheduling/cache variation, and their medians are references rather than gates.

[105 raw pipeline samples](preprocessing-0.9.304.csv) retain all phase timings.
[360 instrumented sorting samples](sorting-0.9.304.csv) retain three samples for
each algorithm, property and input order, with comparison counts. On enumerated
50,000-item Name ascending input:

| Algorithm | Median sorting/permutation ms | Comparisons |
| --- | ---: | ---: |
| Direct introsort | 60.14 | 880,277 |
| Index introsort | 58.70 | 880,277 |
| Standard index merge sort | 29.35 | 483,682 |
| Natural index merge sort | 28.86 | 437,498 |

Natural merge also recognizes the sorted and reverse-sorted cases in 49,999
comparisons. Kernel times include counting comparisons and operate on retained
fixture rows; they are separate from the production worker phase measurements.
All ordering, metadata, identity-index and stability checks passed. The full
release build, C++ regressions and DLL-unload suites passed independently.

## Decisions

- Sort compact row indices, then apply the permutation once. This avoids moving
  strings, cells and shared identities during every comparison-sort partition.
  Filesystem, namespace, This PC and stable grouping use the shared path.
- Detect ascending and strictly descending runs, reverse descending runs, then
  merge adjacent runs in balanced rounds. This stable natural merge sort takes
  O(n log n) worst-case comparisons and O(n) auxiliary indices, and linear work
  for an already ordered or reversed sequence. Equal keys retain their order.
- Retain compact comparison data and one identity index for refresh baselines,
  replacing map nodes, duplicated path keys and full presentation entries.
  Exact path checks resolve folded-hash collisions. Existing live-update and
  first-changed-row reveal rules remain in use. Unchanged revalidations reuse
  the baseline's allocations and keys after checking folder, sort and both
  fingerprints. The first sample per sort includes creating the baseline.
- Build folded path hashes once for both fingerprints and identity indexing.
  Resume FNV hashing after a verified shared folder prefix; other paths use the
  complete-path fallback. Unicode folding still uses the same CRT function.

The path index is now built inside `directory.baseline` and moved into the
snapshot, so compare baseline plus snapshot times across versions rather than
interpreting the shorter snapshot phase alone as the saved work.

## Algorithm research

The installed libc++ 20.1.3 `__algorithm/sort.h` already implements introsort
with ideas from [pdqsort](https://github.com/orlp/pdqsort). Its branchless fast
path targets arithmetic keys and suitable comparators. The mod's comparator
includes Windows natural string comparison, direction, folder placement and
secondary keys; importing another quicksort does not remove those costs.

[IPS4o](https://github.com/ips4o/ips4o) is a parallel comparison sorter. Here the
first targets were avoiding repeated work and reducing expensive comparisons
on existing tab-owned lanes, without adding a shared parallel worker pool.

Natural runs are the useful adaptive idea described in
[CPython's sorting design](https://raw.githubusercontent.com/python/cpython/main/Objects/listsort.txt).
This mod uses a small balanced natural merge implementation, not Python's full
Timsort/Powersort implementation. Windows
[StrCmpLogicalW](https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strcmplogicalw)
remains the comparison authority; byte/radix sorting of filenames would not
preserve its numeric and Unicode ordering.

## Reproduction

```powershell
.\tests\run-regressions.ps1 -SourceFile previous-versions/0.9.304.cpp -TestArguments --preprocessing 2>&1 |
    Tee-Object -FilePath preprocessing-run.log
```

`PREPROCESS` records cover the complete production worker pipeline.
`SORT_KERNEL` records compare sorting plus permutation using the same real
enumerated entries. They include direct introsort, index introsort, standard
index merge sort and natural index merge sort; enumerated, reversed, shuffled,
sorted and reverse-sorted inputs; and comparison counts. The natural sorter is
also checked for stable equivalent keys, small/odd runs and metadata integrity.
The full release suite retains correctness checks without timing thresholds.
Use a matching source/test-harness revision to reproduce historical releases;
newer harnesses may depend on newer production helpers. Preserve the raw CSV
references when evolving the harness itself.
