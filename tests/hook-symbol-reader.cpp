// Lists a system DLL's public symbols from its PDB, for hook-symbol-check.ps1.
// Usage: hook-symbol-reader.exe <dll> <directory holding its pdb>
// One line per symbol: the name as Windhawk's symbol hooks spell it
// (undecorated, without __ptr64), then a tab and the decorated name.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <string>
#ifndef UNDNAME_NO_PTR64
#define UNDNAME_NO_PTR64 0x20000
#endif

static BOOL CALLBACK Each(PSYMBOL_INFOW symbol, ULONG, PVOID) {
    std::wstring decorated(symbol->Name, symbol->NameLen);
    wchar_t plain[4096]{};
    if (!UnDecorateSymbolNameW(decorated.c_str(), plain, ARRAYSIZE(plain), UNDNAME_NO_PTR64))
        wcscpy_s(plain, decorated.c_str());
    // Windhawk's names omit __ptr64, which this dbghelp keeps despite the flag.
    std::wstring name(plain);
    for (size_t at; (at = name.find(L" __ptr64")) != std::wstring::npos;) name.erase(at, 8);
    // UTF-8 out: symbol names are ASCII in practice.
    std::string line;
    for (const wchar_t* text : {name.c_str(), L"\t", decorated.c_str(), L"\n"})
        for (const wchar_t* p = text; *p; ++p) line += char(*p < 128 ? *p : '?');
    fwrite(line.data(), 1, line.size(), stdout);
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_PUBLICS_ONLY | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_EXACT_SYMBOLS);
    if (!SymInitializeW(process, argv[2], FALSE)) return 3;
    const DWORD64 base = SymLoadModuleExW(process, nullptr, argv[1], nullptr, 0x10000000, 0, nullptr, 0);
    IMAGEHLP_MODULEW64 info{sizeof(info)};
    if (!base || !SymGetModuleInfoW64(process, base, &info) || info.SymType != SymPdb) { SymCleanup(process); return 4; }
    const BOOL listed = SymEnumSymbolsW(process, base, L"*", Each, nullptr);
    SymCleanup(process);
    return listed ? 0 : 5;
}
