// 1:1 parity of items inside an expanded folder with the same items shown as
// the open folder's own rows: one private folder is presented twice, expanded
// inline in one real ExplorerBrowser and opened directly in another, and
// everything the list presents is compared item by item: membership (lock
// files, hidden items), order under each sort, names and Shell labels,
// attributes, sizes, dates and every Details column's text. Whatever the
// open folder shows, the expanded folder must show identically.
namespace InlineLive {
// alpha holds files of every kind the list distinguishes.
inline std::wstring MakeParityTree(const std::wstring& fixture,const wchar_t* name) {
    const auto root=MakeTree(fixture,name);
    const auto alpha=JoinPath(root,L"alpha");
    const auto write=[&](const wchar_t* file,size_t bytes,DWORD attributes=FILE_ATTRIBUTE_NORMAL) {
        const auto path=JoinPath(alpha,file);
        HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,attributes,nullptr);
        Check(h!=INVALID_HANDLE_VALUE,"parity fixture file");
        std::string data(bytes,'x');
        DWORD written=0;
        Check(!bytes || WriteFile(h,data.data(),DWORD(bytes),&written,nullptr),"parity fixture content");
        CloseHandle(h);
    };
    write(L"big.bin",3*1024*1024+7);
    write(L"medium.log",40*1024);
    write(L"report.docx",12);
    write(L"~$report.docx",162,FILE_ATTRIBUTE_HIDDEN); // Office's lock file.
    write(L"Ünïcode ñame.txt",3);
    write(L"noext",5);
    write(L"UPPER.TXT",7);
    write(L"hidden.txt",9,FILE_ATTRIBUTE_HIDDEN);
    MakeFolder(JoinPath(alpha,L"child"));
    // A shortcut: the Shell gives it the link overlay.
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))) &&
        SUCCEEDED(link->SetPath(JoinPath(alpha,L"a.txt").c_str())) && SUCCEEDED(link.As(&file)) &&
        SUCCEEDED(file->Save(JoinPath(alpha,L"shortcut.lnk").c_str(),TRUE)),"parity fixture shortcut");
    // NTFS copies a folder's times into its parent's index (what listings
    // read) lazily after its contents change, so two listings moments apart
    // could disagree on alpha's subfolders. Setting each folder's own times
    // on a handle makes NTFS write that copy when the handle closes: both
    // views then list a settled fixture.
    for(const auto& item:std::filesystem::directory_iterator(alpha)) {
        if(!item.is_directory()) continue;
        HANDLE h=CreateFileW(item.path().c_str(),FILE_WRITE_ATTRIBUTES|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        Check(h!=INVALID_HANDLE_VALUE,"open a parity fixture folder");
        FILETIME created{},accessed{},written{};
        Check(GetFileTime(h,&created,&accessed,&written) && SetFileTime(h,&created,&accessed,&written),
            "settle a parity fixture folder's times");
        CloseHandle(h);
    }
    return root;
}
inline std::string Narrow(const std::wstring& text) {
    std::string out(WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),int(out.size()),nullptr,nullptr);
    return out;
}
// Details and icons delivered and presented, as on screen.
inline void Present(Live& live) {
    auto& s=live.state;
    // Explorer's own visible Details columns, as captured once a view has
    // finished starting (RefreshExplorerColumns).
    if(s.columns.size()<2) {
        s.featureInitializationStep=kOverlayFeatureSteps; s.deferColumnProviders=false;
        s.columnModeTransition=false; s.columnPresentationPending=false; s.nativeColumnsDirty=true;
        RefreshExplorerColumns(s,true);
        UpdateColumns(s);
        Check(s.columns.size()>=2,"PRECONDITION: Explorer's Details columns were not captured");
    }
    // Ends on completion: every row ready, or no detail work left in flight
    // (nothing pending, no result waiting) although a row is not ready, which
    // fails at once. The deadline only guards against a hung worker.
    bool done=false;
    Check(Eventually([&] {
        live.Pump(); ReceiveDirectoryResults(s); ReceiveInlineTreeResults(s);
        MSG message{};
        bool delivered=false;
        while(PeekMessageW(&message,s.hwnd,kMsgDetailsReady,kMsgDetailsReady,PM_REMOVE) ||
              PeekMessageW(&message,s.hwnd,kMsgIconReady,kMsgIconReady,PM_REMOVE)) {
            OverlayProc(s.hwnd,message.message,message.wParam,message.lParam);
            delivered=true;
        }
        QueueVisibleDetails(s);
        AdvanceAnimationFrame(s,true); Paint(s);
        if(!live.Idle()) return false;
        done=std::all_of(s.entries.begin(),s.entries.end(),[](const Entry& e) {return e.detailsReady;});
        const bool inFlight=delivered || s.detailResultsPending || s.iconResultsPending ||
            std::any_of(s.entries.begin(),s.entries.end(),[](const Entry& e) {return e.detailsPending;});
        return done || !inFlight;
    },30000),"the details worker never finished (hung)");
    if(!done) {
        RECT client{}; GetClientRect(s.hwnd,&client);
        std::cout<<"PARITY rows without details (view "<<live.root.size()<<" chars, client height "<<client.bottom
            <<", rowPx "<<s.rowPx<<", columns "<<s.columns.size()<<", columnsView "<<s.columnsView<<"):"<<std::endl;
        for(const auto& e:s.entries)
            if(!e.detailsReady) std::cout<<"  depth="<<e.treeDepth<<" pending="<<e.detailsPending<<" "<<Narrow(e.path)<<std::endl;
    }
    Check(done,"the rows' details were not presented");
    // Each row's Shell overlay, through the production request; complete when
    // the query record is no longer pending (only a hung lane waits).
    for(const auto& e:s.entries) {
        const auto key=OverlayQueryKey(e);
        Check(Eventually([&] {
            RequestEntryOverlay(s,e,{{key,0}}); ReceiveIcons(s,false);
            const auto* record=FindIconRecord(s,key);
            return record && !record->pending;
        },30000),"the overlay lane never answered (hung)");
    }
    StopRowTransition(s);
}
struct ParityRow { std::wstring leaf; const Entry* entry; };
inline std::vector<ParityRow> FolderRows(Live& live,const std::wstring& folder) {
    std::vector<ParityRow> rows;
    const auto prefix=folder+L"\\";
    for(const auto& e:live.state.entries) {
        if(_wcsnicmp(e.path.c_str(),prefix.c_str(),prefix.size())) continue;
        const auto leaf=e.path.substr(prefix.size());
        if(leaf.find(L'\\')==std::wstring::npos) rows.push_back({leaf,&e});
    }
    return rows;
}
// Every presented difference is printed, then the comparison fails once.
inline void CompareFolder(Live& nested,Live& open,const std::wstring& folder,const char* what) {
    Regression::Scope scope(what);
    const auto inside=FolderRows(nested,folder), own=FolderRows(open,folder);
    bool same=inside.size()==own.size();
    std::vector<std::string> differences;
    for(size_t i=0;i<std::max(inside.size(),own.size());++i) {
        const auto left=i<inside.size() ? inside[i].leaf : L"(none)";
        const auto right=i<own.size() ? own[i].leaf : L"(none)";
        if(_wcsicmp(left.c_str(),right.c_str())) {
            differences.push_back("order/membership #"+std::to_string(i)+": expanded="+Narrow(left)+" open="+Narrow(right));
            continue;
        }
        const Entry& a=*inside[i].entry; const Entry& b=*own[i].entry;
        const auto field=[&](const char* name,bool equal,const std::wstring& x,const std::wstring& y) {
            if(!equal) differences.push_back(Narrow(left)+" "+name+": expanded=["+Narrow(x)+"] open=["+Narrow(y)+"]");
        };
        field("name",a.name==b.name,a.name,b.name);
        field("label",a.label==b.label,a.label,b.label);
        field("folder",a.folder==b.folder,a.folder?L"1":L"0",b.folder?L"1":L"0");
        field("attributes",a.attributes==b.attributes,std::to_wstring(a.attributes),std::to_wstring(b.attributes));
        field("bytes",a.byteSize==b.byteSize,std::to_wstring(a.byteSize),std::to_wstring(b.byteSize));
        const auto stamp=[](const FILETIME& t) {return std::to_wstring((ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime);};
        WIN32_FILE_ATTRIBUTE_DATA now{};
        GetFileAttributesExW(b.path.c_str(),GetFileExInfoStandard,&now);
        field("modified",CompareFileTime(&a.lastWrite,&b.lastWrite)==0,stamp(a.lastWrite),
            stamp(b.lastWrite)+L" (on disk now "+stamp(now.ftLastWriteTime)+L")");
        field("type",a.type==b.type,a.type,b.type);
        field("size",a.size==b.size,a.size,b.size);
        field("date",a.date==b.date,a.date,b.date);
        // The Shell overlay each view resolved (-2: never asked).
        const auto overlay=[](Live& live,const Entry& e) {
            const auto* record=FindIconRecord(live.state,OverlayQueryKey(e));
            return record ? record->overlayImage : -2;
        };
        const int oa=overlay(nested,a), ob=overlay(open,b);
        field("overlay",oa==ob,std::to_wstring(oa),std::to_wstring(ob));
        for(const auto& cell:b.cells) {
            const auto match=std::find_if(a.cells.begin(),a.cells.end(),[&](const ColumnCell& c) {return SameProperty(c.key,cell.key);});
            wchar_t key[64]{}; StringFromGUID2(cell.key.fmtid,key,64);
            const std::wstring name=std::wstring(key)+L","+std::to_wstring(cell.key.pid);
            field("column",match!=a.cells.end() && match->text==cell.text,
                L"("+name+L") "+(match!=a.cells.end() ? match->text : L"missing"),cell.text);
        }
    }
    for(const auto& d:differences) std::cout<<"PARITY "<<what<<": "<<d<<std::endl;
    Check(same && differences.empty(),"items inside an expanded folder are presented differently from the open folder's own");
}
}

