// Real virtual archive IDataObject, private clipboard stand-in, intercepted
// Shell commands and substituted focus. Never copy into the user's clipboard.
namespace PasteFocusFixture {
struct Target {
    ViewState state;
    HWND frame = nullptr;
    ComPtr<SelectionFolderView> model;
    ComPtr<SelectionCommandView> view;
    ComPtr<SelectionCommandMenu> selection, background;
    explicit Target(const std::wstring& path) {
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = DefWindowProcW; cls.lpszClassName = L"CabinetWClass"; RegisterClassW(&cls);
        frame = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls.lpszClassName, L"private paste focus",
            WS_POPUP | WS_VISIBLE, -32000, -32000, 700, 500, nullptr, nullptr, cls.hInstance, nullptr);
        state.hwnd = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 700, 500, frame, nullptr, cls.hInstance, nullptr);
        state.nativeHwnd = CreateWindowExW(0, L"STATIC", L"native command target", WS_CHILD | WS_VISIBLE,
            0, 0, 700, 500, frame, nullptr, cls.hInstance, nullptr);
        Check(frame && state.hwnd && state.nativeHwnd, "paste focus windows");
        state.frameHwnd = state.activeTabHost = frame; state.background->hwnd = state.hwnd;
        state.visible = state.inScope = state.detailsViewActive = state.loaded = true;
        state.folderPath = path;
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(+[](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)->LRESULT {
            if (msg == WM_KEYDOWN || msg == WM_KEYUP) return OverlayProc(hwnd, msg, wp, lp);
            return DefWindowProcW(hwnd, msg, wp, lp);
        }));
        RegisterTabOverlay(frame, frame, state.hwnd);
        SetPropW(state.nativeHwnd, kOverlayProperty, state.hwnd);
        selection.Attach(new SelectionCommandMenu); background.Attach(new SelectionCommandMenu);
        view.Attach(new SelectionCommandView(selection.Get())); view->window = state.nativeHwnd;
        view->backgroundMenu = background;
        model.Attach(new SelectionFolderView); model->state = &state; model->commandView = view;
        state.folderView = model;
    }
    ~Target() {
        CancelNativeSelectionPreparation(state); UnregisterAnimationWindow(state.hwnd);
        UnregisterTabOverlay(frame, frame, state.hwnd);
        RemovePropW(state.nativeHwnd, kOverlayProperty);
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
        DestroyWindow(frame); Stop(state.background);
    }
};
ComPtr<IDataObject> ArchiveData(const std::filesystem::path& archive) {
    ProviderSelectionFixture::WriteZip(archive, ProviderSelectionFixture::ZipBytes({{"copied.txt", "private virtual archive data"}}));
    ComPtr<IShellItem> item; ComPtr<IShellFolder> folder; ComPtr<IEnumIDList> enumeration;
    Check(SUCCEEDED(SHCreateItemFromParsingName(archive.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder))) &&
        SUCCEEDED(folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &enumeration)), "archive clipboard provider");
    PITEMID_CHILD raw = nullptr;
    Check(enumeration && enumeration->Next(1, &raw, nullptr) == S_OK && raw, "archive clipboard child");
    OwnedPidl child(raw); PCUITEMID_CHILD children[]{child.get()};
    ComPtr<IDataObject> data;
    Check(SUCCEEDED(folder->GetUIObjectOf(nullptr, 1, children, IID_IDataObject, nullptr,
        reinterpret_cast<void**>(data.GetAddressOf()))) && data, "archive virtual data object");
    return data;
}
void Consume(IDataObject* data) {
    ComPtr<IShellItemArray> items; ComPtr<IShellItem> item; DWORD count = 0;
    Check(data && SUCCEEDED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&items))) &&
        SUCCEEDED(items->GetCount(&count)) && count == 1 && SUCCEEDED(items->GetItemAt(0, &item)),
        "paste lost the archive's Shell data object");
    SFGAOF attributes = 0;
    Check(SUCCEEDED(item->GetAttributes(SFGAO_FILESYSTEM, &attributes)) && !(attributes & SFGAO_FILESYSTEM),
        "archive clipboard test unexpectedly used a physical file");
    ComPtr<IStream> stream;
    Check(SUCCEEDED(item->BindToHandler(nullptr, BHID_Stream, IID_PPV_ARGS(&stream))), "archive clipboard stream");
    char bytes[64]{}; ULONG read = 0;
    Check(SUCCEEDED(stream->Read(bytes, sizeof(bytes), &read)) && std::string(bytes, read) == "private virtual archive data",
        "archive deferred data did not survive frame-targeted paste");
}
}

