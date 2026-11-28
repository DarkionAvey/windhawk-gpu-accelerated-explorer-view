// Expandable folders in Details view (inline dirs). Private fixture folders,
// the production names lane for every listing (the tab's own folder and each
// expanded folder), real Windows timers for the busy indicator and DirectWrite
// for independent text measurements. No manufactured messages or user folders.
#include <aclapi.h>
namespace InlineDirs {

inline void MakeFile(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"inline fixture file");
    CloseHandle(file);
}
inline void MakeFolder(const std::wstring& path) {
    Check(std::filesystem::create_directory(path),"inline fixture folder");
}

// alpha\{a.txt,b.txt,sub\{deep.txt}}, beta\ (empty), zeta.txt
inline std::wstring MakeTree(const std::wstring& fixture,const wchar_t* name) {
    const auto root=JoinPath(fixture,name);
    MakeFolder(root);
    const auto alpha=JoinPath(root,L"alpha");
    MakeFolder(alpha);
    MakeFile(JoinPath(alpha,L"b.txt"));
    MakeFile(JoinPath(alpha,L"a.txt"));
    MakeFolder(JoinPath(alpha,L"sub"));
    MakeFile(JoinPath(JoinPath(alpha,L"sub"),L"deep.txt"));
    MakeFolder(JoinPath(root,L"beta"));
    MakeFile(JoinPath(root,L"zeta.txt"));
    return root;
}

// A Details tab over a private folder: production listing through the tab's
// own worker, as in item-drag-hit.
struct Tab {
    ViewState state;
    explicit Tab(const std::wstring& folder,UINT dpi,bool rtl=false,int iconSize=16) {
        StyleFixture::dpi=dpi;
        if(!g_writeFactory)
            Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"inline dirs fonts");
        state.hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private inline dirs",
            WS_POPUP,-32000,-32000,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"inline dirs window");
        // The overlay's own continuations reach OverlayProc with this state.
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        state.nativeHwnd=state.frameHwnd=state.activeTabHost=state.background->hwnd=state.hwnd;
        state.folderPath=folder; state.visible=state.inScope=state.detailsViewActive=true;
        state.rtlLayout=rtl; state.paintWidth=1000.f;
        ApplyViewLayout(state,{FVM_DETAILS,iconSize},false); UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        QueueDirectoryRefresh(state,true);
        auto names=WaitForComplete(*state.background,state.directoryToken);
        PublishDirectoryResult(*state.background,std::move(names)); ReceiveDirectoryResults(state); StopRowTransition(state);
        RebuildGroupSections(state); UpdateColumns(state);
        Check(EnsureTarget(state),"inline dirs text formats");
    }
    ~Tab() {
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
        UnregisterAnimationWindow(state.hwnd); Stop(state.background); DropTarget(state); DestroyWindow(state.hwnd);
    }
    int Row(const std::wstring& name) const {
        for(size_t i=0;i<state.entries.size();++i)
            if(!_wcsicmp(state.entries[i].name.c_str(),name.c_str())) return int(i);
        return -1;
    }
    // Every expanded folder's listing arrives on the names lane; the busy
    // indicator's real timer is serviced if a listing is slow on this PC.
    void Settle() {
        Check(Eventually([&] {
            MSG message{};
            while(PeekMessageW(&message,state.hwnd,WM_TIMER,WM_TIMER,PM_REMOVE))
                if(message.wParam==kInlineTreeBusyTimerId) InlineTreeBusyTick(state);
            while(PeekMessageW(&message,state.hwnd,kMsgInlineTreeReopen,kMsgInlineTreeReopen,PM_REMOVE))
                OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
            bool pending=false;
            {
                ExclusiveSrwLock lock(state.background->directoryLock);
                pending=!state.background->treeResults.empty();
            }
            if(pending) ReceiveInlineTreeResults(state);
            for(const auto& node:state.inlineTree) if(!node.loaded) return false;
            return true;
        }),"inline folder listing did not arrive");
        StopRowTransition(state);
    }
    void Toggle(const std::wstring& name) {
        const int row=Row(name);
        Check(row>=0,"inline fixture row missing");
        ToggleInlineFolder(state,row);
        Settle();
    }
    ItemGeometry Item(int row) {
        return LayoutItem(state,LayoutRowVisual(state,row,1.f));
    }
};

