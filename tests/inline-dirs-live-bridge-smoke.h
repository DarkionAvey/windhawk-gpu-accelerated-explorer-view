// What Explorer itself receives for a selection that includes items inside
// expanded folders, against a real ExplorerBrowser: the objects the selection
// bridge and the item menu's item source (CDefView::_GetItems) hand out, how
// a view is matched to its tab, Explorer's SelectionChanged notice, and the
// selection identities' cache through deletions. Hooks are not installed in
// the regression process; their bodies' shared functions are exercised.
namespace InlineLive {
inline UINT ArrayCount(IUnknown* object) {
    ComPtr<IShellItemArray> array; ComPtr<IObjectArray> objects;
    DWORD count=0; UINT items=0;
    if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&array))) && SUCCEEDED(array->GetCount(&count))) return count;
    if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&objects))) && SUCCEEDED(objects->GetCount(&items))) return items;
    return 0;
}
}

// The selection's objects for every interface Explorer's readers and its item
// menu ask for, for selections inside one expanded folder, across folders,
// and mixed with the open folder's own rows; other interfaces keep
// Explorer's own answer.
static void TestInlineDirsLiveSelectionObjects(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-bridge-objects");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma"); live.Open(L"gamma\\inner");
    for(const auto& selection:std::vector<std::vector<const wchar_t*>>{
            {L"alpha\\a.txt"},
            {L"alpha\\a.txt",L"alpha\\b.txt"},
            {L"alpha\\a.txt",L"gamma\\inner\\i.txt"},
            {L"alpha\\sub",L"gamma\\c.txt",L"zeta.txt"}}) {
        std::string label; for(const auto* item:selection) label+=std::string(item,item+wcslen(item))+" ";
        Regression::Scope scope("selection "+label);
        bool first=true;
        for(const auto* item:selection) { SelectWithModifiers(s,live.Row(item),!first,false); first=false; }
        Check(InlineSelectionDetached(s),"PRECONDITION: the selection includes items of expanded folders");
        for(const IID* riid:{&IID_IShellItemArray,&IID_IObjectCollection,&IID_IObjectArray}) {
            ComPtr<IUnknown> object;
            Check(SUCCEEDED(InlineSelectionViewObject(s,*riid,reinterpret_cast<void**>(object.GetAddressOf()))) && object,
                "no item set for the selection");
            Check(ArrayCount(object.Get())==selection.size(),"the item set does not hold exactly the selection");
        }
        ComPtr<IDataObject> data;
        Check(SUCCEEDED(InlineSelectionViewObject(s,IID_IDataObject,reinterpret_cast<void**>(data.GetAddressOf()))) && data,
            "no data object for the selection");
        Check(DropPaths(data.Get()).size()==selection.size(),"the data object does not carry the selection");
        ComPtr<IContextMenu3> menu;
        Check(SUCCEEDED(InlineSelectionViewObject(s,IID_IContextMenu3,reinterpret_cast<void**>(menu.GetAddressOf()))) && menu,
            "no item menu for the selection");
        const auto verbs=MenuVerbs(menu.Get());
        for(const auto* verb:{L"delete",L"copy",L"cut",L"properties"})
            Check(HasVerb(verbs,verb),"the selection's item menu lacks a standard file command");
        ComPtr<IUnknown> other;
        Check(InlineSelectionViewObject(s,IID_IEnumIDList,reinterpret_cast<void**>(other.GetAddressOf()))==E_NOINTERFACE,
            "an interface the selection does not answer was not left to Explorer");
    }
    std::cout<<"PASS: Explorer's readers and item menu receive exactly the selection, in every interface they ask for\n";
}

