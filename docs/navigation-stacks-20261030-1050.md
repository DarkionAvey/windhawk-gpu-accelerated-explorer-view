# 10:50 capture and 10:51 Windhawk log (0.9.293)

The attached files describe different intervals and different owner UI threads.
QPC frequency is 10,000,000 in both files. Mapping through the capture's recorded
UTC FILETIME gives these local times (+01:00):

| Data | PID | UI thread | Renderer sampled | Event interval |
| --- | --- | --- | --- | --- |
| `Explorer-UI-stacks-20261030-105027-pid6448-tid2276.txt` | 6448 | 2276 | None | 10:50:27.386–10:50:43.301 |
| `windhawk.txt` | 6448 | 2716 | None (renderer 12496 emits trace records) | 10:51:22.442–10:51:40.001 |

The last sample is QPC 255961809609; the first pipeline record is 256353225244,
39.142 seconds later. Buffered log display timestamps are not gesture times.
There is no contemporaneous renderer stack in these attachments. A narrow
search for sibling capture files for this PID/timestamp found only thread 2276.

## What the log establishes

- 41,910 pipeline records were retained and 46,738 were explicitly dropped.
  Missing begin/end records or missing opacity samples cannot prove a stall.
- 228 retained compositor-clock waits average 14.748 ms, maximum 16.802 ms.
  They do not show the former immediate-return pacing defect.
- Sixteen complete native `0x700` handlers take 114.301–185.853 ms
  (mean 153.181 ms). These are UI 2716's handlers, not sampled UI 2276's.
- Retained renderer frames after startup include durations of 30–53 ms.
  Startup includes a 159.758 ms frame: about 102 ms before icon upload,
  41.190 ms in the label span, 8.786 ms flushing and 6.859 ms presenting.
  These are elapsed submission measurements; they do not identify exclusive
  API CPU cost or physical displayed frame rate.
- Committed foreground revision `0x2A` records eight decreasing opacity poses
  at fade ages 0.26–73.01 ms (opacity approximately 0.997–0.087).
  Each has a subsequent retained `render.present` with HRESULT S_OK about
  1.7–2.1 ms later. This demonstrates a progressing committed fade in this
  example, not physical display of every frame or success of every gesture.
  Other revisions have sparse retained opacity records, insufficient to locate
  their missing work given the trace loss.

The bubble remains rasterized in the content renderer's frame as described in
`navigation-stacks-0.9.293-followup.md`. Its state is independently preserved,
but thread separation alone does not remove that rendering dependency. These
new files do not distinguish renderer scheduling, graphics waits or content
preparation during the reported skipped animation. No speculative production
change was made; the published mod remains 0.9.293.

## Capture workflow correction

The portable launcher's default previously captured only the UI thread unless
an additional thread ID was explicitly supplied. The updated launcher requires
renderer selection for normal animation diagnosis. A saved current Windhawk
log supplies the renderer for the selected process, overlay HWND and owner UI;
an unmatched window is rejected. An explicit current thread ID remains usable.
An intentional UI-only capture now requires `-UiOnly`.

The selected window/tab is checked again before sampling. Both helpers start
concurrently, use cooperative cancellation, and their outputs are bundled into
one ZIP. Instructions distinguish the initial log used to discover the renderer
from the new log saved after the captured swipes. No mod update is required.

Windows PowerShell 5.1 validation resolved the actual attached log to UI 2716 /
renderer 12496, rejected UI 2276 and a missing overlay, and ran the actual
parallel sampler against two private native waiting threads. Both produced 12
complete timestamped snapshots (0.541/0.491 ms aggregate pause, 0.057/0.059 ms
maximum pause), were included in one ZIP, and the target resumed/exited normally.
This checks the diagnostic tool, not live Explorer animation smoothness.
