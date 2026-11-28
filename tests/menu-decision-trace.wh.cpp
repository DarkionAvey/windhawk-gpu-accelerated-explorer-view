// ==WindhawkMod==
// @id              explorer-menu-decision-trace
// @name            Explorer menu decision trace
// @description     Temporary diagnostic: how File Explorer chooses its item or folder context menu
// @version         0.1
// @author          DarkionAvey
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lsynchronization
// @license         GPL-2.0
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Temporary diagnostic for tests/menu-decision-probe.cpp. Enable debug logging,
run tests/run-menu-decision-probe.ps1, then save the log and disable this mod.

Logs only while the probe runs (it owns the named event below), and only for
the probe's own folder. Every call is passed through unchanged. Each logged
call carries its stack as module+offset; tests/run-menu-decision-probe.ps1
-Symbolize resolves those to function names with Microsoft's public symbols.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <initializer_list>

namespace {
constexpr wchar_t kActiveEvent[] = L"Local\\ExplorerMenuDecisionProbe";
constexpr wchar_t kPhaseMapping[] = L"Local\\ExplorerMenuDecisionProbePhase";
constexpr wchar_t kFolderMarker[] = L"menu-decision-probe-";
std::atomic<bool> stopping{false};
std::atomic<unsigned> callbacks{0};
std::atomic<unsigned long long> sequence{0};
thread_local bool inside = false;

struct PreserveError {
    DWORD value = GetLastError();
    ~PreserveError() { SetLastError(value); }
};
// While the probe runs (its event exists), every line is also appended to
// %TEMP%\menu-decision-probe.log, so a run can be read without saving the
// Windhawk log. _vsnwprintf keeps Wh_Log's format (%s is a wide string).
void ProbeFile(const wchar_t* format, ...) {
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, kActiveEvent);
    if (!event) return;
    CloseHandle(event);
    static SRWLOCK lock = SRWLOCK_INIT;
    wchar_t line[2048];
    va_list args;
    va_start(args, format);
    int length = _vsnwprintf(line, ARRAYSIZE(line) - 2, format, args);
    va_end(args);
    if (length < 0) length = ARRAYSIZE(line) - 2;
    line[length++] = L'\r'; line[length++] = L'\n';
    char utf8[4096];
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, length, utf8, sizeof(utf8), nullptr, nullptr);
    wchar_t path[MAX_PATH]{};
    if (!GetTempPathW(ARRAYSIZE(path), path) || wcscat_s(path, L"menu-decision-probe.log")) return;
    AcquireSRWLockExclusive(&lock);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, static_cast<DWORD>(bytes), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);
}
#define Log(...) do { PreserveError preserve; Wh_Log(__VA_ARGS__); ProbeFile(__VA_ARGS__); } while (0)

struct Callback {
    Callback() { callbacks.fetch_add(1, std::memory_order_acquire); }
    ~Callback() {
        PreserveError preserve;
        if (callbacks.fetch_sub(1, std::memory_order_release) == 1) WakeByAddressAll(&callbacks);
    }
};
// Nested hooked calls (the Shell calling itself) are still traced; only the
// trace's own Shell queries are not.
struct Inside {
    bool previous = inside;
    Inside() { inside = true; }
    ~Inside() { inside = previous; }
};

// The probe signals its event while it runs and names the current step.
bool ProbeActive(wchar_t* phase, size_t length) {
    if (stopping.load(std::memory_order_acquire)) return false;
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, kActiveEvent);
    if (!event) return false;
    const bool active = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    CloseHandle(event);
    if (!active) return false;
    phase[0] = 0;
    if (HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kPhaseMapping)) {
        if (const auto* text = static_cast<const wchar_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) {
            wcsncpy_s(phase, length, text, _TRUNCATE);
            UnmapViewOfFile(text);
        }
        CloseHandle(mapping);
    }
    return true;
}

