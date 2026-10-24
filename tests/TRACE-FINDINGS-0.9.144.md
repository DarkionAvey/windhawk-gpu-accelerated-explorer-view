# Captured input starvation in the supplied trace

Source: the user's 33,402-line capture beginning at logger time 12:08:00.154.
There are 33,236 recorded pipeline events and 18,597 explicitly reported lost
events. Missing events do not prove a stall. The log begins after tracing was
started, so the START declaration is outside this capture. Paired scope times
establish a QPC frequency of 10 MHz. Logger prefixes are asynchronous write
times; durations below use QPC event timestamps.

For overlay `0000000000080EFA`:

| Capture line | QPC | Observation |
| --- | --- | --- |
| 27066 | 41908009198 | Input ID 217 maps pointer Y=571 to destination 8632.887. |
| 29246 | 41909413889 | Input ID is still 217, although the physical cursor is at Y=841, equivalent to destination 13499.023. The actual destination is still 8632.887. |
| 29268 | 41909471687 | Input ID 218 finally maps Y=851 to destination 13679.251. |

The interval between the two delivered thumb updates is **146.249 ms**.
Within it, 24 recorded Present calls succeed. The pointer-to-target discrepancy
reaches **4866.137 document pixels**. A separate interval on this overlay lasts
128.078 ms. This is sufficient to explain the stop/catch-up motion even with
otherwise regular frame submissions. It does not attribute every prior hitch
to this cause or establish display scan-out timing.

The code previously derived thumb targets exclusively from WM_MOUSEMOVE.
Provider/frame messages can keep drawing active while those moves are delayed.
Windows' default message retrieval order gives posted messages priority over
hardware input: [Microsoft PeekMessage documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew#remarks).
The precise queue producer responsible for every delay is not established by
this capture; the stale target is directly observed.

0.9.144 samples the current cursor during an owned, held GPU capture on the
existing paced frame. It routes frame and message updates through the same
gesture handler, including both scrollbar axes, column drag/resize and marquee.
Unchanged samples do not restart easing or require a paint. Release/capture
completion retains its existing transaction and rollback paths.

The regression harness replays physical samples through private API fixtures;
it never moves the user's cursor or injects input. With all WM_MOUSEMOVE events
withheld, actual kMsgFrame callbacks track the sampled destinations. Stale
message coordinates cannot rewind them. Smooth and instant scrolling,
horizontal RTL, unchanged easing clocks, physical release, capture rollback,
marquee and independent tabs pass alongside the full existing suite and linked
Windhawk host build. These checks are not a live Explorer visual confirmation.
