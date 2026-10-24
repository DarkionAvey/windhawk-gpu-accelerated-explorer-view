# Group animation completion regression

The reported build stopped at `offscreen group animation lost native/input
geometry`. The retained executable passed six subsequent, unchanged focused
runs. Inspection and fixed-clock cases identify a timing-dependent assertion:
the offscreen test waited for document height to be within 0.02 pixels of its
target, then required the group animation to have finished.

With cubic easing, a collapse at 97.5% of its 180 ms duration already meets that
height tolerance for the test's group. The animation is still active. A test
poll landing on that frame therefore fails even though the group geometry was
retained. The replacement wait requires an accepted receipt for the current
sequence and group intent, explicit group completion, and the final height.
The original final native/input geometry assertion remains in place.

There were also two runtime inconsistencies in the same path:

- Rendering ended a group animation when **eased** floating-point progress
  rounded to one. The input owner ended it when **raw** progress reached one.
- Input geometry was reconstructed at the time the receipt was published,
  after rendering, instead of at the time its group geometry was sampled.
  Slow paint/submission could consequently advance input ahead of the image.

Both owners now use the same advancement function and raw-progress completion
rule. A receipt carries the renderer's actual group sample and completion
flag. The existing immutable intent check rejects completion from a superseded
collapse/expansion. No additional timer, wait, worker or per-item packet copy
is introduced.

## Validation

`tests/presentation-owner-smoke.h` adds fixed-clock checks for LTR/RTL and
Details/grid, in both collapse and expansion directions:

- A 97.5% frame reproduces the old height-only completion condition while
  explicitly remaining active.
- A 99.9% frame checks that easing rounded to one cannot finish the duration.
- Completion and post-completion receipts preserve final geometry.
- Arbitrarily delayed receipt publication preserves the rendered sample.
- A reversed intent rejects the preceding intent's receipt.

The focused `--presentation` suite passes, including real offscreen per-tab GPU
owners and the original offscreen-group scenario. The mandatory
`build.bat -SourceFile 0.9.223.cpp` also passes the complete repository,
Windhawk DLL link, 32 option contracts, C++/graphics/input and 21 real DLL-unload
checks. The release was published with an identical latest.cpp and numbered
archive; all 26 earlier archives retain their original hashes. Detailed local
outputs are in `tests/presentation-0.9.223.log` and `tests/build-0.9.223.log`.
These are component tests; they do not measure live Explorer's displayed frame
rate.
