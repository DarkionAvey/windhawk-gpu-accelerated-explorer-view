// Real Shell menu loops on a private desktop. No menu/site replacements,
// synthesized input messages, desktop switching or user's focus/clipboard.
namespace NativeMenuFixture {
struct Item {
    std::wstring text;
    UINT type = 0, state = 0;
    bool submenu = false;
    bool operator==(const Item&) const = default;
};
std::vector<Item> Read(HMENU menu, bool& valid) {
    std::vector<Item> result;
    const int count = GetMenuItemCount(menu);
    if (count < 0 || count > 512) { valid = false; return result; }
    for (int i = 0; i < count; ++i) {
        wchar_t text[1024]{};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU;
        info.dwTypeData = text; info.cch = ARRAYSIZE(text);
        if (!GetMenuItemInfoW(menu, i, TRUE, &info)) { valid = false; break; }
        // Unopened submenus contain provider placeholders until Windows sends
        // WM_INITMENUPOPUP. Compare only the actually displayed root here;
        // expanded submenu interaction belongs to the live mouse check.
        Item item{text, info.fType, info.fState & (MFS_DISABLED | MFS_CHECKED | MFS_DEFAULT), info.hSubMenu != nullptr};
        result.push_back(std::move(item));
    }
    return result;
}
inline thread_local struct Popup* observing = nullptr;
struct Popup {
    unsigned opened = 0;
    bool expired = false, dismissed = false, valid = true;
    RECT bounds{};
    std::vector<Item> items;
    HWND native, frame;
    UINT_PTR timer = 0;
    static LRESULT CALLBACK Owner(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
        // WM_ENTERIDLE comes from Windows' actual menu loop, after a popup is
        // visible. A call or success HRESULT alone is never counted as a menu.
        if (message == WM_ENTERIDLE && wp == MSGF_MENU && observing && lp) {
            const HWND window = reinterpret_cast<HWND>(lp);
            wchar_t type[32]{}; MENUBARINFO bar{sizeof(bar)};
            if (GetClassNameW(window, type, ARRAYSIZE(type)) && !wcscmp(type, L"#32768") &&
                IsWindowVisible(window) && GetMenuBarInfo(window, OBJID_CLIENT, 0, &bar) && bar.hMenu) {
                ++observing->opened;
                GetWindowRect(window, &observing->bounds);
                observing->items = Read(bar.hMenu, observing->valid);
                observing->dismissed = EndMenu() != FALSE;
            }
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    static void CALLBACK Timeout(HWND, UINT, UINT_PTR, DWORD) {
        if (observing) { observing->expired = true; EndMenu(); }
    }
    explicit Popup(HWND native, HWND frame) : native(native), frame(frame) {
        Check(!observing && !MenuFixture::owner, "real popup test must not substitute menu presentation");
        Check(SetWindowSubclass(native, Owner, 273, 0) && SetWindowSubclass(frame, Owner, 273, 0),
            "native menu observation hooks");
        timer = SetTimer(native, 0, 10000, Timeout); // Failure watchdog, never evidence of success.
        Check(timer != 0, "native popup watchdog");
        observing = this;
    }
    ~Popup() {
        KillTimer(native, timer); RemoveWindowSubclass(native, Owner, 273);
        RemoveWindowSubclass(frame, Owner, 273); observing = nullptr;
    }
    bool Displayed() const {
        return opened == 1 && !expired && dismissed && valid && !items.empty() &&
            bounds.right > bounds.left && bounds.bottom > bounds.top;
    }
};

// Exercise the active view's own command provider through its public menu-host
// API. Command offsets, property labels and resulting mode/icon sizes all come
// from Windows. No fabricated WM_COMMAND, replacement menu or desktop input.
struct Commands {
    ComPtr<IContextMenu> menu;
    ComPtr<IContextMenu3> messages;
    HMENU root = CreatePopupMenu();
    explicit Commands(ViewState& state) {
        ComPtr<IShellView> view;
        Check(root && SUCCEEDED(state.folderView.As(&view)) &&
            SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&menu))) &&
            SUCCEEDED(menu->QueryContextMenu(root, 0, 1, 0x7fff, CMF_NORMAL)), "native background command provider");
        menu.As(&messages);
    }
    ~Commands() { DestroyMenu(root); }
    static LRESULT CALLBACK Receive(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(msg==kMsgShellVisibilityChanged || msg==kMsgShellSelectionEvent)
            return OverlayProc(hwnd,msg,wp,lp);
        return DefSubclassProc(hwnd,msg,wp,lp);
    }
    template<class State> static void Bind(State& state) {
        if constexpr (requires { BindNativeViewInput(state.nativeHwnd,state.hwnd); })
            BindNativeViewInput(state.nativeHwnd,state.hwnd);
    }
    template<class State> static void Unbind(State& state) {
        if constexpr (requires { UnbindNativeViewInput(state.nativeHwnd,state.hwnd); })
            UnbindNativeViewInput(state.nativeHwnd,state.hwnd);
    }
    std::wstring Verb(UINT id) {
        wchar_t text[256]{};
        if (id && id != UINT(-1))
            menu->GetCommandString(id-1, GCS_VERBW, nullptr, reinterpret_cast<char*>(text), ARRAYSIZE(text));
        return text;
    }
    HMENU Submenu(const wchar_t* verb) {
        for (int i=0; i<GetMenuItemCount(root); ++i) {
            MENUITEMINFOW item{sizeof(item)}; item.fMask=MIIM_ID|MIIM_SUBMENU;
            Check(GetMenuItemInfoW(root,i,TRUE,&item), "native root menu item");
            if (!item.hSubMenu || Verb(item.wID)!=verb) continue;
            LRESULT result=0;
            Check(messages && SUCCEEDED(messages->HandleMenuMsg2(WM_INITMENUPOPUP,
                reinterpret_cast<WPARAM>(item.hSubMenu),MAKELPARAM(i,FALSE),&result)), "native lazy submenu population");
            return item.hSubMenu;
        }
        Check(false,"native command submenu missing"); return nullptr;
    }
    UINT FindVerb(const wchar_t* parent, const wchar_t* verb) {
        const HMENU sub=Submenu(parent);
        for (int i=0; i<GetMenuItemCount(sub); ++i) {
            const UINT id=GetMenuItemID(sub,i);
            if (Verb(id)==verb) return id;
        }
        for(int i=0;i<GetMenuItemCount(sub);++i)
            std::wcout<<L"MISSING VERB "<<verb<<L" candidate="<<Verb(GetMenuItemID(sub,i))<<std::endl;
        Check(false,"native canonical command missing"); return 0;
    }
    UINT FindRootVerb(const wchar_t* verb) {
        for(int i=0;i<GetMenuItemCount(root);++i) {
            MENUITEMINFOW item{sizeof(item)}; item.fMask=MIIM_ID;
            Check(GetMenuItemInfoW(root,i,TRUE,&item),"native root command");
            if(Verb(item.wID)==verb) return item.wID;
        }
        Check(false,"native root command missing"); return 0;
    }
    UINT FindProperty(ViewState& state, const wchar_t* parent, const PROPERTYKEY& key) {
        CM_COLUMNINFO column{sizeof(column)}; column.dwMask=CM_MASK_NAME;
        Check(state.columnManager && SUCCEEDED(state.columnManager->GetColumnInfo(key,&column)), "native property menu label");
        const HMENU sub=Submenu(parent);
        for (int i=0; i<GetMenuItemCount(sub); ++i) {
            wchar_t text[256]{}; GetMenuStringW(sub,i,text,ARRAYSIZE(text),MF_BYPOSITION);
            std::wstring label=text; std::erase(label,L'&');
            if (label==column.wszName) return GetMenuItemID(sub,i);
        }
        Check(false,"native property command missing"); return 0;
    }
    UINT FindUngroup(ViewState& state) {
        // Windows supplies no canonical verb for None. Identify its sole
        // non-property choice using the actual column catalog, not a caption
        // in a particular language or a private Shell command number.
        UINT count=0;
        Check(state.columnManager && SUCCEEDED(state.columnManager->GetColumnCount(CM_ENUM_ALL,&count)) &&
            count && count<=kMaxShellColumns,"native grouping column catalog");
        std::vector<PROPERTYKEY> keys(count);
        Check(SUCCEEDED(state.columnManager->GetColumns(CM_ENUM_ALL,keys.data(),count)),"native grouping column keys");
        std::vector<std::wstring> names;
        for(const auto& key:keys) {
            CM_COLUMNINFO column{sizeof(column)}; column.dwMask=CM_MASK_NAME;
            if(SUCCEEDED(state.columnManager->GetColumnInfo(key,&column))) {
                std::wstring name=column.wszName; std::erase(name,L'&'); names.push_back(std::move(name));
            }
        }
        const HMENU sub=Submenu(L"groupby"); UINT choice=0;
        for(int i=0;i<GetMenuItemCount(sub);++i) {
            wchar_t text[256]{}; MENUITEMINFOW item{sizeof(item)};
            item.fMask=MIIM_ID|MIIM_FTYPE|MIIM_STRING|MIIM_SUBMENU;
            item.dwTypeData=text; item.cch=ARRAYSIZE(text);
            Check(GetMenuItemInfoW(sub,i,TRUE,&item),"native grouping choice");
            std::wstring name=text; std::erase(name,L'&');
            if((item.fType&MFT_SEPARATOR) || item.hSubMenu || name.empty() || !Verb(item.wID).empty() ||
                std::find(names.begin(),names.end(),name)!=names.end()) continue;
            Check(!choice,"native grouping menu has ambiguous non-property choices"); choice=item.wID;
        }
        Check(choice!=0,"native Group by None command missing"); return choice;
    }
    void Invoke(ViewState& state, UINT id) {
        CMINVOKECOMMANDINFO invoke{sizeof(invoke)};
        invoke.hwnd=state.frameHwnd; invoke.lpVerb=MAKEINTRESOURCEA(id-1); invoke.nShow=SW_SHOWNORMAL;
        Check(id && SUCCEEDED(menu->InvokeCommand(&invoke)), "native menu command invocation");
    }
    static bool HasInk(ViewState& state,ID2D1Bitmap1* source) {
        Check(source!=nullptr,"group animation has no GPU heading source");
        ComPtr<ID2D1Bitmap1> cpu;
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            source->GetPixelFormat());
        Check(SUCCEEDED(state.target->CreateBitmap(source->GetPixelSize(),nullptr,0,&properties,&cpu)) &&
            SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"group animation heading readback");
        D2D1_MAPPED_RECT pixels{};
        Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&pixels)),"group animation heading map");
        const auto size=source->GetPixelSize(); bool ink=false;
        for(UINT y=0;y<size.height && !ink;++y) for(UINT x=0;x<size.width;++x)
            if(pixels.bits[y*pixels.pitch+x*4+3]) {ink=true; break;}
        cpu->Unmap(); return ink;
    }
};

