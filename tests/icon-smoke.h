static HWND IconTestWindow() {
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"icon-pipeline-test",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 420, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hwnd != nullptr, "offscreen icon window");
    return hwnd;
}

template<class State = ViewState>
static void TestIconQueueIsolation() {
    State probe;
    if constexpr (requires { probe.background->iconProviders; }) {
        IconFixture::Session fixture;
        Check(IconFixture::gate != nullptr, "icon provider gate");
        std::vector<std::unique_ptr<State>> tabs;
        auto makeTab = [&](int count, bool slow) -> State& {
            auto state = std::make_unique<State>();
            state->hwnd = IconTestWindow(); state->background->hwnd = state->hwnd;
            state->visible = state->inScope = state->detailsViewActive = true;
            state->columnsView = false; state->rowPx = 34.f;
            state->iconToken = tabs.size() + 100;
            for (int i = 0; i < count; ++i) {
                Entry entry;
                entry.name = (slow || i == 0 ? L"slow" : L"fast") + std::to_wstring(i) + L".exe";
                entry.path = L"wh-icon-test:" + std::to_wstring(tabs.size()) + L":" + entry.name;
                state->entries.push_back(std::move(entry));
            }
            state->total = count;
            tabs.push_back(std::move(state));
            return *tabs.back();
        };
        auto& first = makeTab(12, false);
        RequestVisibleIcons(first);
        Check(!first.firstContentSubmittedAt, "icon fixture already submitted content");
        Check(Eventually([&] {
            ReceiveIcons(first);
            for (int i = 1; i < first.total; ++i) {
                const auto found = first.icons.find(first.entries[i].path);
                if (found == first.icons.end() || !found->second.pixels.Ready()) return false;
            }
            return IconFixture::blocked.load() == 1;
        }), "one slow EXE blocked later icons or icon fetching waited for optional startup");
        Check(first.icons.at(first.entries[0].path).pending, "blocked first EXE unexpectedly completed");
        auto& blocked = makeTab(40, true);
        IconFixture::blockAll = true;
        RequestVisibleIcons(blocked);
        Check(Eventually([&] { return IconFixture::blocked.load() == 1 + blocked.background->iconProviders.size(); }),
              "per-tab icon extraction did not fill its independent provider slots");
        Check(Eventually([&] {
            ReceiveIcons(blocked);
            return blocked.icons.contains(L"type:.exe") && blocked.icons.at(L"type:.exe").pixels.Ready();
        }), "association icon waited behind blocked executable providers");
        // Keep these providers blocked, but allow another tab's fast requests.
        IconFixture::blockAll = false;
        auto& other = makeTab(4, false);
        other.entries[0].name = L"fast0.exe";
        other.entries[0].path = L"wh-icon-test:other:fast0.exe";
        other.entries[1].path = L"wh-icon-test:other:fast1.exe";
        other.entries[2].path = L"wh-icon-test:transient.exe";
        other.entries[3].path = L"wh-icon-test:permanent.exe";
        RequestVisibleIcons(other);
        Check(Eventually([&] {
            ReceiveIcons(other);
            return other.icons.contains(other.entries[0].path) && other.icons.at(other.entries[0].path).pixels.Ready() &&
                other.icons.contains(other.entries[1].path) && other.icons.at(other.entries[1].path).pixels.Ready() &&
                other.icons.contains(other.entries[2].path) && other.icons.at(other.entries[2].path).pixels.Ready() &&
                other.icons.contains(other.entries[3].path) && other.icons.at(other.entries[3].path).resolved;
        }), "blocked tabs occupied another tab's icon workers");
        Check(IconFixture::transientCalls == 2 && IconFixture::permanentCalls == 2,
              "empty Shell responses were cached forever or retried without a bound");
        RequestVisibleIcons(other); ReceiveIcons(other);
        Check(IconFixture::permanentCalls == 2, "permanent icon failure was resubmitted after its retry limit");
        const auto oldId = blocked.icons.at(blocked.entries[5].path).pendingRequestId;
        blocked.scrollNow = blocked.scrollTarget = 15 * blocked.rowPx;
        RequestVisibleIcons(blocked);
        Check(!blocked.icons.at(blocked.entries[5].path).pending &&
              !blocked.icons.at(blocked.entries[5].path).resolved, "offscreen queued icons were stranded as pending/resolved");
        const auto visible = VisibleItemRows(blocked, 0.f);
        {
            ExclusiveSrwLock lock(blocked.background->iconLock);
            Check(!blocked.background->iconJobs.empty(), "scrolled icon demand was not queued");
            const auto& front = blocked.background->iconJobs.front();
            Check(std::any_of(visible.begin(), visible.end(), [&](int row) {
                return front.key == blocked.entries[row].path;
            }), "overscan or old viewport stayed ahead of visible icons");
        }
        blocked.scrollNow = blocked.scrollTarget = 0;
        RequestVisibleIcons(blocked);
        Check(blocked.icons.at(blocked.entries[5].path).pending &&
              blocked.icons.at(blocked.entries[5].path).pendingRequestId != oldId,
              "cancelled queued icon could not be requested on return");
        // Association requests remain useful across navigation; per-file jobs
        // are generation-bound. An obsolete result cannot fill a new request.
        const auto associationRequest = first.icons.at(L"type:.exe").pendingRequestId;
        CancelDepartedIconJobs(first, ++first.iconToken);
        Check(std::all_of(first.icons.begin(), first.icons.end(), [](const auto& pair) {
            return pair.second.association || !pair.second.pending;
        }), "navigation left cancelled per-file requests pinned in the bounded cache");
        RequestVisibleIcons(first);
        Check(first.icons.at(L"type:.exe").pendingRequestId == associationRequest,
              "navigation threw away a reusable association icon");
        std::vector<std::weak_ptr<TabBackground>> lifetimes;
        for (auto& tab : tabs) { lifetimes.push_back(tab->background); Stop(tab->background); }
        Check(!lifetimes[0].expired() && !lifetimes[1].expired(), "blocked icon context was destroyed before return");
        SetEvent(IconFixture::gate);
        Check(Eventually([&] {
            return std::all_of(lifetimes.begin(), lifetimes.end(), [](const auto& weak) { return weak.expired(); });
        }), "icon pool workers did not retire through the regulator");
        for (auto& tab : tabs) DestroyWindow(tab->hwnd);
        CloseHandle(IconFixture::gate); IconFixture::gate = nullptr;
        std::cout << "PASS: cold EXE requests precede first content, blocked providers leave peers/associations/tabs independent; viewport priority, retry and pool retirement\n";
    }
}

