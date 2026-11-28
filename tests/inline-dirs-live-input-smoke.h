// Input on items inside an expanded folder against a real private
// ExplorerBrowser, each compared with the same input on the open folder's own
// items: opening, mouse and marquee selection, dragging out, the file
// shortcut keys, Undo, Properties, middle-click, touch, zoom and Compact
// rows. Input enters the overlay's own window procedure and keyboard route;
// modifiers, focus, launches and drags are substituted, so nothing reaches
// the user's desktop, applications, clipboard or Recycle Bin.
namespace InlineLive {
// Client points on a row: its filename, and blank Name-column space after a
// short filename (a marquee origin on an unselected row).
inline POINT ClientX(ViewState& s,float x,float y) {
    return POINT{LONG(s.rtlLayout ? s.paintWidth-x : x),LONG(y)};
}
inline POINT NamePoint(Live& live,const std::wstring& relative) {
    auto& s=live.state;
    const int row=live.Row(relative);
    Check(row>=0,"input row missing");
    const auto item=LayoutItem(s,LayoutRowVisual(s,row,1.f));
    return ClientX(s,item.text.left+4.f,(item.text.top+item.text.bottom)*.5f);
}
inline POINT BlankPoint(Live& live,const std::wstring& relative) {
    auto& s=live.state;
    const int row=live.Row(relative);
    Check(row>=0,"input row missing");
    const auto item=LayoutItem(s,LayoutRowVisual(s,row,1.f));
    const POINT point=ClientX(s,item.text.right-6.f,(item.text.top+item.text.bottom)*.5f);
    Check(!PointOnFilenameText(s,row,point),"PRECONDITION: the blank point is on the filename");
    return point;
}
inline LPARAM At(POINT p) { return MAKELPARAM(p.x,p.y); }
inline void SelectList(Live& live,const std::vector<const wchar_t*>& items) {
    for(size_t i=0;i<items.size();++i) {
        const int row=live.Row(items[i]);
        Check(row>=0,"selection row missing");
        SelectWithModifiers(live.state,row,i>0,false);
    }
}
// One mouse click through the overlay's window procedure; the modifiers are
// the substituted key state, as Explorer's own list reads them.
inline void Click(Live& live,POINT point,bool control=false,bool shift=false) {
    auto& s=live.state;
    ModifierFixture::active[VK_CONTROL]=control ? 0x80 : 0;
    ModifierFixture::active[VK_SHIFT]=shift ? 0x80 : 0;
    const WPARAM keys=(control ? MK_CONTROL : 0)|(shift ? MK_SHIFT : 0);
    OverlayProc(s.hwnd,WM_LBUTTONDOWN,MK_LBUTTON|keys,At(point));
    OverlayProc(s.hwnd,WM_LBUTTONUP,keys,At(point));
    ModifierFixture::active[VK_CONTROL]=ModifierFixture::active[VK_SHIFT]=0;
    live.Settle("click");
}
// The selection by item name and the focused item's name, for comparing a
// folder's items shown inline with the same items of the folder opened.
using Picture=std::pair<std::vector<std::wstring>,std::wstring>;
inline Picture Selection(Live& live) {
    auto& s=live.state;
    std::vector<std::wstring> selected;
    for(size_t i=0;i<s.entries.size() && i<s.selectedFlags.size();++i)
        if(s.selectedFlags[i]) selected.push_back(PathFindFileNameW(s.entries[i].path.c_str()));
    return {selected,s.selected>=0 ? PathFindFileNameW(s.entries[size_t(s.selected)].path.c_str()) : L""};
}
inline void SameSelection(Live& nested,Live& open,const char* step) {
    const auto a=Selection(nested), b=Selection(open);
    if(a!=b) {
        std::cout<<"PARITY "<<step<<": expanded focus="<<Narrow(a.second)<<" open focus="<<Narrow(b.second)<<std::endl;
        for(const auto& x:a.first) std::cout<<"  expanded selected "<<Narrow(x)<<std::endl;
        for(const auto& x:b.first) std::cout<<"  open selected "<<Narrow(x)<<std::endl;
    }
    Check(a==b,"selection inside an expanded folder differs from the folder opened directly");
}
// The folder opened directly, listing exactly what the expanded one lists.
inline int ListedCount(const std::wstring& folder) {
    SHELLSTATEW shell{};
    SHGetSetSettings(&shell,SSF_SHOWALLOBJECTS,FALSE);
    int listed=0;
    for(const auto& item:std::filesystem::directory_iterator(folder)) {
        const auto leaf=item.path().filename().wstring();
        if(leaf.starts_with(L"~$") || ((GetFileAttributesW(item.path().c_str()) & FILE_ATTRIBUTE_HIDDEN) && !shell.fShowAllObjects)) continue;
        ++listed;
    }
    return listed;
}
// The rows from one row to another, by path relative to the root.
inline std::vector<std::wstring> RowsBetween(Live& live,const std::wstring& from,const std::wstring& to) {
    std::vector<std::wstring> rows;
    for(int i=live.Row(from);i>=0 && i<=live.Row(to);++i)
        rows.push_back(live.state.entries[size_t(i)].path.substr(live.root.size()+1));
    return rows;
}
inline std::vector<std::wstring> SelectedRows(Live& live) {
    std::vector<std::wstring> rows;
    for(size_t i=0;i<live.state.entries.size() && i<live.state.selectedFlags.size();++i)
        if(live.state.selectedFlags[i]) rows.push_back(live.state.entries[i].path.substr(live.root.size()+1));
    return rows;
}
// The native view's current folder (after a navigation it started itself).
inline std::wstring NativeFolder(Live& live) {
    ComPtr<IShellView> view; ComPtr<IFolderView2> folderView;
    std::wstring location; ComPtr<IShellFolder> folder;
    if(SUCCEEDED(live.host.browser->GetCurrentView(IID_PPV_ARGS(&view))) && SUCCEEDED(view.As(&folderView)))
        GetFolderPath(folderView.Get(),location,folder);
    return location;
}
inline void WaitNavigation(Live& live,const char* what) {
    Check(Eventually([&] {live.Pump(); return live.host.sink->navigationComplete;},30000),what);
}
}