static void TestInlineDirsLiveParityPresentation(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeParityTree(fixture,L"live-parity");
    const auto alpha=JoinPath(root,L"alpha");
    Live nested(root,4);
    nested.Open(L"alpha");
    // The folder's own listing, as Explorer's settings show it: hidden items
    // only when shown, never Office's lock file.
    SHELLSTATEW shell{};
    SHGetSetSettings(&shell,SSF_SHOWALLOBJECTS,FALSE);
    int listed=0;
    for(const auto& item:std::filesystem::directory_iterator(alpha)) {
        const auto leaf=item.path().filename().wstring();
        const DWORD attributes=GetFileAttributesW(item.path().c_str());
        if(leaf.starts_with(L"~$") || ((attributes & FILE_ATTRIBUTE_HIDDEN) && !shell.fShowAllObjects)) continue;
        ++listed;
    }
    Live open(alpha,listed);
    open.Settle("the folder opened directly did not list");
    Present(nested); Present(open);
    CompareFolder(nested,open,alpha,"default order");
    {
        const int row=open.Row(L"shortcut.lnk");
        Check(row>=0,"PRECONDITION: the shortcut is listed");
        const auto* overlay=FindIconRecord(open.state,OverlayQueryKey(open.state.entries[size_t(row)]));
        Check(overlay && overlay->overlayImage>=0,"PRECONDITION: the shortcut's link overlay was resolved");
    }
    // The same sort chosen in each view (its Size column header).
    const auto sizeColumn=[](Live& live) {
        for(size_t i=0;i<live.state.columns.size();++i)
            if(SameProperty(live.state.columns[i].key,PKEY_Size)) return int(i);
        return -1;
    };
    Check(sizeColumn(nested)>=0 && sizeColumn(open)>=0,"PRECONDITION: the Details views show a Size column");
    // The comparison is not vacuous: the views show Details text for the items.
    const auto own=FolderRows(open,alpha);
    Check(own.size()>=10 && std::all_of(own.begin(),own.end(),[](const ParityRow& row) {
        return std::any_of(row.entry->cells.begin(),row.entry->cells.end(),[](const ColumnCell& c) {return !c.text.empty();});
    }),"PRECONDITION: the open folder's rows carry Details text");
    std::vector<std::wstring> orders[2];
    for(int pass=0;pass<2;++pass) {
        const SORTDIRECTION direction=pass ? SORT_DESCENDING : SORT_ASCENDING;
        for(Live* live:{&nested,&open}) {
            SortByHeader(live->state,sizeColumn(*live));
            // The native view applies the sort before SortByHeader returns;
            // the mod's re-sort completes with its jobs (Idle).
            live->Settled([&] {return live->state.sortInitialized && SameProperty(live->state.sort.key,PKEY_Size) &&
                live->state.sort.direction==direction;},"the Size sort was not applied");
        }
        Present(nested); Present(open);
        CompareFolder(nested,open,alpha,pass ? "size descending" : "size ascending");
        for(const auto& row:FolderRows(open,alpha)) orders[pass].push_back(row.leaf);
    }
    Check(orders[0]!=orders[1],"PRECONDITION: the two Size sorts did not change the order");
    std::cout<<"PASS: items inside an expanded folder are presented exactly as when the folder is opened, in every sort\n";
}

