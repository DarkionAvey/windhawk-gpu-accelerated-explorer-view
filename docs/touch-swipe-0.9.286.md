# Swipe ownership regression, 0.9.286

The supplied 15:37–15:38 capture contains explicit Horizontal (2) to Pinch (4)
transitions at 15:37:56.560, 15:37:59.688 and 15:38:06.713. At the second
transition, both projected radial values are only about -4.1 pixels. Switching
to Pinch retires the history preview and suppresses its release commitment.

A calibrated Windows recognizer reproduction on unchanged 0.9.285 logic fails
after 104 pixels of horizontal travel with only four pixels of spacing drift.
Both contacts move sideways together, but their vertically offset positions
project inward/outward as their gap changes. Radial agreement alone did not
establish opposed finger movement. Treating every small span change as scaling
also prevented the pan reference from refreshing, allowing drift to accumulate.

The common classifier now additionally requires the full displacement vectors
to oppose (negative dot product) before acquiring pinch. Concordant movement
keeps pan ownership and refreshes its reference through spacing changes. Genuine
unequal opposing motion and pan-to-pinch handoff retain Windows' existing drag
slop and the current span anchor, without the old weaker-finger-versus-centroid
penalty, timers or empirical thresholds. Optional traces include both vectors.

The earlier drifting-pinch fixtures incorrectly required zoom even when a large
shared translation made both fingers travel in the same direction. Those paths
are now covered as swipes; pinch fixtures retain drift with opposing overall
directions. Native tests cover converging/diverging long swipes, staggered
starts, Back/Forward, reversal, within-viewport arming, actual owning-browser
command delivery and no inertial repeats at 96/144/192 DPI in LTR/RTL. Existing
unequal/gradual pinch, late-contact, pan handoff and contact-change guards remain.
These use private offscreen fixtures and actual Windows recognition, without
desktop injection or manufactured recognizer output. Live device validation
remains separate.
