# Resize fitting and option regression coverage

The 0.9.217 negative control in `tests/options-negative-217.log` reproduces
the resize failure through real private-window WM_SIZE messages, automatic
fitting, a dedicated presentation worker and its completion receipts. Widening
the window left old column widths because the navigation readability preflight
discarded the resize request. Separately, a 12-pixel deadband prevented small
viewport changes from scheduling a fit. Resize now redistributes the viewport
through the existing fitter, with interactive completion at WM_EXITSIZEMOVE.
Unchanged native size echoes do not initiate another fit. Disabled fitting and
the navigation rule that preserves readable columns remain covered.

The clipping preflight and fitter also measured sorted captions with the
unsorted font, underestimating the bold caption's width. Both now request the
same format as painting. Independent DirectWrite layouts verify readable and
clipped headers, intrinsic widths after warming an ellipsized body layout,
changed cached layout geometry, and LTR/RTL. These checks do not assume every
long label can fit: the bounded fitter still preserves usable minimum widths
and ellipsis where the viewport cannot hold all content.

`tests/settings-fixture.h` substitutes Windhawk's settings APIs, including the
StringSetting allocation/release contract, without reading or changing installed
mod settings. `TestUserOptions` in `tests/options-smoke.h` reads the selected
source's embedded settings schema and compares its complete key/default set
with an independent expected table. All 32 options have runtime-key/default
checks and alternate-value tests. Numeric settings cover limits, out-of-range
clamping and intermediate values; booleans cover both directions and nonzero
values; string options cover every current enum choice, fallback, font trimming
and custom/accent color parsing. Adding, removing or renaming an option, or
changing a default, requires an intentional coverage update.

The same settings-host test also exercises shared geometry, real text formats,
image demand, hover/selection blending and scrolling consumers. Existing
feature fixtures supplement these binding/consumer checks:

| Options | Additional behavior coverage |
| --- | --- |
| rowHeight, fontSizePercent, fontFamily, edgeMargin | options-smoke: DPI-aware row/font/margin geometry and actual body/header/sorted-header formats; resize-text and attachment: layout and zoom |
| secondaryTextOpacity, dimDotFiles, middleEllipsis, listThumbnails | appearance, text-render, icon and grid: opacity, trimming, image selection and pixel output; options-smoke: dotfile and thumbnail demand toggles |
| zebraStripes, zebraOpacity, showEmptyRows | animation and group-heading: actual stripe readback and trailing/grouped empty areas |
| filenameOnlySelection, selectionColor, selectionOpacity, selectionRadius, gridSelectionRadius, marqueeOpacity | options-smoke: view modes, color parsing, radius and blending; layering and selection: pixels, overlap and marquee |
| animateHover, hoverOpacity, hoverSelectedOpacity | options-smoke: transition toggle and blend equations; appearance and layering: actual rendering |
| smoothScrolling, touchpadDirectManipulation, touchpadSwipeNavigation, showScrollbar, instantScrollbarScrub | options-smoke: scrollbar visibility, motion and scrub mode; pointer-sampling, RTL-window and ui-feedback: grabbed bars, item boundaries, horizontal behavior and gestures |
| folderPlacement, autoRefitColumns | live-sort: actual sort ordering; timing-readiness: data/gesture/zoom completion; options-smoke: real asynchronous window-resize fitting |
| perfLogging, frameTimeLogging, pipelineTracing, uiTransitionLogging, gpuMemoryLogging | options-smoke: independent settings/toggles/defaults; animation, frame-budget, pipeline-trace and graphics-memory: production telemetry paths |

Run `tests/run-regressions.ps1 -SourceFile <working-source> -TestArguments --options`
for focused option/clipping/resize checks. The normal release command runs these
and the existing full feature and DLL-unload suites. The attachment fixture's
warm-image assertion now waits for a real accepted presentation after a rapid
native/GPU toggle; the owner's startup-ready capability alone does not prove
that a new image exists. Windows, data, focus and
clipboard fixtures are private or substituted. Offscreen rendering and receipt
checks verify the submitted behavior; they do not measure live Explorer scan-out
or displayed frame rates.

Validation: the full `build.bat -SourceFile 0.9.218.cpp` release gate passed and
published the source; see `tests/build-0.9.218.log`. The focused navigation run
also passed after the accepted-image fixture correction. All 21 prior archives
are unchanged. Published latest.cpp and the 0.9.218 archive have SHA-256
`7ACBAE31A87D5D211E451BBFB5087E188C59E00BC7BC4217AC30FB02EA5E91B0`.
