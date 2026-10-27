// Private native provider hierarchy. Real enumeration, selection and default
// menu resolution; only InvokeCommand is captured, so no associated app opens.
namespace ProviderNavigationFixture {
class Menu final : public IContextMenu {
    ULONG refs = 1;
public:
    ComPtr<IContextMenu> native;
    unsigned calls = 0;
    UINT expected = UINT(-1);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IContextMenu) return E_NOINTERFACE;
        *out = static_cast<IContextMenu*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        const HRESULT hr = native->QueryContextMenu(menu, index, first, last, flags);
        expected = GetMenuDefaultItem(menu, FALSE, GMDI_GOINTOPOPUPS) - first;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR id, UINT flags, UINT* reserved, LPSTR text, UINT length) override {
        return native->GetCommandString(id, flags, reserved, text, length);
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
        const auto* command = reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(info);
        Check(command->cbSize == sizeof(*command) && IS_INTRESOURCE(command->lpVerb) &&
            LOWORD(command->lpVerb) == expected && !command->lpDirectoryW,
            "provider activation changed the native default or supplied a fictitious disk directory");
        ++calls; return S_OK;
    }
};
class View final : public AttachmentFolderView, public IShellView {
public:
    using AttachmentFolderView::SelectItem;
    ComPtr<IFolderView2> native;
    ComPtr<Menu> menu;
    HWND window = nullptr;
    explicit View(IFolderView2* value) : native(value) { value->GetFolder(IID_PPV_ARGS(&folder)); menu.Attach(new Menu); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IShellView || iid == IID_IOleWindow) {
            *out = static_cast<IShellView*>(this); AddRef(); return S_OK;
        }
        return AttachmentFolderView::QueryInterface(iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return AttachmentFolderView::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return AttachmentFolderView::Release(); }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out) override { *out = window; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetItemObject(UINT flags, REFIID iid, void** out) override {
        ComPtr<IShellView> view;
        Check(SUCCEEDED(native.As(&view)) && SUCCEEDED(view->GetItemObject(flags, IID_PPV_ARGS(&menu->native))),
            "provider native default menu unavailable");
        return menu->QueryInterface(iid, out);
    }
    HRESULT STDMETHODCALLTYPE ItemCount(UINT flags, int* out) override { return native->ItemCount(flags, out); }
    HRESULT STDMETHODCALLTYPE Item(int index, PITEMID_CHILD* out) override { return native->Item(index, out); }
    HRESULT STDMETHODCALLTYPE Items(UINT flags, REFIID iid, void** out) override { return native->Items(flags, iid, out); }
    HRESULT STDMETHODCALLTYPE GetSelection(BOOL pending, IShellItemArray** out) override { return native->GetSelection(pending, out); }
    HRESULT STDMETHODCALLTYPE GetFocusedItem(int* out) override { return native->GetFocusedItem(out); }
    HRESULT STDMETHODCALLTYPE SelectAndPositionItems(UINT count, PCUITEMID_CHILD_ARRAY children, POINT* points, DWORD flags) override {
        return native->SelectAndPositionItems(count, children, points, flags);
    }
#define UNUSED_PROVIDER_VIEW(name, ...) HRESULT STDMETHODCALLTYPE name(__VA_ARGS__) override { return E_NOTIMPL; }
    UNUSED_PROVIDER_VIEW(ContextSensitiveHelp, BOOL)
    UNUSED_PROVIDER_VIEW(TranslateAccelerator, MSG*)
    UNUSED_PROVIDER_VIEW(EnableModeless, BOOL)
    UNUSED_PROVIDER_VIEW(UIActivate, UINT)
    UNUSED_PROVIDER_VIEW(Refresh)
    UNUSED_PROVIDER_VIEW(CreateViewWindow, IShellView*, LPCFOLDERSETTINGS, IShellBrowser*, RECT*, HWND*)
    UNUSED_PROVIDER_VIEW(DestroyViewWindow)
    UNUSED_PROVIDER_VIEW(GetCurrentInfo, LPFOLDERSETTINGS)
    UNUSED_PROVIDER_VIEW(AddPropertySheetPages, DWORD, LPFNSVADDPROPSHEETPAGE, LPARAM)
    UNUSED_PROVIDER_VIEW(SaveViewState)
    UNUSED_PROVIDER_VIEW(SelectItem, PCUITEMID_CHILD, SVSIF)
#undef UNUSED_PROVIDER_VIEW
};
int Row(ViewState& state, const wchar_t* name) {
    for (int row = 0; row < state.total; ++row)
        if (state.entries[row].name == name) return row;
    Check(false, "provider navigation fixture row missing"); return -1;
}
SharedPidl Identity(ViewState& state, const wchar_t* name) {
    const auto identity = state.entries[Row(state, name)].shellIdentity;
    Check(bool(identity), "provider navigation lost the item's complete identity"); return identity;
}
void Activate(ProviderSelectionFixture::Browser& host, int row, bool selected) {
    auto& state = host.state;
    const auto original = state.folderView;
    ComPtr<View> capture; capture.Attach(new View(original.Get())); capture->window = state.nativeHwnd;
    state.folderView = capture;
    if (selected) Select(state, row);
    else {
        state.selected = state.anchor = -1; state.selectedFlags.Fill(0);
        Check(ClearNativeSelection(state), "provider activation initial deselection");
    }
    if (selected) OverlayProc(state.hwnd, WM_KEYDOWN, VK_RETURN, 0);
    else Open(state, row);
    Check(Eventually([&] { host.Pump(); return !state.pendingNativeSelection; }, 30000), "provider default action preparation stalled");
    if(capture->menu->calls!=1 || !state.selectedFlags[row] || state.nativeSelectionUnsafe)
        std::wcout<<L"PROVIDER ACTIVATE folder="<<state.folderPath<<L" row="<<row<<L" selected-route="<<selected
            <<L" item="<<state.entries[row].name<<L" calls="<<capture->menu->calls<<L" gpu="<<state.selected
            <<L" flag="<<int(state.selectedFlags[row])<<L" unsafe="<<state.nativeSelectionUnsafe
            <<L" dirty="<<state.nativeSelectionDirty<<L" importing="<<state.nativeSelectionImportPending
            <<L" recovering="<<state.nativeSelectionRecoveryPending<<L" pending="<<state.pendingNativeSelection
            <<L" keys="<<state.deferredSelectionKeys.size()<<L" provider="<<bool(state.namespaceIdentities)<<std::endl;
    Check(capture->menu->calls == 1 && state.selectedFlags[row] && !state.nativeSelectionUnsafe,
        "provider folder/stream activation never reached its selected native default command");
    Check(CaptureNativeSelection(state, true), "provider activation selection echo");
    Check(Eventually([&] { host.Pump(); return !state.pendingNativeSelection; }, 30000), "provider activation echo stalled");
    Check(state.selectedFlags[row], "provider activation echo lost the selected item");
    state.folderView = original;
}
void Destination(ProviderSelectionFixture::Browser& host, int expected) {
    auto& state = host.state;
    const auto location = state.folderPath;
    const auto token = state.directoryToken;
    if (UsesNamespaceFolderIdentity(location)) {
        Check(state.directoryFolderIdentity && state.directoryWatch && state.directoryWatch->registration &&
            state.dirChange == INVALID_HANDLE_VALUE && state.directoryWatch->path == location,
            "virtual provider folder lost its PIDL watch or attempted a disk watch on its opaque key");
        QueueDirectoryRefresh(state, false, true, true);
        Check(Eventually([&] { host.Pump(); ReceiveDirectoryResults(state); return !state.namespaceCheckPending; }, 30000),
            "provider namespace verification stalled");
        Check(state.directoryToken == token && state.total == expected,
            "unchanged provider verification replaced the snapshot");
        const auto hints = host.shellHints;
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, state.directoryFolderIdentity.get(), nullptr);
        const bool notified = Eventually([&] {
            host.Pump(); ReceiveDirectoryResults(state);
            return host.shellHints > hints && !state.watchedChangePending && !state.namespaceCheckPending;
        }, 10000);
        if (!notified) std::cout << "PROVIDER NOTIFY hints=" << host.shellHints-hints << " watch=" << state.directoryWatch->registration
            << " pending=" << state.watchedChangePending << " check=" << state.namespaceCheckPending << std::endl;
        Check(notified, "provider PIDL folder notification never completed verification");
        Check(state.directoryToken == token, "unchanged provider notification replaced its item snapshot");
    }
    if (!state.columnManager) state.folderView.As(&state.columnManager);
    state.columnsView = true; state.deferColumnProviders = false;
    Check(state.columnManager && RequestColumnCatalog(state), "provider column catalog request");
    Check(Eventually([&] { host.Pump(); ReceiveDirectoryResults(state); return state.columnCatalogReady; }, 30000),
        "provider column catalog attempted to parse an opaque folder key");
    SetActive(state.hwnd, false);
    Check(!state.visible && state.inScope && state.suppressedNativeHosts.empty(), "provider native toggle retained suppression");
    SetActive(state.hwnd, true);
    Check(state.visible && state.inScope && state.folderPath == location && !state.suppressedNativeHosts.empty(),
        "returning from native view detached the provider overlay");
    // No selection or default action may escape a departed folder generation.
    if (expected) {
        Select(state, 0);
        Check(CaptureNativeSelection(state, true) && state.pendingNativeSelection, "provider stale selection fixture");
        const auto old = state.pendingNativeSelection;
        ComPtr<IShellFolder> rebound = state.folder;
        ResetForNewDirectory(state, location, rebound, false, state.activeTabHost);
        Check(Eventually([&] { host.Pump(); return !state.pendingNativeSelection; }, 30000), "departed provider selection capture survived");
        Check(state.background->selectionGeneration.load() != old, "departed provider worker generation remained live");
    }
    QueueDirectoryRefresh(state, true);
    Check(Eventually([&] { host.Pump(); ReceiveDirectoryResults(state); return !state.directoryPending; }, 30000),
        "provider refresh after native toggle stalled");
    Check(state.loaded && state.total == expected && state.enumerationError == ERROR_SUCCESS,
        "provider refresh lost its folder identity");
    if (expected) {
        MenuFixture::Session tracking(state.hwnd);
        ContextMenu(state, 0, POINT{0, 0});
        Check(Eventually([&] { host.Pump(); return !state.pendingNativeSelection; }, 30000), "nested provider context menu stalled");
        Check(MenuFixture::calls == 1 && MenuFixture::items > 0, "nested provider context menu never reached its native menu");
    }
}
}

