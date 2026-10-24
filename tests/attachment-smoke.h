// Exercise production attachment, messages, rendering, and native suppression
// together. Only the Shell view's COM responses are substituted; file I/O and
// the renderer are real. All test windows stay off screen and never take focus.
#include <functional>
class AttachmentFolderView : public IFolderView2 {
    ULONG refs = 1;
public:
    virtual ~AttachmentFolderView() = default;
    ComPtr<IShellFolder> folder;
    UINT mode = FVM_DETAILS;
    int iconSize = 48;
    std::function<void()> modeProbe;
    std::function<HRESULT(const SORTCOLUMN*,int)> sortRequest;
    ShellGroupSpec grouping;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IFolderView && iid != IID_IFolderView2) return E_NOINTERFACE;
        *out = static_cast<IFolderView2*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetCurrentViewMode(UINT* out) override {
        auto probe=std::exchange(modeProbe,{}); if(probe) probe();
        *out = mode; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetViewModeAndIconSize(FOLDERVIEWMODE* out, int* size) override {
        UINT value = 0; const HRESULT result = GetCurrentViewMode(&value);
        *out = static_cast<FOLDERVIEWMODE>(value); *size = iconSize; return result;
    }
    HRESULT STDMETHODCALLTYPE GetFolder(REFIID iid, void** out) override { return folder->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE ItemCount(UINT, int* out) override { *out = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSelection(BOOL, IShellItemArray** out) override { *out = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetGroupBy(PROPERTYKEY* key, BOOL* ascending) override {
        *key=grouping.key; *ascending=grouping.ascending;
        return grouping.enabled ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE SetSortColumns(const SORTCOLUMN* columns,int count) override {
        return sortRequest ? sortRequest(columns,count) : E_NOTIMPL;
    }
#define UNAVAILABLE(name, ...) HRESULT STDMETHODCALLTYPE name(__VA_ARGS__) override { return E_NOTIMPL; }
    UNAVAILABLE(SetCurrentViewMode, UINT)
    UNAVAILABLE(Item, int, PITEMID_CHILD*)
    UNAVAILABLE(Items, UINT, REFIID, void**)
    UNAVAILABLE(GetSelectionMarkedItem, int*)
    UNAVAILABLE(GetFocusedItem, int*)
    UNAVAILABLE(GetItemPosition, PCUITEMID_CHILD, POINT*)
    UNAVAILABLE(GetSpacing, POINT*)
    UNAVAILABLE(GetDefaultSpacing, POINT*)
    UNAVAILABLE(GetAutoArrange)
    UNAVAILABLE(SelectItem, int, DWORD)
    UNAVAILABLE(SelectAndPositionItems, UINT, PCUITEMID_CHILD_ARRAY, POINT*, DWORD)
    UNAVAILABLE(SetGroupBy, REFPROPERTYKEY, BOOL)
    UNAVAILABLE(SetViewProperty, PCUITEMID_CHILD, REFPROPERTYKEY, REFPROPVARIANT)
    UNAVAILABLE(GetViewProperty, PCUITEMID_CHILD, REFPROPERTYKEY, PROPVARIANT*)
    UNAVAILABLE(SetTileViewProperties, PCUITEMID_CHILD, LPCWSTR)
    UNAVAILABLE(SetExtendedTileViewProperties, PCUITEMID_CHILD, LPCWSTR)
    UNAVAILABLE(SetText, FVTEXTTYPE, LPCWSTR)
    UNAVAILABLE(SetCurrentFolderFlags, DWORD, DWORD)
    UNAVAILABLE(GetCurrentFolderFlags, DWORD*)
    UNAVAILABLE(GetSortColumnCount, int*)
    UNAVAILABLE(GetSortColumns, SORTCOLUMN*, int)
    UNAVAILABLE(GetItem, int, REFIID, void**)
    UNAVAILABLE(GetVisibleItem, int, BOOL, int*)
    UNAVAILABLE(GetSelectedItem, int, int*)
    UNAVAILABLE(GetSelectionState, PCUITEMID_CHILD, DWORD*)
    UNAVAILABLE(InvokeVerbOnSelection, LPCSTR)
    UNAVAILABLE(SetViewModeAndIconSize, FOLDERVIEWMODE, int)
    UNAVAILABLE(SetGroupSubsetCount, UINT)
    UNAVAILABLE(GetGroupSubsetCount, UINT*)
    UNAVAILABLE(SetRedraw, BOOL)
    UNAVAILABLE(IsMoveInSameFolder)
    UNAVAILABLE(DoRename)
#undef UNAVAILABLE
};

// Emulate Explorer retrieving and filtering our private message without ever
// calling DispatchMessageW. No mouse, keyboard, paint or restore is injected.
static void TestColdAttachWake() {
    Check(SUCCEEDED(OleInitialize(nullptr)), "cold wake STA initialization");
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"CabinetWClass";
    RegisterClassW(&cls);
    HWND frame = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        cls.lpszClassName, L"cold-wake", WS_POPUP | WS_VISIBLE,
        -32000, -32000, 640, 480, nullptr, nullptr, cls.hInstance, nullptr);
    Check(frame != nullptr, "cold wake frame");
    FocusFixture::Session focusIsolation(frame,nullptr);
    FocusFixture::foreground=nullptr; // A background frame never owns user focus.
    for (bool minimized : {false, true, false}) {
        ShowWindow(frame, minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
        const HWND foreground = FocusFixture::Foreground(), focus = FocusFixture::Focus();
        const auto focusSets=FocusFixture::sets;
        StartColdAttachObserver();
        Check(Eventually([&] { return GetPropW(frame, kBootstrapQueuedProperty) != nullptr; }),
            "existing idle frame received no startup wake");
        MSG msg{};
        Check(PeekMessageW(&msg, frame, g_bootstrapAttachMessage, g_bootstrapAttachMessage, PM_NOREMOVE) &&
            msg.message == g_bootstrapAttachMessage && GetPropW(frame, kBootstrapQueuedProperty),
            "non-removing peek consumed the startup transaction");
        Check(PeekMessageW(&msg, frame, g_bootstrapAttachMessage, g_bootstrapAttachMessage, PM_REMOVE) &&
            msg.message == WM_NULL && !GetPropW(frame, kBootstrapQueuedProperty),
            "startup wake depended on DispatchMessage/mouse input; queue latch remained stuck");
        if (!minimized) {
            const auto began=GetTickCount64();
            Check(Eventually([&] {
                while(PeekMessageW(&msg,frame,g_bootstrapAttachMessage,g_bootstrapAttachMessage,PM_REMOVE))
                    if(GetTickCount64()-began>1200) return true;
                return false;
            }),"cold attach exhausted its retry window before a slow view became ready");
        }
        StopColdAttachObserver();
        while (PeekMessageW(&msg, frame, g_bootstrapAttachMessage, g_bootstrapAttachMessage, PM_REMOVE)) {}
        Check(FocusFixture::Foreground() == foreground && FocusFixture::Focus() == focus &&
            FocusFixture::sets==focusSets &&
            (IsIconic(frame) != FALSE) == minimized && IsWindowEnabled(frame),
            "cold wake changed focus, activation, enabled or minimized state");
        Check(!GetPropW(frame, kBootstrapQueuedProperty) && g_windowCallbacks.load() == 0,
            "cold wake unload retained its latch/callback");
        PostMessageW(frame, g_bootstrapAttachMessage, 0, 0);
        Check(PeekMessageW(&msg, frame, g_bootstrapAttachMessage, g_bootstrapAttachMessage, PM_REMOVE) &&
            msg.message == g_bootstrapAttachMessage, "cold wake hook survived observer shutdown");
    }
    DestroyWindow(frame);
    OleUninitialize();
    std::cout << "PASS: startup wake survives message filtering without DispatchMessage or input, including minimized enable and repeated observer shutdown/restart\n";
}

template<class State>
static void CheckNestedGraphicsMessages(State& state) {
    if constexpr(requires {state.presenter;}) if(state.presenter) {
        const auto sequence=state.presentationSequence;
        state.graphicsInitializing=true;
        Paint(state); ResizeTarget(state,640,480);
        state.graphicsInitializing=false;
        Check(state.visible && !state.target && !state.d3d && !state.swapChain &&
            state.presentationSequence>sequence,"reentrant bridge paint acquired graphics or failed to queue its revision");
        return;
    }
    if constexpr (requires { state.graphicsInitializing; state.resizeDeferred; }) {
        state.graphicsInitializing = true;
        const auto target = state.target.Get();
        Paint(state);
        ResizeTarget(state, 640, 480);
        Check(state.visible && state.target.Get() == target && state.resizeDeferred,
              "nested paint/resize treated in-progress graphics as a permanent failure");
        state.graphicsInitializing = false;
        state.resizeDeferred = false;
    }
}

static void TestAttachedLifecycle(const std::wstring& directory) {
    Check(SUCCEEDED(OleInitialize(nullptr)), "attachment STA initialization");
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "attachment DirectWrite");
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"CabinetWClass";
    RegisterClassW(&cls);
    cls.lpfnWndProc = OverlayProc;
    cls.lpszClassName = kOverlayClass;
    RegisterClassW(&cls);
    HWND frame = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CabinetWClass", L"attachment-smoke",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 640, 480, nullptr, nullptr, cls.hInstance, nullptr);
    HWND host = CreateWindowExW(0, L"CtrlNotifySink", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 640, 480, frame, nullptr, cls.hInstance, nullptr);
    HWND native = CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 640, 480, host, nullptr, cls.hInstance, nullptr);
    Check(frame && host && native, "attachment window hierarchy");
    FocusFixture::Session focusIsolation(frame,nullptr);
    FocusFixture::foreground=nullptr;
    const HWND nativeViewButton=CreateWindowExW(0,L"BUTTON",L"native status fixture",WS_CHILD|WS_VISIBLE,
        598,444,40,36,frame,nullptr,cls.hInstance,nullptr);
    Check(nativeViewButton!=nullptr,"native status button fixture");
    auto* response = new AttachmentFolderView;
    ComPtr<IFolderView2> view;
    view.Attach(response);
    ComPtr<IShellBrowser> browser;
    auto bindFolder = [&](const std::wstring& path) {
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
              SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&response->folder))),
              "attachment filesystem Shell folder");
    };
    bindFolder(directory);
    AttachView(native, view, response->folder, browser, directory);
    const HWND overlay = OverlayForTab(frame);
    ViewState* state = overlay ? State(overlay) : nullptr;
    Check(state && state->visible && state->inScope, "filesystem attachment fell back to native");
