# Rename selection identity (0.9.235)

## What refresh reconciles

`ApplyDirectoryNames` already preserves selected paths and the focused path,
swaps in the worker's newly sorted names/index, and resolves those paths back to
new GPU row numbers. It then applies pending native selection observations.
Consequently, successful path reconciliation alone does not ensure the selected
item survives the subsequent native import/recovery.

The missing case in 0.9.234 is delayed native PIDL readiness. `SetNameOf` can
finish the filesystem rename before the live `IFolderView2` accepts the returned
identity as its selection. `FinishRename` queues an asynchronous single-item
restore when its immediate native synchronization fails. However, a pending
recovery could still clear selection after the names refresh had correctly
reconciled the new path. Down at the last item also cancelled the restore at
message admission, although it selected the same row and performed no new
native synchronization. The next arrow then started from an empty GPU caret.
An unchanged `New Folder` label does not encounter that rename/readiness gap.

## Reproduction and correction

`TestRenameReorderReadiness` creates private folders with `New Folder` away from
the first row and renames it through the real Shell folder to `zyx`, sorted last.
Its substituted native view temporarily refuses the new selection and reports
no selected/focused item, then becomes ready later. It publishes sorted names,
attempts native import/recovery, presses Down, and verifies both identities after
the native view accepts selection. The unchanged-name control exercises ordinary
navigation. Both cases run in LTR/RTL list/grid layouts without changing the
user's desktop focus, folders or clipboard.

Running that new test against unchanged published 0.9.234 fails with
`delayed native rename readiness erased reconciled GPU selection`. The 0.9.235
working source passes the same sequence. This reproduces a code-level mechanism
consistent with the report; it is not a live Explorer capture.

The correction extends the existing single-item restore rather than adding a
creation-route-specific timer or fixed row. The restore carries the canonical
path as well as the display label. Capture, import and recovery preserve that
explicit selected identity while native readiness is pending. A display label
can differ from its parsing leaf, so retry/protection use the retained path.
The test deliberately varies the pending display label to verify this rule.

The existing paced owner-STA retry selects a single PIDL with `SVSI_NOTAKEFOCUS`,
checks the active native view, verifies canonical focused identity and the
selected set, and only then invalidates stale observations and releases the
pending restore. No-op keyboard movement retains verification; a different
explicit selection or navigation cancels it through shared selection paths.
Unverified native selections continue to block file commands.

## Timestamped selection diagnostics

Enable **Logging > Selection identity diagnostics**, reproduce once, and collect
Windhawk records beginning `[GPU selection trace]`. Other performance/pipeline
logging options are independent. Disable the option afterward; names and paths
appear in these records.

Each record includes a per-view sequence, monotonic start/end ticks, thread ID,
view/directory generations, HWND focus, GPU selected row/flag/name/path, native
selection count, native focused row/path, up to four selected native row/paths,
HRESULTs, pending restore path and recovery state. Native samples query the live
owner-STA view rather than its cached selection observation. They are bounded
and protected against reentrant selection imports. The default-disabled trace
does no diagnostic Shell queries. Selected native rows use
[`GetSelectedItem`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-getselecteditem)
with its inclusive starting index and explicit `S_OK` handling.

Trace points bracket Shell rename, committed identity, native synchronization,
names publication/mapping/import, recovery and arrow input. In the controlled
fixture, `names.mapped` and `key.after-select` show GPU row 11 selected at `zyx`
while `native-count=0`; `recovery.defer-identity` retains it. Later,
`native.restore.verified` shows `native-count=1` with matching native/GPU paths.
The component suite captures and checks the actual formatted production log
records as well as the underlying state. Its printed trace is explicitly marked
as a private fixture capture.

## Validation

Focused old-source failure: `tests/rename-reorder-0.9.235-before.log`.
Focused working-source checks: `tests/rename-reorder-0.9.235-diagnostics.log`.
The final trace, including the canonical-path/display-label check, is in
`tests/build-0.9.235-final.log`.
The first full gate caught a status-button plain-Escape routing assertion that
passed immediately in the same retained binary's isolated `--rtl-windows` run.
That fixture used live modifier state despite substituting focus and pointer
input. It now uses the existing scoped neutral-modifier fixture through nested
Shell pumps; every original routing/focus assertion remains enabled.
`build.bat -SourceFile 0.9.235.cpp` passes repository/export checks, real Windhawk
host linking, all 36 option contracts, complete C++/offscreen graphics/input
regressions, 21 real DLL unloads, and process-exit/callback draining checks.
The full run includes the new rename/readiness trace assertions, the status-focus
fixture, and native-selection checks with 551 private filesystem items.
`latest.cpp`, `gpu-accelerated-explorer-view.wh.cpp` and the tested 0.9.235 archive
have identical SHA-256 hashes. All 38 earlier archives retain their original
bytes. Live Explorer behavior has not been manually verified.
