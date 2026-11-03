// Private HWNDs and real USER transactions; no injected input or fabricated messages.
static void TestOverlayStackFilter() {
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    for(const auto* name:{L"CabinetWClass",L"ShellTabWindowClass"}) {
        cls.lpszClassName=name; Check(RegisterClassW(&cls) || GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"stack fixture class");
    }
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"private stack",
        WS_POPUP|WS_VISIBLE,-32000,-32000,640,480,nullptr,nullptr,cls.hInstance,nullptr);
    Check(frame!=nullptr,"stack fixture frame");
    struct Cleanup {HWND frame; ~Cleanup() {g_tabOverlaysByFrame.erase(frame); DestroyWindow(frame);}} cleanup{frame};
    const auto child=[&](const wchar_t* name) {
        HWND hwnd=CreateWindowExW(0,name,L"",WS_CHILD|WS_VISIBLE,0,0,640,480,frame,nullptr,cls.hInstance,nullptr);
        Check(hwnd!=nullptr,"stack fixture child"); return hwnd;
    };
    HWND overlay=child(L"STATIC"),popup=child(L"STATIC"),toggle=child(L"BUTTON");
    HWND hidden=child(L"STATIC"); ShowWindow(hidden,SW_HIDE);
    g_tabOverlaysByFrame[frame]={hidden,overlay};
    const UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE;
    const auto place=[&](HWND hwnd,HWND after) {return NativeSetWindowPosHook(hwnd,after,0,0,0,0,flags);};
    HANDLE rejected=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(rejected!=nullptr,"unrelated stack completion");
    AcquireSRWLockExclusive(&g_tabRegistryLock);
    std::thread unrelated([&] {KeepBelowOverlayStack(popup,HWND_TOP,flags); SetEvent(rejected);});
    const bool independent=WaitForSingleObject(rejected,10000)==WAIT_OBJECT_0;
    ReleaseSRWLockExclusive(&g_tabRegistryLock);
    unrelated.join(); CloseHandle(rejected);
    Check(independent,"unknown surface took the shared tab registry lock");
    for(const auto* name:{L"ShellTabWindowClass",L"CtrlNotifySink",L"SHELLDLL_DefView"}) {
        HWND host=child(name);
        Check(place(overlay,HWND_TOP) && place(host,HWND_TOP) && GetWindow(host,GW_HWNDPREV)==overlay,
            "known content sibling covered GPU surface");
        Check(place(popup,HWND_TOP) && GetWindow(popup,GW_HWNDPREV)==nullptr,"unknown native surface was demoted");
        Check(place(toggle,HWND_TOP),"native control stacking");
        HDWP batch=BeginDeferWindowPos(1);
        batch=NativeDeferWindowPosHook(batch,host,toggle,0,0,0,0,flags);
        Check(batch && EndDeferWindowPos(batch) && GetWindow(host,GW_HWNDPREV)==overlay,
            "deferred known sibling covered GPU surface");
        for(HWND after:{HWND_BOTTOM,HWND_TOPMOST,HWND_NOTOPMOST})
            Check(KeepBelowOverlayStack(host,after,flags)==after,"special z-order request changed");
        Check(KeepBelowOverlayStack(host,HWND_TOP,flags|SWP_NOZORDER)==HWND_TOP,"NOZORDER changed");
        DestroyWindow(host);
    }
    ShowWindow(overlay,SW_HIDE);
    HWND host=child(L"ShellTabWindowClass");
    Check(KeepBelowOverlayStack(host,HWND_TOP,flags)==HWND_TOP,"hidden overlay demoted native content");
    std::cout<<"PASS: immediate/deferred native content stacking, unknown surfaces and hidden overlays\n";
}

static void TestCompositorWaitErrors() {
    HANDLE invalid=nullptr;
    const DWORD result=WaitForMultipleObjects(1,&invalid,FALSE,0);
    const DWORD error=GetLastError();
    Check(result==WAIT_FAILED && error==ERROR_INVALID_HANDLE,"real Windows failed-wait fixture");
    Check(CompositorWaitResult(result)==HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE),"failed wait lost last error");
    // DComp also returns NTSTATUS (documented separately from wait sentinels).
    Check(CompositorWaitResult(0xC0000008UL)==HRESULT(0xD0000008UL),"NTSTATUS conversion regressed");
    std::cout<<"PASS: native failed wait preserves Win32 error and NTSTATUS handling\n";
}

