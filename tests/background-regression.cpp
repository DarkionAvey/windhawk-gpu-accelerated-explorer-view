// Runs the actual mod pipeline in a standalone process. WH_EDITING stubs only
// Windhawk's host API; no Explorer injection, hooks, or visible windows occur.
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
// Parse WinRT ABI methods before the private pointer fixture substitutes the
// Win32 GetAsyncKeyState function (the ABI also contains a method with that name).
#include <windows.ui.viewmanagement.h>
#include "clipboard-fixture.h"
#include "icon-fixture.h"
#include "pointer-fixture.h"
#include "focus-fixture.h"
#include "frame-input-fixture.h"
#include "audit-fixture.h"
#include "locale-fixture.h"
#include "settings-fixture.h"
#include "selection-log-fixture.h"
#include GPU_OVERLAY_SOURCE
#undef Wh_Log
#undef CoCreateInstance
#undef SetWinEventHook
#undef MessageBoxW
#undef Wh_GetIntSetting
#undef Wh_GetStringSetting
#undef Wh_FreeStringSetting
#undef GetUserDefaultUILanguage
#undef CreateThread
#undef DCompositionCreateDevice
#undef GetQueueStatus
#undef GetKeyState
#undef GetFocus
#undef GetForegroundWindow
#undef IsWindowVisible
#undef SetFocus
#undef DefSubclassProc
#undef IsWindowEnabled
#undef GetCursorPos
#undef GetAsyncKeyState
#undef SHGetFileInfoW
#undef SHCreateItemFromParsingName
#undef SHCreateItemFromIDList
#undef SHGetPropertyStoreFromParsingName
#undef GetDriveTypeW
#undef GetDiskFreeSpaceExW
#undef GetClipboardSequenceNumber
#undef OpenClipboard
#undef CloseClipboard
#undef GetClipboardData
#undef IsClipboardFormatAvailable
#undef EmptyClipboard
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <propvarutil.h>

static HANDLE providerGate = nullptr;
static std::atomic<unsigned> blockedProviders{0};

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<class Predicate>
static bool Eventually(Predicate predicate, DWORD timeout = 10000) {
    const ULONGLONG deadline = GetTickCount64() + timeout;
    do {
        if (predicate()) return true;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return predicate();
}

static DWORD WINAPI BlockedProvider(LPVOID) {
    ++blockedProviders;
    WaitForSingleObject(providerGate, INFINITE);
    return 0;
}

template<class State>
static bool FreezeDetailProviders(State& state) {
    if constexpr (requires { state.background->detailProviders; }) {
        const unsigned before = blockedProviders.load();
        for (auto& worker : state.background->detailProviders)
            if (!StartTabPoolWorker(state.background, worker, BlockedProvider)) return false;
        return Eventually([&] { return blockedProviders.load() == before + state.background->detailProviders.size(); });
    } else return StartTabWorker(state.background, state.background->properties, BlockedProvider, THREAD_PRIORITY_BELOW_NORMAL);
}

static HWND MessageWindow() {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"pipeline-test", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hwnd != nullptr, "message window creation");
    return hwnd;
}

static void Stop(std::shared_ptr<TabBackground>& work) {
    {
        std::lock_guard<std::mutex> lock(g_backgroundLifetimeMutex);
        work->Stop();
        work.reset();
    }
    SetEvent(g_backgroundReaperWake);
}

static void TestWorkerStartupIsolation() {
    struct Startup {
        std::shared_ptr<TabBackground> work=std::make_shared<TabBackground>();
        HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        bool held=false, started=false;
        ~Startup() { CloseHandle(done); }
    } a,b;
    a.held=true;
    AuditFixture::threadEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    AuditFixture::threadRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    AuditFixture::heldProcedure=TabWorkerEntry;
    const auto start=+[](LPVOID parameter)->DWORD {
        auto& request=*static_cast<Startup*>(parameter);
        AuditFixture::holdThreadCreation=request.held;
        request.started=StartTabWorker(request.work,request.work->properties,
            +[](LPVOID)->DWORD { return 0; },THREAD_PRIORITY_NORMAL);
        SetEvent(request.done); return 0;
    };
    HANDLE first=CreateThread(nullptr,0,start,&a,0,nullptr);
    const bool entered=first && WaitForSingleObject(AuditFixture::threadEntered,5000)==WAIT_OBJECT_0;
    HANDLE second=CreateThread(nullptr,0,start,&b,0,nullptr);
    const bool independent=second && WaitForSingleObject(b.done,2000)==WAIT_OBJECT_0;
    SetEvent(AuditFixture::threadRelease);
    for(HANDLE thread : {first,second}) if(thread) {
        WaitForSingleObject(thread,INFINITE); CloseHandle(thread);
    }
    Stop(a.work); Stop(b.work);
    CloseHandle(AuditFixture::threadEntered); CloseHandle(AuditFixture::threadRelease);
    AuditFixture::threadEntered=AuditFixture::threadRelease=nullptr;
    AuditFixture::heldProcedure=nullptr;
    Check(entered && independent && a.started && b.started,
        "cold thread creation held the shared lifetime registry and blocked another tab");
    std::cout<<"PASS: cold OS thread creation in one tab does not block another tab's startup\n";
}

static DirectoryResult WaitForComplete(TabBackground& work, unsigned long long token) {
    DirectoryResult result;
    Check(Eventually([&] {
        ExclusiveSrwLock lock(work.directoryLock);
        for (auto it = work.directoryResults.begin(); it != work.directoryResults.end(); ++it) {
            if (it->kind == DirectoryResult::Complete && it->token == token) {
                result = std::move(*it);
                work.directoryResults.erase(it);
                return true;
            }
        }
        return false;
    }), "directory did not complete while other lanes were blocked");
    return result;
}

static void TestThirtyIndependentTabs(const std::wstring& directory) {
    constexpr size_t count = 30;
    providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(providerGate != nullptr, "provider gate");
    std::vector<std::unique_ptr<ViewState>> tabs;
    std::vector<HWND> windows;
    std::vector<std::weak_ptr<TabBackground>> retired;
    for (size_t i = 0; i < count; ++i) {
        auto state = std::make_unique<ViewState>();
        state->hwnd = MessageWindow();
        state->background->hwnd = state->hwnd;
        state->folderPath = directory;
        state->rowPx = 34;
        windows.push_back(state->hwnd);
        auto& work = *state->background;
        Check(StartTabWorker(state->background, work.properties, BlockedProvider,
                             THREAD_PRIORITY_BELOW_NORMAL), "property lane startup");
        Check(StartTabWorker(state->background, work.layout, BlockedProvider,
                             THREAD_PRIORITY_BELOW_NORMAL), "layout lane startup");
        Check(StartTabWorker(state->background, work.icons, BlockedProvider,
                             THREAD_PRIORITY_BELOW_NORMAL), "icon lane startup");
        // More than the old process-wide icon queue limit, without executing
        // any providers. Every tab must retain its own capacity.
        for (int icon = 0; icon < 8; ++icon) {
            IconJob job;
            job.hwnd = state->hwnd;
            job.token = i + 1;
            job.key = L"test" + std::to_wstring(icon);
            job.path = L"C:\\generic.txt";
            job.useAttributes = true;
            Check(QueueIcon(*state, job), "icon queue capacity was shared between tabs");
        }
        tabs.push_back(std::move(state));
    }
    Check(Eventually([&] { return blockedProviders.load() == 3 * count; }), "blocked providers startup");
    const auto started = GetTickCount64();
    for (auto& state : tabs) QueueDirectoryRefresh(*state);
    for (auto& state : tabs) {
        auto result = WaitForComplete(*state->background, state->directoryToken);
        Check(result.error == ERROR_SUCCESS && result.entries.size() == 512, "names scan result");
        Check(result.pathIndex.size() == result.entries.size(), "complete path index");
        Check(state->background->iconJobs.size() == 8, "another tab consumed queued icons");
        const auto old = state->directoryToken;
        QueueDirectoryRefresh(*state, true);
        DirectoryResult obsolete;
        obsolete.kind = DirectoryResult::Complete;
        obsolete.token = old;
        obsolete.hwnd = state->hwnd;
        PublishDirectoryResult(*state->background, std::move(obsolete));
        Check(!DirectoryJobCurrent(*state->background, old), "superseded generation accepted");
        WaitForComplete(*state->background, state->directoryToken);
    }
    std::cout << "PASS: 30 tabs enumerated/revalidated with 90 blocked providers; "
              << GetTickCount64() - started << " ms (local test fixture, not Explorer latency)\n";
    for (auto& state : tabs) {
        retired.push_back(state->background);
        Stop(state->background);
    }
    Check(!retired.front().expired(), "provider context freed before its thread returned");
    SetEvent(providerGate);
    Check(Eventually([&] {
        return std::all_of(retired.begin(), retired.end(), [](const auto& weak) { return weak.expired(); });
    }), "closed tabs did not release their contexts/handles");
    for (HWND hwnd : windows) DestroyWindow(hwnd);
    tabs.clear();
    CloseHandle(providerGate);
    providerGate = nullptr;
    std::cout << "PASS: asynchronous close retains blocked workers, then regulator reclaims all 30 groups\n";
}

static void TestPreviewAndLayout(const std::wstring& directory) {
    ViewState state;
    state.hwnd = MessageWindow();
    state.folderPath = directory;
    state.rowPx = 34;
    state.background->hwnd = state.hwnd;
    providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(StartTabWorker(state.background, state.background->layout, BlockedProvider,
                         THREAD_PRIORITY_BELOW_NORMAL), "blocked layout startup");
    state.sort.explicitOrder = true;
    state.sort.kind = SortKind::Name;
    state.grouping.enabled = true;
    state.grouping.key = PKEY_ItemTypeText;
    QueueDirectoryRefresh(state);
    Check(Eventually([&] {
        ExclusiveSrwLock lock(state.background->directoryLock);
        return !state.background->layoutJobs.empty();
    }), "enumeration did not hand layout to its independent lane");
    {
        ExclusiveSrwLock lock(state.background->directoryLock);
        const auto& results = state.background->directoryResults;
        Check(std::any_of(results.begin(), results.end(), [](const auto& value) {
            return value.kind == DirectoryResult::Preview && !value.entries.empty();
        }), "cold grouped/sorted folder waited for providers before publishing names");
    }
    state.grouping.enabled = false;
    state.sort.explicitOrder = false;
    QueueDirectoryRefresh(state, true);
    WaitForComplete(*state.background, state.directoryToken);
    SetEvent(providerGate);
    Stop(state.background);
    DestroyWindow(state.hwnd);
    CloseHandle(providerGate);
    providerGate = nullptr;
    std::cout << "PASS: grouped/sorted preview precedes blocked layout; next unsorted folder bypasses it\n";
}

static void TestPublicationAndEpochs() {
    TabBackground a, b;
    a.hwnd = MessageWindow();
    b.hwnd = MessageWindow();
    a.directoryGeneration = 10;
    b.directoryGeneration = 20;
    for (int i = 1; i <= 8; ++i) {
        DirectoryResult preview;
        preview.kind = DirectoryResult::Preview;
        preview.token = 10;
        preview.entries.resize(i);
        PublishDirectoryResult(a, std::move(preview));
    }
    Check(a.directoryResults.size() == 1 && a.directoryResults.front().entries.size() == 8,
          "obsolete previews accumulated");
    DirectoryResult complete;
    complete.kind = DirectoryResult::Complete;
    complete.token = 10;
    PublishDirectoryResult(a, std::move(complete));
    Check(a.directoryResults.size() == 1 && a.directoryResults.front().kind == DirectoryResult::Complete,
          "complete result did not supersede preview");
    Check(DirectoryJobCurrent(b, 20), "another tab's cancellation generation changed");
    ViewState state;
    state.background->detailJobs.push_back(DetailJob{});
    const auto previous = state.columnEpoch;
    InvalidateDetailJobs(state);
    Check(state.columnEpoch == previous + 1 && state.background->detailJobs.empty() &&
          state.background->detailEpoch == state.columnEpoch, "detail invalidation transaction");
    DestroyWindow(a.hwnd);
    DestroyWindow(b.hwnd);
    std::cout << "PASS: preview coalescing, final publication, and independent cancellation epochs\n";
}

