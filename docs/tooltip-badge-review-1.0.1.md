# Tooltip and section input follow-up

Changes are in the unfinished `1.0.1.cpp`; no build or regression execution was
performed, as requested. `latest.cpp` and historical sources remain unchanged.

- ICU's capitalization value was correct for the current ABI. Calls now use
  named declarations from ICU headers when available. Windhawk's bundled
  toolchain lacks these headers, so a small compatibility declaration remains.
  Using a symbol does not make a compiled enum dynamically adapt to ABI changes.
- Relative dates retain local calendar-day semantics across DST transitions.
  Larger units count completed months with the anniversary clamped to the
  target month's last valid day, including its wall-clock time. Before a full
  month, elapsed weeks are used. At twelve completed months, approximate years
  are rounded to the nearest year. Past and future dates use the same policy.
- Badge input geometry now receives the accepted paint's sample through the
  existing presentation receipt, alongside its immutable badge map. Merely
  passing the owner's old `itemOpacityNow` would have been insufficient: it was
  previously populated only on the rendering lane. All badge layout callers
  must now supply an explicit sample.
- The old 512-key badge retention cap was already removed. Retention follows
  current section topology, with existing coverage exceeding 512 sections.
- Unmodified collapse-direction arrows focus and reveal the selected item's
  section header, then collapse it on the next press. Expansion is immediate.
  Both list and grid views mirror the directions for RTL. Existing modified
  navigation and item selection remain intact.

Existing component coverage was extended for date boundaries, accepted receipt
clock transfer, delayed badge hit/hover geometry and both keyboard directions
in LTR/RTL list/grid views. These checks have not been executed.

API references:

- [ICU display-context declarations](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/udisplaycontext_8h_source.html)
- [ICU relative-date formatter](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ureldatefmt_8h.html)
