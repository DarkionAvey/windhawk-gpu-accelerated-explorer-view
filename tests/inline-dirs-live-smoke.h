// Expandable folders against a genuine private ExplorerBrowser and the real
// Shell: native selection mirroring, Shell menus and data objects for
// selections that span folders, deletion, outside changes inside expanded
// folders, drops and spring-loaded folders, Back/Up restoration, the native
// view hand-off, the guide tooltip, and stress from rapid or careless use.
// Every file lives under the private fixture; nothing reaches the user's
// clipboard, desktop input, Recycle Bin or Explorer windows.
namespace InlineLive {
using InlineDirs::MakeFile;
using InlineDirs::MakeFolder;

struct Live {
    ProviderSelectionFixture::Browser host{true};
    ViewState& state=host.state;
    std::wstring root;
    InlineDirs::OptionScope option{true};
    Live(const std::wstring& folder,int rows) : root(folder) {
        if(!g_writeFactory)
            Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"live inline fonts");
        host.Navigate(root,rows);
        ApplyViewLayout(state,{FVM_DETAILS,16},false); UpdateMetrics(state); UpdateColumns(state);
        Check(EnsureTarget(state),"live inline target");
        Check(InlineTreeActive(state),"inline folders unavailable in the live Details view");
    }
    // The production overlay's own continuations reach OverlayProc, exactly
    // as they would on the overlay window; everything else is dispatched.
    void Pump() {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            const bool owned=message.hwnd==state.hwnd && (message.message==kMsgPreparedSelection ||
                message.message==kMsgFlushDirectoryChanges || message.message==kMsgNativeCatalog ||
                message.message==kMsgInlineTreeReady || message.message==kMsgAdoptNativeSelectAll ||
                message.message==kMsgInlineTreeReopen || message.message==kMsgInlineSelectionNotice ||
                message.message==kMsgInlineSelectionRename ||
                (message.message==WM_TIMER && message.wParam==kInlineTreeBusyTimerId));
            if(owned) OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    bool Idle() {
        bool queued=false;
        {
            ExclusiveSrwLock lock(state.background->directoryLock);
            queued=!state.background->treeJobs.empty() || !state.background->treeResults.empty();
        }
        if(queued) return false;
        // A job the worker is running is in neither queue: wait for its listing.
        for(const auto& node:state.inlineTree) if(!node.loaded || node.received!=node.request) return false;
        return state.loaded && !state.directoryPending && !state.watchedChangePending;
    }
    void Settle(const char* what) {
        Check(Eventually([&] { Pump(); ReceiveDirectoryResults(state); ReceiveInlineTreeResults(state); return Idle(); },30000),what);
        StopRowTransition(state);
    }
    // Completion, not time: deliver the Shell's change reports for these
    // folders now (SHCNF_FLUSH returns once they are delivered), let the mod
    // finish every job they start (Idle), then judge the outcome once. A wrong
    // outcome fails at once instead of waiting out a deadline.
    void Flush(std::initializer_list<std::wstring> relatives) {
        for(const auto& relative:relatives) {
            const auto folder=relative.empty() ? root : Path(relative);
            SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_PATHW|SHCNF_FLUSH,folder.c_str(),nullptr);
        }
    }
    template<class Done> void Settled(Done done,const char* what) {
        Settle(what);
        Check(done(),what);
    }
    // Only for outcomes with no completion signal the mod can observe (none
    // in these tests); prefer Flush + Settled.
    template<class Done> void Until(Done done,const char* what) {
        Check(Eventually([&] { Pump(); ReceiveDirectoryResults(state); ReceiveInlineTreeResults(state); return done(); },30000),what);
        StopRowTransition(state);
    }
    std::wstring Path(const std::wstring& relative) const { return JoinPath(root,relative); }
    int Row(const std::wstring& relative) const {
        const auto path=Path(relative);
        for(size_t i=0;i<state.entries.size();++i)
            if(!_wcsicmp(state.entries[i].path.c_str(),path.c_str())) return int(i);
        return -1;
    }
    void Toggle(const std::wstring& relative) {
        const int row=Row(relative);
        if(row<0) {
            std::cout<<"LIVE rows for missing "<<std::string(relative.begin(),relative.end())<<":"<<std::endl;
            for(const auto& e:state.entries) std::cout<<"  "<<e.treeDepth<<" "<<std::string(e.path.begin(),e.path.end())<<std::endl;
        }
        Check(row>=0,"live inline row missing");
        ToggleInlineFolder(state,row);
    }
    void Open(const std::wstring& relative) {
        if(FindInlineTreeNode(state,Path(relative))) return;
        Toggle(relative);
        Settle("expanded folder did not list");
    }
    void SelectOnly(std::initializer_list<const wchar_t*> items) {
        bool first=true;
        for(const auto* item:items) {
            const int row=Row(item);
            Check(row>=0,"selection row missing");
            SelectWithModifiers(state,row,!first,false);
            first=false;
        }
    }
    // The model is internally consistent: unique rows, lookups and flags
    // sized to the presented rows, depth only under an expanded folder.
    void CheckConsistent(const char* what) {
        Regression::Scope scope(what);
        Check(state.total==int(state.entries.size()) && state.selectedFlags.size()==state.entries.size(),
            "presented rows, total and selection flags disagree");
        int roots=0;
        std::unordered_set<std::wstring> seen;
        for(size_t i=0;i<state.entries.size();++i) {
            const auto& e=state.entries[i];
            std::wstring lower=e.path; CharLowerBuffW(lower.data(),DWORD(lower.size()));
            Check(seen.insert(lower).second,"a row is presented twice");
            if(!e.treeDepth) { ++roots; continue; }
            Check(i>0,"a child row has no folder above it");
            const auto& above=state.entries[i-1];
            Check(e.treeDepth<=above.treeDepth+1,"a child skipped a level");
            const size_t slash=e.path.find_last_of(L'\\');
            const auto parent=e.path.substr(0,slash);
            Check(FindInlineTreeNode(state,parent)!=nullptr,"a child row has no expanded folder");
        }
        Check(roots==state.rootTotal,"rootTotal does not count the folder's own rows");
        if(state.selected>=0) Check(state.selected<state.total,"focus outside the rows");
    }
    // Explorer's own Ctrl+A accelerator reads the physical Ctrl key, which a
    // parallel test may not press. Select every item through the real native
    // view instead, then continue exactly as SelectAllKeyboardItems does.
    void NativeSelectAll() {
        BeginNativeSelectAll(state);
        int count=0;
        Check(SUCCEEDED(state.folderView->ItemCount(SVGIO_ALLVIEW,&count)),"native item count");
        for(int i=0;i<count;++i) Check(SUCCEEDED(state.folderView->SelectItem(i,SVSI_SELECT)),"native select");
        PostMessageW(state.hwnd,kMsgAdoptNativeSelectAll,reinterpret_cast<WPARAM>(state.nativeHwnd),
            static_cast<LPARAM>(state.token));
    }
    int NativeSelected() {
        int count=-1;
        return state.folderView && SUCCEEDED(state.folderView->ItemCount(SVGIO_SELECTION,&count)) ? count : -1;
    }
};

