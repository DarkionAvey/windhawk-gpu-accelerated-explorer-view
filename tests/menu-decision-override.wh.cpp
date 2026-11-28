// ==WindhawkMod==
// @id              explorer-menu-decision-override
// @name            Explorer menu decision override
// @description     Temporary diagnostic: open File Explorer's item menu for a file of a subfolder
// @version         0.1
// @author          DarkionAvey
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lsynchronization -ladvapi32
// @license         GPL-2.0
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Temporary diagnostic, stage 2 of tests/menu-decision-probe.cpp (findings in
docs/1.1.21-native-menu-decision.md). Acts only during the probe's "forced-*"
steps, in the probe's own folder; everything else passes through unchanged.

During those steps, a menu request with nothing selected (Explorer's folder
menu) is turned into Explorer's item menu for the probe's file in a subfolder:
  - CDefView::OnBackgroundContextMenu -> CDefView::OnSelectionContextMenu
    (same signature, called from the same instruction by ExplorerFrame's
    UIItemsView::ShowContextMenu after its selection test),
  - CDefView::_CreateSelectionContextMenu returns that file's own menu,
  - IFolderView::Items/ItemCount(SVGIO_SELECTION) report that file to the
    modern menu (ContextMenuPresenter).
Needs Microsoft's public symbols for shell32.dll (Windhawk downloads them).
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
#include <string>
#include <vector>
#include <initializer_list>

namespace {
constexpr wchar_t kActiveEvent[] = L"Local\\ExplorerMenuDecisionProbe";
constexpr wchar_t kPhaseMapping[] = L"Local\\ExplorerMenuDecisionProbePhase";
std::atomic<bool> stopping{false};
std::atomic<unsigned> callbacks{0};

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

// The probe's current step is "phase|path;path;...": the items shown by the
// override during a "forced-*" or "minimal-*" step ("minimal-*" uses only the
// redirect and _GetItems, to find the fewest hooks), else nothing.
bool g_minimal = false;
std::vector<std::wstring> ForcedItems() {
    std::vector<std::wstring> items;
    std::wstring text;
    {
        if (stopping.load(std::memory_order_acquire)) return items;
        HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, kActiveEvent);
        if (!event) return items;
        const bool active = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
        CloseHandle(event);
        if (!active) return items;
        if (HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kPhaseMapping)) {
            if (const auto* view = static_cast<const wchar_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) {
                text.assign(view, wcsnlen(view, 4096));
                UnmapViewOfFile(view);
            }
            CloseHandle(mapping);
        }
    }
    const size_t bar = text.find(L'|');
    const bool forced = text.rfind(L"forced-", 0) == 0, minimal = text.rfind(L"minimal-", 0) == 0;
    if ((!forced && !minimal) || bar == std::wstring::npos) return items;
    g_minimal = minimal;
    for (size_t start = bar + 1; start < text.size();) {
        size_t end = text.find(L';', start);
        if (end == std::wstring::npos) end = text.size();
        if (end > start) items.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return items;
}
std::wstring ForcedItem() {
    const auto items = ForcedItems();
    return items.empty() ? std::wstring() : items.front();
}
// The items as Shell items, in order.
HRESULT ForcedShellItems(const std::vector<std::wstring>& paths, IObjectCollection** collection, IShellItemArray** array) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    HRESULT hr = S_OK;
    for (const auto& path : paths) {
        PIDLIST_ABSOLUTE id = nullptr;
        hr = SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr);
        if (FAILED(hr)) break;
        ids.push_back(id);
    }
    if (SUCCEEDED(hr) && collection) {
        hr = CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(collection));
        for (size_t i = 0; SUCCEEDED(hr) && i < ids.size(); ++i) {
            IShellItem* item = nullptr;
            hr = SHCreateItemFromIDList(ids[i], IID_PPV_ARGS(&item));
            if (SUCCEEDED(hr)) { hr = (*collection)->AddObject(item); item->Release(); }
        }
    }
    if (SUCCEEDED(hr) && array)
        hr = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()), const_cast<PCIDLIST_ABSOLUTE*>(ids.data()), array);
    for (auto id : ids) CoTaskMemFree(id);
    return hr;
}
// Who called: module+offset of a few frames, symbolized afterwards.
void Callers(const wchar_t* label) {
    void* frames[4]{};
    const USHORT count = CaptureStackBackTrace(2, ARRAYSIZE(frames), frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH]{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(frames[i]), &module) && GetModuleFileNameW(module, path, ARRAYSIZE(path)))
            Log(L"[MenuOverride] %s stack[%u] %s+0x%llX", label, i, PathFindFileNameW(path),
                static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(frames[i]) - reinterpret_cast<ULONG_PTR>(module)));
    }
}