static void TestProviderNavigation(const std::wstring& fixture) {
    using namespace ProviderSelectionFixture;
    using namespace ProviderNavigationFixture;
    // Nested Shell pumps can replace SetKeyboardState's queue snapshot.
    // Keep synthetic Enter modifiers stable without touching desktop input.
    ModifierFixture::Session keyboard;
    for (const auto* known : {&FOLDERID_ComputerFolder, &FOLDERID_RecycleBinFolder, &FOLDERID_ControlPanelFolder}) {
        PIDLIST_ABSOLUTE raw = nullptr;
        Check(SUCCEEDED(SHGetKnownFolderIDList(*known, 0, nullptr, &raw)), "read-only namespace scope probe");
        OwnedPidl identity(raw);
        Check(IsSupportedLocation(FolderLocation(identity.get())) == (known != &FOLDERID_ControlPanelFolder),
            "filesystem-descendant support changed unrelated virtual-root scope");
    }
    const auto root = std::filesystem::path(fixture) / L"provider-navigation";
    std::filesystem::create_directory(root);
    const auto innerBytes = ZipBytes({{"dir/", ""}, {"dir/deeper/", ""}, {"dir/deeper/leaf.txt", "leaf"},
        {"dir/caf\xc3\xa9.txt", "unicode"}, {"empty/", ""}, {"third.zip", ZipBytes({{"inside/", ""}, {"inside/end.txt", "end"}})}});
    const auto outer = root / L"outer.zip";
    const auto materialized = root / L"native-materialized.zip";
    const auto third = root / L"third-materialized.zip";
    WriteZip(third, ZipBytes({{"inside/", ""}, {"inside/end.txt", "end"}}));
    WriteZip(materialized, innerBytes);
    WriteZip(outer, ZipBytes({{"inner.zip", innerBytes}, {"dir/", ""}, {"dir/deeper/", ""},
        {"dir/deeper/leaf.txt", "leaf"}, {"dir/caf\xc3\xa9.txt", "unicode"}, {"empty/", ""}}));
    {
        Browser host;
        FocusFixture::Session focus(host.frame, host.state.hwnd);
        host.Navigate(outer.wstring(), 3);
        Activate(host, Row(host.state, L"inner.zip"), false);
        const auto outerRoot = [&] { PIDLIST_ABSOLUTE raw = nullptr; SHGetIDListFromObject(host.state.folder.Get(), &raw); return SharedPidl(raw, TaskMemDeleter{}); }();
        const auto dir = Identity(host.state, L"dir");
        Activate(host, Row(host.state, L"dir"), true);
        host.NavigateIdentity(dir.get(), 2);
        Destination(host, 2);
        const auto deeper = Identity(host.state, L"deeper");
        Activate(host, Row(host.state, L"caf\u00e9.txt"), false);
        Activate(host, Row(host.state, L"deeper"), false);
        host.NavigateIdentity(deeper.get(), 1);
        Destination(host, 1);
        Activate(host, 0, true);
        host.NavigateIdentity(outerRoot.get(), 3);
        host.NavigateIdentity(Identity(host.state, L"empty").get(), 0);
        Destination(host, 0);
        host.NavigateIdentity(outerRoot.get(), 3);
        // Native default activation may materialize a stream before navigating.
        // Test its resulting provider independently, without extracting through
        // ShellExecute or opening a real associated app/Explorer frame.
        host.Navigate(materialized.wstring(), 3);
        Destination(host, 3);
        Activate(host, Row(host.state, L"third.zip"), true);
        host.NavigateIdentity(Identity(host.state, L"dir").get(), 2);
        host.NavigateIdentity(Identity(host.state, L"deeper").get(), 1);
        Activate(host, 0, false);
        host.Navigate(third.wstring(), 1);
        host.NavigateIdentity(Identity(host.state, L"inside").get(), 1);
        Destination(host, 1);
        Activate(host, 0, true);
        const auto target = root / L"target", alias = root / L"alias";
        DiskAndAlias(target, alias);
        host.Navigate(alias.wstring(), 99);
        Activate(host, Row(host.state, L"one"), false);
        host.NavigateIdentity(Identity(host.state, L"one").get(), 1);
        Check(!host.state.namespaceIdentities && IsTargetPath(host.state.folderPath),
            "leaving the delegated provider retained its namespace row map");
        host.Navigate(outer.wstring(), 3);
        Destination(host, 3);
        SetFileAttributesW(alias.c_str(), FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW((alias / L"desktop.ini").c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    Check(std::filesystem::weakly_canonical(root).parent_path() == std::filesystem::weakly_canonical(fixture),
        "provider navigation cleanup escaped private fixture");
    Check(Eventually([&] { std::error_code error; std::filesystem::remove_all(root, error); return !error; }),
        "provider navigation cleanup did not settle");
    // SetActive exercises real graphics startup. Restore the process-wide
    // scheduler boundary expected by the following independent fixtures.
    ShutDownFrames(); g_frameStopping = false;
    std::cout << "PASS: native provider hierarchy/Enter/default menus, nested streams, Unicode, deep/empty folders, returns, native/GPU toggles, refresh, columns, PIDL watch notifications and stale capture cancellation; commands captured\n";
}
