# Release 1.0.2 validation

The release build compiled regular, minified and unload-test DLLs plus both
test hosts. Repository publication checks, minification and the 8 KiB callback
stack limit passed (maximum measured callback frame: 2,248 bytes).

The complete suite ran 111 tasks with `-Jobs 16`: 110 passed, and
`graphics-isolation` failed with "deletion has no intermediate submitted frame".
Its first isolated rerun passed using the same compiled binary. All tasks
therefore passed under the user's explicit combined-run acceptance rule.
The original failure receipt is preserved; no assertion or test was relaxed.
Render quality, presentation ownership, rename appearance and DLL unload passed.

Artifacts and original/isolated result CSVs are retained at
`%LOCALAPPDATA%\Temp\gpu-overlay-tests-d62f1fe5b0784fe9aef57806da351fcf`.
Source and README hashes were unchanged between compilation and publication.
The source SHA-256 is
`A942A6562C70C74A851A7348493C9DEFF84CD084591AD383749095463FC23243`.

An initial sandbox attempt stalled in the runner's intentional timeout check;
the verified build processes were stopped and the build rerun outside the
sandbox. The successful preliminary checks exercised timeout handling normally.
Unrelated installed Windhawk mods were observed in private test processes.
Previously authorized temporary test exclusions were applied for the isolated
rerun, then the original empty exclusion was restored and the Windhawk service
verified running. The slow rename and live-sweep tasks completed successfully.

These component and Shell integration tests do not establish displayed
Explorer frame rates or confirm behavior on the other affected PC.
