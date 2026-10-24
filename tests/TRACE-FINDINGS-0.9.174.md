# Remaining arrow-key breaks: input starved by paced paints

Input: user's capture beginning 18:33:07, attachment
`d78bb064-51f8-40e4-92d0-7a671948080d`. Producer QPC frequency is 10 MHz.
The capture reports 14,222 dropped records in 143 notices. Interpret accepted
events and paired spans; missing records are not evidence that no work ran.

## Evidence

There are 160 accepted Up/Down input events. Eleven have queue ages of at
least 100 ms, with a maximum of 172 ms. Frame summaries continue around
165 Hz; their maximum reported active interval is 11.51 ms. The apparent
breaks therefore need not correspond to missing render submissions.

One complete example:

| Record n | QPC | Event |
| --- | --- | --- |
| 309613 | 51140869518 | Selection retargets from visual row 100.7242 to row 97, tangent -5.1061. |
| 309615 | 51140917580 | Native selection transaction finishes in 4.8029 ms. |
| 310686 | 51142636432 | Selection frame approaches row 97 (progress 0.9816). |
| 310711 | 51142697094 | Selection reaches its 180 ms end; input counter is still 3427. |
| 310768 | 51142777166 | The next Up message finally arrives, already 172 ms old, with repeat count 6. |
| 310771 | 51142777276 | Selection starts a fresh move, row 97 to 96. |

There are eleven accepted consecutive retarget gaps between 180 and 200 ms.
The above producer interval contains a steady series of paced frame dispatches;
no paired dispatch span in the capture exceeds 8.8 ms. This distinguishes a
priority/scheduling problem from a single 180 ms provider or rendering call.

## Cause and change

The shared frame worker posts a new wake after the previous paint acknowledges
it. Every wake forces UpdateWindow, including when hardware input is waiting.
Present occupies most of a refresh interval in this capture, so the next frame
can be queued before Explorer retrieves input. That chain lasts until the
animation finishes. The previous velocity fix cannot move a highlight toward
keys which the application has not received yet.

Windows retrieves posted messages ahead of hardware input by default, and
normal WM_PAINT comes after input. See Microsoft's
[GetMessage ordering](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessage).
The high word of
[GetQueueStatus](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getqueuestatus)
includes input which is still pending even if already examined; the low word
only tracks changes. Queue status is a hint, not a guarantee of a retrievable
message, so the mod does not remove or redispatch input itself.

0.9.174 checks that current QS_INPUT mask before forcing the paced paint.
When input is pending it leaves the invalidation and frame latch outstanding.
Explorer processes input with its normal accelerator/dispatch semantics, then
the ordinary paint consumes the latest state and acknowledges the wake. The
worker cannot add another frame for that tab in between. When no input waits,
the existing synchronous paint remains to protect against provider-message
bursts. No sleep, timer, nested message pump, new hook or cross-tab queue is
introduced. The 0.9.173 motion curve and 180 ms duration remain unchanged.

A deferred paint can be hidden before USER delivers it. WM_WINDOWPOSCHANGED
releases its latch on a real hide, preventing native/tab switches from stranding
the next show. Minimize and destruction retain their existing lifecycle paths.
The opt-in `frame.yield.input` trace exposes the queue mask and current state.

## Validation and limits

Preserved 0.9.173 fails with "paced frame painted ahead of pending hardware
input". Final 0.9.174 links against Windhawk and passes the full suite,
including 18 real DLL unloads, motion/selection, native suppression, navigation,
pointer sampling and independent worker regressions.

The new fixture models pending keyboard, mouse, raw, touch and pointer status
with a zero low word (old input), without injecting anything into the user's
desktop. It verifies no synchronous paint ahead of input, retained wake/
invalidation, duplicate-frame rejection, key routing, one paint/ack after input,
and hide/show recovery. Offscreen windows need not receive synthesized paints;
the fixture delivers their retained WM_PAINT explicitly after input. Initial
fixture delivery and the focused graphics prerequisite were corrected before
the passing full run. These tests establish the scheduling contract, not live
Explorer latency or scan-out frame rate; the user's next capture can verify
that the old long input ages disappear.

Preserved 0.9.173 SHA256:
925EFF50AA0CA48342B138CD87EB559D952DB764C7B9888E00E4A1BF47F38C6C.