// An ID that belongs to the probe: its parsing name carries the marker (a
// folder path, or a search-ms: location scoped to the probe's folder).
bool MarkedId(PCIDLIST_ABSOLUTE id) {
    PWSTR name = nullptr;
    if (!id || FAILED(SHGetNameFromIDList(id, SIGDN_DESKTOPABSOLUTEPARSING, &name)) || !name) return false;
    const bool marked = wcsstr(name, kFolderMarker) != nullptr;
    CoTaskMemFree(name);
    return marked;
}
// The probe's view (its folder or search location carries the marker).
bool ProbeFolder(IUnknown* object) {
    if (!object || inside) return false;
    Inside guard;
    IFolderView* view = nullptr;
    IShellView* shell = nullptr;
    bool probe = false;
    if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&view))) && view) {
        IPersistFolder2* folder = nullptr;
        if (SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&folder))) && folder) {
            PIDLIST_ABSOLUTE id = nullptr;
            if (SUCCEEDED(folder->GetCurFolder(&id)) && id) probe = MarkedId(id);
            CoTaskMemFree(id);
            folder->Release();
        }
        view->Release();
    } else if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&shell))) && shell) {
        shell->Release();
    }
    return probe;
}
bool ProbeShellFolder(IShellFolder* folder) {
    if (!folder || inside) return false;
    Inside guard;
    IPersistFolder2* persist = nullptr;
    bool probe = false;
    if (SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&persist))) && persist) {
        PIDLIST_ABSOLUTE id = nullptr;
        if (SUCCEEDED(persist->GetCurFolder(&id)) && id) probe = MarkedId(id);
        CoTaskMemFree(id);
        persist->Release();
    }
    return probe;
}

void Iid(REFIID riid, wchar_t* text, int length) {
    static const struct { const IID* iid; const wchar_t* name; } known[]{
        {&IID_IContextMenu, L"IContextMenu"}, {&IID_IContextMenu2, L"IContextMenu2"},
        {&IID_IContextMenu3, L"IContextMenu3"}, {&IID_IDataObject, L"IDataObject"},
        {&IID_IShellItemArray, L"IShellItemArray"}, {&IID_IDropTarget, L"IDropTarget"},
        {&IID_IExtractIconW, L"IExtractIconW"}, {&IID_IQueryInfo, L"IQueryInfo"},
        {&IID_IQueryAssociations, L"IQueryAssociations"}, {&IID_IShellLinkW, L"IShellLinkW"},
        {&IID_IDispatch, L"IDispatch"}, {&IID_IEnumIDList, L"IEnumIDList"}};
    for (const auto& item : known)
        if (IsEqualIID(riid, *item.iid)) { wcsncpy_s(text, length, item.name, _TRUNCATE); return; }
    StringFromGUID2(riid, text, length);
}

// One module path line per module, then module+offset frames.
void Stack(unsigned long long id) {
    PreserveError preserve;
    void* frames[48]{};
    const USHORT count = CaptureStackBackTrace(2, ARRAYSIZE(frames), frames, nullptr);
    static HMODULE seen[64]{};
    static unsigned seenCount = 0;
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH]{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(frames[i]), &module) && GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
            bool known = false;
            for (unsigned k = 0; k < seenCount; ++k) known |= seen[k] == module;
            if (!known && seenCount < ARRAYSIZE(seen)) {
                seen[seenCount++] = module;
                Log(L"[MenuTrace] module %s", path);
            }
            Log(L"[MenuTrace %llu] stack[%u] %s+0x%llX", id, i, PathFindFileNameW(path),
                static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(frames[i]) - reinterpret_cast<ULONG_PTR>(module)));
        } else Log(L"[MenuTrace %llu] stack[%u] address=%p", id, i, frames[i]);
    }
}
unsigned long long Enter(const wchar_t* phase, const wchar_t* call, const wchar_t* detail) {
    const auto id = sequence.fetch_add(1) + 1;
    Log(L"[MenuTrace %llu] phase=%s %s %s tid=%lu", id, phase, call, detail, GetCurrentThreadId());
    Stack(id);
    return id;
}

using GetItemObject_t = HRESULT (STDMETHODCALLTYPE*)(IShellView*, UINT, REFIID, void**);
using ItemCount_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView*, UINT, int*);
using Items_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView*, UINT, REFIID, void**);
using FocusedItem_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView*, int*);
using SelectedItem_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView2*, int, int*);
using GetSelection_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView2*, BOOL, IShellItemArray**);
using SelectionState_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView2*, PCUITEMID_CHILD, DWORD*);
using GetUIObjectOf_t = HRESULT (STDMETHODCALLTYPE*)(IShellFolder*, HWND, UINT, PCUITEMID_CHILD_ARRAY, REFIID, UINT*, void**);
using CreateDefaultMenu_t = HRESULT (WINAPI*)(const DEFCONTEXTMENU*, REFIID, void**);
GetItemObject_t originalGetItemObject = nullptr;
ItemCount_t originalItemCount = nullptr;
Items_t originalItems = nullptr;
FocusedItem_t originalFocusedItem = nullptr;
SelectedItem_t originalSelectedItem = nullptr;
GetSelection_t originalGetSelection = nullptr;
SelectionState_t originalSelectionState = nullptr;
GetUIObjectOf_t originalGetUIObjectOf = nullptr;
CreateDefaultMenu_t originalCreateDefaultMenu = nullptr;