// Cap height of the renderer's configured body face, read independently from
// the font's design metrics (not through the mod's helpers).
inline float CapHeight(IDWriteTextFormat* format) {
    ComPtr<IDWriteFontCollection> collection;
    Check(SUCCEEDED(g_writeFactory->GetSystemFontCollection(&collection)),"system fonts");
    std::wstring family(format->GetFontFamilyNameLength()+1,L'\0');
    Check(SUCCEEDED(format->GetFontFamilyName(family.data(),UINT32(family.size()))),"body family");
    UINT32 index=0; BOOL exists=FALSE;
    Check(SUCCEEDED(collection->FindFamilyName(family.c_str(),&index,&exists)) && exists,"body family installed");
    ComPtr<IDWriteFontFamily> fonts; ComPtr<IDWriteFont> font;
    Check(SUCCEEDED(collection->GetFontFamily(index,&fonts)) &&
        SUCCEEDED(fonts->GetFirstMatchingFont(format->GetFontWeight(),format->GetFontStretch(),format->GetFontStyle(),&font)),"body face");
    DWRITE_FONT_METRICS metrics{}; font->GetMetrics(&metrics);
    Check(metrics.designUnitsPerEm && metrics.capHeight,"body cap height");
    return float(metrics.capHeight)*format->GetFontSize()/metrics.designUnitsPerEm;
}
// Width of the arrow's box from that measurement: the open (down) chevron is
// one cap height wide; its stroke is an eighth of the cap, at least one DPI
// unit, and half of it reaches past each end.
inline float ExpectedChevronBox(float cap,UINT dpi) {
    return cap+std::max(float(MulDiv(1,int(dpi),96)),cap*.125f);
}
inline bool Near(float a,float b,float tolerance=.51f) { return std::abs(a-b)<=tolerance; }

struct OptionScope {
    bool previous;
    explicit OptionScope(bool on) : previous(g_expandableFolders.exchange(on)) {}
    ~OptionScope() { g_expandableFolders=previous; }
};

} // namespace InlineDirs

