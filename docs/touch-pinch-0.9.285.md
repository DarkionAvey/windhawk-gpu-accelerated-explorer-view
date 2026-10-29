# Pinch acquisition, 0.9.285

The supplied 15:06 capture shows continuing two-contact Windows manipulation
output near the end while the rendered row height stays at 35.2957 pixels.
For example, the recorded scale deltas for pointer 0xE9 at
15:06:41.339–15:06:43.165 multiply to about 1.41; pointer 0xF4 at
15:06:50.277–15:06:50.696 reports about 1.22. The capture lacks raw pair geometry
and committed gesture state, so it cannot identify each rejected finger path.

A calibrated native regression reproduces rejection on unchanged 0.9.284 logic:
both contacts move inward, one four times farther than the other, yet the
gesture stays Pending. The old test required the weaker contact's movement to
exceed centroid drift and tangential motion. This can remain impossible at any
travel distance. Only Pending could become Pinch; an early pan decision or a
second finger added after one-finger scrolling also locked zoom out.

The shared touch path now checks opposing radial movement and matching actual
span change against Windows' DPI-aware drag slop. It imposes no symmetry ratio,
centroid/tangent penalty, or history-specific vector test. Native manipulation
events still own recognition. A coherent pan refreshes its tracked pair's
reference; emerging scaling keeps its evidence across small packets. The same
two contacts may promote pan to pinch, and a newly added second contact starts
pair arbitration. Three-contact sequences and replacement/lost committed
contacts retain their guards. Recognition distance is consumed without changing
the current image; later span changes drive the existing fractional zoom path.
Pinch handoff retires native pan feedback and prevents history commitment.

Optional pipeline traces now include `touch.pair` radial/span evidence and
`touch.intent` transitions. The native tests use private calibrated contacts
without injecting desktop input or manufacturing recognizer output. Coverage
includes unequal and shared-drift paths, gradual drifting pinches, late second
contacts, pan handoff, rotation, replacement, DPI, RTL and both history settings.
These are offscreen component checks; physical touchscreen delivery and live
Explorer behavior still require hardware validation.
