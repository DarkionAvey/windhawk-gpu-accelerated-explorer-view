// ==WindhawkMod==
// @id              explorer-native-tab-command-trace
// @name            Explorer native tab command trace
// @description     Temporary diagnostic for native folder and tab command routing
// @version         0.1
// @author          DarkionAvey
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -lsynchronization
// @license         GPL-2.0
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Temporary diagnostic. Enable debug logging, perform right-click > Open in new
tab once, then middle-click a folder once. Save the [TabTrace] lines and disable
this diagnostic. Calls and arguments are passed through unchanged. Stack frames
are module names and offsets; folder names and PIDL contents are not logged.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <shobjidl.h>
#include <atomic>
#include <cwchar>

namespace {
std::atomic<bool> stopping{false};
std::atomic<unsigned> callbacks{0};
std::atomic<unsigned long long> sequence{0};
thread_local bool logging=false;
struct PreserveError {
    DWORD value=GetLastError();
    ~PreserveError() {SetLastError(value);}
};
#define Log(...) do { PreserveError preserve; Wh_Log(__VA_ARGS__); } while(0)

struct Callback {
    Callback() {callbacks.fetch_add(1,std::memory_order_acquire);}
    ~Callback() {PreserveError preserve;if(callbacks.fetch_sub(1,std::memory_order_release)==1) WakeByAddressAll(&callbacks);}
};

void Stack(unsigned long long id) {
    PreserveError preserve;
    if(logging || stopping.load(std::memory_order_acquire)) return;
    logging=true;
    void* frames[24]{};
    const USHORT count=CaptureStackBackTrace(1,ARRAYSIZE(frames),frames,nullptr);
    for(USHORT i=0;i<count;++i) {
        HMODULE module=nullptr;
        wchar_t path[MAX_PATH]{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(frames[i]),&module) && GetModuleFileNameW(module,path,ARRAYSIZE(path))) {
            const auto* name=wcsrchr(path,L'\\'); name=name ? name+1 : path;
            Log(L"[TabTrace %llu] stack[%u] %s+0x%llX",id,i,name,
                static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(frames[i])-reinterpret_cast<ULONG_PTR>(module)));
        } else Log(L"[TabTrace %llu] stack[%u] address=%p",id,i,frames[i]);
    }
    logging=false;
}

using BrowseFn=HRESULT (STDMETHODCALLTYPE*)(IShellBrowser*,PCUIDLIST_RELATIVE,UINT);
using ExecuteFn=HRESULT (STDMETHODCALLTYPE*)(void*);
using InitializeFn=HRESULT (STDMETHODCALLTYPE*)(void*,PCWSTR,IPropertyBag*);
// Native signatures also used by Sleek WinTabs (MIT, SleekGPX):
// https://github.com/SleekGPX/sleek-windhawk-explorer-tabs/blob/master/sleekwintabs.wh.cpp
using AddTabFn=HRESULT (WINAPI*)(void*,PCIDLIST_ABSOLUTE,int,GUID*);
using AddTabAtIndexFn=HRESULT (WINAPI*)(void*,PCIDLIST_ABSOLUTE,int,int,GUID*);
BrowseFn originalBrowse=nullptr;
ExecuteFn originalExecute=nullptr;
InitializeFn originalInitialize=nullptr;
AddTabFn originalAddTab=nullptr;
AddTabAtIndexFn originalAddTabAtIndex=nullptr;