inline void Notify(LONG event,const std::wstring& path,const std::wstring& second={}) {
    SHChangeNotify(event,SHCNF_PATHW|SHCNF_FLUSH,path.c_str(),second.empty() ? nullptr : second.c_str());
}

inline std::vector<std::wstring> DropPaths(IDataObject* objects) {
    std::vector<std::wstring> paths;
    FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if(FAILED(objects->GetData(&format,&medium))) return paths;
    const auto drop=static_cast<HDROP>(GlobalLock(medium.hGlobal));
    const UINT count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
    for(UINT i=0;i<count;++i) {
        std::wstring path(DragQueryFileW(drop,i,nullptr,0)+1,L'\0');
        path.resize(DragQueryFileW(drop,i,path.data(),UINT(path.size())));
        paths.push_back(path);
    }
    GlobalUnlock(medium.hGlobal); ReleaseStgMedium(&medium);
    return paths;
}

inline std::vector<std::wstring> MenuVerbs(IContextMenu* menu) {
    std::vector<std::wstring> verbs;
    HMENU popup=CreatePopupMenu();
    if(!popup) return verbs;
    if(SUCCEEDED(menu->QueryContextMenu(popup,0,1,0x7FFF,CMF_NORMAL))) {
        for(UINT id=1;id<0x7FFF;++id) {
            wchar_t verb[80]{};
            if(SUCCEEDED(menu->GetCommandString(id-1,GCS_VERBW,nullptr,reinterpret_cast<LPSTR>(verb),ARRAYSIZE(verb))) && *verb)
                verbs.push_back(verb);
            if(GetMenuState(popup,id,MF_BYCOMMAND)==UINT(-1) && id>256) break;
        }
    }
    DestroyMenu(popup);
    return verbs;
}
inline bool HasVerb(const std::vector<std::wstring>& verbs,const wchar_t* verb) {
    return std::any_of(verbs.begin(),verbs.end(),[&](const auto& v) {return !_wcsicmp(v.c_str(),verb);});
}

