// Trace transport is independent of Shell providers, and its writer belongs to
// the same per-tab lifetime inventory as every other background lane.
static ULONGLONG TraceThreadCpu(HANDLE thread) {
    FILETIME created{},exited{},kernel{},user{};
    Check(GetThreadTimes(thread,&created,&exited,&kernel,&user)!=FALSE,"trace thread CPU query");
    const auto ticks=[](FILETIME value) { return (static_cast<ULONGLONG>(value.dwHighDateTime)<<32)|value.dwLowDateTime; };
    return ticks(kernel)+ticks(user);
}
namespace FrameSummaryIo {
inline HANDLE pipe=nullptr,entered=nullptr;
inline std::atomic<bool> first{true},written{false},flushed{false};
inline DWORD loggingThread=0;
inline void Log(PCWSTR text) {
    if(!first.exchange(false)) return;
    loggingThread=GetCurrentThreadId();
    DWORD bytes=0;
    const DWORD length=static_cast<DWORD>((wcslen(text)+1)*sizeof(wchar_t));
    written=WriteFile(pipe,text,length,&bytes,nullptr) && bytes==length;
    SetEvent(entered);
    // Real Windows I/O: this waits until the private client actually reads.
    // No application messages or assumed logging delay emulate backpressure.
    if(written) flushed=FlushFileBuffers(pipe)!=FALSE;
}
}
static void TestFrameSummaryIo() {
    const bool oldTrace=g_pipelineTracing.exchange(false),oldUi=g_uiTransitionLogging.exchange(false);
    const bool oldFrames=g_frameTimeLogging.exchange(true);
    const auto name=L"\\\\.\\pipe\\gpu-frame-summary-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(g_nextToken++);
    HANDLE server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND,PIPE_TYPE_BYTE|PIPE_WAIT,1,65536,0,0,nullptr);
    Check(server!=INVALID_HANDLE_VALUE,"frame summary private pipe server");
    HANDLE client=CreateFileW(name.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    Check(client!=INVALID_HANDLE_VALUE,"frame summary private pipe client");
    Check(ConnectNamedPipe(server,nullptr) || GetLastError()==ERROR_PIPE_CONNECTED,"frame summary private pipe connection");
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(entered && done,"frame summary observation events");
    ViewState state; state.hwnd=MessageWindow();
    EnsurePipelineTrace(state);
    Check(state.background->traceWorker.thread!=nullptr,"frame-only logging did not start its tab writer");
    state.presentationOnly=true;
    FrameSummaryIo::pipe=server; FrameSummaryIo::entered=entered;
    FrameSummaryIo::first=true; FrameSummaryIo::written=false; FrameSummaryIo::flushed=false;
    SelectionLogFixture::frameSink=FrameSummaryIo::Log;
    DWORD producerId=0;
    std::thread producer([&] {
        producerId=GetCurrentThreadId();
        const auto deadline=GetTickCount64()+10000;
        // Use actual profiler frames until the native pipe licenses the check.
        // Contended diagnostic records are deliberately allowed to be dropped.
        do { for(unsigned i=0;i<8;++i) {BeginFrameProfile(state); EndFrameProfile(state);} }
        while(WaitForSingleObject(entered,0)!=WAIT_OBJECT_0 && GetTickCount64()<deadline);
        for(unsigned i=0;i<8;++i) {BeginFrameProfile(state); EndFrameProfile(state);}
        SetEvent(done);
    });
    const bool logging=WaitForSingleObject(entered,10000)==WAIT_OBJECT_0;
    const bool progressed=logging && WaitForSingleObject(done,10000)==WAIT_OBJECT_0;
    const bool held=logging && FrameSummaryIo::written && !FrameSummaryIo::flushed;
    std::vector<wchar_t> output(32768); DWORD read=0;
    const bool received=logging && ReadFile(client,output.data(),static_cast<DWORD>(output.size()*sizeof(wchar_t)),&read,nullptr);
    // Disconnect also releases a failed fixture's pending native write/flush.
    CloseHandle(client); DisconnectNamedPipe(server);
    producer.join();
    SelectionLogFixture::frameSink=nullptr;
    auto lifetime=state.background;
    Stop(state.background);
    lifetime->Join(); // Drain the released native I/O before closing its handles.
    lifetime.reset(); DestroyWindow(state.hwnd);
    CloseHandle(server); CloseHandle(entered); CloseHandle(done);
    g_pipelineTracing=oldTrace; g_uiTransitionLogging=oldUi; g_frameTimeLogging=oldFrames;
    Check(logging && progressed && held && received,"frame profiler waited for a blocked native log consumer");
    const auto attribution=L"thread="+std::to_wstring(producerId)+L" ";
    Check(FrameSummaryIo::loggingThread!=producerId && wcsstr(output.data(),attribution.c_str()) &&
        wcsstr(output.data(),L"frames=8 "),"deferred frame summary lost its producer attribution or metrics");
    std::cout<<"PASS: frame-only diagnostics preserve producer metrics and keep profiling while the tab logger waits for actual named-pipe consumption\n";
}
static void TestIdleTraceCpu() {
    const bool previousTrace=g_pipelineTracing.exchange(true);
    const bool previousUi=g_uiTransitionLogging.exchange(false);
    std::vector<std::unique_ptr<ViewState>> states;
    for(unsigned i=0;i<25;++i) {
        auto state=std::make_unique<ViewState>(); state->hwnd=MessageWindow();
        EnsurePipelineTrace(*state);
        Check(state->background->traceWorker.thread!=nullptr,"idle trace writer startup");
        states.push_back(std::move(state));
    }
    // Actual queue delivery licenses the idle measurement, never a startup sleep.
    Check(Eventually([&] {
        return std::all_of(states.begin(),states.end(),[](const auto& state) {
            return state->background->pipeline->written.load()>=1;
        });
    }),"idle trace initial context was not delivered");
    const auto observeCpu=[&](const char* mode) {
        ULONGLONG before=0;
        for(const auto& state : states) before+=TraceThreadCpu(state->background->traceWorker.thread);
        const auto began=GetTickCount64();
        Sleep(400); // CPU observation interval, not a completion/readiness assumption.
        const auto elapsed=GetTickCount64()-began;
        ULONGLONG after=0;
        for(const auto& state : states) after+=TraceThreadCpu(state->background->traceWorker.thread);
        const double cpuMs=(after-before)/10000.;
        std::cout<<"TRACE IDLE writers=25 mode="<<mode<<" elapsed-ms="<<elapsed<<" aggregate-cpu-ms="<<cpuMs<<std::endl;
        return cpuMs<std::max(200.,elapsed*.5);
    };
    const bool idle=observeCpu("enabled");
    // A disabled writer must sleep too, and later production must wake it.
    g_pipelineTracing=false;
    for(auto& state : states) EnsurePipelineTrace(*state);
    const bool disabledIdle=observeCpu("disabled");
    g_pipelineTracing=true;
    std::vector<unsigned long long> expected;
    for(auto& state : states) {
        EnsurePipelineTrace(*state);
        expected.push_back(state->background->pipeline->written.load()+1);
        Check(Eventually([&] { return state->background->pipeline->Push(PipelineRecord{.operation=L"idle.resume"}); }),
            "idle trace enqueue remained contended");
    }
    const bool resumed=Eventually([&] {
        for(size_t i=0;i<states.size();++i)
            if(states[i]->background->pipeline->written.load()<expected[i]) return false;
        return true;
    });
    std::vector<std::weak_ptr<TabBackground>> retired;
    for(auto& state : states) { retired.push_back(state->background); Stop(state->background); DestroyWindow(state->hwnd); }
    states.clear();
    Check(Eventually([&] { return std::all_of(retired.begin(),retired.end(),[](const auto& work) { return work.expired(); }); }),
        "idle trace writers did not drain during stop");
    g_pipelineTracing=previousTrace; g_uiTransitionLogging=previousUi;
    Check(idle && disabledIdle,"empty trace writers consumed CPU instead of sleeping");
    Check(resumed,"trace enqueue/re-enable lost the idle writer wake");
    std::cout<<"PASS: 25 actual trace writers sleep after drain, resume after disable/enqueue and stop without lost wakes\n";
}

