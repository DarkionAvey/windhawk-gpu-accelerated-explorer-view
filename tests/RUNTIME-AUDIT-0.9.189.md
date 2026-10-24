# Runtime audit — 0.9.189

This audit targets behavior visible to the user: tab isolation, shutdown safety,
UI latency, resource recovery, and cache memory. It reviews the worker inventory,
queue publication/cancellation, COM ownership, directory/icon/text caches, GPU
resource creation/release, paced rendering, callback lifetimes and native-host
suppression. Presentation and naming conventions are outside its scope.

## Findings and fixes

| Finding | User impact | Change |
| --- | --- | --- |
| Dedicated-worker startup called `CreateThread` while holding the process-wide lifetime registry mutex. Private pool startup already used a tab-owned gate. | A slow cold thread creation could delay another tab's startup or closure through that shared lock. | Register the lifetime briefly, then create the thread under the same per-tab submission gate used by pools and Stop. Stop and handle publication remain mutually exclusive. Worker exception shutdown no longer needs the registry lock. |
| Navigation cancellation and Shell icon invalidation destroyed decoded thumbnail buffers on the UI thread, including while holding the icon queue lock. The existing receive/eviction paths already had deferred cleanup. | Cancelling a cold thumbnail burst, changing folders or invalidating cached icons could introduce heap-release stalls. | One pixel-retirement method moves buffer ownership into the existing per-tab cleanup path. Navigation, individual-item invalidation, whole-cache invalidation, receipt and eviction share it. GPU resources remain on their owning UI thread. Cancellation retains associations when appropriate and preserves unrelated request generations/IDs. |
| Folder snapshot estimates omitted property-cell text allocations and PIDLs, and sampled only the first 24 entries. | Metadata-heavy histories could substantially exceed their configured cache budget, especially when the first rows were unresolved. | Snapshot and resolved-property accounting share one entry-payload estimator. The snapshot sample stays bounded at 24 entries, spread across the vector. It includes cell strings and immutable identities; shared identities are conservatively charged. |
| Text LRU, raster-budget eviction and geometry-variant replacement released glyph resources inside the text-drawing path. Font changes already deferred these releases. | Resource release could add unpredictable work to an input/animation frame. | All three eviction paths reuse the existing after-paint retirement queue and its 0.25 ms cleanup slice. Text shaping, masks and live colors retain their current behavior. |
| Early GPU-device/factory creation failure returned with partial resources retained. A subsequent initialization could overwrite populated COM output pointers. | Repeated initialization failures could retain device resources and make recovery less reliable. | The early failure path uses the existing complete target teardown before returning. A later attempt starts from a clean resource graph. |

Source entry points: [worker startup](../previous-versions/0.9.189.cpp#L1947),
[pixel retirement/cancellation](../previous-versions/0.9.189.cpp#L4841),
[payload accounting](../previous-versions/0.9.189.cpp#L6620),
[text cache](../previous-versions/0.9.189.cpp#L9693),
[GPU initialization](../previous-versions/0.9.189.cpp#L11018), and
[icon invalidation](../previous-versions/0.9.189.cpp#L18260).

## Ownership retained

- Each tab retains its own jobs, private provider pools, generation counters,
  caches and graphics device. The lifetime regulator still performs final joins
  outside the UI thread during ordinary tab closure.
- Dedicated Shell lanes keep their own STA and message pump. Pool callbacks
  initialize/uninitialize COM within their callback; provider interfaces do not
  cross into the UI cache. Immutable identities and plain pixel/value snapshots
  continue to cross thread boundaries.
- Threadpool teardown waits for actual callback return. API/window/COM lifetime
  guards and deferred owner-thread ViewState destruction remain in place.
- Icon/property completion and uploads retain their existing frame budgets and
  nonblocking delivery. Native-host suppression, selection ownership and animation
  durations are unchanged by this audit.

## Validation

Final `0.9.189.cpp` linked against the installed Windhawk host and passed the full
standalone regression runner, including 18 actual DLL unloads.

The added checks exercise:

- OS thread creation deliberately paused in one tab while a second tab starts.
  Both starts and all test threads are released/joined before any assertion can
  fail.
- Cancelled, item-invalidated and globally invalidated thumbnail buffers retaining
  their exact allocation pointers until background retirement, with associations
  and unrelated in-flight ownership preserved.
- A folder whose large property strings occur after its first 24 entries being
  rejected by the existing byte-budget policy.
- Evicted geometry-variant and shaped-value glyph masks remaining in the paced
  retirement queue, followed by successful cleanup.
- An injected composition-device allocation failure releasing all partial GPU
  resources, then successfully recovering on an independent offscreen HWND.

The first graphics-only attempt exposed a fixture error: its recovery attempt
reused a window already owning a composition target. The fixture now uses its
own window. No production behavior was changed to accommodate that fixture.

These are isolated ownership, failure and rendering checks. They do not establish
live Explorer frame rates or guarantee the behavior of arbitrary third-party Shell
extensions. Cold device/font setup and explicit native Shell commands can still
take time on the owner thread; this audit does not replace those with a new
asynchronous graphics or command architecture. A provider that never returns still
requires retaining its worker/module lifetime rather than forcibly terminating it.

Preserved `0.9.188.cpp` SHA256:
`2D45AE139E7E207A3227E39ACA91B226A49549C62CFDCF1C62ED543E9FBC78C1`.
