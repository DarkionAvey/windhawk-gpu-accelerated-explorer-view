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

No build, runtime tests or publication were performed. Published 1.0.2 and its
archive remain unchanged; the development source is 1.0.3.cpp.
