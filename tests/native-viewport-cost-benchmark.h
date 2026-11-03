// Focused diagnostic: Explorer UI-thread work done by a hidden native DirectUI
// list. "full" holds the production input layout (the list's real size, as
// every 0.9.288-0.9.345 view did); "patched" is the production default, where
// the list stays 0x0 and a native menu session alone restores its geometry.
// Both use the same real private ExplorerBrowser, DefView and production host
// suppression. Busy time is measured on this STA (message dispatch plus direct
// Shell calls); Shell's background enumeration threads are excluded. Not a
// live Explorer or displayed-frame measurement.

namespace NativeViewportCost {

class Events final : public IExplorerBrowserEvents {
    ULONG refs = 1;
public:
    bool complete = false;
    ComPtr<IFolderView2> view;
    HWND native = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IExplorerBrowserEvents) return E_NOINTERFACE;
        *out = static_cast<IExplorerBrowserEvents*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE OnNavigationPending(PCIDLIST_ABSOLUTE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnNavigationComplete(PCIDLIST_ABSOLUTE) override { complete = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView* shell) override {
        shell->QueryInterface(IID_PPV_ARGS(&view)); shell->GetWindow(&native);
        return S_OK;
    }
};

struct Phase { const char* name; double busyMs = 0; ULONG64 cycles = 0; unsigned messages = 0; };
constexpr const char* kPhases[] = {"navigate", "sort", "view-modes", "resize", "menu-session", "refresh"};
constexpr int kMenuSessions = 10;

void CreateFixture(const std::filesystem::path& root, int count) {
    Check(std::filesystem::create_directory(root), "viewport cost private directory");
    const wchar_t* extensions[] = {L".txt", L".cpp", L".bin", L".log"};
    for (int i = 0; i < count; ++i) {
        const int id = (i * 7919) % count;
        const auto path = root / (L"Item-" + std::to_wstring(id) + extensions[id % 4]);
        if (id % 32 == 0) { Check(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "viewport cost folder"); continue; }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "viewport cost file");
        LARGE_INTEGER length{}; length.QuadPart = (id * 31) % 4096;
        Check(SetFilePointerEx(file, length, nullptr, FILE_BEGIN) && SetEndOfFile(file), "viewport cost file size");
        CloseHandle(file);
    }
}

std::vector<Phase> Run(PCIDLIST_ABSOLUTE root, int expected, bool full) {
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    ViewState state;
    const HWND frame = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"viewport cost",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 1200, 900, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    state.frameHwnd = frame;
    state.hwnd = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1200, 900, frame, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    Check(frame && state.hwnd, "viewport cost windows");
    state.visible = state.inScope = state.detailsViewActive = true;
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    ComPtr<IExplorerBrowser> browser;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&browser))),
        "viewport cost ExplorerBrowser");
    RECT bounds{0, 0, 1200, 900}; FOLDERSETTINGS settings{FVM_DETAILS, FWF_NOBROWSERVIEWSTATE};
    Check(SUCCEEDED(browser->Initialize(frame, &bounds, &settings)), "viewport cost browser initialize");
    browser->SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG);
    auto* sink = new Events;
    DWORD cookie = 0; Check(SUCCEEDED(browser->Advise(sink, &cookie)), "viewport cost events");

    std::vector<Phase> phases;
    Phase* current = nullptr;
    HWND list = nullptr;
    bool applied = false;
    // Apply the production policy the moment DirectUI's list exists. "full"
    // enters the menu-session input layout and keeps it for the whole run.
    const auto apply = [&] {
        if (applied || !sink->native) return;
        list = FindWindowExW(sink->native, nullptr, L"DirectUIHWND", nullptr);
        if (!list) return;
        applied = true;
        state.nativeHwnd = sink->native;
        Check(SuppressNativeContent(state), "viewport cost host suppression");
        Check(!state.suppressedNativeViewports.empty(), "viewport cost list was not acquired");
        if (full) SetNativeInputLayout(state, true);
    };
    const auto timed = [&](auto&& call) {
        LARGE_INTEGER start{}, end{}; ULONG64 cyclesStart = 0, cyclesEnd = 0;
        QueryThreadCycleTime(GetCurrentThread(), &cyclesStart); QueryPerformanceCounter(&start);
        call();
        QueryPerformanceCounter(&end); QueryThreadCycleTime(GetCurrentThread(), &cyclesEnd);
        current->busyMs += 1000. * (end.QuadPart - start.QuadPart) / frequency.QuadPart;
        current->cycles += cyclesEnd - cyclesStart;
    };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            timed([&] { TranslateMessage(&message); DispatchMessageW(&message); });
            ++current->messages;
            apply();
        }
    };
    const auto ready = [&](const char* what) {
        int count = -1;
        Check(Eventually([&] {
            pump();
            return sink->complete && sink->view && applied &&
                SUCCEEDED(sink->view->ItemCount(SVGIO_ALLVIEW, &count)) && count == expected;
        }, 120000), what);
        pump();
    };
    const auto policyHeld = [&](const char* what) {
        RECT client{}; GetClientRect(list, &client);
        Check(!IsWindowVisible(list), "viewport cost list became visible");
        Check(full ? client.right > 0 && client.bottom > 0 : client.right == 0 && client.bottom == 0, what);
    };
    const auto begin = [&](const char* name) { phases.push_back({name}); current = &phases.back(); };

    phases.reserve(std::size(kPhases));
    begin("navigate");
    timed([&] { Check(SUCCEEDED(browser->BrowseToIDList(root, SBSP_ABSOLUTE)), "viewport cost navigate"); });
    ready("viewport cost navigation did not finish");
    policyHeld("viewport cost policy did not hold during navigation");

    begin("sort");
    PROPERTYKEY sorts[] = {PKEY_Size, PKEY_DateModified, PKEY_ItemNameDisplay};
    for (const auto& key : sorts) for (const auto direction : {SORT_DESCENDING, SORT_ASCENDING}) {
        SORTCOLUMN sort{key, direction};
        timed([&] { Check(SUCCEEDED(sink->view->SetSortColumns(&sort, 1)), "viewport cost sort"); });
        pump();
    }

    begin("view-modes");
    for (const auto& [mode, size] : {std::pair{FVM_ICON, 96}, std::pair{FVM_LIST, 16},
                                     std::pair{FVM_SMALLICON, 16}, std::pair{FVM_DETAILS, 16}}) {
        timed([&] { Check(SUCCEEDED(sink->view->SetViewModeAndIconSize(mode, size)), "viewport cost view mode"); });
        pump();
    }

    begin("resize");
    for (int step = 0; step < 24; ++step) {
        RECT rect{0, 0, 700 + (step % 12) * 40, 500 + (step % 8) * 40};
        timed([&] { browser->SetRect(nullptr, rect); });
        pump();
    }
    policyHeld("viewport cost policy did not hold through sort, view and resize");

    // The patched policy's own price: each native menu restores the real
    // geometry before WM_CONTEXTMENU and collapses it once the session ends.
    begin("menu-session");
    if (!full) for (int session = 0; session < kMenuSessions; ++session) {
        timed([&] { SetNativeInputLayout(state, true); });
        RECT client{}; GetClientRect(list, &client);
        Check(client.right > 0 && client.bottom > 0, "menu session did not restore the list's real geometry");
        pump();
        timed([&] { SetNativeInputLayout(state, false); });
        pump();
        policyHeld("menu session did not collapse the list again");
    }

    begin("refresh");
    ComPtr<IShellView> shell;
    Check(SUCCEEDED(sink->view.As(&shell)), "viewport cost shell view");
    timed([&] { Check(SUCCEEDED(shell->Refresh()), "viewport cost refresh"); });
    ready("viewport cost refresh did not finish");
    policyHeld("viewport cost policy changed after refresh");

    RestoreNativeContent(state);
    RECT restored{}; GetClientRect(list, &restored);
    Check(restored.right > 0 && restored.bottom > 0, "native restoration left the list at zero size");
    browser->Unadvise(cookie); browser->Destroy(); browser.Reset(); sink->Release(); DestroyWindow(frame);
    MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    return phases;
}

} // namespace NativeViewportCost

