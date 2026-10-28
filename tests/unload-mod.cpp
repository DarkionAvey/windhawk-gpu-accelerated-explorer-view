// Test-only exports around the actual selected source. No Explorer injection.
#undef WH_MOD_ID
#define WH_MOD_ID L"gpu-accelerated-explorer-view"
#include <windhawk_api.h>
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
#include GPU_OVERLAY_SOURCE
#include "touch-calibration.h"

extern "C" __declspec(dllexport) BOOL PrepareMessages() {return RegisterFrameHookReleaseMessage();}

extern "C" __declspec(dllexport) void Prepare(int kind,FARPROC original) {
    g_perf=false; g_unloading=false; g_backgroundShutdown=false;
    switch(kind) {
    case 0: g_dispatchOriginal=reinterpret_cast<decltype(g_dispatchOriginal)>(original); break;
    case 1: g_translateAcceleratorOriginal=reinterpret_cast<decltype(g_translateAcceleratorOriginal)>(original); break;
    case 2: g_showWindowOriginal=reinterpret_cast<decltype(g_showWindowOriginal)>(original); break;
    case 3: g_showWindowAsyncOriginal=reinterpret_cast<decltype(g_showWindowAsyncOriginal)>(original); break;
    case 4: g_setWindowPosOriginal=reinterpret_cast<decltype(g_setWindowPosOriginal)>(original); break;
    case 5: g_deferWindowPosOriginal=reinterpret_cast<decltype(g_deferWindowPosOriginal)>(original); break;
    case 6: g_raiseStructureEventOriginal=reinterpret_cast<decltype(g_raiseStructureEventOriginal)>(original); break;
    }
}
extern "C" __declspec(dllexport) void Invoke(int kind) {
    switch(kind) {
    case 0: (void)CurrentDispatchThreadState(); DispatchHook(nullptr); break;
    case 1: TranslateAcceleratorHook(nullptr,nullptr,nullptr); break;
    case 2: NativeShowWindowHook(nullptr,SW_HIDE); break;
    case 3: NativeShowWindowAsyncHook(nullptr,SW_HIDE); break;
    case 4: NativeSetWindowPosHook(nullptr,nullptr,0,0,0,0,0); break;
    case 5: NativeDeferWindowPosHook(nullptr,nullptr,nullptr,0,0,0,0,0); break;
    case 6: RaiseStructureEventHook(nullptr,StructureChangeType_ChildrenInvalidated,nullptr,0); break;
    }
}
extern "C" __declspec(dllexport) void Before() { Wh_ModBeforeUninit(); }
extern "C" __declspec(dllexport) void Unload() { Wh_ModUninit(); }
extern "C" __declspec(dllexport) BOOL BlockedPoolAtProcessExit() {
    g_unloading=false; g_backgroundShutdown=false;
    if (!CurrentDispatchThreadState()) return FALSE;
    struct ExitFixture {HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr), gate=CreateEventW(nullptr,TRUE,FALSE,nullptr);};
    static ExitFixture fixture;
    auto work=std::make_shared<TabBackground>();
    work->hwnd=reinterpret_cast<HWND>(&fixture);
    if (!StartTabPoolWorker(work,work->detailProviders[0],+[](void*)->DWORD {
            SetEvent(fixture.entered); WaitForSingleObject(fixture.gate,INFINITE); return 0;
        })) return FALSE;
    work.reset(); // The global registry is now the worker group's only owner.
    return WaitForSingleObject(fixture.entered,5000)==WAIT_OBJECT_0;
}
extern "C" __declspec(dllexport) HWND RetainedFrameAfterHookUpgrade() {
    g_unloading=false;
    WNDCLASSW type{}; type.hInstance=GetModuleHandleW(nullptr); type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"CabinetWClass"; RegisterClassW(&type);
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,type.lpszClassName,L"retained frame",WS_POPUP,
        -32000,-32000,100,100,nullptr,nullptr,type.hInstance,nullptr);
    if (!frame) return nullptr;
    ViewState first; first.frameHwnd=frame;
    InitializeFrameShellEvents(first);
    if (!first.frameLifecycleSubscribed) return nullptr;
    first.viewEvents=nullptr; // Owner admitted before the shared hook acquired its eventual identity.
    ReleaseFrameShellViewHook(first);
    return !g_shellViewHooks.contains(frame) && static_cast<ULONG_PTR>(GetWindowLongPtrW(frame,GWLP_WNDPROC))==
        GetClassLongPtrW(frame,GCLP_WNDPROC) ? frame : nullptr;
}
extern "C" __declspec(dllexport) IUnknown* RetainSink() {
    return static_cast<IDispatch*>(new ShellAutomationNavigationSink(nullptr));
}
extern "C" __declspec(dllexport) BOOL FailedInitialization() {
    const HINSTANCE module=OverlayModuleInstance();
    if(!module || module==GetModuleHandleW(nullptr)) return FALSE;
    WNDCLASSW conflict{}; conflict.hInstance=module;
    conflict.lpfnWndProc=DefWindowProcW; conflict.lpszClassName=kOverlayClass;
    if(!RegisterClassW(&conflict)) return FALSE;
    // A conflicting class in our own module must be rejected, never reused.
    if(Wh_ModInit() || g_overlayClassRegistered) return FALSE;
    WNDCLASSW remaining{};
    const bool preserved=GetClassInfoW(module,kOverlayClass,&remaining) && remaining.lpfnWndProc==DefWindowProcW;
    if(!UnregisterClassW(kOverlayClass,module) || !preserved) return FALSE;
    // The same name in the EXE has a different owner. WH_EDITING rejects the
    // later API hooks: only the DLL's newly registered class should unwind.
    conflict.hInstance=GetModuleHandleW(nullptr);
    if(!RegisterClassW(&conflict) || Wh_ModInit()) return FALSE;
    const bool unwound=!g_frameEvent && !g_writeFactory && !g_overlayClassRegistered &&
        !GetClassInfoW(module,kOverlayClass,&remaining) &&
        GetClassInfoW(conflict.hInstance,kOverlayClass,&remaining) && remaining.lpfnWndProc==DefWindowProcW;
    return UnregisterClassW(kOverlayClass,conflict.hInstance) && unwound;
}
static bool editDestroyed=false;
extern "C" __declspec(dllexport) BOOL NativeBarrierRetirement() {
    g_unloading=false; g_backgroundShutdown=false;
    WNDCLASSW type{}; type.hInstance=GetModuleHandleW(nullptr); type.lpfnWndProc=DefWindowProcW;
    for(const auto name : {L"CabinetWClass",L"CtrlNotifySink",L"SHELLDLL_DefView",L"DirectUIHWND"}) {
        type.lpszClassName=name;
        if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    if(!RegisterOverlayClass()) return FALSE;
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"barrier DLL fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,320,240,nullptr,nullptr,type.hInstance,nullptr);
    auto* state=new ViewState;
    state->frameHwnd=state->activeTabHost=frame;
    state->visible=state->inScope=state->detailsViewActive=true;
    HWND overlay=CreateWindowExW(0,kOverlayClass,L"",WS_CHILD,0,0,320,240,frame,nullptr,OverlayModuleInstance(),state);
    if(!overlay) { delete state; DestroyWindow(frame); return FALSE; }
    state->hwnd=overlay;
    ++g_instances;
    RegisterTabOverlay(frame,frame,overlay);
    InitializeFrameShellEvents(*state);
    HWND host=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,0,0,320,240,frame,nullptr,type.hInstance,nullptr);
    HWND view=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,320,240,host,nullptr,type.hInstance,nullptr);
    state->nativeHwnd=view;
    HWND list=CreateWindowExW(0,L"DirectUIHWND",L"",WS_CHILD|WS_VISIBLE,0,0,320,240,view,nullptr,type.hInstance,nullptr);
    NativeSetWindowPosHook(list,nullptr,0,0,0,0,SWP_NOZORDER|SWP_NOACTIVATE);
    const bool blocked=host && view && GetPropW(host,kSuppressedHostProperty)==overlay &&
        !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE) && list && GetPropW(list,kNativeViewportProperty);
    const bool installed=!g_shellViewHooks.empty();
