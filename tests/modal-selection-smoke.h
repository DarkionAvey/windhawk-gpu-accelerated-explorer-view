// Reproduce the live capture: the frame NEVER disables, the selection survives,
// and Cancel returns keyboard focus to CabinetWClass rather than the list.
// Capture Shell verbs only: these fixtures never execute a file operation.
class SelectionCommandMenu final : public IContextMenu {
    ULONG refs=1;
public:
    std::vector<std::pair<std::wstring,DWORD>> calls;
    std::function<void()> duringInvoke;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IContextMenu) return E_NOINTERFACE;
        *out=static_cast<IContextMenu*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU,UINT,UINT,UINT,UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR,UINT,UINT*,LPSTR,UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
        const auto* command=reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(info);
        Check(command->cbSize==sizeof(*command) && (command->fMask&CMIC_MASK_UNICODE),"shortcut lost Unicode command parameters");
        calls.emplace_back(command->lpVerbW,command->fMask);
        if(duringInvoke) duringInvoke();
        return S_OK;
    }
};
class SelectionCommandView final : public IShellView {
    ULONG refs=1;
public:
    ComPtr<IContextMenu> menu;
    HWND window=nullptr;
    unsigned resolves=0;
    ViewState* navigateDuringResolve=nullptr;
    explicit SelectionCommandView(IContextMenu* value) : menu(value) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IShellView && iid!=IID_IOleWindow) return E_NOINTERFACE;
        *out=static_cast<IShellView*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out) override { *out=window; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetItemObject(UINT flags,REFIID iid,void** out) override {
        Check(flags==SVGIO_SELECTION,"shortcut did not use the native selected group"); ++resolves;
        if(navigateDuringResolve) ++navigateDuringResolve->token;
        return menu->QueryInterface(iid,out);
    }
#define UNUSED_VIEW(name,...) HRESULT STDMETHODCALLTYPE name(__VA_ARGS__) override { return E_NOTIMPL; }
    UNUSED_VIEW(ContextSensitiveHelp,BOOL)
    UNUSED_VIEW(TranslateAccelerator,MSG*)
    UNUSED_VIEW(EnableModeless,BOOL)
    UNUSED_VIEW(UIActivate,UINT)
    UNUSED_VIEW(Refresh)
    UNUSED_VIEW(CreateViewWindow,IShellView*,LPCFOLDERSETTINGS,IShellBrowser*,RECT*,HWND*)
    UNUSED_VIEW(DestroyViewWindow)
    UNUSED_VIEW(GetCurrentInfo,LPFOLDERSETTINGS)
    UNUSED_VIEW(AddPropertySheetPages,DWORD,LPFNSVADDPROPSHEETPAGE,LPARAM)
    UNUSED_VIEW(SaveViewState)
    UNUSED_VIEW(SelectItem,PCUITEMID_CHILD,SVSIF)
#undef UNUSED_VIEW
};
static unsigned frameInputCompletionPaints = 0;
static void TestFrameKeyboardReturn(ViewState& state, SelectionFolderView& native) {
    const HWND oldHwnd=state.hwnd, oldFrame=state.frameHwnd, oldTab=state.activeTabHost;
    const bool oldScope=state.inScope, oldVisible=state.visible, oldDetails=state.detailsViewActive;
    HWND frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"CabinetWClass",L"frame-key-return",
        WS_POPUP|WS_VISIBLE,-32000,-32000,500,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND gpu=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,500,400,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,10,10,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame && gpu && edit,"frame keyboard window fixture");
    state.hwnd=state.background->hwnd=gpu; state.frameHwnd=state.activeTabHost=frame;
    state.inScope=state.visible=state.detailsViewActive=true;
    SetWindowLongPtrW(gpu,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    SetWindowLongPtrW(gpu,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(+[](HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)->LRESULT {
        if(msg==WM_PAINT) if(auto* s=State(hwnd); s && s->frameInputDeferred) {
            PAINTSTRUCT paint{}; BeginPaint(hwnd,&paint); EndPaint(hwnd,&paint);
            ++frameInputCompletionPaints;
            s->framePaintPending=s->frameInputDeferred=false;
            AcknowledgeAnimationFrame(hwnd);
            return 0;
        }
        if(msg==WM_KEYDOWN || msg==WM_CAPTURECHANGED) return OverlayProc(hwnd,msg,wp,lp);
        return DefWindowProcW(hwnd,msg,wp,lp);
    }));
    RegisterTabOverlay(frame,frame,gpu);
    const auto oldDispatch=g_dispatchOriginal;
    const auto oldAccelerator=g_translateAcceleratorOriginal;
    g_dispatchOriginal=DispatchMessageW;
    g_translateAcceleratorOriginal=+[](HWND,HACCEL,LPMSG)->int { return 0; };
    ComPtr<SelectionCommandMenu> commands; commands.Attach(new SelectionCommandMenu);
    ComPtr<SelectionCommandView> commandView; commandView.Attach(new SelectionCommandView(commands.Get()));
    commandView->window=state.nativeHwnd;
    native.commandView=commandView;
    const HWND actualForeground=GetForegroundWindow(),actualFocus=GetFocus();
    {
        FocusFixture::Session session(frame,frame);
        MSG key{}; key.hwnd=frame; key.message=WM_KEYDOWN; key.wParam=VK_UP; key.time=GetTickCount();
        {
            Select(state,7);
            Check(OverlayHasInputFocus(state),"modal frame keyboard recovery lost list focus indicators");
            Check(TranslateAcceleratorHook(frame,nullptr,&key)==TRUE && state.selected==6 && native.selected.size()==1,
                "enabled frame kept selection but swallowed Up after confirmation Cancel");
            key.wParam=VK_DOWN; DispatchHook(&key); key.wParam=VK_UP;
            Check(state.selected==7 && state.selectedFlags[7] && native.selected.size()==1,
                "frame-targeted dispatch did not select the following Shell item");
            BYTE plain[256]{};
            for(auto [code,modifier] : {std::pair{UINT(VK_DELETE),UINT(0)},
                    {UINT(VK_DELETE),UINT(VK_SHIFT)},{UINT('C'),UINT(VK_CONTROL)},{UINT('X'),UINT(VK_CONTROL)}}) {
                if(modifier) plain[modifier]=0x80;
                Check(SetKeyboardState(plain),"file shortcut modifiers");
                key.wParam=code;
                const auto count=commands->calls.size();
                Check(TranslateAcceleratorHook(frame,nullptr,&key)==TRUE && commands->calls.size()==count+1,
                    "title-bar/frame shortcut was highlighted but never reached the Shell");
                Check(commands->calls.back().first==(code==VK_DELETE ? L"delete" : code=='X' ? L"cut" : L"copy") &&
                    bool(commands->calls.back().second&CMIC_MASK_SHIFT_DOWN)==(modifier==VK_SHIFT),
                    "file shortcut changed its verb or Shift+Delete policy");
                DispatchHook(&key);
                Check(commands->calls.size()==count+2,"dispatch fallback lost a frame file shortcut");
                key.lParam=1LL<<30; TranslateAcceleratorHook(frame,nullptr,&key); key.lParam=0;
                Check(commands->calls.size()==count+2,"held shortcut repeated a destructive/native command");
                plain[modifier]=0;
            }
            Check(SetKeyboardState(plain),"clear file shortcut modifiers");
            testClipboard.Seed(DROPEFFECT_MOVE,L"C:\\private-frame-cut.txt");
            RefreshClipboardItems(state);
            key.wParam=VK_ESCAPE;
            Check(TranslateAcceleratorHook(frame,nullptr,&key)==TRUE && state.clipboardPaths.empty(),
                "frame-owned list routed arrows but not Escape Cut cancellation");
            testClipboard.Seed(DROPEFFECT_MOVE,L"C:\\private-frame-cut.txt");
            RefreshClipboardItems(state); DispatchHook(&key);
            Check(state.clipboardPaths.empty(),"frame-owned Escape dispatch did not cancel Cut");
            key.wParam=VK_UP;
        }
        auto untouched=[&] {
            Check(TranslateAcceleratorHook(frame,nullptr,&key)==0 && state.selected==7,
                "frame recovery stole a key from a modal/editor/inactive/native context");
        };
        FocusFixture::focus=edit; untouched(); FocusFixture::focus=frame;
        FocusFixture::foreground=edit; untouched(); FocusFixture::foreground=frame;
        FocusFixture::ownerEnabled=false; untouched(); FocusFixture::ownerEnabled=true;
        state.rename.editor=edit; untouched(); state.rename.editor=nullptr;
        state.activeTabHost=edit; untouched(); state.activeTabHost=frame;
        state.visible=false; untouched(); state.visible=true;
        state.detailsViewActive=false; untouched(); state.detailsViewActive=true;
        key.wParam=VK_SPACE; untouched(); key.wParam=VK_UP;
        Check(FocusFixture::focus==frame && FocusFixture::sets==0,"frame recovery changed focus instead of routing input");
        key.wParam=VK_DELETE; FocusFixture::focus=gpu;
        const auto count=commands->calls.size();
        SendMessageW(gpu,WM_KEYDOWN,VK_DELETE,0);
        Check(commands->calls.size()==count+1,"GPU-owned focus had no native Delete handler");
        FocusFixture::focus=edit; untouched();
        Check(commands->calls.size()==count+1,"editor focus invoked a file command");
        FocusFixture::focus=frame; FocusFixture::foreground=edit; untouched();
        FocusFixture::foreground=frame;
        const auto token=state.token; commandView->navigateDuringResolve=&state;
        TranslateAcceleratorHook(frame,nullptr,&key); commandView->navigateDuringResolve=nullptr;
        Check(commands->calls.size()==count+1,"native menu resolution invoked a stale directory's command");
        state.token=token;
        // Exercise both actual detour paths. This lightweight paint receiver
        // tests callback ordering; TestBatchedDeletion uses actual GPU paints.
        FrameInputFixture::Session inputQueue;
        FrameInputFixture::pending=QS_MOUSEMOVE;
        MSG move{}; move.hwnd=edit; move.message=WM_MOUSEMOVE;
        const auto pending=state.framePaintPending, deferred=state.frameInputDeferred;
        const auto deferredAt=state.frameInputDeferredAt;
        auto hold=[&] {
            state.framePaintPending=state.frameInputDeferred=true;
            state.frameInputDeferredAt=GetTickCount64();
            InvalidateRect(gpu,nullptr,FALSE);
        };
        hold();
        const auto paints=frameInputCompletionPaints;
        Check(TranslateAcceleratorHook(frame,nullptr,&move)==0 &&
            frameInputCompletionPaints==paints && state.framePaintPending,
            "unhandled accelerator presented before its input was dispatched");
        constexpr UINT restorePost=WM_APP+0x500;
        for(int n=0;n<128;++n) Check(PostMessageW(gpu,restorePost,n,0),"input completion native backlog");
        DispatchHook(&move);
        Check(frameInputCompletionPaints==paints+1 && !state.framePaintPending,
            "actual input dispatch did not resume its held frame");
        MSG post{}; unsigned remaining=0;
        while(PeekMessageW(&post,gpu,restorePost,restorePost,PM_REMOVE)) ++remaining;
        Check(remaining==128,"input callback drained native posts to make paint eligible");
        hold(); g_translateAcceleratorOriginal=+[](HWND,HACCEL,LPMSG)->int { return 1; };
        Check(TranslateAcceleratorHook(frame,nullptr,&move)==1 && frameInputCompletionPaints==paints+2 &&
            !state.framePaintPending,"handled accelerator did not release its input-deferred frame");
        g_translateAcceleratorOriginal=+[](HWND,HACCEL,LPMSG)->int { return 0; };
        hold();
        {
            InputFrameCompletion stale(&move,true);
            ++state.token;
        }
        Check(frameInputCompletionPaints==paints+2 && state.framePaintPending,
            "stale input completion painted a different directory generation");
        state.token=token;
        ResumeInputDeferredFrame(gpu,state.lifetimeToken,token,WM_MOUSEMOVE);
        Check(frameInputCompletionPaints==paints+3 && !state.framePaintPending,
            "generation guard stranded the still-valid completion");
        state.framePaintPending=pending; state.frameInputDeferred=deferred;
        state.frameInputDeferredAt=deferredAt;
    }
    Check(GetForegroundWindow()==actualForeground && GetFocus()==actualFocus,"frame keyboard fixture changed user activation/focus");
    native.commandView.Reset();
    g_dispatchOriginal=oldDispatch; g_translateAcceleratorOriginal=oldAccelerator;
    UnregisterTabOverlay(frame,frame,gpu); StopTabVisibilityObserver();
    SetWindowLongPtrW(gpu,GWLP_USERDATA,0); DestroyWindow(frame);
    state.hwnd=state.background->hwnd=oldHwnd; state.frameHwnd=oldFrame; state.activeTabHost=oldTab;
    state.inScope=oldScope; state.visible=oldVisible; state.detailsViewActive=oldDetails;
    std::cout<<"PASS: title-bar/modal frame arrows and Delete/Shift+Delete/Cut/Copy reach native Shell paths without focus changes; GPU Delete, repeat suppression, stale-view/editor/foreground guards; no real file commands\n";
}