// Expanding grafts the listing under its folder in the folder's own sort,
// nested expansion adds deeper rows, the option's indentation matches an
// independent cap-height/DPI expectation, and collapsing removes descendants
// and hands focus to the collapsed folder.
static void TestInlineDirsExpandCollapse(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"inline dirs fonts");
    const auto root=MakeTree(fixture,L"inline-expand");
    for(UINT dpi:{96u,144u}) {
        Regression::Scope scope("inline expand dpi="+std::to_string(dpi));
        OptionScope option(false);
        Tab tab(root,dpi);
        auto& s=tab.state;
        Check(s.total==3 && s.rootTotal==3,"fixture root rows");
        const int alphaRow=tab.Row(L"alpha");
        const float plainIcon=tab.Item(alphaRow).icon.left;
        // Option off: no tree, no arrow space, toggles do nothing.
        ToggleInlineFolder(s,alphaRow);
        Check(s.inlineTree.empty() && s.total==3,"toggle worked with expandable folders off");
        g_expandableFolders=true;
        const float cap=CapHeight(s.textFormat.Get());
        const float box=ExpectedChevronBox(cap,dpi);
        const float level=box+s.iconPx*.42f;
        Check(Near(tab.Item(alphaRow).icon.left,plainIcon+level),
            "root icon is not one chevron box and Finder's icon gap further in");
        tab.Toggle(L"alpha");
        Check(s.rootTotal==3 && s.total==6,"alpha's listing was not grafted");
        const int a=tab.Row(L"alpha");
        const auto at=[&](int offset) { return s.entries[size_t(a+offset)]; };
        {
            // The folder opened on its own, through the ordinary listing, is
            // the expected order of its inline children.
            Tab opened(JoinPath(root,L"alpha"),dpi);
            Check(opened.state.total==3,"alpha listed on its own");
            for(int i=0;i<3;++i)
                Check(at(i+1).name==opened.state.entries[size_t(i)].name,
                    "children are not in the order the folder lists in on its own");
        }
        for(int i=1;i<=3;++i) Check(at(i).treeDepth==1,"child depth");
        Check(s.entries[size_t(a)].treeExpanded && !s.entries[size_t(a)].treeEmpty,"expanded folder state");
        Check(Near(tab.Item(a+2).icon.left,tab.Item(a).icon.left+level),"child is not indented by one level");
        // Nested expansion.
        tab.Toggle(L"sub");
        const int deep=tab.Row(L"deep.txt");
        Check(deep==tab.Row(L"sub")+1 && s.entries[size_t(deep)].treeDepth==2 && s.total==7,"grandchild was not grafted");
        Check(Near(tab.Item(deep).icon.left,tab.Item(a).icon.left+2*level),"grandchild indentation");
        // An empty folder says so (no listing error).
        tab.Toggle(L"beta");
        const int beta=tab.Row(L"beta");
        Check(s.entries[size_t(beta)].treeExpanded && s.entries[size_t(beta)].treeEmpty &&
            s.entries[size_t(beta)].treeError==ERROR_SUCCESS,"empty folder was not reported as empty");
        // Focus inside a folder moves to the folder when it collapses; the
        // collapsed folder forgets its expanded descendants.
        Select(s,tab.Row(L"deep.txt"),false);
        tab.Toggle(L"alpha");
        // Only the three root rows remain: beta stays open but is empty.
        Check(s.total==3 && tab.Row(L"deep.txt")<0 && tab.Row(L"a.txt")<0,"collapse left children behind");
        Check(!FindInlineTreeNode(s,JoinPath(JoinPath(root,L"alpha"),L"sub")),"collapse kept a descendant expanded");
        Check(s.selected==tab.Row(L"alpha"),"focus did not move to the collapsed folder");
        // Turning the option off returns the folder's own rows.
        g_expandableFolders=false;
        ClearInlineTree(s);
        Check(s.total==3 && s.rootTotal==3 && s.inlineTree.empty(),"option off kept grafted rows");
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: inline folders graft in folder sort order, nest, indent by an independently measured level, "
        "report empty folders and collapse with focus handoff at 96/144 DPI\n";
}

