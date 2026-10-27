# Shell provider contents (0.9.246)

The GPU directory worker assumed that a filesystem path obtained from a
folder's navigation PIDL described its contents. That assumption fails for
Shell junctions and delegated folders: their namespace address can name a
physical wrapper while the bound `IShellFolder` enumerates different items.
`dir` and `FindFirstFileExW` enumerate the wrapper correctly, but that is not
the list Explorer displays.

A read-only probe of the reported folder found one visible physical
`target.lnk`, while its bound provider enumerated 54 target items. Its
`IPersistFolder2::GetCurFolder` and `IPersistFolder3::GetFolderTargetInfo`
both returned the wrapper identity. The provider's parsing name and its
children's filesystem paths identified the actual target. Converting the
navigation PIDL to a path therefore cannot resolve this discrepancy.

The names worker now binds the folder provider before choosing enumeration.
Only the standard filesystem provider with a matching backing path uses the
existing Win32 fast path. Other providers use the existing Shell enumeration,
preview, sorting and snapshot commit path. Filesystem-backed provider rows
retain real paths, cached find data, absolute PIDLs and relative child PIDLs.
Provider failures do not fall back to enumerating unrelated wrapper files.
Watch setup resolves the provider's backing path on its existing worker;
Shell notification filtering also accepts that observed target path.

The production change contains no network-location paths or provider-specific
shortcut handling. All provider binding, name
resolution and watch setup remains on tab-owned background lanes.

Microsoft documents provider enumeration through
[`IShellFolder::EnumObjects`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-enumobjects),
and the distinction between physical folders and Shell junctions in
[namespace extension locations](https://learn.microsoft.com/en-us/windows/win32/shell/nse-junction).

## Validation

`tests/shell-provider-smoke.h` creates a private folder shortcut outside known
folders, compares GPU paths with the bound provider, and checks file/folder
metadata, native command identities, selection retention, hidden items, sorted
refresh, an empty target, and actual kernel notifications from target changes.
The new assertion fails on 0.9.245 because it lists the physical wrapper.
The complete 0.9.246 release build passed repository, C++ and DLL-unload suites;
the published source, Windhawk export and archived version are byte-identical.
These are private component and Shell integration checks; they do not operate
the user's live Explorer view or measure displayed frame rates.
