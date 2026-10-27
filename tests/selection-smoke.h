// Real filesystem PIDLs, simulated native selection ownership; no file commands.
class SelectionEnumerator final : public IEnumIDList {
    ULONG refs = 1;
    size_t cursor = 0;
    std::vector<OwnedPidl> items;
public:
    explicit SelectionEnumerator(const std::vector<OwnedPidl>& source) {
        for (const auto& item : source) items.emplace_back(ILClone(item.get()));
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IEnumIDList) return E_NOINTERFACE;
        *out = static_cast<IEnumIDList*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, PITEMID_CHILD* out, ULONG* fetched) override {
        ULONG n = 0;
        while (n < count && cursor < items.size()) out[n++] = ILClone(items[cursor++].get());
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override { cursor = std::min(cursor + count, items.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset() override { cursor = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumIDList**) override { return E_NOTIMPL; }
};

class SelectionFolderView final : public AttachmentFolderView {
public:
    ComPtr<IShellView> commandView;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(iid==IID_IShellView && commandView) return commandView->QueryInterface(iid,out);
        return AttachmentFolderView::QueryInterface(iid,out);
    }
    ViewState* state = nullptr;
    std::vector<OwnedPidl> selected;
    OwnedPidl focused;
    unsigned commits = 0, selectionEnumerations = 0, itemArrayReads = 0;
    int allCountOverride=-1;
    bool failSelectionCount=false;
    bool collapseBatches = false, collapseDeselectBatch = false, reenterCapture = true, rejectSelection = false;
    std::function<void()> duringClear;
    HRESULT STDMETHODCALLTYPE ItemCount(UINT flags, int* count) override {
        if(flags==SVGIO_SELECTION && failSelectionCount) return E_PENDING;
        *count = flags == SVGIO_SELECTION ? static_cast<int>(selected.size()) :
            allCountOverride>=0 ? allCountOverride : state->total;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SelectAndPositionItems(UINT count, PCUITEMID_CHILD_ARRAY items,
                                                       POINT*, DWORD flags) override {
        ++commits;
        if(!count && duringClear) { auto callback=std::exchange(duringClear,{}); callback(); }
        if (rejectSelection) return E_ACCESSDENIED;
        // Selection transactions may synchronously dispatch selection events.
        // A focus-only Ctrl+arrow does not change the selected item set.
        if (reenterCapture && (flags & (SVSI_SELECT | SVSI_DESELECTOTHERS))) {
            SendMessageW(state->hwnd, WM_CAPTURECHANGED, 0, 0);
            CaptureNativeSelection(*state);
        }
        if (flags & SVSI_DESELECTOTHERS) selected.clear();
        const UINT accepted = count > 1 && (collapseBatches ||
            (collapseDeselectBatch && (flags & SVSI_DESELECTOTHERS))) ? 1 : count;
        for (UINT i = 0; i < accepted; ++i) {
            if ((flags & SVSI_SELECT) && std::none_of(selected.begin(), selected.end(),
                [&](const OwnedPidl& item) { return ILIsEqual(item.get(), items[i]) != FALSE; }))
                selected.emplace_back(ILClone(items[i]));
            if (flags & SVSI_FOCUSED) focused.reset(ILClone(items[i]));
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Items(UINT flags, REFIID iid, void** out) override {
        if (flags != SVGIO_SELECTION) return E_NOTIMPL;
        ++selectionEnumerations;
        ComPtr<IEnumIDList> enumerator;
        enumerator.Attach(new SelectionEnumerator(selected));
        return enumerator->QueryInterface(iid, out);
    }
    HRESULT STDMETHODCALLTYPE GetSelection(BOOL, IShellItemArray** out) override {
        ++itemArrayReads;
        *out = nullptr;
        if (selected.empty()) return S_OK;
        PIDLIST_ABSOLUTE raw = nullptr;
        if (FAILED(SHGetIDListFromObject(folder.Get(), &raw))) return E_FAIL;
        OwnedPidl parent(raw);
        std::vector<PCUITEMID_CHILD> items;
        for (const auto& item : selected) items.push_back(item.get());
        return SHCreateShellItemArray(parent.get(), folder.Get(), static_cast<UINT>(items.size()),
            items.data(), out);
    }
    HRESULT STDMETHODCALLTYPE GetFocusedItem(int* index) override { *index = focused ? 17 : -1; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSelectedItem(int start,int* index) override {
        if (start<0 || size_t(start)>=selected.size()) {*index=-1; return S_FALSE;}
        *index=start; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Item(int index, PITEMID_CHILD* out) override {
        *out = index == 17 && focused ? ILClone(focused.get()) :
            index>=0 && size_t(index)<selected.size() ? ILClone(selected[size_t(index)].get()) : nullptr;
        return *out ? S_OK : E_FAIL;
    }
};

#include "modal-selection-smoke.h"
#include "recycle-selection-smoke.h"
#include "selection-recovery-smoke.h"

static void TestSelectionGestures(ViewState& state, SelectionFolderView& native) {
    // Plain clicks must stay plain even if a nested Shell pump refreshes the
    // thread keyboard snapshot from live desktop modifier state.
    ModifierFixture::Session modifiers;
    BYTE saved[256]{}, plain[256]{};
    Check(GetKeyboardState(saved) && SetKeyboardState(plain), "repeat click keyboard setup");
    const bool smooth = g_smooth.exchange(true);
    const auto beforeSingle=native.commits;
    Select(state, 1);
    Check(native.commits==beforeSingle+1 && native.focused && native.selected.size()==1 &&
        ILIsEqual(native.focused.get(),native.selected[0].get()),
        "single selection required a second native focus transaction");
    Check(state.selectionFadeActive, "repeat click fixture did not start a selection fade");
    QueryPerformanceCounter(&state.selectionFadeStartedQpc);
    state.selectionFadeStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.045);
    const auto started = state.selectionFadeStartedQpc.QuadPart;
    const auto commits = native.commits;
    float strength = SelectionStrength(state, 1);
    auto click = [&](int row) {
        const LPARAM point = MAKELPARAM(100, static_cast<int>((row+.5f)*state.rowPx));
        OverlayProc(state.hwnd, WM_LBUTTONDOWN, 0, point);
        OverlayProc(state.hwnd, WM_LBUTTONUP, 0, point);
    };
    for (int i = 0; i < 6; ++i) {
        click(1);
        Check(state.selectionFadeStartedQpc.QuadPart == started && native.commits == commits &&
              SelectionStrength(state, 1) >= strength,
              "identical click restarted the selection fade or native selection transaction");
        strength = SelectionStrength(state, 1);
    }
    StopSelectionFade(state);
    click(1);
    Check(!state.selectionFadeActive && SelectionStrength(state, 1) == 1.f && native.commits == commits,
          "resting repeated click blinked or resynchronized selection");
    Check(ClearNativeSelection(state), "repeat click native repair fixture");
    click(1);
    Check(native.selected.size() == 1 && !state.selectionFadeActive,
          "unchanged GPU click failed to repair a known empty native selection");
    SelectWithModifiers(state, 2, true, false);
    click(2);
    Check(std::count(state.selectedFlags.begin(), state.selectedFlags.end(), 1) == 1 &&
          state.selectedFlags[2] && native.selected.size() == 1,
          "clicking a member no longer collapsed a multi-selection");
    auto expectSingle = [&](int row) {
        Check(state.selected==row && state.anchor==row && state.selectedFlags[row] &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==1 && native.selected.size()==1,
            "plain arrow did not collapse the bulk at its directional edge");
        PITEMID_CHILD child=nullptr;
        Check(SUCCEEDED(EntryChildPidl(state,state.entries[row],&child)) && child,
            "keyboard selection item identity");
        const bool nativeMatches=ILIsEqual(child,native.selected[0].get()) && ILIsEqual(child,native.focused.get());
        CoTaskMemFree(child);
        Check(nativeMatches,"bulk exit did not reach Explorer selection/focus");
    };
    for(bool reverse : {false,true}) for(UINT key : {VK_UP,VK_DOWN}) {
        Select(state,reverse ? 8 : 3); SelectWithModifiers(state,reverse ? 3 : 8,false,true);
        const int target=key==VK_UP ? 2 : 9;
        NavigateKeyboardSelection(state,key,false,false); expectSingle(target);
        NavigateKeyboardSelection(state,key,false,false); expectSingle(target+(key==VK_UP ? -1 : 1));
        Select(state,2); SelectWithModifiers(state,8,true,false); SelectWithModifiers(state,5,true,false);
        NavigateKeyboardSelection(state,key,false,false); expectSingle(key==VK_UP ? 1 : 9);
    }
    for(UINT key : {VK_UP,VK_HOME}) {
        Select(state,3); SelectWithModifiers(state,0,false,true);
        NavigateKeyboardSelection(state,key,false,false); expectSingle(0);
    }
    for(UINT key : {VK_DOWN,VK_END}) {
        Select(state,state.total-4); SelectWithModifiers(state,state.total-1,false,true);
        NavigateKeyboardSelection(state,key,false,false); expectSingle(state.total-1);
    }
    for(bool control : {false,true}) for(bool shift : {false,true}) {
        if(!control && !shift) continue;
        Select(state,8); SelectWithModifiers(state,3,false,true);
        NavigateKeyboardSelection(state,VK_DOWN,control,shift);
        Check(state.selected==4 && (!shift || state.anchor==8) &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==(control ? 6 : 5),
            "bulk-edge handling changed Ctrl/Shift navigation or its anchor");
    }
    Select(state,3); NavigateKeyboardSelection(state,VK_DOWN,true,false);
    NavigateKeyboardSelection(state,VK_DOWN,false,false); expectSingle(5);
    state.sections.resize(3); state.rowGroup.resize(state.total);
    const int boundaries[]{0,4,10,state.total};
    for(int group=0;group<3;++group) {
        auto& section=state.sections[group]; section.first=boundaries[group]; section.end=boundaries[group+1];
        section.collapsed=group==1; section.fraction=group==1 ? 0.f : 1.f;
        std::fill(state.rowGroup.begin()+section.first,state.rowGroup.begin()+section.end,group);
    }
    LayoutGroupSections(state);
    Select(state,12); SelectWithModifiers(state,10,false,true);
    NavigateKeyboardSelection(state,VK_UP,false,false); expectSingle(3);
    state.sections[0].collapsed=true; state.sections[0].fraction=0; LayoutGroupSections(state);
    Select(state,12); SelectWithModifiers(state,10,false,true);
    NavigateKeyboardSelection(state,VK_UP,false,false); expectSingle(10);
    state.sections.clear(); state.rowGroup.clear(); LayoutGroupSections(state);
    state.gridView=true; state.itemsPerRow=4;
    for(UINT key : {VK_UP,VK_DOWN}) {
        Select(state,14); SelectWithModifiers(state,9,false,true);
        NavigateKeyboardSelection(state,key,false,false); expectSingle(key==VK_UP ? 5 : 18);
    }
    state.gridView=false; state.itemsPerRow=1;
    {
        RECT oldBounds{}; GetWindowRect(state.hwnd,&oldBounds);
        const bool wasVisible=IsWindowVisible(state.hwnd)!=FALSE;
        Check(SetWindowPos(state.hwnd,nullptr,-32000,-32000,500,400,
            SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW),"hover seed offscreen window");
        FocusFixture::Session focus(state.frameHwnd,state.nativeHwnd);
        PointerFixture::Session pointer; PointerFixture::held=false;
        auto fresh=[&] {
            Check(ClearNativeSelection(state),"fresh keyboard selection fixture");
            state.selected=state.anchor=-1; ClearPostDeletionCaret(state);
            std::fill(state.selectedFlags.begin(),state.selectedFlags.end(),0);
            SetScrollImmediate(state,0.f); ClearPointerHover(state,false);
        };
        for(bool grid : {false,true}) {
            state.gridView=grid; LayoutGroupSections(state);
            const int seed=grid ? state.itemsPerRow+1 : 5;
            const POINT point{static_cast<LONG>(ListStartX(state)+
                (grid ? DocumentRowLeft(state,seed)+state.itemWidth*.5f : 100.f)),
                static_cast<LONG>(state.headerPx+DocumentRowTop(state,seed)+state.rowPx*.5f)};
            for(UINT key : {VK_UP,VK_DOWN}) {
                fresh(); PointerFixture::At(state.hwnd,point);
                Check(state.hovered==-1,"fresh directory retained hover before the first key");
                NavigateKeyboardSelection(state,key,false,false); expectSingle(seed);
                NavigateKeyboardSelection(state,key,false,false);
                expectSingle(seed+(key==VK_UP ? -state.itemsPerRow : state.itemsPerRow));
                fresh(); PointerFixture::At(state.hwnd,POINT{-20,-20});
                NavigateKeyboardSelection(state,key,false,false); expectSingle(0);
            }
        }
        state.gridView=false; LayoutGroupSections(state);
        fresh(); state.hovered=state.total+3;
        Check(KeyboardNavigationTarget(state,VK_UP)==0 && KeyboardNavigationTarget(state,VK_DOWN)==0,
            "stale hover displaced the first-item fallback");
        state.hovered=10; state.restoredKeyboardRow=7;
        Check(KeyboardNavigationTarget(state,VK_UP)==6 && KeyboardNavigationTarget(state,VK_DOWN)==8,
            "hover displaced a remembered keyboard item");
        state.restoredKeyboardRow=-1; state.vacantKeyboardRow=7;
        Check(KeyboardNavigationTarget(state,VK_UP)==6 && KeyboardNavigationTarget(state,VK_DOWN)==7,
            "hover displaced the post-deletion gap");
        ClearPostDeletionCaret(state); Select(state,3); state.hovered=10;
        NavigateKeyboardSelection(state,VK_DOWN,false,false); expectSingle(4);
        ClearPointerHover(state,false);
        Check(SetWindowPos(state.hwnd,nullptr,oldBounds.left,oldBounds.top,
            oldBounds.right-oldBounds.left,oldBounds.bottom-oldBounds.top,
            SWP_NOACTIVATE|SWP_NOZORDER|(wasVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)),"hover seed window restore");
    }
    Select(state,2); SetScrollImmediate(state,0.f); StopKeyboardSelectionAnimation(state);
    StopSelectionFade(state);
    g_smooth = smooth;
    Check(SetKeyboardState(saved), "repeat click keyboard restore");
    std::cout << "PASS: repeated clicks, bulk edge navigation, stationary-pointer first-key seeding/fallback in list/grid, remembered/deleted caret priority and native selection identities\n";
}

static void TestMarqueeSelection(const std::wstring& directory, bool entireDirectory = false) {
    WorkerComApartment apartment;
    ViewState state;
    state.hwnd = CreateWindowExW(0, L"STATIC", L"hidden-marquee", 0,
        0, 0, 500, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    state.nativeHwnd = MessageWindow();
    state.frameHwnd = state.hwnd;
    state.background->hwnd = state.hwnd;
    state.activeTabHost = state.hwnd;
    state.folderPath = directory;
    state.columnsView = false;
    state.headerPx = 0;
    state.loaded = true;
    state.token = 700;
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PointerCaptureProc));
    SetPropW(state.nativeHwnd, kOverlayProperty, state.hwnd);
    UpdateRefreshRate(state);
    PIDLIST_ABSOLUTE raw = nullptr;
    Check(SUCCEEDED(SHParseDisplayName(directory.c_str(), nullptr, &raw, 0, nullptr)), "marquee folder identity");
    OwnedPidl parent(raw);
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromIDList(parent.get(), IID_PPV_ARGS(&item))) &&
          SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&state.folder))), "marquee folder binding");
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        Entry entry;
        entry.name = file.path().filename().wstring();
        entry.path = file.path().wstring();
        state.entryPathIndex.emplace_back(SelectionPathHash(entry.path), static_cast<int>(state.entries.size()));
        state.entries.push_back(std::move(entry));
    }
    std::sort(state.entryPathIndex.begin(), state.entryPathIndex.end());
    state.total = static_cast<int>(state.entries.size());
    Check(state.total > 200, "marquee fixture needs a long directory");
    state.selectedFlags.resize(state.total);
    auto native = new SelectionFolderView;
    native->state = &state;
    native->folder = state.folder;
    state.folderView.Attach(native);
    if (!entireDirectory) {
        TestRecycleSelectionZoom(state,*native);
        TestRecycleSelectionZoom(state,*native,true);
        TestSelectionRecovery(state,*native);
        TestModalSelectionTransaction(state,*native);
        TestSelectionGestures(state, *native);
    }
    auto finishPending = [&] {
        Check(Eventually([&] {
            MSG message{};
            while (PeekMessageW(&message, state.hwnd, kMsgPreparedSelection, kMsgPreparedSelection, PM_REMOVE))
                OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
            return state.pendingNativeSelection == 0;
        }, 30000), "large marquee native selection did not complete");
    };
    if (!entireDirectory) {
        CancelNativeSelectionPreparation(state);
        auto obsolete=std::make_shared<PreparedNativeSelection>();
        obsolete->generation=g_nextToken.fetch_add(1); obsolete->viewToken=state.token;
        obsolete->native=state.nativeHwnd;
        state.pendingNativeSelection=obsolete->generation;
        state.background->selectionGeneration=obsolete->generation;
        { ExclusiveSrwLock lock(state.background->selectionLock); state.background->selectionResult=obsolete; }
        state.nativeSelectionRecoveryPending=true;
        BeginNativeSelectAll(state);
        Check(!state.pendingNativeSelection && !state.background->selectionGeneration &&
            !state.nativeSelectionRecoveryPending && state.nativeSelectionUnsafe,
            "Ctrl+A retained a previous bulk generation/recovery that could overwrite the new command");
        native->selected.clear();
        native->allCountOverride=state.total-1; // Private equivalent of a Shell-filtered hidden lock file.
        for(int i=0;i<state.total-1;++i) {
            PITEMID_CHILD child=nullptr;
            Check(SUCCEEDED(EntryChildPidl(state,state.entries[i],&child)) && child,"filtered Select All PIDL");
            native->selected.emplace_back(child);
        }
        native->focused.reset(ILClone(native->selected.front().get()));
        SetScrollImmediate(state,MaxScrollPx(state));
        const float atEnd=state.scrollNow;
        const auto arrays=native->itemArrayReads,commits=native->commits;
        OverlayProc(state.hwnd,kMsgAdoptNativeSelectAll,reinterpret_cast<WPARAM>(state.nativeHwnd),state.token-1);
        Check(!state.pendingNativeSelection,"departed Ctrl+A token started a selection import");
        OverlayProc(state.hwnd,kMsgAdoptNativeSelectAll,reinterpret_cast<WPARAM>(state.nativeHwnd),state.token);
        Check(state.pendingNativeSelection,"filtered Select All did not use bounded native identity capture");
        finishPending();
        Check(!state.nativeSelectionUnsafe && !state.nativeSelectionRecoveryPending && !state.directoryPending &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==state.total-1 &&
            !state.selectedFlags.back() && state.scrollNow==atEnd && state.scrollTarget==atEnd &&
            native->itemArrayReads==arrays && native->commits==commits,
            "filtered Ctrl+A refreshed/reset selection, moved the viewport, or fetched Shell arrays on UI");
        CancelNativeSelectionPreparation(state);
        state.selectedFlags.assign(state.total,0); state.selected=state.anchor=-1;
        Check(CaptureNativeSelection(state,true) && state.pendingNativeSelection,
            "filtered native all-selected event used the count-only shortcut");
        finishPending();
        Check(!state.nativeSelectionRecoveryPending && std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==state.total-1,
            "filtered all-selected event was cleared rather than mapped by identity");
        native->allCountOverride=-1;
        BeginNativeSelectAll(state);
        native->selected.clear();
        OverlayProc(state.hwnd,kMsgAdoptNativeSelectAll,reinterpret_cast<WPARAM>(state.nativeHwnd),state.token);
        Check(state.nativeSelectionDirty && state.nativeSelectionUnsafe,
            "early Ctrl+A observation consumed an old empty set as command completion");
        native->failSelectionCount=true; state.nativeSelectionDirty=false;
        OverlayProc(state.hwnd,kMsgAdoptNativeSelectAll,reinterpret_cast<WPARAM>(state.nativeHwnd),state.token);
        Check(state.nativeSelectionDirty && state.nativeSelectionUnsafe,
            "temporary Ctrl+A count failure stranded selection without a completion retry");
        native->failSelectionCount=false;
        state.nativeSelectionObservedCount=0; state.nativeSelectionObservedPaths.clear();
        Check(CaptureNativeSelection(state,false) && !state.nativeSelectionUnsafe,
            "unchanged small-selection idle shortcut stranded an unverified Ctrl+A state");
        state.nativeSelectionDirty=false; state.nativeSelectionUnsafe=false;
        CancelNativeSelectionPreparation(state);
        BeginNativeSelectAll(state,true);
        Check(!state.nativeSelectionUnsafe,"native Ctrl+A blocked Explorer's own subsequent file commands");
        std::cout<<"PASS: Ctrl+A cancels older bulk/recovery, rejects stale folders, imports a filtered native universe without UI arrays or viewport reset, and leaves early completion retryable\n";
    }
    if (!entireDirectory) {
        CancelNativeSelectionPreparation(state);
        native->selected.clear();
        for(int i=1;i<=82;++i) {
            PITEMID_CHILD child=nullptr;
            Check(SUCCEEDED(EntryChildPidl(state,state.entries[i],&child)) && child,"external selected PIDL fixture");
            native->selected.emplace_back(child);
        }
        native->focused.reset(ILClone(native->selected[40].get()));
        state.selectedFlags.assign(state.total,0); state.selected=state.anchor=-1;
        const auto commits=native->commits, arrays=native->itemArrayReads;
        Check(CaptureNativeSelection(state,true) && state.visible && state.pendingNativeSelection,
            "external partial selection above 64 detached instead of starting bounded identity capture");
        finishPending();
        Check(state.visible && !state.nativeSelectionUnsafe && state.confirmedNativeSelection &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==82 && state.selected==41 &&
            native->commits==commits && native->itemArrayReads==arrays && native->selected.size()==82,
            "external bulk import changed native selection, fetched Shell objects on UI, or lost identities/focus");
        CancelNativeSelectionPreparation(state);
        Check(CaptureNativeSelection(state,true) && state.pendingNativeSelection,"changing bulk snapshot setup");
        native->selected.pop_back();
        finishPending();
        Check(state.visible && state.nativeSelectionDirty && !state.pendingNativeSelection,
            "changing native snapshot detached rather than retrying");
        PITEMID_CHILD replacement=nullptr;
        Check(SUCCEEDED(EntryChildPidl(state,state.entries[200],&replacement)) && replacement,"bulk replacement fixture");
        native->selected.emplace_back(replacement);
        Check(CaptureNativeSelection(state,false) && state.pendingNativeSelection,"same-count unsafe retry was skipped as an idle echo");
        finishPending();
        Check(std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==82 && state.selectedFlags[200] &&
            !state.selectedFlags[82] && !state.nativeSelectionUnsafe,
            "bulk retry retained a stale selected set");
        CancelNativeSelectionPreparation(state);
        Check(CaptureNativeSelection(state,true) && state.pendingNativeSelection,"cancel bulk import setup");
        Select(state,200); finishPending();
        Check(native->selected.size()==1 && state.selected==200 && state.selectedFlags[200],
            "departed external import replaced a newer GPU selection");
        InvertKeyboardSelection(state); finishPending();
        Check(state.visible && native->selected.size()==state.entries.size()-1 && !state.selectedFlags[200],
            "large Invert Selection detached instead of using the shared prepared transaction");
        const auto index=std::move(state.entryPathIndex);
        state.entryPathIndex.clear(); state.thisPcGpu=true;
        Check(GpuRowForNativePath(state,state.entries[200].path)==200,"This PC identities stopped at 64 devices");
        state.thisPcGpu=false; state.entryPathIndex=index;
    }
    for (int frames : {20, entireDirectory ? (state.total * 3 + 50) : 180, -80}) {
        CancelNativeSelectionPreparation(state);
        state.scrollNow = state.scrollTarget = frames < 0 ? MaxScrollPx(state) * .5f : 0.f;
        const float startingScroll = state.scrollNow;
        const POINT edge{250, frames < 0 ? -30 : 430}; // Outside content, still captured.
        state.selectedFlags.assign(state.total, 0);
        native->selected.clear();
        native->focused.reset();
        native->commits = 0;
        state.selected = state.anchor = -1;
        OverlayProc(state.hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(2, frames < 0 ? 300 : 50));
        OverlayProc(state.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(edge.x, edge.y));
        Check(state.marqueeActive && GetCapture() == state.hwnd, "marquee did not acquire capture");
        for (int frame = 0; frame < std::abs(frames); ++frame) {
            QueryPerformanceCounter(&state.marqueeLastScrollQpc);
            state.marqueeLastScrollQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency * .024);
            UpdateMarqueeAutoscroll(state, edge, true);
        }
        Check((frames < 0 ? state.scrollNow < startingScroll : state.scrollNow > 0) &&
              native->commits == 0, "autoscroll committed native selection during the drag");
        const auto expected = state.selectedFlags;
        const auto count = std::count(expected.begin(), expected.end(), 1);
        const float viewport = state.scrollNow;
        native->collapseBatches = frames > 20 && !entireDirectory; // Exercise Explorer's first-item-only quirk.
        OverlayProc(state.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(edge.x, edge.y));
        Check(!state.marqueeActive && !state.marqueeFrameActive && GetCapture() != state.hwnd &&
              state.selectedFlags == expected, "release/capture reentrancy discarded the scrolled marquee");
        if (frames > 20) {
            Check(count > 64 && state.pendingNativeSelection != 0, "large selection was rejected instead of prepared");
            MSG key{};
            key.hwnd = state.nativeHwnd; key.message = WM_KEYDOWN; key.wParam = VK_DELETE;
            BYTE savedKeys[256]{}, plainKeys[256]{};
            Check(GetKeyboardState(savedKeys) && SetKeyboardState(plainKeys), "synthetic plain-key setup");
            const bool deferred = HandleNativeSelectionKey(key);
            Check(SetKeyboardState(savedKeys), "synthetic plain-key restoration");
            Check(deferred && state.deferredSelectionKeys.size() == 1,
                  "native command was not deferred behind selection preparation");
            finishPending();
            PidlSelection expectedIds;
            BorrowPreparedSelection(expectedIds, *state.confirmedNativeSelection);
            const unsigned beforePolls = native->selectionEnumerations;
            for (int poll = 0; poll < 8; ++poll)
                Check(CaptureNativeSelection(state, false), "unchanged large-selection idle poll");
            Check(native->selectionEnumerations == beforePolls,
                  "unchanged idle polls re-enumerated a committed large native selection");
            Check(CaptureNativeSelection(state, true) && native->selectionEnumerations > beforePolls,
                  "event-driven large-selection identity verification was skipped");
            if (!entireDirectory) {
                // A valid identity survives different cached size/time data.
                const auto& selectedEntries = state.confirmedNativeSelection->entries;
                const auto changedFile = std::find_if(selectedEntries.begin(), selectedEntries.end(),
                    // Prepared selections carry identities, not file-kind
                    // metadata. Inspect only this private filesystem fixture.
                    [](const Entry& entry) { return std::filesystem::is_regular_file(entry.path); });
                Check(changedFile != selectedEntries.end(), "metadata mutation needs a selected regular file");
                const auto changedIndex = static_cast<size_t>(changedFile - selectedEntries.begin());
                const Entry& changed = *changedFile;
                HANDLE file = CreateFileW(changed.path.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                Check(file != INVALID_HANDLE_VALUE, "fixture metadata change");
                DWORD written = 0;
                const char byte = 'x';
                const BOOL updated = WriteFile(file, &byte, 1, &written, nullptr);
                CloseHandle(file);
                Check(updated && written == 1, "fixture metadata write");
                PITEMID_CHILD refreshed = nullptr;
                Check(SUCCEEDED(EntryChildPidl(state, changed, &refreshed)) && refreshed,
                      "refreshed fixture identity");
                const auto selectedFile = std::find_if(native->selected.begin(), native->selected.end(),
                    [&](const OwnedPidl& item) { return ILIsEqual(item.get(), expectedIds.children[changedIndex]) != FALSE; });
                Check(selectedFile != native->selected.end(), "metadata mutation lost its selected native PIDL");
                selectedFile->reset(refreshed);
                Check(VerifyNativeSelection(state, expectedIds), "cached metadata differences broke identity verification");
            }
            OwnedPidl correct = std::move(native->selected.front());
            PITEMID_CHILD wrong = nullptr;
            Check(SUCCEEDED(EntryChildPidl(state, state.entries.front(), &wrong)) && wrong,
                  "different-item identity fixture");
            native->selected.front().reset(wrong);
            Check(!VerifyNativeSelection(state, expectedIds), "equal native count concealed a different selected item");
            native->selected.front() = std::move(correct);
        }
        Check(state.selectedFlags == expected && native->selected.size() == static_cast<size_t>(count) &&
              native->commits > 0, "Explorer did not receive the entire marquee selection");
        Check(CaptureNativeSelection(state, true) && state.visible && state.scrollNow == viewport &&
              state.selectedFlags == expected, "native selection echo reset the viewport or selected group");
    }
    // A queued old group may never overwrite the next click, even if its ready
    // notification was already posted before cancellation.
    native->collapseBatches = false;
    state.selectedFlags.assign(state.total, 1);
    Check(SyncNativeSelection(state) && state.pendingNativeSelection, "selection cancellation fixture");
    state.selectedFlags.assign(state.total, 0);
    state.selectedFlags[100] = 1;
    state.selected = state.anchor = 100;
    Check(SyncNativeSelection(state), "new click did not supersede bulk preparation");
    finishPending();
    Check(native->selected.size() == 1 && !state.pendingNativeSelection, "stale bulk selection replaced the newer click");
    const auto committedFlags = state.selectedFlags;
    PrepareMarquee(state, POINT{2, 50}, false);
    state.marqueeBeforeDrag = committedFlags;
    state.marqueeBeforeDragSelected = state.selected;
    state.marqueeBeforeDragAnchor = state.anchor;
    state.marqueeActive = state.marqueeFrameActive = true;
    UpdateMarquee(state, POINT{250, 430});
    // The frame's lost-capture recovery must roll back through the same path
    // as WM_CAPTURECHANGED, even if the original notification was missing.
    OverlayProc(state.hwnd, kMsgFrame, 0, 0);
    Check(!state.marqueeActive && !state.marqueeFrameActive && state.selectedFlags == committedFlags &&
          native->selected.size() == 1, "lost-capture recovery left an uncommitted GPU selection");
    if (!entireDirectory) {
        auto blocked = std::make_shared<TabBackground>();
        blocked->hwnd = MessageWindow();
        providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const auto entered = blockedProviders.load();
        Check(StartTabWorker(blocked, blocked->selection, BlockedProvider, THREAD_PRIORITY_NORMAL),
              "blocked selection lane startup");
        Check(Eventually([&] { return blockedProviders.load() > entered; }), "blocked selection lane did not enter");
        state.selectedFlags.assign(state.total, 1);
        native->collapseDeselectBatch = true;
        const unsigned commits = native->commits;
        Check(SyncNativeSelection(state), "independent selection preparation startup");
        finishPending();
        Check(native->selected.size() == state.entries.size() && native->commits - commits <= 5,
              "blocked tab or DESELECTOTHERS quirk prevented a bounded-call native batch");
        native->collapseDeselectBatch = false;
        SetEvent(providerGate);
        Check(Eventually([&] { return blocked->Finished(); }), "blocked lane did not finish");
        DestroyWindow(blocked->hwnd);
        Stop(blocked);
        CloseHandle(providerGate);
        providerGate = nullptr;
    }
    auto queued = std::make_shared<std::atomic<bool>>(false);
    state.selectionEventMessageQueued = queued;
    ComPtr<IDispatch> sink;
    sink.Attach(new ShellSelectionAutomationSink(state.hwnd, state.nativeHwnd, state.token, queued));
    for (int i = 0; i < 10000; ++i)
        sink->Invoke(DISPID_SELECTIONCHANGED, IID_NULL, 0, DISPATCH_METHOD, nullptr, nullptr, nullptr, nullptr);
    int notifications = 0;
    MSG message{};
    while (PeekMessageW(&message, state.hwnd, kMsgShellSelectionEvent, kMsgShellSelectionEvent, PM_REMOVE)) {
        ++notifications;
        OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
    }
    Check(notifications == 1 && !queued->load(), "Shell selection burst flooded the queue or retained an inactive-tab latch");
    native->selected.clear();
    PITEMID_CHILD committedItem = nullptr;
    Check(SUCCEEDED(EntryChildPidl(state, state.entries[100], &committedItem)) && committedItem,
          "rejected native selection fixture");
    native->selected.emplace_back(committedItem);
    native->focused.reset(ILClone(committedItem));
    native->rejectSelection = true;
    for (bool large : {false, true}) {
        state.selectedFlags.assign(state.total, large ? 1 : 0);
        state.selectedFlags[200] = 1;
        state.selected = state.anchor = 200;
        SetScrollImmediate(state,MaxScrollPx(state));
        const float atEnd=state.scrollNow;
        Check(!SyncNativeSelection(state) && !state.pendingNativeSelection && state.visible &&
              state.nativeSelectionRecoveryPending && state.nativeSelectionUnsafe &&
              state.scrollNow==atEnd && state.scrollTarget==atEnd && native->selected.size()==1,
              "failed native commit imported an unrelated first item or reset End's viewport");
    }
    SetScrollImmediate(state,0.f);
    NavigateKeyboardSelection(state,VK_END,false,false);
    Check(state.nativeSelectionRecoveryPending && state.nativeSelectionUnsafe &&
        state.scrollTarget==MaxScrollPx(state),"End lost its end viewport after selecting a native-excluded row");
    SetScrollImmediate(state,MaxScrollPx(state));
    native->rejectSelection = false;
    StopKeyboardSelectionAnimation(state); StopSelectionFade(state); StopHoverAnimation(state);
    StopScrollAnimation(state); state.lastInteractionTick=0;
    MaybeRecoverNativeSelection(state);
    Check(!state.nativeSelectionRecoveryPending && !state.nativeSelectionUnsafe && native->selected.empty() &&
        state.selected==-1 && state.scrollNow==MaxScrollPx(state),
        "failed End selection did not clear both selections while preserving the end viewport");
    CancelNativeSelectionPreparation(state);
    Stop(state.background);
    UnregisterAnimationWindow(state.hwnd);
    RemovePropW(state.nativeHwnd, kOverlayProperty);
    DestroyWindow(state.nativeHwnd);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
    DestroyWindow(state.hwnd);
    std::cout << "PASS: marquee edge autoscroll/release, native reentrancy, groups over 64, identity verification, viewport preservation, and cancellation (" << state.total << " real filesystem items)\n";
}
