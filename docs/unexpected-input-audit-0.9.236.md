# Unexpected-input audit and unified logging (0.9.236)

## Logging

The six public diagnostic options are replaced by one top-level boolean,
**Enable logging**, with no Logging settings section. One settings read controls
performance, frame timing, pipeline, UI-transition, GPU-memory and selection
diagnostics together. The existing bounded per-tab trace queues and background
writers remain in use. Error reporting remains available as before.

The option defaults to off. Logs include file names and paths; the README asks
users to enable it for a reproduction, include Windhawk's log, then disable it.
Internal category gates remain useful to the private component fixtures, but
users cannot configure them separately. The option contract checks the complete
settings schema, each runtime key/default and both switch directions, including
all six diagnostic gates. There are now 31 public options.

## Boundary findings and corrections

The review follows settings, Shell/provider outputs, clipboard capture,
navigation automation, pointer/keyboard/DirectManipulation ingress, shared
scrolling/geometry, decoded images, selection capture and OLE drag/drop. Changes
are made in their shared paths; no creation command or filename gets a special
case.

| Boundary | Earlier assumption/failure | Correction and fallback |
| --- | --- | --- |
| Shell column counts | `GetColumnCount` could cause an arbitrarily large allocation or descriptor loop on the UI apartment. | Reject zero/over-4096 counts before requesting keys in visible columns, the available catalog and the column menu. Preserve existing layout and release a failed catalog's pending latch. |
| Fixed column names | A provider's 80-character `wszName` was assumed to contain a null terminator. | Bound the copy to the actual array. A missing terminator cannot read adjacent stack memory. |
| PIDL enumeration | Capture counted non-null slots independently of `fetched`; contradictory count/status/null combinations could become an accepted selection. A zero-item `S_OK` could also look like a completed namespace enumeration. | Validate the HRESULT, requested/fetched count and slot presence before adoption in namespace enumeration, verification and asynchronous selection capture. Reject malformed batches, retain the GPU identity, keep unverified file commands blocked and use existing retry/error paths. Valid partial `S_FALSE` batches remain supported. |
| Scroll requests and geometry | `std::clamp` does not sanitize NaN; it could enter scroll/animation state. Individually finite track endpoints can also overflow when subtracted. | Ignore non-finite requests in shared vertical/horizontal scrolling and thumb input. Reject invalid/non-finite/overflowing scrollbar models and invalid fractional coordinates; compute the bounded viewport ratio before multiplication. |
| Icon pixel buffers | Readiness required dimensions and nonempty storage, without proving a complete row/stride/height allocation for upload and thumbnail copying. | Validate row-byte multiplication, stride and complete storage without overflowing. Reject a zero decode budget, cap requests at the renderer's existing icon limit and reset a successful decode's output status to `S_OK`. Missing/invalid images use existing fallback demand. |
| Navigation paths | BSTRs can contain embedded nulls, but null-terminated Shell APIs would interpret only a prefix. Oversized paths were copied without the fixed Shell path buffer's bound. | Reject embedded-NUL and oversized automation URLs/paths before conversion or enumeration. Existing bounded BYREF unwrapping rejects cycles and unsupported variants. |
| Incremental search | An empty directory returned before repeated-letter folding, allowing a continuous stream to grow the prefix indefinitely. | Bound the prefix at the supported filename/path character limit; keep Backspace and timeout behavior. |
| OLE target failure and identity | A rejected DragEnter could leave its destination attached for a later Drop. Outgoing callbacks could navigate/reenter while the original destination remained eligible. Targets could advertise effects the source had not allowed. | Detach failed targets, recheck native view/folder epoch and drag session after resolution/enter/over, constrain effects to the source's allowed operations, and cancel instead of invoking an invalid destination. Clear detaches old state before DragLeave callbacks, preserving a reentrant new session. |

The enumeration rules follow
[`IEnumIDList::Next`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ienumidlist-next).
The column label is the fixed array in
[`CM_COLUMNINFO`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ns-shobjidl_core-cm_columninfo).
The BSTR ambiguity is permitted by
[`SysAllocStringLen`](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-sysallocstringlen).

## Existing handling retained

- Numeric settings already clamp to supported ranges; enums, colors and missing
  fonts already have fallbacks. Sorting already treats unsupported variants and
  NaN values as absent; infinity still defines an ordered numeric value.
- DirectManipulation already rejects non-finite transforms/scales and bounds
  posted deltas. Rename already delegates name validity/canonicalization to
  Shell, closes the editor first and rolls back on failure.
- Clipboard capture already checks effect storage size/lock success, handles
  contention and failed clearing, and reads bounded, sequence-checked batches.
  Its native clipboard handles remain within the clipboard API contract.
- Native/GPU selection reconciliation, pending-rename ownership, callback
  draining, active-tab/focus checks and cancellation generations remain shared.
  No provider thread is terminated or moved across its COM ownership boundary.

This is boundary validation with targeted fault injection, not a claim to make
arbitrary invalid pointers, memory corruption or a hung in-process extension
recoverable. No broad exception handler hides those conditions.

## Checks

`tests/unexpected-input-smoke.h` exercises actual shared functions with private
offscreen windows, private temporary Shell folders, substituted focus and fake
drop destinations that only count calls. It performs no user file commands and
does not change the real clipboard or desktop focus.

The focused run covers all table entries, 2,000 deterministic mixed scrollbar
inputs, count overflows and missing PIDLs, valid partial-batch controls, valid
pixel/decode controls, ordinary navigation, oversized/embedded-null paths,
cyclic variants, bounded search/Backspace, failed drop enter/over, disallowed
effects, navigation during resolve/over, reentry during leave and valid copies.
Enumeration cleanup owns only the successfully reported prefix. The final
fixture deliberately places a non-owned sentinel in an unused partial-result
slot and in empty/error outputs; those values must never be dereferenced or
freed. A valid `S_FALSE` prefix still reaches the existing selection worker.
The six fault groups each fail against unchanged 0.9.235:

| Group | Old-source failure |
| --- | --- |
| Geometry | Non-finite input poisons shared scroll/animation state. |
| Pixels | A short pixel buffer is accepted for upload/thumbnail copying. |
| Navigation | An ambiguous or oversized navigation path is accepted. |
| Columns | Invalid count reaches the provider's key-snapshot boundary. |
| Selection | A malformed selection batch is accepted. |
| Drops | A rejected/stale/unauthorized drag advertises a drop effect. |

Focused logs are `tests/unexpected-input-0.9.236-retry.log`,
`tests/unexpected-input-0.9.235-negative.log` and
the per-group `tests/unexpected-input-0.9.235-negative-*.log` files.
Final validation: `build.bat -SourceFile 0.9.236.cpp` passes repository/export
checks, real Windhawk linking, all 31 option contracts, the full C++/offscreen
graphics/input suite, the six fault groups and non-owned-output sentinels,
21 real DLL unloads and process-exit/callback-draining checks. The existing
rename/reorder regression still passes. The final log is
`tests/build-0.9.236-final.log`.

The earlier full run passed its tests but correctly refused publication after
the final enumeration-ownership correction changed the working source. The
complete gate was rerun against the final, unchanged source before release.
Both exports match the tested archive byte for byte; all 39 earlier archives
remain unchanged. Component checks are separate from live Explorer behavior
and displayed frame-rate measurements; live Explorer was not manually tested
for this release.