// Keyboard selection inside an expanded folder: the same keys from the same
// item select and focus the same items as inside the folder opened directly
// (arrows, Shift ranges, Ctrl focus moves and Ctrl+Space toggles).
static void TestInlineDirsLiveParityKeyboard(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeParityTree(fixture,L"live-parity-keys");
    const auto alpha=JoinPath(root,L"alpha");
    Live nested(root,4);
    nested.Open(L"alpha");
    SHELLSTATEW shell{};
    SHGetSetSettings(&shell,SSF_SHOWALLOBJECTS,FALSE);
    int listed=0;
    for(const auto& item:std::filesystem::directory_iterator(alpha)) {
        const auto leaf=item.path().filename().wstring();
        if(leaf.starts_with(L"~$") || ((GetFileAttributesW(item.path().c_str()) & FILE_ATTRIBUTE_HIDDEN) && !shell.fShowAllObjects)) continue;
        ++listed;
    }
    Live open(alpha,listed);
    open.Settle("the folder opened directly did not list");
    const auto state=[&](Live& live) {
        std::vector<std::wstring> selected;
        auto& s=live.state;
        for(size_t i=0;i<s.entries.size() && i<s.selectedFlags.size();++i)
            if(s.selectedFlags[i]) selected.push_back(PathFindFileNameW(s.entries[i].path.c_str()));
        const std::wstring focus=s.selected>=0 ? PathFindFileNameW(s.entries[size_t(s.selected)].path.c_str()) : L"";
        return std::make_pair(selected,focus);
    };
    // Start on the folder's third item in both views (away from its edges).
    const auto rows=FolderRows(open,alpha);
    Check(rows.size()>=8,"PRECONDITION: enough items to move within the folder");
    const auto start=rows[2].leaf;
    SelectWithModifiers(nested.state,nested.Row(L"alpha\\"+start),false,false);
    SelectWithModifiers(open.state,open.Row(start),false,false);
    struct Key { UINT key; bool control, shift; const char* name; };
    for(const Key& key:std::initializer_list<Key>{
            {VK_DOWN,false,false,"Down"},{VK_DOWN,false,true,"Shift+Down"},{VK_DOWN,false,true,"Shift+Down"},
            {VK_UP,false,true,"Shift+Up"},{VK_DOWN,true,false,"Ctrl+Down"},{VK_DOWN,true,false,"Ctrl+Down"},
            {VK_SPACE,true,false,"Ctrl+Space"},{VK_UP,false,false,"Up"},{VK_DOWN,false,true,"Shift+Down"}}) {
        Regression::Scope scope(key.name);
        NavigateKeyboardSelection(nested.state,key.key,key.control,key.shift);
        NavigateKeyboardSelection(open.state,key.key,key.control,key.shift);
        nested.Settle("key"); open.Settle("key");
        const auto a=state(nested), b=state(open);
        if(a!=b) {
            std::cout<<"PARITY keys after "<<key.name<<": expanded focus="<<Narrow(a.second)<<" open focus="<<Narrow(b.second)<<std::endl;
            for(const auto& x:a.first) std::cout<<"  expanded selected "<<Narrow(x)<<std::endl;
            for(const auto& x:b.first) std::cout<<"  open selected "<<Narrow(x)<<std::endl;
        }
        Check(a==b,"keyboard selection inside an expanded folder differs from the folder opened directly");
    }
    std::cout<<"PASS: keyboard selection inside an expanded folder matches the folder opened directly\n";
}

