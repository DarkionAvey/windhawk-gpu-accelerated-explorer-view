# Touch release velocity and repeated flings, 0.9.276

The previous path forwarded native inertia deltas, but discarded all remaining
momentum when the next contact arrived. Its regression required only 0.1 pixel
of movement after release and then interrupted the coast. That could not assess
release strength, full travel, stopping behavior or successive flings.

Frame-paced contacts sent through the real Windows recognizer produced complete
native coasts in this environment: applied distance matched native output.
That does not reproduce or explain the reported touchscreen feel on hardware.
It does establish the missing additive behavior and the earlier test's limits.

## Released motion

Windows still recognizes contacts and supplies release velocity. At the native
transition into inertia, read its effective translation deceleration before
stopping that interaction. `StopInteractionContext` resets the effective value;
reading it before inertia starts also yields zero on this host. Unsupported or
invalid values retain the native inertia path.

Translation uses screen-pixel measurement units. For speed `v` in pixels/ms and
native deceleration `a` in pixels/ms², the stopping time is `v/a`. The existing
compositor clock evaluates cumulative distance `v*t - a*t*t/2` from the release
packet's QPC timestamp, clamped at stopping time. Only the difference since the
previous frame goes through the shared pan/bounds/overscroll path. It does not
scale drag distance or accumulate integration error from animation cadence.
Native touchpad inertia keeps its existing path.

A new DOWN immediately stops displayed coasting and saves its remaining
velocity. That saved velocity continues to decay during contact. At the next
release, add it only when the committed axis and release direction agree.
Reversal, perpendicular movement, taps, cancellation and navigation cannot
inherit unrelated momentum. No speed multiplier, friction constant, extra timer
or time window is introduced. History navigation remains a finger-release
decision and cannot start from coasting.

## Verification

Calibrated raw contacts enter the production input boundary and real Ninput
recognizer. A Windows waitable timer paces contact samples using the view's
display-derived frame interval, with actual QPC timestamps. Tests run the real
owner message queue, compositor scheduler and renderer presentation lane, then
compare the complete applied coast with independently observed native release
velocity and deceleration. They require final renderer acknowledgement.

Scenarios compare short fast flicks against three-times-longer slow drags, verify
the decayed velocity sum and increased travel after successive flings, catch
motion on DOWN, and reject opposing/perpendicular carry. Both vertical
directions, 96/192 DPI and LTR/RTL horizontal scrolling are covered. Existing
native recognition, hold/drag, pinch, history and capture-loss checks remain.
No Windows input messages or recognizer outputs are manufactured. Focus and
desktop side effects are substituted; these tests do not establish touchscreen
driver delivery, displayed frame rates or physical-device feel.

The full `build.bat -SourceFile 0.9.276.cpp` gate passed, including installed
Windhawk linking, repository regressions, the complete C++ suite and actual DLL
unloads. Published, generated and archived 0.9.276 source hashes match. The 14
retained older archives are byte-identical; the release retains 15 versions.

Windows API references:

- [ProcessInertiaInteractionContext](https://learn.microsoft.com/en-us/windows/win32/api/interactioncontext/nf-interactioncontext-processinertiainteractioncontext)
- [GetInertiaParameterInteractionContext](https://learn.microsoft.com/en-us/windows/win32/api/interactioncontext/nf-interactioncontext-getinertiaparameterinteractioncontext)
- [SetInertiaParameterInteractionContext](https://learn.microsoft.com/en-us/windows/win32/api/interactioncontext/nf-interactioncontext-setinertiaparameterinteractioncontext)
