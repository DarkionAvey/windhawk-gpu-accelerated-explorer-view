# Touch gesture routing, 0.9.266

## Evidence

The device trace shows pointer releases reaching the overlay and Windows
recognizing taps and manipulation/inertia. It also reports 541,217 dropped
pipeline records, so it cannot reconstruct all six double-tap cycles or prove
why every navigation preview remained visible. No context-menu monitor change
is included: the user confirmed that behavior also occurs in native Explorer.

Two actual Ninput reproductions established double-tap rejection. First, a
recognized pair was rejected by another timeout measured at owner dispatch.
Second, with Windows' double-click interval at 500 ms, Ninput reported separate
taps for a pair whose DOWN events were about 350 ms apart. The old tests used
only a quarter of that configured interval and identical contact positions.
Each tap remains natively recognized; completed taps now pair using original
DOWN timestamps, `GetDoubleClickTime()` and the shared item/divider identity.
Touch targets are not narrowed to a mouse-sized double-click rectangle.

A native hold reproduction also failed when its low-priority timer was left
unserviced before movement. Recognition now advances stationary holding to the
next physical packet's timestamp before processing that packet. The deadline
still comes from `GetHoldParameterInteractionContext`; no estimated delay or
velocity is introduced. Timer delivery remains the stationary-release path.
Both QPC and `dwTime`-only contacts are exercised. Scheduling maps the original
timestamp to QPC. A fixed context-local mapping normalizes legacy timestamps
for every recognizer frame and timer update; the original packet identity and
time remain authoritative for deduplication and clicks. Mixing native clock
formats reproduced a packet rejection, and unnormalized legacy holding failed.

Two lifecycle gaps were also corrected: physical last-contact release now
finishes history intent even when native inertia completion arrives later,
and cancellation invalidates the rendered preview after clearing owner state.
Finishing the preview fade no longer clears intent while inertia is running.

## Routing

Windows Interaction Context continues to own recognition, rails and inertia.
At Windows' DPI-aware drag threshold, vertical movement scrolls for every
contact count. One-contact horizontal movement enters the shared mouse marquee
geometry, capture, rollback and native selection transaction. Adding another
contact rolls back that provisional selection before returning to manipulation.
Held-item dragging keeps its existing precedence over scrolling/marquee.

Two-contact horizontal movement consumes the actual column scroll range.
Only unconsumed logical displacement drives Back/Forward feedback and release
commit. Reversing pays back that displacement before scrolling columns again.
The shared path also applies column-edge consumption to precision touchpads.
History distance is frozen at release; native inertia cannot create or repeat
a history command. RTL maps physical motion into logical column/history space.

Pinch/pan intent resolves once at Windows drag slop, comparing net span change
with midpoint travel. Established pans cannot be stolen by contact-spacing
noise. Ninput expansion is a radius change:
a calibrated 120-to-150-pixel contact span reports expansion about 15 and scale
about 1.25. An established pinch keeps proportional cumulative scaling while its
midpoint moves, using the existing item-relative layout anchor.

Input ownership persists after release, independently of contact/inertia flags.
Touch cancels mouse tracking and clears mouse hover; stationary cursor refresh
and touch-promoted mouse messages cannot reclaim ownership. Genuine mouse/pen
movement or buttons select that device, and touchpad contacts retain their
DirectManipulation path. Mouse leave also suppresses stationary-hover refresh
until the pointer reenters, fixing the full suite's exposed leave/paint race.

## Explorer click preferences

`SHGetSetSettings(SSF_DOUBLECLICKINWEBVIEW)` controls single-click activation in
the shared completed-click path. Modified clicks remain selection operations.
Point-to-select uses USER's `TrackMouseEvent`/`HOVER_DEFAULT` and is disabled
while touch owns input. Underline preferences are read through Shell's
user/machine registry fallback and applied only to filenames. Cached glyph
masks distinguish underlined and plain variants; metadata remains plain.

The installed Windows Shell implementation was inspected read-only to verify
the `IconUnderline` values: 0 always, 1 never, 2 hot only, 3 browser preference;
the fallback is 2. Browser mode reads `Anchor Underline` (yes/no/hover). This
matches the upstream [Shell implementation's mode definitions](https://github.com/reactos/reactos/blob/master/dll/win32/shell32/dialogs/general.cpp).
Policy is captured on navigation/attachment and refreshed through the existing
owner style queue on relevant `WM_SETTINGCHANGE` broadcasts. No Windows settings
are changed and no mod options are added.

## Verification boundaries

The native component suite covers six successive double-tap cycles with varied
intervals, contact locations and press durations, delayed dispatch,
1/2/3/5-contact vertical movement, marquee commit/capture loss/contact
addition, two-contact column edges in both directions and RTL, reversal before
release, and the actual posted history command reaching the private Shell
browser fixture. Moving pinch is checked across list/grid, grouping, RTL and
96/144/192 DPI. It includes contact-span noise, delayed hold delivery, persistent
input ownership and single-click/modifier behavior. Offscreen glyph pixels are
compared with DirectWrite for underline variants in wrapped/LTR/RTL labels.
Existing overscroll, native inertia, control sizing, tab isolation and DLL
retirement coverage remains enabled.

These use real Ninput with private digitizer calibration and temporary Shell
items; no Windows input messages or recognizer outputs are manufactured.
Desktop feedback and modal effects remain substituted. The development host
has no physical touchscreen. A private-desktop delivery probe was denied by
Windows before injection, so no desktop input was injected. These checks do
not establish physical driver delivery or perceptual parity with native Explorer.
