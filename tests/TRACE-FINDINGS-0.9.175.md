# Gesture-navigation native loading placeholder

Input: the user's capture beginning 18:54:49, attachment
`69b6aab9-0b1d-4f9e-9f68-72c2b21d39f5`, while alternating between This PC
and F:\ through a third-party mouse gesture application.

## Evidence and boundary

The capture has no `presentation.visibility` request hiding the GPU. Geometry
retry events explicitly report `kept=1`. This is distinct from 0.9.173's GPU
visibility gap. The user identifies the flashed text as the native "Working on
it" placeholder; the trace itself does not record that text or its pixels.

Native suppression repeatedly acquires a SHELLDLL_DefView first, then its new
CtrlNotifySink parent later. For example, the records at 18:54:52.613 and
18:54:52.690 hide native HWND 0xA60BB8 and host 0x590BFA respectively.
The producer trace also shows long native 0x700/0x4AE dispatches during those
handoffs (up to roughly 177/76 ms). Wall-clock writer spacing does not measure
the exact interval for which a native surface was exposed.

The existing source uses WINEVENT_OUTOFCONTEXT for lifecycle notifications.
Those callbacks are queued: they cannot serve as a synchronous presentation
barrier while Explorer creates/reparents a new view inside a native dispatch.
Toolbar/key navigation can already have established an optimistic GPU handoff;
an external gesture need not pass through those input prediction paths.
Suppression must work independently of the navigation trigger.

Microsoft documents the timing distinction in
[SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook):
out-of-context delivery is queued; in-context delivery invokes the callback as
events occur. In-context callbacks require the callback's own module handle
and must handle reentrancy. This mod restricts them to its process and the
Explorer frame's owner thread.

## Change

0.9.175 adds one small synchronous presentation subscription to the existing
per-frame, reference-counted hook inventory. It handles native CREATE, SHOW
and PARENTCHANGE through the same suppression helper as the queued fallback.
It recognizes only SHELLDLL_DefView, its dedicated CtrlNotifySink parent and
DirectUIHWND/SysListView32 directly inside that view. A matching shared chrome
class by itself is not sufficient. Only the active, supported GPU tab may
provisionally acquire an incoming surface.

The helper hides the dedicated host and constrains its owned presentation
viewport using the existing ownership, visibility-intent and restoration paths.
Reparent events close the gap between an initially unwrapped view and its final
host. No file/provider call, navigation discovery, GPU paint, new worker or
timer runs in the synchronous callback. Normal Shell/tab/selection work stays
on the original queued subscription.

Explicit native restoration uses the existing internal-visibility guard so it
cannot reacquire hosts while deliberately showing them. User native mode,
unsupported scope/layouts, inactive tabs and unrelated controls remain exempt.
Every added event handle retires with the last overlay in its frame and through
the emergency inventory cleanup. Callback tracking and owner-thread state
retirement remain those established by 0.9.172.

## Validation

Preserved 0.9.174 fails the new regression with "new native loading placeholder
escaped before queued lifecycle delivery". The final 0.9.175 focused navigation
run passes twelve unpumped creation/reparent cycles, including a loading-label
child, immediate native viewport constraint, unchanged GPU model/presentation,
and native/unsupported/inactive/chrome/focus guards. This validates visibility
before queued events; it does not reproduce the user's external gesture app.

The DLL fixture loads the actual selected source as a DLL, installs its real
synchronous event subscription, verifies immediate suppression, destroys the
owner overlay, verifies host restoration/hook retirement, and confirms actual
image unmapping after unload. This also passed. An initial fixture omitted its
ViewState HWND initialization; that fixture error was corrected before passing.
The final Windhawk DLL build and full regression suite passed, including the
prior input-starvation, pointer, selection and navigation checks and 18 API
return/unload cases, as recorded in AGENTS.md and README.
Live Explorer appearance still needs user confirmation. No localized loading
text is matched or filtered by production code.

Preserved 0.9.174 SHA256:
707D18AEBD999822FD98F51329056FBDF5FA5773BA1A9435D834B23870B9C3C8.
