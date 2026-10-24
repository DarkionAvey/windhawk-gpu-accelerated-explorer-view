# Restore animation frame starvation, 0.9.214

## Trace evidence

The supplied 8.07 MB trace is from 0.9.213, GPU window 0x180AFA on Explorer
thread 12756. QPC, rather than buffered log writer wall-clock prefixes, is
used for the following measurements (10 MHz in this capture):

- Restore grows the model from 0 to 62 rows. Scan/model delivery remains short;
  the update scopes do not explain the long interval without a presentation.
- At QPC 123630816049, 62 rows enter model commit. The next accepted
  presentation is QPC 123640323830: **950.778 ms later**.
- Before that commit, frame 2652 yields at QPC 123630109469 with
  queue mask 2 (`QS_MOUSEMOVE`). It holds `framePaintPending` and its compositor
  wake acknowledgement while waiting for ordinary `WM_PAINT`.
- No further paced wake can be posted for that held frame. The ordinary paint
  finally runs after native posted-message traffic subsides. Frame 2653 then
  takes only 2.7144 ms CPU; its Present scope takes 0.0862 ms. These are CPU
  timings and accepted submissions, not display frame-rate measurements.
- Interleaved native message 0xB04 dispatches last 124.54, 328.30 and 246.69 ms.
  A read-only same-process/thread window lookup maps that HWND, 0xB0B66, to
  `SysTreeView32` in the navigation pane. The GPU list is a different HWND.
  The 964 ms right-button scope includes the user's open context menu and is
  not interpreted as a 964 ms renderer stall.

The scheduling failure is therefore distinct from the short directory-update
latency fixed in 0.9.213: yielding once to real input unintentionally yielded
to every subsequently queued native post as well.

## Live sampling and limits

The user reproduced restore during a 1,000-sample read-only UI-thread capture
(`tests/restore-ui-stacks-214.log`). Samples include native Shell filesystem
notifications, ribbon command-state rebuilding, selection devirtualization,
native DirectUI updates and GPU/driver work. They do not isolate the internal
cause of each long navigation-tree 0xB04 dispatch. No claim is made that those
native calls themselves have been shortened or moved to a worker.

Each sample copies at most 128 KiB of stack and resumes the thread before
unwinding or resolving symbols. Total measured pause was 42.866 ms over 1,000
samples, maximum 0.160 ms. A second 800-sample capture after adding matching
Microsoft symbols was predominantly idle and is not used to explain the
restore stall. The manual sampler now also prints common-control/storage
stacks, and the symbol helper accepts discovered absolute module paths,
including the loaded side-by-side common-controls DLL.

## Fix

Keep the existing input-first handoff and one outstanding paced-frame latch.
When a frame yields to queued hardware input it additionally records that
specific reason for deferral. An owner-STA completion scope captures only the
overlay HWND, lifetime and folder/view token before input is delivered.

- After DispatchMessage processing (including mod-handled input), completion
  submits the one held paint using the existing `UpdateWindow`/WM_PAINT path.
- If TranslateAccelerator consumes the input, it runs the same completion
  after the accelerator and its native-view/command scopes finish. Returning
  unhandled leaves the paint waiting for actual dispatch of that input.
- Queued native filesystem posts can remain queued. The completion does not
  remove them, pump other messages, synthesize input, change focus or wait for
  queue idle. It does not request an additional paced frame.
- One existing WM_PAINT advances animation, paints the current model and
  acknowledges the held frame normally. Ordinary paint delivery remains valid.
  Repeated completion calls cannot duplicate it.
- Navigation, HWND reuse, tab hiding, unsupported modes, active graphics
  operations/initialization, recursive painting and unload are guarded. State
  is not dereferenced after `UpdateWindow`, which may destroy or rebind it.

No quiet timer, age threshold or guessed native-operation completion is added.
The elapsed field is diagnostic only. `frame.resume.input[deferredMs]` under
the existing pipelineTracing toggle records the resumed frame's deferred time
and input message. Logging defaults remain off.

This shared scheduling path applies to row insertion/removal, smooth scrolling,
grid/zoom/selection motion and other frame-driven painting. It leaves the
completion-driven directory update/cache validation from 0.9.213 intact.
An individual synchronous native call can still block Explorer's UI thread;
this fix removes the extra queue-idle starvation after input completes.

## Validation

The actual GPU animation regression queues 128 native-style posts for each
keyboard/mouse/raw/touch/pointer input mask. Input still runs first; the shared
completion presents/acknowledges exactly one frame with all 128 posts still
queued. A repeated completion cannot paint twice. Concurrent wheel/collapse,
selection, thumb dragging and hidden/show recovery remain covered.

The private modal/selection fixture additionally exercises the actual
DispatchHook and TranslateAcceleratorHook paths: an unhandled accelerator
does not paint, actual input dispatch does, a consumed accelerator does, and
an obsolete view token does not paint a replacement directory. Its lightweight
paint receiver tests callback ordering separately from the actual GPU test.
Tests substitute clipboard/focus/input APIs and use private files/windows.

Focused graphics and final complete release validation passed: repository
workflow, installed Windhawk DLL linking, all C++/graphics/input tests and 21
actual DLL unloads. The maximum compiled critical callback stack frame remains
1,928 bytes. Twenty warm 4K tabs retain 1,483.52 MiB local GPU memory and return
to 9.86 MiB after close; cached attachment CPU peaks at 0.6367 ms in the private
offscreen fixture. All seventeen older archives and README retain their hashes,
and the published source matches the tested 0.9.214 archive.
Full output is in `tests/build-0.9.214.log`.
These tests establish the scheduling contract, not live displayed frame rates
or elimination of the separately observed native Shell/control stalls.