// Selection callbacks really run during this simulated modal transaction.
// PIDLs/selection commits are real; no Shell file command is invoked.
static void TestModalSelectionTransaction(ViewState& state, SelectionFolderView& native) {
    BYTE keys[256]{}, plain[256]{};
    Check(GetKeyboardState(keys) && SetKeyboardState(plain),"modal selection keyboard fixture");
    TestFrameKeyboardReturn(state,native);
    FocusFixture::Session modal(state.frameHwnd,state.nativeHwnd);
    const auto original=state.entries;
    const auto originalIndex=state.entryPathIndex;
    const auto focusedPath=state.entries[7].path;
    auto select=[&] {
        Select(state,7); StopSelectionFade(state); StopKeyboardSelectionAnimation(state);
        state.lastInteractionTick=0; state.nativeSelectionDirty=false;
        Check(state.selected==7 && state.selectedFlags[7] && native.selected.size()==1,"modal selection fixture did not commit row 7");
    };
    select();
    OwnedPidl selected(ILClone(native.selected.front().get()));
    FocusFixture::EnableOwner(state.frameHwnd,false);
    native.selected.clear();
    Check(!CaptureNativeSelection(state,true) && state.selected==7 && state.selectedFlags[7],
        "disabled owner imported a transient empty native selection");
    Check(state.nativeSelectionDirty,"blocked modal selection was not retained for retry");
    state.nativeSelectionImportPending=true;
    state.nativeSelectionPaths.clear(); state.nativeFocusedPath.clear();
    ApplyImportedNativeSelection(state,false);
    Check(state.selected==7 && state.selectedFlags[7] && state.nativeSelectionImportPending,
        "queued empty import erased the selected index inside the confirmation dialog");
    FocusFixture::EnableOwner(state.frameHwnd,true);
    native.selected.emplace_back(ILClone(selected.get()));
    Check(CaptureNativeSelection(state,true) && state.selected==7 && state.selectedFlags[7],
        "cancelled confirmation did not retain the existing selected item");
    OverlayProc(state.hwnd,WM_KEYDOWN,VK_UP,0);
    Check(state.selected==6 && state.selectedFlags[6] && native.selected.size()==1,
        "Up after cancelling confirmation did not select the preceding Shell item");

    // Also cover an empty notification delivered AFTER the owner is enabled,
    // before the native selection recovers or the names removal is committed.
    select(); native.selected.clear();
    Check(CaptureNativeSelection(state,true) && state.selected==-1 && state.restoredKeyboardRow==7 &&
        state.postDeletionFocusPath==focusedPath && !state.selectedFlags[7],
        "empty native selection forgot the still-present keyboard item");
    OverlayProc(state.hwnd,WM_KEYDOWN,VK_DOWN,0);
    Check(state.selected==8 && state.selectedFlags[8] && native.selected.size()==1,
        "Down after a late empty cancellation snapshot lost the old row position");

    select(); native.selected.clear(); CaptureNativeSelection(state,true);
    auto publish=[&](std::vector<Entry> entries,int restore) {
        DirectoryResult result; result.kind=DirectoryResult::Complete; result.token=state.directoryToken;
        result.entries=std::move(entries); result.keyboardRestoreIndex=restore;
        result.oldTotal=state.total; result.oldRowPx=state.rowPx; result.oldScroll=state.scrollNow;
        for(size_t i=0;i<result.entries.size();++i) result.pathIndex.emplace_back(SelectionPathHash(result.entries[i].path),static_cast<int>(i));
        std::sort(result.pathIndex.begin(),result.pathIndex.end());
        result.orderHash=DirectoryOrderHash(result.entries); result.metadataHash=DirectoryMetadataHash(result.entries);
        PublishDirectoryResult(*state.background,std::move(result)); ReceiveDirectoryResults(state);
    };
    auto removed=original; removed.erase(removed.begin()+7); publish(std::move(removed),-1);
    Check(state.selected==-1 && state.vacantKeyboardRow==7 && state.restoredKeyboardRow==-1 &&
        KeyboardNavigationTarget(state,VK_UP)==6 && KeyboardNavigationTarget(state,VK_DOWN)==7,
        "confirmed deletion lost the gap after a native deselection arrived first");
    CaptureNativeSelection(state,true);
    Check(state.vacantKeyboardRow==7,"late empty selection erased the established deletion gap");
    OverlayProc(state.hwnd,WM_KEYDOWN,VK_DOWN,0);
    Check(state.selected==7 && state.selectedFlags[7] && state.entries[7].path==original[8].path && native.selected.size()==1,
        "Down after confirmed deletion did not select the following Shell item");
    publish(original,7); // Restore the private fixture's names, like an Undo snapshot.
    Check(state.entries.size()==original.size() && state.entryPathIndex==originalIndex,"modal selection fixture did not restore its snapshot");
    state.nativeSelectionDirty=false; Select(state,0);
    SetKeyboardState(keys);
    std::cout<<"PASS: modal native-selection deferral, Cancel retains index, late empty import retains keyboard identity, Delete maps the vacancy and real Up/Down commits reach Shell selection\n";
}
