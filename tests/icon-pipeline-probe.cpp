// Manual read-only probe for an explicitly supplied item. Renders the real
// worker/cache/upload/draw pipeline in private offscreen windows, with no
// Explorer navigation, file writes to the source folder, or clipboard/focus changes.
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
#include GPU_OVERLAY_SOURCE
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
#include "icon-alpha-smoke.h"
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    WorkerComApartment apartment;
    try {
        g_perf=false; g_listThumbnails=false;
        Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"probe fonts");
        ComPtr<IWICImagingFactory> wic;
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"probe WIC");
        {
            ViewState state;
            state.hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private icon source comparison",
                WS_POPUP|WS_VISIBLE,-32000,-32000,1000,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(state.hwnd!=nullptr,"probe window");
            state.frameHwnd=state.nativeHwnd=state.activeTabHost=state.hwnd;state.background->hwnd=state.hwnd;
            state.visible=state.inScope=state.loaded=state.detailsViewActive=true;state.clipboardInitialized=true;
            state.firstContentSubmittedAt=1;state.featureInitializationStep=kOverlayFeatureSteps;state.deferColumnProviders=false;
            Entry entry;entry.path=argv[1];entry.name=std::filesystem::path(argv[1]).filename().wstring();entry.detailsReady=true;
            state.entries.push_back(entry);state.total=1;state.itemsPerRow=1;state.iconPx=256;state.rowPx=340;
            state.paintWidth=1000;state.background->imageMemoryProbeAfter=ULLONG_MAX;
            const auto demand=[&](bool grid) -> IconRecord* {
                state.gridView=grid;state.cachedNativeMode=grid ? FVM_ICON : FVM_DETAILS;
                std::wstring lookup;bool attributes=false;const auto key=IconKey(state,entry,lookup,attributes);
                Check(RequestEntryIcon(state,entry,false,{{key,0}}),"probe icon request");
                const auto deadline=GetTickCount64()+15000;
                IconRecord* result=nullptr;
                do {
                    ReceiveIcons(state,false);result=FindIconRecord(state,key,true);
                    if(result && result->pixels.Ready() && !result->pending) break;
                    Sleep(1);
                } while(GetTickCount64()<deadline);
                Check(result && result->pixels.Ready(),"probe worker result");
                wprintf(L"%ls %ls key=%ls source=%ux%u\n",argv[1],grid ? L"grid" : L"list",key.c_str(),result->pixels.width,result->pixels.height);
                return result;
            };
            IconRecord* grid=demand(true);IconRecord* list=demand(false);
            Check(EnsureTarget(state) && UploadIconBitmap(state,*grid),"probe grid upload");
            if(list!=grid) Check(UploadIconBitmap(state,*list),"probe list upload");
            ComPtr<IShellItemImageFactory> factory;HBITMAP raw=nullptr;ComPtr<IWICBitmap> beforeSource;
            Check(SUCCEEDED(SHCreateItemFromParsingName(argv[1],nullptr,IID_PPV_ARGS(&factory))) &&
                SUCCEEDED(factory->GetImage({256,256},SIIGBF(SIIGBF_ICONONLY|SIIGBF_BIGGERSIZEOK),&raw)),"probe original factory image");
            const auto imported=wic->CreateBitmapFromHBITMAP(raw,nullptr,WICBitmapUseAlpha,&beforeSource);DeleteObject(raw);
            Check(SUCCEEDED(imported),"probe original import");IconRecord before;before.pixels=DecodeIconPixels(wic.Get(),beforeSource.Get());
            Check(UploadIconBitmap(state,before),"probe original upload");
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            ComPtr<ID2D1Bitmap1> surface;
            Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1000,420),nullptr,0,&props,&surface)),"probe surface");
            state.target->SetTarget(surface.Get());state.paintWidth=1000;state.target->BeginDraw();state.target->Clear(Packed(0x181818));
            ComPtr<IDWriteTextFormat> label;
            Check(SUCCEEDED(g_writeFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,23,L"en-US",&label)),"probe labels");
            const wchar_t* labels[]={L"Previous factory source",L"New grid pipeline",L"New list pipeline"};
            IconRecord* records[]={&before,grid,list};
            for(int i=0;i<3;++i) {
                state.gridView=i!=2;
                const auto box=D2D1::RectF(float(i*330+20),16,float(i*330+325),52);
                state.brush->SetColor(Packed(0xFFFFFF));state.target->DrawTextW(labels[i],UINT32(wcslen(labels[i])),label.Get(),box,state.brush.Get());
                Check(DrawCachedIcon(state,*records[i],float(i*330+40),70),"probe actual cached draw");
            }
            const auto nameBox=D2D1::RectF(20,365,980,410);state.brush->SetColor(Packed(0xFFFFFF));
            state.target->DrawTextW(entry.name.c_str(),UINT32(entry.name.size()),label.Get(),nameBox,state.brush.Get());
            Check(SUCCEEDED(state.target->EndDraw()),"probe draw completion");
            IconAlphaFixture::Save(wic.Get(),IconAlphaFixture::Read(state,surface.Get()),argv[2]);
            surface.Reset();DropTarget(state);DestroyWindow(state.hwnd);
            {std::lock_guard lock(g_backgroundLifetimeMutex);state.background->Stop();state.background.reset();}
            SetEvent(g_backgroundReaperWake);
        }
        ShutDownFrames();ShutDownBackgroundWorkers();g_writeFactory.Reset();
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
