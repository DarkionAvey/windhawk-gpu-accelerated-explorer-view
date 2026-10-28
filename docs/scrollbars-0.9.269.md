# Scrollbar engagement and deferred horizontal chrome, 0.9.269

The existing touch-size target followed persistent pointing-device ownership,
so both bars remained enlarged between contacts. Size targets now follow each
axis's actual contact/track engagement or captured thumb drag. Release, track
leave without capture, cancellation and mouse ownership retarget the existing
QPC animation to normal size. Immutable presentation models carry both clocks;
renderer-local hover/press animation remains independent.

Touch hit geometry stays at the existing DPI/native-metric-aware enlarged size
even when idle visuals are small. Neither viewport width nor column fitting
depends on animated thickness. Native-recognizer/GPU checks exposed a related
high-DPI bug: using the enlarged minimum thumb length to anchor a still-small
drawn thumb scrolled content on DOWN. Grabs now anchor to the drawn pose while
retaining the enlarged hit geometry.

Horizontal visibility previously depended only on current overflow. Native
column snapshots can overflow before automatic fitting reduces their widths,
exposing temporary horizontal chrome. A tab-owned admission bit now waits for
existing column discovery, fit and accepted animation completion events, and is
copied into presentation models. Already admitted bars remain usable through
transitions. Actual horizontal scrolling can admit a needed bar immediately.
Directory navigation resets admission. No new settling timer, provider call or
input delay is introduced; vertical bar and horizontal scroll-range calculations
retain their existing paths.

Verification uses calibrated raw contacts with the real Windows recognizer,
actual USER capture loss and GPU readback via model capture/application. It
covers both axes, LTR/RTL, 96/192 DPI, untouched idle bars, enlarged idle hit
targets, continuous size reversal, outside-track captured drags, uncaptured
track leave, mouse hover and unchanged viewport geometry. Cold/delayed native
attachment and real frame/host resizes observe published models to reject
premature horizontal admission and require deferred provisional overflow.
The minimum-width geometry test now uses a private owner window and real
`SetWindowPos` resizes, rather than constructing a renderer without an owner
admission model. Its DPI/font/readability, RTL and manual-divider checks remain.
These are native API/offscreen component checks; physical Steam Deck/touchscreen
delivery and displayed-frame behavior remain hardware validation.