// Explorer's slow second click on a selected item's name starts renaming it:
// an item inside an expanded folder exactly as an item of the open folder.
static void TestInlineDirsLiveParityClickRename(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-click-rename");
    Live live(root,4);
    auto& s=live.state;
    // Explorer in front with the list focused, as for a real slow click.
    FocusFixture::Session focus(live.host.frame,s.hwnd);
    live.Open(L"alpha");
    for(const wchar_t* item:{L"zeta.txt",L"alpha\\a.txt"}) {
        Regression::Scope scope(Narrow(item));
        FocusFixture::focus=s.hwnd; // The clicks give the list keyboard focus.
        live.SelectOnly({item});
        ArmClickRename(s,live.Row(item));
        Check(s.clickRename.armed,"the slow second click was not armed");
        // The double-click time has passed (the wake is due now), without waiting it out.
        s.clickRename.due=GetTickCount64();
        live.Pump();
        FireClickRename(s);
        if(!s.rename.editor)
            std::cout<<"PARITY click-rename blocked: pendingNative="<<s.pendingNativeSelection<<" hold="<<s.nativeRenameHold
                <<" selected="<<s.selected<<" row="<<live.Row(item)<<" count="<<s.selectedFlags.Count(0,s.selectedFlags.size())
                <<" capture="<<(GetCapture()==s.hwnd)<<" marquee="<<s.marqueePossible
                <<" foreground="<<ExplorerFrameHasForeground(s.frameHwnd)<<" focus="<<OverlayHasInputFocus(s)
                <<" owns="<<TabOwnsActiveView(s)<<" details="<<s.detailsViewActive<<std::endl;
        if(!s.rename.editor) {
            BeginRename(s,live.Row(item));
            std::cout<<"PARITY direct BeginRename on the same row opens the field: "<<(s.rename.editor!=nullptr)<<std::endl;
            if(s.rename.editor) CancelRename(s);
        }
        Check(s.rename.editor!=nullptr,"the slow second click did not start renaming");
        Check(s.rename.path==live.Path(item),"the slow second click renamed another item");
        CancelRename(s);
    }
    std::cout<<"PASS: a slow second click renames an item inside an expanded folder as one of the open folder\n";
}