// The item menu's item source answers only for the tab whose menu the mod
// requested, only while its selection includes items of expanded folders,
// learns one view per request, and forgets a view once its folder changes.
static void TestInlineDirsLiveMenuViewOwner(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-bridge-owner");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha");
    live.SelectOnly({L"alpha\\a.txt"});
    alignas(16) static char viewA, viewB;
    t_inlineMenuViews.clear();
    Check(!InlineMenuOwner(&viewA),"a view was answered before any menu request");
    {
        const InlineMenuRequest previous=t_inlineMenuRequest;
        t_inlineMenuRequest=InlineMenuRequest{s.hwnd,s.lifetimeToken};
        Check(InlineMenuOwner(&viewA)==&s,"the requested view was not matched to its tab");
        Check(!InlineMenuOwner(&viewB),"a second view inside the same request was taken for the tab");
        t_inlineMenuRequest=previous;
    }
    Check(InlineMenuOwner(&viewA)==&s,"the view was forgotten when the request ended (the menu's commands run later)");
    live.SelectOnly({L"zeta.txt"});
    Check(!InlineMenuOwner(&viewA),"Explorer's own selection was replaced although it holds no expanded-folder items");
    live.SelectOnly({L"alpha\\a.txt",L"zeta.txt"});
    Check(InlineMenuOwner(&viewA)==&s,"a mixed selection was not answered");
    // A request for a closed tab learns nothing.
    {
        const InlineMenuRequest previous=t_inlineMenuRequest;
        t_inlineMenuRequest=InlineMenuRequest{s.hwnd,s.lifetimeToken+1};
        Check(!InlineMenuOwner(&viewB),"a request of a replaced tab was matched");
        t_inlineMenuRequest=previous;
    }
    // Another folder: the old view is never answered again.
    live.host.Navigate(live.Path(L"gamma"),2);
    Check(!InlineMenuOwner(&viewA),"a view of a folder the tab left was still answered");
    Check(std::none_of(t_inlineMenuViews.begin(),t_inlineMenuViews.end(),[&](const auto& view) {return view.defView==&viewA;}),
        "the left folder's view stayed recorded");
    std::cout<<"PASS: the item source answers only the requested tab's view, while needed, and forgets it on navigation\n";
}

// Explorer's command bar and status bar re-read a selection only on its
// SelectionChanged notice: a selection that starts or stops including items
// of expanded folders, or loses them to a collapse, is announced once.
static void TestInlineDirsLiveSelectionNotice(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-bridge-notice");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub");
    live.SelectOnly({L"alpha\\sub\\deep.txt"});
    SyncNativeSelection(s);
    live.Settled([&] {return s.inlineSelectionWasDetached && !s.inlineSelectionNoticeQueued;},
        "a selection inside an expanded folder was not announced");
    live.SelectOnly({L"zeta.txt"});
    SyncNativeSelection(s);
    live.Settled([&] {return !s.inlineSelectionWasDetached && !s.inlineSelectionNoticeQueued;},
        "returning to the open folder's own selection was not announced");
    live.SelectOnly({L"alpha\\sub\\deep.txt"});
    SyncNativeSelection(s);
    live.Settled([&] {return s.inlineSelectionWasDetached && !s.inlineSelectionNoticeQueued;},"re-selection");
    // A collapse hands the selection to the folder: announced too.
    live.Toggle(L"alpha");
    live.Settled([&] {return !s.inlineSelectionWasDetached && !s.inlineSelectionNoticeQueued;},
        "a collapse that removed the selected items was not announced");
    std::cout<<"PASS: selections entering or leaving expanded folders are announced to Explorer's readers\n";
}

