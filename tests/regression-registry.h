// One manifest entry is one regression. The runner starts every entry in its
// own process, so mod globals, COM apartments, window classes, fixture files
// and workers can never leak from one test into another.
//
// Concurrency is declared, then enforced at run time:
//   * Parallel (the default). The test may run beside any other test.
//   * Serial(resources). Only for machine-wide state that no process can
//     isolate. Code that touches such state calls Claim(); a Parallel test that
//     claims fails, and a Serial test that never claims a declared resource
//     also fails. Serial is therefore neither optional nor a safe default.
// See tests/README.md "Writing a regression" before adding entries.
#pragma once
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace Regression {
enum Resource : unsigned {
    // The user's interactive input desktop: real foreground activation,
    // injected keyboard/mouse input or cursor movement. Private desktops
    // created by a test are not this resource.
    InputDesktop = 1u << 0,
    // The window station's real clipboard. Tests normally use ClipboardFixture.
    Clipboard = 1u << 1,
    // Machine/user settings: SystemParametersInfo(SPI_SET*), HKCU writes or
    // WM_SETTINGCHANGE broadcasts that other processes would observe.
    SystemSettings = 1u << 2,
    // Benchmarks whose purpose is an uncontended timing figure. Correctness
    // assertions must never depend on elapsed time.
    QuietCpu = 1u << 3,
};
struct ResourceInfo { Resource bit; const char* name; };
inline constexpr ResourceInfo kResources[] = {
    {InputDesktop, "input-desktop"}, {Clipboard, "clipboard"},
    {SystemSettings, "system-settings"}, {QuietCpu, "quiet-cpu"},
};
inline std::string ResourceNames(unsigned mask) {
    std::string names;
    for (const auto& resource : kResources) if (mask & resource.bit) {
        if (!names.empty()) names += ',';
        names += resource.name;
    }
    return names;
}

enum class Gate { Release, Focused };
struct Test {
    const wchar_t* name;
    unsigned serial;  // 0 = parallel; otherwise the resources that require it.
    Gate gate;
    void (*run)(const std::wstring& fixture);
    const char* source;
};

inline const Test* current = nullptr;
inline unsigned claimed = 0;

inline std::string Narrow(const std::wstring& text) {
    std::string result(size_t(WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
        nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), result.data(), int(result.size()), nullptr, nullptr);
    return result;
}

// Fixtures call this when they touch machine-wide state. Outside a registered
// test (helper child processes, the DLL harness) there is nothing to enforce.
inline void Claim(Resource resource, const char* use) {
    if (!current) return;
    if (!(current->serial & resource)) {
        const std::string name = ResourceNames(resource);
        throw std::runtime_error("CONCURRENCY: " + Narrow(current->name) + " uses machine-wide resource '" +
            name + "' (" + use + ") but is not declared Serial(" + name + "). Isolate it with a private "
            "desktop/fixture, or declare it in tests/regression-manifest.h");
    }
    claimed |= resource;
}

// A Serial declaration must be backed by actual use of every declared resource.
inline void CheckDeclaredResources() {
    if (!current || !(current->serial & ~claimed)) return;
    const std::string unused = ResourceNames(current->serial & ~claimed);
    throw std::runtime_error("CONCURRENCY: declared Serial(" + unused +
        ") but never used it; the test is parallel-safe, declare it Parallel");
}

// True when the calling thread is attached to the user's input desktop rather
// than a private desktop created by the test.
inline bool OnInputDesktop() {
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!input) return true;  // Locked/secure desktop: treat conservatively.
    wchar_t inputName[256]{}, ownName[256]{}; DWORD length = 0;
    const bool named = GetUserObjectInformationW(input, UOI_NAME, inputName, sizeof(inputName), &length) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, ownName, sizeof(ownName), &length);
    CloseDesktop(input);
    return !named || _wcsicmp(inputName, ownName) == 0;
}

// Activation evidence scoped to this thread and process. Another process (a
// parallel test or the user) may change the desktop foreground at any moment;
// only this process taking it, or this thread's active/focus window changing,
// is attributable to the code under test.
struct Activation {
    HWND focus = nullptr, active = nullptr;
    bool ownsForeground = false;
    static Activation Capture() {
        Activation result;
        result.focus = ::GetFocus(); result.active = ::GetActiveWindow();
        DWORD process = 0; GetWindowThreadProcessId(::GetForegroundWindow(), &process);
        result.ownsForeground = process == GetCurrentProcessId();
        return result;
    }
    bool operator==(const Activation& other) const {
        return focus == other.focus && active == other.active && ownsForeground == other.ownsForeground;
    }
};

// Nested, human-readable context printed with a crash or uncaught exception,
// e.g. "option Appearance.edgeMargin=400 / view=List / dpi=144".
inline thread_local std::vector<std::string>* context = nullptr;
struct Scope {
    std::vector<std::string> storage, *previous = context;
    explicit Scope(std::string text) {
        if (previous) storage = *previous;
        storage.push_back(std::move(text)); context = &storage;
    }
    ~Scope() { context = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
inline std::string ContextText() {
    std::string text;
    if (context) for (const auto& part : *context) { if (!text.empty()) text += " / "; text += part; }
    return text;
}
}  // namespace Regression
