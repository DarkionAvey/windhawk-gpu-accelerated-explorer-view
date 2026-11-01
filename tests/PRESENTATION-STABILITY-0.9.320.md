# Presentation stability audit

The audit followed resize, background-tab creation, native suppression,
foreground controls, rename placement and accepted-image replacement. These
findings distinguish reproducible window-manager churn from displayed flicker,
which still requires validation in live Explorer.

## Confirmed unnecessary work

- The GPU window, view button and rename field did not share an ordering rule.
  The button requested HWND_TOP, the editor independently requested HWND_TOP,
  and the overlay tried to sit immediately below whichever it had pinned.
  With rename open, three unchanged layout refreshes generated nine owned-window
  position transactions in 0.9.319.
- Some checks used the first raw frame child, while others ignored invisible
  children. Raising a private hidden sibling generated two otherwise unnecessary
  owned-window position changes. Hidden tab surfaces cannot cover the source
  tab, but these checks still rearranged its visible windows.
- A tab-host reorder checked whether the GPU window itself was the first child.
  A correctly placed view button already makes that false. This could queue a
  redundant tab-activation repaint; this producer also lacked the existing
  per-tab wake coalescing used by the synchronous presentation barrier.
- Repairs positioned the pinned control and GPU window with separate calls.
  Rename placement also requested SWP_NOCOPYBITS and SWP_FRAMECHANGED even for
  ordinary movement/order repairs. These flags discard valid client pixels and
  force nonclient recalculation. Font initialization still explicitly refreshes
  the custom native border where needed.

0.9.320 uses the order editor, view button, GPU window, with invisible unrelated
siblings ignored. One shared placement path compares actual frame-relative
rectangles and visible sibling order, skips unchanged work, and applies needed
changes through Begin/Defer/EndDeferWindowPos. Geometry-only work preserves Z
order; order-only work preserves geometry. Rename width measurements also retain
valid client pixels. The existing owner/animation continuation retries failures.

## Verification

`presentation-stack-smoke.h` observes USER's real WM_WINDOWPOSCHANGING,
WM_WINDOWPOSCHANGED and WM_SIZE delivery on the existing attached private
Explorer fixture. It uses actual creation/positioning APIs, including hidden and
visible frame siblings, and opens rename through BeginRename on a private file.
It does not manufacture paint/input/position messages or change user focus.

The assertions reject unchanged-layout position changes and intermediate owned
window order during repair. They fail on 0.9.319 and pass on 0.9.320 with and
without rename. Background tabs still prepare independently, preserve the source
directory/visibility, and activate their own cached content. Existing resize
regressions separately inspect retained accepted swap chains, warm glyph pixels
and native resize events in LTR/RTL.

## Paths retained and live-validation limits

The renderer's transparent clears target pending drawing buffers. Resize already
keeps the accepted swap chain attached until a replacement is accepted; changing
that mechanism based only on a Clear call would be unjustified. Native host
suppression and tab-owned render lifetimes remain in their existing paths.

Transient Shell tab-host visibility/ordering is another possible source of live
handoffs. This audit does not establish it as the cause of the reported source-tab
blink, and does not replace active-tab selection with a timing heuristic.

UI transition diagnostics now include `ui.window.position` (HWND in `a`, SWP
flags in `b`, requested geometry in `v`; NOMOVE/NOSIZE mean those coordinates
are ignored) and `ui.window.batch` (overlay HWND in `a`, window count, restack,
geometry-change and success values). Together with existing visibility,
composition-content, resize and accepted-present records, these can identify
remaining live handoffs. Logging uses the existing bounded asynchronous writer.

Private-window order checks and GPU readbacks do not observe DWM scan-out or
prove that every live microblink has been eliminated.

API references: [SetWindowPos flags](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos),
[EndDeferWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enddeferwindowpos).
