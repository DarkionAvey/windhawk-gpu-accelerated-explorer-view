# Keyboard/controller selection and stationary pointers

The supplied handheld trace delivers Up/Down as `WM_KEYDOWN` (`VK_UP` /
`VK_DOWN`). Its mouse samples remain at client `(269,432)`, including duplicate
`WM_MOUSEMOVE` deliveries after Enter. A mouse message alone therefore cannot
establish renewed pointer intent.

Previously, scrolling and navigation refreshed the hovered row beneath that
stationary position even after keyboard navigation took over. In single-click
mode, Windows hover notifications could then select the hovered row through
the same pointer selection path.

When no current, remembered or deletion caret exists, the first Up/Down key
uses the hovered item if present, or the first displayed item otherwise.
Resolve that target before clearing hover. The initial
0.9.303 working candidate incorrectly removed this rule; the correction retains
it while suppressing subsequent stationary hover. Remembered/deletion carets
still take priority over hover.

Keyboard input then clears hover and cancels Windows hover tracking. Each tab
retains the sampled screen position: passive refreshes and duplicate mouse
messages stay suppressed until that position changes. Direct mouse button or
wheel input also restores pointer interaction. Queued hover notifications cannot
select while suppression is active. Remembered selection, deletion carets,
Ctrl/Shift navigation and Shell drop-target feedback retain their existing paths.

The selection and GPU-pixel component tests check list/grid initial hover seeds,
first-item fallback (including a previously suppressed pointer),
stationary-pointer suppression, selection opacity, actual sampled pointer
movement and the existing Windows capture/release path. They use private cursor
and focus fixtures; these checks do not establish live handheld Explorer behavior.

## Release gate

The full build linked the Windhawk DLL and passed callback-stack, DLL-unload,
selection and GPU hover/selection-pixel checks. It then failed the existing
`native view confirmation erased cached group headers before revalidation`
assertion. The isolated `--navigation` run reproduces this failure on both the
candidate and an unchanged Git copy of published 0.9.302 with its original tests.
Failure-only diagnostics observed zero sections/row mappings after confirmation,
three cached row mappings before it, and byte-different folder PIDLs (although
Windows `ILIsEqual` returned true). The existing binding guard resets the rows.
That guard has not been relaxed as part of the input fix. The release remains
unpublished; `latest.cpp`, the Windhawk export and retained history are unchanged.
After restoring initial hover seeding, `--selection` completed with exit code
zero (`tests/keyboard-hover-303-seed-selection.log`). The corrected full build
passed the GPU-pixel and unload checks before failing the same assertion
(`tests/keyboard-hover-303-seed-release.log`). All 15 retained source hashes and
the published copies were verified unchanged after the failed gate.