#ifdef PRESENTATION_MODEL_FIELDS
    SendMessageW(overlay,kMsgWorkerFailed,WPARAM(state->lifetimeToken+1),0);
    Check(state->visible,"a retired worker's failure disabled a different overlay lifetime");
#endif
    Check(IsWindow(state->viewToggle) && IsWindowVisible(state->viewToggle) &&
        GetParent(state->viewToggle)==frame,
        "native-view switch waited for toolbar focus, overflow or first content");
    Check(state->columns.empty() && NameColumnEnd(*state) > NameTextOrigin(*state) + 8,
          "deferred columns gave first-frame filenames no drawable width");
    Check(state->viewEvents, "lifecycle suppression hooks waited for first content");
    CheckNestedGraphicsMessages(*state);
    auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    };
    auto userMode=[&](UINT mode,int iconSize=48) {
        {
            NativeViewInput input(state,true);
            response->mode=mode; response->iconSize=iconSize;
        }
        MSG hint{};
        while (PeekMessageW(&hint,overlay,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE))
            OverlayProc(overlay,hint.message,hint.wParam,hint.lParam);
    };
    Check(Eventually([&] { pump(); return state->loaded && state->firstContentSubmittedAt; }),
          "attached filesystem never presented names");
    {
        const auto columns=state->columns;
        if(state->columns.empty()) state->columns.push_back({PKEY_ItemNameDisplay,L"Name"});
        HWND late=nullptr;
        {
            // Simulate a child whose creation event occurred while suppression
            // was already in a native visibility transaction.
            ScopedFlag internal(t_nativeVisibilityInternal);
            late=CreateWindowExW(0,WC_LISTVIEWW,L"late sort viewport",WS_CHILD|WS_VISIBLE,
                0,0,640,480,native,nullptr,cls.hInstance,nullptr);
        }
        Check(late && !GetPropW(late,kNativeViewportProperty),"late sorting child fixture");
        bool entered=false;
        response->sortRequest=[&](const SORTCOLUMN*,int count) {
            RECT r{}; GetClientRect(late,&r); entered=true;
            Check(count==1 && !r.right && !r.bottom && GetPropW(late,kNativeViewportProperty),
                "header sorting entered Shell with an unsuppressed late child");
            return E_NOTIMPL;
        };
        SortByHeader(*state,0);
        Check(entered,"header sort did not reach native owner");
        const auto sort=state->sort;
        const auto generation=state->directoryToken;
        ComPtr<IFolderView2> replacement; replacement.Attach(new AttachmentFolderView);
        response->sortRequest=[&](const SORTCOLUMN*,int) { state->folderView=replacement; return S_OK; };
        SortByHeader(*state,0);
        Check(SameSort(state->sort,sort) && state->directoryToken==generation,
            "reentrant native sort committed against a replacement view");
        state->folderView=view;
        response->sortRequest={};
        state->columns=columns;
        DestroyWindow(late);
    }
    // External gesture navigation can create/reparent a Shell view entirely
    // inside a sent/native dispatch, without delivering queued WinEvents.
    // Check real visibility immediately, without a message pump or GPU paint.
    const HWND foregroundAtCreation=FocusFixture::Foreground(), focusAtCreation=FocusFixture::Focus();
    for(int pass=0;pass<12;++pass) {
        HWND replacementHost=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,
            0,0,640,480,frame,nullptr,cls.hInstance,nullptr);
        HWND replacement=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,
            0,0,640,480,pass%2 ? frame : replacementHost,nullptr,cls.hInstance,nullptr);
        HWND placeholder=CreateWindowExW(0,L"STATIC",L"Working on it...",WS_CHILD|WS_VISIBLE,
            0,0,200,40,replacement,nullptr,cls.hInstance,nullptr);
        Check(replacementHost && replacement && placeholder,"replacement loading-view fixture");
        Check(!IsWindowVisible(placeholder),"new native loading placeholder escaped before queued lifecycle delivery");
        if(pass%2) {
            SetParent(replacement,replacementHost);
            NotifyWinEvent(EVENT_OBJECT_PARENTCHANGE,replacement,OBJID_WINDOW,0);
        }
        Check(GetPropW(replacementHost,kSuppressedHostProperty)==overlay &&
            !(GetWindowLongPtrW(replacementHost,GWL_STYLE)&WS_VISIBLE),
            "reparented loading host remained visible before queued lifecycle delivery");
        HWND list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT,
            0,0,400,300,replacement,nullptr,cls.hInstance,nullptr);
        RECT listBounds{}; GetClientRect(list,&listBounds);
        Check(list && GetPropW(list,kNativeViewportProperty) && listBounds.right==0 && listBounds.bottom==0,
            "replacement native viewport performed visible layout before its queued capture");
        Check(IsWindowVisible(overlay) && state->nativeHwnd==native && state->loaded,
            "synchronous suppression changed binding, contents or GPU presentation");
        DestroyWindow(replacement); DestroyWindow(replacementHost);
    }
    for(int mode=0;mode<4;++mode) {
        const bool visible=state->visible, scope=state->inScope, supported=state->detailsViewActive;
        const HWND tab=state->activeTabHost;
        if(mode==0) state->visible=false;
        if(mode==1) state->inScope=false;
        if(mode==2) state->detailsViewActive=false;
        if(mode==3) state->activeTabHost=host; // Not the selected tab identity.
        HWND bypass=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,
            0,0,640,480,frame,nullptr,cls.hInstance,nullptr);
        Check(bypass && IsWindowVisible(bypass) && !GetPropW(bypass,kSuppressedHostProperty),
            "synchronous barrier suppressed native-only, unsupported or inactive input");
        DestroyWindow(bypass);
        state->visible=visible; state->inScope=scope; state->detailsViewActive=supported; state->activeTabHost=tab;
    }
    HWND sharedHost=CreateWindowExW(0,L"CtrlNotifySink",L"unrelated navigation host",WS_CHILD|WS_VISIBLE,
        0,0,100,100,frame,nullptr,cls.hInstance,nullptr);
    HWND sharedList=CreateWindowExW(0,WC_LISTVIEWW,L"navigation list",WS_CHILD|WS_VISIBLE,
        0,0,100,100,sharedHost,nullptr,cls.hInstance,nullptr);
    Check(sharedHost && sharedList && IsWindowVisible(sharedList) &&
        !GetPropW(sharedHost,kSuppressedHostProperty) && !GetPropW(sharedList,kNativeViewportProperty),
        "presentation barrier suppressed shared chrome outside a Shell file view");
    DestroyWindow(sharedHost);
    Check(FocusFixture::Foreground()==foregroundAtCreation && FocusFixture::Focus()==focusAtCreation && !FocusFixture::sets,
        "native creation barrier changed foreground/focus");
    std::cout << "PASS: synchronous native creation/reparent/loading and viewport suppression without pumping; native/unsupported/inactive/chrome scope and unchanged GPU focus\n";
    // CREATE/SHOW precedes usable Shell host geometry during navigation.
    // The active tab must keep its last committed GPU pixels over that gap.
    for (int attempt=0;attempt<3;++attempt) {
        SetWindowPos(host,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Check(!PositionOverlayOverNative(*state) && state->nativePresentationPending &&
            IsWindowVisible(overlay) && !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
            "transient host geometry hid the active GPU surface during navigation");
        SetWindowPos(host,nullptr,0,0,640,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Check(ApplyNativePresentation(*state) && IsWindowVisible(overlay) &&
            !state->nativePresentationPending,"host geometry retry did not settle");
    }
    const HWND switchButton=state->viewToggle;
    const HWND switchTooltip=state->viewToggleTooltip;
    const HWND foregroundBeforeSwitch=FocusFixture::Foreground(), focusBeforeSwitch=FocusFixture::Focus();
    wchar_t switchCaption[64]{};
    auto switchCaptionIs=[&](const wchar_t* expected) {
        GetWindowTextW(switchButton,switchCaption,ARRAYSIZE(switchCaption));
        return wcscmp(switchCaption,expected)==0;
    };
    Check(switchCaptionIs(L"Switch to native Explorer view"),"GPU switch lost its accessible action name");
    Check(state->viewToggleIcons[0] && state->viewToggleIcons[1],"view switch icon creation failed");
    Check(SendMessageW(switchButton,BM_GETIMAGE,IMAGE_ICON,0)==reinterpret_cast<LRESULT>(state->viewToggleIcons[0]),
        "view switch did not bind a native HICON");
    ICONINFO iconInfo{}; BITMAP iconBitmap{};
    Check(GetIconInfo(state->viewToggleIcons[0],&iconInfo) &&
        GetObjectW(iconInfo.hbmColor,sizeof(iconBitmap),&iconBitmap) && iconBitmap.bmBitsPixel==32,
        "view switch image was not a 32-bit icon");
    DeleteObject(iconInfo.hbmColor); DeleteObject(iconInfo.hbmMask);
    {
        RECT button{},neighbor{}; GetClientRect(switchButton,&button); GetClientRect(nativeViewButton,&neighbor);
        Check(button.right==neighbor.right && button.bottom==neighbor.bottom && state->viewToggleNeighbor==nativeViewButton,
            "GPU switch did not match the native view button size");
        const int dpi=state->viewToggleDpi, width=button.right, height=button.bottom;
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
        void* bits=nullptr;
        HDC dc=CreateCompatibleDC(nullptr);
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        Check(dc && bitmap,"icon switch readback surface");
        HGDIOBJ previous=SelectObject(dc,bitmap);
        const COLORREF savedBackground=state->viewToggleBackground, savedForeground=state->viewToggleForeground;
        auto draw=[&](UINT flags) {
            RefreshViewToggleForFrame(frame); // Publish the changed palette to the image cache.
            DRAWITEMSTRUCT item{}; item.CtlType=ODT_BUTTON; item.hwndItem=switchButton;
            item.hDC=dc; item.rcItem={0,0,width,height}; item.itemState=flags;
            DrawViewToggle(*state,item);
            GdiFlush();
            return std::vector<DWORD>(static_cast<DWORD*>(bits),static_cast<DWORD*>(bits)+width*height);
        };
        auto pixel=[](COLORREF color) { return DWORD(GetBValue(color)) | DWORD(GetGValue(color))<<8 | DWORD(GetRValue(color))<<16; };
        for(bool dark : {false,true}) {
            state->viewToggleBackground=dark ? RGB(24,30,39) : RGB(245,248,250);
            state->viewToggleForeground=dark ? RGB(230,236,244) : RGB(30,45,60);
            state->viewToggleHot=false;
            const auto idle=draw(0);
            Check(std::all_of(idle.begin(),idle.end(),[](DWORD p){return (p>>24)==255;}),
                "icon switch published transparent GDI pixels to the compositor");
            memset(bits,0,size_t(width)*height*4);
            // Explorer/extensions can consume a parent's owner-draw message.
            // The button must paint itself with that entire route unavailable.
            SUBCLASSPROC discardDraw=[](HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)->LRESULT {
                return msg==WM_DRAWITEM ? TRUE : DefSubclassProc(hwnd,msg,wp,lp);
            };
            Check(SetWindowSubclass(frame,discardDraw,0x156,0),"parent-draw filter fixture");
            SendMessageW(switchButton,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            RemoveWindowSubclass(frame,discardDraw,0x156);
            GdiFlush();
            const auto painted=std::vector<DWORD>(static_cast<DWORD*>(bits),static_cast<DWORD*>(bits)+width*height);
            Check(painted==idle,"actual BUTTON paint did not match the icon renderer");
            Check((idle.front()&0xFFFFFF)==pixel(state->viewToggleBackground),"icon switch ignored its light/dark background");
            int ink=0;
            for(int y=0;y<height;++y) for(int x=0;x<width;++x)
                if((idle[y*width+x]&0xFFFFFF)!=pixel(state->viewToggleBackground)) {
                    ++ink;
                    Check(x>=Scale96(4,dpi) && x<width-Scale96(4,dpi),"switch painted a caption or uncentered icon");
                }
            Check(ink>10 && ink<state->viewToggleIconPx*state->viewToggleIconPx*3/4,
                "switch arrow was missing or replaced by a solid image tile");
            SendMessageW(switchButton,WM_MOUSEMOVE,0,MAKELPARAM(4,4));
            Check(state->viewToggleHot,"icon switch did not track hover");
            const auto hovered=draw(0), pressed=draw(ODS_SELECTED);
            Check(hovered!=idle && pressed!=hovered && draw(ODS_FOCUS)!=hovered && draw(ODS_DISABLED)!=hovered,
                "icon switch lost hover/press/focus/disabled feedback");
            SendMessageW(switchButton,WM_MOUSELEAVE,0,0);
            Check(!state->viewToggleHot && draw(0)==idle,"icon switch did not clear hover");
        }
        state->viewToggleBackground=savedBackground; state->viewToggleForeground=savedForeground;
        SendMessageW(switchButton,WM_THEMECHANGED,0,0);
        Check(!state->viewTogglePaletteDirty && state->viewToggleBackground!=CLR_INVALID &&
            (SendMessageW(switchButton,WM_GETDLGCODE,0,0)&DLGC_BUTTON),"icon switch lost theme refresh or native keyboard semantics");
        wchar_t tooltip[128]{}; TOOLINFOW tool{static_cast<UINT>(TTTOOLINFOW_V2_SIZE)};
        tool.hwnd=frame; tool.uId=reinterpret_cast<UINT_PTR>(switchButton); tool.lpszText=tooltip;
        SendMessageW(switchTooltip,TTM_GETTEXTW,ARRAYSIZE(tooltip),reinterpret_cast<LPARAM>(&tool));
        Check(IsWindow(switchTooltip) && wcscmp(tooltip,L"Switch to native Explorer view")==0,
            "icon switch tooltip lost its English action");
        SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    }
    // Exercise the real button notification route without BM_CLICK, which
    // could move the actual user's focus to this offscreen fixture.
    SendMessageW(frame,WM_COMMAND,MAKEWPARAM(0,BN_CLICKED),reinterpret_cast<LPARAM>(switchButton));
    Check(Eventually([&]{pump(); return !state->visible;}),"persistent switch did not select native view");
    Check(IsWindowVisible(switchButton) && switchCaptionIs(L"Switch to GPU overlay view") &&
        (GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
        "native mode hid the switch back to GPU or retained native suppression");
    SendMessageW(frame,WM_COMMAND,MAKEWPARAM(0,BN_CLICKED),reinterpret_cast<LPARAM>(switchButton));
    Check(Eventually([&]{pump(); return state->visible && IsWindowVisible(overlay);}),"persistent switch did not restore GPU view");
    Check(state->viewToggle==switchButton && switchCaptionIs(L"Switch to native Explorer view") &&
        !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),"mode toggle replaced its control or exposed native content");
    for(bool rtl : {false,true}) {
        state->rtlLayout=rtl;
        for(int width : {320,960,640}) {
            SetWindowPos(frame,nullptr,-32000,-32000,width,480,SWP_NOACTIVATE|SWP_NOZORDER);
            pump(); RefreshViewToggleForFrame(frame); EnsureOverlayTop(*state);
            RECT buttonBounds{},frameBounds{};
            GetWindowRect(switchButton,&buttonBounds);
            MapWindowPoints(HWND_DESKTOP,frame,reinterpret_cast<POINT*>(&buttonBounds),2);
            GetClientRect(frame,&frameBounds);
            Check(IsWindowVisible(switchButton) && buttonBounds.left>=0 && buttonBounds.top>=0 &&
                buttonBounds.right<=frameBounds.right && buttonBounds.bottom<=frameBounds.bottom &&
                GetWindow(overlay,GW_HWNDPREV)==switchButton,
                "resize/RTL or top-order repair clipped the permanent switch behind the renderer");
        }
    }
    state->rtlLayout=false;
    state->inScope=false; RefreshViewToggleForFrame(frame);
    Check(!IsWindowVisible(switchButton),"unsupported namespace left an active GPU switch");
    state->inScope=true;
    const HWND selectedHost=state->activeTabHost;
    state->activeTabHost=host; RefreshViewToggleForFrame(frame);
    Check(!IsWindowVisible(switchButton),"inactive tab retained its switch");
    state->activeTabHost=selectedHost; RefreshViewToggleForFrame(frame);
    Check(IsWindowVisible(switchButton) && state->viewToggle==switchButton &&
        FocusFixture::Foreground()==foregroundBeforeSwitch && FocusFixture::Focus()==focusBeforeSwitch && !FocusFixture::sets,
        "switch maintenance moved focus/activation or failed to reuse the active tab's control");
    {
        FinishColumnTransition(*state); Paint(*state);
#ifdef PRESENTATION_MODEL_FIELDS
        // A rapid native/GPU toggle can coalesce the new active frame with
        // the subsequent hide. Warm-image assertions require an actually
        // accepted image, not the owner's startup-ready capability flag.
        PresentationFixture::Await(*state,[](const auto& receipt) {return receipt.accepted && receipt.ready;});
        ConsumePresentation(*state);
#endif
        auto device=state->d3d; auto swapchain=state->swapChain;
        auto format=state->textFormat;
        LARGE_INTEGER frequency{},began{},ended{}; QueryPerformanceFrequency(&frequency);
        double activationMs=0.;
        for(unsigned cycle=0;cycle<3;++cycle) {
#ifdef PRESENTATION_MODEL_FIELDS
            const HWND activeTab=state->activeTabHost;
            state->activeTabHost=host; // A real inactive ownership state.
#endif
            ShowWindow(overlay,SW_HIDE);
            const bool warm=Eventually([&] { pump(); return state->graphicsStandby && !state->graphicsStandbyPending; });
#ifdef PRESENTATION_MODEL_FIELDS
            if(!warm) std::cout<<"ATTACHED STANDBY ready="<<state->graphicsReady<<" standby="<<state->graphicsStandby
                <<" pending="<<state->graphicsStandbyPending<<" active="<<state->presentationActive
                <<" inactive="<<InactiveGraphics(*state)<<" visible="<<IsWindowVisible(overlay)
                <<" native-pending="<<state->nativePresentationPending<<" seq="<<state->presentationSequence
                <<" accepted="<<state->presentationAcceptedSequence<<" token="<<state->token<<" directory="<<state->directoryToken<<std::endl;
#endif
            Check(warm,"attached tab did not settle into warm standby");
            const auto painted=state->graphicsPaintSerial;
#ifdef PRESENTATION_MODEL_FIELDS
            state->activeTabHost=activeTab;
            const auto acceptedPaint=state->lastAcceptedPaint;
#endif
            SetWindowRgn(host,nullptr,FALSE); // Explorer relayout on tab activation.
            AuditFixture::failComposition=true; // Return must not allocate another domain.
            QueryPerformanceCounter(&began);
            PresentCachedTabSurface(*state);
            QueryPerformanceCounter(&ended);
            activationMs=std::max(activationMs,1000.*(ended.QuadPart-began.QuadPart)/frequency.QuadPart);
            AuditFixture::failComposition=false;
#ifdef PRESENTATION_MODEL_FIELDS
            const bool noSynchronousPaint=state->lastAcceptedPaint==acceptedPaint;
#else
            const bool noSynchronousPaint=state->graphicsPaintSerial==painted;
#endif
            Check(IsWindowVisible(overlay) && !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE) &&
                state->graphicsReady && state->d3d.Get()==device.Get() && state->swapChain.Get()==swapchain.Get() &&
                state->textFormat.Get()==format.Get() && noSynchronousPaint,
                "cached tab activation exposed native content or required graphics/text rebuild before show");
            (void)painted;
        }
        std::cout<<"WARM ATTACHMENT returns=3 max-activation-cpu-ms="<<activationMs<<" (offscreen, not displayed latency)"<<std::endl;
    }
    SetWindowPos(host, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyNativePresentation(*state);
    Check(state->visible && IsWindowVisible(overlay) && state->nativePresentationPending &&
        !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
          "temporary zero-sized native layout hid GPU mode or exposed native content");
    SetWindowPos(host, nullptr, 0, 0, 640, 480, SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(overlay, kMsgFrame, 0, 0);
    Check(state->visible && IsWindowVisible(overlay), "ready native layout waited for maintenance to recover");
    const float headerBeforeMode = state->headerPx;
    userMode(FVM_LIST);
    Check(!state->columnsView && state->columnModeTransition &&
        state->headerPx == headerBeforeMode && IsWindowVisible(overlay) &&
        !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
        "native List mode bypassed the animated/exclusive presentation path");
    state->columnTransitionStartedQpc.QuadPart -= state->qpcFrequency / 12;
    AdvanceColumnTransition(*state);
    const float headerDuringMode = state->headerPx;
    userMode(FVM_DETAILS);
    Check(state->columnsView && state->columnModeTransition &&
        fabsf(state->headerPx-headerDuringMode) < .5f &&
        !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
        "native Details reversal snapped or exposed native content");
    FinishColumnTransition(*state);
    const int revealIndex=std::min(22,state->total-1);
    Check(revealIndex>=0,"mode-switch selection fixture was empty");
    state->selected=revealIndex;
    state->selectedFlags.assign(state->total,0); state->selectedFlags[revealIndex]=1;
    const HWND viewClient=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,640,480,
        native,nullptr,cls.hInstance,nullptr);
    Check(viewClient!=nullptr,"native client layout fixture");
    for (const auto modeAndSize : {std::pair{FVM_SMALLICON,16}, std::pair{FVM_ICON,48},
        std::pair{FVM_ICON,96}, std::pair{FVM_ICON,256}}) {
        SetScrollImmediate(*state,MaxScrollPx(*state));
        state->lastNativeModeProbe=GetTickCount64(); // Fresh safety cache must not delay the event.
        {
            NativeViewInput input(state);
            response->mode = modeAndSize.first; response->iconSize = modeAndSize.second;
            ShellViewEvent(state->viewEvents,EVENT_OBJECT_REORDER,viewClient,OBJID_CLIENT,0,GetCurrentThreadId(),0);
        }
        Check(state->nativeModeSignalQueued,"native layout event did not schedule mode observation");
        // Exercise the queued owner-thread handler without idle timers/input.
        MSG hint{};
        while (PeekMessageW(&hint,overlay,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE))
            DispatchMessageW(&hint);
        FinishColumnTransition(*state);
        Check(state->gridView && state->detailsViewActive && !state->columnsView &&
            state->nativeIconSize == modeAndSize.second && IsWindowVisible(overlay) &&
            !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
            "native icon size/view did not retain exclusive GPU presentation");
        Check(!state->nativeModeSignalQueued &&
            DocumentRowTop(*state,revealIndex)>=state->scrollNow-.5f &&
            DocumentRowTop(*state,revealIndex)+DisplayRowHeight(*state,revealIndex)<=
                state->scrollNow+ViewHeight(*state)+.5f,
            "view switch did not reveal the selected item before idle maintenance");
    }
    // Wheel and pinch share their implementation, but each layout family
    // must restore its own metrics, text size and tab-local preference.
    const int listHeight = state->rowHeight96[false] ? state->rowHeight96[false] : g_row96.load();
    OverlayProc(overlay,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,48*WHEEL_DELTA),0);
    const int gridHeight = state->rowHeight96[true];
    Check(state->zoomAnimating && gridHeight==g_row96.load()+96 && gridHeight>96 &&
        !state->rowHeight96[false],"grid wheel zoom changed the list zoom preference");
    state->wheelRemainder=WHEEL_DELTA/2;
    state->touchpadPinching=true; state->touchpadPinchHeight96=static_cast<float>(gridHeight);
    userMode(FVM_DETAILS); FinishColumnTransition(*state);
    Check(!state->gridView && !state->zoomAnimating && !state->touchpadPinching &&
        !state->wheelRemainder && state->rowPx==Scale96(listHeight,DpiOf(overlay)) &&
        fabsf(state->textFormat->GetFontSize()-state->fontPx)<.001f &&
        state->rowHeight96[true]==gridHeight,
        "leaving a running grid zoom leaked its size, font or input accumulator into Details");
    const bool manipulation = state->manipulationEnabled;
    state->manipulationEnabled=true;
    OverlayProc(overlay,kMsgManipulationPinch,0,
        static_cast<LPARAM>(std::lround(logf(1.08f)*65536.f)));
    const int pinchedList = static_cast<int>(std::lround(listHeight*1.08f));
    AdvanceRowHeight(*state,true);
    Check(state->rowHeight96[false]==pinchedList && state->rowHeight96[true]==gridHeight,
        "pinch resumed from the old grid height or changed the grid preference");
    state->manipulationEnabled=manipulation; state->touchpadPinching=false;
    for (UINT mode : {FVM_LIST,FVM_ICON,FVM_DETAILS,FVM_ICON}) {
        userMode(mode); FinishColumnTransition(*state);
        Check(state->rowHeight96[false]==pinchedList && state->rowHeight96[true]==gridHeight &&
            fabsf(state->fontPx-Scale96(state->gridView ? std::min(gridHeight,96) : pinchedList,DpiOf(overlay))*
                (state->gridView ? g_gridFontPercent.load() : g_fontPercent.load())/100.f)<1.f &&
            fabsf(state->textFormat->GetFontSize()-state->fontPx)<.001f &&
            state->tabPresentations[state->activeTabHost].zoomHeight96==state->rowHeight96 &&
            !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
            "List/Details/grid switches lost independent zoom, text metrics or native suppression");
    }
    std::cout << "PASS: independent list/grid wheel and pinch zoom survives interrupted zoom, repeated native view switches and text-size changes\n";
    userMode(FVM_TILE);
    Check(!IsWindowVisible(overlay) && (GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
          "native-only mode did not restore native content");
    userMode(FVM_DETAILS);
    Check(IsWindowVisible(overlay) && !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
          "Details mode did not reactivate exclusive GPU presentation");
    // Revisit a cached grouped child while Explorer replaces SHELLDLL_DefView.
    // Names/grouping stay identical: the ordinary unchanged-result fast path
    // must preserve the layout across the native view handoff.
    const auto grouped=JoinPath(directory,L"grouped-child");
    std::filesystem::create_directory(grouped);
    for(const auto* name : {L"Alpha.txt",L"Another.txt",L"Beta.txt"}) {
        HANDLE file=CreateFileW(JoinPath(grouped,name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,
            nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"grouped revisit fixture"); CloseHandle(file);
    }
    auto bindReplacement=[&](const std::wstring& path) {
        const HWND old=native;
        native=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,
            0,0,640,480,host,nullptr,cls.hInstance,nullptr);
        Check(native!=nullptr,"replacement grouped native view");
        bindFolder(path); AttachView(native,view,response->folder,browser,path);
        Check(state->nativeHwnd==native,"grouped view did not rebind");
        DestroyWindow(old);
    };
    auto settled=[&] {
        return Eventually([&] {
            pump();
            // An unchanged history restore requires a scan covered by the
            // installed watch, not merely a scan that beat watch creation.
            return state->loaded && !state->namesPreview && !state->directoryPending &&
                state->directoryWatch && state->validatedFolderStamp == state->directoryWatch->stamp &&
                state->validatedFolderRevision == state->directoryWatch->stamp->revision &&
                WaitForSingleObject(state->directoryWatch->change,0) == WAIT_TIMEOUT;
        });
    };
    response->grouping.enabled=true; response->grouping.key=PKEY_FileName;
    bindReplacement(grouped);
    Check(settled() && state->sections.size()==2,"initial child grouping did not resolve");
    for(int revisit=0;revisit<2;++revisit) {
        BeginOptimisticNavigation(*state,directory);
        response->grouping.enabled=false;
        bindReplacement(directory);
        Check(settled() && state->sections.empty(),"ungrouped parent inherited child headings");
        BeginOptimisticNavigation(*state,grouped);
        Check(state->loaded && state->sections.size()==2 && state->optimisticStartedAt,
            "cached child did not show its groups immediately");
        const auto order=state->displayedOrderHash;
        const auto rows=state->rowGroup;
        response->grouping.enabled=true;
        bindReplacement(grouped);
        Check(state->sections.size()==2 && state->rowGroup==rows,
            "native view confirmation erased cached group headers before revalidation");
        Check(settled() && state->sections.size()==2 && state->rowGroup==rows &&
            state->displayedOrderHash==order && SameShellGroup(state->displayedGrouping,response->grouping),
            "unchanged revalidation lost the cached grouping");
        ToggleGroupSection(*state,0);
        bindReplacement(grouped); // Also cover a same-folder native HWND change.
        Check(state->sections.size()==2 && state->sections[0].collapsed && state->rowGroup==rows,
            "same-folder native rebind erased group/collapse geometry");
    }
    // A real Group by None still owns the normal worker/layout transition.
    response->grouping.enabled=false; ReconcileShellGrouping(*state,true);
    Check(settled() && state->sections.empty() && state->rowGroup.empty(),"Group by None kept stale headings");
    response->grouping.enabled=true; ReconcileShellGrouping(*state,true);
    Check(settled() && state->sections.size()==2,"re-enabling grouping did not rebuild headings");
    response->grouping.enabled=false;
    bindReplacement(directory);
    Check(settled() && state->sections.empty(),"grouped-child return left stale headings in the parent");
    std::cout<<"PASS: cached grouped child survives repeated parent returns, native HWND confirmation, unchanged refresh and collapsed-group rebind; Group by None remains authoritative\n";
    // Establish actual user choices, so each folder carries its own mode
    // alongside its cached names. Binding a different Shell default is not input.
    bindReplacement(grouped); Check(settled(),"grid preference fixture did not load");
    userMode(FVM_ICON,96); FinishColumnTransition(*state); Paint(*state);
    bindReplacement(directory); Check(settled(),"list preference fixture did not load");
    userMode(FVM_DETAILS); FinishColumnTransition(*state); Paint(*state);

    // Reproduce destination rows winning the race against Shell mode restore.
    // History and toolbar/key predictions converge on this optimistic ingress.
    for (bool grid : {true,false,true,false}) {
        FinishColumnTransition(*state);
        const auto destination=grid ? grouped : directory;
        const auto sourceNative=state->nativeHwnd;
        {
            NativeViewInput input(state); // Navigation must discard toolbar intent.
            BeginOptimisticNavigation(*state,destination);
        }
        Check(state->optimisticStartedAt && state->loaded && state->directoryModePinned &&
            state->gridView==grid && state->columnsView==!grid && !state->nativeModeBeforeCommand,
            "cached destination rows arrived before their own layout or retained old input intent");
        Paint(*state);
        if (!grid) {
            const int child=GpuRowForNativePath(*state,grouped);
            Check(child>=0 && state->selected==child,"grid-to-parent return did not restore its child selection");
            const auto visual=LayoutRowVisual(*state,child,state->rowTransitionPresentedProgress);
            const float alpha=ItemContentOpacity(*state,state->entries[child],true);
            if(visual.opacity!=1.f || alpha!=1.f)
                std::cout<<"PARENT ALPHA row="<<visual.opacity<<" content="<<alpha<<" motion="
                    <<state->rowTransitionActive<<" attrs="<<state->entries[child].attributes<<std::endl;
            Check(visual.opacity==1.f && alpha==1.f,"grid-to-parent navigation dimmed the returned child");
        }
        const auto origin=state->columnTransitionStartedQpc.QuadPart;
        const int packing=state->itemsPerRow;
        // Keep the native view in the DEPARTING mode across the new HWND bind.
        Check(state->nativeHwnd==sourceNative,"optimistic mode queried/replaced the old native view");
        bindReplacement(destination);
        Check(state->gridView==grid && state->itemsPerRow==packing &&
            state->columnTransitionStartedQpc.QuadPart==origin,
            "native confirmation changed an already presented destination layout");
        response->mode=grid ? FVM_LIST : FVM_ICON; response->iconSize=256;
        for (int i=0;i<8;++i) {
            state->lastNativeModeProbe=0;
            ShellViewEvent(state->viewEvents,EVENT_OBJECT_REORDER,native,OBJID_CLIENT,0,GetCurrentThreadId(),0);
            OverlayProc(overlay,kMsgShellVisibilityChanged,2,0);
            ApplyNativePresentation(*state); Paint(*state);
        }
        Check(state->gridView==grid && state->columnsView==!grid && state->itemsPerRow==packing &&
            state->columnTransitionStartedQpc.QuadPart==origin,
            "late Shell mode/reorder/presentation changed or restarted the committed layout");
        // Clicking an unrelated control while native mode differs is NOT a
        // request to adopt that difference. Compare native before/after input.
        { NativeViewInput input(state); }
        OverlayProc(overlay,kMsgShellVisibilityChanged,2,0);
        Check(state->gridView==grid,"unrelated user input adopted an ignored native mode");
        response->mode=grid ? FVM_ICON : FVM_DETAILS; response->iconSize=96;
        FinishColumnTransition(*state); Paint(*state);
        Check(settled(),"committed navigation revalidation did not settle");
    }
    userMode(FVM_ICON,192); FinishColumnTransition(*state); Paint(*state);
    Check(state->gridView && state->layoutIconSize==192,"user could not override a pinned folder mode");
    userMode(FVM_DETAILS); FinishColumnTransition(*state); Paint(*state);
    // A first visit can publish names before Explorer supplies its folder
    // preference. Show immediately, then keep that first displayed layout.
    const auto cold=JoinPath(directory,L"mode-cold-child");
    const auto coldReady=JoinPath(directory,L"mode-cold-ready");
    std::filesystem::create_directory(cold); std::filesystem::create_directory(coldReady);
    const auto filePath=JoinPath(cold,L"cold-name.txt");
    HANDLE modeFile=CreateFileW(filePath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
        CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(modeFile!=INVALID_HANDLE_VALUE,"cold mode fixture"); CloseHandle(modeFile);
    Check(!state->folderCache.folders.contains(cold),"cold mode fixture was already cached");
    BeginOptimisticNavigation(*state,cold);
    Check(Eventually([&] { ReceiveDirectoryResults(*state); Paint(*state);
        return state->loaded && state->directoryModePinned; }),"cold preview did not submit first content");
    Check(!state->gridView && state->layoutNativeMode==FVM_DETAILS,
        "cold preview adopted a mode from the departing native view");
    response->mode=FVM_ICON; response->iconSize=256;
    bindReplacement(cold); ApplyNativePresentation(*state); Paint(*state);
    Check(!state->gridView && state->layoutNativeMode==FVM_DETAILS,
        "late native mode changed first-visit rows already shown to the user");
    Check(settled(),"cold preview confirmation did not settle");
    userMode(FVM_ICON,256); FinishColumnTransition(*state); Paint(*state);
    Check(state->gridView && state->layoutIconSize==256,
        "cold pinned preview rejected an explicit request matching native mode");
    // Without an early preview, a valid incoming native mode is available
    // before the worker publishes names and must be chosen immediately.
    response->mode=FVM_LIST;
    bindReplacement(coldReady);
    Check(!state->gridView && !state->columnsView && state->layoutNativeMode==FVM_LIST,
        "confirmed cold destination did not choose its native mode before rows");
    Check(Eventually([&] { pump(); Paint(*state); return state->loaded && state->directoryModePinned; }),
        "empty destination never committed its mode");
    response->mode=FVM_ICON; state->lastNativeModeProbe=0;
    ApplyNativePresentation(*state);
    Check(state->layoutNativeMode==FVM_LIST,"empty first-content frame failed to pin its layout");
    response->mode=FVM_DETAILS; bindReplacement(directory);
    Check(settled(),"parent return after cold mode fixtures did not settle");
    FinishColumnTransition(*state); Paint(*state);
    std::cout<<"PASS: destination cache restores layout before rows; delayed Shell modes, rebind/reorder bursts and unrelated input cannot change committed layouts; explicit changes remain immediate\n";
    // Follow the real navigation/rebind/cache path, not merely an in-place
    // mode toggle. The owner thread deliberately stops dispatching after the
    // initial commit, matching the shared DirectUI paint stall in the logs.
    for (bool grid : {true,false,true,false}) {
        FinishColumnTransition(*state);
        response->mode=grid ? FVM_ICON : FVM_DETAILS;
        response->iconSize=96;
        bindReplacement(grid ? grouped : directory);
        Check(state->gridView==grid && state->columnModeTransition && state->loaded,
            "cached cross-directory mode did not start against the destination rows");
        Paint(*state);
#ifdef PRESENTATION_MODEL_FIELDS
        const auto presented=PresentationFixture::Await(*state,[](const auto& r) {return r.accepted && r.scenePublished;});
        const float header=state->headerPx;
        Sleep(65); // No owner dispatch; presentation and DWM continue independently.
        const auto after=PresentationFixture::Await(*state,[&](const auto& r) {return r.accepted && r.frames>presented->frames &&
            r.compositionTime.QuadPart>presented->compositionTime.QuadPart;});
        Check(state->headerPx==header && !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
            "blocked owner changed its model or exposed native content");
        ConsumePresentation(*state);
        Check(grid ? state->headerPx<header : state->headerPx>header,"input geometry did not catch up to the presentation owner");
        FinishColumnTransition(*state); Paint(*state);
        PresentationFixture::Await(*state,[](const auto& r) {return r.accepted && !r.columns && !r.scenePublished;});
#else
        Check(state->modeComposition.published,"cross-directory mode waited for native idle instead of committing DWM motion");
        const auto origin=state->modeComposition.started.QuadPart;
        const float header=state->headerPx;
        DCOMPOSITION_FRAME_STATISTICS before{},after{};
        Check(SUCCEEDED(state->composition->GetFrameStatistics(&before)),"composition clock before owner pause");
        Sleep(65); // Test process only: no input, activation or live Explorer work.
        Check(SUCCEEDED(state->composition->GetFrameStatistics(&after)) &&
            after.currentTime.QuadPart>before.currentTime.QuadPart &&
            state->modeComposition.started.QuadPart==origin && state->headerPx==header &&
            !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
            "owner pause reset the committed animation, changed CPU geometry or exposed native content");
        AdvanceColumnTransition(*state);
        Check(grid ? state->headerPx<header : state->headerPx>header,"CPU geometry did not catch up to the same absolute compositor clock");
        FinishColumnTransition(*state); Paint(*state);
        Check(!state->modeComposition.started.QuadPart,"destination settlement retained temporary composition planes");
#endif
        Check(settled(),"cross-directory mode revalidation did not settle");
    }
    std::cout<<"PASS: cached Details/grid folder navigation commits independent DWM curves before a 65 ms owner pause; absolute geometry catch-up, suppression and plane retirement\n";
    // Reverse navigation without settling the preceding scene. Late native
    // layout notifications must never reveal the swap chain's old header pose.
    for (int pass=0;pass<8;++pass) {
        const bool grid=(pass&1)==0;
        response->mode=grid ? FVM_ICON : FVM_DETAILS;
        bindReplacement(grid ? grouped : directory);
        Check(state->gridView==grid && state->columnModeTransition && state->loaded,
            "rapid cached navigation lost its destination or mode transition");
        QueryPerformanceCounter(&state->columnTransitionStartedQpc);
        state->columnTransitionStartedQpc.QuadPart-=static_cast<LONGLONG>(state->qpcFrequency*(pass<4 ? .045 : .165));
        AdvanceColumnTransition(*state);
        Paint(*state);
#ifdef PRESENTATION_MODEL_FIELDS
        const auto presented=PresentationFixture::Await(*state,[](const auto& r) {return r.accepted && r.scenePublished;});
        const float header=state->headerPx;
        RECT client{}; GetClientRect(overlay,&client);
        SendMessageW(overlay,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom));
        // A no-op native size echo cannot synchronously tear down the render
        // owner's accepted image. Inspect its next versioned receipt.
        Paint(*state);
        const auto after=PresentationFixture::Await(*state,[](const auto& r) {return r.accepted;});
        Check(after->device==presented->device && after->swapchain==presented->swapchain &&
            after->backbuffer==presented->backbuffer && after->columnOrigin.QuadPart==presented->columnOrigin.QuadPart &&
            !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
            "late layout notification replaced the warm render domain or column intent");
        Check(grid ? after->headerHeight<=presented->headerHeight+.5f : after->headerHeight>=presented->headerHeight-.5f,
            "late layout/repaint moved content toward the old header offset");
        (void)header;
#else
        Check(state->modeRootAttached && state->modeComposition.published,"rapid navigation did not publish its scene");
        const auto root=state->modeComposition.root.Get();
        const auto buffer=state->backbuffer.Get();
        const auto origin=state->modeComposition.started.QuadPart;
        const float header=state->headerPx;
        const auto source=LayoutRowVisual(*state,0,1.f);
        RECT client{}; GetClientRect(overlay,&client);
        SendMessageW(overlay,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom));
        Check(state->modeRootAttached && state->modeComposition.published &&
            state->modeComposition.root.Get()==root && state->backbuffer.Get()==buffer &&
            state->modeComposition.started.QuadPart==origin && state->headerPx==header,
            "late layout notification exposed the old full-header frame during rapid navigation");
        Paint(*state);
        Check(state->modeRootAttached && fabsf(LayoutRowVisual(*state,0,1.f).top-source.top)<.01f,
            "late layout/repaint moved grid contents to the old header offset");
#endif
    }
    FinishColumnTransition(*state); Paint(*state);
#ifdef PRESENTATION_MODEL_FIELDS
    PresentationFixture::Await(*state,[](const auto& r) {return r.accepted && !r.scenePublished && !r.columns;});
#endif
    Check(!state->modeRootAttached && !state->modeComposition.started.QuadPart &&
        state->headerPx==Scale96(kHeader96,DpiOf(overlay)),"rapid reversal settlement lost the final Details geometry");
    Check(settled(),"rapid cached navigation revalidation did not settle");
    std::cout<<"PASS: eight interrupted cached Details/grid navigations retain their committed scene through late layout/repaint; near-end reversals and final settlement\n";
    AttachView(native, view, response->folder, browser, kThisPcLocation);
    Check(state->thisPcGpu && state->viewEvents, "This PC transition lost lifecycle hooks");
    KillTimer(overlay, kTimerId);
    Check(Eventually([&] { pump(); return state->loaded && !state->entries.empty(); }),
          "namespace enumeration waited for the permanently empty native model");
    Check(static_cast<bool>(state->entries[0].shellIdentity), "namespace entry had no owned identity");
    {
        // Replacing the native HWND for the same namespace must not clear a
        // committed model or restart the mode animation while names revalidate.
        const auto token=state->token;
        const auto count=state->total;
        const auto path=state->entries[0].path;
        HWND replacement=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,
            0,0,640,480,host,nullptr,cls.hInstance,nullptr);
        Check(replacement!=nullptr,"This PC replacement view fixture");
        AttachView(replacement,view,response->folder,browser,kThisPcLocation);
        Check(state->loaded && state->token==token && state->total==count &&
            state->entries[0].path==path && IsWindowVisible(overlay),
            "same-namespace native replacement cleared committed This PC contents");
        AttachView(native,view,response->folder,browser,kThisPcLocation);
        DestroyWindow(replacement);
        Check(state->loaded && state->token==token,"return native rebind reset This PC contents");
    }
    for (UINT mode : {FVM_TILE,FVM_ICON,FVM_SMALLICON,FVM_THUMBNAIL}) {
        userMode(mode,48); FinishColumnTransition(*state);
        const auto item=LayoutItem(*state,LayoutRowVisual(*state,0,1.f));
        Check(state->detailsViewActive && DriveTiles(*state) && !state->columnsView &&
            IsWindowVisible(overlay) && !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE) &&
            !item.wrapped && item.text.left>=item.icon.right && item.capacity.top>item.text.bottom,
            "This PC Tile/icon view did not retain exclusive GPU drive tiles");
    }
    userMode(FVM_CONTENT);
    Check(!IsWindowVisible(overlay) && (GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
        "This PC tiles accidentally admitted Content view");
    userMode(FVM_TILE); FinishColumnTransition(*state);
    Check(IsWindowVisible(overlay) && state->detailsViewActive && DriveTiles(*state),
        "This PC tiles did not reactivate after native fallback");
    // Bind/enumerate the real Recycle Bin read-only. Its contents may be empty
    // or populated; no fixture is put into it and no native verb is invoked.
    PIDLIST_ABSOLUTE recycleRoot=nullptr;
    Check(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder,0,nullptr,&recycleRoot)) &&
        recycleRoot,"Recycle Bin known-folder identity");
    OwnedPidl recyclePidl(recycleRoot);
    ComPtr<IShellItem> recycleItem;
    Check(SUCCEEDED(SHCreateItemFromIDList(recycleRoot,IID_PPV_ARGS(&recycleItem))) &&
        SUCCEEDED(recycleItem->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),
        "Recycle Bin Shell folder binding");
    std::wstring resolved; ComPtr<IShellFolder> recognized;
    Check(GetFolderPath(view.Get(),resolved,recognized) && IsRecycleBinLocation(resolved),
        "Recycle Bin namespace fell through the filesystem-only scope gate");
    AttachView(native,view,response->folder,browser,kRecycleBinLocation);
    Check(state->inScope && state->visible && !state->thisPcGpu &&
        Eventually([&] { pump(); return state->loaded || state->total>0; }),
        "Recycle Bin did not publish names without an already populated native model");
    userMode(FVM_TILE);
    Check(!state->detailsViewActive && !state->gridView && !IsWindowVisible(overlay) &&
        (GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
        "This PC Tile exception leaked into Recycle Bin");
    for (UINT mode : {FVM_LIST,FVM_DETAILS,FVM_ICON}) {
        userMode(mode,96); FinishColumnTransition(*state);
        Check(IsWindowVisible(overlay) && state->detailsViewActive &&
            state->gridView==(mode==FVM_ICON) && state->columnsView==(mode==FVM_DETAILS) &&
            !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),
            "Recycle Bin Details/List/grid mode lost exclusive GPU presentation");
    }
    if (state->total>0) {
        const auto& item=state->entries.front();
        Check(item.path.starts_with(L"windhawk:pidl:") && item.shellIdentity && item.shellChild,
            "Recycle Bin exposed a display/backing path as command identity");
        ComPtr<IContextMenu> menu;
        PCUITEMID_CHILD child=item.shellChild.get();
        Check(SUCCEEDED(response->folder->GetUIObjectOf(native,1,&child,IID_IContextMenu,
            nullptr,reinterpret_cast<void**>(menu.GetAddressOf()))) && menu,
            "Recycle Bin native item menu could not bind the enumerated child identity");
    }
    const auto recyclePropertyEpoch=state->propertyEpoch;
    // This is an unmodified synthetic F5, independent of the user's Alt state
    // (including state retained by earlier test message pumps). Like the other
    // key fixtures, change only this private thread's keyboard-state table.
    BYTE savedRefreshKeys[256]{},refreshKeys[256]{};
    Check(GetKeyboardState(savedRefreshKeys) && SetKeyboardState(refreshKeys),"Recycle Bin refresh key fixture");
    const auto refreshAlt=GetKeyState(VK_MENU);
    const auto refreshOptimistic=state->optimisticStartedAt;
    SendMessageW(overlay,WM_KEYDOWN,VK_F5,0);
    const bool refreshAccepted=state->propertyEpoch!=recyclePropertyEpoch;
    Check(SetKeyboardState(savedRefreshKeys),"Recycle Bin refresh key restoration");
    const bool recycleRefreshed=refreshAccepted && settled();
    if(!recycleRefreshed) std::cerr<<"RECYCLE F5 epoch="<<recyclePropertyEpoch<<"->"<<state->propertyEpoch
        <<" loaded="<<state->loaded<<" pending="<<state->directoryPending<<" native-pending="<<state->nativeRowsPending
        <<" optimistic="<<state->optimisticStartedAt<<" in-scope="<<state->inScope<<" visible="<<state->visible
        <<" active="<<state->detailsViewActive<<" token="<<state->token<<" preview="<<state->namesPreview
        <<" before-alt="<<refreshAlt<<" before-optimistic="<<refreshOptimistic
        <<" alt="<<GetKeyState(VK_MENU)<<" control="<<GetKeyState(VK_CONTROL)
        <<" clipboard-items="<<state->clipboardPaths.size()<<" clipboard-visible="<<state->clipboardIconsVisible<<"\n";
    Check(recycleRefreshed,
        "Recycle Bin F5 did not invalidate metadata and complete namespace revalidation");
    response->mode=FVM_DETAILS; state->lastNativeModeProbe=0;
    const std::wstring root = std::filesystem::path(directory).root_path().wstring();
    const std::wstring destination = root;
    response->folder.Reset();
    bindFolder(destination);
    AttachView(native, view, response->folder, browser, destination);
    Check(Eventually([&] { pump(); return !state->thisPcGpu && state->loaded; }) &&
          state->visible && state->inScope && IsWindowVisible(overlay) &&
          !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE), "This PC to filesystem root regressed to native");
    DestroyWindow(overlay);
    Check(!IsWindow(switchButton) && !IsWindow(switchTooltip),"overlay disposal leaked its view switch/tooltip");
    PIDLIST_ABSOLUTE computerRoot=nullptr;
    ComPtr<IShellItem> computerItem;
    Check(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder,0,nullptr,&computerRoot)),
        "This PC startup identity");
    OwnedPidl computerPidl(computerRoot);
    Check(SUCCEEDED(SHCreateItemFromIDList(computerRoot,IID_PPV_ARGS(&computerItem))) &&
        SUCCEEDED(computerItem->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),
        "This PC startup folder");
    response->mode=FVM_TILE; response->iconSize=48;
    AttachView(native,view,response->folder,browser,kThisPcLocation);
    const HWND tileStartup=OverlayForTab(frame);
    auto* tileState=tileStartup ? State(tileStartup) : nullptr;
    const bool tileReady=tileState && tileState->graphicsReady && tileState->detailsViewActive && DriveTiles(*tileState) &&
        !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE) &&
        Eventually([&] { pump(); return tileState->loaded && tileState->firstContentSubmittedAt && tileState->directoryModePinned; });
