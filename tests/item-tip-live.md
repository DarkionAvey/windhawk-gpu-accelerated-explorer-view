# Live item tooltip check (0.9.338)

This exercises the loaded mod in a real Explorer window. The component suite
still checks worker cancellation, formatting, cached cells and popup layout;
it does not substitute for this check.

1. Run `tests/prepare-item-tip-live.ps1`. It creates a private directory with a
   real 160 × 96 BMP and a text file, without compiling or loading anything.
2. Open the returned directory in Explorer with the GPU view active. Keep the
   Size column visible in Details view.
3. Enable **Mouse hover → Detailed tooltips** in Windhawk and save the setting.
   Move the pointer onto `hover-image.bmp` and leave it resting there. If using
   input automation without a move-only operation, a native click and release
   on the item also positions the pointer; let Windows deliver its hover event.
4. Verify a visible popup appears with the filename, basic fields and image
   metadata, including dimensions. Check its size text against the column,
   absence of volume capacity/free-space fields and duplicate date values,
   and that no further metadata changes the popup after it appears.
5. Disable **Detailed tooltips**, save, move away and return to the same image.
   Verify a visible popup appears with only basic fields, without image metadata.
6. Leave the setting disabled. Close the fixture window or navigate away before
   removing only the generated fixture directory.

## Observed live result

On November 2, 2026, the user compiled and loaded 0.9.338. Native input through
Computer Use navigated the existing Explorer window to freshly generated
fixtures and positioned the pointer on the BMP. No tooltip functions or hover
messages were called directly.

- Detailed mode: visible popup with `45.0 KB`, BMP type, created date, `160 × 96`
  dimensions, width, height, bit depth and horizontal resolution. No disk fields;
  matching date and resolution values were deduplicated.
- Simplified mode, after the user changed the option: visible popup with size,
  type and one date, without image metadata. Size matched the Details column.
- The user confirmed both worked. This verifies appearance in live Explorer;
  it is not a frame-rate measurement or exhaustive multi-monitor test.
