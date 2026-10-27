// Native verbs are captured only. Private names publications model deletion;
// these checks never invoke a real Delete or touch the user's clipboard/focus.
static void TestSelectionRecovery(ViewState& state,SelectionFolderView& native) {
    const auto entries=state.entries;
    const auto initialIndex=state.entryPathIndex;
    const float originalScroll=state.scrollNow;
    const bool smooth=g_smooth.exchange(false);
    FocusFixture::Session focus(state.frameHwnd,state.hwnd);
    ComPtr<SelectionCommandMenu> menu; menu.Attach(new SelectionCommandMenu);
    ComPtr<SelectionCommandView> shell; shell.Attach(new SelectionCommandView(menu.Get()));
    shell->window=state.nativeHwnd;
    native.commandView=shell;
    const auto settle=[&] {
        StopRowTransition(state); FinishColumnTransition(state);
        StopHoverAnimation(state); StopSelectionFade(state); StopKeyboardSelectionAnimation(state);
        StopScrollAnimation(state);
    };
    const auto publish=[&](std::vector<Entry> rows) {
        DirectoryResult result; result.kind=DirectoryResult::Complete; result.token=state.directoryToken;
        result.entries=std::move(rows); result.oldTotal=state.total;
        result.oldRowPx=state.rowPx; result.oldScroll=state.scrollNow;
        for(size_t i=0;i<result.entries.size();++i)
            result.pathIndex.emplace_back(SelectionPathHash(result.entries[i].path),static_cast<int>(i));
        std::sort(result.pathIndex.begin(),result.pathIndex.end());
        result.orderHash=DirectoryOrderHash(result.entries); result.metadataHash=DirectoryMetadataHash(result.entries);
        PublishDirectoryResult(*state.background,std::move(result)); ReceiveDirectoryResults(state); settle();
    };
    Select(state,7); settle();
    menu->duringInvoke=[&] {
        Check(state.nativeFileCommandDepth,"Delete verb had no actual command lifetime gate");
        RejectNativeSelectionImport(state,L"test: transient Delete selection",0,1);
        Check(!CaptureNativeSelection(state,true) && state.visible && state.selectedFlags[7] && native.selected.size()==1,
            "reentrant Delete mismatch detached or cleared a still-running command's selection");
        state.directoryPending=true;
    };
    ViewState::DeferredSelectionKey deletion; deletion.key=VK_DELETE;
    Check(SUCCEEDED(InvokeNativeFileKey(state,shell.Get(),deletion)) && menu->calls.size()==1 &&
        !state.nativeFileCommandDepth && state.nativeSelectionRecoveryPending,"Delete command did not return with recovery retained");
    menu->duringInvoke={};
    MaybeRecoverNativeSelection(state);
    Check(state.selectedFlags[7] && native.selected.size()==1 && state.visible,
        "directory work was cleared before its actual names completion");
    auto removed=entries; removed.erase(removed.begin()+7); publish(std::move(removed));
    state.watchedChangePending=true;
    MaybeRecoverNativeSelection(state);
    Check(state.nativeSelectionRecoveryPending && native.selected.size()==1,"ongoing watcher batch licensed recovery");
    state.watchedChangePending=false;
    MaybeRecoverNativeSelection(state);
    Check(state.visible && state.total==static_cast<int>(entries.size()-1) &&
        !state.nativeSelectionRecoveryPending && !state.nativeSelectionUnsafe && native.selected.empty() &&
        std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)==0,
        "completed Delete failed to retain the GPU rows and clear both selections");
    publish(entries);
    // Repeated failures to clear native selection stay pending and do not
    // license another file command or recursively post reset attempts.
    Select(state,7); settle(); native.rejectSelection=true;
    RejectNativeSelectionImport(state,L"test: unresolved selection",0,1);
    const auto calls=menu->calls.size();
    for(int attempt=0;attempt<8;++attempt) {
        MaybeRecoverNativeSelection(state);
        Check(state.visible && state.nativeSelectionRecoveryPending && state.nativeSelectionUnsafe && native.selected.size()==1,
            "transient empty-selection failure detached or abandoned recovery");
    }
    MSG key{}; key.hwnd=state.hwnd; key.message=WM_KEYDOWN; key.wParam=VK_DELETE;
    Check(HandleNativeFileKey(key,&state) && menu->calls.size()==calls,
        "unverified selection recovery authorized a file operation");
    native.rejectSelection=false;
    MaybeRecoverNativeSelection(state);
    Check(state.visible && !state.nativeSelectionRecoveryPending && !state.nativeSelectionUnsafe &&
        native.selected.empty() && state.selected==-1,"native reset did not resume after transient failure");
    // A fresh, verified click cancels an old queued recovery.
    Select(state,7); settle(); RejectNativeSelectionImport(state,L"test: obsolete reset",0,1);
    Select(state,11); settle(); MaybeRecoverNativeSelection(state);
    Check(!state.nativeSelectionRecoveryPending && state.selectedFlags[11] && native.selected.size()==1,
        "obsolete recovery wiped a later explicit selection");
    // Even an enabled modal owner must defer recovery until real command exit.
    {
        NativeFileCommandScope command(&state);
        RejectNativeSelectionImport(state,L"test: enabled modal owner",0,1);
        MaybeRecoverNativeSelection(state);
        Check(state.nativeSelectionRecoveryPending && native.selected.size()==1,"enabled modal loop bypassed command completion");
    }
    MaybeRecoverNativeSelection(state);
    Check(!state.nativeSelectionRecoveryPending && native.selected.empty(),"command exit stranded recovery");
    // Persistent identities absent even after refresh and failed refreshes
    // enter the same reset path, without deactivating the renderer.
    Select(state,7); settle();
    state.nativeSelectionPaths={L"private-unmatched-selection"};
    state.nativeSelectionImportPending=state.nativeSelectionAwaitingRefresh=true;
    ApplyImportedNativeSelection(state,false);
    Check(state.visible && state.nativeSelectionRecoveryPending,"unmatched post-refresh identity detached the overlay");
    MaybeRecoverNativeSelection(state);
    Check(!state.nativeSelectionRecoveryPending && native.selected.empty(),"unmatched selection did not recover");
    Select(state,7); settle();
    state.nativeSelectionPaths={L"private-unmatched-selection"};
    state.nativeSelectionImportPending=state.nativeSelectionAwaitingRefresh=state.directoryPending=true;
    DirectoryResult failed; failed.kind=DirectoryResult::Complete; failed.token=state.directoryToken; failed.error=ERROR_NOT_READY;
    PublishDirectoryResult(*state.background,std::move(failed)); ReceiveDirectoryResults(state); settle();
    Check(state.visible && state.loaded && state.nativeSelectionRecoveryPending && state.entries.size()==entries.size(),
        "failed revalidation detached or discarded the last usable rows");
    MaybeRecoverNativeSelection(state);
    Check(!state.nativeSelectionRecoveryPending && native.selected.empty(),"failed refresh stranded recovery");
    Select(state,7); settle();
    const HWND previousNative=state.nativeHwnd;
    const auto previousView=state.folderView;
    const auto previousToken=state.token;
    HWND replacementWindow=MessageWindow();
    auto* replacement=new SelectionFolderView;
    replacement->state=&state; replacement->folder=state.folder;
    ComPtr<IFolderView2> replacementView; replacementView.Attach(replacement);
    PITEMID_CHILD child=nullptr;
    Check(SUCCEEDED(EntryChildPidl(state,state.entries[11],&child)) && child,"reentrant recovery replacement identity");
    replacement->selected.emplace_back(child);
    native.duringClear=[&] {
        state.nativeHwnd=replacementWindow; state.folderView=replacementView; ++state.token;
        state.nativeSelectionRecoveryPending=state.nativeSelectionUnsafe=state.nativeSelectionDirty=false;
        state.selectedFlags.assign(state.total,0); state.selectedFlags[11]=1; state.selected=state.anchor=11;
    };
    RejectNativeSelectionImport(state,L"test: navigation during clear",0,1);
    MaybeRecoverNativeSelection(state);
    Check(state.selectedFlags[11] && replacement->selected.size()==1 && !state.nativeSelectionRecoveryPending,
        "reentrant reset cleared the replacement view or its GPU selection");
    state.nativeHwnd=previousNative; state.folderView=previousView; state.token=previousToken;
    replacementView.Reset(); DestroyWindow(replacementWindow);
    Check(state.entryPathIndex==initialIndex,"selection recovery replaced the directory identity index");
    native.commandView.Reset(); CancelNativeSelectionPreparation(state);
    state.nativeSelectionDirty=false; state.nativeSelectionObservedCount=-1;
    state.enumerationError=ERROR_SUCCESS;
    state.selectedFlags.assign(state.total,0); state.selected=state.anchor=-1;
    ClearPostDeletionCaret(state); settle(); state.scrollNow=state.scrollTarget=originalScroll;
    g_smooth=smooth;
    std::cout<<"PASS: no-detach selection recovery across actual Delete verb lifetime, names/watch completion, failed resets/refresh, unmatched identities, blocked file commands and newer selection; no real file operations\n";
}