// Only the probe's folder (its path is the forced item's parent's parent).
bool ProbeView(IUnknown* object, const std::wstring& item) {
    IFolderView* view = nullptr;
    bool probe = false;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&view))) && view) {
        IPersistFolder2* folder = nullptr;
        if (SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&folder))) && folder) {
            PIDLIST_ABSOLUTE id = nullptr;
            wchar_t path[MAX_PATH]{};
            if (SUCCEEDED(folder->GetCurFolder(&id)) && id && SHGetPathFromIDListW(id, path)) {
                const size_t length = wcslen(path);
                probe = item.size() > length && !_wcsnicmp(item.c_str(), path, length) && item[length] == L'\\';
            }
            CoTaskMemFree(id);
            folder->Release();
        }
        view->Release();
    }
    return probe;
}

using OnMenu_t = HRESULT (*)(void*, const POINT*);
using CreateSelectionMenu_t = HRESULT (*)(void*, int, REFIID, void**);
using ItemCount_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView*, UINT, int*);
using Items_t = HRESULT (STDMETHODCALLTYPE*)(IFolderView*, UINT, REFIID, void**);
OnMenu_t originalBackground = nullptr;
OnMenu_t originalSelection = nullptr;
CreateSelectionMenu_t originalCreateSelection = nullptr;
ItemCount_t originalItemCount = nullptr;
Items_t originalItems = nullptr;
thread_local bool forcing = false; // Inside a background request turned into an item request.

