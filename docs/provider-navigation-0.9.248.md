# Provider navigation investigation (0.9.248)

The supplied trace explicitly logs `DeactivateOutsideScope` and
`native fallback: unsupported Shell view` at 10:08:40.342. That explains the
disappearing switch; the trace does not establish a crash.

Three filesystem assumptions remained after the selection fix:

- Activation interpreted `SFGAO_FOLDER` as a physical directory. Provider
  items can be containers, streams, or both. Their opaque row identities
  failed the path gate, so folder activation returned without a command.
  See Microsoft's [Shell attributes](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/shell/sfgao.md).
- Default commands received the archive file as their working directory.
- Folder discovery and navigation-complete events rejected a folder when
  its PIDL could not convert to a filesystem path. Nested provider folders
  therefore lost scope even when native Explorer could browse them.

Provider snapshots now use the active native view's selected-item default
command for files and folders. No extension, archive class, forced verb or
extraction implementation is introduced. Filesystem-descended namespace
folders retain an opaque PIDL key. Names, selection, column catalogs, sort
verification and Shell watches carry the immutable folder PIDL on existing
tab-owned lanes. The key never becomes a filesystem command target or watch
path. Existing unrelated virtual-root scope remains unchanged.

The new offscreen ExplorerBrowser test resolves real default menus while
capturing invocation, and browses real private provider folders by PIDL. It
covers child archives, three container levels (private materialized copies),
deep/empty directories, Unicode, return navigation, delegated filesystem
providers, refresh, column catalogs, PIDL watches, native/GPU toggles and stale
selection cancellation. Associated applications and real extraction verbs
are not launched. These integration checks do not establish live Explorer
end-to-end activation or visual behavior.

The expanded notification test initially failed because the private STATIC
window only intercepted queued messages. Routing notifications through its
actual window procedure fixed the test. The full release gate was rerun;
the failed run did not publish a source version.
