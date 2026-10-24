// Private Shell filesystem objects and offscreen HWNDs exercise production
// rename paths. Focus, pointer state and error dialogs remain substituted.
namespace RenameHandoffFixture {
inline thread_local std::function<void()> nativeCancel;
class Folder final : public IShellFolder {
    ULONG refs=1;
public:
    ComPtr<IShellFolder> inner;
    std::function<void()> probe;
    std::function<void()> beforeParse;
    bool reject=false;
    ComPtr<IDropTarget> dropTarget;
    std::function<void()> beforeDropResolve;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(iid==IID_IUnknown || iid==IID_IShellFolder) {*out=this; AddRef(); return S_OK;}
        return inner->QueryInterface(iid,out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs; if(!n) delete this; return n;}
    HRESULT STDMETHODCALLTYPE SetNameOf(HWND window,PCUITEMID_CHILD child,LPCWSTR name,SHGDNF flags,PITEMID_CHILD* out) override {
        if(probe) probe();
        if(reject) {*out=nullptr; return E_ACCESSDENIED;}
        return inner->SetNameOf(window,child,name,flags,out);
    }
    HRESULT STDMETHODCALLTYPE ParseDisplayName(HWND w,IBindCtx* c,LPWSTR n,ULONG* e,PIDLIST_RELATIVE* p,ULONG* a) override {
        if(auto callback=std::exchange(beforeParse,{})) callback();
        return inner->ParseDisplayName(w,c,n,e,p,a);
    }
    HRESULT STDMETHODCALLTYPE EnumObjects(HWND w,SHCONTF f,IEnumIDList** o) override {return inner->EnumObjects(w,f,o);}
    HRESULT STDMETHODCALLTYPE BindToObject(PCUIDLIST_RELATIVE p,IBindCtx* c,REFIID i,void** o) override {return inner->BindToObject(p,c,i,o);}
    HRESULT STDMETHODCALLTYPE BindToStorage(PCUIDLIST_RELATIVE p,IBindCtx* c,REFIID i,void** o) override {return inner->BindToStorage(p,c,i,o);}
    HRESULT STDMETHODCALLTYPE CompareIDs(LPARAM l,PCUIDLIST_RELATIVE a,PCUIDLIST_RELATIVE b) override {return inner->CompareIDs(l,a,b);}
    HRESULT STDMETHODCALLTYPE CreateViewObject(HWND w,REFIID i,void** o) override {
        if(i==IID_IDropTarget && dropTarget) {
            if(beforeDropResolve) beforeDropResolve();
            return dropTarget->QueryInterface(i,o);
        }
        return inner->CreateViewObject(w,i,o);
    }
    HRESULT STDMETHODCALLTYPE GetAttributesOf(UINT n,PCUITEMID_CHILD_ARRAY p,SFGAOF* a) override {return inner->GetAttributesOf(n,p,a);}
    HRESULT STDMETHODCALLTYPE GetUIObjectOf(HWND w,UINT n,PCUITEMID_CHILD_ARRAY p,REFIID i,UINT* r,void** o) override {return inner->GetUIObjectOf(w,n,p,i,r,o);}
    HRESULT STDMETHODCALLTYPE GetDisplayNameOf(PCUITEMID_CHILD p,SHGDNF f,STRRET* o) override {return inner->GetDisplayNameOf(p,f,o);}
};
inline LRESULT CALLBACK NativeEdit(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(message==WM_KEYDOWN && wp==VK_ESCAPE) {
        if(nativeCancel) nativeCancel();
        DestroyWindow(window); return 0;
    }
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,NativeEdit,id);
    return DefSubclassProc(window,message,wp,lp);
}
}

