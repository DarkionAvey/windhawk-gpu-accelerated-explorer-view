// Physical viewport models plus real WIC/GPU transfers and bounded fake image
// providers. No desktop focus, clipboard, user folder or file command changes.
static void TestAdaptiveImageDemand() {
    const auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"adaptive image fonts");
    {
        PresentationFixture::Tab tab(L"adaptive image demand",4,false,true);
        auto& state=tab.state; state.presentationOnly=true;
        state.presentationClient={0,0,5120,2880}; state.imageMemoryAllowance=512ULL*1024*1024;
        state.itemsPerRow=2;
        for(int dpi:{96,144,192,288}) {
            state.presentationDpi=dpi; state.iconPx=float(96*dpi/96); state.rowPx=state.iconPx+80;
            const auto request=IconRequestSize(state);
            Check(request>=state.iconPx && request<=state.iconPx*1.5f,"physical DPI demand used a logical-size or excessive image request");
        }
        state.iconPx=1100; state.rowPx=1200;
        const auto large=IconRequestSize(state);
        Check(large>512 && large>=1100 && large<=1536,"5K visible image demand retained the 512px ceiling");
        Entry item; item.name=L"photo.png"; item.path=L"wh-adaptive-image:photo.png";
        std::wstring path; bool attrs=false;
        const auto largeKey=IconKey(state,item,path,attrs);
        Check(!attrs && largeKey.starts_with(L"@"+std::to_wstring(large)+L"|"),"adaptive resolution did not enter its per-item cache key");
        const auto association=IconKey(state,item,path,attrs,true);
        Check(attrs && ImageKeySize(association)<=256,"shared Shell jumbo icon was needlessly upscaled/extracted");
        const auto preview=IconKey(state,item,path,attrs,false,256);
        Check(ImageKeySize(preview)==256 && preview!=largeKey,"offscreen preview used full visible-image demand");
        state.imageMemoryAllowance=4ULL*1024*1024;
        Check(IconRequestSize(state)<large && IconRequestSize(state)>=32,"memory pressure did not lower optional image resolution");
        state.imageMemoryAllowance=2ULL*1024*1024*1024; state.presentationClient={0,0,16384,8192};
        state.total=1; state.itemsPerRow=1; state.iconPx=6144; state.rowPx=6300;
        Check(IconRequestSize(state)==6144,"adaptive demand introduced another fixed 512/1024/2048 cap");
        state.imageTextureLimit=2048;
        Check(IconRequestSize(state)<=2048,"image demand exceeded the actual device texture limit");
        state.imageTextureLimit=16384; state.presentationClient={0,0,800,600}; state.iconPx=1.e12f;
        Check(IconRequestSize(state)<=800,"mostly offscreen giant cell caused an oversized extraction");
        state.iconPx=48; state.rowPx=100;
        Check(IconRequestSize(state)==48,"small icons requested high resolution because the screen was large");
        state.zoomAnimating=true; state.zoomToIconPx=700; state.zoomToRowPx=800;
        Check(IconRequestSize(state)>512,"zoom did not prefetch its destination resolution");
        state.zoomAnimating=false;
        auto& old=state.icons.try_emplace(L"@768|adaptive-cache").first->second;
        old.pixels.width=old.pixels.height=2; old.pixels.stride=8; old.pixels.data.assign(16,255);
        auto& next=state.icons.try_emplace(L"@1536|adaptive-cache").first->second;
        Check(FindIconRecord(state,L"@1536|adaptive-cache",true)==&old,"new high-resolution tiers lost the displayed fallback");
        next.pixels=old.pixels;
        Check(FindIconRecord(state,L"@1536|adaptive-cache",true)==&next,"ready adaptive tier did not replace its fallback");
        auto& detailed=state.icons.try_emplace(L"@1024|actual-detail").first->second;
        detailed.pixels.width=detailed.pixels.height=512; detailed.pixels.stride=2048; detailed.pixels.data.assign(1024*1024,255);
        auto& undersized=state.icons.try_emplace(L"@3072|actual-detail").first->second;
        undersized.pixels.width=undersized.pixels.height=128; undersized.pixels.stride=512; undersized.pixels.data.assign(65536,255);
        Check(FindIconRecord(state,L"@1536|actual-detail",true)==&detailed,
            "a larger request label displaced a more detailed cached source");
    }
    {
        ComPtr<IWICImagingFactory> imaging;
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging))),"adaptive WIC factory");
        ComPtr<IWICBitmap> source;
        Check(SUCCEEDED(imaging->CreateBitmap(2048,1024,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&source)),"adaptive WIC source");
        const auto high=DecodeIconPixels(imaging.Get(),source.Get(),nullptr,1536,16ULL*1024*1024);
        Check(high.Ready() && high.width==1536 && high.height==768,"WIC still downsampled high-resolution provider output to 512");
        const auto preview=DecodeIconPixels(imaging.Get(),source.Get(),nullptr,256);
        Check(preview.width==256 && preview.height==128,"preview decode ignored its demand");
        const auto pressure=DecodeIconPixels(imaging.Get(),source.Get(),nullptr,1536,128*1024);
        Check(pressure.Ready() && pressure.data.size()<=128*1024,"WIC exceeded its explicit pixel-byte budget");
        ThumbnailStyle style; style.filmstrip=true;
        auto capped=DecorateThumbnail(IconPixels(high),style,1536);
        Check(capped.Ready() && capped.width<=1536 && capped.thumbnailWidth==1536,
            "thumbnail adornment exceeded hardware bounds or discarded usable source imagery");
    }
    g_writeFactory=factory;
    std::cout<<"PASS: physical DPI/5K/large viewport demand above 512 through 6144, small/preview tiers, RAM/device limits, zoom prefetch, dynamic cache fallback and byte-bounded WIC\n";
}