// 1. Opening: double-clicking a folder inside an expanded folder navigates
// into it as double-clicking a folder of the open folder does; a file runs
// its own default command (recorded, never launched).
static void TestInlineDirsLiveParityOpen(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-open");
    ModifierFixture::Session modifiers;
    TabActivationFixture::Session launches;
    const auto doubleClick=[](Live& live,POINT point) {
        auto& s=live.state;
        FocusFixture::foreground=live.host.frame; FocusFixture::focus=s.hwnd;
        OverlayProc(s.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,At(point));
        OverlayProc(s.hwnd,WM_LBUTTONUP,0,At(point));
        live.host.sink->navigationComplete=false;
        OverlayProc(s.hwnd,WM_LBUTTONDBLCLK,MK_LBUTTON,At(point));
        OverlayProc(s.hwnd,WM_LBUTTONUP,0,At(point));
    };
    {
        // The open folder's own folder: the native view navigates.
        Live live(root,4);
        FocusFixture::Session focus(live.host.frame,live.state.hwnd);
        doubleClick(live,NamePoint(live,L"gamma"));
        WaitNavigation(live,"double-clicking the open folder's own folder did not navigate");
        Check(!_wcsicmp(NativeFolder(live).c_str(),live.Path(L"gamma").c_str()),
            "PRECONDITION: the open folder's own folder opened elsewhere");
    }
    {
        // A folder inside an expanded folder: the same navigation.
        Live live(root,4);
        FocusFixture::Session focus(live.host.frame,live.state.hwnd);
        live.Open(L"alpha");
        doubleClick(live,NamePoint(live,L"alpha\\sub"));
        WaitNavigation(live,"double-clicking a folder inside an expanded folder did not navigate");
        Check(!_wcsicmp(NativeFolder(live).c_str(),live.Path(L"alpha\\sub").c_str()),
            "a folder inside an expanded folder opened somewhere else");
        Check(TabActivationFixture::calls==0,"a folder was launched instead of opened in the window");
    }
    {
        // A file: its own default command, for that item.
        Live live(root,4);
        FocusFixture::Session focus(live.host.frame,live.state.hwnd);
        live.Open(L"alpha");
        const auto file=NamePoint(live,L"alpha\\a.txt");
        doubleClick(live,file);
        Check(TabActivationFixture::calls==1 && !_wcsicmp(TabActivationFixture::item.c_str(),live.Path(L"alpha\\a.txt").c_str()),
            "double-clicking a file inside an expanded folder did not run its own command");
        Check(TabActivationFixture::verb.empty() && (TabActivationFixture::mask & SEE_MASK_INVOKEIDLIST),
            "the file's command is not its default one, through its Shell item");
        // Enter on the same selection takes the same route.
        TabActivationFixture::calls=0;
        Open(live.state,live.Row(L"alpha\\a.txt"));
        Check(TabActivationFixture::calls==1 && !_wcsicmp(TabActivationFixture::item.c_str(),live.Path(L"alpha\\a.txt").c_str()),
            "Enter on a file inside an expanded folder did not run its own command");
    }
    std::cout<<"PASS: double-click and Enter open items inside an expanded folder as the open folder's own\n";
}

