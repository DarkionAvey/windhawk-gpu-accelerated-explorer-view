# First-frame parent positioning (0.9.297)

Two paths could expose an intermediate viewport during swipe Back:

- `NavigateCurrentShellTab` requested native history navigation before calling
  `MaybeDisplayImmediateBack`. Native input predictions prepare that return
  before dispatch. A Shell rebind can already have changed the departing path
  and retired its Back hint by the time the history request returns.
- On a cache miss, the directory lane published unsorted name prefixes starting
  at row zero. The returned child's final index and grouping were resolved only
  in the complete result. That result then centered the child after provisional
  parent rows had already been eligible for presentation.

The swipe path now prepares its known return before the Shell request. It keeps
the browser alive across COM callbacks, validates the tab/view lifetime, and
rolls back a rejected speculative return only while that generation still owns
the request. Back/Forward history remains native Shell history.

The shared directory request disables initial name prefixes when an ancestor's
child must be revealed. Cached returns still position immediately. On a cache
miss, the renderer retains its accepted image while names are pending; the first
parent snapshot contains the final rows, selection and immediate scroll offset.
This also covers ancestor navigation initiated outside the swipe path. Ordinary
cold child/sibling visits retain their early name prefixes.

The new regression uses a private offscreen ExplorerBrowser with its real travel
log, real filesystem enumeration and Windows' native touch recognizer. LTR and
RTL Back gestures return to a child outside the parent's initial viewport. The
test checks the first nonempty parent snapshot, absence of scroll/row animation,
the actual native history destination, and the unchanged offset after native
completion. It checks model publication and native integration, not physical
display frames. Native callback ordering is documented in Microsoft's
[ExplorerBrowser events reference](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorerbrowserevents).

The focused touch suite and final full release gate passed, including native
menu, rendering, selection, layout/cache and DLL-unload checks. The first full
attempt caught an unremoved private fixture directory; cleanup was corrected
before the successful gate. Only that successful run published 0.9.297.
