# Inline storage bars

The supplied capture repeatedly returns from another folder to This PC. At
19:50:01.849 it starts a 180 ms Details entrance. During the committed transition,
the traced label rectangle stays 1,611 px wide; at 19:50:02.084 the settled
rectangle is 573 px wide. The capture does not contain storage-track rectangles,
so it establishes the source-to-settled geometry discrepancy rather than the
exact displayed movement of the bars.

Inline tracks previously used the space after each individually measured label.
Canonical column sources have fixed origins/widths and animated DWM clips; using
those fixed widths for width-dependent contents caused their geometry to change
again at handoff. This does not depend on particular metadata PROPERTYKEYs.

The shared Name content geometry now converts the live column extent and viewport
clip into canonical source coordinates while painting. It retains the existing
animation clock, transforms, cancellation and fallback. The borrowed live layout
exists only inside the paint scope. No extra animation, timer or worker is added.

Inline rows share balanced label/track bands, retaining the existing minimum
track width and margins. Long labels use normal filename trimming. Fitting and
clipping preflight and filename-only feedback use the same partition; tile bars
keep their existing layout. Side margins are rounded individually before both
fitting and drawing, including at custom DPI values.
The partition needs no label measurement or directory scan. Diagnostic UI traces
now include `ui.drive.track[x,y,w,h]` for the sampled rows.

`--drive-bars` uses private offscreen windows, synthetic identities and real GPU
source readback. It checks alignment, partition/fitting at standard and custom DPI,
canonical/live bounds during Details entrance and reversal, moved Name columns,
LTR/RTL and the settled handoff. These are component checks, not live Explorer
presentation or monitor-frame-rate measurements.
