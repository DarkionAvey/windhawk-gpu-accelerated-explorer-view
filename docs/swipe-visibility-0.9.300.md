# Repeated swipe visibility, 0.9.300

The `164111.778 6708 explorer.exe [WH].txt` capture reports the first bubble
visible and later gestures invisible. Retained diagnostics show later bubbles
inside the viewport, with raster alpha 1, the same root/foreground visual and
unchanged overlay placement. No graphics failure or foreground reset is
retained. The capture drops records, so it cannot alone prove compositor output.

## Comparison with 0.9.285

| Versions | Bubble drawing and fade ownership |
| --- | --- |
| 0.9.285–0.9.294 | Bubble pixels are drawn into each content frame/scene. No persistent foreground opacity effect is reused. |
| 0.9.295–0.9.299 | Independent persistent foreground visuals survive content updates. Successful release attaches an opacity animation ending at zero; the next gesture attempts to restore the same effect with `SetOpacity(1.f)`. |
| 0.9.300 | The native effect is attached only for successful release. Tracking/retraction detaches it; completed cancellation removes content from the visual. Surfaces and visuals remain reusable. |

The relevant lifetime change first appears in 0.9.295. The 0.9.291 gesture
geometry/reversal changes and the later history-binding changes do not explain
the native effect behavior reproduced below. Reverting foreground isolation
would bring back the earlier content-paint and navigation dependencies.

## Native reproduction

A nonactivating, DPI-aware window hosted blue content and a red foreground
surface using the production API sequence. Actual composed screen pixels were
sampled only inside that fixture. The first engage displayed red and its 80 ms
fade finished blue. The second engage returned success from `SetOpacity(1.f)`
but remained blue throughout a three-second observation window. Foreground
ownership was preserved. This is a compositor visibility check, not a desktop
frame-rate measurement.

Although the documented
[static opacity setter](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositioneffectgroup-setopacity(float))
should remove an existing animation, the observed sequence did not restore
visible pixels. Detaching the effect with `SetEffect(nullptr)` restored all
three successive engage/fade cycles on the same visual and surface.

The fix gives the effect an explicit lifetime matching its purpose: the 80 ms
successful release. Tracking and cancellation already encode opacity in their
shared raster pose and need no compositor opacity effect. Hiding a canceled
visual uses `SetContent(nullptr)` so hiding also works while no effect is
attached. No new timer, opacity toggle, per-gesture visual allocation or
content-model coupling is introduced.

## Verification

`tests/run-regressions.ps1 -SourceFile <source> -TestArguments '--swipe-compositor'`
is an explicit on-screen integration probe. It drives native Ninput contacts
through the production gesture and `RenderSwipeFeedback` paths and reads
actual DWM pixels. Its small private windows are nonactivating; focus APIs are
substituted and no desktop touch/mouse input is injected. It covers repeated
successful fades, both directions, cancellation and subsequent reuse with empty
and populated models in LTR/RTL. Ordinary full regressions stay offscreen.

Against 0.9.299, the production-path probe displayed gesture 0 and failed on
gesture 1 because the reused bubble never became visible. Against 0.9.300, all
20 gestures and their fade/cancellation exits passed. Both runs checked the
actual compositor output, rather than flattening CPU-expected opacity over a
bitmap. These fixtures do not measure live Explorer navigation smoothness.

The full 0.9.300 release gate also passed repository checks, native input,
foreground lifetime/reversal/navigation tests, graphics/layout/menu regressions
and the DLL-unload suites.