// Explorer's Copy, Cut and Paste with items inside expanded folders: copied or
// moved items land in the expanded folder chosen as the target, as when
// pasting onto a folder of the open folder. Uses the real clipboard (Serial).
static void TestInlineDirsLiveClipboardCommands(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-clipboard");
    // The Shell's Copy and Cut write the real clipboard inside shell32, where
    // the API guards can't see it: claimed here.
    Regression::Claim(Regression::Clipboard,"the Shell's Copy, Cut and Paste commands");
    // The Shell's clipboard commands need OLE on this thread, as on Explorer's.
    const HRESULT ole=OleInitialize(nullptr);
    Check(SUCCEEDED(ole),"OLE for the clipboard commands");
    struct Ole { ~Ole() { OleUninitialize(); } } oleScope;
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub"); live.Open(L"gamma"); live.Open(L"gamma\\inner");
    constexpr DWORD kQuiet=CMIC_MASK_FLAG_NO_UI;
    // The commands complete inside InvokeCommand (no UI, synchronous copy
    // engine): every outcome is judged right after it, never polled for.
    // A locked session denies clipboard access (ERROR_ACCESS_DENIED): every
    // Copy then leaves nothing. Name that precondition instead of a false GAP.
    // The harness substitutes OpenClipboard/CloseClipboard; this needs user32's own.
    {
        const HMODULE user32=GetModuleHandleW(L"user32.dll");
        const auto realOpen=reinterpret_cast<BOOL(WINAPI*)(HWND)>(GetProcAddress(user32,"OpenClipboard"));
        const auto realClose=reinterpret_cast<BOOL(WINAPI*)()>(GetProcAddress(user32,"CloseClipboard"));
        Check(realOpen && realClose,"user32's clipboard functions");
        const BOOL opened=realOpen(nullptr);
        const DWORD error=opened ? 0 : GetLastError();
        if(opened) realClose();
        if(!opened) std::cout<<"PARITY the real clipboard cannot be opened, error="<<error
            <<" (5: session locked or not interactive)"<<std::endl;
        Check(opened,"the real clipboard is unavailable to this session");
    }
    const auto clipboard=[&] {
        ComPtr<IDataObject> held;
        auto paths=SUCCEEDED(OleGetClipboard(&held)) && held ? DropPaths(held.Get()) : std::vector<std::wstring>{};
        for(auto& p:paths) CharLowerBuffW(p.data(),DWORD(p.size()));
        std::sort(paths.begin(),paths.end());
        return paths;
    };
    const auto lowered=[&](std::initializer_list<const wchar_t*> items) {
        std::vector<std::wstring> paths;
        for(const auto* item:items) { paths.push_back(live.Path(item)); CharLowerBuffW(paths.back().data(),DWORD(paths.back().size())); }
        std::sort(paths.begin(),paths.end());
        return paths;
    };
    const auto paste=[&](const wchar_t* target) {
        live.SelectOnly({target});
        if(InvokeInlineSelectionCommand(s,"paste",kQuiet)) return;
        // GAP report: Explorer's own menu object for the same target and verb.
        ComPtr<IContextMenu> menu;
        Check(InlineSelectionMenu(s,menu),"the target folder's menu");
        HMENU popup=CreatePopupMenu();
        menu->QueryContextMenu(popup,0,1,0x7FFF,CMF_NORMAL);
        CMINVOKECOMMANDINFO info{sizeof(info)};
        info.fMask=kQuiet; info.lpVerb="paste"; info.nShow=SW_SHOWNORMAL;
        const HRESULT hr=menu->InvokeCommand(&info);
        DestroyMenu(popup);
        std::cout<<"PARITY paste onto "<<Narrow(target)<<": the mod's command failed; the same verb invoked directly hr=0x"
            <<std::hex<<hr<<std::dec<<std::endl;
        Check(false,"GAP: Paste onto an expanded folder's subfolder failed through the mod's command");
    };
    // Copy from one expanded folder, paste onto a folder nested in another.
    live.SelectOnly({L"alpha\\a.txt"});
    Check(InvokeInlineSelectionCommand(s,"copy",kQuiet),"Copy was not run");
    if(const auto held=clipboard(); held!=lowered({L"alpha\\a.txt"})) {
        ComPtr<IDataObject> probe;
        const HRESULT got=OleGetClipboard(&probe);
        const HWND opener=GetOpenClipboardWindow();
        DWORD openerPid=0; if(opener) GetWindowThreadProcessId(opener,&openerPid);
        std::cout<<"PARITY clipboard after Copy ("<<held.size()<<" items) get=0x"<<std::hex<<got<<std::dec
            <<" hdrop="<<IsClipboardFormatAvailable(CF_HDROP)<<" seq="<<GetClipboardSequenceNumber()
            <<" openedBy="<<openerPid<<" self="<<GetCurrentProcessId()<<std::endl;
        for(const auto& p:held) std::cout<<"  "<<Narrow(p)<<std::endl;
        Check(false,"the Shell's Copy did not put exactly the copied item on the clipboard");
    }
    paste(L"gamma\\inner");
    Check(std::filesystem::exists(live.Path(L"gamma\\inner\\a.txt")) && std::filesystem::exists(live.Path(L"alpha\\a.txt")),
        "the copied item was not written into the target folder, or the original moved");
    live.Flush({L"gamma\\inner"});
    live.Settled([&] {return live.Row(L"gamma\\inner\\a.txt")>=0;},"the copied item's row did not appear");
    // Cut two items of different folders, paste onto another expanded folder's subfolder.
    live.SelectOnly({L"alpha\\b.txt",L"gamma\\c.txt"});
    Check(InvokeInlineSelectionCommand(s,"cut",kQuiet),"Cut was not run");
    Check(clipboard()==lowered({L"alpha\\b.txt",L"gamma\\c.txt"}),"the Shell's Cut did not put exactly the cut items on the clipboard");
    paste(L"alpha\\sub");
    Check(std::filesystem::exists(live.Path(L"alpha\\sub\\b.txt")) && std::filesystem::exists(live.Path(L"alpha\\sub\\c.txt")) &&
        !std::filesystem::exists(live.Path(L"alpha\\b.txt")) && !std::filesystem::exists(live.Path(L"gamma\\c.txt")),
        "the cut items were not moved");
    // The folders' refresh report (SHCNE_UPDATEDIR), as Windows sends after a
    // burst of changes: the open folder re-lists on it, so must expanded ones.
    live.Flush({L"alpha",L"alpha\\sub",L"gamma"});
    live.Settled([&] {return live.Row(L"alpha\\sub\\b.txt")>=0 && live.Row(L"alpha\\sub\\c.txt")>=0 &&
        live.Row(L"alpha\\b.txt")<0 && live.Row(L"gamma\\c.txt")<0;},
        "GAP: a folder refresh report (SHCNE_UPDATEDIR) for an expanded folder did not re-list it");
    live.CheckConsistent("after clipboard commands");
    std::cout<<"PASS: Copy, Cut and Paste move items in and out of expanded folders like the open folder's own\n";
}


