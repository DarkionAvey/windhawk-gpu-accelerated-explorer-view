// Included after the mod and its fixture #undefs, so it applies to test code
// only. Machine-wide APIs claim their Regression::Resource (see
// regression-registry.h); a Parallel test that calls one fails immediately.
// Prefer per-thread/process observations: Regression::Activation, FocusFixture,
// ClipboardFixture, PointerFixture and private desktops need no claim.
#pragma once
#include "regression-registry.h"

namespace Regression::Guarded {
inline HWND WINAPI ForegroundWindow() {
    if (OnInputDesktop()) Claim(InputDesktop, "GetForegroundWindow");
    return ::GetForegroundWindow();
}
inline BOOL WINAPI SetForeground(HWND hwnd) {
    if (OnInputDesktop()) Claim(InputDesktop, "SetForegroundWindow");
    return ::SetForegroundWindow(hwnd);
}
inline UINT WINAPI Input(UINT count, LPINPUT inputs, int size) {
    if (OnInputDesktop()) Claim(InputDesktop, "SendInput");
    return ::SendInput(count, inputs, size);
}
inline BOOL WINAPI Cursor(int x, int y) {
    if (OnInputDesktop()) Claim(InputDesktop, "SetCursorPos");
    return ::SetCursorPos(x, y);
}
inline BOOL WINAPI Block(BOOL block) { Claim(InputDesktop, "BlockInput"); return ::BlockInput(block); }
inline BOOL WINAPI Clipboard(HWND owner) { Claim(Regression::Clipboard, "OpenClipboard"); return ::OpenClipboard(owner); }
inline BOOL WINAPI Parameters(UINT action, UINT value, PVOID data, UINT update) {
    // Reads never pass update flags; persisting or broadcasting is machine-wide.
    if (update) Claim(SystemSettings, "SystemParametersInfo with SPIF flags");
    return ::SystemParametersInfoW(action, value, data, update);
}
inline BOOL WINAPI Post(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (hwnd == HWND_BROADCAST) Claim(SystemSettings, "PostMessage(HWND_BROADCAST)");
    return ::PostMessageW(hwnd, message, wp, lp);
}
}  // namespace Regression::Guarded
#define GetForegroundWindow Regression::Guarded::ForegroundWindow
#define SetForegroundWindow Regression::Guarded::SetForeground
#define SendInput Regression::Guarded::Input
#define SetCursorPos Regression::Guarded::Cursor
#define BlockInput Regression::Guarded::Block
#define OpenClipboard Regression::Guarded::Clipboard
#define SystemParametersInfoW Regression::Guarded::Parameters
#define PostMessageW Regression::Guarded::Post
