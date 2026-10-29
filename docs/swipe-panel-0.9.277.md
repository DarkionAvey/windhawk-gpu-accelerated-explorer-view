# Navigation panel motion, 0.9.277

## Evidence and source

The supplied 15.79-second recording shows a filled bubble morphing between a
narrow pill and circle, a changing chevron angle, and vertical following. The
previous indicator had a fixed circular outline, progress ring, rigid arrow and
direct/logarithmic position mapping followed by a timed fade. Its threshold was
`max(280 DIPs, 35% of client width)`, which can exceed the available distance on
narrow or high-DPI displays.

The reference is Android SystemUI at revision
`d639934e19ace08fe76545cd0ce3b282679d4cd6`:

- [BackPanelController.kt](https://android.googlesource.com/platform/frameworks/base/+/d639934e19ace08fe76545cd0ce3b282679d4cd6/packages/SystemUI/src/com/android/systemui/navigationbar/gestural/BackPanelController.kt)
- [BackPanel.kt](https://android.googlesource.com/platform/frameworks/base/+/d639934e19ace08fe76545cd0ce3b282679d4cd6/packages/SystemUI/src/com/android/systemui/navigationbar/gestural/BackPanel.kt)
- [EdgePanelParams.kt](https://android.googlesource.com/platform/frameworks/base/+/d639934e19ace08fe76545cd0ce3b282679d4cd6/packages/SystemUI/src/com/android/systemui/navigationbar/gestural/EdgePanelParams.kt)
- [dimensions](https://android.googlesource.com/platform/frameworks/base/+/d639934e19ace08fe76545cd0ce3b282679d4cd6/packages/SystemUI/res/values/dimens.xml)

The C++ implementation independently solves the unit-mass damped spring equation
using closed-form underdamped and critically damped solutions. It uses the
reference's stiffness/damping, cubic stretch curves, activation impulses,
chevron dimensions, 48-DIP active surface, 60-DIP stretched width and separate
edge/far curvature. Vertical following uses its 268-DIP panel envelope,
rubber-band mapping and 64-DIP finger offset. Dimensions scale with actual DPI
and fit within the current client rectangle; no screen/device size is assumed.

## Windows integration

Windows still recognizes and arbitrates gestures. Column movement is consumed
before the panel appears. The deliberate 280-DIP history distance is now capped
at one third of the actual tab content width. Android's edge capture, haptics,
activation delays and gesture recognizer are not introduced into Explorer.
Post-threshold stretch uses an additional capped gesture distance, keeping the
shape responsive on narrow tabs rather than using Android's 412-DIP display span.

Each spring retains position and velocity when retargeted. Immutable owner and
renderer models carry the same QPC origin, so rendering cadence does not alter
the response and threshold activation cannot jump the surface. Commit shrinks
and fades; cancellation retracts and flattens the chevron. The existing
compositor clock stops when the springs reach visible equilibrium, without a
new timer or guessed duration. Released commit motion survives folder reset and
publication; explicit cancellation, fallback and tab changes clear it.

Narrow-window coverage exposed a second horizontal-intent check in the shared
pan path: a touch gesture already committed by the recognizer was tested again
against `x > 2*y`. A valid diagonal horizontal swipe could therefore consume the
edge without displaying history feedback. Touch now honors its committed axis;
the existing touchpad intent policy remains.

System color refresh uses the existing coalesced theme/color broadcasts,
including `WM_DWMCOLORIZATIONCOLORCHANGED` and `ImmersiveColorSet`. The bubble
always uses system accent, independently of a custom selection color. A packed
atomic caches the accent, black/white foreground and the minimum 8-bit alpha
providing 4.5:1 contrast on the filled surface. Unchanged accent skips contrast
calculation. High contrast uses exact highlight/highlight-text colors. Input and
paint read the cache and do not query color providers.

## Checks and limits

Calibrated raw contacts enter real Ninput in 240-by-1,000 private windows at
96/192 DPI, LTR/RTL and both history directions. Both fingers remain inside the
window until release. Checks cover the threshold cap, continuous activation,
actual accent-colored GPU pixels, zero color API calls during motion, release
commit and animation retirement across folder reset. Existing tests retain
column consumption, reversal, unequal fingers, diagonal scrollbar crossings,
capture cancellation, pinch and actual Shell history routing.

Spring subdivision checks compare identical elapsed time at different sample
cadences. Color-event checks verify the cached alpha/contrast. An active-panel
BMP is written only inside the suite's private temporary fixture and removed
after the panel checks, preserving subsequent enumeration fixtures. Existing
fling checks use a half-device-pixel tolerance: a run measured 0.16 pixel of
float-position rounding over 5,939.71 pixels of travel, exceeding the old
arbitrary 0.15-pixel bound without losing visible movement.

The full gate also exposed cached property restoration sitting behind a worker
queue try-lock. A deterministic test holds that actual lock on a private worker
and verifies cached visible data without waiting. It fails before the ordering
change. Cache restoration now precedes queue pruning/submission, which remains
nonblocking and deferred on contention. The sorting fixture also commits its
initial names through the production completion path and verifies its cache
setup; existing sort/refresh retention assertions remain.

These are native API, owner/presentation and offscreen GPU checks. They do not
establish physical touchscreen delivery, displayed frame rates or hardware feel.

The full `build.bat -SourceFile 0.9.277.cpp` gate passed, including installed
Windhawk linking, repository/C++ regressions and actual DLL unloads. Published,
generated and archived source hashes match. All 14 retained older sources are
byte-identical; the archive retains the newest 15 versions.
