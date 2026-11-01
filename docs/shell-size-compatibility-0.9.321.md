# Shell Size text compatibility

The overlay previously used `StrFormatByteSizeW` on Win32 file lengths and
left folder sizes blank. This bypassed the Shell property and formatting
hooks used by [Better file sizes in Explorer details, version 1.6](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/explorer-details-better-file-sizes.wh.cpp).
That mod changes `CFSFolder::_GetSize` to return a `System.Size` value for
folders, and hooks Shell/property-system formatting for its unit settings.
It does not submit a separate public stream of completed display strings.

The small integration is to request the Size column's formatted text using
[`IShellFolder2::GetDetailsOf`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder2-getdetailsof).
The column is located by `MapColumnToSCID` and `PKEY_Size`, without assuming a
column number or localized title. `SHBindToParent` binds a worker-local folder
from the row's absolute PIDL or parsed path. All COM objects are released in
the same apartment. This uses the public Shell path reached by the other
mod's hooks, rather than reading its private cache or adding symbol hooks.

Requests run only for demanded Size cells on the existing tab-owned detail
pool. Results retain its normal cancellation, column epoch, caching and
refresh handling. Empty Shell text is a completed value, so ordinary folders
remain blank. If the Shell request fails, the previous file-size formatter
or namespace property-store path remains the fallback. Without the other
mod, successful queries now follow Windows' Size column formatting.

This is a fresh Shell request, not passive interception of the hidden native
view's cache: it can invoke calculations/Everything queries owned by the
other mod. Those calls stay off the owner thread and use the existing worker
drain on unload. Its Shift-dependent mode observes Shift at query time.
Refresh after enabling/disabling or changing the other mod; no polling for
its settings is added. This change supplies display text only. The overlay's
raw file sizes, size sorting, grouping, and folder placement are unchanged.

`--shell-size` exercises the real Windows Shell and asynchronous detail pool
with private zero-byte/large-file/folder fixtures, covering both path and PIDL
requests and authoritative blank values. It does not load the third-party
mod, and does not establish live Explorer compatibility with every setting
or Windows version. That still requires an Explorer run with both mods.

Validation: `build.bat -SourceFile 0.9.321.cpp` passed the repository checks,
normal/minified DLL builds, full C++ regressions (including this Shell test),
and DLL-unload suite, then finalized the repository release. The initial
sandbox run reached the Shell assertions but failed its filesystem cleanup
boundary check; the full successful run used normal Windows access.
