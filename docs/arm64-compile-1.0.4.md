# ARM64 compilation fix

All three submitted CI logs stop at the unguarded `emmintrin.h` include and
its x86-only intrinsic declarations. The subsequent errors are a cascade from
that include, not evidence of separate ARM64 failures.

Windhawk's [architecture metadata documentation](https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod#architecture)
confirms that `x86-64` targets native ARM64 for an Explorer-only mod on ARM64
devices. `amd64` would narrow the existing platform selection. The working
source retains `x86-64` and guards both the header and the clipboard comparison's
SSE2 block with `__x86_64__ || _M_X64`. Other architectures use the existing
scalar prefix loop and `_wcsicmp` suffix comparison. No other explicit x86
intrinsics or assembly were found in the mod source.

Compile/link checks passed for `aarch64-w64-mingw32` and
`x86_64-w64-mingw32` using the installed Windhawk compiler and the matching
Windhawk 1.7.3_1 host import libraries. The checks compiled the real mod DLL
without editor stubs; neither DLL nor any test executable was run.
Artifacts and `compile-results.json` are retained under
`%LOCALAPPDATA%\Temp\gpu-overlay-arm64-compile-48655aaa7f874b3c8af2b59e4fdbd2b1`.

The build now requires ARM64 compile/link success before x64 compilation and
runtime suites. Release builds check both regular and minified sources. Missing
matching ARM64 host libraries are a hard failure. Architecture outputs and
logs are retained separately, and the ordinary callback stack budget includes
the ARM64 compiler's reports.

A control run used the unchanged archived `previous-versions/1.0.3.cpp` with
the new gate. It failed with the original `emmintrin.h` architecture error;
no x64 compile receipts, x64 DLL or regression executable were created.
The archive and published-source hashes stayed unchanged. The control receipts
are under `%LOCALAPPDATA%\Temp\gpu-arm64-old-version-probe-df8ad88139f14c41b92399d3fb1ca9e5`.
A separate private source with an ARM64-only deliberate compiler error also
verified the stop condition.

The other CI toolchain versions and ARM64 runtime behavior were not tested
locally. See [release validation](release-validation-1.0.4.md) for the completed gate.