void ExerciseCommands(ProviderSelectionFixture::Browser& host) {
    auto& state=host.state;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))) && EnsureTarget(state), "native command GPU target");
    Commands::Bind(state);
    Check(SetWindowSubclass(state.hwnd,Commands::Receive,302,0),"native command observation receiver");
    const auto settle=[&](bool finishMotion=true) {
        Check(Eventually([&] {
            host.Pump(); ReceiveDirectoryResults(state);
            return !state.nativeModeSignalQueued && !state.nativeViewCommandPending && !state.directoryPending;
        }),"native command observation/worker did not settle");
        if(finishMotion) { FinishColumnTransition(state); StopRowTransition(state); }
        const bool gpu=IsGpuViewMode(state.layoutNativeMode,state.thisPcGpu);
        Check(bool(IsWindowVisible(state.nativeHwnd))!=gpu && bool(IsWindowVisible(state.hwnd))==gpu,
            "native menu command broke exclusive native/GPU presentation");
    };
    const auto invoke=[&](const wchar_t* parent,const wchar_t* verb) {
        Commands command(state); command.Invoke(state,command.FindVerb(parent,verb)); settle();
    };
    // Exercise native commands with pre-sorting disabled as well. No GPU
    // header/frame command has primed this opt-out policy.
    state.preSortEnabled=false; state.sort=DefaultSort();
    state.sortInitialized=state.trackNativeSort=false;
    Check(!state.trackNativeSort && !state.sort.explicitOrder,"native menu regression needs an untouched GPU sort");
    for (auto mode : {FVM_DETAILS,FVM_LIST,FVM_ICON}) {
        Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(mode,48)),"native command initial mode");
        host.Pump(); FolderViewMode observed;
        Check(ReadNativeViewMode(state,observed),"native command initial mode read");
        ApplyViewLayout(state,observed,false); state.directoryModePinned=true;
        for (const auto* verb : {L"sortdescending",L"sortascending"}) {
            invoke(L"arrange",verb);
            SortSpec native;
            Check(ReadShellSort(state,native) && state.trackNativeSort && state.sort.explicitOrder &&
                SameSort(native,state.sort) && SameSort(native,state.displayedSort),"menu direction did not reach GPU sorting without a header click");
        }
        for (const auto& key : {PKEY_Size,PKEY_ItemNameDisplay}) {
            Commands command(state); command.Invoke(state,command.FindProperty(state,L"arrange",key)); settle();
            SortSpec native;
            Check(ReadShellSort(state,native) && SameKey(native.key,key) && SameSort(native,state.sort) &&
                std::is_sorted(state.entries.begin(),state.entries.end(),[&](const Entry& a,const Entry& b) {
                    return EntryPrecedes(a,b,state.sort);
                }),"menu property did not reorder the GPU entries");
        }
    }
    // Native mode and icon-size changes must bypass the pinned folder layout,
    // including rapid successive choices before the owner queue is dispatched.
    for (const auto* verb : {L"viewlogicallist",L"viewlogicaldetails",L"viewlogicaliconsmedium",
        L"viewlogicaliconslarge",L"viewlogicaliconssmall",L"viewlogicaliconsextralarge",L"viewlogicalcontent",
        L"viewlogicallist",L"viewlogicaltiles",L"viewlogicaldetails",L"viewlogicallist"}) {
        invoke(L"view",verb); FolderViewMode native;
        Check(ReadNativeViewMode(state,native) && state.layoutNativeMode==native.mode &&
            state.gridView==IsIconViewMode(native.mode,state.thisPcGpu) &&
            (!state.gridView || state.layoutIconSize==native.iconSize),"menu view/icon-size did not reach the pinned GPU layout");
    }
    {
        Commands command(state);
        command.Invoke(state,command.FindVerb(L"view",L"viewlogicaldetails"));
        command.Invoke(state,command.FindVerb(L"view",L"viewlogicaliconslarge"));
        settle(); FolderViewMode native;
        Check(ReadNativeViewMode(state,native) && state.gridView && state.layoutNativeMode==native.mode &&
            state.layoutIconSize==native.iconSize,"queued native choices lost the final layout");
    }
    Check(AttachShellSelectionEvents(state,nullptr) && state.selectionEventCookie,
        "native folder automation event subscription");
    for(const auto* mode : {L"viewlogicaldetails",L"viewlogicallist",L"viewlogicaliconsmedium"}) {
        invoke(L"view",mode);
        unsigned animatedSurvivors=0;
        const auto group=[&](const PROPERTYKEY* key,const wchar_t* direction=nullptr) {
            AnimationClockFixture::Freeze clock;
            DirectoryJob old; CaptureItemSnapshot(state,old);
            Check(!old.oldViewport.empty(),"grouping animation fixture has no displayed source items");
            const auto headers=CaptureVisibleGroupHeaders(state);
            const auto previousSort=state.sort;
            Commands command(state);
            command.Invoke(state,direction ? command.FindVerb(L"groupby",direction) :
                key ? command.FindProperty(state,L"groupby",*key) : command.FindUngroup(state));
            settle(false);
            ShellGroupSpec native;
            Check(TryReadShellGrouping(state,native) && bool(native.enabled)==bool(key) &&
                SameShellGroup(native,state.grouping) && SameShellGroup(native,state.displayedGrouping),
                "native grouping choice did not reach the displayed GPU layout");
            Check(state.rowTransitionActive && state.groupingLayoutTransition,
                "native grouping change skipped the shared item/header animation");
            unsigned survivors=0;
            for(const auto& motion:state.rowMotion) {
                const auto& entry=state.entries[motion.newIndex];
                const auto found=std::find_if(old.oldViewport.begin(),old.oldViewport.end(),
                    [&](const auto& row) {return row.entry.path==entry.path;});
                if(found==old.oldViewport.end()) continue;
                ++survivors;
                if(motion.entering || motion.oldOpacity<.99f)
                    std::cout<<"GROUP MOTION mode="<<state.layoutNativeMode<<" old-sort="<<previousSort.key.pid
                        <<" new-sort="<<state.sort.key.pid<<" entering="<<motion.entering<<" headers="<<headers.size()<<std::endl;
                Check(!motion.entering && motion.oldOpacity>.99f,
                    "grouping recreated a surviving item instead of animating its old position");
                const auto from=LayoutRowVisual(state,motion.newIndex,0.f);
                const auto middle=LayoutRowVisual(state,motion.newIndex,.5f);
                const auto to=LayoutRowVisual(state,motion.newIndex,1.f);
                Check(fabsf(from.top-found->oldTop)<.1f && fabsf(from.left-found->oldLeft)<.1f &&
                    fabsf(middle.top-(from.top+to.top)*.5f)<.1f &&
                    fabsf(middle.left-(from.left+to.left)*.5f)<.1f,
                    "grouping item motion lost its old position or intermediate geometry");
            }
            // A real sort reversal can replace the entire bounded viewport
            // in the larger delegated folder. Check every overlapping identity
            // and require overlap across the command sequence for each mode.
            animatedSurvivors+=survivors;
            for(const auto& header:headers) {
                const bool remains=std::any_of(state.sections.begin(),state.sections.end(),[&](const auto& next) {
                    return next.key==header.key && next.title==header.title;
                });
                if(!remains) Check(std::any_of(state.disappearingHeaders.begin(),state.disappearingHeaders.end(),[&](const auto& old) {
                    return old.key==header.key && old.title==header.title && fabsf(old.oldTop-header.oldTop)<.1f;
                }),"changing the grouping property discarded a visible outgoing heading");
            }
            for(const auto& motion:state.groupHeaderMotion) {
                const auto from=LayoutGroupHeader(state,motion.sectionIndex,0.f,false);
                const auto middle=LayoutGroupHeader(state,motion.sectionIndex,.5f,false);
                const auto to=LayoutGroupHeader(state,motion.sectionIndex,1.f,false);
                Check(fabsf(middle.height-(from.height+to.height)*.5f)<.1f &&
                    fabsf(middle.opacity-(from.opacity+to.opacity)*.5f)<.01f,
                    "group heading growth/fade skipped its intermediate geometry");
            }
            if(!key) Check(state.sections.empty() && state.disappearingHeaders.size()==headers.size() && !headers.empty(),
                "Group by None removed its headings without an outgoing animation");
            const auto model=CapturePresentationModel(state);
            Check(model->rowTransitionActive && model->groupingLayoutTransition && !model->rowMotion.empty(),
                "grouping motion was missing from the renderer's immutable model");
            ::QueryPerformanceCounter(&AnimationClockFixture::instant);
            const bool published=Eventually([&] {Paint(state); return state.modeComposition.rows &&
                state.modeComposition.published;});
            if(!published) std::cout<<"GROUP GPU mode="<<state.layoutNativeMode<<" active="<<state.rowTransitionActive
                <<" composition="<<bool(state.composition)<<" target="<<bool(state.compTarget)<<" backbuffer="<<bool(state.backbuffer)
                <<" swapchain="<<bool(state.swapChain)<<" columns="<<state.columnTransitionActive<<" zoom="<<state.zoomAnimating
                <<" collapse="<<state.groupAnimating<<" row-scene="<<state.modeComposition.rows
                <<" published="<<state.modeComposition.published<<" text="<<state.textRasterPending<<std::endl;
            Check(published,"grouping did not publish its GPU animation scene");
            Check(!state.modeComposition.items.empty() && !state.modeComposition.headings.empty() &&
                Commands::HasInk(state,state.modeComposition.groups.bitmap.Get()),
                "grouping item/header animation has empty GPU sources");
            if(!key) Check(std::any_of(state.modeComposition.headings.begin(),state.modeComposition.headings.end(),
                [](const auto& heading) {return heading.key<0;}),"Group by None lost its outgoing GPU heading visuals");
            AnimationClockFixture::instant.QuadPart+=static_cast<LONGLONG>(state.qpcFrequency*kUiTransitionDurationMs/1000.);
            Paint(state);
            Check(!state.rowTransitionActive,"grouping animation did not settle at the shared duration");
        };
        group(&PKEY_Size);
        group(&PKEY_Size,state.grouping.ascending ? L"groupdescending" : L"groupascending");
        group(&PKEY_ItemTypeText); group(nullptr);
        Check(animatedSurvivors>0,"grouping animation fixture had no visible surviving items");
    }
    for(const auto* verb : {L"viewlogicaldetails",L"viewlogicallist",L"viewlogicaliconsmedium"}) {
        invoke(L"view",verb);
        const auto epoch=state.detailsRefreshEpoch;
        const auto before=state.displayedOrderHash;
        Commands command(state); command.Invoke(state,command.FindRootVerb(L"refresh"));
        Check(Eventually([&] {
            host.Pump(); ReceiveDirectoryResults(state);
            return state.detailsRefreshEpoch>epoch && !state.directoryPending;
        }),"native menu Refresh did not revalidate GPU content and cached metadata");
        settle();
        Check(state.loaded && state.displayedOrderHash==before,
            "unchanged native menu Refresh lost or reordered GPU content");
    }
    DetachShellSelectionEvents(state);
    Commands::Unbind(state);
    RemoveWindowSubclass(state.hwnd,Commands::Receive,302);
    DropTarget(state);
    std::cout<<"PASS: native menus update Details/List, large/medium/small icons, sorting, grouping and Refresh; queued choices, native fallback and exclusive GPU presentation\n";
}