// 2. Mouse selection: clicks, Shift ranges and Ctrl toggles on the items of an
// expanded folder select as on the same folder opened directly; a Shift range
// from inside an expanded folder to the open folder's own row spans every
// row between them, as in Explorer's search results.
static void TestInlineDirsLiveParityMouseSelection(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeParityTree(fixture,L"live-parity-mouse");
    const auto alpha=JoinPath(root,L"alpha");
    ModifierFixture::Session modifiers;
    Live nested(root,4);
    nested.Open(L"alpha");
    Live open(alpha,ListedCount(alpha));
    FocusFixture::Session focus(nested.host.frame,nested.state.hwnd);
    open.Settle("the folder opened directly did not list");
    const auto rows=FolderRows(open,alpha);
    Check(rows.size()>=8,"PRECONDITION: enough items to click within the folder");
    struct Step { size_t item; bool control, shift; const char* name; };
    for(const Step& step:std::initializer_list<Step>{
            {1,false,false,"click"},{5,false,true,"Shift+click"},{3,true,false,"Ctrl+click"},
            {7,true,false,"Ctrl+click"},{2,false,true,"Shift+click"},{6,true,true,"Ctrl+Shift+click"},
            {4,false,false,"click"}}) {
        Regression::Scope scope(step.name);
        const auto leaf=rows[step.item].leaf;
        Click(nested,NamePoint(nested,L"alpha\\"+leaf),step.control,step.shift);
        Click(open,NamePoint(open,leaf),step.control,step.shift);
        SameSelection(nested,open,step.name);
    }
    // Across the folder boundary: Shift+click from a child to the open
    // folder's own row selects every row between, the same as Shift+Down.
    Click(nested,NamePoint(nested,L"alpha\\"+rows[2].leaf));
    Click(nested,NamePoint(nested,L"zeta.txt"),false,true);
    Check(SelectedRows(nested)==RowsBetween(nested,L"alpha\\"+rows[2].leaf,L"zeta.txt"),
        "a Shift range across an expanded folder's boundary does not select the rows between");
    Check(nested.state.selected==nested.Row(L"zeta.txt"),"the Shift range did not move focus to the clicked row");
    // Ctrl+click toggles an open-folder row out of a mixed selection.
    Click(nested,NamePoint(nested,L"gamma"),true);
    Check(!nested.state.selectedFlags[size_t(nested.Row(L"gamma"))],"Ctrl+click did not toggle a row out of a mixed selection");
    nested.CheckConsistent("after mouse selection");
    std::cout<<"PASS: mouse clicks, Shift ranges and Ctrl toggles inside an expanded folder select as in the folder opened\n";
}

// 3. Marquee: dragging a rectangle over items of an expanded folder selects
// as over the same folder opened directly, and across its boundary selects
// the rows it covers on both sides.
static void TestInlineDirsLiveParityMarquee(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeParityTree(fixture,L"live-parity-marquee");
    const auto alpha=JoinPath(root,L"alpha");
    ModifierFixture::Session modifiers;
    Live nested(root,4);
    nested.Open(L"alpha");
    Live open(alpha,ListedCount(alpha));
    FocusFixture::Session focus(nested.host.frame,nested.state.hwnd);
    open.Settle("the folder opened directly did not list");
    // From blank space on one row to the filename of another: the rectangle
    // covers the filenames of every row between.
    const auto marquee=[](Live& live,POINT from,POINT to) {
        auto& s=live.state;
        OverlayProc(s.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,At(from));
        Check(s.marqueePossible,"PRECONDITION: the press did not arm a marquee");
        const POINT middle{(from.x+to.x)/2,(from.y+to.y)/2};
        OverlayProc(s.hwnd,WM_MOUSEMOVE,MK_LBUTTON,At(middle));
        OverlayProc(s.hwnd,WM_MOUSEMOVE,MK_LBUTTON,At(to));
        Check(s.marqueeActive,"PRECONDITION: moving past the drag threshold did not start the marquee");
        OverlayProc(s.hwnd,WM_LBUTTONUP,0,At(to));
        live.Settle("marquee commit");
    };
    const auto rows=FolderRows(open,alpha);
    Check(rows.size()>=6,"PRECONDITION: enough items for a marquee");
    for(const auto& [from,to]:std::initializer_list<std::pair<size_t,size_t>>{{1,4},{5,2}}) {
        Regression::Scope scope("rows "+std::to_string(from)+" to "+std::to_string(to));
        // Blank space drags an item only once it is selected: start unselected.
        ClearSelectionForFolderMenu(nested.state); ClearSelectionForFolderMenu(open.state);
        nested.Settle("clear"); open.Settle("clear");
        marquee(nested,BlankPoint(nested,L"alpha\\"+rows[from].leaf),NamePoint(nested,L"alpha\\"+rows[to].leaf));
        marquee(open,BlankPoint(open,rows[from].leaf),NamePoint(open,rows[to].leaf));
        const auto a=Selection(nested).first, b=Selection(open).first;
        if(a!=b) for(const auto& x:a) std::cout<<"  expanded selected "<<Narrow(x)<<std::endl;
        Check(a==b && !a.empty(),"a marquee inside an expanded folder selects differently from the folder opened");
    }
    // Across the boundary: from the last child of alpha to the open
    // folder's own row below it.
    const auto last=FolderRows(nested,alpha).back().leaf;
    ClearSelectionForFolderMenu(nested.state); nested.Settle("clear");
    marquee(nested,BlankPoint(nested,L"alpha\\"+last),NamePoint(nested,L"gamma"));
    Check(SelectedRows(nested)==RowsBetween(nested,L"alpha\\"+last,L"gamma"),
        "a marquee across an expanded folder's boundary does not select the rows it covers");
    nested.CheckConsistent("after marquee");
    std::cout<<"PASS: marquee selection inside and across an expanded folder matches the folder opened and the rows covered\n";
}

