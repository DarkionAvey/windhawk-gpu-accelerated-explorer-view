// Substitute only the clipboard API in the standalone harness. Never replace
// the user's real clipboard while testing Escape or native-view transitions.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>

struct ClipboardFixture {
    HGLOBAL effect = nullptr, drop = nullptr;
    DWORD sequence = 1;
    bool busy = false, opened = false, failEmpty = false;
    unsigned empties = 0;
    void Release() {
        if (effect) GlobalFree(effect);
        if (drop) GlobalFree(drop);
        effect = drop = nullptr;
    }
    // Simulate external clipboard producers, including plain CF_HDROP data.
    void Seed(DWORD operation, const std::wstring& path, bool preferredEffect = true) {
        Release();
        if (preferredEffect) {
            effect = GlobalAlloc(GHND, sizeof(DWORD));
            auto* value = static_cast<DWORD*>(GlobalLock(effect));
            *value = operation; GlobalUnlock(effect);
        }
        drop = GlobalAlloc(GHND, sizeof(DROPFILES) + (path.size()+2)*sizeof(wchar_t));
        auto* files = static_cast<DROPFILES*>(GlobalLock(drop));
        files->pFiles = sizeof(DROPFILES); files->fWide = TRUE;
        auto* text = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(files)+sizeof(DROPFILES));
        path.copy(text, path.size());
        GlobalUnlock(drop); ++sequence;
    }
    void SeedNonFiles() { Release(); ++sequence; }
    ~ClipboardFixture() { Release(); }
};
static ClipboardFixture testClipboard;
static DWORD WINAPI FixtureClipboardSequence() { return testClipboard.sequence; }
static BOOL WINAPI FixtureOpenClipboard(HWND) {
    if (testClipboard.busy || testClipboard.opened) return FALSE;
    testClipboard.opened = true; return TRUE;
}
static BOOL WINAPI FixtureCloseClipboard() { testClipboard.opened = false; return TRUE; }
static HANDLE WINAPI FixtureGetClipboardData(UINT format) {
    if (!testClipboard.opened) return nullptr;
    return format == CF_HDROP ? testClipboard.drop :
        format == RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT) ? testClipboard.effect : nullptr;
}
static BOOL WINAPI FixtureClipboardFormat(UINT format) {
    return format == CF_HDROP ? testClipboard.drop != nullptr :
        format == RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT) && testClipboard.effect != nullptr;
}
static BOOL WINAPI FixtureEmptyClipboard() {
    if (!testClipboard.opened || testClipboard.failEmpty) return FALSE;
    testClipboard.Release(); ++testClipboard.sequence; ++testClipboard.empties; return TRUE;
}

#define GetClipboardSequenceNumber FixtureClipboardSequence
#define OpenClipboard FixtureOpenClipboard
#define CloseClipboard FixtureCloseClipboard
#define GetClipboardData FixtureGetClipboardData
#define IsClipboardFormatAvailable FixtureClipboardFormat
#define EmptyClipboard FixtureEmptyClipboard
