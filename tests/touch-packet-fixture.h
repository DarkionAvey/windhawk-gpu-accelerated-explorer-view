// Observe native recognition and substitute desktop side effects only. Windows Ninput
// performs gesture recognition and owns the thresholds; no fabricated outputs.
#include <windows.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <interactioncontext.h>
#include <wrl/client.h>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cstring>
#include <cstdio>
#include <algorithm>
namespace TouchFixture {
inline thread_local bool enabled=false;
inline thread_local bool failCreate=false, rejectPackets=false, promotionSucceeds=true, promoted=false;
inline thread_local std::vector<INTERACTION_CONTEXT_OUTPUT2> recognized;
inline thread_local unsigned begins=0,updates=0,ends=0,processCalls=0,promotions=0,drags=0;
inline thread_local DWORD owner=0,callbackThread=0;
inline thread_local LONG overX=0,overY=0;
inline thread_local BOOL inertia=FALSE,animatedEnd=FALSE;
inline thread_local std::function<void()> duringDrag;
inline thread_local std::vector<std::wstring> dragPaths;
struct Binding {INTERACTION_CONTEXT_OUTPUT_CALLBACK2 callback=nullptr; void* client=nullptr; float deceleration=0.f;};
inline thread_local std::map<HINTERACTIONCONTEXT,std::unique_ptr<Binding>> bindings;
inline void CALLBACK Observe(void* value,const INTERACTION_CONTEXT_OUTPUT2* output) {
    auto& binding=*static_cast<Binding*>(value);
    callbackThread=GetCurrentThreadId(); recognized.push_back(*output);
    if(binding.callback) binding.callback(binding.client,output);
}
inline HRESULT WINAPI Register(HINTERACTIONCONTEXT context,INTERACTION_CONTEXT_OUTPUT_CALLBACK2 callback,void* client) {
    if(!enabled) return ::RegisterOutputCallbackInteractionContext2(context,callback,client);
    auto& binding=bindings[context]; if(!binding) binding=std::make_unique<Binding>();
    *binding={callback,client};
    return ::RegisterOutputCallbackInteractionContext2(context,Observe,binding.get());
}
inline HRESULT WINAPI Destroy(HINTERACTIONCONTEXT context) {
    const auto result=::DestroyInteractionContext(context); bindings.erase(context); return result;
}
inline HRESULT WINAPI Create(HINTERACTIONCONTEXT* context) {
    if(enabled && failCreate) {*context=nullptr; return E_OUTOFMEMORY;}
    return ::CreateInteractionContext(context);
}
inline HRESULT WINAPI Process(HINTERACTIONCONTEXT context,UINT32 rows,UINT32 columns,const POINTER_INFO* info) {
    if(enabled) {++processCalls; if(rejectPackets) return E_INVALIDARG;}
    const auto hr=::ProcessPointerFramesInteractionContext(context,rows,columns,info);
    // Snapshot the effective native value after processing returns and before
    // production can stop the recognizer. Stop resets this per-interaction value.
    if(enabled && SUCCEEDED(hr)) if(const auto found=bindings.find(context); found!=bindings.end())
        ::GetInertiaParameterInteractionContext(context,INERTIA_PARAMETER_TRANSLATION_DECELERATION,&found->second->deceleration);
    if(enabled && FAILED(hr)) std::fprintf(stderr,"NINPUT failure hr=%08lx flags=%u columns=%u\n",hr,info[0].pointerFlags,columns);
    return hr;
}
inline BOOL WINAPI Promote() {
    if(!enabled) {
        using Fn=BOOL(WINAPI*)();
        const auto fn=reinterpret_cast<Fn>(::GetProcAddress(GetModuleHandleW(L"user32.dll"),"ConvertPrimaryPointerToMouseDrag"));
        return fn ? fn() : FALSE;
    }
    ++promotions; promoted=promotionSucceeds; return promoted;
}
inline FARPROC WINAPI Procedure(HMODULE module,LPCSTR name) {
    if(enabled && !IS_INTRESOURCE(name) && std::strcmp(name,"ConvertPrimaryPointerToMouseDrag")==0)
        return reinterpret_cast<FARPROC>(Promote);
    return ::GetProcAddress(module,name);
}
inline HRESULT WINAPI Drag(HWND hwnd,IDataObject* data,IDropSource* source,DWORD allowed,DWORD* effect) {
    if(!enabled) return ::SHDoDragDrop(hwnd,data,source,allowed,effect);
    if(!promoted) return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IShellItemArray> items;
    if(SUCCEEDED(SHCreateShellItemArrayFromDataObject(data,IID_PPV_ARGS(&items)))) {
        DWORD count=0; items->GetCount(&count); dragPaths.clear();
        for(DWORD i=0;i<count;++i) {
            Microsoft::WRL::ComPtr<IShellItem> item; PWSTR path=nullptr;
            if(SUCCEEDED(items->GetItemAt(i,&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&path))) {
                dragPaths.emplace_back(path); CoTaskMemFree(path);
            }
        }
    }
    ++drags; if(duringDrag) duringDrag();
    *effect=DROPEFFECT_NONE; promoted=false; return DRAGDROP_S_CANCEL;
}
inline BOOL WINAPI Begin(HWND hwnd) {if(!enabled) return ::BeginPanningFeedback(hwnd); ++begins; return TRUE;}
inline BOOL WINAPI Update(HWND hwnd,LONG x,LONG y,BOOL inertial) {
    if(!enabled) return ::UpdatePanningFeedback(hwnd,x,y,inertial);
    ++updates; overX=x; overY=y; inertia=inertial; return TRUE;
}
inline BOOL WINAPI End(HWND hwnd,BOOL animate) {
    if(!enabled) return ::EndPanningFeedback(hwnd,animate);
    ++ends; animatedEnd=animate; return TRUE;
}
struct Session {
    bool previous=enabled;
    Session() {
        enabled=promotionSucceeds=true; failCreate=rejectPackets=promoted=false;
        begins=updates=ends=processCalls=promotions=drags=0; owner=GetCurrentThreadId();
        recognized.clear(); dragPaths.clear(); duringDrag={};
    }
    ~Session() {enabled=previous; recognized.clear(); duringDrag={};}
};
}
#define CreateInteractionContext TouchFixture::Create
#define RegisterOutputCallbackInteractionContext2 TouchFixture::Register
#define DestroyInteractionContext TouchFixture::Destroy
#define ProcessPointerFramesInteractionContext TouchFixture::Process
#define GetProcAddress TouchFixture::Procedure
#define SHDoDragDrop TouchFixture::Drag
#define BeginPanningFeedback TouchFixture::Begin
#define UpdatePanningFeedback TouchFixture::Update
#define EndPanningFeedback TouchFixture::End
