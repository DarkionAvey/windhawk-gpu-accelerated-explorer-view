# Touch input correction, 0.9.265

The 0.9.264 path deferred touch-promoted mouse presses while waiting for
`WM_GESTURE`. It did not consume `WM_POINTER` frames directly and had no
hold-to-drag handoff. Its tests supplied completed gesture messages, so they
could not establish that Windows would deliver those gestures. The user's
device report demonstrated the resulting dead input path.

## Windows input boundary

The overlay now consumes the touch pointer stream consistently, reads
`GetPointerFrameInfoHistory` while the message is current, and feeds complete
frames oldest first into a tab-owned Windows Interaction Context. Duplicate
per-contact frame notifications are ignored. Bounded catch-up retains the
newest complete history. Pen/mouse and precision-touchpad handling retain their
existing paths. Pointer/recognizer acquisition failure restores native view
visibility and logs the failing API rather than leaving an inputless overlay.

Native recognition supplies tap pairing, holding thresholds, translation,
scaling and inertia. Outputs are bounded and drained after the recognizer
returns; callbacks never enter modal Shell operations. Inertia uses the
existing compositor-paced owner pulse. Native panning feedback supplies
boundary resistance/rebound. Fractional pinch metrics and item-relative
anchoring use the shared grouped/list/grid geometry. Headers and scrollbars
use the same control/capture functions as mouse input, with pointer coordinates
instead of polling the physical mouse cursor.

## Hold and Shell dragging

The native hold delay becomes queryable after the first calibrated contact.
An owner-window deadline uses that reported value and the contact's QPC time.
At the deadline a stationary update enters the real recognizer. An early
timer is rearmed for its remaining QPC duration, without empirical polling.

Holding an unselected item selects it. A held release uses the existing Shell
menu; held movement posts a drag request after native selection is safe.
`ConvertPrimaryPointerToMouseDrag` promotes exactly one primary contact before
the existing `SHDoDragDrop` path. Folder/contact generations, identity and
selection checks run again before handoff. Release, a second finger,
navigation, capture loss and tab hiding revoke queued work. New contacts
cancel deferred context menus. Each tab owns its context; destruction stops
recognition and destroys its native callback registration before client data
or DLL code can disappear.

## Evidence and limits

The development host reports no physical digitizer (`SM_DIGITIZER=0`,
`SM_MAXIMUMTOUCHES=0`), and uses Windows Ninput 10.0.26100.1. The component suite
creates a private synthetic digitizer for genuine device calibration; it never
injects input or changes the user's focus/clipboard. Calibrated contact frames
enter the production native API boundary and the real recognizer. Windows
input messages and gesture outputs are not manufactured. Desktop feedback and
modal OLE effects are intercepted, while Shell selection/data-object identities
are checked against private temporary files.

Coverage includes tap/default activation, real deadline-driven holds, held
release/menu and held movement/drag, release during bulk-selection preparation,
pan/overscroll reversal, coalesced frames, grouped/ungrouped list/grid pinch
anchoring at 96/144/192 DPI, RTL history direction, control capture, independent
tab contexts and native inertia. An actual DLL unload test starts a native
recognizer and arms its hold deadline, then verifies retirement and DLL
unmapping with native visibility restored.

These checks do not establish OS message delivery from a touchscreen driver,
actual modal touch dragging, displayed frame rates, or perceptual 1:1 parity.
Those require comparison with native Explorer on a physical touch device.

## Platform references

- [WM_POINTERDOWN and consistent stream ownership](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerdown)
- [Pointer frame history and message lifetime](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerframeinfohistory)
- [Windows Interaction Context](https://learn.microsoft.com/en-us/windows/win32/api/interactioncontext/)
- [Native hold parameters](https://learn.microsoft.com/en-us/windows/win32/api/interactioncontext/nf-interactioncontext-getholdparameterinteractioncontext)
- [Native inertia processing](https://learn.microsoft.com/en-us/windows/win32/api/interactioncontext/nf-interactioncontext-processinertiainteractioncontext)
- [Primary pointer promotion for OLE dragging](https://learn.microsoft.com/en-us/windows/win32/winmsg/winuser/nf-winuser-convertprimarypointertomousedrag)
