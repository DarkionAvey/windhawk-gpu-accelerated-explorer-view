# Native UI Automation stalls after opening multiple tabs

## What changed the diagnosis

The next real Explorer capture is 9.25 MB and contains repeated native
DirectUI dispatch stalls of 516–812 ms. Crucially, the new diagnostic says
`viewport=0x0 suppressed=1` during those stalls. The late-child repair in
0.9.204 did not address the main cause of the reported multi-tab lag.

The user confirmed that the newest tab becomes affected after opening several
tabs, while other tabs remain usable. This condition was absent from the old
single-control probe. Its small suppressed-viewport CPU measurements must not
be treated as evidence that the live file-management problem was solved.

Read-only live stack sampling then captured the affected Explorer UI thread
while the user reproduced the problem. In a 300-sample capture, 136 stacks
contained DirectUI and 130 contained UIAutomationCore (overlapping inclusive
counts). The recurring native call chain was:

```
UIItemsView::WndProc
  UIItemsView::PreProcessEventQueue
    DirectUI::Element::EndDefer
      DirectUI::DeferCycle::_EndDefer
        DirectUI::EventManager::RaiseStructureEvents
          RaiseStructureChangedEvent / RaiseChildRemovedEvent
            UiaRaiseStructureChangedEvent
              UIAutomationCore provider queries
                DirectUI::ElementProvider::DoInvoke
                  SendMessage / installed message-hook chain
```

The stack names were resolved with matching public Microsoft symbols. Other
installed hooks appeared in the message-call chain; that is evidence of extra
work on those calls, not proof that another mod is independently defective.
Earlier samples caught idle waits and NVIDIA Present activity instead; they
were not used to explain the native stall without the failing-tab capture.

The sampler copies at most 128 KiB of a UI thread's stack while briefly paused,
resumes it before any unwinding/symbol work, and never writes to Explorer,
injects code, changes focus or issues a file operation. The relevant 300-sample
run accumulated 15.12 ms of pause time, with a 0.083 ms maximum pause. Windows
Performance Recorder could not enable profiling under the available account;
no WPR recording was left running. These are diagnostic stack samples, not
display-FPS measurements.

## Fix

Batch **structure notifications**, not Shell model changes. The native folder
view still processes its normal messages, operations, selection and refresh.
An owner-STA scope spans an actual native dispatch/callback for an already
suppressed list. The public `UiaRaiseStructureChangedEvent` hook identifies the
event's fragment root and checks its real HWND against that owned list/view.
DirectUI delegates its HWND property to the host provider; that case must be
supported. A foreign root is passed through normally.

At dispatch completion:

- One event retains its exact provider, change type and copied runtime ID.
- A burst for that native root becomes one `ChildrenInvalidated` event. The
  accessibility client can rebuild its cache once, instead of doing repeated
  synchronous provider queries for the same native update.
- A nested dispatch to other chrome does not inherit the native batch.
- Navigation, ownership loss, native fallback, destruction or unload cancels an
  obsolete batch. No guessed timer decides when to send it.

The provider root and singleton references stay on their original STA. No
worker dereferences `ViewState` or a provider. TLS holds only an automatic-scope
pointer, without a DLL thread-exit destructor. The seventh API hook joins the
existing callback-draining/unload regulation. Its system DLL reference is
released after detours are removed and callbacks drain.

This preserves accessibility notifications rather than disabling UI Automation
globally or dropping the native list's message. It also preserves the existing
no-detach selection recovery. `native.uia.batch` reports event count, bulk kind,
delivery elapsed time and HRESULT; unmatched providers have an aggregated passthrough
diagnostic. Logging still defaults to off.

Microsoft's public structure-change API and bulk-event definitions are the
contract used here:
[UiaRaiseStructureChangedEvent](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiaraisestructurechangedevent),
[StructureChangeType](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/ne-uiautomationcore-structurechangetype).

## Validation and limits

The targeted test now uses **three real Windows ExplorerBrowser views**, a
private 512-file directory and a real MTA UI Automation client subscribed to
subtree structure changes with cached name/offscreen data. Both comparison
cases suppress the native views. The provider's API pointer in the
standalone test process is substituted to route through the production hook;
the real Windows provider, UIA core and client execute the operation. This does
not patch or inject into the user's Explorer.

The client and its handler are created, subscribed, unsubscribed and released
on the same MTA. A failed gate exposed a separate test-isolation problem:
UIA's legacy WinEvent bridge subscribed to desktop-wide events despite the
element-scoped handlers. Live test-process stacks showed its final unsubscribe
waiting in `WinEventManager::RemoveEventListener_AnyThread`, behind the bridge's
`AccessibleObjectFromWindowTimeout`/`WM_GETOBJECT` probes of unrelated apps.
The fixture now substitutes only UIA's own `SetWinEventHook` import (including
delay imports) to restrict zero-process subscriptions to the test process.
Real subscriptions, events, native providers, client cache delivery and final
unregistration still run; unrelated desktop applications cannot determine the
test's completion. This substitution is absent from the mod. Native views are
suppressed before client setup/priming, as well as in both measured cases.

The first attempt passed the simple provider contract tests but failed the
real-client check: 120 native events still produced 120 API notifications.
The real provider returned `VT_EMPTY` for its own HWND property and exposed the
HWND through its host provider. Supporting that documented delegation made
the same test produce 12 notifications for 120 events. After isolating the
legacy WinEvent bridge, a representative focused run took 176 ms with batching
and 601 ms without, across 12 native mode changes. These are controlled,
offscreen component elapsed times, not a replay of the user's tab or proof of
displayed frame rates. The client must also receive a
bulk event with usable cached provider data.

Contract tests cover exact singleton/runtime IDs, host-provider delegation,
818-event bursts, nested same/foreign windows, provider-call reentrancy,
cancellation, foreign-thread ownership and unload passthrough. Final release
validation is recorded in `CHANGELOG.md`. The live failing-tab stacks establish
the cause; the new code's
interactive behavior in that exact live session still requires a reload and
observation rather than an assumption based on these component tests.
