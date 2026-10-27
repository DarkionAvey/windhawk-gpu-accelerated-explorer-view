// Manual read-only Shell image diagnostic. Usage: <item> <private output dir>.
// Never execute the item, modify its files, or interact with Explorer input.
#include <windows.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Inspect(IWICImagingFactory* wic,IWICBitmapSource* source,
                    const std::filesystem::path& output,const wchar_t* label) {
    UINT width=0,height=0; source->GetSize(&width,&height);
    if(!width || !height || width>4096 || height>4096) return;
    ComPtr<IWICFormatConverter> converted; wic->CreateFormatConverter(&converted);
    HRESULT hr=converted->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,
        nullptr,0,WICBitmapPaletteTypeCustom);
    std::vector<BYTE> pixels(size_t(width)*height*4);
    if(SUCCEEDED(hr)) hr=converted->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data());
    if(FAILED(hr)) {wprintf(L"%ls decode=%08lx\n",label,hr);return;}
    UINT left=width,top=height,right=0,bottom=0; size_t ink=0,opaque=0;
    for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
        const auto* p=pixels.data()+(size_t(y)*width+x)*4;
        if(p[3]) {left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=y+1;++ink;}
        opaque+=p[3]==255;
    }
    wprintf(L"%ls size=%ux%u alpha-bounds=%u,%u,%u,%u ink=%zu opaque=%zu\n",
        label,width,height,left,top,right,bottom,ink,opaque); fflush(stdout);
    ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    wic->CreateStream(&stream); hr=stream->InitializeFromFilename(output.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr)) hr=wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr)) hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr)) hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr)) hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr)) hr=frame->SetSize(width,height);
    GUID format=GUID_WICPixelFormat32bppBGRA;
    if(SUCCEEDED(hr)) hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr)) hr=frame->WriteSource(source,nullptr);
    if(SUCCEEDED(hr)) hr=frame->Commit();
    if(SUCCEEDED(hr)) hr=encoder->Commit();
    if(FAILED(hr)) wprintf(L"PNG=%08lx\n",hr);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized)) return 3;
    std::filesystem::create_directories(argv[2]);
    ComPtr<IWICImagingFactory> wic; ComPtr<IShellItemImageFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    if(SUCCEEDED(hr)) hr=SHCreateItemFromParsingName(argv[1],nullptr,IID_PPV_ARGS(&factory));
    if(FAILED(hr)) {wprintf(L"factory=%08lx\n",hr);return 4;}
    for(int side:{32,96,256,512}) for(bool thumbnail:{true,false}) {
        const std::wstring label=std::wstring(thumbnail ? L"thumbnail-" : L"icon-")+std::to_wstring(side);
        HBITMAP raw=nullptr; hr=factory->GetImage({side,side},SIIGBF((thumbnail ? SIIGBF_THUMBNAILONLY : SIIGBF_ICONONLY)|SIIGBF_BIGGERSIZEOK),&raw);
        wprintf(L"%ls image=%08lx\n",label.c_str(),hr); fflush(stdout);
        if(SUCCEEDED(hr) && raw) {
            ComPtr<IWICBitmap> source; hr=wic->CreateBitmapFromHBITMAP(raw,nullptr,WICBitmapUseAlpha,&source);
            if(SUCCEEDED(hr)) Inspect(wic.Get(),source.Get(),std::filesystem::path(argv[2])/(label+L".png"),label.c_str());
        }
        if(raw) DeleteObject(raw);
    }
    SHFILEINFOW info{};
    if(SHGetFileInfoW(argv[1],0,&info,sizeof(info),SHGFI_SYSICONINDEX)) for(int tier:{SHIL_SMALL,SHIL_LARGE,SHIL_EXTRALARGE,SHIL_JUMBO}) {
        const auto label=L"list-"+std::to_wstring(tier); ComPtr<IImageList> list; HICON icon=nullptr;
        hr=SHGetImageList(tier,IID_PPV_ARGS(&list));
        if(SUCCEEDED(hr)) hr=list->GetIcon(info.iIcon,ILD_TRANSPARENT,&icon);
        ComPtr<IImageList2> original;
        int originalWidth=0,originalHeight=0;
        if(list && SUCCEEDED(list.As(&original))) {
            const auto originalResult=original->GetOriginalSize(info.iIcon,ILGOS_FROMSTANDBY,&originalWidth,&originalHeight);
            wprintf(L"%ls original=%08lx %dx%d\n",label.c_str(),originalResult,originalWidth,originalHeight);
        }
        if(SUCCEEDED(hr) && icon) {
            ComPtr<IWICBitmap> source; hr=wic->CreateBitmapFromHICON(icon,&source);
            if(SUCCEEDED(hr)) Inspect(wic.Get(),source.Get(),std::filesystem::path(argv[2])/(label+L".png"),label.c_str());
        }
        if(icon) DestroyIcon(icon);
    }
    factory.Reset(); wic.Reset(); CoUninitialize(); return 0;
}
