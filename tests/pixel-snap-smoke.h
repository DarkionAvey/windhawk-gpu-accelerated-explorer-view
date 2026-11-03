// Pixel-grid placement on real offscreen GPU frames, the drag preview and the
// status-bar switch glyph. Fractional DPIs (125-175%) make every unsnapped
// coordinate visible. The expectation is independent of the renderer: a
// straight edge on whole pixels steps from one value to the next between two
// adjacent pixels; an edge at a fractional coordinate leaves a run of pixels
// that are each a strict blend of their neighbours across the edge.
namespace PixelSnapFixture {
struct SoftEdge {int x=-1,y=-1,length=0; bool vertical=false;};

// Longest straight run of blended edge pixels. Each pixel lies between its
// two neighbours across the edge in every channel, at least a fifth of the
// way from both on the channel that changes most; the three values stay
// constant along the run, as along any straight edge.
// `ignore` excludes images; `stems` excludes only vertical edges inside text
// boxes, where DirectWrite places glyph stems at subpixel advances by design.
inline SoftEdge LongestSoftEdge(const BYTE* pixels,int width,int height,size_t stride,
                                const std::vector<RECT>& ignore={},const std::vector<RECT>& stems={}) {
    const auto at=[&](int x,int y) {return pixels+size_t(y)*stride+size_t(x)*4;};
    const auto blend=[](const BYTE* a,const BYTE* m,const BYTE* b) {
        int dominant=0,difference=0;
        for(int c=0;c<4;++c) {
            const int d=abs(int(a[c])-int(b[c]));
            if(d>difference) {difference=d; dominant=c;}
            if(m[c]+2<std::min(a[c],b[c]) || m[c]>std::max(a[c],b[c])+2) return false;
        }
        const int low=std::min(a[dominant],b[dominant]),high=std::max(a[dominant],b[dominant]);
        if(difference<64 || m[dominant]<low+difference/5 || m[dominant]>high-difference/5) return false;
        // Partial coverage mixes the two sides by one ratio in every
        // premultiplied channel. A translucent stroke over two different
        // backdrops (e.g. a crisp border row over transparency and over a
        // fill) does not.
        const float t=float(int(m[dominant])-int(a[dominant]))/float(int(b[dominant])-int(a[dominant]));
        for(int c=0;c<4;++c)
            if(fabsf(float(a[c])+(float(b[c])-float(a[c]))*t-float(m[c]))>6.f) return false;
        return true;
    };
    const auto alike=[](const BYTE* a,const BYTE* b) {
        for(int c=0;c<4;++c) if(abs(int(a[c])-int(b[c]))>3) return false;
        return true;
    };
    const auto inside=[](const std::vector<RECT>& rects,int x,int y) {
        const POINT point{x,y};
        return std::any_of(rects.begin(),rects.end(),[&](const RECT& r) {return PtInRect(&r,point)!=FALSE;});
    };
    SoftEdge longest;
    for(const bool vertical:{false,true}) {
        const int lines=vertical ? width : height,along=vertical ? height : width;
        for(int line=1;line+1<lines;++line) {
            int run=0; const BYTE *ra=nullptr,*rm=nullptr,*rb=nullptr;
            for(int i=0;i<along;++i) {
                const int x=vertical ? line : i,y=vertical ? i : line;
                const BYTE* a=vertical ? at(x-1,y) : at(x,y-1);
                const BYTE* m=at(x,y);
                const BYTE* b=vertical ? at(x+1,y) : at(x,y+1);
                if(inside(ignore,x,y) || (vertical && inside(stems,x,y)) || !blend(a,m,b)) {run=0; continue;}
                if(run && !(alike(a,ra) && alike(m,rm) && alike(b,rb))) run=0;
                if(!run) {ra=a; rm=m; rb=b;}
                if(++run>longest.length) longest={vertical ? x : i-run+1,vertical ? i-run+1 : y,run,vertical};
            }
        }
    }
    return longest;
}
inline SoftEdge LongestSoftEdge(const std::vector<BYTE>& bgra,int width,int height,
                                const std::vector<RECT>& ignore={},const std::vector<RECT>& stems={}) {
    return LongestSoftEdge(bgra.data(),width,height,size_t(width)*4,ignore,stems);
}
inline std::string Describe(const SoftEdge& edge) {
    return std::string(edge.vertical ? "vertical" : "horizontal")+" soft edge of "+std::to_string(edge.length)+
        " pixels at ("+std::to_string(edge.x)+","+std::to_string(edge.y)+")";
}
}

