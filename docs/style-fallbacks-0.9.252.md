# Native foreground precedence (0.9.252)

0.9.251 incorrectly treated a theme fill or Windows app background as the
actual surface behind the transparent overlay. A valid light native text
color could be rejected when those resources reported a light background,
replacing it with black over Explorer's actual dark background. Periodic
maintenance and theme/settings refreshes could trigger the change. The earlier
tests assumed the reported and rendered backgrounds agreed.

List names, metadata, group labels and shared foreground controls now use
successful 24-bit native text colors without contrast classification. Native
header colors follow the same rule. Black, white and low-contrast values are
all accepted: the mod does not paint the inherited surface.

Each tab retains its last valid native body/header colors when native lookups
fail or return invalid RGB values. Subsequent genuine native colors update
normally. Only tabs without retained native colors use app/previous/system
fallbacks. High contrast preserves Windows system colors without overwriting
the retained ordinary-theme colors. Theme fills are no longer queried for
foreground selection.

Contrast adjustment remains limited to the status button, whose opaque
background the mod actually paints. Font, DPI, accent and metric validation
from 0.9.251 remains in place.

`--styles` now tests conflicting theme/app backgrounds and native foregrounds,
32 maintenance/settings refreshes, failed and invalid lookups, recovered native
colors, high contrast and independent tabs. The GPU check draws the native
foreground over a dark target while both background resources claim white.
These are private component tests, not a live Explorer reproduction.

Validation: the new test fails on preserved 0.9.251 at the conflicting-background
foreground check. `build.bat -SourceFile 0.9.252.cpp` passes that test, the full
C++ pipeline and DLL-unload suites, repository checks and Windhawk link/stack
checks. The gate finalized 0.9.252.
