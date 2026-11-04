# Release 1.0.1 validation

Published on 2026-11-04 using the repository release publisher. The regular
source export, `latest.cpp` and archived `1.0.1.cpp` are byte-identical. The
minified export matches the compiled candidate and retains its read-only flag.

The complete release build linked regular, minified and unload-test DLLs,
checked minification and callback stack budgets, and ran all 111 release tasks
with `-Jobs 16`. Of these, 110 passed in that run. Native-menu interaction passed
on its first subsequent isolated run using the same binary. Publication used
the user's explicit acceptance of combined parallel and isolated passes; the
initial failure remains in the original results CSV. Source, README and
minified hashes were checked before publication.

Earlier persistent failures were corrected in test fixtures: discovery now
prepares its lazy text-runtime prerequisite; the scheduler-stop check precedes
fixtures that start it; Shell overlay expectations include the new cap; and
the synthetic overlay pixel check measures filtered edges rather than assuming
opaque edge pixels. These checks passed both in isolation and in the final
parallel run. The unused search Boolean wrapper was removed; its tests use the
production prefix-length function directly.

Other installed Windhawk mods were observed inside an earlier private test
process. Test executables were temporarily excluded with user approval. The
original exclusion setting was restored and the Windhawk service verified
running afterward. No assertion was disabled and concurrency declarations were
unchanged. Component/GPU checks do not establish displayed Explorer frame rates.

Published source SHA-256:
`117249EBB3BE69D9E17C979CBFCFE2DB67504563B3D19C73FF3A46FCFBA538B6`.
