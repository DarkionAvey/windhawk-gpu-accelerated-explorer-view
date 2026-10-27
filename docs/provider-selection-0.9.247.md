# Provider selection identity (0.9.247)

0.9.246 enabled provider enumeration for filesystem-addressed Shell folders,
including archive roots, whose members have no physical filesystem paths.
Enumeration retained provider PIDLs and used an opaque key for rows without a
filesystem path, but selection import still treated only the Recycle Bin as a
provider namespace.

A private ZIP hosted in a real offscreen ExplorerBrowser reproduced the bug.
The native view accepted the GPU's relative child PIDL and retained one
selected item; the outbound transaction verified successfully. The incoming
selection echo failed: `SIGDN_FILESYSPATH` is unavailable for archive members,
and the fallback import produced a parsing name instead of the enumeration's
opaque row key. The unmatched-item refresh/recovery then removed selection.
Context menus were blocked by the same unverified-selection guard.

The expanded native test also found that the archive view reports a selected
count of zero after deselection while obtaining an empty item array can fail.
An authoritative zero count now imports an empty selection directly, without
asking the provider to construct an array containing no identities.

The shared selection path now chooses identity resolution from the immutable
provider row snapshot, independent of location name or file extension. Native
selected/focused relative PIDLs are captured on their owner STA and mapped to
the snapshot's exact keys on the existing tab-owned selection worker. Both
real-path and opaque-key rows use the same mapping, with canonical provider
comparison for alternate PIDL encodings. Snapshot replacement and refresh
guards apply to every provider snapshot. A successful return to ordinary
filesystem enumeration clears the old provider identity map; failed refreshes
retain the committed map.

No ZIP paths, extensions, CLSIDs or archive-specific selection/menu code are
added to production. The existing outbound verification, command guards and
context-menu implementation remain in use.

Microsoft documents that
[`SIGDN_FILESYSPATH`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-sigdn)
is available only for filesystem items, and that
[`CompareIDs(SHCIDS_CANONICALONLY)`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-compareids)
compares provider identities rather than display names.

## Validation

The provider regression uses a generated private ZIP, a delegated filesystem
folder and an ordinary directory, each with 99 items in offscreen native
ExplorerBrowser views. It checks Details/List/grid, repeated selection echoes,
native-initiated selection, focus, Unicode names, folders, 70-item groups and
menus deferred during import. Popup tracking is intercepted: the real menu is
constructed, but no popup is displayed and no verb is invoked. Separate filesystem-addressed
provider checks exercise mixed real-path/opaque keys, canonically equal but
binary-different selected/focused PIDLs, duplicate labels, stale snapshots and
cancellation. Clipboard/focus APIs remain substituted. These checks do not
operate the user's Explorer windows or user files.

The complete 0.9.247 release gate passed repository regressions, the C++ suite
and DLL-unload tests. Published source, Windhawk export and archived version
are byte-identical; retained historical sources were unchanged.
