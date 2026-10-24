// Synthetic providers/windows only; no commands are launched or desktop input sent.
static void TestLlmReport() {
    {
        PROPVARIANT value{};
        struct Reset {~Reset(){PropertyFixture::sortVariant=nullptr;}} reset;
        for(VARTYPE type:{VT_R4,VT_R8,VT_DATE}) for(double number:{
                std::numeric_limits<double>::quiet_NaN(),-std::numeric_limits<double>::infinity(),
                -3.,0.,7.,std::numeric_limits<double>::infinity()}) {
            value.vt=type;
            if(type==VT_R4) value.fltVal=static_cast<float>(number);
            else if(type==VT_DATE) value.date=number;
            else value.dblVal=number;
            PropertyFixture::sortVariant=&value;
            Entry entry; entry.path=L"wh-property-test:nan";
            ReadExtraShellSortValue(entry,PKEY_Rating);
            Check(entry.sortValueKind==(std::isnan(number) ? 0 : 2),"Shell numeric NaN remained a sortable number");
        }
        std::vector<Entry> entries(6);
        for(size_t i=0;i<entries.size();++i) {
            entries[i].name=std::to_wstring(i); entries[i].sortValueKind=i==0 ? 0 : 2;
            entries[i].sortNumber=i==0 ? std::numeric_limits<double>::quiet_NaN() : double(i);
        }
        SortSpec sort; sort.kind=SortKind::ShellProperty; sort.placement=FolderPlacement::Mixed;
        for(auto direction:{SORT_ASCENDING,SORT_DESCENDING}) {
            sort.direction=direction;
            for(const auto& a:entries) for(const auto& b:entries) for(const auto& c:entries) {
                Check(!EntryPrecedes(a,a,sort),"sort is reflexive");
                Check(!(EntryPrecedes(a,b,sort)&&EntryPrecedes(b,a,sort)),"sort is asymmetric");
                if(EntryPrecedes(a,b,sort)&&EntryPrecedes(b,c,sort))
                    Check(EntryPrecedes(a,c,sort),"numeric/missing sort is not transitive");
            }
            auto ordered=entries;
            std::sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b){return EntryPrecedes(a,b,sort);});
            Check(!ordered.back().sortValueKind,"missing numeric values did not sort last");
        }
    }
    WNDCLASSW type{}; type.hInstance=GetModuleHandleW(nullptr); type.lpfnWndProc=DefWindowProcW;
    type.lpszClassName=L"CabinetWClass"; RegisterClassW(&type);
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,type.lpszClassName,L"private report frame",
        WS_POPUP|WS_VISIBLE,-32000,-32000,320,240,nullptr,nullptr,type.hInstance,nullptr);
    Check(frame!=nullptr,"report frame fixture");
    {
        struct Restore {
            decltype(g_dispatchOriginal) dispatch=g_dispatchOriginal;
            decltype(g_translateAcceleratorOriginal) accelerator=g_translateAcceleratorOriginal;
            BYTE keys[256]{};
            Restore(){Check(GetKeyboardState(keys),"report key fixture");}
            ~Restore(){SetKeyboardState(keys); g_dispatchOriginal=dispatch; g_translateAcceleratorOriginal=accelerator;
                ReportApiFixture::countShellWindows=false; ReportApiFixture::failEvents=false;}
        } restore;
        BYTE keys[256]{}; Check(SetKeyboardState(keys),"report modifiers");
        g_dispatchOriginal=+[](const MSG*)->LRESULT{return 0;};
        ReportApiFixture::countShellWindows=true; ReportApiFixture::shellWindowsCalls=0;
        auto* dispatch=CurrentDispatchThreadState(); Check(dispatch!=nullptr,"report dispatch state");
        dispatch->frames[frame].lastSelectedTab=ActiveExplorerTab(frame);
        SetPropW(frame,kColdAttachResolvedProperty,reinterpret_cast<HANDLE>(1));
        MSG key{}; key.hwnd=frame; key.message=WM_KEYDOWN; key.wParam=VK_F9;
        for(int i=0;i<12;++i) {dispatch->frames[frame].nextScan=0; DispatchHook(&key);}
        Check(!ReportApiFixture::shellWindowsCalls,"resolved unsupported tab repeated ShellWindows discovery");
        RemovePropW(frame,kColdAttachResolvedProperty);
        dispatch->frames[frame].nextScan=0; DispatchHook(&key);
        Check(ReportApiFixture::shellWindowsCalls==1,"navigation did not reopen discovery admission");
        type.lpszClassName=L"SHELLDLL_DefView"; RegisterClassW(&type);
        HWND unsupported=CreateWindowExW(0,type.lpszClassName,L"unsupported native view",WS_CHILD|WS_VISIBLE,
            0,0,100,100,frame,nullptr,type.hInstance,nullptr);
        SetPropW(frame,kColdAttachResolvedProperty,reinterpret_cast<HANDLE>(1));
        key.hwnd=unsupported; key.message=WM_SIZE; DispatchHook(&key);
        const auto discovered=ReportApiFixture::shellWindowsCalls;
        Sleep(300); // Exceed the old same-HWND 250 ms recovery threshold.
        for(int i=0;i<8;++i) {dispatch->frames[frame].nextScan=0; DispatchHook(&key);}
        Check(ReportApiFixture::shellWindowsCalls==discovered,"same unsupported native HWND restarted periodic discovery on resize");
        RemovePropW(frame,kColdAttachResolvedProperty);
        DestroyWindow(unsupported);
        ReportApiFixture::countShellWindows=false;

        HWND unrelated=CreateWindowExW(0,L"STATIC",L"unrelated data",WS_CHILD,0,0,1,1,frame,nullptr,type.hInstance,nullptr);
        HWND dataWindow=CreateWindowExW(0,L"STATIC",L"poisoned data",WS_CHILD,0,0,1,1,frame,nullptr,type.hInstance,nullptr);
        SetWindowLongPtrW(unrelated,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dataWindow));
        SetWindowLongPtrW(dataWindow,GWLP_USERDATA,1);
        g_translateAcceleratorOriginal=+[](HWND,HACCEL,LPMSG)->int{return 123;};
        key.hwnd=unrelated; key.message=WM_KEYDOWN; key.wParam=VK_ESCAPE;
        Check(TranslateAcceleratorHook(frame,nullptr,&key)==123,"unrelated USERDATA was interpreted as mod state");
        ViewState editorOwner; HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,80,24,frame,nullptr,type.hInstance,nullptr);
        editorOwner.hwnd=CreateWindowExW(0,L"STATIC",L"registered editor owner",WS_CHILD|WS_VISIBLE,0,0,10,10,frame,nullptr,type.hInstance,nullptr);
        editorOwner.frameHwnd=editorOwner.activeTabHost=frame;
        SetWindowLongPtrW(editorOwner.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&editorOwner));
        RegisterTabOverlay(frame,frame,editorOwner.hwnd);
        editorOwner.rename.editor=edit;
        Check(SetWindowSubclass(edit,RenameEditProc,reinterpret_cast<UINT_PTR>(RenameEditProc),
            reinterpret_cast<DWORD_PTR>(&editorOwner)),"report rename subclass fixture");
        SetWindowLongPtrW(edit,GWLP_USERDATA,1); key.hwnd=edit;
        Check(TranslateAcceleratorHook(frame,nullptr,&key)==0,"rename editor did not bypass Explorer accelerators through subclass identity");
        RemoveWindowSubclass(edit,RenameEditProc,reinterpret_cast<UINT_PTR>(RenameEditProc));
        editorOwner.rename.editor=nullptr; DestroyWindow(edit);
        UnregisterTabOverlay(frame,frame,editorOwner.hwnd); DestroyWindow(editorOwner.hwnd);

        ViewState first,second; first.frameHwnd=second.frameHwnd=frame;
        ReportApiFixture::failEvents=true; InitializeFrameShellEvents(first);
        Check(first.frameLifecycleSubscribed && !first.viewEvents,"partial frame subscription fixture");
        ReportApiFixture::failEvents=false; InitializeFrameShellEvents(second);
        Check(second.viewEvents && second.frameLifecycleSubscribed,"frame hook retry fixture");
        ReleaseFrameShellViewHook(first); ReleaseFrameShellViewHook(second);
        Check(!g_shellViewHooks.contains(frame) && static_cast<ULONG_PTR>(GetWindowLongPtrW(frame,GWLP_WNDPROC))==
            GetClassLongPtrW(frame,GCLP_WNDPROC),"partial hook upgrade leaked owner/subclass");
        InitializeFrameShellEvents(first);
        DestroyWindow(frame);
        Check(!g_shellViewHooks.contains(frame),"frame destruction retained hooks");
        ReleaseFrameShellViewHook(first);
    }
    StopTabVisibilityObserver(); g_tabWatchStopping=false;
    std::cout<<"PASS: NaN Shell variants and strict ordering; resolved unsupported-tab scan suppression/retry; unrelated USERDATA and rename accelerators; partial frame-hook upgrade/removal and frame destruction\n";
}