// root\alpha\{a.txt,b.txt,sub\{deep.txt}}, root\gamma\{c.txt,inner\{i.txt}},
// root\beta\ (empty), root\zeta.txt
inline std::wstring MakeTree(const std::wstring& fixture,const wchar_t* name) {
    const auto root=InlineDirs::MakeTree(fixture,name);
    const auto gamma=JoinPath(root,L"gamma");
    MakeFolder(gamma); MakeFile(JoinPath(gamma,L"c.txt"));
    MakeFolder(JoinPath(gamma,L"inner")); MakeFile(JoinPath(JoinPath(gamma,L"inner"),L"i.txt"));
    return root;
}

} // namespace InlineLive

// Children are GPU-only: the native view never receives a child, a child-only
// selection leaves it empty, collapsing hands the selection to the folder (and
// to the native view), and native Select All adopts the shown children too.
static void TestInlineDirsLiveNativeSelection(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-selection");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub"); live.Open(L"gamma");
    live.CheckConsistent("expanded");
    live.SelectOnly({L"alpha\\a.txt",L"zeta.txt",L"gamma\\c.txt"});
    SyncNativeSelection(s);
    live.Until([&] {return live.NativeSelected()==1;},"native view did not hold the open folder's own selected row");
    Check(InlineSelectionDetached(s),"a selection with children was not treated as spanning folders");
    live.SelectOnly({L"alpha\\sub\\deep.txt"});
    SyncNativeSelection(s);
    live.Until([&] {return live.NativeSelected()==0;},"a child-only selection leaked into the native view");
    // Collapsing the selected child's grandparent hands focus to it.
    live.Toggle(L"alpha"); live.Settle("collapse");
    Check(s.selected==live.Row(L"alpha") && s.selectedFlags[size_t(s.selected)],"focus did not move to the collapsed folder");
    live.Until([&] {return live.NativeSelected()==1;},"the collapsed folder's selection did not reach the native view");
    live.CheckConsistent("collapsed");
    // Native Select All (Explorer's own Ctrl+A) selects every shown row.
    live.Open(L"alpha");
    live.NativeSelectAll();
    live.Until([&] {return s.selectedFlags.Count(0,s.selectedFlags.size())==s.entries.size();},
        "native Select All did not select the shown children");
    live.Until([&] {return live.NativeSelected()==s.rootTotal;},"native Select All count differs from the folder's own rows");
    live.Settle("select all echo");
    Check(s.selectedFlags.Count(0,s.selectedFlags.size())==s.entries.size(),"the native Select All echo undid the shown children");
    std::cout<<"PASS: live native selection holds only the folder's own rows; child selections stay GPU-only; "
        "collapse hands selection over; Select All includes shown children\n";
}

