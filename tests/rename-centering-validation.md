# List rename vertical alignment (0.9.341 working candidate)

The native multiline EDIT used top padding for every view. List rows can be
taller than their caption, leaving spare space below the text. The shared
formatting path now centers the complete native line block in nonwrapped
item layouts, using the selected HFONT's actual line height. A width change
can rewrap the EDIT, so its resulting line count settles the vertical origin.
The requested formatting rectangle remains cached to avoid selection-driven
reformatting. Grid captions retain their label-aligned origin.

`rename-vertical-alignment` is a separate `PARALLEL` manifest entry. It uses
private files, a bound Shell folder, real native EDIT APIs and substituted
focus. Its independent expectation measures the selected font with
`GetTextMetricsW` and character positions with `EM_POSFROMCHAR`, checking the
native client and row centers. It covers Details/List/Small icons, 96/144/192
DPI, LTR/RTL, wrapped names, replacement, undo, font/row-size changes and
absence of extra format/font/position messages during settled selection.

## Results

- Before the fix, the new test failed: client height 90, one text line at
  y=0..58, leaving 32 pixels below it.
- After the fix, running only `rename-vertical-alignment` passed.
- `build.bat -SourceFile 0.9.341.cpp -Jobs 16` linked both regular and minified
  Windhawk DLLs and passed the unload test. The new test passed alongside the
  suite, as did the existing rename appearance/sizing/handoff/reorder tests.
  Publication was correctly rejected: `graphics-isolation` failed its Cut to
  Copy wake assertion and `selection-checkboxes` failed deletion mirroring.
- Failure-only diagnostics were added temporarily. Both checks passed in
  isolation. A concurrent diagnostic run passed the new test again but failed
  the existing native EDIT palette paint precondition and the hidden animation
  wake assertion. These observations do not establish a cause. No assertions
  were relaxed, no tests were marked Serial, and temporary instrumentation was
  removed. These remaining suite failures still prevent release publication.

Logs are retained locally in `tests/rename-centering-before.log`,
`rename-centering-alone.log`, `rename-centering-release.log`,
`rename-centering-gate-diagnostics.log` and
`rename-centering-diagnostic-batch.log`; binaries and per-test logs are under
`tests/.build/` at the artifact paths printed in those logs.

This is offscreen component validation, not live Explorer visual validation.
`latest.cpp` remains 0.9.340; the fix remains in the unfinished 0.9.341 source.
