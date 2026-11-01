# Native catalog and rejected selection targets (0.9.315)

A read-only probe in a private ExplorerBrowser reproduced the reported Desktop
discrepancy. The folder enumerated 35 children while the native view contained
34. Two children had the Control Panel caption but different PIDLs. The view
accepted its own identity; `GetSelectionState` returned `ERROR_NOT_FOUND` for
the additional provider identity. `SelectAndPositionItems` nevertheless returned
`S_OK` while leaving the preceding selection intact. Canonical `CompareIDs`
reported that the two identities differ. Changing enumeration flags, validating
attributes, or calling `SHGetRealIDL` did not reconcile them.

Provider-backed folder snapshots now obtain their relative child PIDLs from
`IFolderView::Items(SVGIO_ALLVIEW)`. The non-agile enumerator stays on the view's
owner STA and is copied in bounded slices; the existing tab-owned names lane
binds providers and resolves names, metadata and layout. Ordinary filesystem
enumeration retains its existing fast path. Generations cancel obsolete capture
and publication. Authoritative empty catalogs stay empty; the existing Shell
enumeration-completed event and structural notifications drive refreshes.
No captions, namespace GUIDs or Desktop-specific selection rules are added.

Outgoing selections validate native membership before a destructive selection
transaction. Failed child conversions and rejected identities remove only their
own rows. A wholly rejected click retains the preceding verified selection;
large selections keep the native set intact during preparation while existing
pending-command guards remain active. Small and worker-prepared groups share
the same native commit and verification paths.

Validation includes private archives, delegated filesystem folders and ordinary
folders in Details/List/grid, bulk and external selection, native echoes and
context menus. A private-file removal exercises stale single, additive and bulk
selection. An intentional read-only OS probe discovers the Shell root through
Windows APIs and checks catalog equality and bidirectional virtual-item
selection in a private ExplorerBrowser. Focus and clipboard are substituted;
no user Explorer view or file verb is operated. These are offscreen integration
checks, not live Explorer interaction or displayed-frame-rate measurements.