void Child(const std::wstring& fixture) {
    wchar_t desktop[256]{}; DWORD length = 0;
    Check(GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
        desktop, sizeof(desktop), &length) && std::wstring(desktop).starts_with(L"gpu-overlay-menu-"),
        "real popup tests must run on their private desktop");
    const auto root = std::filesystem::path(fixture) / L"native-menus";
    std::filesystem::create_directory(root);
    std::filesystem::create_directory(root / L"folder");
    HANDLE file = CreateFileW((root / L"file.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "native menu private file"); CloseHandle(file);
    const auto archive = std::filesystem::path(fixture) / L"menus.zip";
    ProviderSelectionFixture::WriteZip(archive, ProviderSelectionFixture::ZipBytes({
        {"file.txt", "private menu fixture"}, {"folder/", ""}}));
    const auto disk = std::filesystem::path(fixture) / L"menu-target";
    const auto alias = std::filesystem::path(fixture) / L"menu-alias";
    ProviderSelectionFixture::DiskAndAlias(disk, alias);
    unsigned trials = 0;
    for (const auto& location : {root, archive, alias}) {
    ProviderSelectionFixture::Browser host;
    auto& state = host.state;
    SetWindowLongPtrW(host.frame, GWL_EXSTYLE, GetWindowLongPtrW(host.frame, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
    SetWindowPos(host.frame, nullptr, 0, 0, 1000, 700, SWP_NOZORDER | SWP_SHOWWINDOW);
    SetForegroundWindow(host.frame); // Only this isolated desktop is affected.
    host.Navigate(location.wstring(), location == alias ? 99 : 2);
    ExerciseCommands(host);
    int fileRow = -1, folderRow = -1;
    for (int i = 0; i < state.total; ++i) {
        if (state.entries[i].folder) folderRow = i;
        else fileRow = i;
    }
    Check(fileRow >= 0 && folderRow >= 0, "menu provider fixture needs both a file and folder");
    ComPtr<IShellView> native;
    Check(SUCCEEDED(state.shellBrowser->QueryActiveShellView(&native)), "real popup native view");
    for (auto mode : {FVM_DETAILS, FVM_LIST, FVM_ICON}) {
        Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(mode, 48)), "real popup view mode");
        host.Pump();
        state.gridView = mode == FVM_ICON; state.columnsView = mode == FVM_DETAILS;
        state.layoutNativeMode = mode; UpdateMetrics(state);
        // Single file, single folder, mixed selection, then folder background.
        for (int target : {0, 1, 2, -1}) {
            if (target < 0) ClearSelectionForFolderMenu(state);
            else if (target == 2) {
                state.selectedFlags.Fill(0);
                state.selectedFlags[fileRow] = state.selectedFlags[folderRow] = 1;
                state.selected = state.anchor = folderRow;
                Check(SyncNativeSelection(state), "real popup multi-selection");
            } else Select(state, target == 0 ? fileRow : folderRow);
            Check(!state.pendingNativeSelection, "small popup selection unexpectedly deferred");
            const POINT point{300, 400};
            // Reference is an actual native popup, not a separately constructed
            // IContextMenu/HMENU. This includes view-owned Refresh/Group by.
            RestoreNativeContent(state);
            std::vector<Item> reference;
            {
                Popup popup(state.nativeHwnd, host.frame);
                NativeContextMenu(state, point, false);
                Check(popup.Displayed(), "visible native view handler did not open its reference popup");
                reference = popup.items;
            }
            Check(SuppressNativeContent(state), "restore suppression after native reference menu");
            SetPresentationVisibility(state.hwnd, true);
            Check(!IsWindowVisible(state.nativeHwnd), "real menu test did not suppress the native view");
            {
                Popup popup(state.nativeHwnd, host.frame);
                if (target < 0) BackgroundContextMenu(state, point);
                else ContextMenu(state, target == 0 ? fileRow : folderRow, point);
                std::cout << "ACTUAL MENU mode=" << mode << " target=" << target << " opened=" << popup.opened
                    << " expired=" << popup.expired << " items=" << popup.items.size() << std::endl;
                Check(popup.Displayed(), "right click did not display a real native popup with the native host suppressed");
                if (popup.items != reference) {
                    for (const auto& item : reference) std::wcout << L"REFERENCE " << item.text << L" state=" << item.state << L'\n';
                    for (const auto& item : popup.items) std::wcout << L"HIDDEN " << item.text << L" state=" << item.state << L'\n';
                }
                Check(popup.items == reference, "displayed popup commands/submenus/states differ with the native host hidden");
            }
            Check(!IsWindowVisible(state.nativeHwnd), "popup exposed the suppressed native list");
            int selected = -1;
            Check(SUCCEEDED(state.folderView->ItemCount(SVGIO_SELECTION, &selected)) &&
                selected == (target < 0 ? 0 : target == 2 ? 2 : 1), "popup changed the native selection");
            ++trials;
        }
    }
    }
    Check(SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_NORMAL), "restore private delegated directory attributes");
    std::cout << "PASS: " << trials << " displayed item and background popups; roots match visible native commands/states; native hosts stay hidden; private desktop\n";
}
}
static void TestNativeMenuInteraction() {
    const std::wstring name = L"gpu-overlay-menu-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    Check(desktop != nullptr, "private menu desktop creation");
    wchar_t station[256]{}; DWORD length = 0;
    Check(GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &length),
        "private menu window station");
    std::wstring destination = std::wstring(station) + L"\\" + name;
    std::wstring module(32768, L'\0');
    module.resize(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())));
    std::wstring command = L"\"" + module + L"\" --native-context-menu-child";
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    Check(CreatePipe(&read, &write, &attributes, 65536) && SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0),
        "private menu diagnostics pipe");
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{sizeof(startup)}; startup.lpDesktop = destination.data();
    startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = startup.hStdError = write; startup.hStdInput = input;
    PROCESS_INFORMATION child{};
    const BOOL created = CreateProcessW(module.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
    CloseHandle(write); if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (!created) { CloseHandle(read); CloseDesktop(desktop); Check(false, "private menu child creation"); }
    const DWORD waited = WaitForSingleObject(child.hProcess, 60000);
    DWORD result = DWORD(-1);
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &result);
    else TerminateProcess(child.hProcess, 125); // Test process only; never live Explorer/provider threads.
    DWORD available = 0, received = 0;
    char buffer[4096];
    while (PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available &&
        ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) && received)
        std::cout.write(buffer, received);
    CloseHandle(read);
    CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseDesktop(desktop);
    Check(waited == WAIT_OBJECT_0 && result == 0, "actual native menu interaction failed on the isolated desktop");
}
