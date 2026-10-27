// Private files/native snapshots; commands and focus remain substituted.
namespace SelectionUiFixture {
inline HWND held=nullptr;
inline HANDLE entered=nullptr,released=nullptr,exited=nullptr;
inline void HoldWorker(LPTHREAD_START_ROUTINE procedure,LPVOID context) {
    if(procedure!=TabWorkerEntry || !context) return;
    auto& worker=*static_cast<TabBackground::Worker*>(context);
    if(worker.lane==TabBackground::Lane::Selection && worker.owner->hwnd==held) {
        SetEvent(entered); WaitForSingleObject(released,INFINITE); SetEvent(exited);
    }
}
struct Hold {
    AuditFixture::ThreadProbe previous;
    explicit Hold(HWND target) {
        held=target; entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        released=CreateEventW(nullptr,TRUE,FALSE,nullptr); exited=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        previous=AuditFixture::threadProbe.exchange(HoldWorker);
    }
    void Release() {SetEvent(released);}
    ~Hold() {
        AuditFixture::threadProbe=previous; SetEvent(released);
        if(WaitForSingleObject(entered,0)!=WAIT_OBJECT_0 || WaitForSingleObject(exited,5000)==WAIT_OBJECT_0) {
            CloseHandle(entered); CloseHandle(released); CloseHandle(exited);
        }
        held=nullptr;
    }
};
inline SelectionFolderView* Setup(PresentationFixture::Tab& tab,const std::wstring& folder) {
    auto& state=tab.state;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    state.folderPath=folder; state.selectedFlags.Fill(0); state.selected=state.anchor=-1;
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&state.folder))),"selection UI private folder");
    PIDLIST_ABSOLUTE parent=nullptr;
    Check(SUCCEEDED(SHGetIDListFromObject(state.folder.Get(),&parent)),"selection UI parent identity");
    state.directoryFolderIdentity=SharedPidl(parent,TaskMemDeleter{});
    auto* view=new SelectionFolderView;
    view->state=&state; view->folder=state.folder; view->reenterCapture=false;
    state.folderView.Attach(view);
    for(int i=0;i<state.total;++i) {
        state.entries[size_t(i)].name=L"file"+std::to_wstring(i)+L".txt";
        state.entries[size_t(i)].path=JoinPath(folder,state.entries[size_t(i)].name);
        state.entries[size_t(i)].folder=false;
    }
    state.background->pipeline=std::make_unique<PipelineTrace>();
    state.background->pipelinePublished=state.background->pipeline.get();
    return view;
}
inline void SelectNative(ViewState& state,SelectionFolderView& view,const std::vector<int>& rows) {
    view.selected.clear(); view.focused.reset();
    for(int row:rows) {
        PITEMID_CHILD child=nullptr;
        Check(SUCCEEDED(EntryChildPidl(state,state.entries[size_t(row)],&child)) && child,"selection UI private child");
        view.selected.emplace_back(child);
    }
    if(!view.selected.empty()) view.focused.reset(ILClone(view.selected.back().get()));
}
inline void Settle(ViewState& state) {
    StopRowTransition(state); FinishColumnTransition(state); StopHoverAnimation(state);
    StopScrollAnimation(state); StopKeyboardSelectionAnimation(state); StopSelectionFade(state);
    state.lastInteractionTick=0;
}
}