#ifdef PRESENTATION_MODEL_FIELDS
    // Start the real presentation and frame threads inside this DLL, then
    // disable with an animation in flight. The host verifies actual unmapping
    // after Uninit drains every worker, rather than testing a static wrapper.
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf())))) return FALSE;
    g_frameStopping=false;
    state->folderPath=L"private-dll-presentation:"; state->loaded=true; state->total=80;
    state->firstContentSubmittedAt=1; state->featureInitializationStep=kOverlayFeatureSteps;
    state->deferColumnProviders=false; state->entries.resize(80); state->selectedFlags.assign(80,0);
    for(int i=0;i<80;++i) {
        state->entries[size_t(i)].name=L"Private item "+std::to_wstring(i);
        state->entries[size_t(i)].path=state->folderPath+std::to_wstring(i);
        state->entries[size_t(i)].folder=true;
    }
    state->presentationEntries=std::make_shared<const std::vector<Entry>>(state->entries);
    state->presentationModelFolder=state->folderPath; state->presentationOrderHash=state->displayedOrderHash;
    state->presentationModelGrouping=state->displayedGrouping;
    UpdateMetrics(*state); UpdateColumns(*state); UpdateRefreshRate(*state);
    ShowWindow(overlay,SW_SHOWNOACTIVATE);
    if(!StartPresentation(*state)) {printf("BARRIER DLL presentation startup failed\n"); return FALSE;}
    SubmitPresentation(*state);
    bool presented=false;
    const ULONGLONG deadline=GetTickCount64()+10000;
    while(!presented && GetTickCount64()<deadline) {
        {std::lock_guard lock(state->presenter->mutex);
            presented=state->presenter->receipt && state->presenter->receipt->accepted && state->presenter->receipt->ready;}
        if(!presented) Sleep(1);
    }
    if(!presented) {
        std::lock_guard lock(state->presenter->mutex);
        const auto receipt=state->presenter->receipt;
        printf("BARRIER DLL presentation timeout receipt=%d ready=%d accepted=%d frames=%llu failure=0x%08lx stage=%u\n",
            bool(receipt),receipt && receipt->ready,receipt && receipt->accepted,
            receipt ? receipt->frames : 0,receipt ? receipt->failure : S_OK,receipt ? receipt->failureStage : 0);
        return FALSE;
    }
    MoveScroll(*state,std::min(200.f,MaxScrollPx(*state))); SubmitPresentation(*state);