static void TestPixelSnapping() {
    using namespace PixelSnapFixture;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"pixel snapping fonts");
    // The detector itself: a fill at a fractional row/column is reported
    // and the same fill on whole pixels is not, by its own arithmetic.
    {
        constexpr int size=40;
        for(const float offset:{0.f,.25f,.5f}) {
            std::vector<BYTE> image(size_t(size)*size*4,0);
            for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
                const float coverage=std::clamp(float(x)+1.f-(10.f+offset),0.f,1.f)*
                    std::clamp(float(y)+1.f-(10.f+offset),0.f,1.f);
                std::fill_n(image.data()+(size_t(y)*size+x)*4,4,BYTE(lroundf(255.f*coverage)));
            }
            const auto edge=LongestSoftEdge(image,size,size);
            Check(offset==0.f ? edge.length==0 : edge.length>=size-12,"soft-edge detector did not classify a known fill");
        }
    }
    // Snapping keeps whole-pixel spans whole in every quadrant, at half-pixel
    // ties and through a translated, DPI-scaled target.
    {
        PresentationFixture::Tab tab(L"snap arithmetic",1);
        auto& state=tab.state;
        Check(EnsureTarget(state),"snap target");
        for(const float dpi:{96.f,120.f,144.f,168.f,192.f}) for(const float shift:{0.f,-.5f,.5f,-.25f}) {
            Regression::Scope scope("snap dpi="+std::to_string(int(dpi))+" shift="+std::to_string(shift));
            state.target->SetDpi(dpi,dpi);
            state.target->SetTransform(D2D1::Matrix3x2F::Translation(shift,shift));
            const float scale=dpi/96.f;
            // Exact half-pixel ties are reproducible where the scale is exact
            // in binary (100%, 200%); other scales use off-tie positions.
            const bool exact=dpi==96.f || dpi==192.f;
            const auto starts=exact ? std::vector<float>{-3.5f,-.5f,0.f,.5f,7.5f,-12.5f} :
                std::vector<float>{-3.3f,-.7f,.2f,.6f,7.4f,-12.8f};
            for(const float start:starts) for(const int span:{1,2,7,8}) {
                // A span of whole device pixels at a (possibly half-pixel) device position.
                const float from=(start-shift*scale)/scale,to=(start+float(span)-shift*scale)/scale;
                const auto r=SnapDrawRect(state.target.Get(),D2D1::RectF(from,from,to,to));
                for(const float device:{(r.left+shift)*scale,(r.right+shift)*scale,(r.top+shift)*scale,(r.bottom+shift)*scale})
                    Check(fabsf(device-roundf(device))<.001f,"snapped edge is not on a device pixel");
                Check(lroundf((r.right-r.left)*scale)==span && lroundf((r.bottom-r.top)*scale)==span,
                    "snapping changed a whole-pixel span");
            }
        }
        state.target->SetDpi(96,96); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    // Status-bar switch: both shafts are exactly one DPI-scaled stroke of
    // fully covered rows; no row carries a long partially covered run.
    for(int dpi=96;dpi<=288;dpi+=24) {
        Regression::Scope scope("switch glyph dpi="+std::to_string(dpi));
        const int size=MulDiv(16,dpi,96),stroke=std::max(1,MulDiv(1,dpi,96));
        const auto coverage=ViewToggleGlyphCoverage(size,dpi);
        Check(coverage.size()==size_t(size)*size,"switch glyph was not rasterized");
        std::vector<int> shaftRows;
        for(int y=0;y<size;++y) {
            int full=0,partial=0,bestFull=0,bestPartial=0;
            for(int x=0;x<size;++x) {
                const BYTE value=coverage[size_t(y)*size+x];
                full=value==255 ? full+1 : 0; partial=value>0 && value<255 ? partial+1 : 0;
                bestFull=std::max(bestFull,full); bestPartial=std::max(bestPartial,partial);
            }
            if(bestFull>=size/2) shaftRows.push_back(y);
            Check(bestPartial<std::max(4,size/4),"switch glyph has a soft straight edge");
        }
        Check(shaftRows.size()==size_t(2*stroke) && shaftRows[size_t(stroke)-1]-shaftRows.front()==stroke-1 &&
            shaftRows.back()-shaftRows[size_t(stroke)]==stroke-1,"switch glyph shafts are not whole-pixel strokes");
        std::vector<BYTE> bgra(coverage.size()*4);
        for(size_t i=0;i<coverage.size();++i) std::fill_n(bgra.data()+i*4,4,coverage[i]);
        const auto edge=LongestSoftEdge(bgra,size,size);
        Check(edge.length<std::max(4,size/4),("switch glyph: "+Describe(edge)).c_str());
    }
    // Real list frames: highlights, hover, checkboxes, header, dividers,
    // icons and scrollbars at fractional DPIs, in every layout and direction.
    for(const UINT dpi:{120u,144u,168u}) for(const bool rtl:{false,true})
    for(const UINT mode:{UINT(FVM_DETAILS),UINT(FVM_LIST),UINT(FVM_SMALLICON),UINT(FVM_ICON)}) {
        Regression::Scope scope("frame dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+" mode="+std::to_string(mode));
        StyleFixture::Session style; StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"pixel snapping",40,rtl,IsIconViewMode(mode));
        auto& view=tab.state;
        ApplyViewLayout(view,{mode,48},false); view.cachedNativeMode=mode;
        view.clickPolicy.checkSelect=true;
        UpdateMetrics(view); UpdateColumns(view);
        view.selectedFlags.Fill(0); view.selectedFlags[0]=view.selectedFlags[2]=1; view.selected=2; view.hovered=1;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); view.qpcFrequency=double(frequency.QuadPart);
        AnimationClockFixture::Freeze clock;
        TouchInputTest::RenderedTouch frame;
        const auto pixels=frame.Capture(view);
        const auto extent=frame.renderer.backbuffer->GetPixelSize();
        // Text boxes of every visible item and header caption, in physical pixels.
        std::vector<RECT> text;
        const auto physical=[&](D2D1_RECT_F r) {
            const LONG a=LogicalX(view,LONG(floorf(r.left))),b=LogicalX(view,LONG(ceilf(r.right)));
            return RECT{std::min(a,b)-1,LONG(floorf(r.top)),std::max(a,b)+1,LONG(ceilf(r.bottom))};
        };
        for(const int row:VisibleItemRows(view,0)) text.push_back(physical(LayoutItem(view,LayoutRowVisual(view,row,1.f)).text));
        if(ColumnsPresented(view)) for(size_t i=0;i<view.columns.size();++i)
            text.push_back(physical(ColumnTextBounds(view,i,true,ListStartX(view),int(dpi))));
        const auto edge=LongestSoftEdge(pixels,int(extent.width),int(extent.height),{},text);
        // Glyph bars are shorter; every straight UI edge is far longer.
        if(edge.length>=Scale96(8,int(dpi))) {
            // Name the element: the profile across the edge at its midpoint.
            const int mx=edge.vertical ? edge.x : edge.x+edge.length/2,my=edge.vertical ? edge.y+edge.length/2 : edge.y;
            std::cout<<"PROFILE across edge (BGRA):";
            for(int d=-3;d<=3;++d) {
                const int x=edge.vertical ? mx+d : mx,y=edge.vertical ? my : my+d;
                const BYTE* p=pixels.data()+(size_t(y)*extent.width+x)*4;
                std::cout<<" ["<<int(p[0])<<","<<int(p[1])<<","<<int(p[2])<<","<<int(p[3])<<"]";
            }
            std::cout<<std::endl;
        }
        Check(edge.length<Scale96(8,int(dpi)),("list frame: "+Describe(edge)).c_str());
    }
    // Drag preview chrome: the count pill and the target's caption box. The
    // operation emoji and resampled artwork are images, not grid geometry.
    {
        PresentationFixture::Tab tab(L"pixel snapping drag",1);
        auto& state=tab.state;
        Check(EnsureTarget(state),"drag snapping target");
        IconPixels artwork; artwork.width=artwork.height=48; artwork.stride=48*4;
        artwork.data.assign(size_t(artwork.stride)*artwork.height,255);
        ComPtr<ID2D1Image> previous; state.target->GetTarget(&previous);
        for(const int dpi:{120,144,168}) for(const bool rtl:{false,true}) {
            Regression::Scope scope("drag dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl));
            DragPreview preview; preview.dpi=dpi; preview.side=float(MulDiv(96,dpi,96)); preview.pixels=artwork;
            preview.background=RGB(248,248,248); preview.foreground=RGB(24,24,24); preview.accent=RGB(0,100,200);
            preview.rtl=rtl; preview.count=7; preview.imageCount=1; preview.effect=DROPEFFECT_COPY;
            const float available=float(MulDiv(260,dpi,96));
            Check(UpdateDragCaption(state,preview,ShellDropText{L"Copy to Documents",{8,9}},available) && preview.caption,
                "drag caption layout");
            const auto geometry=LayoutDragPreview(preview);
            ComPtr<ID2D1Bitmap1> surface;
            const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            Check(SUCCEEDED(state.target->CreateBitmap(geometry.extent,nullptr,0,&properties,&surface)),"drag snapping surface");
            state.target->SetTarget(surface.Get()); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            DrawDragPreview(state.target.Get(),preview);
            Check(SUCCEEDED(state.target->EndDraw()),"drag snapping draw");
            const auto read=IconAlphaFixture::Read(state,surface.Get());
            const auto box=[](D2D1_RECT_F r,int pad) {
                return RECT{LONG(floorf(r.left))-pad,LONG(floorf(r.top))-pad,LONG(ceilf(r.right))+pad,LONG(ceilf(r.bottom))+pad};
            };
            // The operation emoji is normalized to the badge's width; taller
            // symbols (the clipboard) overhang its box vertically.
            const int overhang=int(ceilf((geometry.operation.bottom-geometry.operation.top)*.25f));
            const auto edge=LongestSoftEdge(read.data.data(),int(read.width),int(read.height),read.stride,
                {box(geometry.artwork,2),box(geometry.operation,overhang)},
                {box(D2D1::RectF(geometry.caption.left+geometry.edge,geometry.caption.top+geometry.edge,
                    geometry.caption.right-geometry.edge,geometry.caption.bottom-geometry.edge),0)});
            if(edge.length>=Scale96(8,dpi)) {
                const auto print=[](const char* name,D2D1_RECT_F r) {
                    std::cout<<" "<<name<<"=("<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<")";
                };
                std::cout<<"PROFILE drag geometry:"; print("artwork",geometry.artwork); print("operation",geometry.operation);
                print("caption",geometry.caption); std::cout<<" extent="<<geometry.extent.width<<"x"<<geometry.extent.height<<std::endl;
            }
            Check(edge.length<Scale96(8,dpi),("drag preview: "+Describe(edge)).c_str());
        }
        state.target->SetTarget(previous.Get());
    }
    std::cout<<"PASS: whole-pixel snapping at half-pixel ties and fractional DPI; crisp list frames, checkboxes, drag chrome and switch glyph shafts"<<std::endl;
}