// 4. Dragging items out: the drag carries exactly the dragged items, from
// inside an expanded folder, across folders, and with the open folder's own
// row, as dragging the open folder's own items does. The drag loop itself is
// substituted (no drop on the user's desktop).
static void TestInlineDirsLiveParityDragOut(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-drag");
    ModifierFixture::Session modifiers;
    TouchFixture::Session drags;
    Live live(root,4);
    FocusFixture::Session focus(live.host.frame,live.state.hwnd);
    auto& s=live.state;
    InitializeShellDragDrop(s);
    Check(s.shellOleInitialized,"PRECONDITION: drag and drop unavailable");
    live.Open(L"alpha"); live.Open(L"gamma");
    const auto lower=[](std::vector<std::wstring> v) {
        for(auto& p:v) CharLowerBuffW(p.data(),DWORD(p.size()));
        std::sort(v.begin(),v.end()); return v;
    };
    for(const auto& selection:std::vector<std::vector<const wchar_t*>>{
            {L"zeta.txt"},{L"alpha\\a.txt"},{L"alpha\\a.txt",L"alpha\\b.txt"},
            {L"alpha\\a.txt",L"gamma\\c.txt",L"zeta.txt"},{L"alpha\\sub",L"gamma\\inner"}}) {
        std::string label; for(const auto* item:selection) label+=Narrow(item)+" ";
        Regression::Scope scope("drag "+label);
        SelectList(live,selection);
        // Press on a selected item's name and move past the drag threshold.
        const POINT from=NamePoint(live,selection.front());
        const POINT to{from.x+GetSystemMetrics(SM_CXDRAG)+2,from.y+GetSystemMetrics(SM_CYDRAG)+2};
        TouchFixture::promoted=true; TouchFixture::dragPaths.clear();
        const unsigned before=TouchFixture::drags;
        OverlayProc(s.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,At(from));
        OverlayProc(s.hwnd,WM_MOUSEMOVE,MK_LBUTTON,At(to));
        Check(TouchFixture::drags==before+1,"pressing and moving a selected item did not start a drag");
        std::vector<std::wstring> expected;
        for(const auto* item:selection) expected.push_back(live.Path(item));
        if(lower(TouchFixture::dragPaths)!=lower(expected))
            for(const auto& p:TouchFixture::dragPaths) std::cout<<"  dragged "<<Narrow(p)<<std::endl;
        Check(lower(TouchFixture::dragPaths)==lower(expected),"the drag does not carry exactly the selected items");
        live.Settle("after the drag");
        Check(SelectedRows(live).size()==selection.size(),"the drag changed the selection");
    }
    std::cout<<"PASS: dragging items out of expanded folders carries exactly them, alone, across folders and with the open folder's own\n";
}