// The Shell objects behind a selection that includes children: one folder's
// own menu when all items share it, the desktop's default menu for items
// from several folders, and a data object carrying exactly those files.
static void TestInlineDirsLiveShellObjects(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-objects");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma"); live.Open(L"gamma\\inner");
    for(const auto& selection:std::vector<std::vector<const wchar_t*>>{
            {L"alpha\\a.txt",L"alpha\\b.txt"},
            {L"alpha\\a.txt",L"gamma\\inner\\i.txt",L"zeta.txt"},
            {L"alpha\\sub",L"gamma\\c.txt"}}) {
        std::string label; for(const auto* item:selection) { label+=std::string(item,item+wcslen(item))+" "; }
        Regression::Scope scope("selection "+label);
        bool first=true;
        for(const auto* item:selection) { SelectWithModifiers(s,live.Row(item),!first,false); first=false; }
        ComPtr<IContextMenu> menu;
        Check(InlineSelectionMenu(s,menu),"no Shell menu for the selection");
        const auto verbs=MenuVerbs(menu.Get());
        for(const auto* verb:{L"delete",L"copy",L"cut",L"properties"})
            Check(HasVerb(verbs,verb),"the selection's menu lacks a standard file command");
        int count=0;
        const auto objects=InlineSelectionDataObject(s,&count);
        Check(objects && count==int(selection.size()),"no data object for the selection");
        auto paths=DropPaths(objects.Get());
        std::vector<std::wstring> expected;
        for(const auto* item:selection) expected.push_back(live.Path(item));
        const auto lower=[](std::vector<std::wstring> v) {
            for(auto& p:v) CharLowerBuffW(p.data(),DWORD(p.size()));
            std::sort(v.begin(),v.end()); return v;
        };
        Check(lower(paths)==lower(expected),"the data object does not carry exactly the selected files");
    }
    std::cout<<"PASS: shared-folder and cross-folder selections get full Shell menus and exact data objects\n";
}

// Deleting a selection that spans folders, including a whole expanded folder
// with an expanded folder inside it, removes exactly those files and their
// rows, and leaves no orphan tree state behind.
static void TestInlineDirsLiveDelete(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-delete");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub"); live.Open(L"gamma"); live.Open(L"gamma\\inner");
    live.SelectOnly({L"alpha\\a.txt",L"alpha\\sub\\deep.txt",L"gamma",L"zeta.txt"});
    // Permanent deletion without UI: private fixture files only.
    Check(InvokeInlineSelectionCommand(s,"delete",CMIC_MASK_FLAG_NO_UI|CMIC_MASK_SHIFT_DOWN),"Shell delete was not invoked");
    Check(Eventually([&] {live.Pump(); return !std::filesystem::exists(live.Path(L"alpha\\a.txt")) &&
        !std::filesystem::exists(live.Path(L"alpha\\sub\\deep.txt")) && !std::filesystem::exists(live.Path(L"gamma")) &&
        !std::filesystem::exists(live.Path(L"zeta.txt"));},30000),"the selected files were not deleted");
    Check(std::filesystem::exists(live.Path(L"alpha\\b.txt")),"an unselected file was deleted");
    live.Until([&] {return live.Row(L"alpha\\a.txt")<0 && live.Row(L"alpha\\sub\\deep.txt")<0 &&
        live.Row(L"gamma")<0 && live.Row(L"gamma\\c.txt")<0 && live.Row(L"zeta.txt")<0 && live.Idle();},
        "deleted items kept their rows");
    Check(!FindInlineTreeNode(s,live.Path(L"gamma")) && !FindInlineTreeNode(s,live.Path(L"gamma\\inner")),
        "a deleted expanded folder left its tree state behind");
    Check(live.Row(L"alpha\\b.txt")>=0 && FindInlineTreeNode(s,live.Path(L"alpha\\sub")),"surviving expanded folders collapsed");
    live.CheckConsistent("after delete");
    std::cout<<"PASS: deleting a cross-folder selection removes exactly those files, their rows and their tree state\n";
}

