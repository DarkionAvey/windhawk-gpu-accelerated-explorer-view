# Recycle Bin detachment and selection recovery

The supplied 21:41:57–21:42:12 trace captures two deliberate native fallbacks,
not an Explorer process exit. At 21:41:58.247 an import reports zero matching
rows for one selected identity. At 21:42:09.329 the native selection commit
reports `wanted=2 verified=1 fallback=1`. At 21:42:09.590, after a completed
811-row refresh, the import reports:

```
native import rejected (selection not found after refresh): observed=0 expected=2; revealing Explorer
```

Native viewport/visibility restoration follows at 21:42:09.599–.600. Submitted
frames immediately before that have successful EndDraw and Present results.
The log identifies the rejection branch; it does not include the raw selected
PIDLs, so the exact differing encoding in that run cannot be reconstructed.
The later report of deletion detachment has no separate trace attached.

## Identity correction

The old namespace cache key serializes every byte of an absolute PIDL. Such a
key is useful for indexing one snapshot but cannot establish canonical equality
between independently obtained Shell identities. Windows documents the use of
[IShellFolder::CompareIDs](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-compareids)
for canonical item comparison.

Recycle Bin selections now reuse the existing bounded relative-PIDL capture on
the owner STA and the tab-owned selection worker. The names worker publishes
complete relative children beside the existing opaque keys; cached returns keep
the same immutable index. Exact byte matches stay cheap. Canonical comparison
and focus matching occur on the worker's own folder binding, with cancellation
checks. Neither displayed names nor original file paths decide which duplicate
is selected. A changed publication causes a retry. A completed names refresh can
re-resolve an older selection against the fresh index.

Private tests use the documented filesystem bind-data API to produce different
PIDL bytes for the same private file and verify canonical equality. They then
exercise real Ctrl+wheel handlers at display sizes above 1,000 pixels, bounded
512-pixel image requests, repeated selection echoes, duplicate displayed names,
cached returns, stale publications and cancellation. They do not alter the real
Recycle Bin.

## Recovery instead of detachment

Every selection import rejection now cancels obsolete prepared commands and
queues recovery while retaining the GPU surface and directory rows. Unsafe
selections cannot license Delete, Cut, Copy or item menus. The existing owner
continuation and maintenance retry paths service recovery; elapsed time never
certifies completion.

Recovery waits while a native command call, rename, OLE drag/drop, disabled
modal owner, directory job, watcher batch or layout transition is active. Native
commands and dispatched/accelerated command input have per-tab lifetime scopes
that tolerate reentrant navigation and destruction. Shell-owned asynchronous
filesystem changes continue through the existing notification/enumeration paths;
the mod does not replay a verb or infer its completion from a fixed timeout.

When ready, the existing native deselection function verifies a zero selection
before GPU highlights are cleared. If Explorer temporarily refuses, the reset
stays pending and file commands remain blocked. A new verified user selection
supersedes the reset. Each COM callback validates the owner/view generation so a
late reset cannot clear a replacement view. Refresh failures retain the last
usable rows and allow later refreshes.

The selection tests simulate a real Delete-verb invocation using a substituted
context menu, publish private deletion/Undo snapshots, withhold command and
watcher completion, repeatedly refuse deselection, and navigate during a reset.
No real file command, clipboard mutation or user focus change is performed.
These are offscreen component checks, not live Explorer validation.
