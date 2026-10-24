# Selection handoff and count-limit audit — 0.9.195

## Supplied capture

Input: attachment `4d4e170e-bd03-4345-a20e-93a8fc334ee3`, deletion and Undo
in the repository folder (48,077 pasted lines). The capture reports 22,393
dropped pipeline records. Missing spans cannot be treated as proof that no
work happened; producer durations are more useful than writer timestamp gaps.

The detach is explicit: `RejectNativeSelectionImport` reports a **large partial
selection, observed=82, expected=85; revealing Explorer** thirteen times,
starting at the entry timestamped 15:24:34.355. The previous implementation
accepted small native selections or Select All, but rejected a partial group
above 64 unless it could verify an earlier GPU-prepared selection. Undo and
native restoration can produce a legitimate group without that prepared record.
Re-enabling the overlay then re-entered the same rejection path.

The renderer is not the dominant measured cost in the captured jitter:

| Recorded work | Evidence |
| --- | --- |
| Ordinary GPU drawing | Summary averages around 0.4–0.5 ms; the 15:24:46.647 summary records 0.461 ms average / 1.223 ms maximum. |
| Native selection commit | 82-item commit reports 31 ms; one `native.selection` span records 32.597 ms. This remains an owner-apartment Shell transaction. |
| Native navigation-tree dispatch | `SysTreeView32`, message `0xB04`: producer spans include 1,011.119 ms and 972.974 ms. |
| Right-button handler | About 1,299 ms inclusive. It includes popup-menu tracking and nested dispatch, so it is not a measurement of file-operation CPU cost alone. |

These are elapsed submission/dispatch measurements, not display scan-out FPS.
The patch removes the proven count-triggered detach and moves native-import
identity resolution off the UI thread. It does not establish that Explorer's
navigation-tree callbacks or all Undo stalls have been eliminated. Animation
duration/easing and native navigation-tree behavior are unchanged.

## Limits changed

| Previous behavior | 0.9.195 behavior |
| --- | --- |
| Partial native selection above 64 could disable the GPU view. | Selected relative PIDLs are copied in short batches on the owning STA, then resolved on the existing per-tab selection lane. No live folder/view object crosses apartments. The verified native set is imported without reselecting it. |
| Failed-selection recovery above 128 disabled the GPU view. | Uses the same asynchronous import path. Commands wait for a valid identity set. |
| Invert Selection in a directory above 64 entries disabled the GPU view. | Uses the existing GPU flags, selection animation and prepared native transaction at every size. |
| Filtered Ctrl+A above the recovery threshold could detach. | Imports the authoritative native set through the same path. |
| A transient large-context-menu count mismatch detached. | Reconciles native selection and defers the requested menu when preparation is pending. |
| Count/object-array drift during Delete/Undo was treated as an invalid selection. | Raw-PIDL capture retries changing counts without hiding the GPU. Generation, native HWND, tab, count and newer-selection checks reject stale results; unsafe snapshots cannot authorize file commands. |
| Cut feedback disappeared above 8,192 clipboard paths. | Sequence-checked batches publish the complete set atomically. Clipboard ownership is released between batches. Replaced path sets retire through the existing cleanup lane. Escape and newer clipboard contents retain their existing precedence. |
| More than 128 visible columns or 1,024 available columns could be ignored. | Native-reported counts are accepted. Background label mapping follows the provider's end-of-enumeration result, with cancellation and the API's index range. |
| More than 2,048 folder snapshots were evicted even under the byte budget. | RAM accounting controls retention. It also charges the map node/key; pressure victims are ranked once rather than rescanning the history for every eviction. |
| This PC path lookup assumed at most 64 items. | Its directly projected identities remain searchable beyond that count. Filesystem/Recycle Bin snapshots retain their worker-built indexes. |
| Type-to-select cleared a prefix at 256 characters. | The existing timeout/editing logic owns the prefix; a character count no longer resets it. |

## Limits retained and why

- **Per-frame batch/time budgets, worker concurrency and notification coalescing:**
  limit work per turn, rather than total items supported. Pending work continues.
- **Folder/icon/text-cache byte budgets, LRU eviction and texture/raster size
  limits:** bound retained resources. Evicted content can be fetched/rendered
  again; large text uses the existing direct-rendering fallback and enlarged
  grid images retain the existing bounded source-resolution policy.
- **Viewport sampling for automatic fitting and animation snapshots:** fitting
  and paint preparation stay proportional to visible demand. These do not
  truncate the directory or its logical selection.
- **32-entry Shell-change coalescing and frame-registry cleanup thresholds:**
  overflow uses revalidation; valid tabs/windows are not capped at that count.
- **The small linear lookup fallback for unindexed filesystem previews:**
  complete filesystem/Recycle Bin snapshots already carry the background index.
  It prevents an accidental quadratic UI scan, rather than capping folder size.
- **Settings bounds, Win32/API representation limits and explicitly unsupported
  layouts/namespaces:** separate compatibility/presentation policies. Tile mode
  outside This PC and Content mode retain their previously requested behavior.

No process-wide selection queue, new worker lane or timer was added. Workers
remain tab-owned and use the existing stop/unload inventory.

## Validation

Private fixtures cover an 82-item external partial group, count drift and a
same-count replacement, cancellation by a newer click, large Invert Selection,
and This PC lookup beyond 64. They verify zero native reselection and no
`IShellItemArray` construction during the large import. Clipboard checks cover
9,000 paths, released clipboard ownership between batches and Escape. Column
checks cover 1,100 available and 200 visible provider fields. Folder-cache
checks retain 2,100 small snapshots under budget, then verify LRU byte-pressure
eviction. Existing graphics, native suppression, focus, independent-tab and
DLL-unload checks remain part of the full suite.

Live Explorer confirmation remains separate from these isolated regressions.
The final 0.9.195 source linked against the installed Windhawk host API and
passed the complete standalone suite plus all 18 actual DLL unload cycles.