// Inline folders belong to the Details list only. Every other layout (icon
// grids, Tiles, Content, List — the future Miller columns) shows the folder's
// own items: no child becomes a cell, a row or a morphing item, animated or
// not. Returning to Details reopens the same folders.
static void TestInlineDirsLayoutSwitch(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    OptionScope option(true);
    const auto root=MakeTree(fixture,L"inline-layouts");
    for(bool animate:{false,true})
        for(const FolderViewMode target:{FolderViewMode{FVM_ICON,48},FolderViewMode{FVM_ICON,96},FolderViewMode{FVM_SMALLICON,16},
                FolderViewMode{FVM_TILE,48},FolderViewMode{FVM_CONTENT,32},FolderViewMode{FVM_LIST,16}}) {
            Regression::Scope scope("layout mode="+std::to_string(target.mode)+" size="+std::to_string(target.iconSize)+
                " animated="+std::to_string(animate));
            Tab tab(root,96);
            auto& s=tab.state;
            tab.Toggle(L"alpha"); tab.Toggle(L"sub");
            Check(s.total==7 && s.rootTotal==3,"fixture tree did not open");
            ApplyViewLayout(s,target,animate,true);
            Check(!InlineTreeActive(s),"inline folders active outside Details");
            Check(s.total==3 && s.rootTotal==3 && s.entries.size()==3,"inline children shown outside Details");
            for(const auto& e:s.entries) Check(e.treeDepth==0,"a child row reached a non-Details layout");
            for(const auto& motion:s.rowMotion)
                Check(motion.newIndex<s.total,"a layout morph targets a stripped child");
            for(const auto& ghost:s.disappearingRows)
                Check(ghost.entry.treeDepth==0,"a child was captured into the layout morph");
            StopRowTransition(s); FinishColumnTransition(s);
            // Back to Details: the same folders reopen.
            ApplyViewLayout(s,{FVM_DETAILS,16},animate,true);
            FinishColumnTransition(s); StopRowTransition(s);
            tab.Settle();
            Check(InlineTreeActive(s),"Details did not reactivate inline folders");
            Check(s.total==7 && tab.Row(L"deep.txt")>=0 && s.entries[size_t(tab.Row(L"deep.txt"))].treeDepth==2,
                "returning to Details did not reopen the expanded folders");
        }
    // Grid to Details, animated, three round trips, ending the column change
    // by completion or by interruption: the morph's destination already has
    // the arrow's space (no jump when the columns settle), and the expanded
    // folders reopen every time.
    {
        Tab tab(root,96);
        auto& s=tab.state;
        tab.Toggle(L"alpha"); tab.Toggle(L"sub");
        for(int trip=0;trip<3;++trip) for(bool interrupt:{false,true}) {
            Regression::Scope scope("round trip "+std::to_string(trip)+" interrupt="+std::to_string(interrupt));
            ApplyViewLayout(s,{FVM_ICON,48},true,true);
            StopRowTransition(s); FinishColumnTransition(s);
            Check(s.total==3,"grid kept children");
            ApplyViewLayout(s,{FVM_DETAILS,16},true,true);
            const int zeta=tab.Row(L"zeta.txt");
            const float during=tab.Item(zeta).icon.left;
            const float expected=NameColumnStart(s)+float(MulDiv(16,96,96))+
                ExpectedChevronBox(CapHeight(s.textFormat.Get()),96)+s.iconPx*.42f;
            Check(Near(during,expected),"the morph's destination icon leaves no room for the arrow");
            if(interrupt) StopColumnTransition(s); else FinishColumnTransition(s);
            StopRowTransition(s);
            tab.Settle();
            Check(Near(tab.Item(tab.Row(L"zeta.txt")).icon.left,during),"the icon jumped when the columns settled");
            Check(s.total==7 && tab.Row(L"deep.txt")>=0,"the expanded folders did not reopen after the layout change");
        }
    }
    std::cout<<"PASS: icon, Tiles, Content and List layouts show only the folder's own items (animated or not); "
        "Details reopens the expanded folders every time, and its morph targets the arrow's space\n";
}

