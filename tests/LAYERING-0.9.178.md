# Highlight stacking

The user isolated the apparent opacity jump to selection fills: setting
selectionOpacity to zero removed it. Item opacity was not changing in the
isolated navigation check because the fault was in composition order.

The temporary mode scene constructed its background first, then item and
metadata layers, header, and chrome, using `AddVisual(child, TRUE, nullptr)`.
With a null reference, that call places each new visual underneath every
sibling. The background therefore covered names/icons during mode animation.
Returning to the normal renderer restored its correct foreground drawing order,
making the content abruptly look opaque again.

This is the documented [IDCompositionVisual::AddVisual contract](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-addvisual).
The new shared AppendCompositionVisual operation uses FALSE with a null
reference, appending each visual above the preceding layers. Both ordinary
layers and the header use it. No surface, effect, worker or frame is added.

The ordinary renderer already draws row selection, hover, drop-target and
focus feedback before item contents. Its marquee fill/outline was the exception,
drawn after content; it now uses the background phase as well. Full viewport
coverage, document anchoring, fading and input/selection behavior remain.
Actual foreground chrome (scrollbars and navigation gesture feedback) stays on
the existing chrome surface, using the logical viewport geometry.

The new layering test reads real GPU pixels from Details/List/grid views in
LTR and RTL. Opaque label/icon pixels must survive selection, hover, overlap,
drop-target highlight, caret, marquee and fading marquee unchanged. Preserved
0.9.177 fails that pixel check. The test also replays prepared composition
planes using the production append operation and a model of Windows' documented
null-reference sibling contract; backgrounds must precede foreground content.
That replay verifies the submitted ordering contract, not DWM screen pixels or
scan-out. Live Explorer confirmation remains separate.
