# Namespace membership uses the shared item snapshot pipeline (0.9.222)

## Evidence

The supplied log retains overlay HWND `0x1093A`, view token `2`, and its existing
presentation lane while the This PC model grows from two to three items:

- 11:16:01.696: the model contains two items.
- 11:16:03.055: a This PC capacity request is submitted.
- 11:16:03.572: namespace token 40 delivers a complete three-item snapshot.
- 11:16:03.585: the presenter receives three model items; subsequent row scenes
  contain five sources, consistent with two old rows becoming ghosts plus all
  three new rows entering.
- 11:16:05.087: token 42 delivers another complete three-item snapshot.

This supports a row reconciliation problem, rather than destruction of the
overlay window. The trace excerpt does not explicitly label USB insertion and
ejection, so it cannot prove every symptom belongs to a particular hardware
notification. The source and private regressions reproduce the faulty diff.

The old `RefreshThisPcRows` independently rebuilt namespace selection/layout
and populated `CapturedRow::newIndex` only when regrouping or explicitly sorted.
In ordinary native order, all survivors kept `newIndex == -1`. The animation
code therefore considered them removed and created incoming rows again. That
path also cleared a complete empty snapshot immediately. In grouped Details,
structural changes with an unchanged Group by setting could reach the 1D row
transition, whose guard rejects grouped geometry.

## Shared data flow

The existing filesystem pipeline is factored into reusable stages:

1. `CaptureItemSnapshot` captures a bounded previous viewport, group headings,
   scroll anchor, selection feedback and layout dimensions.
2. `PrepareItemSnapshot` builds the identity index, fingerprints and old-to-new
   row matches. Filesystem jobs retain their cancellation-aware worker wrapper;
   the existing small namespace projection supplies its already-resolved RAM
   values. Incoming resolved metadata takes precedence over copied old cells.
3. `CommitItemSnapshot` publishes immutable model content and calls the existing
   `ApplyDirectoryNames` selection/layout/animation reconciliation. Namespace
   names completion does not finish an unrelated capacity request. Its names-only
   startup can also defer optional icon/property demand until actual first
   content, while filesystem startup keeps its existing enrichment policy.

This removes the parallel namespace structural commit. The existing compositor
row/grid/group animations consume the resulting diff without inspecting USB,
drive letters, notification kinds or a particular view location. Stable-rank
capacity enrichment and unchanged snapshots keep their existing fast paths and
do not restart motion. Namespace structural models are available before the
presentation lane starts and remain immutable while it advances independently.

Ordinary captions are insufficient for a membership fingerprint: devices may
have identical labels. `DirectoryOrderHash` now includes the existing folded
path identity hash, so equal-count/equal-label replacements are reconciled.
The shared capture includes headings for unchanged grouping specifications,
and the commit uses the 2D transition whenever grouped geometry requires it.

Drive/media hints now request a provider identity snapshot through the existing
coalesced native-rows queue. The backend's actual completion licenses the diff;
450/650-ms native-list guesses and their retry counter are removed. A new hint
during an in-flight request remains a follow-up intent. Failed revalidation
retains the accepted model, as directory refreshes do; it cannot remove unseen
items merely because a provider returned only a partial/error response. The
existing idle safety resample and background capacity work are retained.

## Validation

`tests/namespace-mutations-smoke.h` supplies backend `NativeRows` results through
the actual publication/delivery endpoint, never calling animation routines to
start insertion/removal. It covers all sixteen combinations of Details/grid,
grouped/ungrouped, LTR/RTL and native/explicit order. Assertions cover surviving
opacity/position, exactly removed ghosts, selection identity, contiguous lookup
indices, cold/partial/failed/unchanged/empty snapshots, equal-label replacements,
capacity independence and notifications requesting fresh identities before any
animation. One real independent GPU lane settles motion with no owner dispatch
and retains its device/swapchain. A private filesystem fixture also exercises
grouped Details insertion/removal and its exiting group header through real
asynchronous scans.

An initial 0.9.221 negative-control run failed with "surviving namespace items
were recreated as deleted ghosts". After adding the failed-provider guard
regression, that source also fails the earlier incomplete-provider assertion
(`tests/namespace-mutations-221.log`). The focused working-release tests, including
the existing cold native-startup readiness test, pass
(`tests/namespace-mutations-222.log`). These use synthetic namespace
identities and private files; no real drive is inserted/ejected and no user
selection, focus or clipboard is changed. Component/offscreen results do not
measure live Explorer display frame rates.

The first full gate caught optional demand starting before the first native
content (`tests/build-0.9.222-startup-failure.log`) and did not publish. The shared
commit now carries the caller's existing optional-demand permission explicitly;
the focused cold-start and mutation checks pass together. A subsequent complete
run hit a Recycle Bin F5 integration failure once
(`tests/build-0.9.222-recycle-f5-failure.log`). The same binary passed that
navigation suite in isolation (`tests/namespace-navigation-222.log`), and the
unchanged mod source passed the next full run with additional pre-key readiness/
modifier diagnostics. No cause or fix is claimed for that non-reproducing check.

`build.bat -SourceFile 0.9.222.cpp` passed the complete release gate and published
the tested source; see [`tests/build-0.9.222.log`](../tests/build-0.9.222.log).
This includes all 32 option contracts, the new snapshot cases, existing graphics,
animation, selection, clipboard, cache, startup and unload suites. All 21 actual
API-hook DLL unload cycles pass and critical callback stacks remain below 8 KiB
(maximum 1,960 bytes). All 25 previous source archives retain their recorded
hashes and latest.cpp matches the 0.9.222 archive. No live USB insertion/ejection
was performed.