// Grouping belongs to the open folder. In a grouped folder, expanded children
// are never grouped themselves: they stay directly under their folder inside
// its group (no new sections), and collapse with that group.
static void TestInlineDirsGrouped(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    OptionScope option(true);
    const auto root=MakeTree(fixture,L"inline-grouped");
    MakeFolder(JoinPath(root,L"gamma"));
    MakeFile(JoinPath(JoinPath(root,L"gamma"),L"zz.txt"));
    Tab tab(root,96);
    auto& s=tab.state;
    // Group by name, through the tab's own listing: alpha, beta, gamma and
    // zeta.txt fall into four groups (A, B, G, Z).
    s.grouping.enabled=true; s.grouping.key=PKEY_FileName;
    QueueDirectoryRefresh(s,true);
    auto names=WaitForComplete(*s.background,s.directoryToken);
    PublishDirectoryResult(*s.background,std::move(names)); ReceiveDirectoryResults(s); StopRowTransition(s);
    const size_t groups=s.sections.size();
    Check(groups>=3,"grouped fixture has no groups");
    Check(InlineTreeActive(s),"inline folders unavailable in a grouped folder");
    tab.Toggle(L"alpha"); tab.Toggle(L"sub"); tab.Toggle(L"gamma");
    Check(s.total==3+1+3+1+1 && s.sections.size()==groups,"expanded children created or lost groups");
    const auto groupOf=[&](const wchar_t* name) { return s.rowGroup[size_t(tab.Row(name))]; };
    for(const auto* child:{L"a.txt",L"b.txt",L"sub",L"deep.txt"})
        Check(groupOf(child)==groupOf(L"alpha"),"a child left its folder's group");
    Check(groupOf(L"zz.txt")==groupOf(L"gamma") && groupOf(L"zz.txt")!=groupOf(L"zeta.txt"),
        "a child was grouped by its own name");
    for(const auto& section:s.sections)
        for(int i=section.first;i<section.end;++i)
            Check(s.rowGroup[size_t(i)]==int(&section-s.sections.data()),"a group is not one contiguous run");
    // Collapsing the folder's group hides its children too.
    ToggleGroupSection(s,groupOf(L"alpha")); StopRowTransition(s);
    while(s.groupAnimating) AdvanceGroupAnimation(s);
    Check(!DisplayedRow(s,tab.Row(L"deep.txt")) && !DisplayedRow(s,tab.Row(L"a.txt")),"a collapsed group still shows children");
    Check(DisplayedRow(s,tab.Row(L"zz.txt")),"another group's children were hidden");
    // Regrouping re-lists the open folder; the tree stays under its folders.
    s.grouping.enabled=false;
    QueueDirectoryRefresh(s,true);
    names=WaitForComplete(*s.background,s.directoryToken);
    PublishDirectoryResult(*s.background,std::move(names)); ReceiveDirectoryResults(s); StopRowTransition(s);
    Check(s.sections.empty() && s.total==9 && tab.Row(L"deep.txt")==tab.Row(L"sub")+1,"ungrouping lost the tree");
    std::cout<<"PASS: in a grouped folder, expanded children stay under their folder in its group and collapse with it\n";
}

// A folder that refuses listing reports Windows' own error text instead of
// "empty". The deny entry is on a private fixture folder and is removed again.
static void TestInlineDirsAccessDenied(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    OptionScope option(true);
    const auto root=JoinPath(fixture,L"inline-denied");
    MakeFolder(root);
    const auto locked=JoinPath(root,L"locked");
    MakeFolder(locked);
    PACL original=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
    Check(GetNamedSecurityInfoW(locked.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
        nullptr,nullptr,&original,nullptr,&descriptor)==ERROR_SUCCESS,"read fixture ACL");
    HANDLE token=nullptr;
    Check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token),"process token");
    DWORD size=0; GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<BYTE> user(size);
    Check(GetTokenInformation(token,TokenUser,user.data(),size,&size),"token user");
    CloseHandle(token);
    EXPLICIT_ACCESSW deny{};
    deny.grfAccessPermissions=FILE_LIST_DIRECTORY;
    deny.grfAccessMode=DENY_ACCESS;
    deny.Trustee.TrusteeForm=TRUSTEE_IS_SID;
    deny.Trustee.TrusteeType=TRUSTEE_IS_USER;
    deny.Trustee.ptstrName=reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
    PACL denied=nullptr;
    Check(SetEntriesInAclW(1,&deny,original,&denied)==ERROR_SUCCESS,"deny entry");
    Check(SetNamedSecurityInfoW(const_cast<LPWSTR>(locked.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
        nullptr,nullptr,denied,nullptr)==ERROR_SUCCESS,"apply deny entry");
    struct Restore {
        const std::wstring& path; PACL acl; PACL denied; PSECURITY_DESCRIPTOR descriptor;
        ~Restore() {
            SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
                nullptr,nullptr,acl,nullptr);
            LocalFree(denied); LocalFree(descriptor);
        }
    } restore{locked,original,denied,descriptor};
    {
        Tab tab(root,96);
        auto& s=tab.state;
        tab.Toggle(L"locked");
        const auto& row=s.entries[size_t(tab.Row(L"locked"))];
        Check(row.treeExpanded && row.treeEmpty && row.treeError==ERROR_ACCESS_DENIED,
            "a refused listing was reported as an empty folder");
        wchar_t* buffer=nullptr;
        const DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,ERROR_ACCESS_DENIED,0,reinterpret_cast<wchar_t*>(&buffer),0,nullptr);
        std::wstring expected=length && buffer ? std::wstring(buffer,length) : std::wstring();
        if(buffer) LocalFree(buffer);
        while(!expected.empty() && iswspace(expected.back())) expected.pop_back();
        Check(!expected.empty() && *InlineTreeErrorCaption(ERROR_ACCESS_DENIED)==expected,
            "the caption is not Windows' own text for the error");
    }
    std::cout<<"PASS: a folder that denies listing shows the system's localized error text, not \"empty\"\n";
}