// Use real, read-only Windows executable icons with an empty mod cache. Shell
// and OS caches are uncontrolled: this is a pipeline profile, not Explorer latency.
static void ProfileExecutableIcons() {
    ViewState state;
    state.hwnd = IconTestWindow(); state.background->hwnd = state.hwnd; state.iconToken = 991;
    state.visible = state.inScope = state.detailsViewActive = true;
    state.columnsView = false; state.rowPx = 34.f;
    state.firstContentSubmittedAt = GetTickCount64();
    wchar_t system[MAX_PATH]{};
    Check(GetSystemDirectoryW(system, ARRAYSIZE(system)) != 0, "system icon profile directory");
    std::vector<std::wstring> paths;
    for (const wchar_t* name : {L"cmd.exe", L"notepad.exe", L"taskmgr.exe", L"control.exe", L"calc.exe", L"schtasks.exe",
        L"net.exe", L"ipconfig.exe", L"where.exe", L"attrib.exe", L"robocopy.exe", L"whoami.exe"}) {
        const auto path = JoinPath(system, name);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) paths.push_back(path);
    }
    LARGE_INTEGER start{}, end{}, frequency{};
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    for (const auto& path : paths) {
        Entry entry; entry.path = path; entry.name = PathFindFileNameW(path.c_str());
        state.entries.push_back(std::move(entry));
    }
    state.total = static_cast<int>(state.entries.size());
    RequestVisibleIcons(state);
    const bool complete = Eventually([&] {
        ReceiveIcons(state);
        return std::all_of(paths.begin(), paths.end(), [&](const auto& path) {
            const auto found = state.icons.find(path);
            return found != state.icons.end() && found->second.pixels.Ready();
        });
    }, 30000);
    if (!complete) for (const auto& path : paths) {
        const auto found = state.icons.find(path);
        if (found == state.icons.end() || !found->second.pixels.Ready())
            std::wcerr << L"ICON PROFILE FAILURE: " << path << L'\n';
    }
    Check(complete, "real executable icon resolution");
    QueryPerformanceCounter(&end);
    std::cout << "PROFILE: " << paths.size() << " real EXE Shell icons + WIC decoding=" <<
        1000. * (end.QuadPart-start.QuadPart) / frequency.QuadPart << " ms (empty mod cache; OS cache uncontrolled)\n";
    Stop(state.background); DestroyWindow(state.hwnd);
}