HRESULT BackgroundHook(void* self, const POINT* point) {
    Callback callback;
    if (ForcedItems().empty()) return originalBackground(self, point);
    Log(L"[MenuOverride] folder menu requested: opening the item menu instead (interface this=%p point=%p)", self, point);
    const bool previous = forcing;
    forcing = true;
    const HRESULT hr = originalSelection(self, point);
    forcing = previous;
    Log(L"[MenuOverride] item menu request returned hr=0x%08X", static_cast<unsigned>(hr));
    return hr;
}
HRESULT SelectionHook(void* self, const POINT* point) {
    Callback callback;
    return originalSelection(self, point);
}
// Explorer builds the item menu itself (its site, callback and curated
// commands); it takes the items from _GetItems below.
HRESULT CreateSelectionHook(void* self, int flags, REFIID riid, void** out) {
    Callback callback;
    const HRESULT hr = originalCreateSelection(self, flags, riid, out);
    if (forcing) Log(L"[MenuOverride] Explorer built the item menu (CDefView=%p flags=%d) hr=0x%08X", self, flags, static_cast<unsigned>(hr));
    return hr;
}
// CDefView::_GetItems(svgio, ?, riid, out): the view's own source of its
// selected items (menu object, commands). During a forced step it yields the
// probe's items instead of the (empty) native selection.
using GetItems_t = HRESULT (*)(void*, UINT, int, REFIID, void**);
GetItems_t originalGetItems = nullptr;
// CDefView::_GetContextMenuFromObjectCollection(IObjectCollection*, REFIID, void**): called, not hooked.
using FromCollection_t = HRESULT (*)(void*, IObjectCollection*, REFIID, void**);
FromCollection_t fromCollection = nullptr;
void IidText(REFIID riid, wchar_t* text, int length) {
    static const struct { const IID* iid; const wchar_t* name; } known[]{
        {&IID_IShellItemArray, L"IShellItemArray"}, {&IID_IObjectCollection, L"IObjectCollection"},
        {&IID_IObjectArray, L"IObjectArray"}, {&IID_IDataObject, L"IDataObject"}, {&IID_IEnumIDList, L"IEnumIDList"},
        {&IID_IContextMenu, L"IContextMenu"}};
    for (const auto& item : known)
        if (IsEqualIID(riid, *item.iid)) { wcsncpy_s(text, length, item.name, _TRUNCATE); return; }
    StringFromGUID2(riid, text, length);
}
// A pass-through menu object that logs what the modern menu asks of it: each
// QueryInterface (other interfaces are handed out from the real menu, so only
// the request and its answer are logged), and the IContextMenu calls.
class LoggingMenu final : public IContextMenu3 {
public:
    LoggingMenu(IUnknown* inner, const wchar_t* label) : label(label) {
        inner->QueryInterface(IID_PPV_ARGS(&menu));
        inner->QueryInterface(IID_PPV_ARGS(&menu2));
        inner->QueryInterface(IID_PPV_ARGS(&menu3));
        unknown = inner; unknown->AddRef();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        wchar_t iid[64]; IidName(riid, iid, ARRAYSIZE(iid));
        HRESULT hr;
        if (IsEqualIID(riid, IID_IUnknown) || (IsEqualIID(riid, IID_IContextMenu) && menu) ||
            (IsEqualIID(riid, IID_IContextMenu2) && menu2) || (IsEqualIID(riid, IID_IContextMenu3) && menu3)) {
            *out = static_cast<IContextMenu3*>(this); AddRef(); hr = S_OK;
        } else hr = unknown->QueryInterface(riid, out);
        Log(L"[MenuLog %s] QueryInterface(%s) -> 0x%08X", label, iid, static_cast<unsigned>(hr));
        return hr;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references;
        if (!left) {
            Log(L"[MenuLog %s] released", label);
            for (IUnknown* object : std::initializer_list<IUnknown*>{menu, menu2, menu3, unknown}) if (object) object->Release();
            delete this;
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU popup, UINT index, UINT first, UINT last, UINT flags) override {
        const HRESULT hr = menu->QueryContextMenu(popup, index, first, last, flags);
        Log(L"[MenuLog %s] QueryContextMenu(index=%u first=%u last=%u flags=0x%X) -> 0x%08X, %d entries", label, index, first, last,
            flags, static_cast<unsigned>(hr), GetMenuItemCount(popup));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(CMINVOKECOMMANDINFO* info) override {
        const HRESULT hr = menu->InvokeCommand(info);
        Log(L"[MenuLog %s] InvokeCommand(verb=%p) -> 0x%08X", label, info ? info->lpVerb : nullptr, static_cast<unsigned>(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR command, UINT type, UINT* reserved, LPSTR name, UINT length) override {
        const HRESULT hr = menu->GetCommandString(command, type, reserved, name, length);
        if (++commandStrings <= 40)
            Log(L"[MenuLog %s] GetCommandString(%llu, type=0x%X) -> 0x%08X %s", label, static_cast<unsigned long long>(command), type,
                static_cast<unsigned>(hr), SUCCEEDED(hr) && (type & GCS_UNICODE) && name ? reinterpret_cast<LPCWSTR>(name) : L"");
        return hr;
    }
    HRESULT STDMETHODCALLTYPE HandleMenuMsg(UINT message, WPARAM wParam, LPARAM lParam) override {
        return menu2 ? menu2->HandleMenuMsg(message, wParam, lParam) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE HandleMenuMsg2(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result) override {
        return menu3 ? menu3->HandleMenuMsg2(message, wParam, lParam, result) : E_NOTIMPL;
    }
private:
    static void IidName(REFIID riid, wchar_t* text, int length) {
        HKEY key = nullptr;
        wchar_t guid[64]{}, path[128]{};
        StringFromGUID2(riid, guid, ARRAYSIZE(guid));
        swprintf_s(path, L"Interface\\%s", guid);
        DWORD size = static_cast<DWORD>(length * sizeof(wchar_t));
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key) == ERROR_SUCCESS &&
            RegQueryValueExW(key, nullptr, nullptr, nullptr, reinterpret_cast<BYTE*>(text), &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            wcscat_s(text, length, L" ");
            wcscat_s(text, length, guid);
            return;
        }
        if (key) RegCloseKey(key);
        wcsncpy_s(text, length, guid, _TRUNCATE);
    }
    std::atomic<ULONG> references{1};
    const wchar_t* label;
    IUnknown* unknown = nullptr;
    IContextMenu* menu = nullptr;
    IContextMenu2* menu2 = nullptr;
    IContextMenu3* menu3 = nullptr;
    unsigned commandStrings = 0;
};

HRESULT GetItemsHook(void* self, UINT svgio, int option, REFIID riid, void** out) {
    Callback callback;
    const auto items = (svgio & SVGIO_TYPE_MASK) == SVGIO_SELECTION ? ForcedItems() : std::vector<std::wstring>();
    wchar_t iid[64];
    IidText(riid, iid, ARRAYSIZE(iid));
    if (items.empty() || !out) {
        const HRESULT hr = originalGetItems(self, svgio, option, riid, out);
        if (forcing) Log(L"[MenuOverride] _GetItems(svgio=0x%X, %d, %s) passed through hr=0x%08X", svgio, option, iid, static_cast<unsigned>(hr));
        return hr;
    }
    *out = nullptr;
    HRESULT hr;
    const bool menu = IsEqualIID(riid, IID_IContextMenu) || IsEqualIID(riid, IID_IContextMenu2) ||
        IsEqualIID(riid, IID_IContextMenu3);
    if (menu) {
        // _GetItems(selection, 1, IContextMenu) gathers the selection into an
        // IObjectCollection and builds the menu with
        // _GetContextMenuFromObjectCollection: the same call, with these items.
        // _GetContextMenuFromObjectCollection builds it through the view's own
        // folder (CFSFolder::GetContextMenu with the items' last IDs), which
        // names the wrong files for items of other folders. The items' own
        // folder builds it through the same Shell path (BHID_SFUIObject).
        IShellItemArray* array = nullptr;
        hr = ForcedShellItems(items, nullptr, &array);
        if (SUCCEEDED(hr)) hr = array->BindToHandler(nullptr, BHID_SFUIObject, riid, out);
        if (array) array->Release();
        if (FAILED(hr)) {
            // Items of several folders: Explorer's search results build this
            // menu by asking their folder (CDBFolder) through the interface
            // CDefView::_GetContextMenuFromObjectCollection uses:
            // {47D9E2B2-...}, method 3 = GetContextMenu(HWND, IShellItemArray*,
            // IBindCtx*, REFIID, void**). A search results folder (public
            // factory; nothing is searched) is asked the same way.
            Log(L"[MenuOverride] the items' own folder gave no menu hr=0x%08X; asking a search results folder", static_cast<unsigned>(hr));
            // Explorer passes its view window (CDefView+0x2C8 on this build).
            HWND viewWindow = *reinterpret_cast<HWND*>(static_cast<BYTE*>(self) + 0x2C8);
            if (!IsWindow(viewWindow)) viewWindow = nullptr;
            Log(L"[MenuOverride] view window %p", viewWindow);
            static const IID kItemsMenuFactory{0x47D9E2B2, 0xCBB3, 0x4FE3, {0xA9, 0x25, 0xF4, 0x99, 0x78, 0x68, 0x59, 0x82}};
            using GetContextMenu_t = HRESULT (STDMETHODCALLTYPE*)(IUnknown*, HWND, IShellItemArray*, IBindCtx*, REFIID, void**);
            ISearchFolderItemFactory* factory = nullptr;
            IShellItemArray* all = nullptr;
            IShellItem* results = nullptr;
            IShellFolder* searchFolder = nullptr;
            IUnknown* builder = nullptr;
            hr = ForcedShellItems(items, nullptr, &all);
            if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_SearchFolderItemFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            if (SUCCEEDED(hr)) hr = factory->SetScope(all);
            if (SUCCEEDED(hr)) hr = factory->GetShellItem(IID_PPV_ARGS(&results));
            if (SUCCEEDED(hr)) hr = results->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&searchFolder));
            if (SUCCEEDED(hr)) hr = searchFolder->QueryInterface(kItemsMenuFactory, reinterpret_cast<void**>(&builder));
            if (SUCCEEDED(hr)) {
                const auto getContextMenu = reinterpret_cast<GetContextMenu_t>((*reinterpret_cast<void***>(builder))[3]);
                hr = getContextMenu(builder, viewWindow, all, nullptr, riid, out);
                Log(L"[MenuOverride] search folder menu for the plain items hr=0x%08X", static_cast<unsigned>(hr));
                if (FAILED(hr)) {
                    // Its own children, as the search view passes them: the
                    // folder's ParseDisplayName for each full path.
                    std::vector<PITEMID_CHILD> children;
                    HRESULT parsed = S_OK;
                    for (const auto& path : items) {
                        std::wstring name = path;
                        PIDLIST_RELATIVE child = nullptr;
                        parsed = searchFolder->ParseDisplayName(nullptr, nullptr, name.data(), nullptr, &child, nullptr);
                        Log(L"[MenuOverride]   search child for %s hr=0x%08X size=%u", path.c_str(), static_cast<unsigned>(parsed),
                            child ? ILGetSize(child) : 0);
                        if (FAILED(parsed)) break;
                        children.push_back(reinterpret_cast<PITEMID_CHILD>(child));
                    }
                    IShellItemArray* wrapped = nullptr;
                    if (SUCCEEDED(parsed))
                        parsed = SHCreateShellItemArray(nullptr, searchFolder, static_cast<UINT>(children.size()),
                            reinterpret_cast<PCUITEMID_CHILD_ARRAY>(const_cast<const ITEMIDLIST**>(reinterpret_cast<ITEMIDLIST**>(children.data()))), &wrapped);
                    if (SUCCEEDED(parsed)) hr = getContextMenu(builder, viewWindow, wrapped, nullptr, riid, out);
                    Log(L"[MenuOverride] search folder menu for its own children hr=0x%08X (array hr=0x%08X)",
                        static_cast<unsigned>(hr), static_cast<unsigned>(parsed));
                    if (wrapped) wrapped->Release();
                    for (auto child : children) CoTaskMemFree(child);
                }
            }
            if (FAILED(hr)) {
                // A library also lists items of several folders: its folder's builder.
                IShellLibrary* library = nullptr;
                IShellItem* libraryItem = nullptr;
                IShellFolder* libraryFolder = nullptr;
                IUnknown* libraryBuilder = nullptr;
                HRESULT step = SHLoadLibraryFromKnownFolder(FOLDERID_DocumentsLibrary, STGM_READ, IID_PPV_ARGS(&library));
                if (SUCCEEDED(step)) step = SHGetKnownFolderItem(FOLDERID_DocumentsLibrary, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&libraryItem));
                if (SUCCEEDED(step)) step = libraryItem->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&libraryFolder));
                if (SUCCEEDED(step)) step = libraryFolder->QueryInterface(kItemsMenuFactory, reinterpret_cast<void**>(&libraryBuilder));
                if (SUCCEEDED(step)) {
                    const auto getContextMenu = reinterpret_cast<GetContextMenu_t>((*reinterpret_cast<void***>(libraryBuilder))[3]);
                    step = getContextMenu(libraryBuilder, viewWindow, all, nullptr, riid, out);
                }
                Log(L"[MenuOverride] library folder menu for the plain items hr=0x%08X", static_cast<unsigned>(step));
                if (SUCCEEDED(step)) hr = step;
                for (IUnknown* object : std::initializer_list<IUnknown*>{libraryBuilder, libraryFolder, libraryItem, library})
                    if (object) object->Release();
            }
            Log(L"[MenuOverride] cross-folder menu hr=0x%08X", static_cast<unsigned>(hr));
            for (IUnknown* object : std::initializer_list<IUnknown*>{builder, searchFolder, results, factory, all})
                if (object) object->Release();
        }
        if (FAILED(hr)) {
            // The desktop's default menu over full IDs, as Explorer builds it
            // for search results: built fine earlier, but the modern menu
            // never showed it. Traced through LoggingMenu below.
            std::vector<PIDLIST_ABSOLUTE> ids;
            for (const auto& path : items) {
                PIDLIST_ABSOLUTE id = nullptr;
                if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr))) ids.push_back(id);
            }
            IShellFolder* desktop = nullptr;
            IShellFolder* owner = nullptr;
            IUnknown* associations = nullptr;
            PCUITEMID_CHILD leaf = nullptr;
            if (!ids.empty() && SUCCEEDED(SHBindToParent(ids.front(), IID_PPV_ARGS(&owner), &leaf)))
                owner->GetUIObjectOf(nullptr, 1, &leaf, IID_IQueryAssociations, nullptr, reinterpret_cast<void**>(&associations));
            hr = ids.size() == items.size() ? SHGetDesktopFolder(&desktop) : E_FAIL;
            if (SUCCEEDED(hr)) {
                DEFCONTEXTMENU definition{};
                definition.psf = desktop;
                definition.cidl = static_cast<UINT>(ids.size());
                definition.apidl = reinterpret_cast<PCUITEMID_CHILD_ARRAY>(const_cast<const ITEMIDLIST**>(
                    reinterpret_cast<ITEMIDLIST**>(ids.data())));
                definition.punkAssociationInfo = associations;
                hr = SHCreateDefaultContextMenu(&definition, riid, out);
            }
            Log(L"[MenuOverride] desktop default menu hr=0x%08X", static_cast<unsigned>(hr));
            for (IUnknown* object : std::initializer_list<IUnknown*>{associations, owner, desktop}) if (object) object->Release();
            for (auto id : ids) CoTaskMemFree(id);
        }
        // Every menu object handed to the modern menu goes through the logger,
        // so a shown menu and a dropped one can be compared call by call.
        if (SUCCEEDED(hr) && *out) {
            auto* logger = new LoggingMenu(static_cast<IUnknown*>(*out), items.size() > 1 ? L"multi" : L"single");
            static_cast<IUnknown*>(*out)->Release();
            hr = logger->QueryInterface(riid, out);
            logger->Release();
        }
    } else if (IsEqualIID(riid, IID_IObjectCollection) || IsEqualIID(riid, IID_IObjectArray)) {
        IObjectCollection* collection = nullptr;
        hr = ForcedShellItems(items, &collection, nullptr);
        if (SUCCEEDED(hr)) hr = collection->QueryInterface(riid, out);
        if (collection) collection->Release();
    } else {
        IShellItemArray* array = nullptr;
        hr = ForcedShellItems(items, nullptr, &array);
        if (SUCCEEDED(hr)) {
            hr = array->QueryInterface(riid, out);
            if (FAILED(hr) && IsEqualIID(riid, IID_IDataObject)) hr = array->BindToHandler(nullptr, BHID_DataObject, riid, out);
            array->Release();
        }
    }
    Log(L"[MenuOverride] _GetItems(CDefView=%p svgio=0x%X, %d, %s) -> %zu probe item(s) hr=0x%08X",
        self, svgio, option, iid, items.size(), static_cast<unsigned>(hr));
    Callers(L"_GetItems caller");
    if (SUCCEEDED(hr)) return hr;
    return originalGetItems(self, svgio, option, riid, out);
}
// The public selection queries; skipped in "minimal-*" steps, to learn
// whether they already reach _GetItems.
HRESULT STDMETHODCALLTYPE ItemCountHook(IFolderView* self, UINT flags, int* count) {
    Callback callback;
    if (count && (flags & SVGIO_TYPE_MASK) == SVGIO_SELECTION) {
        const auto items = ForcedItems();
        if (!items.empty() && !g_minimal && ProbeView(self, items.front())) {
            *count = static_cast<int>(items.size());
            Log(L"[MenuOverride] ItemCount(selection) -> %d", *count);
            return S_OK;
        }
        if (!items.empty()) {
            const HRESULT hr = originalItemCount(self, flags, count);
            Log(L"[MenuOverride] ItemCount(selection) passed through -> %d (IFolderView=%p)", *count, self);
            Callers(L"ItemCount caller");
            return hr;
        }
    }
    return originalItemCount(self, flags, count);
}
HRESULT STDMETHODCALLTYPE ItemsHook(IFolderView* self, UINT flags, REFIID riid, void** out) {
    Callback callback;
    if (out && (flags & SVGIO_TYPE_MASK) == SVGIO_SELECTION) {
        const auto items = ForcedItems();
        if (!items.empty() && !g_minimal && ProbeView(self, items.front())) {
            IShellItemArray* array = nullptr;
            HRESULT hr = ForcedShellItems(items, nullptr, &array);
            if (SUCCEEDED(hr)) hr = array->QueryInterface(riid, out);
            if (array) array->Release();
            Log(L"[MenuOverride] Items(selection, flags=0x%X) -> %zu probe item(s) hr=0x%08X", flags, items.size(), static_cast<unsigned>(hr));
            if (SUCCEEDED(hr)) return hr;
        }
        if (!items.empty()) {
            const HRESULT hr = originalItems(self, flags, riid, out);
            Log(L"[MenuOverride] Items(selection, flags=0x%X) passed through hr=0x%08X (IFolderView=%p)", flags, static_cast<unsigned>(hr), self);
            Callers(L"Items caller");
            return hr;
        }
    }
    return originalItems(self, flags, riid, out);
}
}

