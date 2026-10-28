# Touch intent and fitting directories, 0.9.267

## Reproduction and cause

The new native Ninput regression fails on archived 0.9.266 when one finger
begins moving before the other. The earlier classifier compares aggregate
span change with midpoint travel on the first significant output. Moving
only one finger along the pair's axis makes span change twice midpoint
travel, so it commits to pinch before the other finger starts scrolling.
The pinch branch then explicitly applies translation as well as scaling.

The expanded run also observed `contactCount == 2` in Ninput output while
three physical contacts were active. Gesture policy now uses the peak
physical count from the existing pointer frames. Releasing a finger cannot
turn a still-pending two-finger gesture into one-finger marquee.

## Arbitration

The existing native recognizer still owns manipulation output, scale, rails,
inertia, taps and holding. No input polling, velocity estimate, additional
gesture timer or user setting is introduced.

When two contacts are present, save their IDs, initial positions and centroid.
Resolve IDs independently of frame array order.
Normalize displacement by their initial centroid radius, R. With normalized
contact displacements d0 and d1:

- Common movement C = length((d0 + d1) / 2).
- Differential movement D = length((d1 - d0) / 2).
- Uncertainty E = Windows' current DPI-aware drag slop / R.
- Pan has sufficient shared movement when C > D + E. A single early-moving
  finger has C = D, so does not prematurely decide the gesture. Unequal speeds
  and angles are allowed once common translation dominates their difference.
- For each finger, measure both displacement projected onto its original
  radial direction and actual change in distance from the original centroid.
  Both must agree on inward/outward movement. Rotation alone fails this check.
- Both fingers must agree on contraction or expansion. The weaker finger's
  radial evidence must exceed the combined magnitude of common translation,
  tangential movement and system uncertainty. When Back/Forward is enabled,
  their complete displacement vectors must also point in opposing directions.

This uses relative geometry and system uncertainty rather than a fixed pixel
distance or an assumed delay between finger movements. Ambiguous input remains
pending. Native translation accumulated during that period is applied exactly
once when pan commits, so initialization does not discard scroll distance.

After commitment, the gesture stays on that action until completion/cancel.
Marquee retains its existing rollback when another contact is added. Pinch
aligns the first native scale delta to the measured reference span, then uses
native scale deltas and the original centroid as its item anchor. It does not
separately pan columns, scroll rows or build
history displacement. Losing/replacing a committed pinch contact suspends
scaling for that contact sequence. A replacement pair before commitment
rebases geometry. This matters because the replacement reproduction observes
native cumulative scale moving from 0.875 back to 1.0; treating the old native
value as the new pair's baseline undercounts the measured expansion.

One-contact translation enters the existing captured marquee path when
`MaxScrollPx()` is zero, regardless of direction. This covers fitting list and
grid directories, including RTL. A scrollable directory retains vertical
scrolling and boundary feedback even if the gesture begins at either end.

## Verification

The native component suite covers staggered swipe starts, unequal paths and
spacing drift at 96/144/192 DPI, both movement directions, different pair
orientations and Back/Forward enabled/disabled. Pinch/expand covers different
initial separations, axes, DPI and staggered movement. It verifies exclusive
zoom, a fixed content anchor, rotation rejection, contact replacement and
commitment across added/lifted contacts. Fitting-directory tests cover real
shared marquee selection commit/capture cancellation in list/grid and RTL.
Existing double-tap, hold/drag, scrollable-view overscroll, edge navigation,
inertia and tab-isolation coverage remains enabled.

These feed calibrated pointer frames through production code into actual
Windows Ninput. They do not manufacture Windows messages or recognizer output,
and do not inject into the user's desktop. Desktop/modal effects are substituted.
They establish component behavior, not physical touchscreen delivery or
perceptual equivalence to native Explorer; this host has no physical touchscreen.

The full `build.bat -SourceFile 0.9.267.cpp` gate passed: installed Windhawk
DLL linking, repository regressions, full C++ suite and actual DLL retirement.
Published/latest/archive source hashes match, and retained historical bytes
are unchanged. An initial full run failed the existing sort-property cache
assertion for file511.txt. Its retained binary passed `--smart-refresh`, and
the full rerun passed with added metadata-change diagnostics and the original
assertion intact. The cause of that initial failure is not established; no
sorting behavior was changed or sorting assertion weakened.
