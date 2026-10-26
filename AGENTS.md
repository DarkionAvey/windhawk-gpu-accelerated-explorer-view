# Repository instructions

## Source and release workflow

- `latest.cpp` is the current published Windhawk source. Read its `@version`
  header to determine the current release. `mod.cpp`, if present in the archive,
  is the preserved 0.9.112 baseline, never the development target.
- Historical sources live in `previous-versions/`. Never edit or overwrite them.
  After successful publication, the build deletes all but the five newest source
  versions, including the published version. Preserve the retained files' bytes.
- Before editing the mod, run
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/new-version.ps1`.
  This copies the newest source to the next numbered filename in the root and
  changes its version header. `-Version 0.9.N` selects an explicit newer version.
  Continue an existing unfinished working version instead of creating duplicates.
- Edit the numbered working copy only. Do not edit `latest.cpp` directly.
- Run `build.bat -SourceFile <working-version>.cpp` before the final report.
  This syncs README, links the Windhawk DLL, runs repository regressions and the
  full C++/DLL-unload suites, then moves all root version sources to
  `previous-versions/` and publishes an identical `latest.cpp`.
  Without `-SourceFile`, the command chooses the newest root working source or
  creates the next version if none exists.
- Failed tests leave the working source and diagnostic binaries available;
  they must not publish `latest.cpp`. Fix and rerun. Do not bypass this gate,
  overwrite an archive conflict, or report a release finalized after failure.
- Focused tests use `tests/run-regressions.ps1 -SourceFile <source>` and do not
  publish or archive. Always select the version being developed. Paths passed
  to scripts are repository-relative or absolute, independent of shell cwd.
- Toolchain discovery uses `-WindhawkDirectory`, `WINDHAWK_HOME`, Windows
  installed-app registration, PATH and Program Files locations. Add no local
  username, fixed drive, Codex runtime or private directory dependency.

## Documentation

- `README.md` is public user documentation and the source of the embedded
  Windhawk README. Keep it clean, minimal and focused on use and installation.
  **Do not turn README.md into a changelog, audit dump or test report.**
- Add user-readable changes to `CHANGELOG.md`. Keep technical
  investigations in `tests/` or `docs/`. Do not make it verbose.
- Do not hand-edit the embedded Windhawk README; the build replaces it from
  root `README.md`. Avoid C++ comment terminators in README content.
- Distinguish offscreen component tests and CPU submission measurements from
  live Explorer behavior and actual displayed frame rates.

## Runtime and test invariants

- Prioritize immediate names/layout and responsive input. Resolve optional
  metadata/icons on existing tab-owned lanes. Keep cold provider calls, waiting,
  cache cleanup and unbounded work off the UI thread.
- Tabs own their state, queues, caches and workers independently. Shared
  lifetime regulation manages teardown; it does not serialize tabs' work.
- Preserve native-host suppression before layout and restore native visibility
  on fallback/unload. Never expose both native and GPU lists.
- Adapt shared geometry, input, animation and enrichment paths before adding
  parallel implementations. Preserve continuous input and concurrent scrolling.
- Respect owner STA/COM boundaries, cancellation generations and callback
  draining before DLL unload. Do not forcibly terminate provider threads.
- Preserve RTL/LTR, DPI, focus, clipboard, selection and cached-data behavior.
  Tests use private temporary fixtures and substituted clipboard/focus APIs;
  they must not depend on user folders or change the user's clipboard/focus.
- Only intentional read-only OS/Shell integration probes may use real system
  resources; discover those locations through Windows APIs. Synthetic drive
  strings used as identities must not become filesystem assumptions.
