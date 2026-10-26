# Remaining-space inline storage tracks

0.9.243 split the available space between labels and tracks. This preserved
alignment but made the empty label band grow with a wide Name column.

The label band now uses the widest intrinsic label width in the existing bounded
visible-row fitting sample, plus one pixel for glyph overhang. Both gaps keep
their existing DPI-scaled size. The track takes all space after the label band
and gaps. Narrow columns trim labels to retain the existing minimum track width;
columns with insufficient space omit the track.

Each tab caches the bounded label sample, font and measured maximum. Width changes
reuse that maximum. Text or font changes use the existing intrinsic shaping cache;
there are no Shell calls, full-directory scans, new workers or animation clocks.
Within a paint, all row and feedback passes share one sample and measurement.
The live/canonical column geometry correction from 0.9.243 is preserved.

`--drive-bars` checks that a 400 px width increase grows only the track, keeps its
start fixed and preserves alignment in LTR/RTL with a moved Name column. It also
checks late label changes, font changes, narrow layouts, custom DPI, filename-only
feedback, animation reversal/handoff and offscreen GPU track pixels. These private
component checks do not measure live Explorer presentation.
