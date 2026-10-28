# Touch input

Touchscreen contacts previously entered the mouse selection path: only
precision touchpads were offered to DirectManipulation. An unselected file or
background contact could therefore arm marquee selection and capture the mouse
before Windows recognized the pan.

The overlay now configures single-finger pan, guttering, inertia and pinch via
`SetGestureConfig` on `WM_GESTURENOTIFY`. It leaves contact recognition, double
tap pairing and press-and-hold promotion to Windows. Content mouse presses
promoted from touch are deferred without capture; completed taps use the
existing selection/default-command path. Native right-button and context-menu
promotion use the existing Shell menu path. Header and scrollbar contacts keep
direct manipulation of their controls. Pen/mouse selection remains available.

Touchscreens and touchpads share pan intent, scroll offsets, history previews
and release commits. Gesture instance and folder epochs reject canceled input
and stale history commands. Native `BeginPanningFeedback`,
`UpdatePanningFeedback` and `EndPanningFeedback` supply boundary resistance and
rebound. The overpan accumulator unwinds before scrolling back into bounds.
Navigation, tab hiding, cancellation, fallback and unload end feedback.

Touch pinch uses consecutive finger-distance ratios and fractional metrics on
the shared list/grid geometry. It anchors the touched item rather than pulling
the viewport toward a distant selection. Touchpad/wheel zoom keeps its existing
eased transition. Scrollbar breadth uses current DPI, native scrollbar metrics
and Windows' documented 40 epx touch target. Enlargement overlays inward, using
the existing animation clock, without changing column/grid layout. Physical
mouse, pen or touchpad input reverses the transition; no idle timer is used.

References:

- [Windows gesture configuration](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setgestureconfig)
- [Gesture messages and handle ownership](https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-gesture)
- [Input source identification](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcurrentinputmessagesource)
- [Windows panning feedback](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-updatepanningfeedback)
- [Touch target guidance](https://learn.microsoft.com/en-us/windows/apps/develop/input/touch-interactions)

The `--touch` suite replays the Windows message contract in private fixtures,
including real Shell selection/default-menu preparation with command invocation
and popup display intercepted. It does not inject hardware input or validate
the digitizer/driver's recognition, live Explorer rebound appearance, or actual
displayed frame rates. On-device comparison with the native view is still
required to establish perceptual 1:1 parity.
