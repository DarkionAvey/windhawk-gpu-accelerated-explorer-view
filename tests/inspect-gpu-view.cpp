// Manual read-only snapshot of a matching running mod build. Compile with
// GPU_OVERLAY_SOURCE set to that build's preserved source. No injection,
// process writes, suspension, selection/focus changes or hooks are performed.
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
#include GPU_OVERLAY_SOURCE
#include <cstdio>
#include <type_traits>

template<class T> bool Remote(HANDLE process,ULONG_PTR address,T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    SIZE_T read=0;
    return ReadProcessMemory(process,reinterpret_cast<void*>(address),&value,sizeof(value),&read) && read==sizeof(value);
}
static std::wstring RemoteLongString(HANDLE process,ULONG_PTR address) {
    const std::wstring probe(137,L'x');
    const auto words=reinterpret_cast<const ULONG_PTR*>(&probe);
    size_t pointerOffset=0,sizeOffset=0;
    for(size_t i=0;i<sizeof(probe)/sizeof(ULONG_PTR);++i) {
        if(words[i]==reinterpret_cast<ULONG_PTR>(probe.data())) pointerOffset=i*sizeof(ULONG_PTR);
        if(words[i]==probe.size()) sizeOffset=i*sizeof(ULONG_PTR);
    }
    ULONG_PTR data=0,length=0;
    if(!Remote(process,address+pointerOffset,data) || !Remote(process,address+sizeOffset,length) ||
        data<65536 || !length || length>32768) return {};
    std::wstring result(length,L'\0'); SIZE_T read=0;
    if(!ReadProcessMemory(process,reinterpret_cast<void*>(data),result.data(),length*sizeof(wchar_t),&read) ||
        read!=length*sizeof(wchar_t)) return {};
    return result;
}
static BOOL CALLBACK Inspect(HWND hwnd,LPARAM filter) {
    wchar_t cls[128]{}; GetClassNameW(hwnd,cls,128);
    if(wcscmp(cls,kOverlayClass)) return TRUE;
    DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return TRUE;
    const ULONG_PTR state=GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    const auto path=RemoteLongString(process,state+offsetof(ViewState,folderPath));
    if(_wcsicmp(path.c_str(),reinterpret_cast<const wchar_t*>(filter))!=0) { CloseHandle(process); return TRUE; }
    int total=-1,selected=-1,anchor=-1;
    ULONG_PTR vector[3]{}; unsigned long long lifetime=0,token=0;
    Remote(process,state+offsetof(ViewState,total),total);
    Remote(process,state+offsetof(ViewState,selected),selected);
    Remote(process,state+offsetof(ViewState,anchor),anchor);
    Remote(process,state+offsetof(ViewState,lifetimeToken),lifetime);
    Remote(process,state+offsetof(ViewState,token),token);
    Remote(process,state+offsetof(ViewState,entries),vector);
    if(total<0 || total>1000000 || vector[1]<vector[0] || vector[1]-vector[0]!=size_t(total)*sizeof(Entry)) {
        wprintf(L"Unmatched ABI/snapshot hwnd=%p total=%d; no entries inspected\n",hwnd,total);
        CloseHandle(process); return TRUE;
    }
    wprintf(L"gpu=%p pid=%lu lifetime=%llu token=%llu total=%d selected=%d anchor=%d\n",hwnd,pid,lifetime,token,total,selected,anchor);
    for(int i=std::max(0,total-6);i<total;++i) {
        const auto entry=vector[0]+size_t(i)*sizeof(Entry);
        const auto itemPath=RemoteLongString(process,entry+offsetof(Entry,path));
        bool folder=false; Remote(process,entry+offsetof(Entry,folder),folder);
        wprintf(L"tail-row=%d folder=%d path=%ls\n",i,folder,itemPath.c_str());
    }
    if(selected>=0 && selected<total) {
        const auto itemPath=RemoteLongString(process,vector[0]+size_t(selected)*sizeof(Entry)+offsetof(Entry,path));
        wprintf(L"selected-path=%ls\n",itemPath.c_str());
    }
    unsigned long long after=0; ULONG_PTR afterVector[3]{};
    Remote(process,state+offsetof(ViewState,lifetimeToken),after);
    Remote(process,state+offsetof(ViewState,entries),afterVector);
    wprintf(L"snapshot-stable=%d\n",lifetime==after && vector[0]==afterVector[0] && vector[1]==afterVector[1]);
    CloseHandle(process); return TRUE;
}
static BOOL CALLBACK Frame(HWND hwnd,LPARAM filter) {
    wchar_t cls[128]{}; GetClassNameW(hwnd,cls,128);
    if(wcscmp(cls,L"CabinetWClass")==0) EnumChildWindows(hwnd,Inspect,filter);
    return TRUE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { fwprintf(stderr,L"Usage: inspect-gpu-view.exe <directory>\n"); return 2; }
    EnumWindows(Frame,reinterpret_cast<LPARAM>(argv[1])); return 0;
}