static void TestIconUploadBatches() {
    ViewState state;
    state.hwnd = IconTestWindow(); state.nativeHwnd = state.frameHwnd = state.hwnd;
    state.visible = state.inScope = state.detailsViewActive = true;
    state.columnsView = false; state.fontPx = 16.f; state.rowPx = 34.f; state.iconPx = 20.f;
    Check(EnsureTarget(state), "icon upload target");
    UpdateRefreshRate(state);
    for (int i = 0; i < 20; ++i) {
        Entry entry; entry.name = L"app" + std::to_wstring(i) + L".exe"; entry.path = L"icon-upload:" + entry.name;
        auto& record = state.icons.try_emplace(entry.path).first->second;
        record.pixels.width = record.pixels.height = 8; record.pixels.stride = 32;
        record.pixels.data.assign(8 * 32, 255);
        state.entries.push_back(std::move(entry));
    }
    state.total = static_cast<int>(state.entries.size());
    auto ready = [&] { return std::count_if(state.icons.begin(), state.icons.end(), [](const auto& pair) { return pair.second.bitmap != nullptr; }); };
    const auto began = GetTickCount64();
    size_t frames = 0, maximum = 0;
    do {
        state.iconUploadsPending = true;
        const auto previous = ready();
        UploadVisibleIcons(state);
        maximum = std::max(maximum, static_cast<size_t>(ready()-previous));
        ++frames;
    } while (ready() < state.total && frames <= 20);
    Check(ready() == state.total && maximum > 1 && frames < 20 && !state.iconUploadsPending,
          "ready icon uploads still impose one display frame per icon");
    std::cout << "PROFILE: 20 ready icon textures uploaded in " << frames << " frame batches, max batch=" << maximum <<
        ", CPU submission=" << GetTickCount64()-began << " ms (no display wait simulated)\n";
    // Read actual GPU pixels: the OS association is shown until the specific
    // EXE bitmap arrives, and then the per-file icon takes priority.
    auto& association = state.icons.try_emplace(L"type:.exe").first->second;
    association.pixels.width = association.pixels.height = 8; association.pixels.stride = 32;
    association.pixels.data.assign(8 * 32, 0);
    for (size_t i = 0; i < association.pixels.data.size(); i += 4) {
        association.pixels.data[i] = 200; association.pixels.data[i+3] = 255;
    }
    Check(UploadIconBitmap(state, association), "association icon fixture upload");
    auto& specific = state.icons.at(state.entries[0].path);
    specific.bitmap.Reset(); specific.bitmapAttempted = false;
    ComPtr<ID2D1Bitmap1> pixel;
    const auto properties = D2D1::BitmapProperties1(static_cast<D2D1_BITMAP_OPTIONS>(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW),
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1, 1), nullptr, 0, &properties, &pixel)), "icon readback bitmap");
    auto sample = [&] {
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
        DrawActualIcon(state, state.entries[0], 16, 16);
        Check(SUCCEEDED(state.target->EndDraw()), "icon fallback draw");
        const auto area = D2D1::RectU(18, 18, 19, 19);
        Check(SUCCEEDED(pixel->CopyFromBitmap(nullptr, state.backbuffer.Get(), &area)), "icon pixel copy");
        D2D1_MAPPED_RECT mapped{};
        Check(SUCCEEDED(pixel->Map(D2D1_MAP_OPTIONS_READ, &mapped)), "icon pixel map");
        const std::array<BYTE, 4> value{mapped.bits[0], mapped.bits[1], mapped.bits[2], mapped.bits[3]};
        pixel->Unmap(); return value;
    };
    Check(sample() == std::array<BYTE, 4>{200, 0, 0, 255}, "pending EXE did not draw its association icon");
    Check(UploadIconBitmap(state, specific) && sample() == std::array<BYTE, 4>{255, 255, 255, 255},
          "per-file EXE icon failed to supersede its association");
    // Use the actual shared icon path and private clipboard, with fixed phase
    // samples so GPU pixel assertions do not depend on machine/frame timing.
    ComPtr<ID2D1Image> original; state.target->GetTarget(&original);
    ComPtr<ID2D1Bitmap1> clipboardSurface, clipboardReadback;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(200,200),nullptr,0,&props,&clipboardSurface)),"clipboard icon surface");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(200,200),nullptr,0,&props,&clipboardReadback)),"clipboard icon readback");
    state.target->SetTarget(clipboardSurface.Get()); state.paintWidth=200; state.inScope=true;
    state.clipboardIconViewport=D2D1::RectF(0,0,200,200);
    const double itemDelay=static_cast<double>(SelectionPathHash(state.entries[0].path)%121);
    auto clipboardPixels=[&](double ms,int index=0) {
        state.clipboardIconSize96=state.iconPx; state.clipboardIconElapsedMs=ms; state.clipboardIconsVisible=false;
        const auto transform=D2D1::Matrix3x2F::Translation(1.f,2.f);
        state.target->SetTransform(transform);
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0.f,0.f));
        DrawActualIcon(state,state.entries[index],16,16,ItemContentOpacity(state,state.entries[index]));
        D2D1_MATRIX_3X2_F after{}; state.target->GetTransform(&after);
        Check(memcmp(&after,&transform,sizeof(after))==0,"clipboard icon transform leaked into text or other rows");
        Check(SUCCEEDED(state.target->EndDraw()) &&
            SUCCEEDED(clipboardReadback->CopyFromBitmap(nullptr,clipboardSurface.Get(),nullptr)),"clipboard icon draw/copy");
        D2D1_MAPPED_RECT mapped{}; Check(SUCCEEDED(clipboardReadback->Map(D2D1_MAP_OPTIONS_READ,&mapped)),"clipboard icon map");
        std::vector<BYTE> result(200*200*4);
        for(size_t y=0;y<200;++y) memcpy(result.data()+y*800,mapped.bits+y*mapped.pitch,800);
        clipboardReadback->Unmap(); return result;
    };
    Check(ClipboardIconWiggleAngle(16,90)>3.f*ClipboardIconWiggleAngle(128,90) &&
        ClipboardIconWiggleAngle(16,270)<0.f,"clipboard icon amplitude/phase did not adapt to size");
    Check(ClipboardIconWiggleAngle(16,90)>5.f && ClipboardIconWiggleAngle(256,90)>.6f &&
        ClipboardIconWiggleAngle(16,80,120)==0.f && fabsf(ClipboardIconWiggleAngle(16,121,120))<1.f,
        "Clipboard wiggle was not strengthened or delayed icons did not start from neutral");
    for(bool rtl : {false,true}) for(float side : {16.f,128.f}) {
        state.rtlLayout=rtl; state.iconPx=side;
        state.entries[0].attributes=FILE_ATTRIBUTE_HIDDEN;
        testClipboard.SeedNonFiles(); RefreshClipboardItems(state);
        const auto still=clipboardPixels(90);
        Check(clipboardPixels(270)==still && !state.clipboardIconsVisible,"hidden-only item wiggled");
        for(DWORD effect : {DWORD(DROPEFFECT_COPY),DWORD(DROPEFFECT_MOVE)}) {
            testClipboard.Seed(effect,state.entries[0].path); RefreshClipboardItems(state);
            const auto left=clipboardPixels(90+itemDelay), right=clipboardPixels(270+itemDelay);
            Check(left!=still && right!=still && left!=right && state.clipboardIconsVisible &&
                NeedsAnimationPaint(state,false),"Copy/Cut did not rotate icons or retain frame pacing");
            Check(clipboardPixels(0)==still,"Copy/Cut changed hidden alpha or the icon pivot at its neutral phase");
            auto bitmap=std::move(specific.bitmap); // Association fallback must wiggle too.
            const auto fallback=clipboardPixels(90+itemDelay);
            Check(state.clipboardIconsVisible && fallback!=clipboardPixels(270+itemDelay),"pending per-file icon lost its Copy/Cut wiggle");
            specific.bitmap=std::move(bitmap);
            RefreshClipboardItems(state,true);
            if (effect==DROPEFFECT_COPY) {
                Check(state.clipboardPaths.contains(state.entries[0].path) && clipboardPixels(90+itemDelay)==left,
                    "Escape canceled Copy feedback");
                testClipboard.SeedNonFiles(); RefreshClipboardItems(state);
            }
            Check(!state.clipboardIconsVisible && !state.clipboardIconSize96 && !state.clipboardIconElapsedMs && !state.clipboardIconsStartedQpc.QuadPart &&
                clipboardPixels(90)==still && !NeedsAnimationPaint(state,false),
                "clearing Copy/Cut left moving pixels or idle animation work");
        }
    }
    state.rtlLayout=false; state.iconPx=16;
    state.entries[0].attributes=state.entries[1].attributes=0;
    testClipboard.Seed(DROPEFFECT_COPY,state.entries[0].path+L'\0'+state.entries[1].path);
    RefreshClipboardItems(state);
    const auto firstPhase=clipboardPixels(220,0), secondPhase=clipboardPixels(220,1);
    Check(state.clipboardPaths.size()==2 && firstPhase!=secondPhase,
        "different copied items still wiggled in sync");
    std::swap(state.entries[0],state.entries[1]);
    Check(clipboardPixels(220,1)==firstPhase && clipboardPixels(220,0)==secondPhase,
        "reordering copied items rerolled their animation delays");
    std::swap(state.entries[0],state.entries[1]);
    testClipboard.Seed(DROPEFFECT_MOVE,L"unrelated:cut-file"); RefreshClipboardItems(state);
    clipboardPixels(90);
    Check(!state.clipboardIconsVisible && !NeedsAnimationPaint(state,false),
        "Cut in another directory kept this tab animating");
    testClipboard.Seed(DROPEFFECT_COPY,L"unrelated:cut-file"); RefreshClipboardItems(state);
    clipboardPixels(90);
    Check(!state.clipboardIconsVisible && !NeedsAnimationPaint(state,false),
        "Copy in another directory kept this tab animating");
    state.entries[0].attributes=0; state.rtlLayout=false; state.iconPx=20; state.inScope=false;
    state.target->SetTransform(D2D1::Matrix3x2F::Identity()); state.target->SetTarget(original.Get());
    state.inScope=state.loaded=true; state.firstContentSubmittedAt=GetTickCount64();
    state.featureInitializationStep=5;
    auto paintClipboard=[&] { auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap); };
    for(int i=0;i<100;++i) { paintClipboard(); if(!state.textRasterPending) break; }
    testClipboard.Seed(DROPEFFECT_COPY,state.entries[0].path); RefreshClipboardItems(state);
    state.clipboardIconsStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*(itemDelay+90.)/1000.);
    paintClipboard();
    Check(state.clipboardIconsVisible && fabsf(ClipboardIconWiggleAngle(state.clipboardIconSize96,state.clipboardIconElapsedMs,
        SelectionPathHash(state.entries[0].path)))>3.f && NeedsAnimationFrames(state),
        "full Paint failed to sample Copy motion or retain the animation");
    Check(ItemContentOpacity(state,state.entries[0])==1 && !state.itemOpacityAnimating,
        "full Paint dimmed copied content");
    AcquireSRWLockShared(&g_frameLock);
    const bool clipboardRegistered=std::any_of(g_activeFrames.begin(),g_activeFrames.end(),
        [&](const auto& f) { return f.hwnd==state.hwnd; });
    ReleaseSRWLockShared(&g_frameLock);
    Check(clipboardRegistered,"first Copy paint did not start the compositor pacer");
    ShowWindow(state.hwnd,SW_HIDE);
    Check(!NeedsAnimationPaint(state,false),"hidden tab retained Copy repaint work");
    ShowWindow(state.hwnd,SW_SHOWNOACTIVATE);
    testClipboard.SeedNonFiles(); RefreshClipboardItems(state); paintClipboard();
    Check(!state.clipboardIconsVisible && !state.clipboardIconSize96 && !state.clipboardIconElapsedMs,"full Paint retained externally cleared Copy rotation");
    state.inScope=state.loaded=false; state.firstContentSubmittedAt=0; state.featureInitializationStep=0;
    std::cout<<"PASS: clipboard Copy/Cut GPU icon wiggle in LTR/RTL, size-dependent rotation, stable per-item delays/reordering, Cut-only dimming, hidden alpha, fallback icons, transform isolation, full-paint Copy pacing, external clear and no hidden/unrelated-directory pacing\n";
    for (auto& pair : state.icons) { pair.second.bitmap.Reset(); pair.second.bitmapAttempted = false; }
    state.rowTransitionActive = true; state.iconUploadsPending = true;
    AdvanceAnimationFrame(state, true);
    Check(ready() > 0, "structural animation imposed a full-duration icon blackout");
    StopRowTransition(state);
    UnregisterAnimationWindow(state.hwnd);
    DropTarget(state); DestroyWindow(state.hwnd);
    std::cout << "PASS: timed icon upload batches, GPU association/per-file pixels, and progress during structural motion\n";
}