// Changes made by other programs inside expanded folders: created, deleted
// and renamed children, and expanded folders renamed or removed from under
// the view, including bursts.
static void TestInlineDirsLiveOutsideChanges(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-outside");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub"); live.Open(L"gamma"); live.Open(L"gamma\\inner");
    // A burst of creations in a nested folder.
    for(int i=0;i<20;++i) {
        const auto path=live.Path(L"alpha\\sub\\new"+std::to_wstring(i)+L".txt");
        MakeFile(path); Notify(SHCNE_CREATE,path);
    }
    live.Until([&] {return live.Row(L"alpha\\sub\\new19.txt")>=0 && live.Row(L"alpha\\sub\\new0.txt")>=0 && live.Idle();},
        "files created inside an expanded folder did not appear");
    live.CheckConsistent("after creations");
    // Delete and rename children.
    Check(DeleteFileW(live.Path(L"alpha\\b.txt").c_str()),"outside delete");
    Notify(SHCNE_DELETE,live.Path(L"alpha\\b.txt"));
    Check(MoveFileW(live.Path(L"gamma\\c.txt").c_str(),live.Path(L"gamma\\renamed.txt").c_str()),"outside rename");
    Notify(SHCNE_RENAMEITEM,live.Path(L"gamma\\c.txt"),live.Path(L"gamma\\renamed.txt"));
    live.Until([&] {return live.Row(L"alpha\\b.txt")<0 && live.Row(L"gamma\\c.txt")<0 &&
        live.Row(L"gamma\\renamed.txt")>=0 && live.Idle();},"outside deletes/renames inside expanded folders were missed");
    // Rename an expanded folder away: its tree state goes; the row follows.
    Check(MoveFileW(live.Path(L"alpha\\sub").c_str(),live.Path(L"alpha\\moved").c_str()),"rename expanded folder");
    Notify(SHCNE_RENAMEFOLDER,live.Path(L"alpha\\sub"),live.Path(L"alpha\\moved"));
    live.Until([&] {return live.Row(L"alpha\\sub")<0 && live.Row(L"alpha\\moved")>=0 &&
        live.Row(L"alpha\\sub\\deep.txt")<0 && live.Idle();},"a renamed expanded folder kept its old rows");
    Check(!FindInlineTreeNode(s,live.Path(L"alpha\\sub")),"a renamed expanded folder kept stale tree state");
    // Remove an expanded folder tree from under the view.
    std::filesystem::remove_all(live.Path(L"gamma"));
    Notify(SHCNE_RMDIR,live.Path(L"gamma"));
    live.Until([&] {return live.Row(L"gamma")<0 && live.Row(L"gamma\\inner\\i.txt")<0 && live.Idle();},
        "a removed expanded folder kept its rows");
    Check(!FindInlineTreeNode(s,live.Path(L"gamma\\inner")),"a removed folder's expanded child kept tree state");
    live.CheckConsistent("after outside changes");
    std::cout<<"PASS: outside creations, deletions and renames inside expanded folders, and removal of expanded folders, "
        "keep the tree current\n";
}

// Dropping onto a child folder stores the file in that folder; holding a drag
// over a closed folder opens it (spring-loaded) so a deeper folder becomes a
// target; rows that move during the drag never retarget the drop.
static void TestInlineDirsLiveDrop(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-drop");
    const auto outside=JoinPath(fixture,L"live-drop-source");
    MakeFolder(outside);
    const auto payload=JoinPath(outside,L"payload.txt");
    MakeFile(payload);
    Live live(root,4);
    auto& s=live.state;
    InitializeShellDragDrop(s);
    Check(s.shellDropBridge!=nullptr,"drop target unavailable");
    live.Open(L"alpha");
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(payload.c_str(),nullptr,IID_PPV_ARGS(&item))),"payload item");
    ComPtr<IDataObject> objects;
    Check(SUCCEEDED(item->BindToHandler(nullptr,BHID_DataObject,IID_PPV_ARGS(&objects))),"payload data object");
    const auto over=[&](const std::wstring& relative) {
        const int row=live.Row(relative);
        Check(row>=0,"drop row missing");
        const auto item=LayoutItem(s,LayoutRowVisual(s,row,1.f));
        POINT point{LONG((item.text.left+item.text.right)*.5f),LONG((item.text.top+item.text.bottom)*.5f)};
        if(s.rtlLayout) point.x=LONG(s.paintWidth)-point.x;
        ClientToScreen(s.hwnd,&point);
        return POINTL{point.x,point.y};
    };
    auto& target=*s.shellDropBridge.Get();
    // Spring-loading: hold over the closed "gamma" until it opens.
    DWORD effect=DROPEFFECT_COPY;
    Check(SUCCEEDED(target.DragEnter(objects.Get(),0,over(L"gamma"),&effect)),"drag enter");
    live.Until([&] {
        DWORD hold=DROPEFFECT_COPY;
        target.DragOver(0,over(L"gamma"),&hold);
        return FindInlineTreeNode(s,live.Path(L"gamma"))!=nullptr && live.Idle();
    },"holding a drag over a closed folder did not open it");
    // A deeper folder that only exists in the opened listing takes the drop.
    effect=DROPEFFECT_COPY;
    target.DragOver(0,over(L"gamma\\inner"),&effect);
    effect=DROPEFFECT_COPY;
    Check(SUCCEEDED(target.Drop(objects.Get(),0,over(L"gamma\\inner"),&effect)),"drop");
    Check(Eventually([&] {live.Pump(); return std::filesystem::exists(live.Path(L"gamma\\inner\\payload.txt"));},30000),
        "dropping on a child folder did not store the file there");
    Check(!std::filesystem::exists(live.Path(L"payload.txt")) && !std::filesystem::exists(live.Path(L"gamma\\payload.txt")),
        "the drop landed in the wrong folder");
    // A drop onto a child file goes to that file's folder.
    effect=DROPEFFECT_COPY;
    Check(SUCCEEDED(target.DragEnter(objects.Get(),0,over(L"alpha\\a.txt"),&effect)),"second drag enter");
    effect=DROPEFFECT_COPY;
    Check(SUCCEEDED(target.Drop(objects.Get(),0,over(L"alpha\\a.txt"),&effect)),"drop on child file");
    Check(Eventually([&] {live.Pump(); return std::filesystem::exists(live.Path(L"alpha\\payload.txt"));},30000),
        "dropping on a child file did not store the file in its folder");
    Check(std::filesystem::exists(payload),"a copy removed its source");
    live.Settle("drop settle");
    live.CheckConsistent("after drops");
    std::cout<<"PASS: drops onto child folders and child files land in their own folders; holding a drag opens a folder\n";
}