// 5. The file keys through Explorer's own key route (its accelerator
// translation), on a selection with items inside expanded folders: F2 renames
// the item or the set, Shift+Delete asks the Shell to delete exactly those
// items permanently (declined at its confirmation), as for the open folder's
// own items. Copy and Cut reach the real clipboard: inline-dirs-live-
// clipboard-commands covers them, with Paste.
static void TestInlineDirsLiveParityFileKeys(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-keys-route");
    ModifierFixture::Session modifiers;
    Live live(root,4);
    auto& s=live.state;
    FocusFixture::Session focus(live.host.frame,s.nativeHwnd);
    live.Open(L"alpha"); live.Open(L"gamma");
    // The overlay's window procedure receives the keys Explorer's route
    // forwards to it; Explorer's own accelerators are Windows' function.
    BindNativeViewInput(s.nativeHwnd,s.hwnd);
    const auto previousAccelerator=std::exchange(g_translateAcceleratorOriginal,
        +[](HWND hwnd,HACCEL accelerator,LPMSG msg)->int { return ::TranslateAcceleratorW(hwnd,accelerator,msg); });
    const auto forward=+[](HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)->LRESULT {
        if(message==WM_KEYDOWN || message==WM_SYSKEYDOWN) return OverlayProc(hwnd,message,wp,lp);
        return DefSubclassProc(hwnd,message,wp,lp);
    };
    Check(SetWindowSubclass(s.hwnd,forward,8,0),"overlay key procedure");
    struct Unbind { HWND native,overlay; decltype(g_translateAcceleratorOriginal) accelerator; SUBCLASSPROC forward;
        ~Unbind() { RemoveWindowSubclass(overlay,forward,8); UnbindNativeViewInput(native,overlay);
            g_translateAcceleratorOriginal=accelerator; } }
        unbind{s.nativeHwnd,s.hwnd,previousAccelerator,forward};
    const auto key=[&](UINT vk,bool shift=false,UINT message=WM_KEYDOWN) {
        ModifierFixture::active[VK_SHIFT]=shift ? 0x80 : 0;
        MSG msg{s.nativeHwnd,message,vk,1};
        const int handled=TranslateAcceleratorInput(live.host.frame,nullptr,&msg);
        ModifierFixture::active[VK_SHIFT]=0;
        return handled;
    };
    // F2 on one item inside an expanded folder, then on a set across folders.
    for(const auto& selection:std::vector<std::vector<const wchar_t*>>{
            {L"alpha\\a.txt"},{L"alpha\\a.txt",L"gamma\\c.txt",L"zeta.txt"}}) {
        Regression::Scope scope(selection.size()==1 ? "F2 one item" : "F2 a set");
        SelectList(live,selection);
        FocusFixture::focus=s.nativeHwnd;
        key(VK_F2); live.Pump();
        Check(s.rename.editor!=nullptr,"F2 did not start renaming the selection");
        Check(s.rename.multiple==(selection.size()>1),"F2 did not rename the whole selection");
        CancelRename(s);
    }
    // Shift+Delete: the Shell's permanent delete of exactly the selection
    // (private fixture files). A dialog the Shell shows on the way (where
    // this machine's Shell asks) is accepted; its owner sees its modal loop.
    std::wstring asked;
    struct Watch {
        static LRESULT CALLBACK Owner(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
            if(message==WM_ENTERIDLE && wp==MSGF_DIALOGBOX && lp) {
                const HWND dialog=reinterpret_cast<HWND>(lp);
                auto& text=*reinterpret_cast<std::wstring*>(data);
                if(text.empty()) {
                    EnumChildWindows(dialog,[](HWND child,LPARAM out)->BOOL {
                        wchar_t buffer[512]{}; GetWindowTextW(child,buffer,ARRAYSIZE(buffer));
                        *reinterpret_cast<std::wstring*>(out)+=std::wstring(buffer)+L"|"; return TRUE;
                    },reinterpret_cast<LPARAM>(&text));
                    if(text.empty()) text=L"(dialog)";
                }
                PostMessageW(dialog,WM_COMMAND,IDYES,0);
            }
            return DefSubclassProc(hwnd,message,wp,lp);
        }
    };
    Check(SetWindowSubclass(live.host.frame,Watch::Owner,7,reinterpret_cast<DWORD_PTR>(&asked)),"confirmation observer");
    live.SelectOnly({L"alpha\\a.txt",L"gamma\\c.txt"});
    FocusFixture::focus=s.nativeHwnd;
    Check(key(VK_DELETE,true),"Shift+Delete on a selection inside expanded folders was not handled");
    RemoveWindowSubclass(live.host.frame,Watch::Owner,7);
    if(!asked.empty()) std::cout<<"NOTE: the Shell showed a dialog: "<<Narrow(asked)<<std::endl;
    Check(!std::filesystem::exists(live.Path(L"alpha\\a.txt")) && !std::filesystem::exists(live.Path(L"gamma\\c.txt")),
        "Shift+Delete did not delete the selected items");
    Check(std::filesystem::exists(live.Path(L"alpha\\b.txt")) && std::filesystem::exists(live.Path(L"zeta.txt")),
        "Shift+Delete deleted an item that was not selected");
    live.Flush({L"alpha",L"gamma"});
    live.Settled([&] {return live.Row(L"alpha\\a.txt")<0 && live.Row(L"gamma\\c.txt")<0;},"the deleted items kept their rows");
    live.CheckConsistent("after the file keys");
    std::cout<<"PASS: F2 and Shift+Delete on items inside expanded folders take Explorer's key route to the whole selection\n";
}

