# Interaction follow-up, working version 0.9.348

Uncommitted New-item rename implementation, tests and changelog claims were
discarded at the user's request after the live issue persisted. Rename is deferred.

Active type-to-select owns Space; idle Space retains its Shell preview route.
The prefix matcher skips whitespace in both strings without constructing
per-row folded copies and returns the original filename extent for highlighting.
`type-search-input` uses Windows TranslateMessage for Space and the actual USER
timeout, with private files containing spaces and punctuation. The extended
`type-search-prefix` checks whitespace, punctuation and independent highlight
length expectations.

The prefix outline included one pixel of horizontal padding but was clipped
to the unpadded text box, removing its leading edge. Its clip now includes that
padding; visible rectangle bounds are intersected before the inset stroke is
drawn. `type-search-border` reads actual GPU pixels and checks all four edges
against a separate DirectWrite layout for LTR/RTL, single/wrapped and narrow
labels with DPI-scaled fonts and independently measured line heights. The
original code fails its leading-edge assertion for the screenshot's text.

The toggle's visibility follows the explicit button option and selected frame,
rather than whether GPU presentation supports the current folder/layout. An
unsupported view keeps a disabled presence button with an explanatory caption
and tooltip. The private native-return route probe exercises genuine
BUTTON notifications, the hotkey, Details/List/icons, resize, and Content view
fallback; its fallback assertions include button presence and option off/on.

## Native restoration: 0.9.340 to 0.9.341

The user's version bisection identifies 0.9.341 as the first affected release.
That version added the WH_CALLWNDPROC fallback `NativeSurfaceMessages`. The
SetWindowPos/DeferWindowPos API hook records a native SHOW request, then replaces
SHOW with HIDE to suppress native content. The message hook sees those already
constrained flags and incorrectly records HIDE as the native request. Toggle,
fallback and unload subsequently restore that false hidden intent.

The fix passes `recordIntent=false` at the message hook, matching the existing
native-host subclass. Caller intent remains recorded at the API boundary.
There is no restoration override, extra state, COM query or recovery timer.

`native-return-routes` and `native-return-unload` install the real fallback
Windows hook for a private ExplorerBrowser and issue actual immediate/deferred
USER show transactions through the production API hooks. Both failed before
the fix because the message hook overwrote SHOW with the suppression HIDE.
The tests check native list visibility/bounds and realized UI Automation file
rows after the button, Ctrl+Alt+F9 and actual Wh_ModBeforeUninit/Wh_ModUninit.
The earlier speculative explicit-SW_HIDE setup was removed; no speculative
native restoration implementation remains. Historical sources are unchanged.

Publication, the full build and full regression suite remain deferred. These
private component/Shell tests do not establish behavior in the user's Explorer.

After the rename rollback and border fix, eight selected checks passed with
`-Jobs 16`: `type-search-border`, `type-search-prefix`, `type-search-input`,
`render-quality`, `pixel-snapping`, `native-return-routes`, `native-return-unload`,
and `attached-lifecycle`. The new border test also passed alone, checking 160
actual pixel edges; its exported preview was visually inspected. The linked
Windhawk-host DLL passes the callback stack gate (maximum 2,296 bytes).

The retained native and search changes are separate from the removed rename
attempt. Offscreen creation checks did not establish the live New-item behavior.