// Back/Up into a folder left with expanded folders reopens them and selects
// the folder the user came from, at once from the cache; repeated
// round trips, collapsing everything and turning the option off all hold.
static void TestInlineDirsLiveReturn(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-return");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub");
    for(int trip=0;trip<4;++trip) {
        Regression::Scope scope("round trip "+std::to_string(trip));
        live.host.returning=false;
        try { live.host.Navigate(live.Path(L"alpha\\sub"),1); }
        catch(...) {
            const auto narrow=[](const std::wstring& w){return std::string(w.begin(),w.end());};
            std::cout<<"RETURN folder="<<narrow(s.folderPath)<<" total="<<s.total<<" root="<<s.rootTotal
                <<" loaded="<<s.loaded<<" dir="<<s.directoryPending<<" watch="<<bool(s.directoryWatch)
                <<" changed="<<s.watchedChangePending<<" ns="<<s.namespaceCheckPending<<" tree="<<s.inlineTree.size()<<std::endl;
            for(const auto& e:s.entries) std::cout<<"  "<<e.treeDepth<<" "<<narrow(e.path)<<std::endl;
            throw;
        }
        Check(s.inlineTree.empty() && s.total==1,"the child folder inherited the parent's tree");
        live.host.returning=true;
        live.host.Navigate(root,4);
        live.host.returning=false;
        live.Settle("return listing");
        Check(FindInlineTreeNode(s,live.Path(L"alpha")) && FindInlineTreeNode(s,live.Path(L"alpha\\sub")),
            "expanded folders were not reopened on return");
        Check(s.selected==live.Row(L"alpha\\sub") && s.selectedFlags.Count(0,s.selectedFlags.size())==1,
            "the folder the user came from was not selected");
        live.CheckConsistent("after return");
    }
    // Collapsing everything is remembered too.
    live.Toggle(L"alpha"); live.Settle("collapse all");
    live.host.Navigate(live.Path(L"beta"),0);
    live.host.returning=true; live.host.Navigate(root,4); live.host.returning=false;
    live.Settle("return after collapse");
    Check(s.inlineTree.empty() && s.total==4,"collapsed folders reopened on return");
    // Turning the option off forgets expansion.
    live.Open(L"alpha");
    live.host.Navigate(live.Path(L"beta"),0);
    g_expandableFolders=false;
    SendMessageW(s.hwnd,WM_NULL,0,0);
    OverlayProc(s.hwnd,kMsgSettings,0,0);
    g_expandableFolders=true;
    live.host.Navigate(root,4);
    live.Settle("return after option off");
    Check(s.inlineTree.empty() && s.total==4,"expansion came back after the option was turned off");
    std::cout<<"PASS: Back/Up reopens expanded folders and selects the origin across round trips; collapse and "
        "the option switch are honored\n";
}