// 6. Undo (Ctrl+Z, Explorer's own command) after renaming an item inside an
// expanded folder restores its name and row as for the open folder's own.
static void TestInlineDirsLiveParityUndo(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-undo");
    Live live(root,4);
    FocusFixture::Session focus(live.host.frame,live.state.hwnd);
    auto& s=live.state;
    live.Open(L"alpha");
    ComPtr<IShellView> view;
    Check(SUCCEEDED(live.host.browser->GetCurrentView(IID_PPV_ARGS(&view))),"native view");
    // Explorer's Undo: the open folder's background command (what Ctrl+Z
    // and the menu's Undo run), without UI.
    const auto undo=[&] {
        ComPtr<IContextMenu> menu;
        if(FAILED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu))) || !menu) return false;
        HMENU popup=CreatePopupMenu();
        menu->QueryContextMenu(popup,0,1,0x7FFF,CMF_NORMAL);
        const auto verbs=MenuVerbs(menu.Get());
        // The menu's Undo entry by its command (the folder menu runs it by id).
        UINT command=UINT(-1);
        for(UINT id=0;id<0x7FFE && command==UINT(-1);++id) {
            wchar_t verb[80]{};
            if(SUCCEEDED(menu->GetCommandString(id,GCS_VERBW,nullptr,reinterpret_cast<LPSTR>(verb),ARRAYSIZE(verb))) &&
                !_wcsicmp(verb,L"undo")) command=id;
        }
        CMINVOKECOMMANDINFO info{sizeof(info)};
        info.fMask=CMIC_MASK_FLAG_NO_UI; info.nShow=SW_SHOWNORMAL; info.hwnd=live.host.frame;
        info.lpVerb=command!=UINT(-1) ? MAKEINTRESOURCEA(command) : "undo";
        // The entry names what it undoes ("Undo Rename"); printed as evidence.
        wchar_t text[128]{};
        if(command!=UINT(-1)) GetMenuStringW(popup,1+command,text,ARRAYSIZE(text),MF_BYCOMMAND);
        const HRESULT hr=menu->InvokeCommand(&info);
        DestroyMenu(popup);
        if(FAILED(hr)) {
            std::cout<<"UNDO entry: "<<Narrow(text)<<" hr=0x"<<std::hex<<hr<<std::dec<<" verbs:";
            for(const auto& v:verbs) std::cout<<" "<<Narrow(v);
            std::cout<<std::endl;
        }
        return SUCCEEDED(hr);
    };
    struct Case { const wchar_t* item; const wchar_t* renamed; const wchar_t* folder; };
    bool restored[2]{};
    bool selected[2]{};
    int index=0;
    for(const Case& c:{Case{L"zeta.txt",L"zeta renamed.txt",L""},Case{L"alpha\\a.txt",L"alpha\\a renamed.txt",L"alpha"}}) {
        Regression::Scope scope(Narrow(c.item));
        live.SelectOnly({c.item});
        BeginRename(s,live.Row(c.item));
        Check(s.rename.editor!=nullptr,"rename editor");
        Check(SetWindowTextW(s.rename.editor,PathFindFileNameW(c.renamed)),"rename text");
        FinishRename(s,true);
        Check(std::filesystem::exists(live.Path(c.renamed)),"PRECONDITION: the rename did not happen");
        live.Flush({c.folder}); live.Settle("re-list after the rename");
        // Undo runs as a Shell file operation on its own thread: it is done
        // when the folder's names change on disk (the file system's own
        // change signal); the deadline only guards against a hung Undo.
        // The Shell adds the rename to its Undo stack after the rename returns.
        Check(Eventually([&] {
            live.Pump();
            ComPtr<IContextMenu> menu;
            if(FAILED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu))) || !menu) return false;
            return HasVerb(MenuVerbs(menu.Get()),L"undo");
        },30000),"PRECONDITION: the Shell never offered to undo the rename");
        const HANDLE changed=FindFirstChangeNotificationW(live.Path(c.folder).c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME);
        Check(changed!=INVALID_HANDLE_VALUE,"folder change signal");
        struct Close { HANDLE h; ~Close() { FindCloseChangeNotification(h); } } close{changed};
        Check(undo(),"Explorer's Ctrl+Z was not accepted");
        Check(Eventually([&] {live.Pump(); return WaitForSingleObject(changed,0)==WAIT_OBJECT_0;},30000),
            "Undo did not change the folder");
        restored[index]=std::filesystem::exists(live.Path(c.item)) && !std::filesystem::exists(live.Path(c.renamed));
        live.Flush({c.folder}); live.Settle("re-list after Undo");
        if(restored[index]) Check(live.Row(c.item)>=0 && live.Row(c.renamed)<0,"Undo restored the name but not the row");
        selected[index]=live.Row(c.item)>=0 && s.selectedFlags[size_t(live.Row(c.item))];
        ++index;
    }
    Check(restored[0],"PRECONDITION: Undo did not restore the open folder's own renamed item");
    Check(restored[1],"Undo did not restore an item renamed inside an expanded folder");
    Check(selected[0]==selected[1],"after Undo, the item inside the expanded folder is selected differently from the open folder's own");
    live.CheckConsistent("after Undo");
    std::cout<<"PASS: Undo restores an item renamed inside an expanded folder as one of the open folder\n";
}

// 7. Properties (Alt+Enter, through Explorer's key route) on a selection with
// items inside expanded folders shows the properties of exactly those items.
// The sheet is a real window: the run is on a private desktop.
static void TestInlineDirsLiveParityPropertiesCore(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-properties");
    ModifierFixture::Session modifiers;
    Live live(root,4);
    auto& s=live.state;
    FocusFixture::Session focus(live.host.frame,s.hwnd);
    live.Open(L"alpha"); live.Open(L"gamma");
    std::vector<std::wstring> log;
    SelectionLogFixture::Session logSession(log);
    const auto* previousPrefix=std::exchange(SelectionLogFixture::prefix,L"[GPU tree] command");
    const bool previousPerf=g_perf.exchange(true);
    struct Restore { const wchar_t* prefix; bool perf; ~Restore() { SelectionLogFixture::prefix=prefix; g_perf=perf; } }
        restore{previousPrefix,previousPerf};
    for(const auto& selection:std::vector<std::vector<const wchar_t*>>{
            {L"alpha\\a.txt"},{L"alpha\\a.txt",L"gamma\\c.txt",L"zeta.txt"}}) {
        Regression::Scope scope(std::to_string(selection.size())+" items");
        SelectList(live,selection);
        log.clear();
        ModifierFixture::active[VK_MENU]=0x80;
        MSG msg{s.hwnd,WM_SYSKEYDOWN,VK_RETURN,1|(1<<29)};
        const int handled=TranslateAcceleratorInput(live.host.frame,nullptr,&msg);
        ModifierFixture::active[VK_MENU]=0;
        Check(handled,"Alt+Enter on a selection inside expanded folders was not handled");
        std::vector<std::wstring> items;
        for(const auto& line:log)
            if(line.find(L"command 'properties' item")!=std::wstring::npos) items.push_back(line.substr(line.rfind(L' ')+1));
        std::vector<std::wstring> expected;
        for(const auto* item:selection) expected.push_back(PathFindFileNameW(live.Path(item).c_str()));
        for(auto& item:items) item=PathFindFileNameW(item.c_str());
        std::sort(items.begin(),items.end()); std::sort(expected.begin(),expected.end());
        if(items!=expected) for(const auto& line:log) std::cout<<"  "<<Narrow(line)<<std::endl;
        Check(items==expected,"Alt+Enter did not show the properties of exactly the selection");
    }
    std::cout<<"PASS: Alt+Enter shows the properties of exactly the selection inside expanded folders\n";
}
static void TestInlineDirsLiveParityProperties(const std::wstring&) {
    PrivateDesktopFixture::Run(L"inline-properties",L"--test inline-dirs-live-parity-properties-core",110000);
}