HRESULT STDMETHODCALLTYPE GetItemObjectHook(IShellView* self, UINT item, REFIID riid, void** out) {
    Callback callback;
    wchar_t phase[64], iid[64], detail[160];
    unsigned long long id = 0;
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        Iid(riid, iid, ARRAYSIZE(iid));
        swprintf_s(detail, L"svgio=0x%X iid=%s", item, iid);
        id = Enter(phase, L"IShellView::GetItemObject", detail);
    }
    const HRESULT hr = originalGetItemObject(self, item, riid, out);
    if (id) Log(L"[MenuTrace %llu] -> hr=0x%08X", id, static_cast<unsigned>(hr));
    return hr;
}
HRESULT STDMETHODCALLTYPE ItemCountHook(IFolderView* self, UINT flags, int* count) {
    Callback callback;
    const HRESULT hr = originalItemCount(self, flags, count);
    wchar_t phase[64], detail[96];
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        swprintf_s(detail, L"svgio=0x%X -> %d hr=0x%08X", flags, count ? *count : -1, static_cast<unsigned>(hr));
        Enter(phase, L"IFolderView::ItemCount", detail);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE ItemsHook(IFolderView* self, UINT flags, REFIID riid, void** out) {
    Callback callback;
    const HRESULT hr = originalItems(self, flags, riid, out);
    wchar_t phase[64], iid[64], detail[160];
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        Iid(riid, iid, ARRAYSIZE(iid));
        swprintf_s(detail, L"svgio=0x%X iid=%s hr=0x%08X", flags, iid, static_cast<unsigned>(hr));
        Enter(phase, L"IFolderView::Items", detail);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE FocusedItemHook(IFolderView* self, int* item) {
    Callback callback;
    const HRESULT hr = originalFocusedItem(self, item);
    wchar_t phase[64], detail[96];
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        swprintf_s(detail, L"-> %d hr=0x%08X", item ? *item : -1, static_cast<unsigned>(hr));
        Enter(phase, L"IFolderView::GetFocusedItem", detail);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE SelectedItemHook(IFolderView2* self, int start, int* item) {
    Callback callback;
    const HRESULT hr = originalSelectedItem(self, start, item);
    wchar_t phase[64], detail[96];
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        swprintf_s(detail, L"start=%d -> %d hr=0x%08X", start, item ? *item : -1, static_cast<unsigned>(hr));
        Enter(phase, L"IFolderView2::GetSelectedItem", detail);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE GetSelectionHook(IFolderView2* self, BOOL noneImpliesFolder, IShellItemArray** out) {
    Callback callback;
    const HRESULT hr = originalGetSelection(self, noneImpliesFolder, out);
    wchar_t phase[64], detail[96];
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        swprintf_s(detail, L"noneImpliesFolder=%d hr=0x%08X", noneImpliesFolder, static_cast<unsigned>(hr));
        Enter(phase, L"IFolderView2::GetSelection", detail);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE SelectionStateHook(IFolderView2* self, PCUITEMID_CHILD item, DWORD* state) {
    Callback callback;
    const HRESULT hr = originalSelectionState(self, item, state);
    wchar_t phase[64], detail[96];
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeFolder(self)) {
        swprintf_s(detail, L"-> 0x%lX hr=0x%08X", state ? *state : 0, static_cast<unsigned>(hr));
        Enter(phase, L"IFolderView2::GetSelectionState", detail);
    }
    return hr;
}
// Each child: its ID size and the full item's parsing name (how the folder
// identifies items of other locations, such as search results).
void Children(unsigned long long id, IShellFolder* folder, UINT count, PCUITEMID_CHILD_ARRAY children) {
    if (!children) return;
    Inside guard;
    IPersistFolder2* persist = nullptr;
    PIDLIST_ABSOLUTE parent = nullptr;
    CLSID cls{};
    if (SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&persist))) && persist) {
        persist->GetCurFolder(&parent);
        persist->GetClassID(&cls);
        persist->Release();
    }
    wchar_t clsText[64]{};
    StringFromGUID2(cls, clsText, ARRAYSIZE(clsText));
    Log(L"[MenuTrace %llu]   folder class=%s", id, clsText);
    for (UINT i = 0; i < count && i < 4; ++i) {
        PIDLIST_ABSOLUTE full = parent ? ILCombine(parent, children[i]) : nullptr;
        PWSTR name = nullptr;
        if (full) SHGetNameFromIDList(full, SIGDN_DESKTOPABSOLUTEPARSING, &name);
        Log(L"[MenuTrace %llu]   child[%u] idsize=%u name=%s", id, i, ILGetSize(children[i]), name ? name : L"?");
        CoTaskMemFree(name);
        ILFree(full);
    }
    CoTaskMemFree(parent);
}
HRESULT STDMETHODCALLTYPE GetUIObjectOfHook(IShellFolder* self, HWND owner, UINT count, PCUITEMID_CHILD_ARRAY children,
                                            REFIID riid, UINT* reserved, void** out) {
    Callback callback;
    wchar_t phase[64], iid[64], detail[160];
    unsigned long long id = 0;
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeShellFolder(self)) {
        Iid(riid, iid, ARRAYSIZE(iid));
        swprintf_s(detail, L"items=%u iid=%s owner=%p", count, iid, owner);
        id = Enter(phase, L"IShellFolder::GetUIObjectOf", detail);
        Children(id, self, count, children);
    }
    const HRESULT hr = originalGetUIObjectOf(self, owner, count, children, riid, reserved, out);
    if (id) Log(L"[MenuTrace %llu] -> hr=0x%08X", id, static_cast<unsigned>(hr));
    return hr;
}
// The search results folder's own GetUIObjectOf (a separate implementation).
GetUIObjectOf_t originalSearchGetUIObjectOf = nullptr;
HRESULT STDMETHODCALLTYPE SearchGetUIObjectOfHook(IShellFolder* self, HWND owner, UINT count, PCUITEMID_CHILD_ARRAY children,
                                                  REFIID riid, UINT* reserved, void** out) {
    Callback callback;
    wchar_t phase[64], iid[64], detail[160];
    unsigned long long id = 0;
    if (ProbeActive(phase, ARRAYSIZE(phase)) && ProbeShellFolder(self)) {
        Iid(riid, iid, ARRAYSIZE(iid));
        swprintf_s(detail, L"items=%u iid=%s owner=%p", count, iid, owner);
        id = Enter(phase, L"SearchFolder::GetUIObjectOf", detail);
        Children(id, self, count, children);
    }
    const HRESULT hr = originalSearchGetUIObjectOf(self, owner, count, children, riid, reserved, out);
    if (id) Log(L"[MenuTrace %llu] -> hr=0x%08X", id, static_cast<unsigned>(hr));
    return hr;
}
HRESULT WINAPI CreateDefaultMenuHook(const DEFCONTEXTMENU* definition, REFIID riid, void** out) {
    Callback callback;
    wchar_t phase[64], iid[64], detail[160];
    unsigned long long id = 0;
    if (ProbeActive(phase, ARRAYSIZE(phase)) && definition && ProbeShellFolder(definition->psf)) {
        Iid(riid, iid, ARRAYSIZE(iid));
        swprintf_s(detail, L"items=%u iid=%s", definition->cidl, iid);
        id = Enter(phase, L"SHCreateDefaultContextMenu", detail);
    }
    const HRESULT hr = originalCreateDefaultMenu(definition, riid, out);
    if (id) Log(L"[MenuTrace %llu] -> hr=0x%08X", id, static_cast<unsigned>(hr));
    return hr;
}

