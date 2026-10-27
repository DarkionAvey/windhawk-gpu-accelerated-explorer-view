# File-command input ownership (0.9.250)

The 0.9.249 frame-focus fallback sent a key to the GPU window every time
`TranslateAcceleratorW` was called. The window immediately invoked the Shell
verb. Rechecking that same input with another accelerator table, or passing it
to dispatch, invoked the verb again. A focused native list retained Explorer's
own Paste handling, explaining why the duplication required frame focus.

The previous regression was wrong: it explicitly expected two Paste calls when
the same `MSG` went through accelerator translation and dispatch. Separate
successful path checks did not establish one command per input.

The corrected regression counts commands across repeated accelerator checks,
dispatch and window delivery for one input. It also covers reentrant command
callbacks, release and immediate repress (including identical timestamps),
dispatch-only delivery, held keys, independent tabs, loading destinations and
native/editor input ownership. A real private archive supplies the virtual
`IDataObject`; clipboard, command execution and focus are substituted. These
are offscreen component checks, not a live Explorer file-operation test.

The shared file-key handler records consumption before calling Shell code or
queuing a selected-item command. Frame recovery calls that handler directly.
Hook-to-window forwarding preserves the originating message identity across
nested message loops. Duplicate checks return handled without invoking another
command, including after navigation or focus changes. Key release permits a
fresh press with the same timestamp; a later message works even if release
occurred outside the frame. This is input identity tracking, without a timing
debounce, path-specific rules or clipboard conversion.

Windows documents accelerator processing as consumption of a queued key
message: [accelerator tables](https://learn.microsoft.com/en-us/windows/win32/learnwin32/accelerator-tables).
