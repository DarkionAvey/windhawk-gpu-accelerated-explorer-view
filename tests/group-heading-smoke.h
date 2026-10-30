static void TestStickyGroupHeaders(HWND frame) {
    for(bool rtl : {false,true}) for(UINT mode : {FVM_DETAILS,FVM_LIST,FVM_ICON}) {
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"sticky group headings",WS_CHILD,0,0,640,360,
            frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"sticky heading window");
        state.nativeHwnd=state.frameHwnd=state.hwnd; state.background->hwnd=state.hwnd;
        FocusFixture::Session focus(state.frameHwnd,state.nativeHwnd);
        state.visible=state.inScope=state.loaded=state.detailsViewActive=true;
        state.firstContentSubmittedAt=GetTickCount64(); state.featureInitializationStep=5;
        state.rtlLayout=rtl; state.nativeTextColor=state.nativeHeaderTextColor=RGB(255,255,255);
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=320;
        ExplorerColumn type; type.key=PKEY_ItemTypeText; type.title=L"Type"; type.width=250;
        state.columns={name,type}; state.grouping.enabled=true;
        state.total=180; state.entries.resize(state.total); state.selectedFlags.assign(state.total,1);
        for(int i=0;i<state.total;++i) {
            auto& e=state.entries[i]; e.name=L"Visible item "+std::to_wstring(i);
            e.path=L"sticky:"+e.name; e.folder=true; e.groupKey=std::to_wstring(i/60);
            e.groupTitle=L"Section "+e.groupKey; e.type=L"File folder";
            e.detailsReady=true; e.propertyEpoch=state.propertyEpoch;
        }
        ApplyViewLayout(state,{mode,64},false); UpdateMetrics(state);
        RebuildGroupSections(state); UpdateColumns(state);
        Check(EnsureTarget(state),"sticky heading GPU target");
        const float heading=GroupHeadingHeight(state);
        const float lastBottom=DocumentRowTop(state,59)+DisplayRowHeight(state,59);
        Check(fabsf(state.sections[0].documentEnd-lastBottom-heading)<.01f &&
            state.sections[1].documentTop==state.sections[0].documentEnd &&
            RowAtDocument(state,lastBottom+heading*.5f,1.f)==-1 && state.total==180,
            "group separator is missing or became a selectable item");
        const int x=LogicalX(state,int(ListStartX(state)+40));
        const auto paintBand=[&](ID2D1Bitmap1* plane=nullptr,float offset=0.f,float height=-1.f) {
            if(!plane) {
                auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap);
            }
            Check(state.backbuffer!=nullptr,"sticky heading paint failed");
            ID2D1Bitmap1* source=plane ? plane : state.backbuffer.Get();
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                source->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(source->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"sticky heading readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"sticky heading map");
            const int a=int(rtl ? 640-ListEndX(state) : ListStartX(state))+1;
            const int b=int(rtl ? 640-ListStartX(state) : ListEndX(state))-1;
            std::vector<BYTE> result;
            const float top=(plane ? state.modeComposition.header : state.headerPx)+offset;
            for(int y=int(top);y<int(top+(height<0.f ? GroupHeadingHeight(state) : height));++y)
                result.insert(result.end(),map.bits+y*map.pitch+a*4,map.bits+y*map.pitch+b*4);
            cpu->Unmap(); return result;
        };
        state.scrollNow=state.scrollTarget=0.f;
        const auto reference=paintBand();
        state.scrollNow=state.scrollTarget=heading+state.rowPx*5.25f;
        Check(GroupAtDocument(state,state.scrollNow)==0,"sticky fixture did not stay inside its first section");
        const auto pinned=LayoutGroupHeader(state,0,1.f);
        const int y=int(state.headerPx+heading*.5f);
        Check(pinned.top==state.headerPx && GroupHeaderAtPoint(state,x,y)==0 && RowFromPoint(state,y,x)==-1,
            "sticky header did not own its displayed hit rectangle");
        const auto actual=paintBand();
        Check(actual.size()==reference.size(),"sticky heading band changed geometry");
        size_t ink=0;
        for(size_t p=0;p<actual.size();++p) {
            ink+=reference[p]!=0;
            Check(abs(int(actual[p])-int(reference[p]))<=8,
                "scrolling labels/icons/selection/zebra bled through the sticky header");
        }
        Check(ink>20,"sticky heading reference was blank");
        // A pinned heading belongs to the file content. The marquee must
        // cross its text as well as the item body below its reserved band.
        const auto activeTabHost=state.activeTabHost;
        state.activeTabHost=state.frameHwnd;
        Check(OverlayHasInputFocus(state),"sticky marquee fixture has no active list input");
        const auto marqueeAlpha=g_marqueeAlpha.exchange(80);
        const auto marqueeColor=g_selectionRgb.exchange(RGB(0,96,220));
        state.marqueeActive=true;
        state.marqueeStart={0,LONG(state.headerPx)};
        state.marqueeStartDocumentY=state.scrollNow;
        state.marqueeCurrent={640,360};
        const auto covered=paintBand();
        size_t coveredInk=0;
        for(size_t p=0;p<actual.size();p+=4)
            if(actual[p+3]) {
                ++coveredInk;
                Check(memcmp(actual.data()+p,covered.data()+p,4)!=0,
                    "marquee was clipped below pinned group text");
            }
        Check(coveredInk>20,"sticky marquee fixture had no heading text");
        state.marqueeActive=false;
        state.activeTabHost=activeTabHost;
        g_marqueeAlpha=marqueeAlpha; g_selectionRgb=marqueeColor;
        UpdatePointerHover(state,{x,y},false);
        Check(state.hotSection==0 && state.hovered==-1,"sticky hover targeted an underlying file");
        const auto captured=CaptureVisibleGroupHeaders(state);
        Check(std::any_of(captured.begin(),captured.end(),[](const auto& h) { return h.key==L"0"; }),
            "offscreen natural header was lost from the sticky transition capture");
        state.scrollNow=state.scrollTarget=state.sections[1].documentTop-heading*.5f;
        const auto outgoing=LayoutGroupHeader(state,0,1.f), incoming=LayoutGroupHeader(state,1,1.f);
        Check(fabsf(outgoing.top-(state.headerPx-heading*.5f))<.01f &&
            fabsf(outgoing.top+outgoing.height-incoming.top)<.01f &&
            GroupHeaderAtPoint(state,x,int(state.headerPx+heading*.25f))==0 &&
            GroupHeaderAtPoint(state,x,int(state.headerPx+heading*.75f))==1,
            "next section failed to push the pinned header away");
        state.scrollNow=state.scrollTarget=state.sections[1].documentTop;
        Check(GroupHeaderAtPoint(state,x,y)==1,"sticky header handoff retained the previous group");
        const float target=RevealScrollTarget(state,20,DocumentRowTop(state,20)+state.rowPx);
        Check(DocumentRowTop(state,20)-target>=heading-.01f,
            "keyboard/zoom reveal hid the item behind its sticky heading");
        if(!state.gridView) {
            // A half-collapsed section places its spacer AND the following
            // heading over an odd global stripe, including fractional edges.
            state.sections[0].fraction=.5f; LayoutGroupSections(state);
            state.scrollNow=state.scrollTarget=state.sections[1].documentTop-2.f*state.rowPx;
            state.hotSection=-1; state.hovered=-1;
            const float spacer=state.sections[0].documentEnd-heading*.5f-state.scrollNow;
            const float next=state.sections[1].documentTop-state.scrollNow;
            const bool zebra=g_zebra.exchange(false), empty=g_emptyRows.load();
            const int opacity=g_zebraOpacity.exchange(80);
            for(int n=0;n<12;++n) { paintBand(); if(!state.textRasterPending) break; }
            const auto plainSpacer=paintBand(nullptr,spacer,heading*.5f);
            const auto plainHeader=paintBand(nullptr,next,heading);
            g_zebra=true;
            for(bool showEmpty : {false,true}) {
                g_emptyRows=showEmpty;
                const auto gap=paintBand(nullptr,spacer,heading*.5f);
                const auto caption=paintBand(nullptr,next,heading);
                Check(gap==plainSpacer,"zebra stripes painted the section spacer");
                Check(caption.size()==plainHeader.size(),"group caption readback changed dimensions");
                for(size_t p=0;p<caption.size();++p) {
                    if(abs(int(caption[p])-int(plainHeader[p]))>8)
                        std::cout<<"GROUP ZEBRA: rtl="<<rtl<<" mode="<<mode<<" empty="<<showEmpty
                            <<" byte="<<p<<" actual="<<unsigned(caption[p])<<" expected="<<unsigned(plainHeader[p])<<"\n";
                    Check(abs(int(caption[p])-int(plainHeader[p]))<=8,
                        "zebra stripes painted a group heading");
                }
            }
            // An odd-sized predecessor must not invert the next section.
            // Sample background pixels, with no selection covering stripes,
            // after full/fractional/collapsed predecessors and while scrolled.
            const auto selection=state.selectedFlags;
            std::fill(state.selectedFlags.begin(),state.selectedFlags.end(),0);
            state.sections[0].end=59; state.sections[1].first=59; state.rowGroup[59]=1;
            for(float fraction : {1.f,.5f,.37f,0.f}) for(bool showEmpty : {false,true}) {
                state.sections[0].fraction=fraction; LayoutGroupSections(state);
                g_emptyRows=showEmpty;
                for(size_t section : {1u,2u}) for(float offset : {0.f,.25f*state.rowPx}) {
                    state.scrollNow=state.scrollTarget=state.sections[section].documentTop+offset;
                    for(int slot=0;slot<3;++slot) {
                        const float y=heading+(slot+.5f)*state.rowPx-offset;
                        g_zebra=false;
                        const auto plain=paintBand(nullptr,y,1.f);
                        g_zebra=true;
                        const auto striped=paintBand(nullptr,y,1.f);
                        int difference=0;
                        Check(plain.size()==striped.size(),"section stripe readback changed dimensions");
                        for(size_t p=0;p<plain.size();++p)
                            difference=std::max(difference,abs(int(plain[p])-int(striped[p])));
                        Check(slot%2==0 ? difference>20 : difference<=2,
                            "section stripes did not start at the first item or restart while scrolling");
                    }
                }
            }
            state.sections[0].end=60; state.sections[1].first=60; state.rowGroup[59]=0;
            state.selectedFlags=selection;
            g_zebra=zebra; g_emptyRows=empty; g_zebraOpacity=opacity;
        }
        state.sections[0].collapsed=true; state.sections[0].fraction=.5f;
        LayoutGroupSections(state);
        Check(fabsf(state.sections[0].documentEnd-(heading+
            (state.rowPx*ItemLineCount(state,60)+heading)*.5f))<.01f,
            "section separator did not animate with collapse");
        state.sections[0].fraction=0.f; LayoutGroupSections(state);
        Check(state.sections[0].documentEnd==heading && !DisplayedRow(state,0),
            "collapsed section left an empty selectable spacer");
        state.sections[0].collapsed=false; state.sections[0].fraction=1.f;
        ApplyViewLayout(state,{UINT(mode==FVM_DETAILS ? FVM_ICON : FVM_DETAILS),64},true);
        LayoutGroupSections(state);
        state.scrollNow=state.scrollTarget=GroupHeadingHeight(state)+state.rowPx*5.25f;
        Paint(state);
        Check(state.modeComposition.published,"sticky heading mode scene was not published");
        for(auto* plane : {&state.modeComposition.names,&state.modeComposition.metadata}) {
            if(!plane->bitmap) continue;
            const auto band=paintBand(plane->bitmap.Get());
            const auto first=std::find_if(band.begin(),band.end(),[](BYTE pixel) { return pixel!=0; });
            if(first!=band.end())
                std::cout<<"STICKY PLANE: rtl="<<rtl<<" mode="<<mode<<" metadata="<<(plane==&state.modeComposition.metadata)
                    <<" header="<<state.headerPx<<" canonical="<<state.modeComposition.header
                    <<" heading="<<GroupHeadingHeight(state)<<" pinned="<<GroupAtDocument(state,state.scrollNow)
                    <<" first-byte="<<(first-band.begin())<<" value="<<unsigned(*first)<<"\n";
            Check(std::all_of(band.begin(),band.end(),[](BYTE pixel) { return pixel==0; }),
                "mode compositor foreground covered the sticky heading");
        }
        const auto band=paintBand([&]<class S>(S& s) {
            if constexpr(requires {s.modeComposition.groups;}) return s.modeComposition.groups.bitmap.Get();
            else return s.modeComposition.background.bitmap.Get();
        }(state));
        Check(std::any_of(band.begin(),band.end(),[](BYTE pixel) { return pixel!=0; }),
            "mode compositor lost the sticky heading");
        UnregisterAnimationWindow(state.hwnd); Stop(state.background);
        DropTarget(state); DestroyWindow(state.hwnd);
    }
    std::cout<<"PASS: sticky group headings push/handoff and own hover/click pixels in LTR/RTL Details/List/grid; first-item stripes per section, blank headers/spacers, body masking, real empty separators, collapse, capture and reveal\n";
}
