# Follow-up: foreground independence in 0.9.293

The 01:42 capture samples Explorer's UI thread 1252 in PID 6580. The accompanying
log identifies this tab's presentation worker as thread 4924. Native-module PDB
identities match the earlier capture. Sample QPCs now permit direct correlation;
buffered Windhawk output timestamps are not used as event time.

## Confirmed architecture and timing

- `PresentationWorkerProc` owns a per-tab graphics state and renders without
  waiting for the UI owner to consume presentation receipts. Foreground packets
  have their own revisions and can arrive without a content model.
- Input recognition, Shell interfaces and the overlay HWND remain on the owner
  UI thread. A contact's new position cannot be processed there while that
  thread is blocked. Released animation should not require more contact input.
- `DrawSwipeFeedback` still rasterizes the bubble near the end of the full
  file-list paint. Scene preparation, icon uploads and text precede it. The
  bubble shares that frame's flush/presentation. This is rendering-work
  coupling despite the independent foreground state and separate render thread.
- The 302 retained compositor-clock observations average 14.438 ms, with a
  maximum of 17.003 ms. The prior immediate-return pacing defect is not what
  these observations show.
- Eight retained complete renderer frames take over 20 ms; the largest takes
  50.509 ms. In that frame, 37.323 ms elapses between animation advancement
  ending at QPC 223765452939 and `columns.composition.ready` at 223765826171,
  before icon upload and drawing. This brackets preparation work, not a named
  native API's exclusive execution time.

## Correlated fade example

Foreground revision `0x12C` has retained opacity 0.9971 at fade age 0.231 ms
(QPC 223653252725), then 0.3603 at 51.1732 ms (223653762146). During this interval,
the UI samples resolve to native ItemsView/DirectUI creation, tooltip creation,
and native icon/resource loading. The renderer's first retained gap begins
after a `frame.flush` begin record; its matching end was not retained.

The trace explicitly reports 47,229 dropped pipeline records. The example
therefore does not establish that no intervening frames were drawn or identify
the render thread's wait. UI-thread stacks cannot distinguish its graphics
wait, CPU scheduling delay, content preparation or lack of a renderer wake.
The renderer thread itself must be sampled to make that attribution.

## Necessary synchronization versus avoidable coupling

Window attachment/geometry, Shell COM navigation and resource teardown have
owner/lifetime requirements. The application does not intentionally require a
UI/Shell acknowledgement for every released-bubble frame. Thread separation
also does not reserve CPU/GPU time against other work.

A dedicated foreground composition visual with compositor-owned opacity and
motion would give released feedback stronger isolation from file-list painting.
DirectComposition can run property animations independently of application
threads ([Microsoft animation documentation](https://learn.microsoft.com/en-us/windows/win32/directcomp/animation)).
Conversely, DXGI documents that `Present` can wait on a message-pump thread
([Present documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)).
That is a possible mechanism, not an identification of this capture's missing
renderer stack or proof that this composition swap chain took that path.

## Next capture and validation

The portable launcher accepts `-AdditionalThreadId` and starts the UI and
additional-thread samplers concurrently. Both retain QPC timelines. For the
unchanged Explorer session in this report, use `Capture.cmd -AdditionalThreadId
4924`; a restarted renderer requires its new thread ID from the log. Return
both generated text files and the contemporaneous Windhawk log.

The launcher drains each helper's stdout/stderr asynchronously and cancels
through a shared native event, never forced termination during a stack copy.
Windows PowerShell 5.1 validation used two actual waiting threads in a private
process and a sampler path containing spaces and Unicode. Both produced 12
complete timestamped snapshots (0.511/0.479 ms aggregate pause), and the target
resumed and exited with code zero. No mod source was changed for this analysis.
