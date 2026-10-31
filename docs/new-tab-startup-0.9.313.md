# New-tab startup and presentation

The supplied 0.9.312 capture repeats the same sequence twice in one Explorer
frame. New background hosts appear at 19:06:59.007 and 19:07:05.709. The current
This PC renderer remains the same visible HWND and lifetime. The new F: tabs
have no registered renderer when selected at 19:07:01.087 and 19:07:07.030;
attachment starts only after a later ordinary dispatch. They become visible GPU
surfaces at 19:07:01.174 and 19:07:07.159. This is delayed initialization, not a
recreation of the This PC renderer.

Cold startup previously stopped at a frame's existing active renderer, while
discovery and initialization rejected inactive browsers. Startup requests now
retain each new tab's host identity separately, use the existing owner-STA
retrieval hook and retry lane, and query only the matching tab browser. Each
tab prepares one hidden first-content surface on its own presentation worker,
then enters ordinary standby. Activation presents that tab's existing content.
Unsupported layouts, failure, cancellation and unload retain native fallback.

Creating a new native frame sibling can change HWND stacking before queued
lifecycle delivery. The synchronous barrier repairs the current GPU window's
stacking only. An unfinished 0.9.313 candidate incorrectly called the complete
positioning path here; the user observed horizontal jumping over navigation
controls. This handler now uses `SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE` and
keeps the existing rename/status controls above the overlay. Native layout
notifications retain responsibility for geometry.

Adjacency to a pinned control is insufficient if a new native sibling is above
both windows. Stacking repair raises the control and its GPU surface together,
with a reentrancy guard and unchanged bounds. Activation also handles USER's
parent reorder events, including selection among multiple visible tab hosts.

Private Windows fixtures create/show/reposition real HWNDs with a navigation
pane, including a temporary whole-tab native layout. They check unchanged GPU
bounds before pumping messages, independent hidden/visible tab-host preparation,
cached activation, the enabled view-switch button, input-free retrieval and draining.
These are component checks, not proof of a blink-free live Explorer transition.