static void TestAdaptiveImagePipeline() {
    AdaptiveImageFixture::Session providers;
    const auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"adaptive pipeline fonts");
    {
        PresentationFixture::Tab tab(L"adaptive provider and GPU",1,false,true,false,2400,1400);
        auto& state=tab.state; SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame,state.hwnd); ModifierFixture::Session modifiers;
        state.cachedNativeMode=FVM_ICON; state.iconPx=900; state.rowPx=1000; state.itemsPerRow=1;
        state.background->imageMemoryProbeAfter=ULLONG_MAX;
        state.entries[0].name=L"adaptive.png"; state.entries[0].path=L"wh-adaptive-image:fast.png";
        state.entries[0].folder=false;
        std::wstring path; bool attrs=false; const auto key=IconKey(state,state.entries[0],path,attrs);
        const auto request=ImageKeySize(key); const std::unordered_map<std::wstring,unsigned> wanted{{key,0}};
        Check(request>512 && RequestEntryIcon(state,state.entries[0],false,wanted),"large provider request was not queued");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,key); return image && image->resolved;}),"large provider result");
        auto* image=FindIconRecord(state,key);
        Check(AdaptiveImageFixture::maximumRequest==request && image->pixels.thumbnailWidth==request && request>512,
            "Shell request and decoded high-resolution pixels diverged");
        Check(EnsureTarget(state) && UploadIconBitmap(state,*image) && image->bitmap->GetPixelSize().width>512,
            "large CPU image did not reach a real GPU bitmap");
        // The immutable presentation packet must carry both dynamic tiers and
        // the existing fallback without enumerating every historical image.
        auto& preview=state.icons.try_emplace(L"@256|"+state.entries[0].path).first->second;
        preview.pixels.width=preview.pixels.height=2; preview.pixels.stride=8; preview.pixels.data.assign(16,255);
        const auto model=CapturePresentationModel(state);
        Check(std::any_of(model->images.begin(),model->images.end(),[&](const auto& item){return item.key==key;}) &&
            std::any_of(model->images.begin(),model->images.end(),[&](const auto& item){return item.key==L"@256|"+state.entries[0].path;}),
            "presentation packet dropped a dynamic image tier or its fallback");
        const auto calls=AdaptiveImageFixture::requests.load();
        state.iconPx=700;
        const auto smaller=IconKey(state,state.entries[0],path,attrs);
        Check(RequestEntryIcon(state,state.entries[0],false,{{smaller,0}}) && AdaptiveImageFixture::requests==calls,
            "nearby adequate high-resolution source was extracted again");
        state.imageTextureLimit=512;
        const auto deviceSized=IconKey(state,state.entries[0],path,attrs);
        Check(RequestEntryIcon(state,state.entries[0],false,{{deviceSized,0}}) && state.pendingImages.contains(deviceSized),
            "cached image larger than a replacement device prevented compatible image demand");
        DropTarget(state);
    }
    {
        PresentationFixture::Tab tab(L"adaptive viewport cancellation",24,false,true,false,1600,1000);
        struct ReleaseGate { ~ReleaseGate() {SetEvent(AdaptiveImageFixture::gate);} } release;
        auto& state=tab.state; SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame,state.hwnd); ModifierFixture::Session modifiers;
        state.cachedNativeMode=FVM_ICON; state.iconPx=800; state.rowPx=900; state.itemsPerRow=2; state.itemWidth=780;
        state.background->imageMemoryProbeAfter=ULLONG_MAX;
        for(int i=0;i<24;++i) {state.entries[size_t(i)].name=L"adaptive.png";
            state.entries[size_t(i)].folder=false;
            state.entries[size_t(i)].path=L"wh-adaptive-image:"+std::wstring(i==0 ? L"slow" : L"fast")+std::to_wstring(i)+L".png";}
        RequestVisibleIcons(state);
        Check(Eventually([&] {return AdaptiveImageFixture::blocked>0;}),"slow adaptive provider did not enter");
        std::wstring path; bool attrs=false; const auto departed=IconKey(state,state.entries[0],path,attrs);
        const auto pending=state.pendingImages.find(departed);
        Check(pending!=state.pendingImages.end(),"in-flight image cancellation token was not tracked");
        const auto cancelled=pending->second.cancelled;
        state.scrollNow=state.scrollTarget=6*state.rowPx; RequestVisibleIcons(state);
        Check(cancelled->load() && !state.icons.at(departed).pending,"scroll did not cancel an obsolete in-flight high-resolution request");
        SetEvent(AdaptiveImageFixture::gate);
        Check(Eventually([&] {ReceiveIcons(state,false); return AdaptiveImageFixture::blocked==0 && state.background->iconProviderBusy==0;}),"cancelled provider did not drain");
        const auto* old=FindIconRecord(state,departed);
        Check(!old || !old->pixels.Ready(),"obsolete high-resolution result was accepted after viewport cancellation");
        Check(state.iconCacheBytes<=ImageMemoryBudget(state),"viewport image reservations inflated the memory budget");
    }
    {
        PresentationFixture::Tab tab(L"memory changed after demand",1,false,true,false,2400,1400);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        state.cachedNativeMode=FVM_ICON; state.iconPx=900; state.rowPx=1000; state.itemsPerRow=1;
        state.entries[0].name=L"pressure.png"; state.entries[0].path=L"wh-adaptive-image:pressure.png";
        state.entries[0].folder=false; state.background->imageMemoryProbeAfter=ULLONG_MAX;
        std::wstring path; bool attrs=false; const auto stale=IconKey(state,state.entries[0],path,attrs);
        const auto calls=AdaptiveImageFixture::requests.load();
        state.background->imageMemoryAllowance=4ULL*1024*1024;
        Check(RequestEntryIcon(state,state.entries[0],false,{{stale,0}}),"stale memory-budget request");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,stale); return image && !image->pending;}),"memory-pressure rejection");
        Check(AdaptiveImageFixture::requests==calls && !state.icons.at(stale).resolved && !state.icons.at(stale).failures,
            "stale large request allocated a provider bitmap or became a permanent artwork failure");
        RequestVisibleIcons(state);
        const auto reduced=IconKey(state,state.entries[0],path,attrs);
        Check(reduced!=stale,"fresh memory allowance did not reduce image demand");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,reduced); return image && image->pixels.Ready();}),"reduced memory-budget image");
        state.background->imageMemoryAllowance=kInitialImageMemoryAllowance;
        RequestVisibleIcons(state);
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,stale); return image && image->pixels.Ready();}),"quality did not recover after memory pressure ended");
    }
    {
        PresentationFixture::Tab tab(L"provider artwork ceiling",1,false,true,false,2400,1400);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        state.imageMemoryAllowance=state.background->imageMemoryAllowance=256ULL*1024*1024;
        state.background->imageMemoryProbeAfter=ULLONG_MAX;
        state.cachedNativeMode=FVM_ICON; state.iconPx=700; state.rowPx=900; state.itemsPerRow=1;
        state.entries[0].name=L"limited.png"; state.entries[0].path=L"wh-adaptive-image:limited.png";
        state.entries[0].folder=false;
        AdaptiveImageFixture::sourceLimit=128;
        std::wstring path; bool attrs=false; const auto key=IconKey(state,state.entries[0],path,attrs);
        Check(RequestEntryIcon(state,state.entries[0],false,{{key,0}}),"limited artwork request");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,key); return image && image->resolved;}),"limited artwork completion");
        const auto calls=AdaptiveImageFixture::requests.load(); state.iconPx=1300;
        const auto larger=IconKey(state,state.entries[0],path,attrs);
        Check(RequestEntryIcon(state,state.entries[0],false,{{larger,0}}),"larger source-quality probe");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,larger); return image && image->resolved;}),"source-quality probe completion");
        Check(AdaptiveImageFixture::requests>calls && state.icons.at(larger).sourceLimited,
            "one undersized response prematurely capped the provider or repeated output was not recognized");
        const auto probed=AdaptiveImageFixture::requests.load();
        state.imageMemoryAllowance=256ULL*1024*1024; state.iconPx=1900;
        SetWindowPos(state.hwnd,nullptr,0,0,5120,2880,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        const auto biggest=IconKey(state,state.entries[0],path,attrs);
        Check(biggest!=larger && RequestEntryIcon(state,state.entries[0],false,{{biggest,0}}) &&
            AdaptiveImageFixture::requests==probed && !state.icons.contains(biggest),
            "repeatedly confirmed lower-resolution artwork was extracted again");
        InvalidateShellIconKey(state,state.entries[0].path);
        Check(state.icons.at(key).needsRefresh && state.icons.at(larger).needsRefresh &&
            !state.icons.at(larger).sourceLimited && state.icons.at(larger).pixels.Ready(),
            "Shell item updates discarded fallback artwork or retained its learned source ceiling");
        state.entries[0].path=L"wh-adaptive-image:warming.png"; state.iconPx=700;
        const auto cold=IconKey(state,state.entries[0],path,attrs);
        Check(RequestEntryIcon(state,state.entries[0],false,{{cold,0}}),"cold undersized provider request");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,cold); return image && image->resolved;}),"cold undersized result");
        AdaptiveImageFixture::sourceLimit=1024; state.iconPx=1300;
        const auto warm=IconKey(state,state.entries[0],path,attrs);
        Check(RequestEntryIcon(state,state.entries[0],false,{{warm,0}}),"warming provider upgrade");
        Check(Eventually([&] {ReceiveIcons(state,false); const auto* image=FindIconRecord(state,warm); return image && image->resolved;}),"warming provider completion");
        Check(state.icons.at(warm).pixels.thumbnailWidth==1024 && !state.icons.at(warm).sourceLimited,
            "a first undersized response prevented a provider from supplying better artwork");
    }
    g_writeFactory=factory;
    std::cout<<"PASS: actual high-resolution Shell-factory request/WIC/GPU upload, sparse presentation handoff, cache reuse, viewport cancellation, bounded reservations and provider-limited artwork\n";
}