static void TestEditorSelectionOwnership(const std::wstring& directory) {
    auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"editor ownership fonts");
    const HWND actualFocus=GetFocus(),actualForeground=GetForegroundWindow();
    for(bool rtl:{false,true}) for(bool grid:{false,true}) for(bool folderItem:{false,true}) for(unsigned route=0;route<3;++route) {
        PresentationFixture::Tab tab(L"editor selection ownership",12,rtl,grid);
        auto& state=tab.state;
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame,state.hwnd);
        PointerFixture::Session pointer; PointerFixture::held=false;
        ModifierFixture::Session modifiers;
        const auto path=std::filesystem::path(directory)/(L"editor-ownership-"+std::to_wstring(rtl)+
            std::to_wstring(grid)+std::to_wstring(folderItem)+std::to_wstring(route));
        std::filesystem::create_directories(path);
        for(int i=0;i<12;++i) {
            auto& entry=state.entries[size_t(i)]; entry.name=L"item"+std::to_wstring(i);
            entry.path=(path/entry.name).wstring(); entry.folder=folderItem;
            if(folderItem) std::filesystem::create_directories(entry.path);
            else {
                HANDLE file=CreateFileW(entry.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                    nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                Check(file!=INVALID_HANDLE_VALUE,"private New file fixture"); CloseHandle(file);
            }
            state.entryPathIndex.emplace_back(SelectionPathHash(entry.path),i);
        }
        std::sort(state.entryPathIndex.begin(),state.entryPathIndex.end());
        state.folderPath=path.wstring(); state.selected=state.anchor=6;
        state.selectedFlags.assign(state.entries.size(),0); state.selectedFlags[6]=1;
        HWND native=CreateWindowExW(0,L"STATIC",L"native list",WS_CHILD,0,0,800,480,tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        state.nativeHwnd=native;
        SetPropW(native,kOverlayProperty,state.hwnd);
        HWND address=CreateWindowExW(0,L"EDIT",L"address/search fixture",WS_CHILD,0,0,100,24,
            tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        FocusFixture::focus=address;
        Check(InputFocusOwner(state)==ViewInputOwner::OtherControl && !OverlayHasInputFocus(state) &&
            !ReturnListInputFocus(state,nullptr) && FocusFixture::focus==address,"list return stole address/search input");
        MSG staleKey{}; staleKey.hwnd=native; staleKey.message=WM_KEYDOWN; staleKey.wParam=VK_DOWN;
        Check(!OverlayKeyboardTarget(staleKey),"native queue key stole address/search ownership");
        FocusFixture::focus=state.frameHwnd; FocusFixture::ownerEnabled=false;
        Check(!ReturnListInputFocus(state,nullptr),"disabled/modal frame accepted list focus");
        FocusFixture::ownerEnabled=true; FocusFixture::foreground=nullptr;
        Check(!ReturnListInputFocus(state,nullptr),"background Explorer stole list focus");
        FocusFixture::foreground=tab.frame; FocusFixture::rejected=native;
        Check(ReturnListInputFocus(state,tab.frame) && FocusFixture::focus==state.hwnd,"rejected hidden host had no GPU input fallback");
        FocusFixture::rejected=nullptr; FocusFixture::focus=state.hwnd;
        auto* folder=new RenameHandoffFixture::Folder;
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&folder->inner))),"ownership private folder");
        state.folder.Attach(folder);
        auto* view=new SelectionFolderView; view->folder=state.folder; view->state=&state; view->reenterCapture=true;
        state.folderView.Attach(view);
        Check(SyncNativeSelection(state),"initial editor selection");
        if(route==0) {
            state.pendingCreatedItemPath=state.entries[6].path;
            state.pendingCreatedItemNative=native; state.pendingCreatedItemToken=state.token;
            TryStartCreatedItemRename(state);
        } else {
            if(route==1) ArmInteractiveCreation(state,1,L"shortcut ownership fixture");
            HWND edit=CreateWindowExW(0,L"EDIT",L"item6",WS_CHILD,0,0,100,24,native,nullptr,GetModuleHandleW(nullptr),nullptr);
            SetWindowSubclass(edit,RenameHandoffFixture::NativeEdit,1,0); FocusFixture::focus=edit;
            RenameHandoffFixture::nativeCancel=[&] {
                view->selected.clear(); view->focused.reset();
                CaptureNativeSelection(state,true); // Shell focus/selection echo during cancellation.
            };
            Check(AdoptNativeRenameEditor(state,edit),"native editing ownership transfer");
            RenameHandoffFixture::nativeCancel={};
        }
        Check(state.rename.editor && state.selected==6 && state.selectedFlags[6] &&
            InputFocusOwner(state)==ViewInputOwner::RenameEditor && !OverlayHasInputFocus(state),"editor failed to own its selected identity/input");
        Check(!OverlayKeyboardTarget(staleKey),"native queue key bypassed the active editor owner");
        // A worker import captured before editor focus must not overwrite the
        // editing session, even when names delivery tries applying it directly.
        state.nativeSelectionImportPending=true; state.nativeSelectionPaths.clear(); state.nativeFocusedPath.clear();
        ApplyImportedNativeSelection(state);
        Check(state.selected==6 && state.selectedFlags[6],"stale empty native import erased editor-owned selection");
        SetWindowTextW(state.rename.editor,L"renamed item");
        FinishRename(state,true);
        Check(state.selected==6 && state.selectedFlags[6] && OverlayHasInputFocus(state),"editor-to-list handoff lost selection or HWND focus");
        const auto renamed=state.entries[6].path;
        Check(GpuRowForNativePath(state,renamed)==6,"renamed identity missing from live path index");
        CaptureNativeSelection(state,true);
        Check(state.selected==6 && state.selectedFlags[6] && view->selected.size()==1 && view->focused,
            "post-editor native focus echo replaced the selected item");
        // Publish a reordered names snapshot through the real directory path.
        auto rows=state.entries; auto renamedEntry=rows[6]; rows.erase(rows.begin()+6); rows.insert(rows.begin()+3,renamedEntry);
        DirectoryResult result; result.kind=DirectoryResult::Complete; result.token=state.directoryToken;
        result.entries=std::move(rows); result.oldTotal=state.total; result.oldRowPx=state.rowPx; result.oldScroll=state.scrollNow;
        for(size_t i=0;i<result.entries.size();++i) result.pathIndex.emplace_back(SelectionPathHash(result.entries[i].path),int(i));
        std::sort(result.pathIndex.begin(),result.pathIndex.end()); result.orderHash=DirectoryOrderHash(result.entries);
        result.metadataHash=DirectoryMetadataHash(result.entries);
        PublishDirectoryResult(*state.background,std::move(result)); ReceiveDirectoryResults(state);
        StopRowTransition(state); StopScrollAnimation(state);
        Check(state.selected==3 && state.selectedFlags[3],"post-rename reorder lost identity/caret");
        for(UINT key:{VK_DOWN,VK_UP}) {
            const int target=KeyboardNavigationTarget(state,key,true);
            MSG input{}; input.hwnd=FocusFixture::focus; input.message=WM_KEYDOWN; input.wParam=key;
            Check(OverlayKeyboardTarget(input)==state.hwnd,"native list focus did not route list arrows to the active overlay");
            OverlayProc(state.hwnd,WM_KEYDOWN,key,0);
            Check(target>=0 && state.selected==target && state.selectedFlags[size_t(target)] &&
                state.selected!=0,"arrow navigation restarted at row zero after editor handoff");
        }
        CancelRename(state);
        folder->beforeParse=[&] {++state.token;};
        BeginRename(state,state.selected);
        Check(!folder->beforeParse && !state.rename.editor,"reentrant Shell rebind opened an editor for an obsolete input owner");
        RemovePropW(native,kOverlayProperty);
    }
    g_writeFactory=factory;
    Check(GetFocus()==actualFocus && GetForegroundWindow()==actualForeground,"ownership tests changed desktop focus");
    std::cout<<"PASS: shared editor/list focus and selection ownership; address/modal/background guards and hidden-host fallback; native cancellation echoes, stale imports, indexed rename/reorder and routed arrows across all New file/folder routes in LTR/RTL list/grid\n";
}

