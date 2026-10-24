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
    Check(registered==128 && retired && joined && live.valid,"dispatch reaper lost live state or retained exited threads");
    Check(Eventually([&] {return count()==baseline;}),"last dispatch thread state was not reclaimed");
    std::cout<<"PASS: unrelated dispatches allocate no state; regulator reclaims 128 exited-thread maps while a live thread retains its independent cache\n";
}