static void TestSelectionUiModeReads() {
    class CountedView final : public AttachmentFolderView {
    public:
        unsigned modes=0,groups=0;
        HRESULT STDMETHODCALLTYPE GetCurrentViewMode(UINT* value) override {
            ++modes; return AttachmentFolderView::GetCurrentViewMode(value);
        }
        HRESULT STDMETHODCALLTYPE GetGroupBy(PROPERTYKEY* key,BOOL* ascending) override {
            ++groups; return AttachmentFolderView::GetGroupBy(key,ascending);
        }
    };
    PresentationFixture::Tab tab(L"mode read reuse",1);
    auto& state=tab.state;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    auto* view=new CountedView; state.folderView.Attach(view); view->mode=FVM_LIST;
    ApplyViewLayout(state,{FVM_LIST,48},false); state.cachedNativeMode=FVM_LIST;
    FolderViewMode before{FVM_LIST,48};
    const auto probe=[&] {
        PostShellVisibilityProbe(state,true,true,&before);
        MSG message{};
        Check(PeekMessageW(&message,state.hwnd,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE),
            "mode command did not queue its observation");
        OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
    };
    probe();
    std::cout<<"MODE READS unchanged-command="<<view->modes<<" grouping="<<view->groups<<std::endl;
    Check(view->modes==1 && view->groups==1,"unchanged command repeated a successful post-command mode read");
    view->modes=view->groups=0;
    state.directoryModePinned=true; view->mode=before.mode=FVM_DETAILS;
    probe();
    Check(view->modes==1 && state.cachedNativeMode==FVM_LIST && state.layoutNativeMode==FVM_LIST,
        "successful observation adopted an unchanged ignored provider mode");
    state.directoryModePinned=false; view->mode=before.mode=FVM_LIST;
    view->modes=view->groups=0;
    view->modeProbe=[&] {++state.token; view->mode=FVM_ICON;};
    probe();
    Check(view->modes==1 && !view->groups && state.cachedNativeMode==FVM_LIST && state.layoutNativeMode==FVM_LIST,
        "reentrant mode read updated the replacement view or continued the old command");
    std::cout<<"PASS: unchanged commands reuse one mode read; reentrant view generations reject the sampled mode and old command\n";
}