// 8. Middle-click: a folder inside an expanded folder asks for Explorer's own
// "Open in new tab" on that folder; the open folder's own folder, while items
// inside expanded folders are selected, takes the native route and selects
// it alone, as when nothing else is selected.
static void TestInlineDirsLiveParityMiddleClick(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-middle");
    ModifierFixture::Session modifiers;
    Live live(root,4);
    FocusFixture::Session focus(live.host.frame,live.state.hwnd);
    auto& s=live.state;
    live.Open(L"alpha");
    const bool previousMiddle=g_middleClickNewTab.exchange(true);
    std::vector<std::wstring> log;
    SelectionLogFixture::Session logSession(log);
    const auto* previousPrefix=std::exchange(SelectionLogFixture::prefix,L"[GPU tree]");
    const bool previousPerf=g_perf.exchange(true);
    struct Restore { const wchar_t* prefix; bool perf, middle; ~Restore() {
        SelectionLogFixture::prefix=prefix; g_perf=perf; g_middleClickNewTab=middle; } }
        restore{previousPrefix,previousPerf,previousMiddle};
    const auto commanded=[&](const wchar_t* verb) {
        return std::any_of(log.begin(),log.end(),[&](const std::wstring& line) {
            return line.find(std::wstring(L"command '")+verb+L"' on")!=std::wstring::npos;});
    };
    // A child folder: its own "opennewtab" verb, for that folder alone.
    live.SelectOnly({L"alpha\\a.txt"});
    log.clear();
    OverlayProc(s.hwnd,WM_MBUTTONDOWN,MK_MBUTTON,At(NamePoint(live,L"alpha\\sub")));
    OverlayProc(s.hwnd,WM_MBUTTONUP,0,At(NamePoint(live,L"alpha\\sub")));
    Check(commanded(L"opennewtab"),"middle-clicking a folder inside an expanded folder did not ask for a new tab");
    Check(SelectedRows(live)==std::vector<std::wstring>{L"alpha\\sub"},"the new-tab request was not for that folder alone");
    // A child file: nothing (only folders open in a new tab).
    log.clear();
    OverlayProc(s.hwnd,WM_MBUTTONDOWN,MK_MBUTTON,At(NamePoint(live,L"alpha\\a.txt")));
    Check(log.empty(),"middle-clicking a file inside an expanded folder ran a command");
    // The open folder's own folder while a child is selected: the native
    // route, which selects that folder alone.
    live.SelectOnly({L"alpha\\a.txt",L"gamma"});
    log.clear();
    OverlayProc(s.hwnd,WM_MBUTTONDOWN,MK_MBUTTON,At(NamePoint(live,L"gamma")));
    OverlayProc(s.hwnd,WM_MBUTTONUP,0,At(NamePoint(live,L"gamma")));
    live.Settle("middle-click on the open folder's own folder");
    Check(!commanded(L"opennewtab"),"the open folder's own folder took the expanded-folder route");
    Check(SelectedRows(live)==std::vector<std::wstring>{L"gamma"},"middle-click on the open folder's own folder did not select it alone");
    std::cout<<"PASS: middle-click opens folders inside expanded folders in a new tab; the open folder's own take the native route\n";
}

