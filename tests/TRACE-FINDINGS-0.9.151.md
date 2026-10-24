# Directory view-mode transition tracing

The supplied captures start at 17:32:59 (directory changes) and 17:33:24
(same-directory changes). Both use Explorer PID 12372, owner thread 24908,
overlay HWND 0x80DC0 and tab host 0x140176.

| Measurement | Directory changes | Same directory |
| --- | --- | --- |
| Active frame gaps | 54.96, 54.15, 33.63 ms | Mostly 6 ms; transition summaries around 164–175 submissions/s |
| Shared DirectUI paint | 49.05, 46.57 ms in original native dispatch | No equivalent paint stall in the captured transition |
| Native navigation dispatch | About 244 and 316 ms before destination animation | No directory navigation |
| Mode transition setup | 0.0412 and 0.0024 ms | 0.0013 and 0.0053 ms |
| Destination animation drawing | About 0.11–0.23 ms average | About 0.1–0.72 ms average |

At QPC 236624243362–236624733910, WM_PAINT on 0x930B70 occupies native dispatch
for 49.0545 ms. The next overlay frame wake waits 47 ms, producing a 54.96 ms
frame interval. A second paint occupies 46.5732 ms and overlaps a 54.15 ms
interval. The later 33.63 ms interval includes native OLE dispatch rather than
expensive metadata delivery or GPU drawing.

A read-only HWND probe found 0x930B70 is DirectUIHWND under DUIViewWndClassName,
under ShellTabWindowClass, under CabinetWClass. Its client spans the tab canvas,
including the navigation area. It is not a descendant of the suppressed native
file-list host. Constraining or hiding it as a list viewport would remove native
navigation controls. The logs do not reveal the internal cause of its paint cost.
Windows Performance Recorder CPU profiling failed with policy error 0xc5585011;
`wpr -status` confirmed no recording remained running.

0.9.151 removes CPU paint availability from the existing mode transition's
presentation clock. Committed DirectComposition curves use the exact same QPC
origin and 180 ms cubic as CPU layout. A blocked owner can delay new thumbnails,
input and CPU submissions, but the already committed header/column/body motion
continues. This does not remove the native navigation work before the first
destination frame, nor does it claim that arbitrary application animations are
now independent of Explorer's thread.

The temporary planes belong to each overlay's own graphics device and leave on
normal settlement, target resize/recovery or teardown. Their intermediate targets
hold canonical pixels; CPU input state is restored before returning from Paint.
Text masks can reuse that stable geometry during motion. Scrollbars are a separate
untransformed chrome plane. The temporary planes include one header of overscan
so upward body displacement cannot expose missing bottom pixels. Their pixels
and transforms publish in one composition transaction; canonical mode pixels
never enter the ordinary swap chain. This avoids pairing an old visual tree
with a new bitmap and removes the swap-chain Present wait from mode frames.

The API timing contract is documented by Microsoft in
[Animation](https://learn.microsoft.com/en-us/windows/win32/directcomp/animation),
[SetAbsoluteBeginTime](https://learn.microsoft.com/en-us/windows/win32/api/dcompanimation/nf-dcompanimation-idcompositionanimation-setabsolutebegintime)
and [AddCubic](https://learn.microsoft.com/en-us/windows/win32/api/dcompanimation/nf-dcompanimation-idcompositionanimation-addcubic).
Live Explorer visual verification is separate from the private offscreen checks.
