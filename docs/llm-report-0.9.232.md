# Seven-point report decisions (0.9.232)

All seven findings were reconciled to current functions and addressed:

1. Cached history now holds a weak directory-change stamp. Active tab/watch work
   owns the handle; visiting a folder no longer leaves its watch alive solely
   through history. Expired history validates names asynchronously before display.
2. Resolved unsupported tabs suppress repeated discovery until a real lifecycle
   change, including repeated same-HWND resize attempts.
3. File activation invokes the selected group's native default context-menu verb
   with the current working directory, retaining deferred selection preparation.
4. Frame subscription release follows the subscribing state's ownership rather
   than mutable hook identity. Unload closes attachment admission, drains API
   callbacks and repeats overlay cleanup; frame hooks are released on their owner.
5. Accelerator handling resolves the registered active overlay and exact editor
   HWND, without interpreting unrelated controls' application-owned USERDATA.
6. Floating-point/date NaN sort values become missing values; infinities remain
   valid, preserving strict ordering.
7. The background-lifetime registry is explicitly released during normal teardown
   and has no process-exit destructor that could join callbacks under loader lock.

Validation: `build.bat -SourceFile 0.9.232.cpp` passed the repository/export gate,
Windhawk DLL linking, all option contracts, full C++ offscreen suites, real DLL
unload and the blocked-callback process-exit regression. Focused report checks
cover failed-then-upgraded frame hooks, arbitrary USERDATA, unsupported discovery,
NaNs, selected-group default activation and watch release. The two published
sources matched the tested archive and all 35 earlier archives remained unchanged.
These checks do not substitute for live Explorer visual/performance validation.
