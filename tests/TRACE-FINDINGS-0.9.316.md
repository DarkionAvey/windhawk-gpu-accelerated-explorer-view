# Right-only context-menu selection

The supplied trace keeps the selected item verified throughout each native
selection transaction. Left-click transactions retain the native list's focus
host (`0D70F20`); the right-only transactions retain `11F1480`, identified by
the focus diagnostics as `InputSiteWindowClass`. Imports then echo the same
selected identities. This points to feedback ownership rather than selection
being cleared and restored.

`Select` publishes a presentation model before entering Shell selection calls.
The mouse context-menu handler previously called it without returning focus to
the list, unlike left-click. A model captured while the previous menu's input
host owns focus can suppress selection feedback until the next popup binds.

The mouse handler now uses the shared list-focus return helper before selecting
or clearing items. It prefers native focus and falls back to the GPU window when
the hidden native host rejects focus, with lifetime/navigation checks after the
reentrant call. Keyboard requests retain their existing focus behavior. Item and background menus, and
touch gestures that use the mouse menu route, share the correction.

The private-desktop native menu integration checks use Windows' default
right-release handler to generate the context-menu request. They start from a
different selected item and another focus owner, and observe list focus plus
presentation feedback before the real native popup handler runs. They cover
files/folders in Details, List and grid views for disk, ZIP and delegated folders.
Focus/foreground APIs are substituted because the non-input desktop has no
foreground window; both accepted native focus and rejected-native GPU fallback
are exercised without changing the user's focus.
These are isolated integration checks, not a live Explorer visual verification.

The full-suite background-tab failure was a readiness race in its fixture.
`ReceiveDirectoryResults` installs an asynchronously created watch, then queues
a scan to close the interval between initial enumeration and watch installation.
The test captured its baseline generation before that watch-covered scan. Under
full-suite scheduling, the watch result arrived during background-tab preparation
and advanced the current tab's generation as designed. The fixture now requires
an installed watch and a completed snapshot with the same change stamp before
capturing that baseline, consistent with the existing history and filesystem
mutation checks. The generation-isolation assertion remains unchanged.

Validation: `build.bat -SourceFile 0.9.316.cpp` passed the complete repository,
C++ and DLL-unload suites, including all 36 real-popup checks and the full-suite
background-tab check. The build finalized 0.9.316 and archived the working source.