#endif
    // Own a real Ninput callback and an armed system-threshold hold deadline
    // when the DLL is disabled. The private digitizer never injects desktop IO.
    TouchCalibration digitizer;
    if(!digitizer.device || !StartTouchRecognizer(*state)) {
        printf("BARRIER DLL touch startup failed digitizer=%d\n",bool(digitizer.device)); return FALSE;
    }
    POINT screen{100,100};
    MapUiPoint(overlay,nullptr,screen);
    POINTER_INFO contact{};
    contact.pointerType=PT_TOUCH; contact.pointerId=1; contact.frameId=1;
    contact.sourceDevice=digitizer.device; contact.hwndTarget=overlay; contact.historyCount=1;
    contact.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_NEW|POINTER_FLAG_INRANGE|
        POINTER_FLAG_INCONTACT|POINTER_FLAG_PRIMARY|POINTER_FLAG_CONFIDENCE|POINTER_FLAG_FIRSTBUTTON;
    contact.ptPixelLocation=contact.ptPixelLocationRaw=screen;
    contact.ptHimetricLocation=contact.ptHimetricLocationRaw=digitizer.Himetric(screen);
    contact.dwTime=GetTickCount();
    LARGE_INTEGER touchClock{}; QueryPerformanceCounter(&touchClock); contact.PerformanceCount=touchClock.QuadPart;
    contact.ButtonChangeType=POINTER_CHANGE_FIRSTBUTTON_DOWN;
    FeedTouchFrames(*state,state->touchRecognizer,1,1,&contact);
    const bool touchArmed=state->touch.active && state->touch.holdDeadline && g_callbackObjects.load()!=0;
    Wh_ModBeforeUninit(); // Exercise actual disable, not just direct HWND destruction.
    RECT bounds{}; GetClientRect(list,&bounds);
    const bool retired=touchArmed && g_callbackObjects.load()==0 &&
        g_shellViewHooks.empty() && g_windowCallbacks.load()==0 && !g_instances.load() &&
        !GetPropW(host,kSuppressedHostProperty) && (GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE) &&
        !GetPropW(list,kNativeViewportProperty) && bounds.right==320 && bounds.bottom==240;
    if(!blocked || !retired) printf("BARRIER DLL blocked=%d installed=%d retired=%d hooks=%zu callbacks=%u instances=%d host=%p view=%p prop=%p style=0x%llx\n",
        blocked,installed,retired,g_shellViewHooks.size(),g_windowCallbacks.load(),g_instances.load(),host,view,
        GetPropW(host,kSuppressedHostProperty),static_cast<unsigned long long>(GetWindowLongPtrW(host,GWL_STYLE)));
    DestroyWindow(frame);
    // WinEvent delivery belongs to this UI apartment. The standalone host has
    // no Explorer message loop; drain its queued delivery/retirement while the
    // DLL is still mapped, after all production subscriptions are removed.
    MSG pending{};
    while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)) {
        TranslateMessage(&pending); DispatchMessageW(&pending);
    }
    for(const auto name : {L"CabinetWClass",L"CtrlNotifySink",L"SHELLDLL_DefView",L"DirectUIHWND"}) UnregisterClassW(name,type.hInstance);
    return blocked && retired;
}
static bool editFontAliveDuringDestroy=false;
static LRESULT CALLBACK ObserveEditDestruction(HWND window,UINT message,WPARAM wParam,LPARAM lParam,
                                              UINT_PTR id,DWORD_PTR data) {
    if(message==WM_NCDESTROY) {
        editDestroyed=true;
        LOGFONTW font{};
        editFontAliveDuringDestroy=GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)),
            sizeof(font),&font)!=0;
        RemoveWindowSubclass(window,ObserveEditDestruction,id);
    }
    return DefSubclassProc(window,message,wParam,lParam);
}
static BOOL RenameRetirement(HWND* retainedEditor) {
    WNDCLASSW type{}; type.hInstance=GetModuleHandleW(nullptr);
    if(!RegisterOverlayClass()) return FALSE;
    HWND frame=CreateWindowExW(0,L"STATIC",L"unload-fixture",0,0,0,320,240,
        nullptr,nullptr,type.hInstance,nullptr);
    auto* state=new ViewState;
    state->frameHwnd=frame;
    state->hwnd=CreateWindowExW(0,kOverlayClass,L"",WS_CHILD,0,0,320,240,
        frame,nullptr,OverlayModuleInstance(),state);
    if(!state->hwnd) { delete state; DestroyWindow(frame); return FALSE; }
    ++g_instances;
    editDestroyed=false;
    editFontAliveDuringDestroy=false;
    HWND edit=CreateWindowExW(0,L"EDIT",L"private rename fixture",WS_CHILD,
        0,0,160,28,frame,nullptr,type.hInstance,nullptr);
    state->rename.editor=edit;
    if(!edit) return FALSE;
    state->rename.font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(!state->rename.font) return FALSE;
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(state->rename.font),FALSE);
    if(!retainedEditor && !SetWindowSubclass(edit,ObserveEditDestruction,1,0)) return FALSE;
    if(!SetWindowSubclass(edit,RenameEditProc,reinterpret_cast<UINT_PTR>(RenameEditProc),
        reinterpret_cast<DWORD_PTR>(state))) return FALSE;
    // Keep a physical EDIT alive to exercise DisposeOverlayState's fallback.
    state->rename.finishing=retainedEditor!=nullptr;
    struct ForeignRead { HWND window; bool rejected=false; } read{state->hwnd};
    HANDLE reader=CreateThread(nullptr,0,[](void* context) -> DWORD {
        auto& request=*static_cast<ForeignRead*>(context);
        request.rejected=State(request.window)==nullptr;
        return 0;
    },&read,0,nullptr);
    if(!reader) return FALSE;
    WaitForSingleObject(reader,INFINITE); CloseHandle(reader);
    bool retained=false;
    {
        WindowCallbackGuard outer;
        const HWND overlay=state->hwnd;
        DestroyWindow(overlay);
        // A native call has reentered DestroyWindow. The outer callback must
        // still have its state AND resources, while new input cannot find it.
        retained=!IsWindow(overlay) && state->background && State(overlay)==nullptr && g_instances==1;
    }
    const bool disposed=g_instances==0;
    if(retainedEditor) {
        // The standalone host uses legacy comctl32, which has no named
        // GetWindowSubclass export. With no other subclasses in this fixture,
        // the native class procedure establishes that the chain is detached.
        const bool detached=IsWindow(edit) &&
            static_cast<ULONG_PTR>(GetWindowLongPtrW(edit,GWLP_WNDPROC))==GetClassLongPtrW(edit,GCLP_WNDPROC) &&
            SendMessageW(edit,WM_GETFONT,0,0)==reinterpret_cast<LRESULT>(GetStockObject(DEFAULT_GUI_FONT));
        *retainedEditor=edit;
        return retained && disposed && read.rejected && detached;
    }
    DestroyWindow(frame);
    return retained && disposed && read.rejected && editDestroyed && editFontAliveDuringDestroy && !IsWindow(edit);
}
extern "C" __declspec(dllexport) BOOL NestedRetirement() {
    return RenameRetirement(nullptr);
}
extern "C" __declspec(dllexport) HWND RetainedRenameEditor() {
    HWND editor=nullptr;
    return RenameRetirement(&editor) ? editor : nullptr;
}
