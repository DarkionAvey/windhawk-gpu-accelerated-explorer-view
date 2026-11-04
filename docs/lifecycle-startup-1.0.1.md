# Startup and shutdown review (1.0.1)

Source inspection against Windhawk's [mod lifetime](https://github.com/ramensoftware/windhawk/wiki/Mod-lifetime),
[hook API](https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod#wh_applyhookoperations)
and [process shutdown](https://github.com/ramensoftware/windhawk/wiki/Global-objects-and-process-shutdown) guidance.

## Startup

- Windhawk still loads the mod DLL in explorer.exe. The mod retains its early
  layout, input and UIA hooks. Late application of hooks is documented as very
  slow, and native suppression must cover the first owned layout/dispatch.
- Wh_ModAfterInit starts the existing process-local cold-window observer. It
  subscribes to window events and enumerates existing file-manager frames once.
  With no candidate frame, it waits for events without a polling deadline.
  Desktop/taskbar and other applications' windows are excluded before child scans.
- DirectWrite's module reference and shared factory now initialize only when the
  observer has an actual file-manager attachment to post. Initialization runs
  on that observer, before waking the owner STA. Publication uses INIT_ONCE and
  an acquire/release readiness flag; Shell discovery waits for readiness.
  Failure retains the existing pending attachment/retry path and native view.
- The mod no longer directly imports DWriteCreateFactory. Windows or other
  imported dependencies may still load dwrite.dll independently.
- The composition wake event and thread start on presentation/animation demand.
  GPU devices, tab workers and the visibility observer were already demand-driven.
- UIAutomationCore is still loaded for its early suppression hook. Other graphics
  DLLs remain normal imports. This change defers initialization; it does not claim
  that the complete DLL image/dependency graph is absent at login.

## Teardown

- Before unhooking, admission closes, observers join and overlays retire on their
  owning threads. After unhooking, API callbacks drain; frame/dispatch registrations
  and tab workers then drain before shared modules and factories are released.
- The shared DirectWrite COM pointer now has clang::no_destroy. Normal unload
  explicitly releases the factory before its owned module reference. Process
  exit performs neither operation from a global destructor.
- The provider lifetime registry already has no_destroy and explicit joining.
  Other reviewed globals and function statics hold plain data, raw handles,
  heap-only values or weak references; they do not require the attribute.
  TLS state is trivial, and COM/provider state remains owned by tabs/workers.

Existing cold-attachment and DLL-unload coverage was extended for deferred fonts,
idle composition resources, explicit cleanup and process exit with initialized
DirectWrite. No build, regression execution or live startup timing was performed,
as requested. These are source-level findings, not a measured startup-speed claim.