static void TestNativeSuppression() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"SHELLDLL_DefView";
    RegisterClassW(&cls);
    cls.lpszClassName = L"CtrlNotifySink";
    // Deliberately use USER's real default procedure. The old fixture called
    // our exported-API hook itself and therefore assumed the barrier worked.
    cls.lpfnWndProc = DefWindowProcW;
    RegisterClassW(&cls);
    HWND frame = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"hidden-test-frame",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 200, 200, nullptr, nullptr, cls.hInstance, nullptr);
    HWND host = CreateWindowExW(0, L"CtrlNotifySink", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 200, 200, frame, nullptr, cls.hInstance, nullptr);
    HWND native = CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 200, 200, host, nullptr, cls.hInstance, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    Check(InitCommonControlsEx(&controls), "header control initialization");
    HWND header = CreateWindowExW(0, WC_HEADERW, L"", WS_CHILD | WS_VISIBLE,
        0, 0, 200, 24, native, nullptr, cls.hInstance, nullptr);
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE,
        0, 24, 200, 176, native, nullptr, cls.hInstance, nullptr);
    Check(header && IsWindowVisible(header), "native header fixture was not initially visible");
    Check(list != nullptr,"native list restoration fixture");
    ViewState state;
    state.hwnd = CreateWindowExW(0, L"STATIC", L"overlay", WS_CHILD,
        0, 0, 200, 200, frame, nullptr, cls.hInstance, nullptr);
    state.frameHwnd = frame;
    state.nativeHwnd = native;
    Check(SetWindowRgn(host, CreateRectRgn(3, 4, 123, 124), FALSE), "original region setup");
    Check(SuppressNativeContent(state), "initial suppression");
    const auto hidden = [&] { return !(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE); };
    Check(hidden() && NativeViewAvailable(native), "host was not hidden or live Shell view was rejected");
    // The host can be acquired before its presentation child exists. No SHOW
    // notification is required for children created under that hidden host.
    HWND late=CreateWindowExW(0,WC_LISTVIEWW,L"late native list",WS_CHILD|WS_VISIBLE,
        0,24,200,176,native,nullptr,cls.hInstance,nullptr);
    Check(late && !GetPropW(late,kNativeViewportProperty),"late native child setup");
    Check(SuppressNativeContent(state),"recheck already suppressed host");
    RECT lateBounds{}; GetClientRect(late,&lateBounds);
    Check(GetPropW(late,kNativeViewportProperty) && !lateBounds.right && !lateBounds.bottom,
        "known hidden host skipped its later presentation child");
    DestroyWindow(late);
    // A child can dispatch work before the queued lifecycle/maintenance pass.
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    late=CreateWindowExW(0,WC_LISTVIEWW,L"first dispatch native list",WS_CHILD|WS_VISIBLE,
        0,24,200,176,native,nullptr,cls.hInstance,nullptr);
    SuppressDispatchedNativeViewport(late);
    GetClientRect(late,&lateBounds);
    Check(GetPropW(late,kNativeViewportProperty) && !lateBounds.right && !lateBounds.bottom,
        "first native work dispatch escaped viewport suppression");
    DestroyWindow(late);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
    // No overlay messages, paints, timers, or frame worker are allowed here.
    // Every operation must return while the host is STILL hidden.
    for (int i = 0; i < 100; ++i) {
        NativeShowWindowHook(host, SW_SHOWNOACTIVATE);
        Check(hidden(), "ShowWindow exposed the native host");
        NativeShowWindowAsyncHook(host, SW_SHOW);
        Check(hidden(), "ShowWindowAsync queued native exposure");
        Check(NativeSetWindowPosHook(host, nullptr, 0, 0, 200 + i, 200 + i,
            SWP_SHOWWINDOW | SWP_NOSENDCHANGING | SWP_NOACTIVATE | SWP_NOZORDER), "direct native resize");
        Check(hidden(), "NOSENDCHANGING resize exposed native content");
        HDWP batch = BeginDeferWindowPos(1);
        Check(batch != nullptr, "native deferred resize batch could not be created");
        batch = NativeDeferWindowPosHook(batch, host, nullptr, 0, 0, 201 + i, 202 + i,
            SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
        Check(batch != nullptr, "native deferred resize could not be queued");
        Check(EndDeferWindowPos(batch) != FALSE, "native deferred resize failed to apply");
        Check(hidden(), "batched resize exposed native content");
        SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) | WS_VISIBLE);
        SetWindowRgn(host, nullptr, TRUE); // Region resets must not matter.
        Check(hidden(), "style or region reset exposed native content");
    }
    ViewState other;
    other.hwnd = MessageWindow();
    other.frameHwnd = frame;
    other.nativeHwnd = native;
    Check(!SuppressNativeContent(other), "two overlays acquired the same native host");
    // Native layout can echo the intentionally empty physical viewport back
    // as another resize while the GPU owns presentation. That empty echo
    // must not erase the last usable restoration bounds.
    NativeSetWindowPosHook(list,nullptr,3,27,260,220,
        SWP_NOACTIVATE | SWP_NOZORDER);
    NativeSetWindowPosHook(list,nullptr,0,0,0,0,
        SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    RECT suppressed{}; GetClientRect(list,&suppressed);
    Check(suppressed.right==0 && suppressed.bottom==0,"native list escaped zero-viewport suppression");
    RestoreNativeContent(state);
    RECT restored{}; GetWindowRect(list,&restored);
    MapWindowPoints(nullptr,native,reinterpret_cast<POINT*>(&restored),2);
    Check(restored.left==3 && restored.top==27 && restored.right-restored.left==260 && restored.bottom-restored.top==220,
        "native restoration replayed a zero-size suppression echo instead of usable list bounds");
    Check(!hidden() && !GetPropW(host, kSuppressedHostProperty), "native visibility intent was not restored");
    Check(IsWindowVisible(header), "native header visibility was not restored");
    // A creation-time acquisition may have no usable bounds yet. Dedicated
    // file-list children recover from their live Shell parent's client area.
    SetWindowPos(list,nullptr,0,0,0,0,SWP_NOACTIVATE | SWP_NOZORDER);
    Check(SuppressNativeContent(state),"pre-layout native list suppression");
    RestoreNativeContent(state);
    RECT parentBounds{}; GetClientRect(native,&parentBounds); GetClientRect(list,&restored);
    Check(restored.right==parentBounds.right && restored.bottom==parentBounds.bottom && restored.right>0,
        "pre-layout native list did not recover its Shell parent's client bounds");
    // A sent native layout callback can reenter presentation during handoff.
    struct Restoration { ViewState* state; bool entered=false, reacquired=false; } restoration{&state};
    const auto reenter=[](HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context) -> LRESULT {
        auto& fixture=*reinterpret_cast<Restoration*>(context);
        if(message==WM_WINDOWPOSCHANGED && t_nativeVisibilityInternal &&
            !GetPropW(window,kNativeViewportProperty) && !fixture.entered) {
            fixture.entered=true;
            fixture.reacquired=SuppressNativeContent(*fixture.state);
            SuppressNativeViewport(*fixture.state,window);
        }
        return DefSubclassProc(window,message,wp,lp);
    };
    Check(SetWindowSubclass(list,reenter,333,reinterpret_cast<DWORD_PTR>(&restoration)),"native restoration reentry fixture");
    Check(SuppressNativeContent(state),"native restoration reentry setup");
    RestoreNativeContent(state);
    Check(restoration.entered && !restoration.reacquired && state.suppressedNativeHosts.empty() && state.suppressedNativeViewports.empty() &&
        !GetPropW(host,kSuppressedHostProperty) && !GetPropW(list,kNativeViewportProperty),
        "reentrant restoration retained native presentation ownership");
    RemoveWindowSubclass(list,reenter,333);
    Check(SuppressNativeContent(state), "second suppression");
    NativeShowWindowHook(host, SW_HIDE); // Shell really hid a departing host.
    RestoreNativeContent(state);
    Check(hidden(), "restoration resurrected an outgoing native host");
    ShowWindow(host, SW_SHOWNOACTIVATE);
    state.activeTabHost = frame;
    Check(SuppressNativeContent(state), "minimized suppression setup");
    ShowWindow(frame, SW_SHOWMINNOACTIVE);
    Check(IsIconic(frame), "minimize fixture did not become iconic");
    const HWND foreground = GetForegroundWindow(), focus = GetFocus();
    NativeShowWindowHook(host, SW_HIDE); // Shell layout while minimized.
    RestoreNativeContent(state);
    Check(!hidden() && IsIconic(frame) && IsWindowEnabled(frame),
          "unload while minimized stranded the selected native host hidden");
    Check(GetForegroundWindow() == foreground && GetFocus() == focus,
          "minimized native restoration changed activation/focus");
    // Recover an older version's orphaned host without accepting a genuinely
    // hidden view, tab or frame as a live Shell view.
    SetWindowPos(host, nullptr, 0, 0, 0, 0,
        SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE);
    Check(NativeViewAvailable(native), "reload rejected a minimized orphaned content host");
    ShowWindow(native, SW_HIDE);
    Check(!NativeViewAvailable(native), "minimized recovery accepted a hidden Shell view");
    ShowWindow(native, SW_SHOWNOACTIVATE);
    Check(SuppressNativeContent(state), "reload could not acquire minimized host");
    const UINT constrained = ConstrainNativePosition(host, SWP_SHOWWINDOW);
    Check((constrained & (SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) ==
        (SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOOWNERZORDER) && !(constrained & SWP_SHOWWINDOW),
        "native visibility barrier allowed activation/owner reordering");
    RestoreNativeContent(state);
    Check(!hidden(), "minimized orphan recovery did not retain logical visibility");
    Check(SuppressNativeContent(state), "inactive minimized host setup");
    NativeShowWindowHook(host, SW_HIDE);
    state.activeTabHost = other.hwnd;
    RestoreNativeContent(state);
    Check(hidden(), "minimized cleanup resurrected an inactive tab's native host");
    state.activeTabHost = frame;
    ShowWindow(frame, SW_SHOWNOACTIVATE);
    ShowWindow(host, SW_SHOW);
    Check(SuppressNativeContent(state), "third suppression");
    RemovePropW(host, kSuppressedHostProperty); // Simulate ownership loss/reuse.
    SetWindowRgn(host, CreateRectRgn(7, 8, 107, 108), FALSE);
    RestoreNativeContent(state);
    RECT box{};
    Check(GetWindowRgnBox(host, &box) == SIMPLEREGION && box.left == 7 && box.top == 8,
          "restoration changed a host no longer owned by this overlay");
    Check(static_cast<ULONG_PTR>(GetWindowLongPtrW(host, GWLP_WNDPROC)) == GetClassLongPtrW(host, GCLP_WNDPROC),
          "ownership loss left a DLL subclass installed");
    DestroyWindow(other.hwnd);
    DestroyWindow(frame);
    std::cout << "PASS: native host suppression through 100 unpumped show/style/resize cycles; zero-size echo, pre-layout and reentrant restoration; minimized/inactive ownership and cleanup\n";
}

static void PrimeSnapshotWatch(ViewState& state, const std::wstring& directory) {
    state.validatedFolderStamp = std::make_shared<FolderChangeStamp>();
    state.validatedFolderStamp->change = FindFirstChangeNotificationW(directory.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    Check(state.validatedFolderStamp->change != INVALID_HANDLE_VALUE, "private snapshot watch");
    state.directoryWatch=std::make_unique<FolderWatch>();
    state.directoryWatch->stamp=state.validatedFolderStamp;
    state.directoryWatch->change=state.validatedFolderStamp->change;
    state.validatedFolderRevision = 0;
}

static void TestLayoutWatchAndCache(const std::wstring& directory) {
    ViewState state;
    state.hwnd = MessageWindow();
    state.folderPath = directory;
    state.rowPx = 34;
    state.sort.explicitOrder = true;
    state.sort.kind = SortKind::Name;
    state.sort.direction = SORT_ASCENDING;
    state.grouping.enabled = true;
    state.grouping.key = PKEY_FileName;
    QueueDirectoryRefresh(state);
    auto result = WaitForComplete(*state.background, state.directoryToken);
    Check(result.error == ERROR_SUCCESS && result.entries.size() == 512, "real layout snapshot");
    Check(std::is_sorted(result.entries.begin(), result.entries.end(), [](const Entry& a, const Entry& b) {
        return CompareEntryNames(a, b) < 0;
    }), "real layout sort order");
    Check(!result.entries.front().groupKey.empty(), "real group keys not resolved");
    state.entries = std::move(result.entries);
    state.entryPathIndex = std::move(result.pathIndex);
    state.total = static_cast<int>(state.entries.size());
    state.loaded = true;
    state.displayedOrderHash = result.orderHash;
    state.displayedMetadataHash = result.metadataHash;
    state.displayedSort = state.sort;
    state.displayedGrouping = state.grouping;
    PrimeSnapshotWatch(state, directory);
    CacheDepartingFolder(state);
    Check(state.entries.empty() && state.folderCache.snapshotCount == 1, "cache snapshot not moved");
    ShellGroupSpec grouping;
    SortSpec sort;
    Check(CachedFolderSort(state, directory, sort, &grouping) &&
          SameSort(sort, state.sort) && SameShellGroup(grouping, state.grouping), "cached presentation lookup");
    Check(RestoreCachedFolder(state) && state.entries.size() == 512 &&
          state.folderCache.snapshotCount == 0, "cached vector/index restore");
    const auto& target = state.entries[256].path;
    Check(GpuRowForNativePath(state, target) == 256, "restored path index");

    ViewState rich;
    rich.hwnd=MessageWindow(); rich.folderPath=directory;
    rich.loaded=rich.inScope=true; rich.folderCache.budgetBytes=128*1024;
    rich.entries.resize(128);
    for(size_t i=0;i<rich.entries.size();++i) {
        rich.entries[i].path=directory+L"\\rich"+std::to_wstring(i);
        if(i>=24) rich.entries[i].cells.push_back({PKEY_Comment,std::wstring(2048,L'x')});
    }
    CacheDepartingFolder(rich);
    Check(rich.entries.empty() && rich.folderCache.folders.empty() && !rich.folderCache.snapshotCount,
        "cached folder ignored large property strings outside its first 24 rows");
    Stop(rich.background); DestroyWindow(rich.hwnd);

    ViewState many;
    many.hwnd=MessageWindow(); many.loaded=many.inScope=true;
    many.folderCache.budgetBytes=16*1024*1024;
    for(int i=0;i<2100;++i) {
        many.folderPath=directory+L"\\history-"+std::to_wstring(i);
        CacheDepartingFolder(many);
    }
    Check(many.folderCache.snapshotCount==2100 && many.folderCache.usedBytes<many.folderCache.budgetBytes,
        "folder history evicted warm snapshots at 2048 despite available byte budget");
    many.folderCache.budgetBytes=many.folderCache.usedBytes-1;
    TrimFolderCache(many);
    Check(many.folderCache.snapshotCount==2099 && !many.folderCache.folders.contains(directory+L"\\history-0"),
        "byte pressure did not retain least-recently-used eviction behavior");
    ClearFolderSnapshots(many); Stop(many.background); DestroyWindow(many.hwnd);

    WatchDirectory(state);
    // A row generation change must not invalidate the independent watch job.
    state.directoryToken = g_nextToken.fetch_add(1);
    state.background->directoryGeneration = state.directoryToken;
    Check(Eventually([&] {
        ExclusiveSrwLock lock(state.background->directoryLock);
        auto& results = state.background->directoryResults;
        auto it = std::find_if(results.begin(), results.end(), [](const auto& value) {
            return value.kind == DirectoryResult::Watch;
        });
        if (it == results.end()) return false;
        Check(it->token == state.watchToken && it->watch &&
              it->watch->change != INVALID_HANDLE_VALUE, "watch not installed off-thread");
        state.directoryWatch = std::move(it->watch);
        state.dirChange = state.directoryWatch->change;
        results.erase(it);
        return true;
    }), "watch result lost after row-generation change");
    const std::filesystem::path changed = std::filesystem::path(directory) / L"watch-test.txt";
    HANDLE file = CreateFileW(changed.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "watch fixture creation");
    CloseHandle(file);
    Check(WaitForSingleObject(state.dirChange, 10000) == WAIT_OBJECT_0, "filesystem watch missed creation");
    Check(DeleteFileW(changed.c_str()), "watch fixture cleanup");
    CloseDirectoryWatch(state);
    Check(state.dirChange == INVALID_HANDLE_VALUE && !state.directoryWatch, "watch RAII cleanup");
    ClearFolderSnapshots(state);
    RetireCachedEntries(state, state.entries);
    Stop(state.background);
    DestroyWindow(state.hwnd);
    std::cout << "PASS: real background sorting/grouping, cache/index restore, independent watch delivery/cleanup\n";
}

#include "filesystem-mutation-smoke.h"

static void TestSortDetails(const std::wstring& directory) {
    ViewState state;
    state.hwnd = MessageWindow();
    SetWindowPos(state.hwnd, nullptr, 0, 0, 640, 240, SWP_NOZORDER | SWP_NOACTIVATE);
    state.folderPath = directory;
    state.rowPx = 20;
    state.visible = state.inScope = state.detailsViewActive = true;
    state.columnsView = false;
    state.background->hwnd = state.hwnd;
    providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(FreezeDetailProviders(state), "sort detail provider gate");
    QueueDirectoryRefresh(state);
    auto initial = WaitForComplete(*state.background, state.directoryToken);
    state.entries = std::move(initial.entries);
    state.entryPathIndex = std::move(initial.pathIndex);
    state.total = static_cast<int>(state.entries.size());
    state.loaded = true;
    state.directoryPending = false;
    state.displayedOrderHash = initial.orderHash;
    state.displayedMetadataHash = initial.metadataHash;
    const std::vector<PROPERTYKEY> keys{PKEY_DateModified, PKEY_Size, PKEY_ItemTypeText, PKEY_Author};
    for (const auto& key : std::vector<PROPERTYKEY>{PKEY_FileName, PKEY_DateModified,
                                                  PKEY_Size, PKEY_ItemTypeText, PKEY_Author}) {
        ExplorerColumn column; column.key = key; state.columns.push_back(std::move(column));
    }
    // Deliver already-resolved cells through the real completion path. All 512
    // entries are known, including rows outside the capture and next viewport.
    auto expected = [](const Entry& e, size_t key) {
        return key == 3 && e.name == L"file0.txt" ? std::wstring{} :
            std::to_wstring(key) + L":" + e.name;
    };
    DirectoryResult details;
    details.kind = DirectoryResult::Details; details.token = state.directoryToken;
    for (int i = 0; i < state.total; ++i) {
        DetailValue value; value.index = i; value.columnEpoch = state.columnEpoch;
        for (size_t k = 0; k < keys.size(); ++k)
            value.cells.push_back({keys[k], expected(state.entries[i], k)});
        details.details.push_back(std::move(value));
    }
    PublishDirectoryResult(*state.background, std::move(details));
    ReceiveDirectoryResults(state);
    state.columnsView = true;
    auto checkVisible = [&] {
        for (int i : VisibleItemRows(state, 12.f)) {
            const auto& entry = state.entries[i];
            for (size_t k = 0; k < keys.size(); ++k) {
                const auto* value = ColumnCellText(entry, keys[k], state.columnEpoch, state.propertyEpoch);
                Check(value && *value == expected(entry, k),
                      "sort discarded a previously resolved property before the first paint");
            }
        }
        ExclusiveSrwLock lock(state.background->directoryLock);
        Check(state.background->detailJobs.empty(), "sort re-requested already cached properties");
    };
    auto refresh = [&] {
        QueueDirectoryRefresh(state, true, true);
        auto complete = WaitForComplete(*state.background, state.directoryToken);
        PublishDirectoryResult(*state.background, std::move(complete));
        ReceiveDirectoryResults(state);
    };
    // Demand uses a nonblocking queue lock. A worker can own that lock just
    // as names finish; advance the retained frame continuation before judging
    // whether a request was lost. Do not require synchronous queue insertion.
    const auto inspectDemand = [&](auto inspect, const char* failure) {
        Check(Eventually([&] {
            AdvanceAnimationFrame(state,true);
            QueueVisibleDetails(state);
            ExclusiveSrwLock lock(state.background->directoryLock);
            if (state.background->detailJobs.empty()) return false;
            for (const auto& job : state.background->detailJobs)
                for (const auto& request : job.requests) inspect(request);
            return true;
        }),failure);
    };
    for (const auto& [key, direction] : std::vector<std::pair<PROPERTYKEY, SORTDIRECTION>>{
             {PKEY_FileName, SORT_ASCENDING}, {PKEY_FileName, SORT_DESCENDING},
             {PKEY_DateModified, SORT_DESCENDING}, {PKEY_DateModified, SORT_ASCENDING},
             {PKEY_ItemTypeText, SORT_ASCENDING}, {PKEY_Size, SORT_DESCENDING}}) {
        state.sort.explicitOrder = true; state.sort.key = key;
        state.sort.kind = ClassifySort(key); state.sort.direction = direction;
        refresh(); checkVisible();
    }
    // Revisiting rows after several other orders must retain offscreen values.
    SetScrollImmediate(state, MaxScrollPx(state));
    QueueVisibleDetails(state); checkVisible();
    PrimeSnapshotWatch(state, directory);
    CacheDepartingFolder(state);
    Check(RestoreCachedFolder(state), "sorted folder cache restore");
    state.sort.key = PKEY_FileName; state.sort.kind = SortKind::Name;
    state.sort.direction = SORT_ASCENDING;
    refresh(); checkVisible();
    InvalidateDetailJobs(state); // Same properties, a new column generation.
    QueueVisibleDetails(state); checkVisible();
    ExplorerColumn added; added.key = PKEY_Title; state.columns.push_back(added);
    InvalidateDetailJobs(state);
    QueueVisibleDetails(state);
    inspectDemand([&](const DetailRequest& req) {
        Check(req.missingKeys.size() == 1 && SameKey(req.missingKeys[0], PKEY_Title),
              "column generation re-requested existing properties");
    },"new properties were never requested");
    state.columns.pop_back(); InvalidateDetailJobs(state);
    QueueVisibleDetails(state); checkVisible();
    const auto path = state.entries.front().path;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    Check(file != INVALID_HANDLE_VALUE && WriteFile(file, "x", 1, &written, nullptr) && written == 1,
          "sort metadata change fixture");
    CloseHandle(file);
    refresh();
    inspectDemand([&](const DetailRequest& req) {
        Check(req.path == path, "a changed file invalidated unrelated cached rows");
    },"changed file reused stale cached properties");
    // A Shell UPDATEITEM can change a property without changing size/timestamps.
    const auto updated = state.entries[1].path;
    QueueShellItemUpdate(state, updated);
    FlushShellChangeHints(state, GetTickCount64() + kShellVisualCoalesceMs);
    refresh();
    Check(state.entries[1].cells.empty(), "UPDATEITEM resurrected an invalidated cached property");
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    BYTE savedKeys[256]{},keysDown[256]{}; GetKeyboardState(savedKeys); SetKeyboardState(keysDown);
    for(int mode=0;mode<3;++mode) {
        state.gridView=mode==1; state.columnsView=mode==0;
        state.selected=17; state.selectedFlags.assign(state.total,0); state.selectedFlags[17]=1;
        const auto propertyEpoch=state.propertyEpoch;
        if(mode==2) { keysDown[VK_CONTROL]=0x80; SetKeyboardState(keysDown); }
        OverlayProc(state.hwnd,WM_KEYDOWN,mode==2 ? 'R' : VK_F5,0);
        Check(state.propertyEpoch!=propertyEpoch,"F5/Ctrl+R retained the old property generation");
        auto refreshed=WaitForComplete(*state.background,state.directoryToken);
        PublishDirectoryResult(*state.background,std::move(refreshed)); ReceiveDirectoryResults(state);
        state.columnsView=true; QueueVisibleDetails(state);
        Check(state.selected==17 && state.selectedFlags[17],"forced refresh lost selection");
        inspectDemand([&](const DetailRequest& req) {
            Check(req.missingKeys.size()==4,"forced refresh revived an old property-cell cache");
        },"unchanged names blocked forced metadata retrieval");
    }
    SetKeyboardState(savedKeys); SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
    DirectoryResult refetched; refetched.kind=DirectoryResult::Details; refetched.token=state.directoryToken;
    for(int i=0;i<state.total;++i) {
        DetailValue value; value.index=i; value.columnEpoch=state.columnEpoch;
        for(size_t key=0;key<keys.size();++key) value.cells.push_back({keys[key],expected(state.entries[i],key)});
        refetched.details.push_back(std::move(value));
    }
    PublishDirectoryResult(*state.background,std::move(refetched)); ReceiveDirectoryResults(state);
    state.gridView=false; state.columnsView=true;
    state.sort.key=PKEY_FileName; state.sort.kind=SortKind::Name; state.sort.direction=SORT_ASCENDING;
    refresh(); checkVisible(); // A manual refresh must not disable caching on later ordinary sorts.
    SetEvent(providerGate);
    ClearFolderSnapshots(state);
    std::weak_ptr<TabBackground> lifetime = state.background;
    Stop(state.background);
    Check(Eventually([&] { return lifetime.expired(); }), "sort property workers did not retire");
    DestroyWindow(state.hwnd);
    CloseHandle(providerGate); providerGate = nullptr;
    std::cout << "PASS: repeated name/date/type/size sorts preserve cached cells before paint with blocked providers; offscreen/cache returns, column generations and real invalidations\n";
}

static LRESULT CALLBACK PointerCaptureProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CAPTURECHANGED) return OverlayProc(hwnd, msg, w, l);
    return DefWindowProcW(hwnd, msg, w, l);
}