// Selection checkboxes: a click on the checkbox of an item inside an expanded
// folder adds it to (or removes it from) the selection, through the list's
// own window procedure, exactly as on the open folder's own items.
static void TestInlineDirsLiveParityCheckboxes(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-checkboxes");
    Live live(root,4);
    auto& s=live.state;
    FocusFixture::Session focus(live.host.frame,s.hwnd);
    s.clickPolicy.checkSelect=true;
    live.Open(L"alpha");
    const auto click=[&](const wchar_t* item) {
        const int row=live.Row(item);
        const auto visual=LayoutRowVisual(s,row,1.f);
        const auto box=SelectionCheckboxHitRect(s,LayoutItem(s,visual),visual);
        const POINT point{LONG((box.left+box.right)/2),LONG((box.top+box.bottom)/2)};
        Check(SelectionCheckboxAt(s,point)==row,"the item's checkbox is not where the item draws it");
        const LPARAM at=MAKELPARAM(point.x,point.y);
        OverlayProc(s.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,at);
        OverlayProc(s.hwnd,WM_LBUTTONUP,0,at);
        live.Settle("checkbox click");
    };
    const auto selected=[&](const wchar_t* item) {return s.selectedFlags[size_t(live.Row(item))]!=0;};
    // The same sequence on the open folder's items and on an expanded folder's.
    for(const auto& pair:std::vector<std::pair<const wchar_t*,const wchar_t*>>{{L"zeta.txt",L"beta"},{L"alpha\\a.txt",L"alpha\\b.txt"}}) {
        Regression::Scope scope(Narrow(pair.first));
        live.SelectOnly({pair.first});
        click(pair.second);
        Check(selected(pair.first) && selected(pair.second),"a checkbox click did not add its item to the selection");
        click(pair.first);
        Check(!selected(pair.first) && selected(pair.second),"a checkbox click did not remove its item from the selection");
    }
    std::cout<<"PASS: checkboxes inside expanded folders add and remove items like the open folder's own\n";
}

