// Manual read-only diagnostic for a user-supplied directory. Never select,
// navigate, focus, dispatch keys, modify files or install hooks in Explorer.
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <exdisp.h>
#include <servprov.h>
#include <shlguid.h>
#include <propsys.h>
#include <shlwapi.h>
#include <docobj.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { fwprintf(stderr,L"Usage: inspect-shell-view.exe <directory>\n"); return 2; }
    const HRESULT initialized=OleInitialize(nullptr);
    if(FAILED(initialized)) return 3;
    ComPtr<IShellWindows> windows;
    HRESULT hr=CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows));
    if(FAILED(hr)) { wprintf(L"ShellWindows hr=%08lx\n",hr); return 4; }
    long count=0; windows->get_Count(&count); bool found=false;
    for(long i=0;i<count;++i) {
        VARIANT index{}; index.vt=VT_I4; index.lVal=i;
        ComPtr<IDispatch> dispatch; ComPtr<IWebBrowser2> automation;
        ComPtr<IServiceProvider> service; ComPtr<IShellBrowser> browser;
        ComPtr<IShellView> shell; ComPtr<IFolderView2> view; ComPtr<IPersistFolder2> folder;
        if(FAILED(windows->Item(index,&dispatch)) || !dispatch || FAILED(dispatch.As(&automation)) ||
            FAILED(automation.As(&service)) ||
            FAILED(service->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser))) ||
            FAILED(browser->QueryActiveShellView(&shell)) || FAILED(shell.As(&view)) ||
            FAILED(view->GetFolder(IID_PPV_ARGS(&folder)))) continue;
        PIDLIST_ABSOLUTE pidl=nullptr; PWSTR path=nullptr;
        hr=folder->GetCurFolder(&pidl);
        if(SUCCEEDED(hr)) hr=SHGetNameFromIDList(pidl,SIGDN_FILESYSPATH,&path);
        const bool matches=path && _wcsicmp(path,argv[1])==0;
        CoTaskMemFree(pidl); CoTaskMemFree(path);
        if(!matches) continue;
        found=true;
        int all=-1,selected=-1,focused=-1,sortCount=0,iconSize=0;
        FOLDERVIEWMODE mode{}; DWORD flags=0;
        view->ItemCount(SVGIO_ALLVIEW,&all); view->ItemCount(SVGIO_SELECTION,&selected);
        view->GetFocusedItem(&focused); view->GetSortColumnCount(&sortCount);
        view->GetViewModeAndIconSize(&mode,&iconSize); view->GetCurrentFolderFlags(&flags);
        HWND hwnd=nullptr; shell->GetWindow(&hwnd);
        wprintf(L"view=%p all=%d selected=%d focused=%d mode=%u icon=%d flags=%08x sortCount=%d\n",
            hwnd,all,selected,focused,mode,iconSize,unsigned(flags),sortCount);
        ComPtr<IOleCommandTarget> commands;
        HRESULT commandHr=shell.As(&commands);
        OLECMD selectAll{OLECMDID_SELECTALL,0};
        if(SUCCEEDED(commandHr)) commandHr=commands->QueryStatus(nullptr,1,&selectAll,nullptr);
        wprintf(L"select-all-query=%08lx flags=%08lx\n",commandHr,selectAll.cmdf);
        ComPtr<IContextMenu> menu;
        if(SUCCEEDED(shell->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu))) && menu) {
            HMENU popup=CreatePopupMenu();
            const HRESULT populated=menu->QueryContextMenu(popup,0,1,0x7fff,CMF_NORMAL);
            if(SUCCEEDED(populated)) for(UINT n=0;n<HRESULT_CODE(populated);++n) {
                wchar_t verb[128]{};
                if(SUCCEEDED(menu->GetCommandString(n,GCS_VERBW,nullptr,reinterpret_cast<char*>(verb),128)) && verb[0])
                    wprintf(L"background-verb=%ls\n",verb);
            }
            DestroyMenu(popup);
        }
        SHELLSTATE visibility{};
        SHGetSetSettings(&visibility,SSF_SHOWALLOBJECTS|SSF_SHOWSUPERHIDDEN,FALSE);
        wprintf(L"show-hidden=%d show-protected=%d\n",visibility.fShowAllObjects,visibility.fShowSuperHidden);
        ComPtr<IEnumIDList> items; ComPtr<IShellFolder> sf;
        std::set<std::wstring> nativeNames;
        if(SUCCEEDED(view->Items(SVGIO_ALLVIEW,IID_PPV_ARGS(&items))) && items &&
            SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&sf)))) {
            PITEMID_CHILD child=nullptr; ULONG fetched=0;
            while(items->Next(1,&child,&fetched)==S_OK && fetched==1) {
                STRRET name{}; PWSTR text=nullptr;
                if(SUCCEEDED(sf->GetDisplayNameOf(child,SHGDN_FORPARSING,&name)) &&
                    SUCCEEDED(StrRetToStrW(&name,child,&text))) nativeNames.emplace(text);
                CoTaskMemFree(text); CoTaskMemFree(child); child=nullptr;
            }
            wprintf(L"enumerated-native=%zu\n",nativeNames.size());
            WIN32_FIND_DATAW data{};
            const std::wstring root=std::wstring(argv[1])+L"\\";
            HANDLE search=FindFirstFileW((root+L"*").c_str(),&data);
            if(search!=INVALID_HANDLE_VALUE) {
                do {
                    if(wcscmp(data.cFileName,L".")==0 || wcscmp(data.cFileName,L"..")==0) continue;
                    const auto path=root+data.cFileName;
                    bool included=false;
                    for(const auto& name:nativeNames) if(_wcsicmp(name.c_str(),path.c_str())==0) {included=true;break;}
                    if(!included) {
                        std::wstring leaf=data.cFileName; PITEMID_CHILD child=nullptr; ULONG eaten=0;
                        SFGAOF attributes=SFGAO_HIDDEN|SFGAO_SYSTEM|SFGAO_NONENUMERATED|SFGAO_FOLDER;
                        const HRESULT parsed=sf->ParseDisplayName(nullptr,nullptr,leaf.data(),&eaten,&child,&attributes);
                        if(SUCCEEDED(parsed) && child) sf->GetAttributesOf(1,const_cast<PCUITEMID_CHILD*>(&child),&attributes);
                        wprintf(L"not-in-native-view=%ls attributes=%08lx shell-attributes=%08lx parse-hr=%08lx\n",
                            data.cFileName,data.dwFileAttributes,attributes,parsed);
                        CoTaskMemFree(child);
                    }
                } while(FindNextFileW(search,&data));
                FindClose(search);
            }
        }
        if(sortCount>0 && sortCount<32) {
            std::vector<SORTCOLUMN> columns(sortCount);
            if(SUCCEEDED(view->GetSortColumns(columns.data(),sortCount))) for(const auto& column:columns) {
                PWSTR key=nullptr; PSGetNameFromPropertyKey(column.propkey,&key);
                wprintf(L"sort=%ls direction=%d\n",key?key:L"unknown",int(column.direction)); CoTaskMemFree(key);
            }
        }
        if(focused>=0) {
            PITEMID_CHILD child=nullptr; ComPtr<IShellFolder> sf;
            if(SUCCEEDED(view->Item(focused,&child)) && child && SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&sf)))) {
                STRRET name{}; PWSTR text=nullptr;
                if(SUCCEEDED(sf->GetDisplayNameOf(child,SHGDN_FORPARSING,&name)) &&
                    SUCCEEDED(StrRetToStrW(&name,child,&text))) wprintf(L"focused-path=%ls\n",text);
                CoTaskMemFree(text);
            }
            CoTaskMemFree(child);
        }
    }
    windows.Reset(); OleUninitialize();
    return found?0:1;
}
