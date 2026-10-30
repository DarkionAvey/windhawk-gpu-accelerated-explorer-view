# Navigation binding and the second layout (0.9.298)

`back log.txt` contains 31,295 retained pipeline records and 8,848 reported
drops. The records establish a second submitted layout after swipe navigation:

| Parent model | Early Saved Games row | After native rebind | Scroll change |
| --- | ---: | ---: | ---: |
| 0x40 | 12 | 10 | 460 → 386 |
| 0x42 | 12 | 10 | 460 → 386 |
| 0x44 | 12 | 10 | 460 → 386 |

For 0x40, record 214527 draws Saved Games at row 12 and record 214528 gives its
label Y as 333. After rebind and names revalidation, records 214908–214909 draw
the same path at row 10, Y 285. Both models contain 13 items. These are rendering
submissions, not physical display measurements. Later native Back sequences
0x46 and 0x48 start with the confirmed binding and select row 10 on their first
complete model, at scroll 437.

The earlier fix prevented partial prefixes at row zero. It did not keep the
same provider across the complete speculative model and its later refresh.
The old swipe test also stopped before production attachment/revalidation.

The shared optimistic navigation path discarded the destination PIDL and
started its names worker with a parsing path. `BindDirectoryProvider` then used
`SHCreateItemFromParsingName`. Once Explorer bound the actual history entry,
the worker used that native folder's PIDL through `SHCreateItemFromIDList`.
A filesystem path can expose more than one Shell provider. The existing code
already documented User Files versus the filesystem view of a profile path;
the speculative path bypassed that native-binding protection. The log does
not record provider CLSIDs, so it proves the ordering change and timing rather
than the exact provider class used on that PC.

The fix changes the shared authority boundary:

- A cached destination retains its previously captured native folder identity
  even when its names need revalidation. Early worker jobs use that identity.
- Path-only speculation waits for a Shell PIDL event or the actual native bind.
  The shared directory-request ingress enforces this for all callers.
- Shell destination events preserve their PIDL instead of discarding it during
  path conversion. An actual PIDL can upgrade or replace a pending prediction.
- Native confirmation compares folder identities as well as paths. Two bindings
  exposing the same path cannot silently share provisional rows.

No row number, folder name, sorting exception, scroll suppression window or
delay is used. The same rules apply to swipe, mouse, keyboard, breadcrumb and
Shell-event navigation through these shared paths.

The new test retains the real native parent PIDL, drives a native LTR/RTL swipe,
then runs production `AttachView` and the confirmed provider's second names scan.
It verifies identity, order hash and scroll stability across that full handoff.
The cold-load test verifies that a path-only prediction cannot start a worker,
and that supplying a real Shell PIDL resumes the existing transaction.

Focused navigation and touch suites passed. The final full release gate passed,
including rendering, native menus, provider/selection/layout tests and DLL
unload. The first full attempt stopped on an extra disabled Notepad++ menu
entry. The unchanged retained binary passed all 36 menu comparisons in
isolation; the complete gate then passed without altering the menu checks.
