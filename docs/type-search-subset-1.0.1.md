# First-letter candidates for incremental type-to-select

The first Unicode character establishes a candidate group. Subsequent characters
and Backspace within that group still use the same flexible prefix matcher;
longer-query failures never remove an item from the first-letter group. A name
may qualify after leading separators/emoji, just as it did before this change.
Leading separator-only or malformed initial characters use the uncached path.

Candidate eligibility is learned during normal first-hit searching rather than
through an extra full-directory pass. A bitmap retains exclusions and a second
bitmap remembers confirmed candidates. Known noncandidates are skipped a word
at a time. Unexamined rows remain eligible until reached, so a first-letter hit
near the top still returns immediately. The first later query that reaches the
bottom completes the group; subsequent queries inspect only that group.

The arrays allocate only when necessary and use two bits per row, about 25 KB
for 100,000 rows. When all rows qualify, subsequent searches bypass the index.
All state belongs to the tab owner; presentation snapshots do not copy it.
There are no new workers, messages, timers, provider calls or string copies per
filename. Empty-query cancellation, timeout, navigation, deactivation and rename
reset the cache. Directory replacements reset it, and binding also checks the
row storage, count, listing generation, layout revision, ordered-name digest,
first character and Unicode-classification availability.
Allocation failure disables the optional index and retains ordinary searching.

## Benchmark and validation

The standalone native harness extracts the production candidate structure,
preparation function, search traversal and matcher directly from the working
source. It compares cached searches with uncached searches using the **same
bounded flexible matcher**. This isolates the new index from the earlier
[filename-tail optimization](type-search-1.0.1.md).

On AMD Ryzen 9 9950X3D, Clang 20.1.3, x86-64, C++23, -O2, -static, medians of
seven alternating-order samples for 100,000 in-memory names:

| Mode | Six-character sequence | Uncached ms | Cached ms | Ratio |
|---|---|---:|---:|---:|
| ICU | Mixed first letters, late System match | 26.505 | 7.198 | 3.68x faster |
| ICU | Every item starts with S, late match | 26.710 | 27.651 | 3.5% slower |
| ICU | Immediate match on each key | 0.000070 | 0.000171 | +0.000101 ms |
| Windows fallback | Mixed first letters, late System match | 30.297 | 8.447 | 3.59x faster |
| Windows fallback | Every item starts with S, late match | 31.976 | 31.115 | No meaningful change |

Measured working-source SHA256:
DB4B19AD3FA4E24E35E0961D995BBEF0CF17450BC1E0DDF1D58F6BD6E11550B5.

The sequence is S, Sy, Sys, Syst, Syste, System. Mixed first letters reduced total
filename examinations from 500,019 to 115,389: S examined 19; Sy learned the rest
of the group while examining 99,982; each following query examined 3,847 instead
of 100,000. Thus this deliberately avoids an eager full scan on S; it does not
claim that an unexamined suffix is filtered for free before Sy.

The all-S case cannot narrow the group. Its one-time classification has a cost;
the cache detects complete acceptance and bypasses itself thereafter. Immediate
hits still examine six filenames across six keys and allocate no bitmaps. Their
sub-microsecond timings are throughput measurements of repeated sequences,
not claims about individual input-event latency.

The run passed 201,352 exact matcher/highlight comparisons against the preserved
original matcher and 40,000 indexed-versus-full-search selection comparisons.
The latter cover Backspace-like query reductions, repeated-letter cycling,
wraparound, empty-query resets, surrogate pairs, punctuation, dynamically hidden
rows, rename invalidation, reordered rows and additions across bitmap boundaries.
Actual owner-window invalidation wiring was source-reviewed; the mod was not
compiled or run, and no regression suite or live Explorer timing was performed.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/benchmark-type-search.ps1 -SourceFile 1.0.1.cpp -Rows 100000 -IncrementalOnly
```

[Raw results](type-search-subset-1.0.1-results.csv) include per-key examination
counts and both classification modes. Times exclude rendering, native input
delivery, selection synchronization and filesystem IO.