#ifdef PRESENTATION_MODEL_FIELDS
    if(!tileReady && tileState) std::cout<<"TILE STARTUP ready="<<tileState->graphicsReady<<" visible="<<tileState->visible
        <<" scoped="<<tileState->inScope<<" supported="<<tileState->detailsViewActive<<" grid="<<tileState->gridView
        <<" tile="<<DriveTiles(*tileState)<<" loaded="<<tileState->loaded<<" total="<<tileState->total
        <<" first="<<tileState->firstContentSubmittedAt<<" pinned="<<tileState->directoryModePinned
        <<" seq="<<tileState->presentationSequence<<" accepted="<<tileState->presentationAcceptedSequence
        <<" stage="<<tileState->graphicsFailureStage<<" hr="<<std::hex<<tileState->graphicsFailure<<std::dec<<std::endl;
#endif
    Check(tileReady,"fresh This PC Tile attachment waited for a mode change or exposed native content");
    DestroyWindow(tileStartup);
    response->folder.Reset(); bindFolder(destination);
    response->mode = FVM_TILE;
    AttachView(native, view, response->folder, browser, destination);
    const HWND nativeFirst = OverlayForTab(frame);
    state = nativeFirst ? State(nativeFirst) : nullptr;
    Check(state && state->viewEvents && !state->graphicsReady && !IsWindowVisible(nativeFirst),
          "initial native-only mode had no lifecycle hooks or exposed a GPU surface");
    response->mode = FVM_DETAILS;
    state->lastNativeModeProbe = 0;
    ApplyNativePresentation(*state);