static void TestSelectionUiWork(const std::wstring& fixture) {
    const bool tracing=g_pipelineTracing.exchange(true);
    struct Restore {bool trace; ~Restore() {g_pipelineTracing=trace;}} restore{tracing};
    PresentationFixture::Tab first(L"held selection",64),second(L"independent selection",64);
    auto& a=first.state; auto& b=second.state;
    auto* native=SelectionUiFixture::Setup(first,fixture);
    auto* peer=SelectionUiFixture::Setup(second,fixture);
    FocusFixture::Session focus(first.frame,a.hwnd);
    ModifierFixture::Session modifiers;
    ComPtr<SelectionCommandMenu> menu; menu.Attach(new SelectionCommandMenu);
    ComPtr<SelectionCommandView> commands; commands.Attach(new SelectionCommandView(menu.Get()));
    commands->window=a.nativeHwnd; native->commandView=commands;
    a.selected=a.anchor=3; a.selectedFlags[3]=1;
    SelectionUiFixture::SelectNative(a,*native,{5});
    SelectionUiFixture::Hold held(a.hwnd);
    Check(CaptureNativeSelection(a,true) && a.pendingNativeSelection && a.nativeSelectionUnsafe &&
        a.selected==3 && !native->itemArrayReads,"small selection resolved paths synchronously or discarded displayed selection");
    Check(Eventually([&]{return WaitForSingleObject(SelectionUiFixture::entered,0)==WAIT_OBJECT_0;}),
        "held selection worker did not start");
    MSG snapshot{};
    Check(PeekMessageW(&snapshot,a.hwnd,kMsgPreparedSelection,kMsgPreparedSelection,PM_REMOVE),"small raw snapshot was not queued");
    OverlayProc(a.hwnd,snapshot.message,snapshot.wParam,snapshot.lParam);
    Check(!a.nativeSelectionCaptureJob && a.pendingNativeSelection,"raw snapshot did not reach the held lane");

    SelectionUiFixture::SelectNative(b,*peer,{0,1});
    const auto before=peer->selectionCountReads;
    ProbeNativeSelectionOnIdle(b,GetTickCount64());
    Check(peer->selectionCountReads==before+1 && b.pendingNativeSelection,
        "idle observation read its initial selection count twice or failed to queue");
    WaitForPreparedSelection(b);
    Check(b.selected==1 && b.selectedFlags[0] && b.selectedFlags[1] && !b.nativeSelectionUnsafe &&
        !peer->itemArrayReads,"held tab blocked its peer or peer read Shell item paths on UI");

    MSG key{}; key.hwnd=a.hwnd; key.message=WM_KEYDOWN; key.wParam=VK_DELETE; key.lParam=1;
    Check(HandleNativeFileKey(key,&a) && a.deferredSelectionKeys.size()==1 && menu->calls.empty(),
        "file command bypassed the pending small selection");
    SelectionUiFixture::SelectNative(a,*native,{7}); // Silent replacement, still one item.
    held.Release(); WaitForPreparedSelection(a);
    Check(a.selected==3 && a.nativeSelectionDirty && a.nativeSelectionUnsafe && menu->calls.empty(),
        "equal-count replacement accepted stale worker paths or invoked a stale file command");
    Check(CaptureNativeSelection(a,true),"small selection retry did not queue"); WaitForPreparedSelection(a);
    Check(a.selected==7 && a.selectedFlags[7] && !a.nativeSelectionUnsafe,"replacement selection did not recover");
    SelectionUiFixture::Settle(a);
    Check(CaptureNativeSelection(a,true) && a.pendingNativeSelection,"pending observation before Up did not queue");
    OverlayProc(a.hwnd,WM_KEYDOWN,VK_UP,0);
    Check(a.selected==6 && a.selectedFlags[6] && native->selected.size()==1 && !a.pendingNativeSelection &&
        !a.nativeSelectionUnsafe,"Up did not supersede a pending small-selection observation immediately");
    SelectionUiFixture::Settle(a);

    for(int count:{1,2,64}) {
        std::vector<int> rows; for(int i=0;i<count;++i) rows.push_back(i);
        SelectionUiFixture::SelectNative(b,*peer,rows);
        Check(CaptureNativeSelection(b,true),"small selection capture failed"); WaitForPreparedSelection(b);
        Check(b.selected==count-1 && std::count(b.selectedFlags.begin(),b.selectedFlags.end(),1)==count &&
            !b.nativeSelectionUnsafe,"small worker import changed selection or focus");
        SelectionUiFixture::Settle(b); ValidateRect(b.hwnd,nullptr);
        b.nativeSelectionLastProbe=0;
        const auto reads=peer->selectionCountReads;
        ProbeNativeSelectionOnIdle(b,GetTickCount64()); WaitForPreparedSelection(b);
        Check(peer->selectionCountReads==reads+3 && !GetUpdateRect(b.hwnd,nullptr,FALSE) &&
            !b.nativeSelectionUnsafe,"unchanged idle selection repainted or lost its capture/completion fences");
    }
    SelectionUiFixture::SelectNative(b,*peer,{2});
    Check(CaptureNativeSelection(b,true),"stale navigation capture failed");
    ++b.token; CancelNativeSelectionPreparation(b);
    SelectionUiFixture::SelectNative(b,*peer,{4});
    Check(CaptureNativeSelection(b,true),"new navigation capture failed"); WaitForPreparedSelection(b);
    Check(b.selected==4 && b.selectedFlags[4] && !b.nativeSelectionUnsafe,"old navigation capture overwrote the new view");
    SelectionUiFixture::Settle(b);
    SelectionUiFixture::SelectNative(b,*peer,{});
    const auto arrays=peer->itemArrayReads,enumerations=peer->selectionEnumerations;
    Check(CaptureNativeSelection(b,true) && !b.pendingNativeSelection && b.selected==-1 &&
        peer->itemArrayReads==arrays && peer->selectionEnumerations==enumerations,"empty selection queued provider work");
    b.nativeSelectionLastProbe=0;
    const auto emptyReads=peer->selectionCountReads;
    ProbeNativeSelectionOnIdle(b,GetTickCount64());
    Check(peer->selectionCountReads==emptyReads+1 && !b.pendingNativeSelection,"empty idle probe did redundant work");

    unsigned resolved=0,verified=0,unchanged=0;
    std::array<PipelineRecord,128> batch;
    for(auto* state:{&a,&b}) {
        auto& trace=*state->background->pipeline;
        for(size_t n;(n=trace.Drain(batch.data(),batch.size()))!=0;) for(size_t i=0;i<n;++i) {
            const auto& event=batch[i];
            if(event.operation && !wcscmp(event.operation,L"selection.resolve") && event.phase==L'E') {
                Check(event.thread!=GetCurrentThreadId(),"selection name resolution ran on the UI thread"); ++resolved;
            }
            if(event.operation && !wcscmp(event.operation,L"shell.selection.identities.verify") && event.phase==L'E') ++verified;
            if(event.operation && !wcscmp(event.operation,L"selection.import.unchanged")) ++unchanged;
        }
    }
    Check(resolved>=8 && verified>=8 && unchanged==3 && !native->itemArrayReads && !peer->itemArrayReads,
        "missing worker/query diagnostics or a small selection requested live Shell item arrays");
    std::cout<<"PASS: small 1/2/64-item selection resolves on independent tab workers; one initial count plus snapshot/completion fences; unchanged idle avoids repaint; silent equal-count replacement, deferred Delete, empty and navigation cancellation\n";
    TestSelectionUiModeReads();
}