static void TestKeyboardPresentation(ViewState& state) {
    FocusFixture::Session focus(state.frameHwnd,state.nativeHwnd);
    state.columnsView = false;
    state.headerPx = 0;
    state.total = 32;
    state.entries.resize(state.total);
    for (int row = 0; row < state.total; ++row) {
        state.entries[row].name = L"row" + std::to_wstring(row);
        state.entries[row].path = state.entries[row].name;
    }
    UpdateRefreshRate(state);
    auto elapsed = [&](LARGE_INTEGER& started, double ms) {
        QueryPerformanceCounter(&started);
        started.QuadPart -= static_cast<LONGLONG>(ms * state.qpcFrequency / 1000.);
    };
    {
        // Test actual elapsed input retargets, not just equal start positions.
        // A cubic restarted from rest/its default speed has a velocity jump
        // even when every frame arrives on time (the reported rubber band).
        Select(state,4);
        Select(state,5,true);
        auto sample=[&](double ms) {
            elapsed(state.keyboardSelectionStartedQpc,ms);
            return AnimatedSelectionRow(state);
        };
        const float velocity=(sample(34.)-sample(32.))/2.f;
        sample(33.);
        const float position=AnimatedSelectionRow(state);
        Select(state,6,true);
        const float continued=(sample(1.)-state.keyboardSelectionFromRow);
        Check(fabsf(state.keyboardSelectionFromRow-position)<.02f &&
            fabsf(continued-velocity)<.001f,
            "arrow repeat reset the moving selection velocity");
        for (int row : {7,8,9,3,2,10}) {
            sample(31.);
            Select(state,row,true);
            const float from=state.keyboardSelectionFromRow;
            const float to=DocumentRowTop(state,row)/state.rowPx;
            float previous=from;
            for(int ms=0;ms<=180;ms+=5) {
                const float next=sample(ms);
                Check(next>=std::min(from,to)-.01f && next<=std::max(from,to)+.01f &&
                    (to>=from ? next>=previous-.01f : next<=previous+.01f),
                    "retargeted selection reversed or overshot its destination");
                previous=next;
            }
            Check(fabsf(previous-to)<.01f,"selection did not arrive in 180 ms");
        }
        StopKeyboardSelectionAnimation(state);
    }
    Select(state, 1);
    for (UINT key : {VK_NEXT, VK_PRIOR, VK_END, VK_HOME}) {
        const float before = state.scrollNow;
        NavigateKeyboardSelection(state, key, false, false);
        const float target = state.scrollTarget;
        Check(state.animationActive && state.scrollNow == before && target != before,
              "page/home/end did not start a smooth viewport transition");
        // Exercise the real import path used by Shell SelectionChanged echoes.
        state.nativeSelectionPaths = {state.entries[state.selected].path};
        state.nativeSelectionImportPending = true;
        ApplyImportedNativeSelection(state, true);
        Check(state.animationActive && state.scrollNow == before && state.scrollTarget == target,
              "native selection echo snapped an in-flight keyboard scroll");
        elapsed(state.smoothStartedQpc, 90.);
        AdvanceScroll(state);
        Check(state.scrollNow > std::min(before, target) &&
              state.scrollNow < std::max(before, target), "scroll had no intermediate frame");
        elapsed(state.smoothStartedQpc, 181.);
        AdvanceScroll(state);
        Check(!state.animationActive && state.scrollNow == target,
              "keyboard scroll did not settle within the shared 180 ms duration");
    }
    // Back/Forward can restore native selection after cached rows are already
    // displayed. Import the late identity through the production path.
    const float beforeRestore=state.scrollNow;
    state.nativeSelectionPaths={state.entries.back().path};
    state.nativeFocusedPath=state.entries.back().path;
    state.nativeSelectionImportPending=true;
    ApplyImportedNativeSelection(state,false);
    const float restoredTarget=state.scrollTarget;
    const auto restoreClock=state.smoothStartedQpc.QuadPart;
    Check(state.selected==state.total-1 && state.selectedFlags.back() &&
          state.animationActive && state.scrollNow==beforeRestore && restoredTarget>beforeRestore,
          "late history selection snapped instead of scrolling smoothly");
    state.nativeSelectionPaths={state.entries.back().path};
    state.nativeSelectionImportPending=true;
    ApplyImportedNativeSelection(state,true);
    Check(state.animationActive && state.scrollTarget==restoredTarget &&
          state.smoothStartedQpc.QuadPart==restoreClock,
          "restored selection echo restarted its smooth reveal");
    elapsed(state.smoothStartedQpc,90.); AdvanceScroll(state);
    Check(state.scrollNow>beforeRestore && state.scrollNow<restoredTarget,
          "history selection reveal had no intermediate scroll position");
    elapsed(state.smoothStartedQpc,181.); AdvanceScroll(state);
    Check(!state.animationActive && state.scrollNow==restoredTarget,
          "history selection reveal did not settle within 180 ms");
    // A deliberate scroll after restoration owns the viewport; late echoes
    // cannot restart the trip to the selected item.
    SetScrollImmediate(state,0.f);
    state.nativeSelectionPaths={state.entries.back().path};
    state.nativeSelectionImportPending=true;
    ApplyImportedNativeSelection(state,true);
    Check(!state.animationActive && state.scrollNow==0.f && state.scrollTarget==0.f,
          "restored selection echo overrode subsequent manual scrolling");
    // The shared reveal still honors the user's disabled smooth-scrolling
    // option and establishes hidden-view geometry without a stranded clock.
    const bool smooth=g_smooth.exchange(false);
    RevealNativeSelection(state,state.total-1);
    Check(state.scrollNow>0.f && !state.animationActive,"instant reveal option stopped working");
    g_smooth=smooth;
    SetScrollImmediate(state,0.f); state.visible=false;
    RevealNativeSelection(state,state.total-1);
    Check(state.scrollNow>0.f && state.scrollNow==state.scrollTarget && !state.animationActive,
          "hidden selection reveal stranded an animation before presentation");
    state.visible=true;
    state.selected=state.anchor=0;
    state.selectedFlags.assign(state.total,0); state.selectedFlags[0]=1;
    NavigateKeyboardSelection(state, VK_HOME, false, false);
    Check(state.animationActive && state.scrollTarget == 0.f && state.scrollNow > 0.f,
          "Home did not animate back to an already selected first row");
    SetScrollImmediate(state, 0.f);
    StopKeyboardSelectionAnimation(state);

    // Read pixels from the production D2D paint, away from text and icons.
    // Use a fixed GPU bitmap: Present rotates the swap-chain buffers before
    // readback, so its old backbuffer is not a reliable rendering snapshot.
    auto swapChain = std::move(state.swapChain);
    auto backbuffer = std::move(state.backbuffer);
    const auto surfaceProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        backbuffer->GetPixelFormat());
    Check(SUCCEEDED(state.target->CreateBitmap(backbuffer->GetPixelSize(), nullptr,
        0, &surfaceProps, &state.backbuffer)), "fixed GPU readback target");
    state.target->SetTarget(state.backbuffer.Get());
    auto alpha = [&](int row) {
        Paint(state);
        Check(state.backbuffer != nullptr, "highlight render lost its GPU target");
        ComPtr<ID2D1Bitmap1> pixel;
        const auto props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            state.backbuffer->GetPixelFormat());
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1, 1), nullptr, 0,
            &props, &pixel)), "highlight readback bitmap");
        const UINT32 y = row * state.rowPx + state.rowPx / 2;
        const D2D1_RECT_U area = D2D1::RectU(250, y, 251, y + 1);
        Check(SUCCEEDED(pixel->CopyFromBitmap(nullptr, state.backbuffer.Get(), &area)),
              "highlight pixel copy");
        D2D1_MAPPED_RECT mapped{};
        Check(SUCCEEDED(pixel->Map(D2D1_MAP_OPTIONS_READ, &mapped)), "highlight pixel map");
        const int result = mapped.bits[3];
        pixel->Unmap();
        return result;
    };
    const bool zebra = g_zebra.exchange(false);
    const int combined = g_bothAlpha.load();
    const POINT pointer{50, static_cast<LONG>(2.5f * state.rowPx)};
    for (int opacity : {70, 20}) {
        g_bothAlpha = opacity;
        ClearPointerHover(state, false);
        Select(state, 1);
        StopSelectionFade(state);
        UpdatePointerHover(state, pointer, false);
        NavigateKeyboardSelection(state, VK_DOWN, false, false);
        elapsed(state.keyboardSelectionStartedQpc, -1000.);
        const int restingHover = alpha(2);
        Check(std::abs(restingHover - 255 * g_hoverAlpha.load() / 100) <= 2,
              "keyboard selection erased the destination's resting hover");
        elapsed(state.keyboardSelectionStartedQpc, 181.);
        const int keyboardOverlap = alpha(2);
        StopKeyboardSelectionAnimation(state);
        Check(std::abs(alpha(2) - keyboardOverlap) <= 2 &&
              std::abs(keyboardOverlap - 255 * opacity / 100) <= 2,
              "combined highlight jumped when keyboard motion ended");
        NavigateKeyboardSelection(state, VK_DOWN, false, false);
        elapsed(state.keyboardSelectionStartedQpc, 181.);
        Check(std::abs(alpha(2) - 255 * g_hoverAlpha.load() / 100) <= 2,
              "hover disappeared when selection left its row");
        StopKeyboardSelectionAnimation(state);
        Select(state, 2);
        StopSelectionFade(state);
        ClearPointerHover(state, false);
        UpdatePointerHover(state, pointer, true);
        elapsed(state.hoverStartedQpc, kHoverAnimationDurationMs + 1.);
        Check(std::abs(alpha(2) - keyboardOverlap) <= 2,
              "mouse and keyboard overlap painted different backgrounds");
        NavigateKeyboardSelection(state, VK_UP, false, false);
        Check(state.hoverAnimationActive, "keyboard input cancelled independent hover motion");
        StopKeyboardSelectionAnimation(state);
        StopHoverAnimation(state);
    }
    // The earlier repeat-click check observed selection strength without any
    // hovered row. Read the composited background through a real ReleaseCapture
    // notification: selection can stay unchanged while hover makes it blink.
    const auto windowData = GetWindowLongPtrW(state.hwnd, GWLP_USERDATA);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    const auto windowProc = SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(PointerCaptureProc));
    BYTE savedKeys[256]{}, plainKeys[256]{};
    Check(GetKeyboardState(savedKeys) && SetKeyboardState(plainKeys), "highlight click keyboard setup");
    const LPARAM clickPoint = MAKELPARAM(pointer.x, pointer.y);
    Select(state, 2); StopSelectionFade(state);
    for (int opacity : {70, 20}) {
        g_bothAlpha = opacity;
        UpdatePointerHover(state, pointer, false);
        const int resting = alpha(2);
        for (int click = 0; click < 4; ++click) {
            OverlayProc(state.hwnd, WM_MOUSEMOVE, 0, clickPoint);
            OverlayProc(state.hwnd, WM_LBUTTONDOWN, 0, clickPoint);
            Check(GetCapture() == state.hwnd && std::abs(alpha(2)-resting) <= 2,
                  "pointer press changed the resting combined background");
            OverlayProc(state.hwnd, WM_LBUTTONUP, 0, clickPoint);
            Check(GetCapture() != state.hwnd && state.hovered == 2 &&
                  std::abs(alpha(2)-resting) <= 2,
                  "normal capture release blinked the hovered selection background");
        }
        UpdatePointerHover(state, POINT{pointer.x, static_cast<LONG>(1.5f*state.rowPx)}, false);
        UpdatePointerHover(state, pointer, true);
        elapsed(state.hoverStartedQpc, 30.);
        const auto hoverStarted = state.hoverStartedQpc.QuadPart;
        OverlayProc(state.hwnd, WM_LBUTTONDOWN, 0, clickPoint);
        OverlayProc(state.hwnd, WM_LBUTTONUP, 0, clickPoint);
        Check(state.hovered == 2 && state.hoverAnimationActive &&
              state.hoverStartedQpc.QuadPart == hoverStarted,
              "normal capture release restarted an active hover transition");
        StopHoverAnimation(state);
        OverlayProc(state.hwnd, WM_LBUTTONDOWN, 0, clickPoint);
        OverlayProc(state.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(-20, -20));
        Check(state.hovered == -1, "release outside kept a stale row hover");
        UpdatePointerHover(state, pointer, false);
        SetCapture(state.hwnd);
        SetCapture(state.frameHwnd); // A different owner really steals capture.
        Check(state.hovered == -1, "capture theft retained the row's hover");
        ReleaseCapture();
        UpdatePointerHover(state, pointer, false);
        OverlayProc(state.hwnd, WM_MOUSELEAVE, 0, 0);
        elapsed(state.hoverStartedQpc, kHoverAnimationDurationMs+1.);
        Check(state.hovered == -1 && std::abs(alpha(2)-255*g_selectionAlpha.load()/100) <= 2,
              "real pointer leave no longer removed hover");
    }
    Check(SetKeyboardState(savedKeys), "highlight click keyboard restore");
    SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, windowProc);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, windowData);
    g_zebra = zebra;
    g_bothAlpha = combined;
    ClearPointerHover(state, false);
    state.backbuffer = std::move(backbuffer);
    state.swapChain = std::move(swapChain);
    state.target->SetTarget(state.backbuffer.Get());
    std::cout << "PASS: 180 ms page/home/end and late history-selection scrolling survive native echoes and manual scrolling; hidden/instant reveals, GPU hover/selection pixels and capture/release guards\n";
}

