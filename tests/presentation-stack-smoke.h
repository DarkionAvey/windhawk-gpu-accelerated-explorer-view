// Observe USER's genuine position notifications on the attached private frame.
// No input, focus or paint messages are manufactured.
namespace PresentationStackFixture {
struct Probe {
    ViewState* state=nullptr;
    unsigned positions=0,sizes=0,intermediate=0;
    bool repairing=false;
};
HWND PreviousVisible(HWND hwnd) {
    while((hwnd=GetWindow(hwnd,GW_HWNDPREV)) && !IsWindowVisible(hwnd)) {}
    return hwnd;
}
bool Ordered(const ViewState& state) {
    HWND before=nullptr;
    for(HWND child:{state.rename.editor,state.viewToggle,state.hwnd}) {
        if(!child || !IsWindowVisible(child)) continue;
        if(PreviousVisible(child)!=before) return false;
        before=child;
    }
    return true;
}
LRESULT CALLBACK Observe(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    auto& probe=*reinterpret_cast<Probe*>(data);
    if(message==WM_WINDOWPOSCHANGING) ++probe.positions;
    if(message==WM_SIZE) ++probe.sizes;
    const LRESULT result=DefSubclassProc(hwnd,message,wp,lp);
    if(message==WM_WINDOWPOSCHANGED && probe.repairing && !Ordered(*probe.state)) ++probe.intermediate;
    if(message==WM_NCDESTROY) RemoveWindowSubclass(hwnd,Observe,id);
    return result;
}
void CheckStable(ViewState& state,bool rename) {
    const HWND frame=state.frameHwnd;
    if(rename) {
        state.selected=state.anchor=0;
        state.selectedFlags.assign(state.entries.size(),0); state.selectedFlags[0]=1;
        FocusFixture::focus=state.hwnd;
        BeginRename(state,0);
        Check(state.rename.editor!=nullptr,"stack fixture native rename did not open");
    }
    RefreshViewToggleForFrame(frame);
    EnsureOverlayStacking(state);
    if(rename) RepositionRenameEdit(state);
    Probe probe{&state};
    for(HWND child:{state.rename.editor,state.viewToggle,state.hwnd}) if(child)
        Check(SetWindowSubclass(child,Observe,1,reinterpret_cast<DWORD_PTR>(&probe)),"stack observer");
    HWND hidden=CreateWindowExW(0,L"STATIC",L"private hidden sibling",WS_CHILD,
        0,0,1,1,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(hidden!=nullptr,"hidden frame sibling");
    Check(SetWindowPos(hidden,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"raise hidden frame sibling");
    for(int n=0;n<3;++n) {
        RefreshViewToggleForFrame(frame);
        EnsureOverlayStacking(state);
        if(rename) RepositionRenameEdit(state);
    }
    if(probe.positions || probe.sizes) std::cout<<"STACK settled rename="<<rename
        <<" positions="<<probe.positions<<" sizes="<<probe.sizes<<std::endl;
    Check(!probe.positions && !probe.sizes,"settled owned windows fought over Z order or reacted to a hidden sibling");
    Check(Ordered(state),"settled owned presentation stack is inconsistent");
    DestroyWindow(hidden);
    probe.positions=probe.sizes=probe.intermediate=0; probe.repairing=true;
    HWND foreign=CreateWindowExW(0,L"STATIC",L"private visible sibling",WS_CHILD|WS_VISIBLE,
        0,0,320,240,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(foreign!=nullptr,"visible frame sibling");
    Check(SetWindowPos(foreign,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"raise visible frame sibling");
    EnsureOverlayStacking(state);
    probe.repairing=false;
    if(probe.intermediate || probe.sizes) std::cout<<"STACK repair rename="<<rename
        <<" intermediate="<<probe.intermediate<<" sizes="<<probe.sizes<<std::endl;
    Check(Ordered(state) && !probe.intermediate && !probe.sizes,
        "stack repair exposed intermediate owned-window order or resized the viewport");
    {
        // Explorer's own raise of a frame child reaches the hooked positioning
        // calls. It lands directly below the stack: never above it, even
        // transiently, and the stack itself is not repositioned.
        probe.positions=probe.sizes=probe.intermediate=0;
        const UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE;
        Check(NativeSetWindowPosHook(foreign,HWND_TOP,0,0,0,0,flags),"Explorer-style frame child raise");
        Check(Ordered(state) && GetWindow(state.hwnd,GW_HWNDNEXT)==foreign,"frame child raise covered the GPU stack");
        HWND top=nullptr;
        for(HWND child=GetWindow(frame,GW_CHILD);child && !top;child=GetWindow(child,GW_HWNDNEXT))
            if(IsWindowVisible(child)) top=child;
        Check(NativeSetWindowPosHook(foreign,top,0,0,0,0,flags) && Ordered(state) &&
            GetWindow(state.hwnd,GW_HWNDNEXT)==foreign,"frame child placed after a stack window covered the stack");
        HDWP batch=BeginDeferWindowPos(1);
        batch=batch ? NativeDeferWindowPosHook(batch,foreign,HWND_TOP,0,0,0,0,flags) : nullptr;
        Check(batch && EndDeferWindowPos(batch) && Ordered(state) && GetWindow(state.hwnd,GW_HWNDNEXT)==foreign,
            "deferred frame child raise covered the GPU stack");
        Check(!probe.positions && !probe.sizes,"preventing a frame child raise repositioned the GPU stack");
        Check(NativeSetWindowPosHook(foreign,HWND_BOTTOM,0,0,0,0,flags) && GetWindow(foreign,GW_HWNDNEXT)==nullptr,
            "explicit bottom placement of a frame child was altered");
    }
    DestroyWindow(foreign);
    for(HWND child:{state.rename.editor,state.viewToggle,state.hwnd}) if(child)
        RemoveWindowSubclass(child,Observe,1);
    if(rename) CancelRename(state);
    std::cout<<"PASS: stable native presentation stack, hidden-sibling no-op, grouped Z-order repair without resize; rename="<<rename<<'\n';
}
}