static void TestRenameHandoff(const std::wstring& directory) {
    auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"rename fonts");
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    cls.lpszClassName=L"CabinetWClass"; RegisterClassW(&cls);
    const HWND actualFocus=GetFocus(),actualForeground=GetForegroundWindow();
    for(bool rtl:{false,true}) for(bool grid:{false,true}) {
        PresentationFixture::Tab tab(L"rename handoff",1,rtl,grid,false,800,480,cls.lpszClassName);
        auto& state=tab.state;
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        HWND nativeHost=CreateWindowExW(0,L"STATIC",L"suppressed native view",WS_CHILD,
            0,0,800,480,tab.frame,nullptr,cls.hInstance,nullptr);
        state.nativeHwnd=nativeHost;
        AcquireSRWLockExclusive(&g_tabRegistryLock); g_tabOverlayByHost[tab.frame]=state.hwnd; ReleaseSRWLockExclusive(&g_tabRegistryLock);
        FocusFixture::Session focus(tab.frame,state.hwnd);
        PointerFixture::Session pointer; PointerFixture::held=false;
        const auto path=std::filesystem::path(directory)/(L"rename-"+std::to_wstring(rtl)+std::to_wstring(grid));
        std::filesystem::create_directories(path/L"old folder");
        state.folderPath=path.wstring();
        auto* folder=new RenameHandoffFixture::Folder;
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&folder->inner))),"rename private Shell folder");
        state.folder.Attach(folder);
        auto* view=new SelectionFolderView; view->folder=state.folder; view->state=&state; view->reenterCapture=false;
        state.folderView.Attach(view);
        state.entries[0].name=L"old folder"; state.entries[0].path=(path/L"old folder").wstring();
        state.entries[0].folder=true; state.selected=state.anchor=0; state.selectedFlags={1};
        ULONG eaten=0; PITEMID_CHILD child=nullptr;
        std::wstring oldName=L"old folder";
        Check(SUCCEEDED(folder->ParseDisplayName(nullptr,nullptr,oldName.data(),&eaten,&child,nullptr)),"rename private PIDL");
        view->focused.reset(child);
        // Each route shares the native-editor handoff. Only the shortcut and
        // context menu have a pre-recorded intent; the ribbon has none.
        for(unsigned route=0;route<3;++route) {
            if(route<2) ArmInteractiveCreation(state,route==0 ? 1 : 3,L"private New route");
            HWND editor=CreateWindowExW(0,L"EDIT",L"typed before names arrived",WS_CHILD|ES_AUTOHSCROLL,
                10,10,180,24,nativeHost,nullptr,cls.hInstance,nullptr);
            Check(editor && !IsWindowVisible(editor),"native editor must actually be obscured");
            SetWindowSubclass(editor,RenameHandoffFixture::NativeEdit,1,0);
            SendMessageW(editor,EM_SETSEL,2,8); FocusFixture::focus=editor;
            RefreshInputFocusIndicators(state);
            Check(route==2 || state.creationCommandMask,"native EDIT focus erased New intent");
            state.loaded=false; state.directoryPending=true;
            Check(!AdoptNativeRenameEditor(state,editor) && IsWindow(editor),"late names lost native input");
            state.loaded=true; state.directoryPending=false;
            if(route==0) {
                MSG input{}; input.hwnd=editor; input.message=WM_CHAR; input.wParam='a';
                MSG routed{};
                Check(RouteNativeRenameMessage(input,routed) && routed.hwnd==state.rename.editor &&
                    routed.message==WM_CHAR && routed.wParam=='a',"first native character did not target the visible editor");
            } else {
                QueueNativeRenameEditor(editor);
                MSG wake{};
                Check(PeekMessageW(&wake,state.hwnd,kMsgNativeRenameEditor,kMsgNativeRenameEditor,PM_REMOVE),"native show/focus handoff was not queued");
                OverlayProc(state.hwnd,wake.message,wake.wParam,wake.lParam);
            }
            Check(state.rename.editor && IsWindowVisible(state.rename.editor) && !IsWindow(editor),"New route did not replace the hidden editor");
            wchar_t text[64]{}; GetWindowTextW(state.rename.editor,text,ARRAYSIZE(text));
            DWORD first=0,last=0; SendMessageW(state.rename.editor,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
            Check(!wcscmp(text,L"typed before names arrived") && first==2 && last==8,"handoff lost typed text/caret");
            ShowWindow(state.rename.editor,SW_HIDE); SetWindowPos(state.hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            RepositionRenameEdit(state);
            Check(IsWindowVisible(state.rename.editor) && GetWindow(tab.frame,GW_CHILD)==state.rename.editor,"unchanged geometry did not repair EDIT visibility/Z order");
            CancelRename(state); FocusFixture::focus=state.hwnd;
        }
        // Commit by the same blank-space mouse-down used in Explorer. Freeze
        // the Shell call at its entry and inspect the already submitted frame.
        BeginRename(state,0); Check(state.rename.editor!=nullptr,"rename commit editor");
        HWND oldEditor=state.rename.editor;
        SetWindowTextW(oldEditor,L"new folder");
        auto lane=std::make_shared<PresentationLane>(); lane->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        lane->lifetime=state.lifetimeToken; lane->hwnd=state.hwnd; state.presenter=lane;
        bool entered=false;
        folder->probe=[&] {
            entered=true;
            Check(!IsWindow(oldEditor) && !state.rename.editor,"Shell rename started with a live field");
            Check(lane->pending && !lane->pending->renameEditor && lane->pending->items.at(0).entry.name==L"new folder",
                "replacement label waited for SetNameOf/directory notification");
            state.entries[0].name=L"old folder"; // Reentrant stale names delivery.
            auto packet=CapturePresentationModel(state);
            Check(packet->items.at(0).entry.name==L"new folder","reentrant old snapshot covered the pending new label");
        };
        OverlayProc(state.hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(700,450));
        Check(entered && state.renamePreviewPath.empty() && state.entries[0].name==L"new folder" &&
            std::filesystem::exists(path/L"new folder"),"rename did not publish Shell success immediately");
        folder->probe={}; folder->reject=true;
        state.selectedFlags={1}; state.selected=state.anchor=0; FocusFixture::focus=state.hwnd;
        BeginRename(state,0); Check(state.rename.editor!=nullptr,"rename rollback editor");
        SetWindowTextW(state.rename.editor,L"rejected folder");
        ReportApiFixture::suppressRenameError=true;
        const auto errors=ReportApiFixture::renameErrors;
        FinishRename(state,true); ReportApiFixture::suppressRenameError=false;
        Check(state.entries[0].name==L"new folder" && state.renamePreviewPath.empty() &&
            ReportApiFixture::renameErrors==errors+1,"Shell failure did not restore the real name/report error");
        state.presenter.reset(); CloseHandle(lane->wake); lane->wake=nullptr;
        AcquireSRWLockExclusive(&g_tabRegistryLock); g_tabOverlayByHost.erase(tab.frame); ReleaseSRWLockExclusive(&g_tabRegistryLock);
        CancelRename(state); ClearInteractiveCreation(state);
    }
    g_writeFactory=factory;
    Check(GetFocus()==actualFocus && GetForegroundWindow()==actualForeground,"rename tests changed user focus");
    std::cout<<"PASS: rename click handoff precedes Shell work; native New editor adoption preserves text/caret; Z order, stale-name protection and failure rollback in LTR/RTL list/grid\n";
}
