// Resolves "[MenuTrace n] stack[i] module+0xOFFSET" lines of a saved Windhawk
// log (tests/menu-decision-trace.wh.cpp) to function names. Modules are loaded
// from the paths the trace logged; symbols come from the given directory
// (tests/.symbol-cache, filled by fetch-windows-symbols.ps1).
// Usage: menu-trace-symbolizer.exe <log> <symbol directory> <output>
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace {
std::wstring Widen(const std::string& text) {
    std::wstring out(MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), int(out.size()));
    return out;
}
std::string Narrow(const std::wstring& text) {
    std::string out(WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), int(out.size()), nullptr, nullptr);
    return out;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { fwprintf(stderr, L"usage: menu-trace-symbolizer <log> <symbols> <output>\n"); return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) { fwprintf(stderr, L"cannot read %s\n", argv[1]); return 2; }
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

    const HANDLE session = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x4D454E55)); // Any unique value.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_EXACT_SYMBOLS);
    if (!SymInitializeW(session, argv[2], FALSE)) { fwprintf(stderr, L"SymInitialize failed: %lu\n", GetLastError()); return 1; }

    // Module paths, as logged, keyed by file name; each loaded at its own base.
    std::map<std::wstring, DWORD64> bases;
    const std::regex moduleLine(R"(\[MenuTrace\] module (.+?)\r?$)");
    DWORD64 next = 0x100000000ull;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        std::smatch match;
        if (!std::regex_search(line, match, moduleLine)) continue;
        const std::wstring path = Widen(match[1].str());
        const wchar_t* leaf = wcsrchr(path.c_str(), L'\\');
        std::wstring name = leaf ? leaf + 1 : path;
        for (auto& c : name) c = towlower(c);
        if (bases.count(name)) continue;
        const DWORD64 base = SymLoadModuleExW(session, nullptr, path.c_str(), nullptr, next, 0x10000000, nullptr, 0);
        if (!base) { fwprintf(stderr, L"no image for %s (%lu)\n", path.c_str(), GetLastError()); continue; }
        IMAGEHLP_MODULEW64 info{sizeof(info)};
        SymGetModuleInfoW64(session, base, &info);
        fwprintf(stderr, L"%s: %s\n", name.c_str(), info.SymType == SymPdb ? L"symbols" : L"exports only");
        bases[name] = base;
        next += 0x10000000;
    }

    const std::regex frame(R"(stack\[(\d+)\] ([^ +]+)\+0x([0-9A-Fa-f]+))");
    std::ofstream output(argv[3], std::ios::binary);
    std::istringstream again(text);
    for (std::string line; std::getline(again, line);) {
        std::smatch match;
        if (std::regex_search(line, match, frame)) {
            std::wstring name = Widen(match[2].str());
            for (auto& c : name) c = towlower(c);
            const auto found = bases.find(name);
            if (found != bases.end()) {
                const DWORD64 address = found->second + std::stoull(match[3].str(), nullptr, 16);
                alignas(SYMBOL_INFOW) char storage[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)]{};
                auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(storage);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
                symbol->MaxNameLen = 512;
                DWORD64 displacement = 0;
                if (SymFromAddrW(session, address, &displacement, symbol)) {
                    char suffix[64];
                    snprintf(suffix, sizeof(suffix), "+0x%llX", static_cast<unsigned long long>(displacement));
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    line += "  " + Narrow(symbol->Name) + suffix + "\r";
                }
            }
        }
        output << line << "\n";
    }
    SymCleanup(session);
    return 0;
}
