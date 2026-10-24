# Office owner-file filtering (0.9.221)

The user-provided `~$…docx` example has Hidden and Archive attributes, without
System. The overlay's existing attribute-only filter permits that combination
when hidden items are shown. The user reports that the native view omits the
owner file, leaving an extra GPU row that can disrupt native selection matching.
Only the example's attributes were read; its contents were not opened or changed.

Extend `ShellItemVisible` at the existing filesystem enumeration boundary to
omit non-folder leaf names beginning with `~$`. This deliberately handles the
owner-file convention independently of extension or current hidden attributes,
including the interval before Office applies those attributes. It does not hide
directories with that prefix, names with the prefix in the middle, lone `~`/`$`
names, ordinary temporary files or unrelated hidden files. Existing Explorer
hidden/protected-file policies continue to apply to other entries. The Shell
namespace enumeration path remains authoritative for virtual folders.

Filtering precedes `Entry` construction and all names previews, sort/group
layout, path indices, selection and folder caching. There is no extra pass over
the directory or filtering during painting/input. Cancellation still counts all
visited filesystem records, including rejected files. The check reads only two
leading characters from the filename already supplied by `FindFirstFileExW` /
`FindNextFileW`; it performs no allocation, file open, attribute lookup or Shell
provider query. The enumeration lane already visits each filename and copies
the visible names to construct the model.

## Validation and performance

`tests/office-lock-smoke.h` creates a private child fixture and exercises real
asynchronous names/layout publication with sorted/unsorted directories under
all four hidden/protected policy combinations. It rejects owner files from
every preview/completed snapshot that is observed, verifies contiguous path
indices and End navigation, commits all filtered identities through the real
selection transaction with a substituted native view, and checks that owner
creation/removal changes neither visible hashes nor selection. Its cache
roundtrip uses the existing private validation-watch fixture. No user selection,
clipboard, focus, files or Shell settings are changed.

The archived 0.9.220 source fails because an Office lock enters the published
model (`tests/office-lock-220.log`). The focused 0.9.221 regression passes
(`tests/office-lock-221.log`). The predicate profile compares the same production
function with/without a supplied name over 100,000 cached `WIN32_FIND_DATAW`
records. Forty measured warm passes alternate execution order and consume and
verify the result counts. This isolates CPU predicate cost from filesystem and
Shell work. It is not a live Explorer latency or frame-rate measurement, and
timing noise must not be interpreted as a speedup when the difference is negative.

The optimized release run measured median passes of 0.166 ms with attribute
filtering alone and 0.1731 ms with the prefix check: 0.0071 ms additional across
100,000 records in this run. The small difference includes timing noise and is
not a cross-hardware guarantee. It supports retaining the simple background
filter rather than introducing a separate visible-but-noninteractive item model.

`build.bat -SourceFile 0.9.221.cpp` passed the complete release gate and published
the tested source; see [`tests/build-0.9.221.log`](../tests/build-0.9.221.log).
This includes all 32 option contracts, existing independent presentation,
animation, clipboard, selection, cache and filesystem-watch regressions, and
21 actual API-hook DLL unload cycles. Critical callback stacks stay below 8 KiB
(maximum 1,960 bytes). All 24 earlier source archives retain their recorded
hashes, and latest.cpp matches the 0.9.221 archive. These are private/offscreen
checks, separate from live Explorer display validation.
