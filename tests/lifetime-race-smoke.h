// Private worker/window fixtures. No user folders, clipboard or focus changes.
static void TestLifetimeRaces() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"lifetime fixture fonts");
    for(bool presentation:{false,true}) {
        struct Race {HANDLE entered,release,stopping; bool presentation; std::atomic<bool> started{false};};
        Race race{CreateEventW(nullptr,TRUE,FALSE,nullptr),CreateEventW(nullptr,TRUE,FALSE,nullptr),
            CreateEventW(nullptr,TRUE,FALSE,nullptr),presentation};
        Check(race.entered && race.release && race.stopping,"frame race events");
        AcquireSRWLockExclusive(&g_frameLock);
        Check(!g_frameThread && !g_frameEvent,"frame race requires an idle scheduler");
        g_frameStopping=false; g_frameEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        ReleaseSRWLockExclusive(&g_frameLock);
        AuditFixture::heldProcedure=CompositionFrameProc;
        AuditFixture::threadEntered=race.entered; AuditFixture::threadRelease=race.release;
        HANDLE starter=CreateThread(nullptr,0,+[](void* value)->DWORD {
            auto& race=*static_cast<Race*>(value);
            AuditFixture::holdThreadCreation=true;
            if(race.presentation) {
                PresentationFixture::Tab tab(L"shutdown race",8);
                race.started=StartPresentation(tab.state);
            } else {
                HWND window=MessageWindow(); RegisterAnimationWindow(window);
                race.started=true; UnregisterAnimationWindow(window); DestroyWindow(window);
            }
            AuditFixture::holdThreadCreation=false;
            return 0;
        },&race,0,nullptr);
        Check(starter!=nullptr,"frame race starter");
        const bool entered=WaitForSingleObject(race.entered,10000)==WAIT_OBJECT_0;
        HANDLE stopper=CreateThread(nullptr,0,+[](void* value)->DWORD {
            SetEvent(static_cast<Race*>(value)->stopping); ShutDownFrames(); return 0;
        },&race,0,nullptr);
        const bool contended=stopper && WaitForSingleObject(race.stopping,5000)==WAIT_OBJECT_0 &&
            WaitForSingleObject(stopper,20)==WAIT_TIMEOUT;
        SetEvent(race.release); // Always release a failed historical startup too.
        const bool joined=WaitForSingleObject(starter,10000)==WAIT_OBJECT_0 && stopper &&
            WaitForSingleObject(stopper,10000)==WAIT_OBJECT_0;
        CloseHandle(starter); if(stopper) CloseHandle(stopper);
        CloseHandle(race.entered); CloseHandle(race.release); CloseHandle(race.stopping);
        AuditFixture::heldProcedure=nullptr;
        Check(entered && contended && joined && race.started,"frame startup/shutdown did not settle");
        AcquireSRWLockShared(&g_frameLock);
        const bool stopped=!g_frameThread && !g_frameEvent && g_activeFrames.empty();
        ReleaseSRWLockShared(&g_frameLock);
        ViewState rejected;
        Check(stopped && !StartPresentation(rejected),"frame startup reopened a stopped scheduler");
        g_frameStopping=false;
    }
    Check(!g_tabWatchThread && !g_tabWatchWake,"observer fixture requires an idle observer");
    struct Observer {HANDLE entered,wake;};
    Observer observer{CreateEventW(nullptr,TRUE,FALSE,nullptr),CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    g_tabWatchStopping=false;
    HANDLE watching=CreateThread(nullptr,0,+[](void* value)->DWORD {
        auto& observer=*static_cast<Observer*>(value); SetEvent(observer.entered);
        return TabVisibilityObserverProc(observer.wake);
    },&observer,0,nullptr);
    Check(watching && WaitForSingleObject(observer.entered,5000)==WAIT_OBJECT_0,"observer startup");
    const bool waited=WaitForSingleObject(watching,20)==WAIT_TIMEOUT;
    SetEvent(observer.wake);
    const bool ended=WaitForSingleObject(watching,5000)==WAIT_OBJECT_0;
    CloseHandle(watching); CloseHandle(observer.entered); CloseHandle(observer.wake);
    Check(waited && ended,"observer did not wait on its captured event with the global handle absent");
    for(int i=0;i<32;++i) {
        g_tabWatchStopping=false; StartTabVisibilityObserver();
        Check(g_tabWatchThread && g_tabWatchWake,"observer restart");
        StopTabVisibilityObserver();
        Check(!g_tabWatchThread && !g_tabWatchWake,"observer stop retained a handle");
    }
    g_tabWatchStopping=false;
    std::cout<<"PASS: held animation/presentation startup races with shutdown; stopped scheduler rejects restart; captured observer event and 32 start/stop cycles\n";
}