static void TestZoomPresentation(ViewState& state, const ViewState& other) {
    // This component fixture supplies a visible overlay and a valid active
    // tab identity; production now correctly ignores hidden/inactive paints.
    FocusFixture::Session privateVisibility(state.hwnd,state.hwnd);
    const auto previousTab=state.activeTabHost;
    const auto firstContent = state.firstContentSubmittedAt;
    const int savedSelected = state.selected;
    const auto savedFlags = state.selectedFlags;
    state.selected = -1;
    state.firstContentSubmittedAt = 0; // Geometry checks do not need icon providers.
    state.activeTabHost = state.frameHwnd;
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    state.rowHeight96[state.gridView] = 34;
    UpdateMetrics(state);
    Check(MakeTextFormats(state), "zoom initial text formats");
    const auto header = state.headerFormat.Get();
    const auto sortedHeader = state.sortedHeaderFormat.Get();
    const auto device = state.d3d.Get();
    const float otherRow = other.rowPx, otherFont = other.fontPx;
    const auto otherZoom = other.rowHeight96;
    const auto otherFormat = other.textFormat.Get();
    auto tick = [&](double ms) {
        QueryPerformanceCounter(&state.zoomStartedQpc);
        state.zoomStartedQpc.QuadPart -= static_cast<LONGLONG>(ms * state.qpcFrequency / 1000.);
        OverlayProc(state.hwnd, kMsgFrame, 0, 0);
        OverlayProc(state.hwnd, WM_PAINT, 0, 0); // Normal delivery advances geometry after input.
    };
    for (bool grouped : {false, true}) {
        state.grouping.enabled = grouped;
        for (int row = 0; row < state.total; ++row)
            state.entries[row].groupKey = row < 16 ? L"first" : L"second";
        RebuildGroupSections(state);
        state.scrollNow = state.scrollTarget = DocumentRowTop(state, 8) + .37f * state.rowPx;
        const float anchor = (state.scrollNow + ViewHeight(state) * .5f) / state.rowPx;
        const float start = state.rowPx, font = state.fontPx;
        const float icon = IconPixelSize(state), textOrigin = NameTextOrigin(state);
        const int target = state.rowHeight96[state.gridView] + 2;
        OverlayProc(state.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
        Check(state.zoomAnimating && state.rowHeight96[state.gridView] == target && state.rowPx == start &&
              NeedsAnimationFrames(state), "Ctrl+wheel snapped geometry or failed to register frames");
        tick(45.);
        Check(state.rowPx > start && state.rowPx < Scale96(target, DpiOf(state.hwnd)) &&
              fabsf(state.rowPx - std::round(state.rowPx)) > .01f,
              "small zoom did not produce fractional intermediate row heights");
        Check(state.fontPx > font && IconPixelSize(state) > icon && NameTextOrigin(state) > textOrigin &&
              fabsf(state.textFormat->GetFontSize() - state.fontPx) < .001f,
              "zoom text, icon size, and text origin did not follow interpolated geometry");
        Check(fabsf((state.scrollNow + ViewHeight(state) * .5f) / state.rowPx - anchor) < .001f &&
              RowFromPoint(state, ViewHeight(state) / 2, 100) == RowAtDocument(state, anchor * state.rowPx),
              "zoom moved the fractional center anchor or desynchronized hit testing");
        Paint(state);
        Check(state.target && state.textFormat && state.d3d.Get() == device,
              "rendering interpolated zoom geometry lost the GPU target");
        const float intermediate = state.rowPx;
        ApplyRowHeight(state, target + 6);
        Check(state.rowPx == intermediate && state.zoomFromRowPx == intermediate &&
              state.tabPresentations[state.activeTabHost].zoomHeight96[state.gridView] == target + 6,
              "rapid zoom retarget jumped or saved an intermediate tab preference");
        tick(90.);
        const float reversing = state.rowPx;
        ApplyRowHeight(state, target - 2);
        Check(state.rowPx == reversing && state.zoomFromRowPx == reversing,
              "reversing zoom jumped to its old destination");
        tick(181.);
        Check(!state.zoomAnimating && state.rowPx == Scale96(target - 2, DpiOf(state.hwnd)),
              "zoom did not settle at its requested height after 180 ms");
    }
    state.selected = state.total-9;
    state.selectedFlags.assign(state.total,0);
    state.selectedFlags[state.selected] = 1;
    auto checkSelected = [&] {
        const float top = DocumentRowTop(state,state.selected)-state.scrollNow;
        Check(top >= -.5f && top+DisplayRowHeight(state,state.selected) <= ViewHeight(state)+.5f,
            "list zoom moved selection outside the viewport");
    };
    for (int destination : {80,24}) {
        SetScrollImmediate(state,DocumentRowTop(state,state.selected) +
            (destination==80 ? state.rowPx-ViewHeight(state)+2.f : -2.f));
        ApplyRowHeight(state,destination);
        tick(45.); checkSelected();
        tick(90.); checkSelected();
        tick(181.); checkSelected();
        Check(!state.animationActive,"zoom selection reveal started an independent scroll");
    }
    Check(state.headerFormat.Get() == header && state.sortedHeaderFormat.Get() == sortedHeader &&
          state.d3d.Get() == device, "zoom recreated headers or the graphics device");
    Check(other.rowPx == otherRow && other.fontPx == otherFont && other.rowHeight96 == otherZoom &&
          other.textFormat.Get() == otherFormat && !other.zoomAnimating,
          "zoom mutated another overlay");
    state.manipulationEnabled = true;
    state.touchpadPinching = false;
    SetScrollImmediate(state,DocumentRowTop(state,state.selected)+state.rowPx-ViewHeight(state)+1.f);
    const float beforePinch = state.rowPx;
    const int beforePinchHeight = state.rowHeight96[state.gridView];
    OverlayProc(state.hwnd, kMsgManipulationPinch, 0,
        static_cast<LPARAM>(std::lround(logf(1.08f) * 65536.f)));
    Check(state.zoomAnimating && state.rowPx == beforePinch && state.rowHeight96[state.gridView] > beforePinchHeight,
          "pinch did not use the shared animated zoom path");
    state.manipulationFrameActive = true;
    const int pinchTarget = state.rowHeight96[state.gridView];
    OverlayProc(state.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    Check(state.rowHeight96[state.gridView] == pinchTarget, "synthetic Ctrl+wheel doubled a pinch");
    tick(90.);
    checkSelected();
    ResetMouseGestureForNavigation(state);
    Check(!state.zoomAnimating && state.rowPx == Scale96(pinchTarget, DpiOf(state.hwnd)),
          "navigation retained a zoom anchor from the preceding folder");
    state.manipulationEnabled = state.manipulationFrameActive = state.touchpadPinching = false;
    g_smooth = false;
    ApplyRowHeight(state, pinchTarget + 2);
    Check(!state.zoomAnimating && state.rowPx == Scale96(pinchTarget + 2, DpiOf(state.hwnd)),
          "disabling smooth animation left zoom unfinished");
    g_smooth = true;
    ApplyRowHeight(state, 96);
    UpdateMetrics(state); // Settings, DPI and resize use this same cancellation path.
    Check(!state.zoomAnimating && state.rowPx == Scale96(96, DpiOf(state.hwnd)),
          "metrics refresh retained an obsolete zoom transition");
    state.grouping.enabled = false;
    RebuildGroupSections(state);
    state.selected = savedSelected;
    state.selectedFlags = savedFlags;
    state.firstContentSubmittedAt = firstContent;
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
    state.activeTabHost=previousTab;
    std::cout << "PASS: shared 180 ms wheel/pinch zoom, selected-item visibility in/out, fractional geometry, grouped center anchoring, retarget/reversal, font reuse, and tab isolation\n";
}


#include "animation-smoke.h"
#include "column-fit-composition-smoke.h"
#include "row-composition-smoke.h"
#include "presentation-owner-smoke.h"
#include "group-heading-smoke.h"
#include "text-render-smoke.h"
#include "grid-smoke.h"
#include "recycle-smoke.h"
#include "icon-smoke.h"
#include "provider-smoke.h"
#include "cold-scroll-smoke.h"
#include "pointer-sampling-smoke.h"
#include "cold-text-smoke.h"
#include "resize-text-smoke.h"
#include "appearance-smoke.h"
#include "layering-smoke.h"
#include "live-sort-smoke.h"

#include "graphics-memory-smoke.h"

static void TestGraphicsIsolation(const std::wstring& fixture) {
    WorkerComApartment apartment;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "DirectWrite setup");
    HWND frame = CreateWindowExW(0, L"STATIC", L"hidden-graphics-frame", 0,
        0, 0, 400, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ViewState a, b;
    for (ViewState* state : {&a, &b}) {
        state->hwnd = CreateWindowExW(0, L"STATIC", L"hidden-gpu", WS_CHILD,
            0, 0, 400, 400, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        state->nativeHwnd = state->hwnd;
        state->frameHwnd = state->activeTabHost = frame;
        state->fontPx = 16;
        state->rowPx = 34;
    }
    LARGE_INTEGER profileFrequency{}, profileStart{}, profileFirst{}, profileSecond{};
    QueryPerformanceFrequency(&profileFrequency);
    QueryPerformanceCounter(&profileStart);
    const bool firstReady = EnsureTarget(a);
    QueryPerformanceCounter(&profileFirst);
    const bool secondReady = EnsureTarget(b);
    QueryPerformanceCounter(&profileSecond);
    std::cout << "PROFILE: GPU target setup first=" <<
        1000.0 * (profileFirst.QuadPart - profileStart.QuadPart) / profileFrequency.QuadPart <<
        " ms next-tab=" << 1000.0 * (profileSecond.QuadPart - profileFirst.QuadPart) /
        profileFrequency.QuadPart << " ms (standalone process)\n";
    if (firstReady && secondReady) {
        Check(a.d3d.Get() != b.d3d.Get() && a.d2dDevice.Get() != b.d2dDevice.Get() &&
              a.composition.Get() != b.composition.Get(), "graphics devices shared between overlays");
        const auto deviceB = b.d3d.Get();
        a.textFormat.Reset();
        const auto targetA = a.target.Get();
        Check(EnsureTarget(a) && a.target.Get() == targetA, "font reset recreated GPU target");
        DropTarget(a);
        Check(b.d3d.Get() == deviceB && b.target && b.graphicsReady,
              "dropping one overlay affected another graphics device");
        Check(EnsureTarget(a) && a.d3d.Get() != b.d3d.Get(), "tab-local graphics recovery");
        ViewState failed;
        failed.hwnd=CreateWindowExW(0,L"STATIC",L"GPU init failure",WS_CHILD,
            0,0,400,400,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(failed.hwnd!=nullptr,"GPU init failure window");
        failed.frameHwnd=frame; failed.nativeHwnd=failed.hwnd;
        AuditFixture::failComposition=true;
        const bool unexpected=EnsureTarget(failed);
        AuditFixture::failComposition=false;
        Check(!unexpected && !failed.d3d && !failed.dxgiDevice && !failed.dxgiFactory &&
            !failed.d2dFactory && !failed.d2dDevice && !failed.composition && !failed.target &&
            !failed.graphicsInitializing,"partial GPU initialization retained device resources after failure");
        Check(EnsureTarget(failed) && failed.graphicsReady && b.d3d.Get()==deviceB,
            "failed GPU initialization did not recover independently on retry");
        DropTarget(failed);
        DestroyWindow(failed.hwnd);
        a.loaded = true;
        QueueFeatureInitialization(a);
        Check(!a.featureInitializationQueued && a.featureInitializationStep == 0,
              "optional startup services ran before the first content submission");
        Check(Eventually([&] {
            Paint(a);
            return !a.presentPending;
        }), "nonblocking graphics submission retry");
        Check(!a.deferColumnProviders && a.graphicsReady,
              "column providers were not released after first content submission");
        Check(a.firstContentSubmittedAt && a.featureInitializationQueued &&
              a.featureInitializationStep == 0,
              "first submission did not queue optional services independently of painting");
        std::cout << "PASS: independent graphics devices, font-only refresh, and isolated target recovery\n";
        TestKeyboardPresentation(a);
        TestZoomPresentation(a, b);
        ProfileAnimatedText(a);
        TestTextRaster(a);
        TestResizeText(frame);
        TestAppearanceRefinements(frame, fixture);
        TestHighlightLayering(frame);
        TestZoomHeaderLayering(frame);
        TestItemOpacityTransitions(frame, fixture);
        TestColdTextDrawing(frame);
        TestColumnViewTransition(frame);
        TestColumnFitComposition(frame);
        TestRowComposition<ViewState>(frame);
        TestStickyGroupHeaders(frame);
        TestIconGrid(frame, fixture);
        TestLiveSorting(frame, fixture);
        TestRecycleIdentities(frame, fixture);
        TestColumnEntrance(frame);
        TestZebraBackground(a);
        TestBatchedDeletion(a, fixture);
        TestIconUploadBatches();
        TestColdScrollDelivery();
        TestSampledPointer();
    } else {
        std::cout << "SKIP: graphics runtime unavailable in this session; Explorer graphics validation required\n";
    }
    DropTarget(a);
    DropTarget(b);
    g_writeFactory.Reset();
    DestroyWindow(frame);
}

static HANDLE nativeProviderEntered = nullptr;
static DWORD WINAPI SlowNativeNames(LPVOID context) {
    WorkerComApartment apartment;
    auto& lane = *static_cast<TabBackground::Worker*>(context);
    auto& work = *lane.owner;
    NativeRowsJob job;
    {
        ExclusiveSrwLock lock(work.directoryLock);
        job = std::move(work.nativeRowsJobs.front());
        work.nativeRowsJobs.pop_front();
    }
    ResolveNativeRowsJob(work, job, [](const NativeRowIdentity& identity, Entry& entry) {
        if (identity.cachedName == L"deliberately slow label") {
            SetEvent(nativeProviderEntered);
            WaitForSingleObject(providerGate, INFINITE);
        }
        return ResolveNativeRowIdentity(identity, entry);
    });
    return 0;
}

static void TestNativeStartupPipeline(const std::wstring& directory) {
    WorkerComApartment apartment;
    PIDLIST_ABSOLUTE rawFolder = nullptr;
    Check(SUCCEEDED(SHParseDisplayName(directory.c_str(), nullptr, &rawFolder, 0, nullptr)),
          "native snapshot fixture folder");
    OwnedPidl folder(rawFolder);
    std::vector<OwnedPidl> absolute;
    std::vector<NativeRowIdentity> identities;
    for (int index = 0; index < 2; ++index) {
        PIDLIST_ABSOLUTE raw = nullptr;
        const auto path = std::filesystem::path(directory) / (L"file" + std::to_wstring(index) + L".txt");
        Check(SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &raw, 0, nullptr)), "namespace fixture item");
        absolute.emplace_back(raw);
        NativeRowIdentity identity;
        identity.absolute.reset(ILCloneFull(raw));
        Entry fast;
        Check(SeedNativeRow(identity, fast) && fast.path == path.wstring() && fast.shellIdentity,
              "PIDL filesystem identity required a label/property provider");
        if (index == 1) identity.cachedName = L"deliberately slow label";
        identities.push_back(std::move(identity));
    }

    ViewState state;
    state.hwnd = MessageWindow();
    state.folderPath = kThisPcLocation;
    state.thisPcGpu = true;
    state.token = 50;
    state.nativeRowsViewToken = state.token;
    state.nativeRowsToken = 100;
    state.nativeRowsNeedCapture = false;
    state.nativeRowsPending = true;
    state.background->hwnd = state.hwnd;
    state.background->nativeRowsGeneration = state.nativeRowsToken;
    NativeRowsJob job;
    job.token = state.nativeRowsToken;
    job.identities = std::move(identities);
    state.background->nativeRowsJobs.push_back(std::move(job));
    providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    nativeProviderEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(StartTabWorker(state.background, state.background->nativeRows, SlowNativeNames,
                         THREAD_PRIORITY_NORMAL), "native name lane startup");
    Check(WaitForSingleObject(nativeProviderEntered, 10000) == WAIT_OBJECT_0, "slow second native name");
    ReceiveDirectoryResults(state);
    Check(state.total == 1 && !state.loaded && state.namesPreview,
          "first native name waited for the blocked later provider");
    Check(!state.featureInitializationQueued && !state.clipboardListenerInstalled &&
          !state.manipulationManager && !state.background->icons.thread &&
          !state.background->properties.thread,
          "optional services or metadata preceded the first native content");
    QueueNativeRows(state); // A harmless event burst must not postpone already available names.
    Check(state.total == 1 && !ShellRowsInteractive(state), "native hint authorized provisional indices");
    // Capacity generations must not invalidate native-name jobs.
    state.directoryToken = 500;
    state.background->directoryGeneration = state.directoryToken;
    SetEvent(providerGate);
    Check(Eventually([&] {
        ReceiveDirectoryResults(state);
        return state.loaded;
    }), "native completion was tied to the capacity generation");
    Check(state.total == 2 && !state.namesPreview && state.entries[0].shellIdentity && state.entries[1].shellIdentity &&
          ILIsEqual(state.entries[0].shellIdentity.get(), absolute[0].get()), "complete immutable namespace identities");
    Check(state.nativeRowsNeedCapture && ShellRowsInteractive(state),
          "in-flight namespace changes were lost or blocked an owned snapshot");
    std::vector<Entry> sorted(2);
    sorted[0].name = L"alpha";
    sorted[1].name = L"zeta";
    SortSpec namespaceSort = DefaultSort();
    namespaceSort.explicitOrder = true;
    namespaceSort.direction = SORT_DESCENDING;
    std::map<std::wstring, std::wstring> types;
    SortDirectoryEntries(sorted, namespaceSort, types, *state.background, state.nativeRowsToken, true);
    Check(sorted[0].name == L"zeta", "namespace sorting used the unrelated directory cancellation generation");
    state.thisPcRowsDirty = true;
    RefreshThisPcRows(state); // No folderView exists: projection must not query it.
    Check(state.loaded && state.total == 2, "metadata projection reopened the native view");
    state.firstContentSubmittedAt = GetTickCount64();
    RefreshThisPcRows(state); // Same rows, now across the first-content boundary.
    Check(state.directoryPending &&
          state.thisPcSpaceRequestedPaths.size() == 2 && !state.nativeRowsPending,
          "deferred capacity work on unchanged rows waited for the maintenance timer");

    PITEMID_CHILD selectedChild = nullptr;
    Check(SUCCEEDED(EntryChildPidl(state, state.entries[0], &selectedChild)) && selectedChild &&
          ILIsEqual(selectedChild, ILFindLastID(absolute[0].get())),
          "namespace command depended on a native view index");
    CoTaskMemFree(selectedChild);
    CancelNativeRows(state);
    state.thisPcGpu = false;
    DirectoryResult obsolete;
    obsolete.kind = DirectoryResult::NativeRows;
    obsolete.hwnd = state.hwnd;
    obsolete.token = 100;
    obsolete.nativeRowsComplete = true;
    PublishDirectoryResult(*state.background, std::move(obsolete));
    Check(state.background->directoryResults.empty(), "departed namespace result accepted");
    Stop(state.background);
    DestroyWindow(state.hwnd);
    CloseHandle(providerGate);
    CloseHandle(nativeProviderEntered);
    providerGate = nativeProviderEntered = nullptr;

    ViewState waiting;
    waiting.hwnd = MessageWindow();
    waiting.thisPcGpu = true;
    QueueNativeRows(waiting);
    QueueNativeRows(waiting);
    MSG message{};
    Check(PeekMessageW(&message, waiting.hwnd, kMsgThisPcRows, kMsgThisPcRows, PM_REMOVE),
          "native readiness waited for an idle timer");
    Check(!PeekMessageW(&message, waiting.hwnd, kMsgThisPcRows, kMsgThisPcRows, PM_REMOVE) &&
          NeedsAnimationFrames(waiting), "readiness hints did not coalesce/use frame-paced retry");
    waiting.nativeRowsNeedCapture = false;
    UnregisterAnimationWhenIdle(waiting);
    DestroyWindow(waiting.hwnd);
    std::cout << "PASS: PIDL names without providers, names before blocked virtual resolution, immutable command identities, cached projection, and cancellation\n";
}

static void TestRenameSizing() {
    for (bool rtl : {false, true}) for (bool externalDestroy : {false, true}) {
        ViewState state;
        HWND frame = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"rename-frame",
            WS_POPUP | WS_VISIBLE, -32000, -32000, 640, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        state.hwnd = CreateWindowExW(0, L"STATIC", L"rename-overlay", WS_CHILD | WS_VISIBLE,
            0, 0, 640, 400, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(frame && state.hwnd, "rename windows");
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        state.frameHwnd = state.activeTabHost = frame;
        state.nativeHwnd = state.hwnd;
        state.visible = state.inScope = state.loaded = true;
        state.rtlLayout = rtl; state.rowPx = 34; state.fontPx = 16; state.headerPx = 0;
        state.folderPath = L"rename-fixture";
        Entry entry; entry.name = L"f.ext"; entry.path = L"rename-fixture\\f.ext";
        state.entries.push_back(entry); state.total = 1;
        ExplorerColumn name; name.key = PKEY_FileName; name.end = name.width = 350;
        state.columns.push_back(name);
        const RECT initial = RenameEditorRect(state, 0);
        auto& r = state.rename;
        r.editor = CreateWindowExW(0, L"EDIT", entry.name.c_str(), WS_CHILD | WS_BORDER |
            ES_AUTOHSCROLL | (rtl ? ES_RIGHT : ES_LEFT), initial.left, initial.top,
            initial.right-initial.left, initial.bottom-initial.top, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(r.editor != nullptr, "rename native EDIT");
        r.index = 0; r.path = entry.path; r.folder = state.folderPath;
        r.tab = state.activeTabHost; r.native = state.nativeHwnd; r.frameRect = initial;
        // Subclass reference data must not depend on the control's USERDATA.
        SetWindowLongPtrW(r.editor, GWLP_USERDATA, 0x227);
        Check(SetWindowSubclass(r.editor, RenameEditProc, reinterpret_cast<UINT_PTR>(RenameEditProc),
            reinterpret_cast<DWORD_PTR>(&state)), "rename editor subclass");
        SendMessageW(r.editor, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        RepositionRenameEdit(state);
        SetWindowPos(r.editor, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        auto width = [&] {
            UpdateWindow(r.editor);
            RECT rect{}; GetWindowRect(r.editor, &rect); return rect.right-rect.left;
        };
        const int shortWidth = width();
        SendMessageW(r.editor, EM_SETSEL, 0, -1);
        SendMessageW(r.editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"pic of a flower.ext"));
        SendMessageW(r.editor, EM_SCROLLCARET, 0, 0);
        const int mediumWidth = width();
        Check(mediumWidth > shortWidth, "rename EDIT did not grow with replacement text");
        Check(static_cast<SHORT>(LOWORD(SendMessageW(r.editor, EM_POSFROMCHAR, 0, 0))) >= 0,
              "expanded rename EDIT kept the beginning of a fitting name scrolled away");
        DWORD begin = 0, end = 0;
        SendMessageW(r.editor, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
        RepositionRenameEdit(state);
        DWORD nextBegin = 0, nextEnd = 0;
        SendMessageW(r.editor, EM_GETSEL, reinterpret_cast<WPARAM>(&nextBegin), reinterpret_cast<LPARAM>(&nextEnd));
        Check(begin == nextBegin && end == nextEnd, "rename sizing moved the caret/selection");
        const std::wstring longName(200, L'W');
        SetWindowTextW(r.editor, longName.c_str());
        const int limit = std::max(1, static_cast<int>(NameColumnEnd(state)-9.f-NameTextOrigin(state)));
        Check(width() == limit, "rename EDIT did not stop at its Name column boundary");
        SendMessageW(r.editor, EM_SETSEL, 0, -1);
        SendMessageW(r.editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"花.ext"));
        Check(width() < mediumWidth, "rename EDIT did not shrink with shorter Unicode text");
        SendMessageW(r.editor, EM_UNDO, 0, 0);
        Check(width() == limit, "undo did not resize rename EDIT");
        // Column ends are in document coordinates, while NameTextOrigin
        // includes the viewport margin. Leave exactly 50 pixels for the name.
        state.columns[0].end = NameTextOrigin(state) - ListStartX(state) + state.horizontalScroll + 50.f;
        RepositionRenameEdit(state);
        Check(width() <= 41, "rename minimum width escaped a narrow Name column");
        for (WPARAM key : {VK_RETURN, VK_ESCAPE}) {
            const auto epoch = r.epoch;
            SendMessageW(r.editor, WM_KEYDOWN, key, 0);
            SendMessageW(r.editor, WM_CHAR, key, 0);
            MSG decision{};
            Check(PeekMessageW(&decision, state.hwnd, kMsgRenameDecision, kMsgRenameDecision, PM_REMOVE) &&
                decision.wParam == (key == VK_RETURN ? 1u : 0u) &&
                decision.lParam == static_cast<LPARAM>(epoch), "rename key decision lost its session epoch");
            Check(!PeekMessageW(&decision, state.hwnd, kMsgRenameDecision, kMsgRenameDecision, PM_REMOVE),
                "rename character generated a duplicate decision");
        }
        Check(GetWindowLongPtrW(r.editor, GWLP_USERDATA) == 0x227, "rename subclass overwrote control USERDATA");
        const HWND editor = r.editor;
        const auto epoch = r.epoch;
        if (externalDestroy) Check(DestroyWindow(editor), "external rename destruction");
        else CancelRename(state);
        Check(!r.editor && !IsWindow(editor) && !r.parentSubclassInstalled && r.epoch > epoch,
            "rename destruction leaked its editor, parent subclass or session epoch");
        DestroyWindow(state.hwnd); DestroyWindow(frame);
    }
    std::cout << "PASS: rename subclass preserves native text/undo/caret, Enter/Escape epochs, USERDATA and cancellation/external destruction in LTR/RTL\n";
}

static void TestEscapeClipboard() {
    ViewState state, other;
    for (auto* tab : {&state, &other}) {
        tab->hwnd = MessageWindow(); tab->frameHwnd = tab->hwnd;
        SetWindowLongPtrW(tab->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tab));
        tab->visible = tab->inScope = true;
    }
    const std::wstring first = L"C:\\clipboard-fixture\\cut.ext";
    const std::wstring next = L"C:\\clipboard-fixture\\new-cut.ext";
    auto escape = [&] { OverlayProc(state.hwnd, WM_KEYDOWN, VK_ESCAPE, 0); };
    for (int mode = 0; mode < 4; ++mode) {
        state.loaded = mode == 0; state.namesPreview = mode == 1;
        state.optimisticStartedAt = mode == 2 ? GetTickCount64() : 0;
        state.thisPcGpu = mode == 3;
        testClipboard.Seed(DROPEFFECT_MOVE, first);
        RefreshClipboardItems(state); RefreshClipboardItems(other);
        Check(state.clipboardPaths.contains(first) && other.clipboardPaths.contains(first), "cut clipboard fixture");
        Check(state.clipboardIconsStartedQpc.QuadPart && other.clipboardIconsStartedQpc.QuadPart,"Cut did not start tab-owned icon clocks");
        state.clipboardIconSize96=other.clipboardIconSize96=20.f;
        state.clipboardIconElapsedMs=other.clipboardIconElapsedMs=90.;
        state.clipboardIconsVisible=other.clipboardIconsVisible=true;
        const auto empties = testClipboard.empties;
        escape();
        Check(state.visible && state.clipboardPaths.empty() && testClipboard.empties == empties+1 &&
              !testClipboard.effect, "Escape failed to cancel Cut through clipboard or exposed native view");
        OverlayProc(other.hwnd, WM_CLIPBOARDUPDATE, 0, 0);
        Check(other.clipboardPaths.empty() && other.visible, "clipboard cancellation did not propagate to another tab");
        Check(!state.clipboardIconSize96 && !other.clipboardIconSize96 && !state.clipboardIconElapsedMs && !other.clipboardIconElapsedMs &&
            !state.clipboardIconsVisible && !other.clipboardIconsVisible &&
            !state.clipboardIconsStartedQpc.QuadPart && !other.clipboardIconsStartedQpc.QuadPart,
            "Escape/clipboard update failed to stop both tabs' Cut animations");
    }
    state.thisPcGpu = state.namesPreview = false; state.optimisticStartedAt = 0; state.loaded = true;
    testClipboard.Seed(DROPEFFECT_COPY, first);
    RefreshClipboardItems(state);
    Check(state.clipboardPaths.contains(first) && !state.clipboardIsCut && state.clipboardIconsStartedQpc.QuadPart,
        "Copy clipboard failed to start transaction feedback");
    auto empties = testClipboard.empties;
    escape();
    Check(testClipboard.effect && testClipboard.empties == empties && state.visible && state.clipboardPaths.contains(first),
        "Escape erased a Copy clipboard or its feedback");
    const auto updateTabs = [&] {
        for (auto* tab : {&state,&other}) OverlayProc(tab->hwnd,WM_CLIPBOARDUPDATE,0,0);
    };
    // External replacements update each tab from clipboard identities/effect,
    // even when only the operation changes and no local key was pressed.
    updateTabs();
    for (DWORD effect : {DWORD(DROPEFFECT_MOVE),DWORD(DROPEFFECT_COPY)}) {
        testClipboard.Seed(effect,first); updateTabs();
        for (auto* tab : {&state,&other})
            Check(tab->clipboardPaths.size()==1 && tab->clipboardPaths.contains(first) &&
                tab->clipboardIsCut==(effect==DROPEFFECT_MOVE),"external same-path Copy/Cut did not update both tabs");
    }
    testClipboard.Seed(DROPEFFECT_COPY,next); updateTabs();
    Check(state.clipboardPaths.contains(next) && !state.clipboardPaths.contains(first) &&
        state.clipboardPaths==other.clipboardPaths,"external Copy replacement retained old identities");
    testClipboard.SeedNonFiles(); updateTabs();
    for (auto* tab : {&state,&other})
        Check(tab->clipboardPaths.empty() && !tab->clipboardIsCut && !tab->clipboardIconsStartedQpc.QuadPart,
            "non-file clipboard replacement retained Copy/Cut feedback");
    testClipboard.Seed(DROPEFFECT_COPY,first,false); updateTabs();
    Check(state.clipboardPaths.contains(first) && !state.clipboardIsCut && state.clipboardPaths==other.clipboardPaths,
        "plain CF_HDROP did not produce Copy feedback");
    for (DWORD effect : {DWORD(DROPEFFECT_LINK),DWORD(DROPEFFECT_NONE)}) {
        testClipboard.Seed(effect,first); updateTabs();
        Check(state.clipboardPaths.empty() && other.clipboardPaths.empty(),"non-Copy/Cut drop effect animated file items");
    }
    testClipboard.Seed(DROPEFFECT_MOVE, first); RefreshClipboardItems(state);
    testClipboard.busy = true; escape();
    Check(state.clipboardRetryPending && state.clipboardPaths.contains(first), "busy clipboard lost cancellation");
    testClipboard.busy = false; RefreshClipboardItems(state);
    Check(!testClipboard.effect && state.clipboardPaths.empty(), "busy clipboard cancellation did not retry");
    testClipboard.Seed(DROPEFFECT_MOVE, first); RefreshClipboardItems(state);
    testClipboard.busy = true; escape();
    testClipboard.Seed(DROPEFFECT_MOVE, next); testClipboard.busy = false;
    empties = testClipboard.empties; RefreshClipboardItems(state);
    Check(testClipboard.effect && state.clipboardPaths.contains(next) && testClipboard.empties == empties,
          "a delayed Escape erased a newer clipboard");
    testClipboard.busy=true; escape();
    testClipboard.Seed(DROPEFFECT_COPY,first); testClipboard.busy=false;
    empties=testClipboard.empties; RefreshClipboardItems(state);
    Check(testClipboard.effect && state.clipboardPaths.contains(first) && !state.clipboardIsCut &&
        testClipboard.empties==empties,"delayed Escape erased a newer Copy or left Cut opacity");
    testClipboard.Seed(DROPEFFECT_MOVE,first); RefreshClipboardItems(state);
    testClipboard.failEmpty = true; escape();
    Check(state.clipboardRetryPending && testClipboard.effect, "clipboard failure discarded Cut cancellation");
    testClipboard.failEmpty = false; RefreshClipboardItems(state);
    Check(!testClipboard.effect && state.clipboardPaths.empty(), "failed Cut cancellation did not recover");
    state.deferredSelectionKeys.push_back({'X', CMIC_MASK_CONTROL_DOWN});
    state.deferredSelectionKeys.push_back({'C', CMIC_MASK_CONTROL_DOWN});
    escape();
    Check(state.deferredSelectionKeys.size() == 1 && state.deferredSelectionKeys.front().key == 'C',
          "Escape retained a deferred Cut or removed another command");
    std::wstring many;
    for(int i=0;i<9000;++i) { many+=L"C:\\clipboard-fixture\\cut-"+std::to_wstring(i); many.push_back(L'\0'); }
    for (DWORD effect : {DWORD(DROPEFFECT_COPY),DWORD(DROPEFFECT_MOVE)}) {
        testClipboard.Seed(effect,many); RefreshClipboardItems(state);
        Check(state.clipboardNextPath && !testClipboard.opened && state.clipboardPaths.empty(),
            "large Copy/Cut held the clipboard, ran synchronously or published a partial snapshot");
        unsigned batches=0;
        while(state.clipboardNextPath && ++batches<10000) {
            MSG message{};
            Check(PeekMessageW(&message,state.hwnd,WM_CLIPBOARDUPDATE,WM_CLIPBOARDUPDATE,PM_REMOVE),"large clipboard continuation lost");
            OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
            Check(!testClipboard.opened,"Copy/Cut capture held clipboard between batches");
        }
        Check(state.clipboardPaths.size()==9000 && state.clipboardPaths.contains(L"C:\\clipboard-fixture\\cut-8999") &&
            state.clipboardIsCut==(effect==DROPEFFECT_MOVE),"Copy/Cut feedback truncated or published a mismatched effect");
        if (effect==DROPEFFECT_MOVE) escape();
        else { testClipboard.SeedNonFiles(); updateTabs(); }
        Check(state.clipboardPaths.empty() && !testClipboard.effect,"large Copy/Cut clear retained visual state");
    }
    testClipboard.Seed(DROPEFFECT_MOVE,first); RefreshClipboardItems(state);
    testClipboard.Seed(DROPEFFECT_COPY,many); RefreshClipboardItems(state);
    Check(state.clipboardNextPath && state.clipboardPaths.contains(first) && state.clipboardIsCut,
        "partial Copy snapshot overwrote the previous complete Cut snapshot");
    testClipboard.Seed(DROPEFFECT_COPY,next); updateTabs();
    Check(!state.clipboardNextPath && state.clipboardPendingPaths.empty() && state.clipboardPaths.size()==1 &&
        state.clipboardPaths.contains(next) && !state.clipboardIsCut,"external replacement mixed pending clipboard generations");
    testClipboard.Seed(DROPEFFECT_MOVE,many); RefreshClipboardItems(state);
    testClipboard.SeedNonFiles(); updateTabs();
    MSG continuation{};
    while (PeekMessageW(&continuation,state.hwnd,WM_CLIPBOARDUPDATE,WM_CLIPBOARDUPDATE,PM_REMOVE))
        OverlayProc(state.hwnd,continuation.message,continuation.wParam,continuation.lParam);
    Check(state.clipboardPaths.empty() && state.clipboardPendingPaths.empty() && !state.clipboardNextPath && !state.clipboardIsCut,
        "stale clipboard continuation resurrected externally cleared items");
    for (auto* tab : {&state, &other}) {
        Stop(tab->background);
        SetWindowLongPtrW(tab->hwnd, GWLP_USERDATA, 0); DestroyWindow(tab->hwnd);
    }
    std::cout << "PASS: clipboard-driven Copy/Cut across tabs, external same-path effect/replacement/non-file changes, plain CF_HDROP, atomic 9000-item capture/replacement; Escape preserves Copy/newer data and retries contention\n";
}

static void TestPreviewInput() {
    ViewState state;
    state.hwnd = CreateWindowExW(0, L"STATIC", L"hidden-preview-input", 0,
        0, 0, 400, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.hwnd != nullptr, "preview input window");
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    state.frameHwnd = state.hwnd;
    state.columnsView = false;
    state.rowPx = 34;
    state.headerPx = 0;
    state.visible = true;
    state.namesPreview = true;
    state.total = 1;
    state.entries.resize(1);
    state.entries[0].folder = true;
    const LPARAM point = MAKELPARAM(50, 20);
    OverlayProc(state.hwnd, WM_LBUTTONDOWN, 0, point);
    Check(state.mousePressActive, "preview first press not recorded");
    OverlayProc(state.hwnd, WM_LBUTTONUP, 0, point);
    Check(state.mouseClickRow == 0, "preview completed click not recorded");
    ++state.mouseFolderEpoch;
    OverlayProc(state.hwnd, WM_LBUTTONDBLCLK, 0, point);
    Check(state.selected == -1, "stale cross-folder double-click opened a preview row");
    OverlayProc(state.hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
    Check(state.visible, "Escape exposed native view during preview");
    state.grouping.enabled = true;
    RebuildGroupSections(state);
    Check(state.sections.empty(), "provisional names exposed unresolved group headings");
    state.grouping.enabled = false;
    state.directoryToken = 111;
    state.background->directoryGeneration = 111;
    state.background->hwnd = state.hwnd;
    state.mouseFolderEpoch = state.mouseClickEpoch = 55;
    state.mouseClickRow = 0;
    state.mouseClickTick = GetTickCount64();
    for (bool complete : {false, true}) {
        DirectoryResult result;
        result.hwnd = state.hwnd;
        result.token = 111;
        result.kind = complete ? DirectoryResult::Complete : DirectoryResult::Preview;
        result.entries.resize(complete ? 32 : 8);
        PublishDirectoryResult(*state.background, std::move(result));
        ReceiveDirectoryResults(state);
        Check(state.mouseFolderEpoch == 55 && state.mouseClickEpoch == 55,
              "stable prefix append invalidated an unchanged folder click");
    }
    Stop(state.background);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
    DestroyWindow(state.hwnd);
    std::cout << "PASS: preview click generations, append stability, ungrouped first rows, and Escape retains GPU view\n";
}

#include "attachment-smoke.h"
#include "selection-smoke.h"
#include "office-lock-smoke.h"
#include "ui-work-smoke.h"
#include "ui-feedback-smoke.h"
#include "frame-budget-smoke.h"
#include "native-viewport-smoke.h"
#include "native-uia-smoke.h"
#include "pipeline-trace-smoke.h"
#include "refresh-smoke.h"
#include "rtl-window-smoke.h"

#include "selection-trace.h"
#include "timing-readiness-smoke.h"
#include "options-smoke.h"
#include "column-bridge-smoke.h"
#include "column-minimum-smoke.h"
#include "namespace-mutations-smoke.h"
#include "status-button-smoke.h"
#include "lifetime-race-smoke.h"
#include "render-quality-smoke.h"
#include "llm-report-smoke.h"
#include "rename-handoff-smoke.h"
#include "rename-reorder-smoke.h"
#include "unexpected-input-smoke.h"
#include "adaptive-image-smoke.h"

int wmain(int argc,wchar_t** argv) {
    if(argc>1 && wcscmp(argv[1],L"--trace-selection")==0)
        return TraceLiveSelection(argc>2 ? std::clamp(_wtoi(argv[2]),1,180) : 90);
    WorkerComApartment mainApartment; // Keep the test process's main STA alive through worker shutdown.
    try {
        g_perf = false;
        {
            // These are identities only; no real volume/share is accessed.
            Check(IsTargetPath(L"Q:\\") && IsTargetPath(L"q:/ordinary/folder") &&
                  IsTargetPath(L"Q:\\Windows.old") && IsTargetPath(L"\\\\scope-fixture\\share\\folder") &&
                  !IsTargetPath(L"Q:relative") && !IsTargetPath(L"relative\\folder") &&
                  !IsTargetPath(L"shell:unsupported") && !IsTargetPath(L""),
                  "all-folder filesystem scope rejected a drive/share or accepted a non-filesystem identity");
            Check(IsSupportedLocation(kThisPcLocation) && IsSupportedLocation(kRecycleBinLocation) &&
                  !IsSupportedLocation(kUnsupportedShellLocation),"filesystem scope changed supported namespace fallbacks");
        }
        wchar_t temporary[MAX_PATH]{};
        Check(GetTempPathW(MAX_PATH, temporary) != 0, "temp directory");
        const auto fixture = std::filesystem::path(temporary) /
            (L"gpu-overlay-regression-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(fixture);
        for (int i = 0; i < 512; ++i) {
            HANDLE file = CreateFileW((fixture / (L"file" + std::to_wstring(i) + L".txt")).c_str(),
                GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Check(file != INVALID_HANDLE_VALUE, "fixture creation");
            CloseHandle(file);
        }
        const bool navigationOnly=argc>1 && wcscmp(argv[1],L"--navigation")==0;
        if (argc>1 && wcscmp(argv[1],L"--adaptive-images")==0) {
            TestAdaptiveImageDemand(); TestAdaptiveImagePipeline();
        } else if (argc>1 && wcscmp(argv[1],L"--unexpected-inputs")==0) {
            TestUnexpectedInputs(fixture.wstring(),argc>2 ? argv[2] : L"all");
        } else if (argc>1 && wcscmp(argv[1],L"--llm-report")==0) {
            TestLlmReport(); TestFilesystemMutationDelivery(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--render-quality")==0) {
            TestRenderQuality(); TestToggleRepaint();
        } else if (argc>1 && wcscmp(argv[1],L"--lifetime")==0) {
            TestLifetimeRaces(); TestDispatchStateRetirement();
        } else if (argc>1 && wcscmp(argv[1],L"--namespace-mutations")==0) {
            TestNamespaceItemMutations(fixture.wstring());
            TestNativeStartupPipeline(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--office-locks")==0) {
            TestOfficeLockFiltering(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--status-buttons")==0) {
            TestStatusButtonLayout();
        } else if (argc>1 && wcscmp(argv[1],L"--drive-refresh")==0) {
            TestDriveSpaceRefresh();
        } else if (argc>1 && wcscmp(argv[1],L"--column-minimum")==0) {
            TestColumnMinimumWidths();
        } else if (argc>1 && wcscmp(argv[1],L"--column-bridge")==0) {
            TestNativeSuppression();
            TestExplorerColumnBridge(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--options")==0) {
            TestUserOptions();
            TestClippingPreflight();
            TestResizeAutoRefit();
        } else if (argc>1 && wcscmp(argv[1],L"--live-sort")==0) {
            HWND frame=CreateWindowExW(0,L"STATIC",L"hidden-live-sort-frame",0,
                0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(frame!=nullptr,"live sort frame");
            TestLiveSorting(frame,fixture.wstring());
            DestroyWindow(frame);
        } else if (argc>1 && wcscmp(argv[1],L"--selection")==0) {
            TestNativeSuppression();
            WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
            cls.lpszClassName=L"CabinetWClass"; RegisterClassW(&cls);
            TestMarqueeSelection(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--rtl-windows")==0) {
            TestNativeSuppression();
            TestMirroredWindowCoordinates(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--gpu-memory")==0) {
            TestGraphicsMemoryProbe();
        } else if (argc>1 && wcscmp(argv[1],L"--gpu-lifecycle")==0) {
            TestGraphicsResourceLifecycle();
        } else if (argc>1 && wcscmp(argv[1],L"--trace-cpu")==0) {
            TestIdleTraceCpu();
        } else if (argc>1 && wcscmp(argv[1],L"--native-uia")==0) {
            TestNativeSuppression();
            TestNativeUiaBatch();
            TestRealNativeUiaPressure(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--native-viewport")==0) {
            TestRealNativeViewport(fixture.wstring());
            TestNativeSuppression();
            TestNativeUiaBatch();
            TestRealNativeUiaPressure(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--filesystem")==0) {
            TestFilesystemMutationDelivery(fixture.wstring());
            TestBurstRowContinuity(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--timing")==0) {
            TestTimingReadiness(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--presentation")==0) {
            TestPresentationOwners();
        } else if (argc>1 && wcscmp(argv[1],L"--row-composition")==0) {
            WorkerComApartment apartment;
            Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"row compositor fonts");
            HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private row fixture",
                WS_POPUP|WS_VISIBLE,-32000,-32000,800,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(frame!=nullptr,"row compositor frame");
            TestRowComposition<ViewState>(frame); DestroyWindow(frame); g_writeFactory.Reset();
        } else if (argc>1 && wcscmp(argv[1],L"--rename")==0) {
            TestRenameSizing();
            TestRenameHandoff(fixture.wstring());
            TestEditorSelectionOwnership(fixture.wstring());
            TestRenameReorderReadiness(fixture.wstring());
        } else if (argc>1 && wcscmp(argv[1],L"--graphics")==0) {
            TestNativeSuppression();
            TestGraphicsIsolation(fixture.wstring());
            TestTimingReadiness(fixture.wstring());
        } else if (navigationOnly) {
            TestNativeSuppression();
            TestAttachedLifecycle(fixture.wstring());
            TestUiFeedbackAudit(fixture.wstring());
        } else {
        TestLlmReport(); TestRenderQuality(); TestToggleRepaint();
        TestLifetimeRaces(); TestDispatchStateRetirement();
        TestStatusButtonLayout();
        TestUserOptions();
        TestClippingPreflight();
        TestResizeAutoRefit();
        TestColumnMinimumWidths();
        TestOfficeLockFiltering(fixture.wstring());
        TestNamespaceItemMutations(fixture.wstring());
        TestNativeSuppression();
        TestExplorerColumnBridge(fixture.wstring());
        TestPublicationAndEpochs();
        TestWorkerApartment();
        TestWorkerStartupIsolation();
        TestThirtyIndependentTabs(fixture.wstring());
        TestColdColumnCatalog(fixture.wstring());
        TestDeferredColumnCapture(fixture.wstring());
        TestSnapshotSelectionRestoration(fixture.wstring());
        ProfileExecutableIcons();
        TestIconQueueIsolation();
        TestAdaptiveImageDemand(); TestAdaptiveImagePipeline();
        TestFrameBudgets();
        TestPipelineTrace();
        TestPropertyPoolIsolation(fixture.wstring());
        TestDriveSpaceRefresh();
        TestPreviewAndLayout(fixture.wstring());
        TestLayoutWatchAndCache(fixture.wstring());
        TestFilesystemMutationDelivery(fixture.wstring());
        TestBurstRowContinuity(fixture.wstring());
        TestSortDetails(fixture.wstring());
        TestRealNativeViewport(fixture.wstring());
        TestNativeSuppression();
        TestNativeUiaBatch();
        TestRealNativeUiaPressure(fixture.wstring());
        TestGraphicsIsolation(fixture.wstring());
        TestGraphicsResourceLifecycle();
        TestPresentationOwners();
        TestTimingReadiness(fixture.wstring());
        TestMirroredWindowCoordinates(fixture.wstring());
        TestRenameSizing();
        TestRenameHandoff(fixture.wstring());
        TestEditorSelectionOwnership(fixture.wstring());
        TestRenameReorderReadiness(fixture.wstring());
        TestUnexpectedInputs(fixture.wstring());
        TestEscapeClipboard();
        TestPreviewInput();
        TestNativeStartupPipeline(fixture.wstring());
        TestColdAttachWake();
        TestAttachedLifecycle(fixture.wstring());
        TestTypeSearchPrefix();
        TestUiFeedbackAudit(fixture.wstring());
        TestMarqueeSelection(fixture.wstring());
        }
        Check(g_windowCallbacks.load() == 0, "window callbacks were not drained");
        Check(g_shellViewHooks.empty(), "frame lifecycle/creation hooks outlived their overlays");
        ShutDownFrames();
        ShutDownBackgroundWorkers();
        Check(!g_backgroundLifetimes && !g_backgroundReaper && !g_backgroundReaperWake,
              "background shutdown leaked lifetime registry/handles");
        Check(std::filesystem::weakly_canonical(fixture).parent_path() ==
              std::filesystem::weakly_canonical(std::filesystem::path(temporary)),
              "fixture deletion escaped its temporary directory");
        // Shell releases of a nested bound folder can finish after its files
        // are deleted. Retry only this verified private fixture's cleanup.
        Check(Eventually([&] {
            std::error_code error;
            std::filesystem::remove_all(fixture,error);
            return !error;
        }), "private fixture cleanup did not settle");
        std::cout << (navigationOnly ? "Navigation regressions passed. " : "All pipeline regressions passed. ") <<
            "Explorer visual/ETW validation is separate.\n";
        return 0;
    } catch (const std::exception& error) {
        if (providerGate) SetEvent(providerGate);
        ShutDownFrames();
        ShutDownBackgroundWorkers();
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