// Deleting inside an expanded folder removes the row as soon as the Shell
// reports it, as for the open folder's own rows; a selection whose item is
// gone answers nothing (and is not parsed again on every poll) until the rows
// change.
static void TestInlineDirsLiveDeleteImmediate(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-bridge-delete");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma");
    // The selection's identities, then one item disappears unannounced.
    live.SelectOnly({L"alpha\\a.txt",L"alpha\\b.txt"});
    std::vector<OwnedPidl> items;
    Check(InlineSelectionPidls(s,items) && items.size()==2,"the selection has no identities");
    Check(DeleteFileW(live.Path(L"alpha\\b.txt").c_str()),"remove a selected item");
    s.inlineSelectionCache.items.clear(); // As after any change of the selection's rows.
    items.clear();
    Check(!InlineSelectionPidls(s,items) && s.inlineSelectionCache.failed,"a vanished item was handed to Explorer");
    Check(!InlineSelectionPidls(s,items),"the missing identity was answered on a later poll");
    // The Shell's report: the row leaves at once, before its folder's re-list.
    NoteInlineTreeChange(s,live.Path(L"alpha\\b.txt"),L"",true);
    Check(live.Row(L"alpha\\b.txt")<0,"the deleted row waited for its folder's re-list");
    Check(!s.inlineSelectionCache.failed,"the missing identity outlived the rows' change");
    Check(InlineSelectionPidls(s,items) && items.size()==1,"the remaining selection has no identity");
    // A rename report is not a removal: the row stays until its new name arrives.
    Check(MoveFileW(live.Path(L"gamma\\c.txt").c_str(),live.Path(L"gamma\\moved.txt").c_str()),"rename outside");
    NoteInlineTreeChange(s,live.Path(L"gamma\\c.txt"),live.Path(L"gamma\\moved.txt"),true);
    Check(live.Row(L"gamma\\c.txt")>=0,"a renamed row left before its new name arrived");
    live.Settled([&] {return live.Row(L"gamma\\moved.txt")>=0 && live.Row(L"gamma\\c.txt")<0;},
        "the renamed row did not take its new name");
    live.CheckConsistent("after deletions");
    std::cout<<"PASS: deleted items inside expanded folders leave at once; vanished identities are never handed out\n";
}

