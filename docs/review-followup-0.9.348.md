# Review follow-up, working version 0.9.348

The published 0.9.347 source and historical archives remain unchanged. The
user requested targeted tests only; publication, the full regression suite,
the full-suite Jobs 16 run and DLL-unload validation are deferred.

| Finding | Resolution and focused coverage |
| --- | --- |
| Global z-order cost and blanket child demotion | Reject special requests and unknown classes before consulting the tab registry. Only ShellTabWindowClass, CtrlNotifySink and SHELLDLL_DefView content siblings qualify. Scan the existing registry under its shared lock without copying a vector. The shared layout/activation/rename stack repair also preserves unrelated surfaces above the content. `overlay-stack-filter` checks actual immediate/deferred USER transactions, unknown surfaces, lock independence and hidden overlays; `background-tab-startup` checks attached layout/rename maintenance. |
| Wheel settings | Both SPI wheel actions map to input changes; the queued handler reloads wheel lines with the same validation used at startup. `style-events` checks disabled, line and page modes without changing system settings. |
| Compositor wait error | Capture WAIT_FAILED's GetLastError immediately; retain NTSTATUS conversion for other native error statuses. `compositor-wait-errors` uses a real failed Windows wait. |
| Tooltip properties | Resolve the type's InfoTip and PreviewDetails lists instead of FullDetails plus every handler property. `item-tips` compares against Windows' actual configured lists with real JPEG/WAV metadata and cached columns. |
| Placeholder hover | Worker rechecks file attributes, avoids size providers/content handlers for offline/recall files, and uses only fast properties if fallback is necessary. `cloud-item-tip` registers a private sync root and creates a genuine dehydrated JPEG through Cloud Files APIs. It probes the handler store without OPENSLOWITEM and checks zero on-disk content before/after tooltip resolution, including stale listing attributes. This is a platform fixture, not a live OneDrive account test. |
| High-contrast selection | Store the selection accent after applying COLOR_HIGHLIGHT. `style-events` checks entry and exit from high contrast. Explicit user-selected colors retain their existing policy. |
| Drag preview property | Central constant and RemovePropW before destruction on both cleanup paths. `drag-preview-lifecycle` observes the real WM_DESTROY and checks idempotent release. |
| Stale forwarding comment | Describes the frame subclass's sent-broadcast handling. Existing `style-events` exercises this path. |
| Private message IDs | All WM_APP declarations share one numerically ordered block; existing values are preserved. No runtime behavior changed. |
| DPI fallback | Clamp both window and copied presentation DPI to 72–768. A failed GetDpiForWindow (zero) still uses 96. `resolved-styles` covers boundaries and extreme inputs. |
| Empty frame summary | Guard every frame-count denominator. `empty-frame-summary` checks the actual formatted log for finite zero averages. |
| Hold diagnostic | Gate the failure log on g_perf, matching its surrounding diagnostics. No gesture/timer behavior changed. |
| SSE2 comparator | Retained: `dense-grid-0.9.260.md` records isolated opacity lookup improving from 2.78 ms to 0.54 ms. `dense-grid-drawing` covers Unicode/case/NUL/block-boundary ordering and reports private CPU timings. Those measurements are not displayed Explorer FPS. |
| Drag caption width | Forty average characters measured in the actual DirectWrite font at the destination DPI, capped by the monitor work area. `drag-preview` checks an independent font measurement, narrow bounds and DPI scaling. Existing shape proportions remain unchanged. |
| Localized labels/counts and Back | Windows property labels and DROPDESCRIPTION remain authoritative. Badge numbers use GetNumberFormatEx plus Windows' native-digit substitution policy; formatted once per production drag. `drag-count-locale` checks English/Arabic/Persian and overflow. Back reaches only a performance-log reason argument, not displayed text. |

Comment/constant organization and the one-line logging guard do not have
separate runtime tests. No artificial gesture outputs or OS broadcasts were
added. New manifest entries declare PARALLEL and use process-private fixtures.

Validation: six new manifest entries and five extended existing regressions.
All 14 affected/adjacent entries passed individually and together with
`-Jobs 16`: `overlay-stack-filter`, `compositor-wait-errors`,
`empty-frame-summary`, `drag-count-locale`, `drag-preview-lifecycle`,
`cloud-item-tip`, `style-events`, `resolved-styles`, `drag-preview`, `item-tips`,
`dense-grid-drawing`, `background-tab-startup`, `cold-attach-wake`, and
`touch-input`. The final lazy property-store change was also checked with
standalone `--test item-tips` and `--test cloud-item-tip` invocations.
The selected source links against the Windhawk host; callback stack frames
remain within the existing 8 KiB gate (maximum 2,296 bytes). `git diff --check`
passes and all 46 private message IDs remain distinct. Artifacts were retained.

The sandbox initially denied private fixture canonicalization, desktop
creation and Cloud Files registration; the targeted tests passed with normal
Windows access. An adjacent test's old blanket-STATIC-demotion expectation
was replaced with a known content host and explicit native-popup preservation
assertions to match the revised stacking policy.

Windows API references:

- [Property store flags](https://learn.microsoft.com/windows/win32/api/propsys/ne-propsys-getpropertystoreflags): offline handlers require OPENSLOWITEM; FASTPROPERTIESONLY excludes content handlers.
- [CfCreatePlaceholders](https://learn.microsoft.com/windows/win32/api/cfapi/nf-cfapi-cfcreateplaceholders): creates the private dehydrated test file.
- [Compositor clock wait](https://learn.microsoft.com/windows/win32/api/dcomp/nf-dcomp-dcompositionwaitforcompositorclock): also documents native status returns, whose conversion remains supported.