// Switching to the native view with items selected inside one expanded folder
// opens that folder natively and selects them; mixed origins stay put.
static void TestInlineDirsLiveNativeHandoff(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-handoff");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma");
    // Mixed origins: no hand-off.
    live.SelectOnly({L"alpha\\a.txt",L"gamma\\c.txt"});
    Check(!PrepareNativeInlineReveal(s) && !s.nativeRevealFolder,"a mixed-origin selection prepared a hand-off");
    live.SelectOnly({L"alpha\\a.txt",L"zeta.txt"});
    Check(!PrepareNativeInlineReveal(s),"a selection with a root row prepared a hand-off");
    // One folder: hand off.
    live.SelectOnly({L"alpha\\a.txt",L"alpha\\b.txt"});
    Check(PrepareNativeInlineReveal(s),"a single-folder child selection did not prepare a hand-off");
    s.visible=false; // As SetActive does when switching to the native view.
    live.host.sink->navigationComplete=false;
    BrowseNativeToInlineSelection(s);
    // The navigation signal handler applies the selection once the native
    // view shows the folder (retrying while it is still enumerating).
    Check(Eventually([&] {
        live.Pump();
        if(live.host.sink->navigationComplete) ApplyNativeInlineReveal(s);
        return !s.nativeRevealFolder;
    },30000),"the native view never showed the hand-off selection");
    ComPtr<IShellView> view; ComPtr<IFolderView2> folderView;
    Check(SUCCEEDED(live.host.browser->GetCurrentView(IID_PPV_ARGS(&view))) && SUCCEEDED(view.As(&folderView)),"native view");
    std::wstring location; ComPtr<IShellFolder> folder;
    Check(GetFolderPath(folderView.Get(),location,folder) && !_wcsicmp(location.c_str(),live.Path(L"alpha").c_str()),
        "the native view did not open the items' folder");
    ComPtr<IShellItemArray> selected;
    DWORD count=0;
    Check(SUCCEEDED(folderView->GetSelection(FALSE,&selected)) && selected && SUCCEEDED(selected->GetCount(&count)) && count==2,
        "the native view did not select the handed-off items");
    s.visible=true;
    std::cout<<"PASS: native-view hand-off opens the single shared folder and selects the items; mixed origins stay\n";
}

// The guide tooltip names the guide's folder relative to the open folder,
// with the leading path muted; moving off the guide or collapsing hides it.
static void TestInlineDirsLiveGuideTip(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-tip");
    Live live(root,4);
    auto& s=live.state;
    RegisterItemTipClass(OverlayModuleInstance());
    live.Open(L"alpha"); live.Open(L"alpha\\sub");
    s.clickPolicy.itemTips=true; s.pointerInside=true; s.pointingDevice=PointingDevice::Mouse;
    const int row=live.Row(L"alpha\\sub\\deep.txt");
    const auto item=LayoutItem(s,LayoutRowVisual(s,row,1.f));
    const float level=InlineTreeLevelWidth(s);
    const float base=item.icon.left-3*level;
    const auto guide=[&](int ancestor) {
        const float x=InlineTreeChevronCenter(s,base+(ancestor+1)*level);
        return POINT{LONG(s.rtlLayout ? s.paintWidth-x : x),LONG((item.cell.top+item.cell.bottom)*.5f)};
    };
    s.hovered=row;
    const POINT inner=guide(1);
    Check(InlineTreeBranchAt(s,inner)==live.Row(L"alpha\\sub"),"the inner guide does not belong to sub");
    HandlePointerHover(s,inner,0);
    Check(s.itemTip.item.ends_with(live.Path(L"alpha\\sub")),"no tooltip for the guide");
    Check(s.itemTip.name==L"alpha\\sub" && s.itemTip.mutedPrefix==6,"the guide tooltip is not the relative path with a muted prefix");
    // The outer guide names a first-level folder with nothing muted.
    UpdateInlineTreeBranchHover(s,guide(0));
    HandlePointerHover(s,guide(0),0);
    Check(s.itemTip.name==L"alpha" && s.itemTip.mutedPrefix==0,"the first-level guide tooltip");
    // Collapsing the folder under the tooltip hides it.
    live.Toggle(L"alpha"); live.Settle("collapse under tip");
    Check(s.itemTip.item.empty(),"the guide tooltip outlived its rows");
    HideItemTip(s); s.itemTip.Release();
    std::cout<<"PASS: guide tooltips show the relative folder path with a muted prefix and hide with their rows\n";
}

