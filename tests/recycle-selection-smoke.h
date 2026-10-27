// Produce two legitimate, binary-different PIDLs for a private file through
// Shell's documented bind-data API. No real Recycle Bin item is modified.
class SelectionBindData final : public IFileSystemBindData {
    ULONG refs=1;
public:
    WIN32_FIND_DATAW data{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IFileSystemBindData) return E_NOINTERFACE;
        *out=static_cast<IFileSystemBindData*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetFindData(const WIN32_FIND_DATAW* value) override { data=*value; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFindData(WIN32_FIND_DATAW* value) override { *value=data; return S_OK; }
};

static void TestRecycleSelectionZoom(ViewState& state,SelectionFolderView& native, bool delegated = false) {
    const auto entries=state.entries;
    const auto index=state.entryPathIndex;
    const auto originalFolder=state.folderPath;
    const auto originalParent=state.directoryFolderIdentity;
    const auto originalIdentities=state.namespaceIdentities;
    const auto originalMode=state.cachedNativeMode;
    const int originalIconSize=state.nativeIconSize;
    const auto originalHeights=state.rowHeight96;
    const bool smooth=g_smooth.exchange(false);
    PIDLIST_ABSOLUTE rawParent=nullptr;
    Check(SUCCEEDED(SHGetIDListFromObject(state.folder.Get(),&rawParent)) && rawParent,"namespace selection parent");
    state.directoryFolderIdentity=SharedPidl(rawParent,TaskMemDeleter{});
    state.folderPath=delegated ? originalFolder : kRecycleBinLocation;
    state.gridView=true; state.cachedNativeMode=FVM_ICON;
    state.nativeIconSize=256;
    auto identities=std::make_shared<NamespaceIdentities>();
    const auto alternate=[&](int row,DWORD size) {
        ComPtr<SelectionBindData> data; data.Attach(new SelectionBindData);
        HANDLE find=FindFirstFileW(entries[row].path.c_str(),&data->data);
        Check(find!=INVALID_HANDLE_VALUE,"private selected file metadata"); FindClose(find);
        data->data.nFileSizeLow=size; data->data.nFileSizeHigh=1;
        ComPtr<IBindCtx> context;
        Check(SUCCEEDED(CreateBindCtx(0,&context)) &&
            SUCCEEDED(context->RegisterObjectParam(const_cast<LPWSTR>(STR_FILE_SYS_BIND_DATA),data.Get())),
            "selected PIDL bind context");
        PITEMID_CHILD child=nullptr; std::wstring name=entries[row].name;
        Check(SUCCEEDED(state.folder->ParseDisplayName(nullptr,context.Get(),name.data(),nullptr,&child,nullptr)) &&
            child,"alternate selected PIDL");
        return OwnedPidl(child);
    };
    for(int row:{7,70,128}) {
        PITEMID_CHILD raw=nullptr;
        Check(SUCCEEDED(EntryChildPidl(state,entries[row],&raw)) && raw,"cached namespace child");
        auto& entry=state.entries[row];
        entry.shellChild=SharedPidl(raw,TaskMemDeleter{});
        entry.shellIdentity=SharedPidl(ILCombine(rawParent,raw),TaskMemDeleter{});
        entry.path=delegated && row!=70 ? entries[row].path : NamespaceItemKey(entry.shellIdentity.get());
        entry.name=L"duplicate photo.jpg"; // Display text must never decide identity.
        identities->emplace(entry.path,entry.shellChild);
    }
    state.namespaceIdentities=identities;
    state.entryPathIndex.clear();
    for(size_t i=0;i<state.entries.size();++i)
        state.entryPathIndex.emplace_back(SelectionPathHash(state.entries[i].path),static_cast<int>(i));
    std::sort(state.entryPathIndex.begin(),state.entryPathIndex.end());
    auto first=alternate(7,12345),second=alternate(70,67890);
    for(auto [row,child]:{std::pair{7,first.get()},std::pair{70,second.get()}}) {
        OwnedPidl absolute(ILCombine(rawParent,child));
        const auto canonical=state.folder->CompareIDs(SHCIDS_CANONICALONLY,child,state.entries[row].shellChild.get());
        Check(!PidlBytesEqual(child,state.entries[row].shellChild.get()),"alternate PIDL fixture was byte-identical");
        Check(SUCCEEDED(canonical) && HRESULT_CODE(canonical)==0,"alternate PIDL fixture was not canonically equal");
        Check(GpuRowForNativePath(state,NamespaceItemKey(absolute.get()))<0,"alternate PIDL unexpectedly matched raw index");
    }
    const auto pump=[&] {
        Check(Eventually([&] {
            MSG message{};
            while(PeekMessageW(&message,state.hwnd,kMsgPreparedSelection,kMsgPreparedSelection,PM_REMOVE))
                OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
            return !state.pendingNativeSelection;
        },30000),"namespace selection identity resolution did not finish");
    };
    UpdateMetrics(state);
    for(int sample=0;sample<80;++sample)
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
    RECT viewport{}; ViewClientRect(state,&viewport);
    Check(state.iconPx>1000.f && IconRequestSize(state)>=32 &&
        IconRequestSize(state)<=UINT(std::max(viewport.right,viewport.bottom)) && state.visible,
        "large grid zoom was limited or detached before selection");
    native.selected.emplace_back(std::move(first)); native.selected.emplace_back(std::move(second));
    native.focused=alternate(70,98765); // Focus may use a third binary encoding.
    const auto commits=native.commits,arrays=native.itemArrayReads;
    const auto generation=state.directoryToken;
    const auto columnEpoch=state.columnEpoch,propertyEpoch=state.propertyEpoch;
    for(int echo=0;echo<4;++echo) {
        Check(CaptureNativeSelection(state,true) && state.pendingNativeSelection,
            "small namespace selection bypassed raw-PIDL background capture");
        pump();
        Check(state.visible && !state.nativeSelectionUnsafe && !state.nativeSelectionImportPending &&
            state.selected==70 && state.selectedFlags[7] && state.selectedFlags[70] && !state.selectedFlags[128] &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==2 && !state.directoryPending &&
            state.directoryToken==generation,"large-zoom selection echo detached, refreshed names or selected a duplicate");
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
    }
    Check(native.commits==commits && native.itemArrayReads==arrays && state.columnEpoch==columnEpoch &&
        state.propertyEpoch==propertyEpoch,"namespace import echoed commands or used the UI item-array/provider path");
    if (!delegated) {
        CacheDepartingFolder(state);
        Check(state.entries.empty() && !state.namespaceIdentities &&
            RestoreCachedFolder(state) && state.namespaceIdentities==identities,
            "cached namespace revisit lost its canonical selection snapshot");
        Check(CaptureNativeSelection(state,true),"cached namespace selection setup"); pump();
        Check(state.visible && state.selectedFlags[7] && state.selectedFlags[70],"cached revisit detached after selection import");
    }
    // A completion computed against an obsolete row snapshot retries rather
    // than applying keys to a later publication or revealing native Explorer.
    Check(CaptureNativeSelection(state,true),"stale namespace snapshot setup");
    state.namespaceIdentities=std::make_shared<NamespaceIdentities>(*identities);
    pump();
    Check(state.visible && state.nativeSelectionDirty && state.selectedFlags[7] && state.selectedFlags[70],
        "stale namespace completion changed selection or detached");
    Check(CaptureNativeSelection(state,true),"namespace snapshot retry setup"); pump();
    Check(!state.nativeSelectionUnsafe && state.visible,"namespace snapshot retry did not recover");
    auto absent=alternate(129,13579);
    const auto unmatched=NamespaceSelectionKey(state.folder.Get(),rawParent,absent.get(),identities.get(),[] { return true; });
    Check(!identities->contains(unmatched),"unmatched child was guessed from a duplicate display name");
    Check(CaptureNativeSelection(state,true),"namespace cancellation setup");
    CancelNativeSelectionPreparation(state); pump();
    Check(!state.pendingNativeSelection && state.visible,"cancelled namespace capture revived");
    native.selected.clear(); native.focused.reset();
    StopHoverAnimation(state); StopSelectionFade(state); StopKeyboardSelectionAnimation(state);
    StopRowTransition(state); StopScrollAnimation(state); CancelAutoRefit(state);
    state.entries=entries; state.entryPathIndex=index; state.folderPath=originalFolder;
    state.directoryFolderIdentity=originalParent; state.namespaceIdentities=originalIdentities;
    state.gridView=false; state.cachedNativeMode=originalMode; state.nativeIconSize=originalIconSize;
    state.rowHeight96=originalHeights; state.wheelRemainder=0;
    state.nativeSelectionDirty=state.nativeSelectionMessageQueued=state.nativeSelectionImportPending=false;
    state.nativeSelectionAwaitingRefresh=state.nativeSelectionUnsafe=false;
    state.nativeSelectionObservedCount=-1; state.nativeSelectionObservedPaths.clear();
    state.selectedFlags.assign(state.total,0); state.selected=state.anchor=-1;
    UpdateMetrics(state); state.scrollNow=state.scrollTarget=0; g_smooth=smooth;
    std::cout<<"PASS: "<<(delegated ? "filesystem-addressed provider with mixed path/opaque keys" : "Recycle Bin")
        <<" grid zoom, binary-different canonical selection/focus PIDLs, duplicate labels, small echoes, background import, stale snapshots and cancellation; private fixtures only\n";
}
