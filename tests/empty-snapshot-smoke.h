// Empty-folder claims versus the Shell's own CFSFolder enumeration. Private
// fixture files only; the substituted native view lists real child PIDLs.
class EmptySnapshotFolderView final : public AttachmentFolderView {
public:
    std::vector<OwnedPidl> items; // Explorer's model of the folder.
    HRESULT STDMETHODCALLTYPE ItemCount(UINT flags, int* out) override {
        *out = flags == SVGIO_SELECTION ? 0 : static_cast<int>(items.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Item(int index, PITEMID_CHILD* out) override {
        *out = index >= 0 && size_t(index) < items.size() ? ILCloneChild(items[size_t(index)].get()) : nullptr;
        return *out ? S_OK : E_FAIL;
    }
};

static std::vector<std::wstring> ShellProviderNames(IShellFolder* folder, const ShellVisibilityPolicy& policy) {
    std::vector<std::wstring> names;
    ComPtr<IEnumIDList> children;
    Check(folder->EnumObjects(nullptr, ShellEnumerationFlags(policy), &children) == S_OK && children,
        "Shell provider enumeration");
    for (;;) {
        PITEMID_CHILD raw = nullptr; ULONG fetched = 0;
        if (children->Next(1, &raw, &fetched) != S_OK || !fetched) break;
        OwnedPidl child(raw);
        STRRET name{}; PWSTR text = nullptr;
        Check(SUCCEEDED(folder->GetDisplayNameOf(child.get(), SHGDN_INFOLDER | SHGDN_FORPARSING, &name)) &&
            SUCCEEDED(StrRetToStrW(&name, child.get(), &text)), "Shell provider child name");
        names.emplace_back(text); CoTaskMemFree(text);
    }
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return names;
}

static void TestEmptySnapshotReconciliation(const std::wstring& fixture) {
    const auto directory = JoinPath(fixture, L"empty-snapshot");
    Check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "empty snapshot private directory");
    std::vector<std::wstring> files, folders;
    const auto create = [&](const std::wstring& name, DWORD attributes, bool folder = false) {
        const auto path = JoinPath(directory, name);
        if (folder) {
            Check(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "empty snapshot private folder");
            folders.push_back(path);
        } else {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            Check(file != INVALID_HANDLE_VALUE, "empty snapshot private file"); CloseHandle(file);
            files.push_back(path);
        }
        Check(SetFileAttributesW(path.c_str(), attributes) != FALSE, "empty snapshot fixture attributes");
        return path;
    };
    const auto remove = [&](const std::wstring& path) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        Check(DeleteFileW(path.c_str()) != FALSE, "empty snapshot private removal");
        std::erase(files, path);
    };
    ViewState state;
    state.hwnd = MessageWindow(); state.nativeHwnd = state.hwnd; state.activeTabHost = state.frameHwnd = state.hwnd;
    state.background->hwnd = state.hwnd; state.folderPath = directory;
    state.inScope = state.detailsViewActive = true; state.columnsView = false;
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    struct Cleanup {
        ViewState& state; std::vector<std::wstring>& files; std::vector<std::wstring>& folders; const std::wstring& directory;
        ~Cleanup() {
            if (state.background) Stop(state.background);
            SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0); DestroyWindow(state.hwnd);
            for (const auto& path : files) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
            for (const auto& path : folders) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(path.c_str()); }
            RemoveDirectoryW(directory.c_str());
        }
    } cleanup{state, files, folders, directory};
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&state.folder))), "empty snapshot private provider");
    auto* native = new EmptySnapshotFolderView; native->folder = state.folder; state.folderView.Attach(native);
    const auto child = [&](const std::wstring& path) {
        PIDLIST_RELATIVE raw = nullptr;
        Check(SUCCEEDED(state.folder->ParseDisplayName(nullptr, nullptr,
            const_cast<LPWSTR>(PathFindFileNameW(path.c_str())), nullptr, &raw, nullptr)) && raw, "fixture child identity");
        return OwnedPidl(raw);
    };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, state.hwnd, kMsgReconcileEmptySnapshot, kMsgReconcileEmptySnapshot, PM_REMOVE))
            OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
    };
    const auto complete = [&] {
        DirectoryResult result;
        Check(Eventually([&] {
            ExclusiveSrwLock lock(state.background->directoryLock);
            for (auto it = state.background->directoryResults.begin(); it != state.background->directoryResults.end();) {
                if (it->token != state.directoryToken ||
                    (it->kind != DirectoryResult::Preview && it->kind != DirectoryResult::Complete)) { ++it; continue; }
                if (it->kind == DirectoryResult::Complete) {
                    result = std::move(*it); state.background->directoryResults.erase(it); return true;
                }
                it = state.background->directoryResults.erase(it);
            }
            return false;
        }), "empty snapshot directory completion");
        PublishDirectoryResult(*state.background, std::move(result)); ReceiveDirectoryResults(state);
    };
    const auto refresh = [&] { QueueDirectoryRefresh(state, true); complete(); };

    // The provider probe follows CFSFolder and the Win32 visibility rules.
    const ShellVisibilityPolicy plain{false, false}, hidden{true, false};
    Check(!ProviderListsVisibleChild(state.folder.Get(), hidden, [] { return true; }), "empty folder listed a provider child");
    const auto hiddenFile = create(L"hidden.txt", FILE_ATTRIBUTE_HIDDEN);
    Check(!ProviderListsVisibleChild(state.folder.Get(), plain, [] { return true; }) &&
        ProviderListsVisibleChild(state.folder.Get(), hidden, [] { return true; }), "hidden provider child policy");
    remove(hiddenFile);
    const auto owner = create(L"~$owner.docx", FILE_ATTRIBUTE_NORMAL);
    Check(!ProviderListsVisibleChild(state.folder.Get(), hidden, [] { return true; }), "Office owner proved a non-empty folder");

    // Contradicted empty snapshots revalidate once; a stale or filtered
    // Explorer item does not hand the folder back.
    state.visibility = hidden; refresh();
    Check(state.loaded && state.total == 0 && state.inScope, "Office owner-only folder snapshot");
    native->items.push_back(child(owner));
    Check(CaptureNativeSelection(state), "contradicted selection was not deferred");
    pump();
    Check(state.directoryPending && state.emptySnapshotRecheckToken == state.directoryToken, "contradicted empty snapshot was not revalidated");
    complete(); pump();
    Check(state.inScope && !state.emptySnapshotWitnessed && state.total == 0 && state.nativeFallbackFolder.empty(),
        "filtered Explorer item returned the folder to Explorer");
    remove(owner); native->items.clear();
    const auto deleted = create(L"deleted.txt", FILE_ATTRIBUTE_NORMAL);
    refresh();
    Check(state.total == 1, "deletion fixture snapshot");
    native->items.push_back(child(deleted));
    remove(deleted); refresh(); pump();
    Check(state.directoryPending && state.emptySnapshotRecheckToken == state.directoryToken, "deletion race was not revalidated");
    complete(); pump();
    Check(state.inScope && !state.emptySnapshotWitnessed && state.total == 0,
        "item still in Explorer's change queue returned the folder to Explorer");
    native->items.clear(); refresh();
    const auto created = create(L"created.txt", FILE_ATTRIBUTE_NORMAL);
    native->items.push_back(child(created)); pump();
    Check(CaptureNativeSelection(state), "new item contradiction was not deferred");
    pump();
    Check(state.directoryPending, "new Explorer item did not revalidate the empty snapshot");
    complete(); pump();
    Check(state.inScope && state.total == 1 && GpuRowForNativePath(state, created) == 0, "revalidation missed Explorer's new item");

    // SYSTEM alone is listed by CFSFolder; HIDDEN|SYSTEM needs both options.
    remove(created); native->items.clear();
    create(L"plain.txt", FILE_ATTRIBUTE_NORMAL);
    create(L"system.txt", FILE_ATTRIBUTE_SYSTEM);
    create(L"protected.txt", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    create(L"hidden.txt", FILE_ATTRIBUTE_HIDDEN);
    create(L"system-folder", FILE_ATTRIBUTE_SYSTEM, true);
    create(L"customized-folder", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM, true);
    for (bool showHidden : {false, true}) for (bool showProtected : {false, true}) {
        state.visibility = {showHidden, showProtected}; refresh();
        std::vector<std::wstring> gpu;
        for (const auto& entry : state.entries) gpu.push_back(entry.name);
        std::sort(gpu.begin(), gpu.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        Check(gpu == ShellProviderNames(state.folder.Get(), state.visibility),
            "GPU visibility differs from the Shell provider's enumeration");
    }

    // A validated, visible Explorer item absent from both listings is not
    // reproducible on NTFS fixtures. Its committed outcome hands the folder
    // back without clearing Explorer's model.
    for (auto path : std::vector<std::wstring>(files)) remove(path);
    for (const auto& path : folders) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(path.c_str()); }
    folders.clear();
    const auto witness = create(L"witness.txt", FILE_ATTRIBUTE_NORMAL);
    native->items.push_back(child(witness)); remove(witness);
    state.visibility = plain; refresh(); pump();
    complete(); pump();
    Check(state.inScope && state.emptySnapshotRecheckToken == state.directoryToken, "fallback fixture generation");
    state.emptySnapshotWitnessed = true;
    Check(ReconcileEmptySnapshot(state) && !state.inScope && state.nativeFallbackFolder == directory &&
        native->items.size() == 1, "contradicted empty snapshot was not returned to Explorer");
    std::cout << "PASS: empty GPU snapshots follow CFSFolder visibility, revalidate against Explorer's items, ignore stale/filtered witnesses and hand contradicted folders back to Explorer\n";
}