#ifdef PRESENTATION_MODEL_FIELDS
    if(!state->graphicsReady || (!state->directoryPending && !state->loaded))
        std::cout<<"NATIVE TO GPU ready="<<state->graphicsReady<<" suspended="<<state->graphicsSuspended
            <<" visible="<<state->visible<<" scope="<<state->inScope<<" supported="<<state->detailsViewActive
            <<" mode="<<state->cachedNativeMode<<" observed="<<response->mode<<" presenter="<<bool(state->presenter)
            <<" loaded="<<state->loaded<<" directory-pending="<<state->directoryPending<<" initializing="<<state->overlayInitializing
            <<" owns="<<TabOwnsActiveView(*state)<<" pinned="<<state->directoryModePinned
            <<" stage="<<state->graphicsFailureStage<<" hr="<<std::hex<<state->graphicsFailure<<std::dec<<std::endl;
#endif
    Check(state->graphicsReady && (state->directoryPending || state->loaded),
          "native-only initial attachment waited for maintenance to start GPU mode");
    // Production dispose/reattach models unload/reload while minimized. Use
    // nonactivating show states so the user's foreground app is untouched.
    HWND current = nativeFirst;
    for (int cycle = 0; cycle < 3; ++cycle) {
        ShowWindow(frame, SW_SHOWMINNOACTIVE);
        pump();
        const HWND foreground = FocusFixture::Foreground(), focus = FocusFixture::Focus();
        Check(IsIconic(frame) && IsWindowEnabled(frame), "attachment minimize fixture");
        NativeShowWindowHook(host, SW_HIDE);
        SendMessageW(current, kMsgDispose, 0, 0);
        Check(!IsWindow(current) && !GetPropW(host, kSuppressedHostProperty) &&
            (GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
            "minimized disposal left an orphaned hidden native host");
        Check(g_shellViewHooks.empty(), "minimized disposal retained frame hook ownership");
        Check(static_cast<ULONG_PTR>(GetWindowLongPtrW(frame, GWLP_WNDPROC)) ==
            GetClassLongPtrW(frame, GCLP_WNDPROC), "minimized disposal retained a frame subclass");
        AttachView(native, view, response->folder, browser, destination);
        current = OverlayForTab(frame);
        state = current ? State(current) : nullptr;
        Check(state && state->visible && state->graphicsReady && state->viewEvents &&
            !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
            "minimized reattachment failed or exposed native content");
        const auto step = state->featureInitializationStep;
        SendMessageW(current, kMsgInitializeFeatures, state->lifetimeToken, 0);
        SendMessageW(current, kMsgFrame, 0, 0);
        SendMessageW(current, WM_TIMER, kTimerId, 0);
        Check(state->featureInitializationStep == step && !state->framePaintPending,
            "minimized attachment ran optional UI work or held a frame wake");
        AcquireSRWLockShared(&g_frameLock);
        const bool frameRegistered = std::any_of(g_activeFrames.begin(), g_activeFrames.end(),
            [&](const auto& f) { return f.hwnd == current; });
        ReleaseSRWLockShared(&g_frameLock);
        Check(!frameRegistered,
            "minimized attachment kept compositor polling alive");
        Check(SendMessageW(frame, WM_QUERYOPEN, 0, 0) != FALSE,
            "overlay vetoed the normal restore query");
        Check(IsIconic(frame) && FocusFixture::Foreground() == foreground && FocusFixture::Focus() == focus && !FocusFixture::sets,
            "reattach/restore query changed iconic state, focus or activation");
        ShowWindow(frame, SW_SHOWNOACTIVATE);
        Check(Eventually([&] { pump(); return state->loaded && state->firstContentSubmittedAt &&
            IsWindowVisible(current); }), "first restore did not resume GPU presentation");
        Check(!IsIconic(frame) && IsWindowEnabled(frame) &&
            !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE),
            "restored overlay exposed native content or altered enabled state");
    }
    DestroyWindow(current);
    DestroyWindow(frame);
    StopTabVisibilityObserver();
#ifdef PRESENTATION_MODEL_FIELDS
    // Production resets the process-wide factory only after draining workers
    // at unload. This fixture resets it between cases, so observe that same
    // boundary for the now-asynchronous render owners of its closed windows.
    std::vector<std::shared_ptr<TabBackground>> retiring;
    {
        std::lock_guard lock(g_backgroundLifetimeMutex);
        for(const auto& work:g_backgroundLifetimes)
            if(work->stopping.load() && work->presentation.thread) retiring.push_back(work);
    }
    for(const auto& work:retiring) Check(WaitForSingleObject(work->presentation.thread,10000)==WAIT_OBJECT_0,
        "closed attachment renderer retained the shared factory");
#endif
    g_writeFactory.Reset();
    OleUninitialize();
    std::cout << "PASS: production attachment, native/GPU toggles, read-only Recycle Bin scope/enumeration/Details/List/grid/PIDL menu, namespace-to-filesystem navigation and minimized dispose/reattach/restore\n";
}
