# Marquee stacking

The marquee was painted in the background pass, beneath item selection,
icons, labels, drive capacity bars and group headings. It now paints after
content on the ordinary target or the existing transition chrome plane,
before scrollbars. Navigation bubbles retain their persistent foreground
composition siblings above the content root.

The marquee uses the restored live viewport during mode/column transitions.
Its clip includes pinned group headings and the empty selection area while
excluding column headers. Document anchoring, physical RTL pointer coordinates,
focus suppression, opacity and fading keep their existing behavior.

The graphics regressions read back active/fading marquee pixels in LTR/RTL
Details/List/grid, pinned heading text, live mode bounds and row chrome planes.
These are offscreen component/source checks, not live Explorer screenshots.

The optimized native `--graphics` run passed, including private fixture cleanup.
`build.bat -SourceFile 0.9.301.cpp` linked the Windhawk DLL and passed repository
and DLL-unload checks, then stopped at the previously documented navigation
assertion: `native view confirmation erased cached group headers before revalidation`.
No release was published; `latest.cpp` remains 0.9.300 and retained archive
hashes are unchanged. See [the existing investigation](rename-0.9.301.md).
