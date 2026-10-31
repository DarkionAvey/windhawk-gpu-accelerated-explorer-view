# Additional input gestures

The supplied Explorer log shows a middle-button press at 14:54:00.676,
followed by another input handler injecting a selection click and keyboard
shortcut. The injected left press enters the normal selection handler,
which cancels the pending middle-click identity. The middle release therefore
cannot reach its capture-release or tab-command branch. Later button-free
mouse moves still report capture, including coordinates outside the GPU view.
Explorer continues dispatching; capture redirects clicks away from its controls.

Folder middle-clicks now dispatch on press and never acquire capture or retain
a pending mouse gesture. Native selection preparation and tab-command invocation
remain on the existing paths. Another input handler can still assign its own
action to the same middle-click; this mod does not change that mod's settings.

A button-free mouse move releases orphaned GPU capture only when Windows also
reports all physical mouse buttons up. It leaves active touch/OLE operations and
other controls' capture alone. Double-click pairing distinguishes injected,
mouse and replayed touch sources as well as item identity and folder generation.
The three gestures have independent, default-enabled settings.

A native Ninput comparison also found that enabling multi-finger taps on the
existing manipulation context suppresses small pinch updates as candidate taps.
Tap detection therefore uses a tap-only context in the same tab-owned recognizer,
sharing calibrated packets, bounded output, cancellation and teardown. The
existing manipulation configuration stays intact; a recognized pan or pinch
excludes a tap menu. Both contexts are revoked and destroyed before unloading.

The gesture regression uses private Shell files and Windows recognition. Mouse
hit testing and native command routing are component checks; live interaction
with other input handlers requires Explorer validation.

## 0.9.311 tab routing correction

Live testing showed that 0.9.310's registered Shell activation could open a new
window despite receiving a browser service site. The corrected path uses the
native view's selected-item `IContextMenu`, initializes it with
`QueryContextMenu`, resolves the enabled `opennewtab` entry by canonical name,
then invokes its numeric command on that same object. It requires the owning
tab's browser HWND and revalidates tab, selection and lifetime before invoking.
There is no Shell activation or new-window fallback.

Read-only inspection also found that remote COM `GetCommandString` calls can
fail even though the same locally bound menu exposes the verb. They cannot
establish that Explorer's in-process menu lacks the command. The private test
host has no Explorer tabs; regressions check selection identity, queue draining
and rejection without window activation, not actual tab creation. The earlier
activation interception checked API arguments but did not prove a tab opened.

## 0.9.312 native signaling trace

Live testing of the menu route showed a busy cursor without a new tab. A native
right-click trace, symbolized with the matching Microsoft PDBs, established:

`RegDataDrivenCommand::_Invoke` → `CBindAndInvokeStaticVerb` →
`CExecuteFolder::Execute` → `CExecuteFolder::ShowFolder` →
`CExplorerLauncher::ShowWindow` → `TryBrowseInPlace` →
`CShellBrowser::BrowseObject(0x411)`.

After `BrowseObject` returned `S_OK`, Explorer's own window-message dispatch
called `CExplorerFrame::AddTab(entry=2)` → `AddTabAtIndex(index=-1)`, both `S_OK`.
The trace covers the modern menu and the classic `CDefView` menu route. Tab
creation is queued by Explorer; it does not need a mod-owned waiting queue.

Middle-click now sends the complete observed request to the owning tab's
`IShellBrowser`: `SBSP_ABSOLUTE | SBSP_SAMEBROWSER | SBSP_OPENMODE | 0x400`.
The native new-tab bit is undocumented, and is also recognized by
[Explorer Column View](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/explorer-column-view.wh.cpp).
The clicked folder's absolute PIDL comes from the existing verified native
selection lane, including provider folders. Tab, browser, selection and lifetime
are checked again before dispatch. It loads no context-menu handlers and creates
no pending middle-button capture, tab-creation polling, or new-window fallback.

`tests/native-tab-command-trace.wh.cpp` resolves the native command and tab
methods through Windhawk's Microsoft symbols. It records initialization,
execution, browser flags and tab creation with bounded module-offset stacks,
then passes each call through unchanged. It preserves thread last-error state
and drains callbacks after hook removal. This avoids suspending Explorer for a
debugger attach; offline symbolization uses the matching Microsoft PDBs.

The first full release run failed the combined hide/deferred-frame assertion.
The focused graphics run and complete release rerun passed without a change to
the hide handler. The assertions now distinguish a failed hide, a retained paint
latch and a queued frame wake. The original failure's cause is unconfirmed;
these runs do not establish a fix for that intermittent failure. Actual tab
creation was subsequently confirmed by the user; the later tab-startup blinking
is investigated separately in `new-tab-startup-0.9.313.md`.
