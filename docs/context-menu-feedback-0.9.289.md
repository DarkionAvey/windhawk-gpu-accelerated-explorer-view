# Selection feedback during native menus

The supplied 0.9.288 trace retains the same selected PIDL before and after the
WinUI handoff, while focus moves to a separate `InputSiteWindowClass`. Live
observation reproduced the missing background. The native menu returns from
`WM_CONTEXTMENU` while its owned popup remains visible; Windows reports no
Win32 menu-loop flags for that WinUI popup. Classic menu-loop lifetime therefore
cannot represent both presenters.

0.9.289 separates selection feedback from keyboard input ownership. It binds
feedback to the native invocation's folder generation and delegated focus host,
then requires a visible, enabled popup owned by the foreground Explorer frame
on its owner thread. Existing focus and popup visibility events refresh that
state. A popup replacement gap preserves the delegated focus binding. Visible
popup discovery handles older hidden enabled popups, and runs only on native
invocation/focus/visibility notifications; painting validates the cached HWND.
Keyboard focus, caret, hover and command routing keep their existing
ownership rules. Menu invocation and selection are unchanged.

GPU pixel checks cover Details/List/Icon in LTR/RTL, real USER popup show/hide
and destruction, successive popup replacement (including hidden enabled
predecessors), unrelated popups and focus, modal/background frames and stale
folder generations. Their focus APIs are substituted and do not affect the
user. The immutable presentation model carries selection feedback separately
from input focus. A negative control reinstating the old input-focus-only
rendering rule fails at the selected-pixel comparison. The existing 36 real
Shell popup comparisons pass independently; they exercise classic popups on a
private desktop, not live WinUI presentation.

Live Explorer verification on 2026-10-29 used the loaded candidate and actual
mouse input on a private file and folder. Their selected backgrounds stayed
visible after the WinUI popup settled with focus on the separate input host, through Show
more options, and after dismissal back to the selected row. Address-bar focus
and its autocomplete popup still hid list feedback while preserving selection.

Subsequent consecutive-item mouse testing exposed a later asynchronous focus
handoff that this version did not cover. See the
[0.9.290 investigation](context-menu-feedback-0.9.290.md).

The popup relationship uses the documented
[GetWindow / GW_ENABLEDPOPUP](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindow)
contract. No WinUI class names, menu items, artificial keys, visibility timeout
or provider-specific menu code is used by the fix.