static void TestDispatchStateRetirement() {
    const auto count=[] {std::lock_guard lock(g_dispatchStateMutex); return g_dispatchStates.size();};
    const auto baseline=count();
    const auto original=g_dispatchOriginal;
    g_dispatchOriginal=+[](const MSG*)->LRESULT {return 0;};
    for(int i=0;i<64;++i) {
        HANDLE thread=CreateThread(nullptr,0,+[](void*)->DWORD {DispatchHook(nullptr); return 0;},nullptr,0,nullptr);
        Check(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"unrelated dispatch thread");
        CloseHandle(thread);
    }
    g_dispatchOriginal=original;
    Check(count()==baseline,"unrelated dispatches allocated frame scan state");
    struct Live {HANDLE entered,release; std::atomic<bool> valid{false};};
    Live live{CreateEventW(nullptr,TRUE,FALSE,nullptr),CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    HANDLE keeper=CreateThread(nullptr,0,+[](void* value)->DWORD {
        auto& live=*static_cast<Live*>(value); auto* state=CurrentDispatchThreadState();
        const HWND identity=reinterpret_cast<HWND>(&live);
        if(state) state->frames[identity].nextScan=228;
        SetEvent(live.entered); WaitForSingleObject(live.release,INFINITE);
        live.valid=state && CurrentDispatchThreadState()==state && state->frames[identity].nextScan==228;
        return 0;
    },&live,0,nullptr);
    Check(keeper && WaitForSingleObject(live.entered,5000)==WAIT_OBJECT_0,"live dispatch state");
    AuditFixture::infiniteWaits=AuditFixture::secondPolls=0;
    AuditFixture::watchedWait=g_backgroundReaperWake; SetEvent(g_backgroundReaperWake);
    const bool idle=Eventually([&] {return AuditFixture::infiniteWaits>0;});
    Sleep(1200); // A live owner must not cause the old once-per-second poll.
    const bool polling=AuditFixture::secondPolls!=0;
    AuditFixture::watchedWait=nullptr;
    HANDLE failed=CreateThread(nullptr,0,+[](void*)->DWORD {
        AuditFixture::failDispatchWait=true;
        return CurrentDispatchThreadState()==nullptr ? 0 : 1;
    },nullptr,0,nullptr);
    Check(failed && WaitForSingleObject(failed,5000)==WAIT_OBJECT_0,"dispatch wait failure fixture");
    DWORD failedResult=1; GetExitCodeThread(failed,&failedResult); CloseHandle(failed);
    std::atomic<unsigned> registered{0};
    for(int batch=0;batch<8;++batch) {
        std::array<HANDLE,16> threads{};
        for(auto& thread:threads) {
            thread=CreateThread(nullptr,0,+[](void* value)->DWORD {
                if(auto* state=CurrentDispatchThreadState()) {
                    for(UINT_PTR i=1;i<=64;++i) state->frames[reinterpret_cast<HWND>(i)].nextScan=i;
                    static_cast<std::atomic<unsigned>*>(value)->fetch_add(1);
                }
                return 0;
            },&registered,0,nullptr);
        }
        for(HANDLE thread:threads) {
            Check(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"dispatch churn thread");
            CloseHandle(thread);
        }
    }
    const bool retired=Eventually([&] {return count()==baseline+1;});
    SetEvent(live.release);
    const bool joined=WaitForSingleObject(keeper,5000)==WAIT_OBJECT_0;
    CloseHandle(keeper); CloseHandle(live.entered); CloseHandle(live.release);
    Check(idle && !polling && failedResult==0 && registered==128 && retired && joined && live.valid,
        "dispatch reaper polled, lost live state, retained exited threads or mishandled wait failure");
    Check(Eventually([&] {return count()==baseline;}),"last dispatch thread state was not reclaimed");
    std::cout<<"PASS: unrelated dispatches allocate no state; exit notifications reclaim 128 maps, preserve live caches, idle without one-second polling and tolerate wait-registration failure\n";
}

static void TestFrameHookRelease() {
    Check(g_releaseFrameHooksMessage>=0xC000 && g_releaseFrameHooksMessage<=0xFFFF &&
        g_releaseFrameHooksMessage==RegisterWindowMessageW(L"" WH_MOD_ID L"-release-frame-hooks"),
        "frame release message is not registered under the mod identity");
    for (unsigned mode=0;mode<4;++mode) {
        struct Fixture {
            HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            HWND frame=nullptr,cleanup=nullptr;
            unsigned mode=0;
            bool subscribed=false,removed=false;
        } fixture;
        fixture.mode=mode;
        DWORD ownerId=0;
        g_unloading=false;
        HANDLE owner=CreateThread(nullptr,0,+[](void* context)->DWORD {
            auto& fixture=*static_cast<Fixture*>(context);
            HWND frame=fixture.frame=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"private hook owner",
                WS_POPUP,-32000,-32000,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            ViewState state; state.frameHwnd=frame;
            AuditFixture::watchEventRemoval=true;
            if (fixture.mode==1) AuditFixture::rejectSubclass=frame;
            InitializeFrameShellEvents(state);
            AuditFixture::rejectSubclass=nullptr;
            fixture.subscribed=state.frameLifecycleSubscribed;
            AcquireSRWLockShared(&g_shellHookLock);
            if (const auto it=g_shellViewHooks.find(frame);it!=g_shellViewHooks.end())
                fixture.cleanup=it->second.cleanupWindow;
            ReleaseSRWLockShared(&g_shellHookLock);
            if (fixture.mode>=2) {
                RemoveWindowSubclass(frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass));
                DestroyWindow(frame); // Simulate loss of the frame without our NCDESTROY.
            }
            if (fixture.mode==3) {
                RemoveWindowSubclass(fixture.cleanup,FrameHookCleanupSubclass,reinterpret_cast<UINT_PTR>(FrameHookCleanupSubclass));
                DestroyWindow(fixture.cleanup);
                SetEvent(fixture.ready); return 0; // USER automatically unhooks at thread exit.
            }
            SetEvent(fixture.ready);
            MSG message{};
            while (GetMessageW(&message,nullptr,0,0)>0) DispatchMessageW(&message);
            fixture.removed=AuditFixture::eventsRemoved>0 && !AuditFixture::eventRemovalFailures &&
                !IsWindow(fixture.cleanup);
            ReleaseFrameShellViewHook(state);
            if (IsWindow(frame)) DestroyWindow(frame);
            return 0;
        },&fixture,0,&ownerId);
        Check(owner && WaitForSingleObject(fixture.ready,5000)==WAIT_OBJECT_0 &&
            fixture.subscribed && fixture.cleanup,"frame release owner fixture");
        if (mode==3) Check(WaitForSingleObject(owner,5000)==WAIT_OBJECT_0,"dead installer fixture");
        g_unloading=true;
        if (mode<2) {
            SendMessageW(fixture.frame,WM_APP+73,0,0);
            AcquireSRWLockShared(&g_shellHookLock);
            const bool retained=g_shellViewHooks.contains(fixture.frame);
            ReleaseSRWLockShared(&g_shellHookLock);
            Check(retained,"foreign WM_APP message released frame hooks");
        }
        HANDLE clearing=CreateThread(nullptr,0,+[](void*)->DWORD {ClearFrameShellViewHooks(); return 0;},nullptr,0,nullptr);
        const bool cleared=clearing && WaitForSingleObject(clearing,5000)==WAIT_OBJECT_0;
        PostThreadMessageW(ownerId,WM_QUIT,0,0);
        const bool joined=WaitForSingleObject(owner,5000)==WAIT_OBJECT_0;
        CloseHandle(owner); if (clearing) CloseHandle(clearing); CloseHandle(fixture.ready);
        AcquireSRWLockShared(&g_shellHookLock);
        const bool absent=!g_shellViewHooks.contains(fixture.frame);
        ReleaseSRWLockShared(&g_shellHookLock);
        Check(cleared && joined && absent && (mode==3 || fixture.removed),
            "frame cleanup hung or removed WinEvent hooks outside their installer STA");
    }
    g_unloading=false;
    std::cout<<"PASS: registered release message ignores foreign WM_APP; owner-STA cleanup handles live frames, failed subclass installation, destroyed frames and exited installers\n";
}
