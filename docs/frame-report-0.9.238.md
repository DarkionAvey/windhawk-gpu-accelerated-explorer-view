# Frame messages, rename input and dispatch-state lifetime (0.9.238)

All four findings reconcile to the published 0.9.237 code. Their old line
numbers were not used to locate the implementation.

## Decisions

1. **Registered frame-hook release message: addressed.** `WM_APP + 73` was sent
   to Explorer frames. Its numeric value could collide with Explorer or another
   subclass's private protocol. `Wh_ModInit` now registers
   `WH_MOD_ID + "-release-frame-hooks"` and fails initialization if registration
   fails. Receivers require a nonzero matching message and unloading state.
   Remaining `WM_APP` messages target the mod's own overlay window class.

2. **Accelerator caller's message: addressed.** Native rename adoption used to
   overwrite the caller's `MSG` and return false. The accelerator path now
   translates and dispatches the routed copy, then returns true. This preserves
   Explorer's message and tells its loop not to translate/dispatch it again.
   The ordinary dispatch path still routes its own copy.

3. **Frame-hook cleanup loop: addressed with a lifetime qualification.** A
   successful send that did not remove a record could spin; a missing frame
   subclass had no handler for the release request. Each shared frame subscription
   now owns a message-only STATIC window with its own cleanup subclass and a
   real handle to the installer thread. Cleanup reaches that window even when
   frame subclass installation fails, another component removes the frame
   subclass, or the Explorer frame is destroyed without notifying it. Hook removal
   and cleanup-window destruction run on the installer STA. Normal last-owner
   release also destroys the cleanup window and closes the thread handle.

   Exited installer threads are recognized by the signaled handle; USER has
   automatically removed their WinEvent hooks/windows, so stale registry entries
   can be discarded. Empty records with no live callbacks are also removed.
   A failed or ineffective send sleeps before retrying and logs once.

   Blindly erasing live WinEvent hooks on the unload thread is unsafe:
   `UnhookWinEvent` must run on the installing thread. A genuinely hung live
   installer therefore still delays unload until it handles cleanup or exits.
   A fixed timeout followed by unloading executable callbacks would be a use
   after unload, so the report's claim that unload should become impossible to
   block is not adopted literally.

4. **Permanent dispatch-state polling: addressed.** A one-shot registered wait
   watches each retained real thread handle. Its callback only signals the
   existing regulator event; the regulator reclaims exited-thread maps off the
   UI thread. It waits indefinitely when no stopping tab group needs polling.
   Registration failure leaves no state/watch behind. Explicit unload unregisters
   and drains each wait before closing watched handles or the regulator event.
   There is no TLS destructor or teardown API in a process-exit destructor.

## Checks

Private offscreen fixtures cover:

- Registered message identity/range and an unrelated `WM_APP + 73` message that
  must not release the subscription during unloading.
- Actual WinEvent removal on the installing STA for live frames, failed frame
  subclass installation and a destroyed frame with its subclass removed.
- A terminated installer with deliberately removed window subclasses, exercising
  USER's automatic hook cleanup and stale registry retirement.
- Reclamation of 128 exited-thread caches while retaining a live owner's data,
  idle waits beyond the former one-second interval, and failed wait registration.
- Native rename adoption with an unchanged caller `MSG`, one translated
  `WM_CHAR`, correct visible editor text/caret, and existing commit/rollback checks
  in LTR/RTL Details/grid layouts.
- Actual DLL unload before dispatch-owner thread exit, so a cancelled exit wait
  cannot call back into an unmapped DLL; process exit also has a pending exit
  wait while provider work is abandoned.

These are component/OS integration checks, not live Explorer interaction.
The focused `--frame-report` suite passed, followed by the full
`build.bat -SourceFile 0.9.238.cpp` release gate. Repository checks, the linked
Windhawk DLL, full C++/graphics/input checks, process exit and all 21 actual DLL
unloads passed. Logs are `tests/frame-report-0.9.238.log` and
`tests/build-0.9.238.log`. Published source, Windhawk export and archived version
have identical SHA-256 hashes. All 41 prior archives retain their original bytes.

## API contracts

- [WM_APP ranges](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-app)
  and [RegisterWindowMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerwindowmessagew).
- [TranslateAcceleratorW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translateacceleratorw)
  documents that a consumed message must not be translated again.
- [UnhookWinEvent](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unhookwinevent)
  requires the installing thread and documents automatic removal when it ends.
- [RegisterWaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-registerwaitforsingleobject)
  requires cancellation even for one-shot waits and an open watched handle.
