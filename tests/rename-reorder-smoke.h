// Model Shell's view accepting the new PIDL later than the filesystem rename.
// All windows/files are private; focus and modifiers are substituted.
static void TestRenameReorderReadiness(const std::wstring& directory) {
    auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"rename reorder fonts");
    for(bool rtl:{false,true}) for(bool grid:{false,true}) for(bool changeName:{false,true}) {
        PresentationFixture::Tab tab(L"rename reorder readiness",12,rtl,grid);
        auto& state=tab.state;
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame,state.hwnd);
        PointerFixture::Session pointer; PointerFixture::held=false;
        ModifierFixture::Session modifiers;
        const auto path=std::filesystem::path(directory)/(L"rename-reorder-"+std::to_wstring(rtl)+
            std::to_wstring(grid)+std::to_wstring(changeName));
        std::filesystem::create_directories(path);
        const wchar_t* names[]={L"a",L"b",L"c",L"d",L"New Folder",L"r",L"s",L"t",L"u",L"v",L"w",L"zebra"};
        for(int i=0;i<12;++i) {
            auto& entry=state.entries[size_t(i)]; entry.name=names[i]; entry.path=(path/entry.name).wstring();
            entry.folder=true; std::filesystem::create_directories(entry.path);
            state.entryPathIndex.emplace_back(SelectionPathHash(entry.path),i);
        }
        std::sort(state.entryPathIndex.begin(),state.entryPathIndex.end());
        state.folderPath=path.wstring(); state.selected=state.anchor=4;
        state.selectedFlags.assign(12,0); state.selectedFlags[4]=1;
        HWND native=CreateWindowExW(0,L"STATIC",L"delayed native rename view",WS_CHILD,0,0,800,480,
            tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        state.nativeHwnd=native;
        auto* folder=new RenameHandoffFixture::Folder;
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&folder->inner))),"reorder private folder");
        state.folder.Attach(folder);
        auto* view=new SelectionFolderView; view->folder=state.folder; view->state=&state; view->reenterCapture=false;
        state.folderView.Attach(view);
        ComPtr<SelectionCommandMenu> menu; menu.Attach(new SelectionCommandMenu);
        ComPtr<SelectionCommandView> shell; shell.Attach(new SelectionCommandView(menu.Get())); shell->window=native;
        view->commandView=shell;
        auto* browser=new TimingShellBrowser; browser->window=tab.frame; browser->view=shell;
        state.shellBrowser.Attach(browser);
        Check(SyncNativeSelection(state),"rename readiness initial selection");
        std::vector<std::wstring> trace;
        SelectionLogFixture::Session traceCapture(trace);
        const bool loggingBefore=g_selectionLogging.exchange(changeName);
        BeginRename(state,4,true); Check(state.rename.editor!=nullptr,"rename readiness field");
        folder->probe=[&] {view->selected.clear(); view->focused.reset(); view->rejectSelection=true;};
        if(changeName) SetWindowTextW(state.rename.editor,L"zyx");
        FinishRename(state,true);
        Check(state.selected==4 && state.selectedFlags[4],"rename immediately lost GPU identity");
        auto rows=state.entries;
        std::sort(rows.begin(),rows.end(),[](const Entry& a,const Entry& b) {return _wcsicmp(a.name.c_str(),b.name.c_str())<0;});
        DirectoryResult result; result.kind=DirectoryResult::Complete; result.token=state.directoryToken;
        result.entries=std::move(rows); result.oldTotal=state.total; result.oldRowPx=state.rowPx; result.oldScroll=state.scrollNow;
        for(size_t i=0;i<result.entries.size();++i) result.pathIndex.emplace_back(SelectionPathHash(result.entries[i].path),int(i));
        std::sort(result.pathIndex.begin(),result.pathIndex.end()); result.orderHash=DirectoryOrderHash(result.entries);
        result.metadataHash=DirectoryMetadataHash(result.entries);
        PublishDirectoryResult(*state.background,std::move(result)); ReceiveDirectoryResults(state);
        StopRowTransition(state); StopScrollAnimation(state); StopSelectionFade(state);
        state.directoryPending=state.refreshAgain=state.watchedChangePending=false;
        const int expected=changeName ? 11 : 4;
        Check(state.selected==expected && state.selectedFlags[size_t(expected)],"names refresh did not reconcile renamed selection by path");
        const auto mismatch=CaptureSelectionIdentitySample(state);
        Check(mismatch.contextCurrent && mismatch.gpuRow==expected && mismatch.gpuSelected &&
            mismatch.nativeCount==(changeName ? 0 : 1) && mismatch.recovery==changeName &&
            (!changeName || mismatch.gpuPath==(path/L"zyx").wstring()),
            "focused diagnostics did not distinguish current GPU identity from delayed native selection");
        const auto sequence=state.selectionTraceSequence;
        const bool logging=g_selectionLogging.exchange(false);
        TraceSelectionIdentity(state,L"fixture.disabled");
        Check(state.selectionTraceSequence==sequence,"disabled selection diagnostics performed work");
        g_selectionLogging=true; TraceSelectionIdentity(state,L"fixture.mismatch");
        Check(state.selectionTraceSequence==sequence+1 && !state.selectionTraceRunning,"selection diagnostics lost sequence/reentrancy guard");
        g_selectionLogging=logging;
        CaptureNativeSelection(state,true);
        MaybeRecoverNativeSelection(state);
        Check(state.selected==expected && state.selectedFlags[size_t(expected)],"delayed native rename readiness erased reconciled GPU selection");
        // Down at the renamed last item is a boundary action, not a new
        // selection intent that can discard the outstanding native identity.
        const int target=KeyboardNavigationTarget(state,VK_DOWN,true);
        OverlayProc(state.hwnd,WM_KEYDOWN,VK_DOWN,0);
        Check(target>=0 && state.selected==target && state.selectedFlags[size_t(target)] &&
            (!changeName || target==11),"Down after zyx selected the first item or lost the boundary selection");
        if(changeName) {
            Check(state.lastFolderNativePendingPath==(path/L"zyx").wstring(),
                "rename retry did not retain the canonical identity");
            // A display label (localized or with a hidden extension) need not
            // equal the parsing leaf. Protection and retries use the path.
            state.lastFolderNativePending=L"different display label";
            Check(NativeSelectionRestorePending(state),"display label mismatch cancelled pending path ownership");
        }
        view->rejectSelection=false;
        state.lastFolderNativeNextAttempt=0; TrySyncLastFolderSelection(state);
        CaptureNativeSelection(state,true);
        Check(state.lastFolderNativePending.empty() && state.lastFolderNativePendingPath.empty() &&
            !state.nativeSelectionRecoveryPending &&
            state.selected==target && state.selectedFlags[size_t(target)] && view->selected.size()==1,
            "late native readiness did not retain/verify the same selected identity");
        const auto matched=CaptureSelectionIdentitySample(state);
        Check(matched.contextCurrent && matched.nativeCount==1 && matched.nativeFocusPath==matched.gpuPath &&
            matched.nativeSelected.find(matched.gpuPath)!=std::wstring::npos,
            "focused diagnostics did not report matching current GPU/native identities after verification");
        if(changeName) {
            const auto contains=[&](const wchar_t* point,const wchar_t* stateText) {
                return std::any_of(trace.begin(),trace.end(),[&](const std::wstring& line) {
                    return line.find(point)!=std::wstring::npos && line.find(stateText)!=std::wstring::npos;
                });
            };
            Check(contains(L"point=names.mapped",L"gpu-row=11 gpu-selected=1") &&
                contains(L"point=recovery.defer-identity",L"native-count=0") &&
                contains(L"point=key.after-select",L"gpu-row=11 gpu-selected=1") &&
                contains(L"point=native.restore.verified",L"native-count=1"),
                "timestamped selection trace did not expose the mismatch, reconciliation and verification timepoints");
            if(!rtl && !grid) {
                std::wcout<<L"CONTROLLED_SELECTION_TRACE_BEGIN (private fixtures; not a live Explorer capture)\n";
                for(const auto& line:trace) std::wcout<<line<<L'\n';
                std::wcout<<L"CONTROLLED_SELECTION_TRACE_END\n";
            }
        }
        g_selectionLogging=loggingBefore;
    }
    g_writeFactory=factory;
    std::cout<<"PASS: New Folder unchanged versus zyx-to-last rename, delayed native PIDL acceptance, names reconciliation, recovery, boundary Down and late native verification in LTR/RTL list/grid\n";
}