// The space before a row's first control is one fixed DPI gap at any zoom:
// edge to checkbox, checkbox to arrow (or icon), with and without inline folders.
static void TestInlineDirsLeadingGap(const std::wstring& fixture,UINT dpi) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"leading gap fonts");
    const auto root=MakeTree(fixture,L"inline-leading");
    const float gap=float(MulDiv(16,int(dpi),96));
    const float checkbox=float(MulDiv(20,int(dpi),168));
    for(bool rtl:{false,true}) for(int size:{16,48}) for(bool tree:{false,true}) for(bool boxes:{false,true}) {
        Regression::Scope scope("leading dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+
            " icon="+std::to_string(size)+" tree="+std::to_string(tree)+" checkboxes="+std::to_string(boxes));
        OptionScope option(tree);
        Tab tab(root,dpi,rtl,size);
        auto& s=tab.state;
        s.clickPolicy.checkSelect=boxes;
        s.checkboxRevealStarted={};
        const auto item=tab.Item(tab.Row(L"zeta.txt"));
        const float start=NameColumnStart(s);
        float first=item.icon.left;
        if(tree) first=item.icon.left-s.iconPx*.42f-ExpectedChevronBox(CapHeight(s.textFormat.Get()),dpi);
        if(boxes) {
            Check(Near(item.checkbox.left-start,gap),"edge to checkbox is not the fixed gap");
            Check(Near(item.checkbox.right-item.checkbox.left,checkbox),"checkbox size");
            Check(Near(first-item.checkbox.right,gap),"checkbox to the next control is not the same gap");
        } else Check(Near(first-start,gap),"edge to the first control is not the fixed gap");
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: rows start one fixed DPI gap from the edge, and the checkbox keeps the same gap to the arrow or icon, "
        "at two zoom levels, with and without inline folders, LTR/RTL, "<<dpi<<" DPI\n";
}

// Tree keys: Right opens a folder, then moves into it; Left on a child moves
// to its folder, then closes it. With nothing focused, arrows keep their usual
// first-press behavior.
static void TestInlineDirsKeyboard(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    OptionScope option(true);
    const auto root=MakeTree(fixture,L"inline-keys");
    for(bool rtl:{false,true}) {
        Regression::Scope scope(rtl ? "keys rtl" : "keys ltr");
        Tab tab(root,96,rtl);
        auto& s=tab.state;
        const UINT forward=rtl ? VK_LEFT : VK_RIGHT, back=rtl ? VK_RIGHT : VK_LEFT;
        s.selected=-1; s.selectedFlags.Fill(0);
        NavigateKeyboardSelection(s,forward,false,false);
        Check(s.selected>=0,"first arrow press with nothing focused did nothing");
        Select(s,tab.Row(L"alpha"),true);
        NavigateKeyboardSelection(s,forward,false,false); tab.Settle();
        Check(s.total==6 && s.entries[size_t(tab.Row(L"alpha"))].treeExpanded,"Right did not open the folder");
        NavigateKeyboardSelection(s,forward,false,false);
        Check(s.selected==tab.Row(L"alpha")+1,"Right on an open folder did not move to its first item");
        NavigateKeyboardSelection(s,back,false,false);
        Check(s.selected==tab.Row(L"alpha"),"Left on a child did not move to its folder");
        NavigateKeyboardSelection(s,back,false,false); tab.Settle();
        Check(s.total==3,"Left on an open folder did not close it");
    }
    std::cout<<"PASS: Right/Left open, enter, leave and close inline folders (mirrored in RTL); first arrow keeps its behavior\n";
}