static void TestNativeViewportCost(const std::wstring& fixture) {
    using namespace NativeViewportCost;
    Regression::Claim(Regression::QuietCpu, "uncontended native viewport UI-thread timings");
    constexpr int kRuns = 5;
    std::cout << "VIEWPORT_COST_HEADER,items,policy,run,phase,busy_ms,mcycles,messages\n";
    for (const int count : {2000, 10000}) {
        const auto root = std::filesystem::path(fixture) / (L"viewport-cost-" + std::to_wstring(count));
        CreateFixture(root, count);
        OwnedPidl pidl;
        PIDLIST_ABSOLUTE raw = nullptr;
        Check(SUCCEEDED(SHParseDisplayName(root.c_str(), nullptr, &raw, 0, nullptr)), "viewport cost root");
        pidl.reset(raw);
        // Warm the filesystem and Shell caches once so both policies start equal.
        Run(pidl.get(), count, true);
        std::map<std::pair<bool, std::string>, std::vector<double>> busy, cycles;
        for (int run = 0; run < kRuns; ++run) {
            // Alternate which policy goes first to cancel ordering bias.
            for (const bool full : run % 2 ? std::array{false, true} : std::array{true, false}) {
                for (const auto& phase : Run(pidl.get(), count, full)) {
                    std::cout << "VIEWPORT_COST," << count << ',' << (full ? "full" : "patched") << ','
                        << run << ',' << phase.name << ',' << phase.busyMs << ',' << phase.cycles / 1e6 << ','
                        << phase.messages << '\n';
                    busy[{full, phase.name}].push_back(phase.busyMs);
                    cycles[{full, phase.name}].push_back(phase.cycles / 1e6);
                }
            }
        }
        const auto median = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        };
        for (const char* name : kPhases) {
            const double full = median(busy[{true, name}]), patched = median(busy[{false, name}]);
            std::cout << "PROFILE native-viewport-cost items=" << count << " phase=" << name
                << " full-ms=" << full << " patched-ms=" << patched
                << " full-mcycles=" << median(cycles[{true, name}])
                << " patched-mcycles=" << median(cycles[{false, name}])
                << " saved-ms=" << full - patched;
            if (!std::strcmp(name, "menu-session")) std::cout << " per-session-ms=" << patched / kMenuSessions;
            std::cout << std::endl;
        }
    }
    std::cout << "PASS: measured production hidden-list policy against the real-size layout\n";
}