// 9. Touch: a tap on an item inside an expanded folder selects it, and a
// press and hold asks for its context menu, as on the open folder's own item.
// Contacts come from a private digitizer through Windows' own recognizer.
static void TestInlineDirsLiveParityTouch(const std::wstring& fixture) {
    using namespace InlineLive;
    using namespace TouchInputTest;
    const auto root=MakeTree(fixture,L"live-parity-touch");
    Device input;
    TouchFixture::Session touch;
    FrameInputFixture::Session queue;
    PointerFixture::Session pointer;
    ModifierFixture::Session modifiers;
    Live live(root,4);
    auto& s=live.state;
    FocusFixture::Session focus(live.host.frame,s.hwnd);
    MenuFixture::Session menus(s.hwnd);
    // As the overlay's window procedure: Windows turns the replayed right
    // button release into WM_CONTEXTMENU on this window (touch-input's Owner).
    const auto owner=+[](HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)->LRESULT {
        if((message==WM_TIMER && wp==kTouchHoldTimerId) || message==kMsgTouchDrag || message==WM_CAPTURECHANGED ||
            message==WM_CONTEXTMENU) return OverlayProc(hwnd,message,wp,lp);
        return DefSubclassProc(hwnd,message,wp,lp);
    };
    Check(SetWindowSubclass(s.hwnd,owner,9,0),"overlay touch procedure");
    struct Unsubclass { HWND hwnd; SUBCLASSPROC proc; ~Unsubclass() { RemoveWindowSubclass(hwnd,proc,9); } } unsubclass{s.hwnd,owner};
    live.Open(L"alpha");
    for(const wchar_t* item:{L"zeta.txt",L"alpha\\a.txt"}) {
        Regression::Scope scope(Narrow(item));
        // Tap: selects that item alone.
        live.SelectOnly({L"gamma"});
        Reset(s,input);
        const POINT point=NamePoint(live,item);
        input.Next(); input.Send(s,{input.Packet(s,input.primary,point,down)});
        input.Next(); input.Send(s,{input.Packet(s,input.primary,point,up)});
        live.Settle("tap");
        Check(SelectedRows(live)==std::vector<std::wstring>{item} && s.selected==live.Row(item),
            "a tap did not select the item alone");
        // Press and hold past the system's hold time: the item's menu.
        Reset(s,input);
        MenuFixture::calls=0;
        // Windows' recognizer holds on its own clock: real time, and the
        // mod's hold deadline (its completion state), as touch-input does.
        Reset(s,input,true);
        input.Next(); input.Send(s,{input.Packet(s,input.primary,point,down)});
        Held(s);
        input.Next(); input.Send(s,{input.Packet(s,input.primary,point,up)});
        Pump(s); live.Settle("hold");
        Check(MenuFixture::calls>0,"press and hold did not ask for the item's menu");
        Check(s.selectedFlags[size_t(live.Row(item))],"the held item is not the menu's selection");
    }
    std::cout<<"PASS: taps and press-and-hold on items inside an expanded folder select and open menus as on the open folder's own\n";
}

// 10. Zoom and Compact rows: items inside expanded folders keep the open
// folder's row height, icon size and text size, and their indentation stays
// one level per depth, at every zoom and with Compact rows on or off.
static void TestInlineDirsLiveParityZoom(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-parity-zoom");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub");
    struct ResetCompact { ~ResetCompact() { ExplorerSettingsFixture::compact=false; } } resetCompact;
    for(bool compact:{false,true}) for(int icon:{16,24,32,48}) {
        Regression::Scope scope("compact="+std::to_string(compact)+" icon="+std::to_string(icon));
        ExplorerSettingsFixture::compact=compact;
        ApplyViewLayout(s,{FVM_DETAILS,icon},false); UpdateMetrics(s); UpdateColumns(s);
        ReconcileCompactRows(s); AdvanceRowHeight(s,true); StopRowTransition(s);
        live.Settle("zoom");
        const int own=live.Row(L"zeta.txt"), child=live.Row(L"alpha\\a.txt"), deep=live.Row(L"alpha\\sub\\deep.txt");
        Check(own>=0 && child>=0 && deep>=0,"PRECONDITION: rows at each depth");
        const auto a=LayoutItem(s,LayoutRowVisual(s,own,1.f));
        const auto b=LayoutItem(s,LayoutRowVisual(s,child,1.f));
        const auto c=LayoutItem(s,LayoutRowVisual(s,deep,1.f));
        Check(DisplayRowHeight(s,child)==DisplayRowHeight(s,own) && DisplayRowHeight(s,deep)==DisplayRowHeight(s,own),
            "rows inside expanded folders are not as tall as the open folder's own");
        const auto width=[](const D2D1_RECT_F& r) {return r.right-r.left;};
        const auto height=[](const D2D1_RECT_F& r) {return r.bottom-r.top;};
        Check(width(b.icon)==width(a.icon) && width(c.icon)==width(a.icon) && height(b.icon)==height(a.icon),
            "icons inside expanded folders are sized differently");
        Check(height(b.text)==height(a.text) && height(c.text)==height(a.text),"text inside expanded folders is sized differently");
        // One level per depth: the same step from the open folder's row to a
        // child and from the child to a grandchild, wider than the icon.
        const float step=b.icon.left-a.icon.left, next=c.icon.left-b.icon.left;
        Check(step>width(a.icon) && std::fabs(step-next)<.01f,"indentation is not one equal level per depth");
    }
    std::cout<<"PASS: items inside expanded folders keep the open folder's row, icon and text size and one level per depth at every zoom and with Compact rows\n";
}