// With "Always on", metadata cells are shortened in the middle like
// filenames; otherwise they keep end trimming. DirectWrite's own hit testing
// says which characters are hidden.
static void TestDetailsMetadataMiddleEllipsis(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"ellipsis fonts");
    const auto root=JoinPath(fixture,L"metadata-ellipsis");
    MakeFolder(root);
    MakeFile(JoinPath(root,L"x.txt"));
    const auto previous=g_middleEllipsis.load();
    {
        Tab tab(root,96);
        auto& s=tab.state;
        const std::wstring value=L"Microsoft PowerPoint Presentation (PPTX)";
        const float width=ColumnTextWidth(s,value,false)*.5f;
        for(auto mode:{ViewOption::AlwaysOn,ViewOption::GridOnly,ViewOption::AlwaysOff}) {
            Regression::Scope scope(mode==ViewOption::AlwaysOn ? "alwaysOn" : mode==ViewOption::GridOnly ? "gridOnly" : "alwaysOff");
            g_middleEllipsis=mode;
            auto* layout=ResolveTextLayout(s,value,width,s.rowPx);
            Check(layout!=nullptr,"metadata layout");
            const auto visible=[&](UINT32 position) {
                float x=0.f,y=0.f; DWRITE_HIT_TEST_METRICS hit{};
                Check(SUCCEEDED(layout->HitTestTextPosition(position,FALSE,&x,&y,&hit)),"hit test");
                return !hit.isTrimmed && hit.left+hit.width<=width+.5f;
            };
            const bool lastShown=visible(UINT32(value.size()-1));
            Check(visible(0),"the start of a shortened value must stay visible");
            Check(lastShown==(mode==ViewOption::AlwaysOn),
                "metadata ellipsis position does not follow the middle-ellipsis option");
        }
        // Filenames keep following the option (grid only means end trimming in Details).
        g_middleEllipsis=ViewOption::GridOnly;
    }
    g_middleEllipsis=previous;
    g_writeFactory.Reset();
    std::cout<<"PASS: Details metadata is shortened in the middle only with middle ellipsis Always on\n";
}

// Touch: the folder icon toggles too (a finger is wider than the arrow); a
// mouse keeps the icon for selection. Type-to-select finds shown children.
static void TestInlineDirsTouchAndTypeSearch(const std::wstring& fixture) {
    using namespace InlineDirs;
    StyleFixture::Session style;
    OptionScope option(true);
    const auto root=MakeTree(fixture,L"inline-touch");
    Tab tab(root,96);
    auto& s=tab.state;
    const int alpha=tab.Row(L"alpha");
    const auto item=tab.Item(alpha);
    const POINT icon{LONG((item.icon.left+item.icon.right)*.5f),LONG((item.icon.top+item.icon.bottom)*.5f)};
    s.pointingDevice=PointingDevice::Mouse;
    Check(!InlineTreeExpanderHit(s,alpha,icon),"a mouse click on the folder icon toggles the folder");
    s.pointingDevice=PointingDevice::Touch;
    Check(InlineTreeExpanderHit(s,alpha,icon),"a touch tap on the folder icon does not toggle the folder");
    const POINT name{LONG(item.text.left+4.f),icon.y};
    Check(!InlineTreeExpanderHit(s,alpha,name),"a touch tap on the name toggles the folder");
    s.pointingDevice=PointingDevice::Mouse;
    tab.Toggle(L"alpha"); tab.Toggle(L"sub");
    Select(s,tab.Row(L"zeta.txt"),true);
    s.typeSearch.clear();
    s.typeSearch=L"dee";
    ApplyTypeSearch(s,L'e');
    Check(s.selected==tab.Row(L"deep.txt"),"type-to-select did not reach a shown child");
    std::cout<<"PASS: touch taps on folder icons toggle (mouse clicks select); type-to-select finds shown children\n";
}