namespace EmptyFrameSummaryFixture {
inline std::wstring line;
inline void Capture(PCWSTR text) {line=text;}
}
static void TestEmptyFrameSummary() {
    SelectionLogFixture::frameSink=EmptyFrameSummaryFixture::Capture;
    WriteFrameSummary(FrameSummary{});
    SelectionLogFixture::frameSink=nullptr;
    const auto& line=EmptyFrameSummaryFixture::line;
    Check(!line.empty() && line.find(L"nan")==std::wstring::npos && line.find(L"inf")==std::wstring::npos &&
        line.find(L"cpu-ms avg=0.00")!=std::wstring::npos,"empty frame summary contains nonfinite averages");
    std::cout<<"PASS: zero-frame summary emits finite zero averages\n";
}

static void TestDragCountLocale() {
    for(const auto* locale:{L"en-US",L"ar-SA",L"fa-IR"}) for(bool rtl:{false,true}) {
        Regression::Scope scope("drag count rtl="+std::to_string(rtl));
        wchar_t digits[32]{}; DWORD policy=1;
        Check(GetLocaleInfoEx(locale,LOCALE_SNATIVEDIGITS,digits,ARRAYSIZE(digits)) &&
            GetLocaleInfoEx(locale,LOCALE_IDIGITSUBSTITUTION|LOCALE_RETURN_NUMBER,
                reinterpret_cast<LPWSTR>(&policy),2),"native locale digit policy");
        for(int count:{2,37,999,1200}) {
            const auto ascii=std::to_wstring(std::min(count,999));
            std::wstring expected=ascii;
            if(policy==2 || (policy==0 && rtl)) for(auto& ch:expected) ch=digits[ch-L'0'];
            if(count>999) expected+=L'+';
            Check(FormatDragCount(count,rtl,locale)==expected,"badge ignored locale digits, direction or overflow suffix");
        }
        // Independent Windows formatting supplies grouping and separators;
        // native-digit expectations are applied without using the mod helper.
        wchar_t grouped[128]{};
        Check(GetNumberFormatEx(locale,0,L"1234567",nullptr,grouped,ARRAYSIZE(grouped)),"native grouped section count");
        wchar_t decimal[8]{};
        Check(GetLocaleInfoEx(locale,LOCALE_SDECIMAL,decimal,ARRAYSIZE(decimal)),"native decimal separator");
        std::wstring expected=grouped;
        const auto fraction=expected.find(decimal);
        if(fraction!=std::wstring::npos) expected.resize(fraction);
        if(policy==2 || (policy==0 && rtl)) for(auto& ch:expected)
            if(ch>=L'0' && ch<=L'9') ch=digits[ch-L'0'];
        Check(FormatSelectionCount(1234567,rtl,locale)==expected,"section count lost grouping or native digits");
        Check(FormatSelectionCount(37,rtl,locale)==FormatDragCount(37,rtl,locale),
            "section and drag badges disagree on digit substitution");
    }
    std::cout<<"PASS: section/drag counts follow actual English/Arabic/Persian digit policies, grouping and overflow\n";
}

namespace DragPropertyFixture {
inline bool destroyed=false,removed=false;
inline LRESULT CALLBACK Observe(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_DESTROY) {destroyed=true; removed=GetPropW(hwnd,kDragPreviewProperty)==nullptr;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
}
static void TestDragPreviewLifecycle(const std::wstring& fixture) {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"lifecycle fonts");
    {
        PresentationFixture::Tab tab(L"drag lifecycle",1);
        auto& state=tab.state; state.entries[0].path=fixture;
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        Check(StartPresentation(state),"drag lifecycle presentation lane");
        ShellDragFeedback feedback(state,0,37,nullptr);
        Check(feedback.HasPreview() && state.dragPreview,"drag preview creation");
        const HWND popup=state.dragPreview->window;
        Check(GetPropW(popup,kDragPreviewProperty)!=nullptr,"drag identity missing");
        DragPropertyFixture::destroyed=DragPropertyFixture::removed=false;
        Check(SetWindowSubclass(popup,DragPropertyFixture::Observe,1,0),"drag destroy observer");
        feedback.ReleasePreview(); feedback.ReleasePreview();
        Check(DragPropertyFixture::destroyed && DragPropertyFixture::removed && !IsWindow(popup) && !state.dragPreview,
            "drag window identity survived until destruction or release was not idempotent");
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: preview identity is removed before real USER destruction\n";
}
