# Touch ownership and drag previews

The two-contact classifier explicitly permitted committed horizontal/vertical
pans to acquire pinch. Classification now runs only while intent is pending.
The native recognition suite exercises opposing radial movement after a pan,
including an already visible history bubble, at three DPIs and in LTR/RTL.
Adding a second finger to a one-contact gesture retains its existing arbitration;
releasing/replacing contacts cannot reclassify a committed two-contact action.

Held-item dragging previously depended on Ninput producing a manipulation after
HOLD. Physical frames now test Windows' DPI-aware drag threshold after the native
hold deadline, retaining the held item's identity and prepared native selection.
The queued continuation still promotes the real primary contact through USER
immediately before the shared Shell OLE drag. Pending drags exclude tap/menu/pan
outputs and are revoked on release, extra contacts, navigation or cancellation.
The native test now moves by the system drag threshold instead of 80 pixels.
These checks substitute promotion/modal desktop effects; they do not establish
physical digitizer delivery or actual drops in a live Explorer window.

The previous preview used an exact-size-only cache lookup, nearest-neighbor
resampling, a fixed light tile and a rectangle fallback. It also treated the
thumbnail/badge atlas as a single square image. The replacement shares the
currently displayed cached pixels immediately and preserves the thumbnail's
content rectangle and aspect ratio. A bounded sharper size request uses the
existing tab-owned image lanes, request IDs, cancellation and cache budgets.
Visible-demand updates retain this request while the drag exists. Completed
artwork updates the preview through normal image delivery; there is no polling
or synchronous Shell image extraction on the input thread.

One input-transparent, nonactivating system-class popup is scoped to the OLE
source. Small immutable feedback packets go directly to the existing tab render
lane, independently of directory/content snapshots. The renderer reuses its
device, surface publishing and UI animation curves; the popup has no callback,
timer, worker or extra GPU device. Cursor movement only repositions the window.
Effect/artwork changes repaint a bounded texture. DirectComposition animates a
short lift/settle and honors Windows' client-area-animation preference.

Tiles use Explorer's cached theme colors. All operation badges use Segoe UI Emoji:
🚫 for unavailable drops, 📋 for Copy, directional arrows for Move, and 🔗 for Link.
The emoji have no backplates. Bounds of their recorded color layers give them
the same width while retaining their proportions and centering in both axes,
independently of base-glyph bearings and baseline whitespace. The current symbol's
vector command list is cached, without CPU pixel readback or another GPU device.
Badge size follows artwork and native
icon metrics. Abbreviated selection counts share that renderer. OLE still uses
the native selected-item IDataObject and existing Shell target bridge, including
self-drop rejection, application/archive targets, modifiers and virtual items.
The source implements OLE's standard release/Escape policy for logical left
drags. Default OLE cursors remain available if preview composition fails.

Popup identity tokens reject delayed packets for reused HWNDs. Composition and
pixel references retire on the tab lane; the source destroys its popup and clears
its packet when the modal drag ends. A hidden source tab retains only its existing
graphics resources needed by the in-progress drag until that drag finishes.

Offscreen GPU checks cover alpha edges, actual color emoji pixels, dark/light
palettes, both layout directions, atlas-aware wide thumbnails and three DPIs.
Private Shell fixtures verify immediate shared pixels and sharper worker results.
A private offscreen layered window checks DirectComposition target creation,
reuse of the tab device, animation attachment, cancellation and HWND identity.
These are component checks, not screenshots of live Explorer or displayed frame
rate measurements.

The full `build.bat -SourceFile 0.9.333.cpp -Jobs 2` release gate passed all 12
regression groups, installed-Windhawk/minified linking and actual DLL unload.
Earlier runs exposed external foreground changes, a directory snapshot failure
and a graphics standby timeout. Their causes are not established by the passing
rerun. Fixtures now have GUID-suffixed identities so retained failures cannot
collide with recycled process IDs; snapshot failures also report error/count.
