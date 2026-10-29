# Native context menu investigation: 0.9.287–0.9.288

The original folder-level `GetUIObjectOf`/`TrackPopupMenuEx` implementation
bypassed Explorer's modern presenter and omitted native view commands.
0.9.287 replaced that with `IContextMenuSite::DoContextMenuPopup`, but the
native implementation rejects an invisible view. The mod deliberately hides
the native content host. The presentation-intercepting regression did not
exercise that rejection and incorrectly passed.

Calling later native selection/background callbacks is also insufficient:
it skips the input-origin setup that distinguishes mouse invocation from
keyboard invocation. On the investigated Windows 11 build this produced
keyboard keytips for mouse clicks.

Matching Microsoft public symbols for shell32 10.0.26100.9278 show that
`CDefView::_OnContextMenu` first validates a mouse point against its native
list window, records the origin, and then enters the native list control's
context-menu operation. The keyboard sentinel skips the geometry check.
The mod's zero-size native viewport therefore allowed keyboard menus while
rejecting real mouse points. A hidden dedicated host could also miss the
layout that normally sizes its Shell view. This is shared presentation/input
state, rather than a This PC menu-provider exception.

0.9.288 lets Windows' default window procedure generate `WM_CONTEXTMENU` and
forwards that request to the active native Shell view's complete handler.
Screen coordinates and the keyboard sentinel remain distinct. Explorer owns
menu selection, presentation, extension callbacks, Show more options and
command execution. The mod keeps its committed selection and no longer
performs an extra menu-only selection transaction. This PC uses the same
verified selection path as other supported locations.

The native content host stays hidden before layout. Native presentation
children retain real geometry; an unlaid-out direct child recovers geometry
from its actual parent. This uses existing owner-thread lifecycle/layout
events, with no menu-specific namespace branches, synthetic keys, private COM
interfaces, binary offsets, menu reconstruction or timed presentation tricks.
Retaining native layout also retains its native row-realization costs; the
offscreen viewport probe measures those costs separately from displayed FPS.

Validation boundaries:

- Selection/navigation/gesture component tests observe routing only. Their
  intercepted call counts are not evidence that a popup appeared.
- `--native-context-menu` runs real Shell menu loops on a private desktop,
  without switching desktops or changing the user's focus/clipboard. It
  requires a visible menu window, a real menu-loop notification, nonempty
  displayed commands and successful dismissal. Across directories, ZIPs and
  delegated folders, it compares item/folder/mixed/background popup roots and
  states against actual visible-native reference popups in Details/List/Icon.
  Lazy unopened submenu contents are not claimed as tested.
- A negative control restored the old popup-site bypass in an isolated source
  copy. Its visible reference opened, but the suppressed invocation displayed
  zero popups and the regression failed. The 0.9.288 full release gate passed.
- An isolated This PC ExplorerBrowser had a positive count but could not
  retrieve even its first native item with suppression disabled. That was an
  invalid reference fixture, not evidence of a passing namespace interaction.
  Real Explorer was used for the mouse check instead.
- Live Explorer checks on 2026-10-29 used actual mouse input with the loaded
  candidate: This PC drive and background menus, expanded Group by, and a
  private file's modern/classic menus. The file's displayed modern commands
  and classic Rename entry matched the native-view comparison. Mouse menus
  had no keyboard keytips. A real Menu-key press on the GPU-selected private
  folder opened the native keyboard menu with keytips. These checks do not
  establish behavior for every third-party namespace or extension.

Windows documents the input contract in
[WM_CONTEXTMENU](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu).
The legacy popup-site method is documented as
[unavailable since Windows Server 2003](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenusite-docontextmenupopup).