// Explorer asks the view to edit an item (SVSI_EDIT) both for the Shell's
// Rename of the selection and for an item it has just created (New folder,
// Ctrl+Shift+N). With inline children selected, only the former starts the
// mod's rename of the selection; a new item keeps Explorer's own route.
static void TestInlineDirsLiveRenameBridgeTarget(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-bridge-rename-target");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha");
    live.SelectOnly({L"alpha\\a.txt",L"zeta.txt"});
    // The overlay's own binding of the native view, as on attachment.
    BindNativeViewInput(s.nativeHwnd,s.hwnd);
    struct Unbind { HWND native,overlay; ~Unbind() { UnbindNativeViewInput(native,overlay); } } unbind{s.nativeHwnd,s.hwnd};
    Check(InlineSelectionViewOwner(s.folderView.Get(),nullptr)==&s,
        "PRECONDITION: the native view is answered for a selection with inline children");
    const auto child=[&](const std::wstring& folder,const wchar_t* name) {
        ComPtr<IShellFolder> shell;
        PIDLIST_ABSOLUTE absolute=nullptr;
        Check(SUCCEEDED(SHParseDisplayName(folder.c_str(),nullptr,&absolute,0,nullptr)) && absolute &&
            SUCCEEDED(SHBindToObject(nullptr,absolute,nullptr,IID_PPV_ARGS(&shell))),"bind a fixture folder");
        CoTaskMemFree(absolute);
        std::wstring text=name;
        PIDLIST_RELATIVE id=nullptr;
        Check(SUCCEEDED(shell->ParseDisplayName(nullptr,nullptr,text.data(),nullptr,&id,nullptr)) && id,"an item ID");
        return OwnedPidl(reinterpret_cast<PIDLIST_ABSOLUTE>(id));
    };
    const auto renameQueued=[&] {
        MSG message{};
        return PeekMessageW(&message,s.hwnd,kMsgInlineSelectionRename,kMsgInlineSelectionRename,PM_REMOVE)!=FALSE;
    };
    {
        const HANDLE file=CreateFileW(live.Path(L"created.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"create the new item");
        CloseHandle(file);
    }
    const auto created=child(root,L"created.txt");
    Check(!BridgeInlineRename(s.folderView.Get(),SVSI_EDIT|SVSI_DESELECTOTHERS,nullptr,
        reinterpret_cast<PCUITEMID_CHILD>(created.get())) && !renameQueued(),
        "editing a newly created item renamed the selection instead");
    // A selected child, named relative to its own folder, and a selected own row.
    const auto nested=child(live.Path(L"alpha"),L"a.txt");
    Check(BridgeInlineRename(s.folderView.Get(),SVSI_EDIT,nullptr,reinterpret_cast<PCUITEMID_CHILD>(nested.get())) &&
        renameQueued(),"the Shell's rename of a selected child did not start the selection's rename");
    const auto own=child(root,L"zeta.txt");
    Check(BridgeInlineRename(s.folderView.Get(),SVSI_EDIT,nullptr,reinterpret_cast<PCUITEMID_CHILD>(own.get())) &&
        renameQueued(),"the Shell's rename of a selected own row did not start the selection's rename");
    Check(!BridgeInlineRename(s.folderView.Get(),SVSI_SELECT,nullptr,reinterpret_cast<PCUITEMID_CHILD>(own.get())),
        "a plain selection request was taken for a rename");
    std::cout<<"PASS: Explorer's edit request starts the selection's rename only for a selected item\n";
}

// The Shell's folder refresh report (SHCNE_UPDATEDIR: F5 in other windows,
// coalesced bursts of changes) lists an expanded folder again, as the open
// folder re-lists on its own; folders not refreshed, or not expanded, are
// left alone.
static void TestInlineDirsLiveFolderRefresh(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-bridge-refresh");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma");
    // The refresh reports the mod received, printed only on failure.
    std::vector<std::wstring> treeLog;
    SelectionLogFixture::Session logSession(treeLog);
    const auto* previousPrefix=std::exchange(SelectionLogFixture::prefix,L"[GPU tree] Shell folder refresh");
    const bool previousPerf=g_perf.exchange(true);
    struct RestoreLog { const wchar_t* prefix; bool perf; ~RestoreLog() { SelectionLogFixture::prefix=prefix; g_perf=perf; } }
        restoreLog{previousPrefix,previousPerf};
    const auto report=[&](bool ok,const char* what) {
        if(!ok) {
            std::cout<<"REFRESH reports received ("<<treeLog.size()<<"):"<<std::endl;
            for(const auto& line:treeLog) std::cout<<"  "<<Regression::Narrow(line)<<std::endl;
        }
        Check(ok,what);
    };
    const auto request=[&](const wchar_t* relative) {
        const auto* node=FindInlineTreeNode(s,live.Path(relative));
        Check(node!=nullptr,"expanded folder node");
        return node->request;
    };
    // No change on disk: only the refresh report itself can start a listing.
    const auto alpha=request(L"alpha"), gamma=request(L"gamma");
    live.Flush({L"alpha",L"alpha\\sub"});
    live.Settle("the refreshed folder's listing did not finish");
    report(request(L"alpha")!=alpha,"a refresh report for an expanded folder did not list it again");
    Check(request(L"gamma")==gamma,"a refresh report for one folder listed another");
    Check(!FindInlineTreeNode(s,live.Path(L"alpha\\sub")),"a refresh report expanded a collapsed folder");
    // A change the refresh report stands for appears with it.
    {
        const HANDLE file=CreateFileW(live.Path(L"gamma\\fresh.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"create a file outside the view");
        CloseHandle(file);
    }
    live.Flush({L"gamma"});
    live.Settle("the refreshed folder's listing did not finish");
    report(live.Row(L"gamma\\fresh.txt")>=0,"the refreshed folder's new item did not appear");
    live.CheckConsistent("after folder refresh reports");
    std::cout<<"PASS: a folder refresh report lists that expanded folder again, and only it\n";
}