static void TestPasteFocus(const std::wstring& fixture) {
    using namespace PasteFocusFixture;
    const auto root = std::filesystem::path(fixture) / L"paste-focus";
    std::filesystem::create_directory(root);
    const auto archive = root / L"source.zip";
    auto sourceData = ArchiveData(archive);
    Consume(sourceData.Get());
    const auto foreground = GetForegroundWindow(), focus = GetFocus();
    const auto sequence = testClipboard.sequence;
    const auto empties = testClipboard.empties;
    struct Restore {
        decltype(g_dispatchOriginal) dispatch = g_dispatchOriginal;
        decltype(g_translateAcceleratorOriginal) accelerator = g_translateAcceleratorOriginal;
        ~Restore() { g_dispatchOriginal = dispatch; g_translateAcceleratorOriginal = accelerator; }
    } restore;
    g_dispatchOriginal = DispatchMessageW;
    g_translateAcceleratorOriginal = +[](HWND, HACCEL, LPMSG)->int { return 0; };
    {
        Target source(archive.wstring()), target(root.wstring());
        ComPtr<IDataObject> clipboard;
        source.state.entries.resize(1); source.state.total = 1; source.state.selected = source.state.anchor = 0;
        source.state.selectedFlags.assign(1, 1);
        source.selection->duringInvoke = [&] {
            Check(source.selection->calls.back().first == L"copy", "archive copy changed its selected verb");
            clipboard = sourceData;
        };
        std::function<void()> reenter;
        target.background->duringInvoke = [&] {
            Check(target.background->calls.back().first == L"paste" && target.state.nativeFileCommandDepth,
                "frame paste did not hold the native command lifetime");
            Consume(clipboard.Get());
            if (reenter) reenter();
        };
        FocusFixture::Session session(source.frame, source.frame);
        ModifierFixture::Session modifierSession;
        auto& modifiers = modifierSession.keys; modifiers[VK_CONTROL] = 0x80;
        MSG key{}; key.hwnd = source.frame; key.message = WM_KEYDOWN; key.wParam = 'C';
        key.time = 100; key.lParam = 1 | (MapVirtualKeyW('C',MAPVK_VK_TO_VSC) << 16);
        const auto release = [&] {
            MSG up = key; up.message = WM_KEYUP; up.lParam |= (1LL << 30) | (1LL << 31);
            TranslateAcceleratorHook(GetAncestor(up.hwnd,GA_ROOT),nullptr,&up); DispatchHook(&up);
        };
        const auto press = [&](HWND hwnd, WPARAM code = 'V') {
            release(); key.hwnd=hwnd; key.wParam=code; ++key.time;
            key.lParam=1 | (MapVirtualKeyW(static_cast<UINT>(code),MAPVK_VK_TO_VSC) << 16);
        };
        const int copied = TranslateAcceleratorHook(source.frame, nullptr, &key);
        if (copied != TRUE || clipboard != sourceData)
            std::cout << "PASTE COPY handled=" << copied << " resolves=" << source.view->resolves
                << " invokes=" << source.selection->calls.size() << " selected=" << source.state.selected
                << " ready=" << ShellRowsInteractive(source.state) << " focus=" << OverlayHasInputFocus(source.state) << std::endl;
        Check(copied == TRUE && clipboard == sourceData,
            "archive copy never produced the private virtual clipboard data");
        source.state.pendingNativeSelection=99;
        press(source.frame,'X');
        TranslateAcceleratorHook(source.frame,nullptr,&key);
        TranslateAcceleratorHook(source.frame,reinterpret_cast<HACCEL>(1),&key); DispatchHook(&key);
        Check(source.state.deferredSelectionKeys.size()==1 && source.state.deferredSelectionKeys.front().key=='X' &&
            source.selection->calls.size()==1,"one pending-selection key queued multiple native commands");
        source.state.pendingNativeSelection=0; source.state.deferredSelectionKeys.clear();
        release();
        FocusFixture::foreground = FocusFixture::owner = FocusFixture::focus = target.frame;
        press(target.frame);
        const auto before = target.background->calls.size();
        target.view->duringResolve=[&] {
            Check(target.view->resolves==1,"reentrant input resolved another native Paste menu");
            Check(TranslateAcceleratorHook(target.frame,reinterpret_cast<HACCEL>(4),&key)==TRUE,
                "input was not reserved before native menu resolution");
            DispatchHook(&key);
            Check(target.background->calls.size()==before,"menu resolution reentered native Paste");
        };
        bool reentered=false;
        reenter = [&] {
            Check(!reentered,"one input reentered the native Paste command"); reentered=true;
            Check(TranslateAcceleratorHook(target.frame,reinterpret_cast<HACCEL>(3),&key)==TRUE,
                "reentrant accelerator did not consume the active key");
            DispatchHook(&key);
            // Mirrors a forwarded WndProc while the command pumps a loop.
            SendMessageW(target.state.hwnd,WM_KEYDOWN,key.wParam,key.lParam);
            Check(target.background->calls.size()==before+1,"reentrant input repeated native Paste");
        };
        Check(TranslateAcceleratorHook(target.frame, nullptr, &key) == TRUE && target.background->calls.size() == before + 1,
            "Ctrl+V with destination frame focus never reached native Paste");
        reenter={};
        target.view->duringResolve={};
        const MSG original=key;
        TranslateAcceleratorHook(target.frame, reinterpret_cast<HACCEL>(1), &key);
        TranslateAcceleratorHook(target.frame, reinterpret_cast<HACCEL>(2), &key);
        DispatchHook(&key);
        Check(target.background->calls.size() == before + 1, "one frame Ctrl+V invoked Paste more than once across accelerator tables/dispatch");
        Check(key.hwnd==original.hwnd && key.message==original.message && key.wParam==original.wParam &&
            key.lParam==original.lParam && key.time==original.time,"file command mutated Explorer's MSG");
        // One key release permits another press at the exact same timestamp.
        release();
        Check(TranslateAcceleratorHook(target.frame,nullptr,&key)==TRUE && target.background->calls.size()==before+2,
            "input deduplication swallowed a fresh press in the same tick");
        DispatchHook(&key);
        Check(target.background->calls.size()==before+2,"fresh press duplicated at dispatch");
        key.lParam = 1LL << 30;
        TranslateAcceleratorHook(target.frame, nullptr, &key); DispatchHook(&key); key.lParam = 0;
        Check(target.background->calls.size() == before + 2, "held Ctrl+V repeated a native paste");
        press(target.frame);
        DispatchHook(&key); DispatchHook(&key);
        TranslateAcceleratorHook(target.frame,nullptr,&key);
        Check(target.background->calls.size()==before+3,"dispatch-only frame input was lost or repeated");
        ++key.time; // Release can occur in another app; a new queued input still works.
        TranslateAcceleratorHook(target.frame,nullptr,&key); DispatchHook(&key);
        Check(target.background->calls.size()==before+4,"a missing frame keyup stranded future Paste");
        // Folder Paste is independent of selected items and names/selection
        // readiness, including a selected subfolder and a pending PIDL import.
        target.state.entries.resize(1); target.state.entries[0].folder = true;
        target.state.total = 1; target.state.selected = target.state.anchor = 0;
        target.state.selectedFlags.assign(1, 1);
        target.state.loaded = false; target.state.namesPreview = true; target.state.pendingNativeSelection = 99;
        target.state.nativeSelectionUnsafe = target.state.nativeSelectionImportPending = target.state.nativeSelectionAwaitingRefresh = true;
        const auto commits = target.model->commits;
        press(target.frame);
        Check(TranslateAcceleratorHook(target.frame, nullptr, &key) == TRUE && target.background->calls.size() == before + 5 &&
            target.selection->calls.empty() && target.model->commits == commits && target.state.deferredSelectionKeys.empty(),
            "Paste targeted a selected folder, required selection readiness, or queued with selected items");
        target.state.pendingNativeSelection = 0;
        FocusFixture::focus = target.state.hwnd;
        press(target.state.hwnd);
        Check(TranslateAcceleratorHook(target.frame, nullptr, &key) == TRUE && target.background->calls.size() == before + 6,
            "GPU-targeted Paste escaped the accelerator hook");
        DispatchHook(&key);
        Check(target.background->calls.size() == before + 6, "GPU-targeted Paste repeated at dispatch");
        release();
        // Use an actual private queue message to check WndProc's GetMessageTime
        // identity, without any live focus or clipboard operation.
        Check(PostMessageW(target.state.hwnd,WM_KEYDOWN,'V',key.lParam),"private Paste queue");
        Check(PeekMessageW(&key,target.state.hwnd,WM_KEYDOWN,WM_KEYDOWN,PM_REMOVE),"private Paste dequeue");
        g_dispatchOriginal(&key);
        TranslateAcceleratorHook(target.frame,nullptr,&key); DispatchHook(&key);
        Check(target.background->calls.size()==before+7,"window-first Paste duplicated when hooks saw the same queued input");
        release();
        const MSG targetInput=key;
        FocusFixture::foreground=FocusFixture::owner=FocusFixture::focus=source.frame;
        press(source.frame);
        key.time=targetInput.time;
        source.background->duringInvoke=[&] { Consume(clipboard.Get()); };
        Check(TranslateAcceleratorHook(source.frame,nullptr,&key)==TRUE && source.background->calls.size()==1,
            "another tab could not consume its own Paste input");
        FocusFixture::foreground=FocusFixture::owner=FocusFixture::focus=target.frame;
        key=targetInput; key.hwnd=target.frame;
        // Restore the window-first claim after release, then check isolation
        // with the very same message signature in both tabs.
        const auto targetCalls=target.background->calls.size();
        TranslateAcceleratorHook(target.frame,nullptr,&key); DispatchHook(&key);
        Check(target.background->calls.size()==targetCalls+1,"another tab consumed this tab's new press");
        const HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 30, 20, target.frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(edit != nullptr, "paste editor guard fixture");
        const auto untouched = [&] {
            ++key.time;
            const auto calls = target.background->calls.size();
            Check(TranslateAcceleratorHook(target.frame, nullptr, &key) == 0, "Paste intercepted an editor/inactive/native context");
            DispatchHook(&key);
            Check(target.background->calls.size() == calls, "Paste guard invoked a native file operation");
        };
        FocusFixture::focus = edit; untouched(); FocusFixture::focus = target.frame;
        FocusFixture::foreground = source.frame; untouched(); FocusFixture::foreground = target.frame;
        FocusFixture::ownerEnabled = false; untouched(); FocusFixture::ownerEnabled = true;
        target.state.rename.editor = edit; untouched(); target.state.rename.editor = nullptr;
        target.state.activeTabHost = edit; untouched(); target.state.activeTabHost = target.frame;
        target.state.visible = false; untouched(); target.state.visible = true;
        target.state.inScope = false; untouched(); target.state.inScope = true;
        target.state.optimisticStartedAt = 1; untouched(); target.state.optimisticStartedAt = 0;
        target.state.detailsViewActive = false; untouched(); target.state.detailsViewActive = true;
        modifiers[VK_SHIFT] = 0x80; untouched(); modifiers[VK_SHIFT] = 0;
        modifiers[VK_MENU] = 0x80; untouched(); modifiers[VK_MENU] = 0;
        modifiers[VK_CONTROL] = 0; untouched(); modifiers[VK_CONTROL] = 0x80;
        key.hwnd = target.state.nativeHwnd; FocusFixture::focus = target.state.nativeHwnd; untouched();
        key.hwnd = target.frame; FocusFixture::focus = target.frame;
        const auto token = target.state.token;
        const auto calls = target.background->calls.size();
        target.view->navigateDuringResolve = &target.state;
        press(target.frame);
        Check(TranslateAcceleratorHook(target.frame, nullptr, &key) == TRUE && target.background->calls.size() == calls,
            "native Paste menu resolution invoked a departed folder's command");
        target.view->navigateDuringResolve = nullptr;
        target.state.token = token;
        DispatchHook(&key);
        Check(target.background->calls.size()==calls,"a consumed stale-view key retried after navigation");
        target.view->window = source.state.nativeHwnd;
        press(target.frame);
        Check(TranslateAcceleratorHook(target.frame, nullptr, &key) == TRUE && target.background->calls.size() == calls,
            "Paste invoked a different native view's folder");
        target.view->window = target.state.nativeHwnd;
        press(target.frame);
        Check(TranslateAcceleratorHook(target.frame,nullptr,&key)==TRUE && target.background->calls.size()==calls+1,
            "stale native view failure blocked the next valid Paste");
        ++target.state.token;
        target.state.visible=false; FocusFixture::focus=edit; modifiers[VK_CONTROL]=0;
        Check(TranslateAcceleratorHook(target.frame,nullptr,&key)==TRUE,"consumed input fell through after focus/navigation changes");
        DispatchHook(&key);
        Check(target.background->calls.size()==calls+1,"departing-folder input invoked another Paste");
        target.state.token=token; target.state.visible=true; FocusFixture::focus=target.frame; modifiers[VK_CONTROL]=0x80;
        Check(FocusFixture::focus == target.frame && FocusFixture::sets == 0,
            "Paste moved focus instead of routing the command");
        DestroyWindow(edit);
    }
    sourceData.Reset(); StopTabVisibilityObserver();
    Check(GetForegroundWindow() == foreground && GetFocus() == focus && testClipboard.sequence == sequence && testClipboard.empties == empties,
        "paste test changed user focus or the clipboard");
    Check(std::filesystem::weakly_canonical(root).parent_path() == std::filesystem::weakly_canonical(fixture), "paste cleanup escaped private fixture");
    Check(Eventually([&] { std::error_code error; std::filesystem::remove_all(root, error); return !error; }), "paste fixture cleanup");
    std::cout << "PASS: one Paste per frame/GPU key across repeated accelerator tables, dispatch, WndProc and reentrant Shell callbacks; release/repress in one tick, dispatch-only and independent tabs; archive virtual data, loading/selection/focus/stale-view guards; no user clipboard/focus changes\n";
}
