# Navigation flicker and arrow-selection motion

Inputs: the user's two 2026-10-21 captures, beginning 18:12:33 and 18:13:55
(`718cda52-de8b-49e7-9c3c-bcd65ab5ef38` and
`7c3ce7a0-0aee-4cdc-b475-6f6b015a8f61`). Their asynchronous writer timestamps
do not measure producer duration; use QPC/span fields. Both captures report
dropped records, so missing events are not proof of inactivity.

## Navigation evidence

The first trace alternates F:\ Details (mode 4) and This PC Tiles (mode 6).
Header mode animations are expected. Independently, WATCH_SURFACE records the
same GPU window becoming invisible and then visible during This PC handoffs:
18:12:35.976–18:12:36.027, 18:12:39.718–18:12:39.747 and
18:12:58.367–18:12:58.417. The last WATCH_HIDDEN_END reports 47 ms.
This is actual HWND visibility loss, separate from thumbnail readiness.

The source has two transient handoff paths that explicitly hide a valid GPU
surface: PositionOverlayOverNative's pending geometry/suppression branch and
ReportShellNavigationEvent's early CREATE suppression failure. Both retry
later, so the persistent surface unnecessarily disappears during the gap.
The old trace does not record which visibility caller ran; the component
regression reproduces the geometry case directly.

The This PC attachment branch also treats a replacement native HWND for the
same namespace as a new directory: it clears committed entries, capacities,
selection and animation state before asynchronous names arrive again.
Filesystem attachment already distinguishes binding confirmation from a
different directory. Apply that distinction to This PC as well.

0.9.173 keeps the active tab's committed surface over transient host/geometry
failures, retaining suppression ownership and the existing paced retry.
Actual native-mode choice, unsupported views and inactive tabs still hide it.
Same-namespace This PC replacement retains its model and animation while the
existing names worker revalidates identities. No readiness delay is added.

## Keyboard evidence and scope

In the second capture C:\Windows has 117 rows, warm text, negligible enrichment
work and draw averages around 0.37–0.44 ms. Most reported submission cadences
are near 165 Hz; there are some late intervals (including a 27.21 ms maximum).
Native selection spans repeatedly cost roughly 3–8 ms. This does not support
attributing the whole rubbery appearance to slow thumbnail or metadata work.

The old code preserves *position* when an arrow retargets, then restarts an
ease-out cubic whose initial speed is 3*new_distance/180 ms. This resets speed
on each keyboard repeat even at perfect frame cadence. For example, movement
4→5 interrupted at 33 ms has speed about 0.0111 rows/ms; retargeting to 6
restarts near 0.0257 rows/ms, over twice the previous speed.

0.9.173 adapts the shared keyboard selection geometry to a cubic Hermite curve
with the same 180 ms duration and zero arrival velocity. Fresh moves retain
the original cubic. Repeated moves carry elapsed position and tangent in Y and
both X edges. Tangents are clamped to monotone bounds for reversals/shorter
destinations, avoiding overshoot. Selection/native command identities remain
immediate. Single-item native selection combines focus/selection mark with the
selection transaction, avoiding the second focus transaction/verification;
multi-selection's existing verified fallback remains.

The old capture lacks highlight positions/velocities, so it cannot prove that
this mathematical discontinuity accounts for every visible jitter. New trace
events expose retarget/frame/stop geometry and actual visibility requests;
START includes the installed mod version. They use the existing opt-in,
nonblocking tab-owned trace writer.

## Validation

The new transient-host regression fails on preserved 0.9.172 with
"transient host geometry hid the active GPU surface during navigation".
0.9.173 passes the focused --navigation suite, including repeated zero-size
handoffs and same-namespace native replacement without clearing This PC rows.
The renderer regression verifies elapsed velocity continuity and monotonic
180 ms arrival after repeated/reversed selection input. The selection fixture
checks a single native transaction with matching selected/focused PIDLs.
Full-suite results are recorded in AGENTS.md and tests/README.md after running
the final source. These offscreen checks do not establish live Explorer feel.

Preserved 0.9.172 SHA256:
8267F0068027D353F6E33D99C385A30EE8C5797C1FB59D48B248896EDC143703.