static void TestPipelineTrace() {
    const bool oldTrace=g_pipelineTracing.exchange(false);
    const bool oldUi=g_uiTransitionLogging.exchange(false);
    const bool oldFrames=g_frameTimeLogging.exchange(false);
    auto work=std::make_shared<TabBackground>();
    work->pipeline=std::make_unique<PipelineTrace>();
    work->pipelinePublished=work->pipeline.get();
    auto& ring=*work->pipeline;
    PipelineEvent(*work,{.operation=L"disabled"});
    Check(ring.count==0,"disabled trace accepted events");
    g_uiTransitionLogging=true;
    PipelineEvent(*work,{.operation=L"ordinary"});
    Check(ring.count==0,"UI-only tracing enabled unrelated pipeline traffic");
    PipelineEvent(*work,{.operation=L"ui.only"},true);
    PipelineRecord ui;
    Check(ring.Drain(&ui,1)==1 && ui.qpc && wcscmp(ui.operation,L"ui.only")==0,
        "UI-only diagnostics were lost without full pipeline tracing");
    g_uiTransitionLogging=false;
    g_pipelineTracing=true; ring.frame=42; ring.input=7;
    { PipelineScope scope(*work,L"paired",101,56); Sleep(2); }
    std::array<PipelineRecord,128> batch;
    Check(ring.Drain(batch.data(),batch.size())==2 && batch[0].phase==L'B' && batch[1].phase==L'E' &&
        batch[0].span==batch[1].span && batch[0].frame==42 && batch[1].input==7 &&
        batch[1].a==101 && batch[1].b==56 && batch[1].v[0]>=1.,"trace lost correlation or scope duration");
    AcquireSRWLockExclusive(&ring.lock);
    PipelineEvent(*work,{.operation=L"contended"});
    ReleaseSRWLockExclusive(&ring.lock);
    Check(ring.dropped.exchange(0)==1,"trace waited for its writer instead of reporting a dropped record");
    for(size_t i=0;i<ring.capacity+3;++i) PipelineEvent(*work,{.operation=L"overflow"});
    Check(ring.count==ring.capacity && ring.dropped.exchange(0)==3,"trace buffer grew or silently lost overflow");
    while(ring.Drain(batch.data(),batch.size())) {}
    std::array<std::thread,4> producers;
    for(auto& thread : producers) thread=std::thread([&] {
        for(int i=0;i<1000;++i) PipelineEvent(*work,{.operation=L"producer"});
    });
    for(auto& thread : producers) thread.join();
    size_t accepted=0; unsigned long long previous=0;
    for(size_t n;(n=ring.Drain(batch.data(),batch.size()))!=0;) for(size_t i=0;i<n;++i) {
        Check(batch[i].sequence>previous && batch[i].thread && batch[i].frame==42,
            "parallel trace producers corrupted record ordering or ownership");
        previous=batch[i].sequence; ++accepted;
    }
    Check(accepted+ring.dropped.exchange(0)==4000,"parallel trace loss was not accounted");
    work.reset();

    ViewState state,other;
    state.hwnd=MessageWindow(); other.hwnd=MessageWindow();
    g_pipelineTracing=false; EnsurePipelineTrace(state);
    Check(!state.background->pipeline && !state.background->traceWorker.thread,"disabled tracing started a writer");
    g_pipelineTracing=true; EnsurePipelineTrace(state); EnsurePipelineTrace(other);
    auto* trace=state.background->pipeline.get();
    Check(trace && other.background->pipeline && trace!=other.background->pipeline.get(),"tabs shared trace buffers");
    { PipelineScope scope(state.background,L"writer-test"); }
    BeginFrameProfile(state);
    { FrameStageScope stage(state,FrameStage::Draw); }
    BeginFrameProfile(state); EndFrameProfile(state); EndFrameProfile(state);
    Check(state.pipelineFrames.empty() && trace->nextFrame==2,"nested frame traces corrupted their parent");
    Check(Eventually([&] { return trace->written+trace->lostTotal>=12; }),
        "writer did not drain/account input/frame/stage records without normal profiling");
    Check(trace->writerThread!=GetCurrentThreadId() && state.background->traceWorker.thread!=other.background->traceWorker.thread,
        "trace formatting ran on the UI thread or another tab's writer");
    const auto writer=state.background->traceWorker.thread;
    g_pipelineTracing=false; EnsurePipelineTrace(state);
    Check(!trace->enabled,"trace settings did not disable producers");
    g_uiTransitionLogging=true; EnsurePipelineTrace(state);
    Check(trace->enabled && state.background->traceWorker.thread==writer,"UI diagnostics created a second writer");
    const auto written=trace->written.load();
    TraceUiState(state,L"ui.test");
    Check(Eventually([&] { return trace->written> written; }),"UI-only writer did not drain state changes");
    g_uiTransitionLogging=false; EnsurePipelineTrace(state);
    Check(!trace->enabled,"disabling both trace options retained producers");
    g_pipelineTracing=true; EnsurePipelineTrace(state);
    Check(trace->enabled && state.background->traceWorker.thread==writer,"re-enabling tracing leaked another writer");
    std::weak_ptr<TabBackground> first=state.background, second=other.background;
    Stop(state.background); Stop(other.background);
    Check(Eventually([&] { return first.expired() && second.expired(); }),"trace workers/buffers escaped the lifetime regulator");
    DestroyWindow(state.hwnd); DestroyWindow(other.hwnd);
    g_pipelineTracing=oldTrace; g_frameTimeLogging=oldFrames; g_uiTransitionLogging=oldUi;
    TestIdleTraceCpu();
    TestFrameSummaryIo();
    std::cout<<"PASS: disabled trace, nonblocking bounded transport, accounted overflow/contended producers, independent background writers, nested frames and stop/re-enable/reclamation\n";
}
