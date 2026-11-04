# Release 1.0.3 validation

The final release build compiled the regular and minified Windhawk DLLs,
unload-test DLL and both test hosts. Repository publication checks,
minification and the callback stack limit passed (maximum 2,248 bytes).

The full suite ran 111 tasks with `-Jobs 16`: 109 passed. `item-tips` failed
creating a fixture under the temporarily nested temp path; `graphics-isolation`
failed its intermediate-frame assertion. Both passed on their first isolated
reruns using the same executable and normal temp path. All release tasks thus
passed under the user's explicit combined-run acceptance rule. Original
results and separate isolated receipts are retained; assertions were not relaxed.
The full rename matrix passed in 270 seconds, within its normal timeout.

Activation/selection, provider navigation and delegated watch checks also passed
independently on the final executable. The strengthened native travel-log test
passed alone before the full run and again in parallel. It checks canonical
Shell identity and immediate row/order/viewport preservation through native
return confirmation and refresh.

Earlier investigation distinguished a real delegated-target watch regression
and equivalent-PIDL navigation reset from fixture mistakes: modifier observation
used the wrong keyboard API and the touch fixture skipped the production child
rebind. Initial 180-second isolated rename limits were too short; validation
used the repository's normal timeout. Temporary diagnostic tracing was removed.
These component results do not establish live Explorer timing or reproduce the
return issue in a live user tab.

Final receipts and artifacts are retained under
`%LOCALAPPDATA%\Temp\gpu-overlay-tests-b0ca4ed9b17a425d8a904acebfc939d3\gpu-overlay-tests-099f95cf5a624048b1f290c40d21b356`.
`release-acceptance.json` records complete manifest coverage and combined results.
Source and README hashes were verified unchanged before publication. The source
SHA-256 is `9AE29C251D1511847D85B1B87E7D4D515993BB6968FDF31DD57BACE0584567CF`.

Unrelated installed Windhawk mods were observed in early private test processes.
The previously authorized temporary exclusion was restricted to this build's
private artifact directory. The original empty exclusion was restored and the
Windhawk service verified running before publication.

The published source, archive and regular export are byte-identical. Regular
and minified exports use CRLF; the minified export remains read-only. Historical
source retention and preservation follow the repository release workflow.