// Careless and power use: hammering a toggle while a large folder lists,
// refreshing in the middle, deep nesting, Ctrl+A then collapse, and toggling
// a folder whose listing was cancelled. The tree must end consistent.
static void TestInlineDirsLiveStress(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-stress");
    const auto big=JoinPath(root,L"big");
    MakeFolder(big);
    constexpr int kBig=1500;
    for(int i=0;i<kBig;++i) {
        wchar_t name[32]; swprintf_s(name,L"f%05d.txt",i);
        MakeFile(JoinPath(big,name));
    }
    std::wstring chain=JoinPath(root,L"deep");
    MakeFolder(chain);
    constexpr int kDepth=24;
    for(int i=0;i<kDepth;++i) { chain=JoinPath(chain,L"d"+std::to_wstring(i)); MakeFolder(chain); }
    MakeFile(JoinPath(chain,L"bottom.txt"));
    Live live(root,6);
    auto& s=live.state;
    // 31 toggles without waiting, with a refresh of the open folder midway.
    for(int i=0;i<31;++i) {
        live.Toggle(L"big");
        if(i==15) QueueDirectoryRefresh(s,true);
        if(i%5==0) live.Pump();
    }
    live.Settle("hammered toggle");
    Check(FindInlineTreeNode(s,big)!=nullptr,"an odd number of toggles did not leave the folder open");
    const int first=live.Row(L"big")+1;
    Check(s.total==6+kBig,"the large folder's rows are missing or duplicated");
    for(int i=0;i<kBig;++i) {
        wchar_t name[32]; swprintf_s(name,L"f%05d.txt",i);
        Check(s.entries[size_t(first+i)].name==name,"the large folder's rows are out of order");
    }
    live.CheckConsistent("after hammering");
    // Ctrl+A, then collapse: children leave the selection, the rest stays.
    live.NativeSelectAll();
    live.Until([&] {return s.selectedFlags.Count(0,s.selectedFlags.size())==s.entries.size();},"select all");
    live.Toggle(L"big"); live.Settle("collapse after select all");
    Check(s.total==6 && s.selectedFlags.Count(0,s.selectedFlags.size())==6,"collapse broke the remaining selection");
    live.CheckConsistent("after select all and collapse");
    // Deep nesting, opened level by level with Right.
    Select(s,live.Row(L"deep"),true);
    std::wstring path=L"deep";
    for(int i=0;i<=kDepth;++i) {
        NavigateKeyboardSelection(s,VK_RIGHT,false,false); live.Settle("deep open");
        NavigateKeyboardSelection(s,VK_RIGHT,false,false);
    }
    Check(live.Row(chain.substr(root.size()+1)+L"\\bottom.txt")>=0,"the deepest file never appeared");
    Check(s.entries[size_t(live.Row(chain.substr(root.size()+1)+L"\\bottom.txt"))].treeDepth==kDepth+1,"deep row depth");
    live.CheckConsistent("deep");
    // Collapsing the top forgets every level.
    live.Toggle(L"deep"); live.Settle("collapse deep");
    Check(s.inlineTree.empty() && s.total==6,"collapsing the top level left descendants");
    // Expand and collapse before the listing arrives: the result is dropped.
    live.Toggle(L"big"); live.Toggle(L"big");
    live.Settle("cancelled listing");
    Check(s.total==6 && s.inlineTree.empty(),"a cancelled listing was grafted");
    std::cout<<"PASS: rapid toggles with a refresh, a 1500-item folder, 25 nested levels, select-all then collapse and "
        "cancelled listings leave a consistent tree\n";
}