HRESULT STDMETHODCALLTYPE Browse(IShellBrowser* self,PCUIDLIST_RELATIVE target,UINT flags) {
    Callback callback;
    const auto id=sequence.fetch_add(1)+1;
    if(!stopping.load()) {Log(L"[TabTrace %llu] BrowseObject ENTER tid=%lu browser=%p pidl=%p flags=0x%08X",id,GetCurrentThreadId(),self,target,flags);Stack(id);}
    const HRESULT hr=originalBrowse(self,target,flags);
    if(!stopping.load()) Log(L"[TabTrace %llu] BrowseObject EXIT hr=0x%08X",id,static_cast<unsigned>(hr));
    return hr;
}
HRESULT STDMETHODCALLTYPE Execute(void* self) {
    Callback callback;
    const auto id=sequence.fetch_add(1)+1;
    if(!stopping.load()) {Log(L"[TabTrace %llu] ExecuteFolder ENTER tid=%lu object=%p",id,GetCurrentThreadId(),self);Stack(id);}
    const HRESULT hr=originalExecute(self);
    if(!stopping.load()) Log(L"[TabTrace %llu] ExecuteFolder EXIT hr=0x%08X",id,static_cast<unsigned>(hr));
    return hr;
}
HRESULT STDMETHODCALLTYPE Initialize(void* self,PCWSTR command,IPropertyBag* properties) {
    Callback callback;
    if(command && !_wcsicmp(command,L"opennewtab") && !stopping.load())
        Log(L"[TabTrace] Initialize opennewtab tid=%lu object=%p",GetCurrentThreadId(),self);
    return originalInitialize(self,command,properties);
}
HRESULT WINAPI AddTab(void* self,PCIDLIST_ABSOLUTE target,int entry,GUID* tab) {
    Callback callback;
    const auto id=sequence.fetch_add(1)+1;
    if(!stopping.load()) {Log(L"[TabTrace %llu] AddTab ENTER tid=%lu frame=%p pidl=%p entry=%d",id,GetCurrentThreadId(),self,target,entry);Stack(id);}
    const HRESULT hr=originalAddTab(self,target,entry,tab);
    if(!stopping.load()) Log(L"[TabTrace %llu] AddTab EXIT hr=0x%08X",id,static_cast<unsigned>(hr));
    return hr;
}
HRESULT WINAPI AddTabAtIndex(void* self,PCIDLIST_ABSOLUTE target,int entry,int index,GUID* tab) {
    Callback callback;
    const auto id=sequence.fetch_add(1)+1;
    if(!stopping.load()) {Log(L"[TabTrace %llu] AddTabAtIndex ENTER tid=%lu frame=%p pidl=%p entry=%d index=%d",id,GetCurrentThreadId(),self,target,entry,index);Stack(id);}
    const HRESULT hr=originalAddTabAtIndex(self,target,entry,index,tab);
    if(!stopping.load()) Log(L"[TabTrace %llu] AddTabAtIndex EXIT hr=0x%08X",id,static_cast<unsigned>(hr));
    return hr;
}
}

BOOL Wh_ModInit() {
    HMODULE module=GetModuleHandleW(L"explorerframe.dll");
    if(!module) {Log(L"[TabTrace] ExplorerFrame not loaded");return FALSE;}
    struct Hook {PCWSTR name;void* target;void* detour;void** original;bool required;};
    Hook hooks[]{
        {L"CShellBrowser::BrowseObject(",nullptr,reinterpret_cast<void*>(Browse),reinterpret_cast<void**>(&originalBrowse),true},
        {L"CExecuteFolder::Execute(",nullptr,reinterpret_cast<void*>(Execute),reinterpret_cast<void**>(&originalExecute),true},
        {L"CExecuteFolder::Initialize(",nullptr,reinterpret_cast<void*>(Initialize),reinterpret_cast<void**>(&originalInitialize),false},
        {L"CExplorerFrame::AddTab(",nullptr,reinterpret_cast<void*>(AddTab),reinterpret_cast<void**>(&originalAddTab),true},
        {L"CExplorerFrame::AddTabAtIndex(",nullptr,reinterpret_cast<void*>(AddTabAtIndex),reinterpret_cast<void**>(&originalAddTabAtIndex),false}
    };
    WH_FIND_SYMBOL symbol{};
    HANDLE search=Wh_FindFirstSymbol(module,nullptr,&symbol);
    if(!search) {Log(L"[TabTrace] Microsoft symbols unavailable");return FALSE;}
    do {for(auto& hook:hooks) if(!hook.target && symbol.symbol && wcsstr(symbol.symbol,hook.name)) hook.target=symbol.address;}
    while(Wh_FindNextSymbol(search,&symbol));
    Wh_FindCloseSymbol(search);
    for(const auto& hook:hooks) if(hook.required && !hook.target) {
        Log(L"[TabTrace] Required native symbol unavailable: %s",hook.name);return FALSE;
    }
    for(const auto& hook:hooks) if(hook.target) {
        if(!Wh_SetFunctionHook(hook.target,hook.detour,hook.original)) return FALSE;
        Log(L"[TabTrace] Hooked %s ExplorerFrame+0x%llX",hook.name,
            static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(hook.target)-reinterpret_cast<ULONG_PTR>(module)));
    }
    Log(L"[TabTrace] READY pid=%lu; native calls are passed through unchanged",GetCurrentProcessId());
    return TRUE;
}
void Wh_ModBeforeUninit() {stopping.store(true,std::memory_order_release);}
void Wh_ModUninit() {
    stopping.store(true,std::memory_order_release);
    // Windhawk has removed the hooks, closing admission before this drain.
    for(unsigned active=callbacks.load(std::memory_order_acquire);active;active=callbacks.load(std::memory_order_acquire))
        WaitOnAddress(&callbacks,&active,sizeof(active),INFINITE);
}