// Item tooltips: hovering an item inside an expanded folder shows the same
// name and the same property lines as hovering it in the folder opened directly.
static void TestInlineDirsLiveParityItemTips(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeParityTree(fixture,L"live-parity-tips");
    const auto alpha=JoinPath(root,L"alpha");
    // Tips of recent items say how long ago ("3 seconds ago"), which moves
    // between the two views' tips. A fixed old date keeps every tip's text
    // the same whenever it is built.
    {
        SYSTEMTIME fixed{2001,1,1,1,12,0,0,0};
        FILETIME old{};
        Check(SystemTimeToFileTime(&fixed,&old),"the fixed fixture date");
        for(const auto& item:std::filesystem::directory_iterator(alpha)) {
            HANDLE h=CreateFileW(item.path().c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
            Check(h!=INVALID_HANDLE_VALUE && SetFileTime(h,&old,&old,&old),"date a tip fixture item");
            CloseHandle(h);
        }
    }
    Live nested(root,4);
    nested.Open(L"alpha");
    SHELLSTATEW shell{};
    SHGetSetSettings(&shell,SSF_SHOWALLOBJECTS,FALSE);
    int listed=0;
    for(const auto& item:std::filesystem::directory_iterator(alpha)) {
        const auto leaf=item.path().filename().wstring();
        if(leaf.starts_with(L"~$") || ((GetFileAttributesW(item.path().c_str()) & FILE_ATTRIBUTE_HIDDEN) && !shell.fShowAllObjects)) continue;
        ++listed;
    }
    Live open(alpha,listed);
    open.Settle("the folder opened directly did not list");
    Present(nested); Present(open);
    // The tooltip's request completes on the columns lane: done when its
    // token is cleared by the result (a hung lane is the only wait).
    const auto tip=[&](Live& live,int row) {
        auto& s=live.state;
        s.visible=s.inScope=s.pointerInside=true;
        s.clickPolicy.itemTips=true;
        ItemTipHoverMoved(s,-1);
        ItemTipHoverMoved(s,row);
        s.hovered=row;
        ShowItemTip(s,POINT{40,60});
        Check(Eventually([&] {
            MSG message{};
            while(PeekMessageW(&message,s.hwnd,kMsgItemTipReady,kMsgItemTipReady,PM_REMOVE)) ConsumeItemTip(s);
            return !s.itemTip.token;
        },30000),"the tooltip lane never finished (hung)");
        std::vector<std::wstring> lines{s.itemTip.name};
        for(const auto& line:s.itemTip.rows) lines.push_back(line.label+L"="+line.value);
        ItemTipHoverMoved(s,-1);
        return lines;
    };
    for(const auto& row:FolderRows(open,alpha)) {
        Regression::Scope scope(Narrow(row.leaf));
        const auto a=tip(nested,nested.Row(L"alpha\\"+row.leaf));
        const auto b=tip(open,open.Row(row.leaf));
        if(a!=b) {
            for(const auto& x:a) std::cout<<"PARITY tip expanded: "<<Narrow(x)<<std::endl;
            for(const auto& x:b) std::cout<<"PARITY tip open: "<<Narrow(x)<<std::endl;
        }
        Check(a==b,"an item's tooltip inside an expanded folder differs from the folder opened directly");
        Check(b.size()>1,"PRECONDITION: the tooltip shows property lines");
    }
    std::cout<<"PASS: item tooltips inside expanded folders match the folder opened directly\n";
}
