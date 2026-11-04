# Review fixes for 1.0.3

All three reported issues exist in the published 1.0.2 source.

- Non-import native selection preparation now clears and verifies Explorer's
  previous selection before starting the worker. Importing a selection owned
  by Explorer keeps that selection intact. Clearing can reenter the owner, so
  the queued job and its caller recheck view/directory identity afterward.
  Failure uses the existing verified-GPU rollback/reconciliation paths; a
  restored GPU snapshot is not marked native-safe when Explorer is known empty.
  The early already-empty clear path now updates the same observation cache.
- Touch promotion resolves only the named user32 export. Missing exports use
  the existing failed-promotion cancellation path; no ordinal is called.
- Both dispatch admission checks read the resolved-unsupported marker from the
  selected tab, matching ScanShellWindow's writer and tabless frame behavior.

Updated existing selection coverage to require empty native command targets
while preparation is pending and refusal to queue when clearing fails. The
real private-folder provider case checks the native selection count directly.
Discovery coverage now includes both tabless frames and separate tab hosts.

Additional review fixes correct the two-finger navigation setting description,
retain the bound IShellItem for SIGDN_FILESYSPATH lookup, and reset shaped-layout
paint stamps along with the released target's counters. The provider lookup no
longer passes a null child PIDL to GetDisplayNameOf/StrRetToStrW. Watch fallback
paths and filesystem-provider classification use the same item-owned lookup.
The alternate index-sort algorithms were used only by benchmarks; they now
live in the benchmark header and share the production permutation helper.
The working C++ source is saved with CRLF line endings for upstream review.

Ordinary activation now calls IFolderView2::InvokeVerbOnSelection(nullptr)
for files, filesystem/provider folders, This PC items and Recycle Bin items.
Removed direct folder PIDL parsing/BrowseObject, the Recycle Bin ShellExecute
branch and guessed eager navigation previews. Explicit new-tab activation keeps
its separate user-requested disposition. Activation reuses verified first-click
selection, waits for asynchronous selection completion and rechecks
owner/view/directory/item identity after
reentrant calls. Existing Shell navigation notifications drive previews.
The earlier unification still manually queried and invoked IContextMenu, which
the affected-PC report confirms retained the discrepancy for double-click and
Enter. The remaining manual default-command branch and its supplied working
directory have now been removed. Explorer's view owns the launch parameters;
there is no manual menu fallback or synthetic keyboard-state change.
Extended native view-API capture coverage to ordinary filesystem folders
and files and asynchronous selected-group activation. Captured test
commands do not launch associated apps or navigate real user windows.

Default activation also rechecks the captured selection storage and directory
generation after the Shell window lookup. Reentrant selection replacement,
directory changes and selection recovery cancel activation. Existing command
coverage now exercises selection replacement and directory changes during that
COM call without launching files.

Validation caught a delegated-folder watch regression: SIGDN_FILESYSPATH on the
alias item names the alias, not its displayed target. A private Shell probe
confirmed BHID_LinkTargetItem resolves the target; the worker now uses that
documented handler before requesting its filesystem path. The modifier capture
uses the existing substituted keyboard API, and the native touch-history case
runs the real AttachView rebind before asserting confirmed travel history.

The native return check also exposed a repeated-announcement race: binary
differences between canonically equal folder PIDLs discarded already-positioned
rows. Announcement and confirmation now ask Shell for canonical identity when
bytes differ, retaining conservative reset on mismatch or comparison failure.
Both calls revalidate tab-owned state after COM reentrancy. The real travel-log
case independently compares native Shell items and requires immediate row,
order and viewport preservation at confirmation and through refresh.
