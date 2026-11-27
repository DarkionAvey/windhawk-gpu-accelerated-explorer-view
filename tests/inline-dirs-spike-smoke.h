// Phase 0 spikes for inline folders and Miller columns (focused probes, run
// on request). They record how the real Shell and the tab's lanes behave;
// the results are written up in docs/, not asserted as product behavior.

// Spike: SBSP_WRITENOHISTORY. Miller columns move the native view between
// columns; those moves must not flood Back/Forward. A private ExplorerBrowser
// with its own travel log navigates root -> child -> grandchild, the last with
// SBSP_WRITENOHISTORY, then goes Back and reports where it lands.
static void TestInlineDirsSpikeWriteNoHistory(const std::wstring& fixture) {
    const auto root=JoinPath(fixture,L"spike-history");
    InlineDirs::MakeFolder(root);
    const auto child=JoinPath(root,L"child");
    InlineDirs::MakeFolder(child);
    const auto grandchild=JoinPath(child,L"grandchild");
    InlineDirs::MakeFolder(grandchild);
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW; cls.lpszClassName=L"CabinetWClass";
    RegisterClassW(&cls);
    const HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"private history spike",
        WS_POPUP|WS_VISIBLE,-32000,-32000,800,600,nullptr,nullptr,cls.hInstance,nullptr);
    Check(frame!=nullptr,"spike frame");
    ComPtr<IExplorerBrowser> browser;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&browser))),"spike browser");
    RECT bounds{0,0,800,600}; FOLDERSETTINGS settings{FVM_DETAILS,FWF_NOBROWSERVIEWSTATE};
    Check(SUCCEEDED(browser->Initialize(frame,&bounds,&settings)),"spike browser init");
    browser->SetOptions(EBO_NOBORDER); // Travel log on.
    auto* events=new OptionEffects::BrowserEvents;
    DWORD cookie=0; Check(SUCCEEDED(browser->Advise(events,&cookie)),"spike events");
    const auto pump=[] { MSG m{}; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&m); DispatchMessageW(&m);} };
    const auto current=[&] {
        ComPtr<IFolderView2> view; std::wstring path; ComPtr<IShellFolder> folder;
        if(SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view)))) GetFolderPath(view.Get(),path,folder);
        return path;
    };
    const auto go=[&](const std::wstring* path,UINT flags) {
        events->complete=false;
        OwnedPidl pidl;
        if(path) { PIDLIST_ABSOLUTE raw=nullptr; Check(SUCCEEDED(SHParseDisplayName(path->c_str(),nullptr,&raw,0,nullptr)),"spike pidl"); pidl.reset(raw); }
        const HRESULT hr=browser->BrowseToIDList(pidl.get(),flags);
        const bool done=SUCCEEDED(hr) && Eventually([&] {pump(); return events->complete;},15000);
        return std::make_pair(hr,done);
    };
    const auto narrow=[](const std::wstring& w){ return std::string(w.begin(),w.end()); };
    Check(go(&root,SBSP_ABSOLUTE).second && go(&child,SBSP_ABSOLUTE).second,"spike setup navigation");
    const auto quiet=go(&grandchild,SBSP_ABSOLUTE|SBSP_WRITENOHISTORY);
    std::cout<<"SPIKE write-no-history navigate hr=0x"<<std::hex<<unsigned(quiet.first)<<std::dec
        <<" completed="<<quiet.second<<" at="<<narrow(current())<<std::endl;
    const auto back=go(nullptr,SBSP_NAVIGATEBACK);
    std::cout<<"SPIKE back after quiet navigation hr=0x"<<std::hex<<unsigned(back.first)<<std::dec
        <<" completed="<<back.second<<" at="<<narrow(current())
        <<" (child="<<narrow(child)<<", root="<<narrow(root)<<")"<<std::endl;
    const auto forward=go(nullptr,SBSP_NAVIGATEFORWARD);
    std::cout<<"SPIKE forward hr=0x"<<std::hex<<unsigned(forward.first)<<std::dec
        <<" completed="<<forward.second<<" at="<<narrow(current())<<std::endl;
    browser->Unadvise(cookie); browser->Destroy(); events->Release(); browser.Reset();
    DestroyWindow(frame);
    std::cout<<"PASS: SBSP_WRITENOHISTORY probe recorded (see SPIKE lines)\n";
}

// Spike: latency. How long an expanded folder takes from the toggle to its
// grafted rows, against listing the same folder as the tab's own folder, and
// what one regraft costs on the UI thread with many rows above it.
static void TestInlineDirsSpikeLatency(const std::wstring& fixture) {
    using namespace InlineDirs;
    Regression::Claim(Regression::QuietCpu,"uncontended inline folder listing timings");
    StyleFixture::Session style;
    OptionScope option(true);
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    const auto ms=[&](LARGE_INTEGER a,LARGE_INTEGER b) { return (b.QuadPart-a.QuadPart)*1000.0/frequency.QuadPart; };
    for(int size:{100,1000,10000}) {
        const auto root=JoinPath(fixture,L"spike-latency-"+std::to_wstring(size));
        MakeFolder(root);
        const auto inner=JoinPath(root,L"inner");
        MakeFolder(inner);
        for(int i=0;i<size;++i) {
            wchar_t name[32]; swprintf_s(name,L"f%05d.txt",i);
            MakeFile(JoinPath(inner,name));
            MakeFile(JoinPath(root,name)); // As many rows beside the folder.
        }
        std::vector<double> own,inline_,regraft;
        for(int run=0;run<5;++run) {
            LARGE_INTEGER a{},b{};
            {
                QueryPerformanceCounter(&a);
                Tab opened(inner,96);
                QueryPerformanceCounter(&b);
                own.push_back(ms(a,b));
            }
            Tab tab(root,96);
            QueryPerformanceCounter(&a);
            tab.Toggle(L"inner");
            QueryPerformanceCounter(&b);
            inline_.push_back(ms(a,b));
            Check(tab.state.total==2*size+1,"spike listing incomplete");
            QueryPerformanceCounter(&a);
            RegraftInlineTree(tab.state,false);
            QueryPerformanceCounter(&b);
            regraft.push_back(ms(a,b));
        }
        const auto median=[](std::vector<double> v) { std::sort(v.begin(),v.end()); return v[v.size()/2]; };
        std::cout<<"PROFILE: spike items="<<size<<" own-folder listing+attach median="<<median(own)
            <<"ms inline toggle-to-grafted median="<<median(inline_)<<"ms UI regraft ("<<(2*size+1)
            <<" rows) median="<<median(regraft)<<"ms"<<std::endl;
    }
    std::cout<<"PASS: inline folder latency probe recorded (see PROFILE lines)\n";
}
