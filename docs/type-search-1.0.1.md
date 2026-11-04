# Flexible search: bounded filename-word scanning

The fallback matcher previously classified an entire filename word before
comparing it, even when the remaining query word was much shorter. It now stops
that scan once it has reached the number of UTF-16 units the query word can
consume. Query words, separators, case comparison, result ordering, wraparound
and highlight semantics are unchanged. No normalized-name cache or asynchronous
search result handling was added.

The comparison still consumes the minimum of the two word lengths. Once the
filename scan reaches the query word length, any later filename characters
cannot change that minimum. Surrogate-pair stepping remains intact; the final
comparison length still uses that same minimum. The literal-prefix fast path
is unchanged. This bounds unnecessary filename work, not the folder row count:
a missing match can still visit every displayed row on the UI thread.

## Native CPU benchmark

Run on November 4, 2026, AMD Ryzen 9 9950X3D. The harness uses the actual Windows
CompareStringOrdinal/GetStringTypeW APIs and system ICU, with the current matcher
and ICU binding extracted directly from 1.0.1.cpp. The original matcher is
preserved in tests/type-search-baseline.h. Both implementations run in one native
executable, compiled with Windhawk's bundled Clang 20.1.3, C++23, x86_64-w64-mingw32,
-O2 and -static. The mod itself was not compiled.

Measured working-source SHA256:
E3201AD84C4A070FE53950AF9D0EB385C756C26A22DF159EA0E9EC584A1C33F9.

Each sample measures a full pass over 100,000 in-memory names for four queries;
the table reports milliseconds per query, averaged over those four queries.
Values are the median of seven samples, alternating which implementation runs
first. Allocation, corpus construction, ICU loading and correctness checks are
outside the measured intervals. Raw samples are in
[type-search-1.0.1-results.csv](type-search-1.0.1-results.csv).

| Mode | Workload | Before ms | After ms | Ratio |
|---|---|---:|---:|---:|
| ICU | Literal prefixes | 2.315 | 1.913 | 1.21x |
| ICU | Ordinary flexible queries | 32.837 | 32.185 | 1.02x |
| ICU | Short queries, long words | 262.138 | 20.628 | 12.71x |
| ICU | Emoji and long names | 104.427 | 27.503 | 3.80x |
| Windows fallback | Literal prefixes | 2.441 | 1.809 | 1.35x |
| Windows fallback | Ordinary flexible queries | 27.925 | 25.465 | 1.10x |
| Windows fallback | Short queries, long words | 282.302 | 24.177 | 11.68x |
| Windows fallback | Emoji and long names | 166.639 | 25.403 | 6.56x |

Literal-prefix instructions were not changed; differences there indicate timing
and code-layout noise, not an algorithmic improvement. The ordinary ICU workload
also shows no meaningful gain. The large gains are specific to avoiding long
unused filename tails. These synthetic CPU scans exclude native input delivery,
collapsed-section traversal, selection changes, rendering and provider IO. They
are not live Explorer latency measurements or frame-rate claims.

## Matching checks

The successful 100,000-row run performed 3,401,352 differential comparisons of
exact UTF-16 highlight extents, including nonmatches. It checked every benchmark
name/query pair in both modes, plus a deterministic corpus covering case,
punctuation, whitespace, emoji/ZWJ/variation selectors, supplementary letters and
digits, combining marks, malformed surrogates and embedded nulls. Independent
expected extents also check emoji and whitespace matches and preservation of a
supplementary leading letter.

Reproduce only this standalone benchmark and its checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/benchmark-type-search.ps1 -SourceFile 1.0.1.cpp -Rows 100000
```

The runner discovers the installed Windhawk toolchain and leaves its executable,
extracted source, raw samples and machine/compiler metadata under tests/.build/.
It does not invoke the release build or regression suite. Neither was run for
this change.
