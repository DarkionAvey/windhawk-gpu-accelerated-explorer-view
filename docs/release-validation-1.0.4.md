# Release 1.0.4 validation

The new mandatory ARM64 phase compiled and linked both the regular and minified
mod DLLs before x64 compilation. Both outputs have ARM64 PE machine type 0xAA64;
the regular and minified x64 outputs have machine type 0x8664. All seven compile
tasks passed. Minification, repository checks and the callback stack budget
passed; the maximum critical callback frame across both targets was 2,248 bytes.

The unchanged archived 1.0.3 source was tested with the new runner. Its
unguarded SSE headers reproduced the original ARM64 error. The runner stopped
before creating any x64 compilation receipts, x64 DLL or regression executable.
Archive and publication hashes were unchanged by this control. A private source
with a deliberate ARM64-only error independently verified the same failure gate.
See [ARM64 investigation](arm64-compile-1.0.4.md) for the control receipts.

The complete x64 runtime/unload suite ran 111 tasks at `-Jobs 16`: 109 passed.
`native-menu-interaction` failed its displayed popup comparison and
`graphics-isolation` failed its intermediate-frame assertion. Both passed on
their first isolated reruns with the same binary. All release tasks therefore
passed under the user's combined-run acceptance policy, with original results
and separate isolated logs retained. No assertions were relaxed.

Artifacts, architecture compile receipts, original/isolated runtime results
and `release-acceptance.json` are retained under
`%LOCALAPPDATA%\Temp\gpu-overlay-tests-c49a0ded71f64166907e9907b486a0fe`.
Source, README and runner hashes stayed unchanged through validation. The
published source SHA-256 is
`9CE247BFB633A606C0F4A088CA7AC5BE75862A18F98092C508428D3DCA9FFD9F`.

The previously authorized Windhawk exclusion was restricted to this build's
private artifact directory. The original empty setting was restored and the
service verified running before publication. Regular source, archive and export
are byte-identical; regular/minified exports use CRLF, and minified remains
read-only. The 15-version archive retention preserves retained source bytes.

ARM64 binaries were compile/link checked, not executed on ARM64 hardware. The
other CI Windhawk toolchain versions were not locally tested. Component timings
and x64 integration tests do not establish live Explorer frame rates.