BOOL Wh_ModInit() {
    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    if (!shell32) { Log(L"[MenuOverride] shell32 not loaded"); return FALSE; }
    struct Hook { const wchar_t* name; void* target; void* detour; void** original; };
    Hook hooks[]{
        {L"CDefView::OnBackgroundContextMenu(", nullptr, reinterpret_cast<void*>(BackgroundHook), reinterpret_cast<void**>(&originalBackground)},
        {L"CDefView::OnSelectionContextMenu(", nullptr, reinterpret_cast<void*>(SelectionHook), reinterpret_cast<void**>(&originalSelection)},
        {L"CDefView::_CreateSelectionContextMenu(", nullptr, reinterpret_cast<void*>(CreateSelectionHook), reinterpret_cast<void**>(&originalCreateSelection)},
        {L"CDefView::_GetItems(", nullptr, reinterpret_cast<void*>(GetItemsHook), reinterpret_cast<void**>(&originalGetItems)},
    };
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(shell32, nullptr, &symbol);
    if (!search) { Log(L"[MenuOverride] Microsoft symbols for shell32 unavailable"); return FALSE; }
    do {
        for (auto& hook : hooks)
            if (!hook.target && symbol.symbol && wcsstr(symbol.symbol, hook.name)) {
                hook.target = symbol.address;
                Log(L"[MenuOverride] symbol %s", symbol.symbol);
            }
        if (!fromCollection && symbol.symbol && wcsstr(symbol.symbol, L"CDefView::_GetContextMenuFromObjectCollection(")) {
            fromCollection = reinterpret_cast<FromCollection_t>(const_cast<void*>(symbol.address));
            Log(L"[MenuOverride] symbol %s", symbol.symbol);
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    for (auto& hook : hooks)
        if (!hook.target || !Wh_SetFunctionHook(hook.target, hook.detour, hook.original)) {
            Log(L"[MenuOverride] could not hook %s", hook.name);
            return FALSE;
        }
    // The view's selection queries, on the Shell's view implementation.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return FALSE;
    void* slots[2]{};
    {
        IShellFolder* desktop = nullptr;
        if (SUCCEEDED(SHGetDesktopFolder(&desktop)) && desktop) {
            SFV_CREATE create{};
            create.cbSize = sizeof(create);
            create.pshf = desktop;
            IShellView* shell = nullptr;
            IFolderView* folderView = nullptr;
            if (SUCCEEDED(SHCreateShellFolderView(&create, &shell)) && shell) {
                if (SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&folderView))) && folderView) {
                    void** table = *reinterpret_cast<void***>(folderView);
                    slots[0] = table[7]; // IFolderView::ItemCount
                    slots[1] = table[8]; // IFolderView::Items
                    folderView->Release();
                }
                shell->Release();
            }
            desktop->Release();
        }
    }
    CoUninitialize();
    if (!slots[0] || !slots[1] ||
        !Wh_SetFunctionHook(slots[0], reinterpret_cast<void*>(ItemCountHook), reinterpret_cast<void**>(&originalItemCount)) ||
        !Wh_SetFunctionHook(slots[1], reinterpret_cast<void*>(ItemsHook), reinterpret_cast<void**>(&originalItems))) {
        Log(L"[MenuOverride] could not hook the view's selection queries");
        return FALSE;
    }
    Log(L"[MenuOverride] ready");
    return TRUE;
}

void Wh_ModUninit() {
    stopping.store(true, std::memory_order_release);
    for (unsigned value = callbacks.load(std::memory_order_acquire); value;
         value = callbacks.load(std::memory_order_acquire))
        WaitOnAddress(&callbacks, &value, sizeof(value), INFINITE);
}
