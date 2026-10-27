// Read-only probe of a supplied folder's namespace and backing identities.
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;

static void PrintIdentity(const wchar_t* label, PCIDLIST_ABSOLUTE pidl) {
    PWSTR name = nullptr;
    const HRESULT hr = pidl ? SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &name) : E_POINTER;
    wprintf(L"%ls hr=%08lx %ls\n", label, hr, name ? name : L"");
    CoTaskMemFree(name);
}
static void PrintFolder(IShellFolder* folder) {
    STRRET ownName{}; PWSTR ownText = nullptr;
    HRESULT ownHr = folder->GetDisplayNameOf(nullptr, SHGDN_FORPARSING, &ownName);
    if (SUCCEEDED(ownHr)) StrRetToStrW(&ownName, nullptr, &ownText);
    wprintf(L"self-name=%08lx %ls\n", ownHr, ownText ? ownText : L"");
    CoTaskMemFree(ownText);
    ComPtr<IPersistFolder2> persist;
    if (SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&persist)))) {
        PIDLIST_ABSOLUTE root = nullptr;
        persist->GetCurFolder(&root);
        PrintIdentity(L"root", root);
        CoTaskMemFree(root);
        CLSID clsid{};
        persist->GetClassID(&clsid);
        wchar_t text[40]{};
        StringFromGUID2(clsid, text, 40);
        wprintf(L"class=%ls\n", text);
    }
    ComPtr<IPersistFolder3> target;
    HRESULT hr = folder->QueryInterface(IID_PPV_ARGS(&target));
    wprintf(L"IPersistFolder3=%08lx\n", hr);
    if (target) {
        PERSIST_FOLDER_TARGET_INFO info{};
        hr = target->GetFolderTargetInfo(&info);
        wprintf(L"GetFolderTargetInfo=%08lx parsing=%ls attributes=%08lx\n", hr,
            info.szTargetParsingName, info.dwAttributes);
        PrintIdentity(L"target", info.pidlTargetFolder);
        CoTaskMemFree(info.pidlTargetFolder);
    }
    ComPtr<IEnumIDList> enumeration;
    hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumeration);
    wprintf(L"EnumObjects=%08lx\n", hr);
    unsigned count = 0;
    while (enumeration) {
        PITEMID_CHILD child = nullptr;
        ULONG fetched = 0;
        if (enumeration->Next(1, &child, &fetched) != S_OK || !fetched) break;
        if (count++ < 8) {
            STRRET name{};
            PWSTR text = nullptr;
            if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING, &name)))
                StrRetToStrW(&name, child, &text);
            wprintf(L"child=%ls\n", text ? text : L"");
            CoTaskMemFree(text);
            WIN32_FIND_DATAW data{};
            HRESULT dataHr = SHGetDataFromIDListW(folder, child, SHGDFIL_FINDDATA, &data, sizeof(data));
            wprintf(L"find-data=%08lx name=%ls attributes=%08lx\n", dataHr, data.cFileName, data.dwFileAttributes);
            ComPtr<IShellItem> childItem;
            HRESULT itemHr = SHCreateItemWithParent(nullptr, folder, child, IID_PPV_ARGS(&childItem));
            text = nullptr;
            if (SUCCEEDED(itemHr)) itemHr = childItem->GetDisplayName(SIGDN_FILESYSPATH, &text);
            wprintf(L"item-path=%08lx %ls\n", itemHr, text ? text : L"");
            CoTaskMemFree(text);
        }
        CoTaskMemFree(child);
    }
    wprintf(L"count=%u\n", count);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || FAILED(OleInitialize(nullptr))) return 2;
    ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(argv[1], nullptr, IID_PPV_ARGS(&item));
    ComPtr<IShellFolder> folder;
    if (SUCCEEDED(hr)) hr = item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder));
    wprintf(L"bind=%08lx\n", hr);
    if (folder) PrintFolder(folder.Get());
    folder.Reset(); item.Reset();
    OleUninitialize();
    return FAILED(hr) ? 1 : 0;
}