bool Hook(void* target, void* detour, void** original, const wchar_t* name) {
    const bool hooked = target && Wh_SetFunctionHook(target, detour, original);
    Log(L"[MenuTrace] hook %s %s at %p", name, hooked ? L"installed" : L"FAILED", target);
    return hooked;
}
}

BOOL Wh_ModInit() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return FALSE;
    void* view[8]{};
    void* folder = nullptr;
    void* search = nullptr;
    {
        // The Shell's view implementation, from a windowless instance (no I/O).
        IShellFolder* desktop = nullptr;
        if (SUCCEEDED(SHGetDesktopFolder(&desktop)) && desktop) {
            SFV_CREATE create{sizeof(create)};
            create.pshf = desktop;
            IShellView* shell = nullptr;
            if (SUCCEEDED(SHCreateShellFolderView(&create, &shell)) && shell) {
                view[0] = (*reinterpret_cast<void***>(shell))[15]; // IShellView::GetItemObject
                IFolderView2* folderView = nullptr;
                if (SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&folderView))) && folderView) {
                    void** table = *reinterpret_cast<void***>(folderView);
                    view[1] = table[7];  // IFolderView::ItemCount
                    view[2] = table[8];  // IFolderView::Items
                    view[3] = table[10]; // IFolderView::GetFocusedItem
                    view[4] = table[31]; // IFolderView2::GetSelectedItem
                    view[5] = table[32]; // IFolderView2::GetSelection
                    view[6] = table[33]; // IFolderView2::GetSelectionState
                    folderView->Release();
                }
                shell->Release();
            }
            desktop->Release();
        }
        // The file system folder implementation, from the temporary folder.
        wchar_t temp[MAX_PATH]{};
        PIDLIST_ABSOLUTE id = nullptr;
        IShellFolder* fileSystem = nullptr;
        if (GetTempPathW(ARRAYSIZE(temp), temp) && SUCCEEDED(SHParseDisplayName(temp, nullptr, &id, 0, nullptr)) && id &&
            SUCCEEDED(SHBindToObject(nullptr, id, nullptr, IID_PPV_ARGS(&fileSystem))) && fileSystem) {
            folder = (*reinterpret_cast<void***>(fileSystem))[10]; // IShellFolder::GetUIObjectOf
            fileSystem->Release();
        }
        // The search results folder implementation: a search scoped to the
        // temporary folder (nothing is searched until it is enumerated).
        ISearchFolderItemFactory* factory = nullptr;
        IShellItem* scope = nullptr;
        IShellItemArray* scopes = nullptr;
        IShellItem* results = nullptr;
        IShellFolder* searchFolder = nullptr;
        if (id && SUCCEEDED(CoCreateInstance(CLSID_SearchFolderItemFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(SHCreateItemFromIDList(id, IID_PPV_ARGS(&scope))) &&
            SUCCEEDED(SHCreateShellItemArrayFromShellItem(scope, IID_PPV_ARGS(&scopes))) &&
            SUCCEEDED(factory->SetScope(scopes)) && SUCCEEDED(factory->GetShellItem(IID_PPV_ARGS(&results))) &&
            SUCCEEDED(results->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&searchFolder))) && searchFolder)
            search = (*reinterpret_cast<void***>(searchFolder))[10]; // IShellFolder::GetUIObjectOf
        for (IUnknown* object : std::initializer_list<IUnknown*>{searchFolder, results, scopes, scope, factory})
            if (object) object->Release();
        CoTaskMemFree(id);
    }
    CoUninitialize();
    Hook(view[0], reinterpret_cast<void*>(GetItemObjectHook), reinterpret_cast<void**>(&originalGetItemObject), L"IShellView::GetItemObject");
    Hook(view[1], reinterpret_cast<void*>(ItemCountHook), reinterpret_cast<void**>(&originalItemCount), L"IFolderView::ItemCount");
    Hook(view[2], reinterpret_cast<void*>(ItemsHook), reinterpret_cast<void**>(&originalItems), L"IFolderView::Items");
    Hook(view[3], reinterpret_cast<void*>(FocusedItemHook), reinterpret_cast<void**>(&originalFocusedItem), L"IFolderView::GetFocusedItem");
    Hook(view[4], reinterpret_cast<void*>(SelectedItemHook), reinterpret_cast<void**>(&originalSelectedItem), L"IFolderView2::GetSelectedItem");
    Hook(view[5], reinterpret_cast<void*>(GetSelectionHook), reinterpret_cast<void**>(&originalGetSelection), L"IFolderView2::GetSelection");
    Hook(view[6], reinterpret_cast<void*>(SelectionStateHook), reinterpret_cast<void**>(&originalSelectionState), L"IFolderView2::GetSelectionState");
    Hook(folder, reinterpret_cast<void*>(GetUIObjectOfHook), reinterpret_cast<void**>(&originalGetUIObjectOf), L"IShellFolder::GetUIObjectOf");
    if (search && search != folder)
        Hook(search, reinterpret_cast<void*>(SearchGetUIObjectOfHook), reinterpret_cast<void**>(&originalSearchGetUIObjectOf), L"SearchFolder::GetUIObjectOf");
    if (HMODULE shell32 = GetModuleHandleW(L"shell32.dll"))
        Hook(reinterpret_cast<void*>(GetProcAddress(shell32, "SHCreateDefaultContextMenu")),
            reinterpret_cast<void*>(CreateDefaultMenuHook), reinterpret_cast<void**>(&originalCreateDefaultMenu),
            L"SHCreateDefaultContextMenu");
    return TRUE;
}

void Wh_ModUninit() {
    stopping.store(true, std::memory_order_release);
    for (unsigned value = callbacks.load(std::memory_order_acquire); value;
         value = callbacks.load(std::memory_order_acquire))
        WaitOnAddress(&callbacks, &value, sizeof(value), INFINITE);
}
