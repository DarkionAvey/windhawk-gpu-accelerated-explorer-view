// Standalone, opt-in offscreen cost probe; not a regression or live Explorer test.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi1_6.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) {
    if (FAILED(hr)) { char text[64]; std::snprintf(text,sizeof(text),"HRESULT 0x%08lx",(unsigned long)hr); throw std::runtime_error(text); }
}
struct Probe {
    static constexpr UINT width=2160, height=4000, visible=200;
    ComPtr<ID3D11Device> gpu;
    ComPtr<ID3D11DeviceContext> commands;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> draw;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    ComPtr<ID2D1BitmapBrush> noise;
    std::array<ComPtr<ID2D1BitmapBrush>,2> cached;
    ComPtr<ID3D11Query> disjoint, start, end;
    LARGE_INTEGER frequency{};
    Probe(bool integrated) {
        if(integrated) {
            ComPtr<IDXGIFactory6> adapters; Check(CreateDXGIFactory1(IID_PPV_ARGS(&adapters)));
            for(UINT index=0;;++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT hr=adapters->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_MINIMUM_POWER,IID_PPV_ARGS(&candidate));
                if(hr==DXGI_ERROR_NOT_FOUND) break;
                Check(hr);
                DXGI_ADAPTER_DESC1 info{}; Check(candidate->GetDesc1(&info));
                if(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                ComPtr<ID3D11Device> candidateDevice;
                ComPtr<ID3D11DeviceContext> candidateCommands;
                const HRESULT created=D3D11CreateDevice(candidate.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&candidateDevice,nullptr,&candidateCommands);
                if(FAILED(created)) { std::printf("adapter_probe=%ls; create_hr=0x%08lx\n",info.Description,(unsigned long)created); continue; }
                D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
                const HRESULT queried=candidateDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2,&options,sizeof(options));
                std::printf("adapter_probe=%ls; UMA=%d; feature_hr=0x%08lx\n",info.Description,
                    SUCCEEDED(queried)?int(options.UnifiedMemoryArchitecture):-1,(unsigned long)queried);
                if(SUCCEEDED(queried) && options.UnifiedMemoryArchitecture) {
                    gpu=std::move(candidateDevice); commands=std::move(candidateCommands); break;
                }
            }
            if(!gpu) throw std::runtime_error("No hardware unified-memory adapter available; no fallback");
        } else {
            Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&gpu,nullptr,&commands)); // Deliberately no software fallback.
        }
        ComPtr<IDXGIDevice> dxgi; Check(gpu.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter; Check(dxgi->GetAdapter(&adapter));
        DXGI_ADAPTER_DESC desc{}; Check(adapter->GetDesc(&desc));
        std::printf("adapter=%ls; vendor=0x%x; device=0x%x\n",desc.Description,desc.VendorId,desc.DeviceId);
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,IID_PPV_ARGS(&factory)));
        Check(factory->CreateDevice(dxgi.Get(),&device));
        Check(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&draw));
        auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(draw->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,properties,&target));
        draw->SetTarget(target.Get());
        D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(0x3b82f6,1.f)},{1,D2D1::ColorF(0x1e3a8a,26.f/255.f)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        Check(draw->CreateGradientStopCollection(stops,2,D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,&collection));
        Check(draw->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(),D2D1::Point2F(1,0)),collection.Get(),&gradient));
        // Cached, low-amplitude noise overlay candidate. Equal black/white counts;
        // premultiplied BGRA, alpha 2/255. This is visual noise, not a custom
        // shader that dithers the final composite's quantization. Quality differs.
        std::array<UINT32,64*64> pixels{};
        for (size_t i=0;i<pixels.size();++i) pixels[i]=(i&1)?0x02020202:0x02000000;
        UINT32 rng=0x593a7681;
        for (size_t i=pixels.size()-1;i>0;--i) {
            rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
            std::swap(pixels[i],pixels[rng%(i+1)]);
        }
        ComPtr<ID2D1Bitmap> bitmap;
        Check(draw->CreateBitmap(D2D1::SizeU(64,64),pixels.data(),64*4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&bitmap));
        Check(draw->CreateBitmapBrush(bitmap.Get(),D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_WRAP,D2D1_EXTEND_MODE_WRAP,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),&noise));
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT,0}; Check(gpu->CreateQuery(&query,&disjoint));
        query.Query=D3D11_QUERY_TIMESTAMP; Check(gpu->CreateQuery(&query,&start)); Check(gpu->CreateQuery(&query,&end));
        QueryPerformanceFrequency(&frequency);
        // Alternative cost probe: exact-width, precomputed ordered-dither ramp.
        // Bake opacity into texels; a subsequent brush-opacity multiplication
        // would introduce another quantization after this dithering step.
        // This synthetic ramp is not a pixel-equivalence implementation of D2D's
        // interpolation, nor dithering of the final background composite.
        constexpr BYTE bayer[64]={0,48,12,60,3,51,15,63,32,16,44,28,35,19,47,31,
            8,56,4,52,11,59,7,55,40,24,36,20,43,27,39,23,
            2,50,14,62,1,49,13,61,34,18,46,30,33,17,45,29,
            10,58,6,54,9,57,5,53,42,26,38,22,41,25,37,21};
        for(int mode=0;mode<2;++mode) {
            LARGE_INTEGER a{},b{}; QueryPerformanceCounter(&a);
            constexpr UINT rampWidth=width-16;
            std::vector<UINT32> ramp(rampWidth*8);
            const float opacity=mode?.4f:1.f;
            for(UINT y=0;y<8;++y) for(UINT x=0;x<rampWidth;++x) {
                const float t=(x+.5f)/rampWidth;
                const float alpha=(1.f+(26.f/255.f-1.f)*t)*opacity;
                auto quantize=[&](float value,UINT phase) {
                    const float threshold=(bayer[(y*8+x%8+phase)%64]+.5f)/64.f;
                    return UINT32(std::clamp(std::floor(value*255.f+threshold),0.f,255.f));
                };
                const UINT32 alpha8=quantize(alpha,0);
                const UINT32 r=std::min(alpha8,quantize((59.f+(30.f-59.f)*t)/255.f*alpha,13));
                const UINT32 g=std::min(alpha8,quantize((130.f+(58.f-130.f)*t)/255.f*alpha,29));
                const UINT32 b8=std::min(alpha8,quantize((246.f+(138.f-246.f)*t)/255.f*alpha,47));
                ramp[y*rampWidth+x]=(alpha8<<24)|(r<<16)|(g<<8)|b8;
            }
            ComPtr<ID2D1Bitmap> rampBitmap;
            Check(draw->CreateBitmap(D2D1::SizeU(rampWidth,8),ramp.data(),rampWidth*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&rampBitmap));
            Check(draw->CreateBitmapBrush(rampBitmap.Get(),D2D1::BitmapBrushProperties(
                D2D1_EXTEND_MODE_CLAMP,D2D1_EXTEND_MODE_WRAP,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),&cached[mode]));
            QueryPerformanceCounter(&b);
            std::printf("cached_setup_opacity=%.1f; cpu_ms=%.4f; bytes=%u\n",opacity,
                1000.*(b.QuadPart-a.QuadPart)/frequency.QuadPart,rampWidth*8*4);
        }
    }
    template<class T> T Result(ID3D11Query* query) {
        T data{}; const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        for (;;) {
            HRESULT hr=commands->GetData(query,&data,sizeof(data),0); Check(hr);
            if(hr==S_OK) return data;
            if(std::chrono::steady_clock::now()>deadline) throw std::runtime_error("GPU query deadline");
            SwitchToThread();
        }
    }
    void SavePng(const std::filesystem::path& path,UINT imageHeight) {
        ComPtr<ID2D1Bitmap1> readback;
        auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(draw->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,properties,&readback));
        Check(readback->CopyFromBitmap(nullptr,target.Get(),nullptr));
        D2D1_MAPPED_RECT mapped{}; Check(readback->Map(D2D1_MAP_OPTIONS_READ,&mapped));
        struct Unmap { ID2D1Bitmap1* bitmap; ~Unmap(){bitmap->Unmap();} } unmap{readback.Get()};
        ComPtr<IWICImagingFactory> wic;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
        ComPtr<IWICStream> stream; Check(wic->CreateStream(&stream));
        Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder; Check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
        Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame; Check(encoder->CreateNewFrame(&frame,nullptr));
        Check(frame->Initialize(nullptr)); Check(frame->SetSize(width,imageHeight));
        Check(frame->SetResolution(96,96));
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA; Check(frame->SetPixelFormat(&format));
        if(format!=GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("Unexpected PNG pixel format");
        Check(frame->WritePixels(imageHeight,mapped.pitch,mapped.pitch*imageHeight,mapped.bits));
        Check(frame->Commit()); Check(encoder->Commit());
    }
    void Visuals(const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);
        ComPtr<IDWriteFactory> textFactory;
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(textFactory.GetAddressOf())));
        ComPtr<IDWriteTextFormat> font;
        Check(textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,24,L"en-us",&font));
        ComPtr<ID2D1SolidColorBrush> ink; Check(draw->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&ink));
        auto label=[&](std::wstring_view value,float y) {
            draw->DrawText(value.data(),UINT32(value.size()),font.Get(),
                D2D1::RectF(8,y,float(width)-8,y+36),ink.Get());
        };
        const std::array<std::wstring_view,3> names={L"Native Direct2D gradient",L"Native gradient + cached noise overlay",L"Cached ordered-dither texture (benchmark prototype)"};
        for(int example=0;example<3;++example) {
            const bool light=example==2;
            const float opacity=example==1?1.f:.4f;
            draw->BeginDraw(); draw->Clear(D2D1::ColorF(light?0xf3f3f3:0x202020));
            ink->SetColor(D2D1::ColorF(light?0x171717:0xf3f3f3));
            label(light?L"Integrated GPU | light background | mod opacity 40%":
                example==1?L"Integrated GPU | dark background | mod opacity 100%":
                L"Integrated GPU | dark background | mod opacity 40%",8);
            label(L"Blue gradient: #3b82f6ff to #1e3a8a1a | full-width samples, 2144 physical pixels",42);
            gradient->SetOpacity(opacity); noise->SetOpacity(opacity);
            for(int mode=0;mode<3;++mode) {
                const float y=112.f+mode*192.f;
                label(names[mode],y-32);
                const auto rectangle=D2D1::RoundedRect(D2D1::RectF(8,y,float(width)-8,y+144),4,4);
                if(mode==2) {
                    auto brush=cached[opacity<1?1:0].Get();
                    brush->SetTransform(D2D1::Matrix3x2F::Translation(8,y));
                    draw->FillRoundedRectangle(rectangle,brush);
                } else {
                    gradient->SetStartPoint(D2D1::Point2F(8,y));
                    gradient->SetEndPoint(D2D1::Point2F(float(width)-8,y));
                    draw->FillRoundedRectangle(rectangle,gradient.Get());
                    if(mode==1) draw->FillRoundedRectangle(rectangle,noise.Get());
                }
            }
            Check(draw->EndDraw());
            // Copy actual rendered pixels, then magnify with nearest-neighbour.
            // The crop has no contrast enhancement or reconstructed gradient.
            ComPtr<ID2D1Bitmap1> snapshot;
            auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
            Check(draw->CreateBitmap(D2D1::SizeU(width,640),nullptr,0,properties,&snapshot));
            const auto source=D2D1::RectU(0,0,width,640);
            Check(snapshot->CopyFromBitmap(nullptr,target.Get(),&source));
            draw->BeginDraw();
            label(L"6x pixel crops | final 256 pixels of each gradient | no contrast enhancement",678);
            for(int mode=0;mode<3;++mode) {
                const float y=756.f+mode*144.f;
                label(names[mode],y-32);
                const auto crop=D2D1::RectF(float(width)-8-256,144.f+mode*192.f,float(width)-8,160.f+mode*192.f);
                draw->DrawBitmap(snapshot.Get(),D2D1::RectF(8,y,8+256*6,y+16*6),1,
                    D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,&crop);
            }
            Check(draw->EndDraw());
            const char* filename=light?"gradient-light-40.png":example==1?"gradient-dark-100.png":"gradient-dark-40.png";
            SavePng(directory/filename,1170);
            std::printf("visual=%s\n",filename);
        }
    }
    struct Timing { double cpu, gpu, completed; };
    Timing Frame(UINT count, int mode, float opacity) {
        LARGE_INTEGER before{},submitted{},finished{}; QueryPerformanceCounter(&before);
        commands->Begin(disjoint.Get()); commands->End(start.Get());
        draw->BeginDraw(); draw->Clear(D2D1::ColorF(0x202020));
        gradient->SetOpacity(opacity); noise->SetOpacity(opacity);
        for(UINT i=0;i<count;++i) {
            const float y=float(i%visible)*20.f;
            const auto bounds=D2D1::RoundedRect(D2D1::RectF(8,y+1,float(width)-8,y+19),4,4);
            gradient->SetStartPoint(D2D1::Point2F(8,y+10));
            gradient->SetEndPoint(D2D1::Point2F(float(width)-8,y+10));
            if(mode==2) {
                auto brush=cached[opacity<1.f?1:0].Get();
                brush->SetTransform(D2D1::Matrix3x2F::Translation(8,y+1));
                draw->FillRoundedRectangle(bounds,brush);
            } else {
                draw->FillRoundedRectangle(bounds,gradient.Get());
                if(mode==1) draw->FillRoundedRectangle(bounds,noise.Get());
            }
        }
        Check(draw->EndDraw());
        commands->End(end.Get()); commands->End(disjoint.Get()); commands->Flush();
        QueryPerformanceCounter(&submitted);
        const auto status=Result<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(disjoint.Get());
        const auto a=Result<UINT64>(start.Get()), b=Result<UINT64>(end.Get());
        QueryPerformanceCounter(&finished);
        if(status.Disjoint || !status.Frequency || b<a) throw std::runtime_error("Invalid GPU timestamps; sample not retried");
        return {1000.*(submitted.QuadPart-before.QuadPart)/frequency.QuadPart,
            1000.*double(b-a)/status.Frequency,
            1000.*(finished.QuadPart-before.QuadPart)/frequency.QuadPart};
    }
};
static double Quantile(std::vector<double> data,double q) {
    std::sort(data.begin(),data.end()); return data[size_t(q*double(data.size()-1)+.5)];
}
int main(int argc,char** argv) try {
    bool integrated=false;
    std::filesystem::path visuals;
    for(int i=1;i<argc;++i) {
        if(std::string_view(argv[i])=="--integrated") integrated=true;
        else if(std::string_view(argv[i])=="--visuals" && i+1<argc) visuals=argv[++i];
        else throw std::runtime_error("Usage: gradient-dither-benchmark [--integrated] [--visuals directory]");
    }
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
    struct ComRuntime { ~ComRuntime(){CoUninitialize();} } com;
    Probe probe(integrated);
    if(!visuals.empty()) { probe.Visuals(visuals); return 0; }
    constexpr int warmup=3,samples=21;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(110);
    std::puts("surface=2160x4000; compact_row=20px; visible=200; warmup=3; samples=21; hardware_only=1");
    std::puts("mode: 0=native, 1=native+noise_overlay, 2=cached_dither_ramp");
    std::puts("items,mod_opacity,mode,cpu_median_ms,cpu_p95_ms,gpu_median_ms,gpu_p95_ms,completed_median_ms");
    for(float opacity:{1.f,.4f}) for(UINT count:{200u,1000u,5000u,20000u}) {
        std::array<std::vector<Probe::Timing>,3> results;
        for(int sample=-warmup;sample<samples;++sample) for(int pass=0;pass<3;++pass) {
            if(std::chrono::steady_clock::now()>deadline) throw std::runtime_error("Benchmark deadline");
            const int order=(sample+warmup)%6;
            const int mode=order<3?(order+pass)%3:(order-3+2*pass)%3; // All six orders.
            auto timing=probe.Frame(count,mode,opacity);
            if(sample>=0) results[mode].push_back(timing);
        }
        for(int mode=0;mode<3;++mode) {
            std::vector<double> cpu,gpu,completed;
            for(auto timing:results[mode]) { cpu.push_back(timing.cpu); gpu.push_back(timing.gpu); completed.push_back(timing.completed); }
            std::printf("%u,%.1f,%d,%.4f,%.4f,%.4f,%.4f,%.4f\n",count,opacity,mode,
                Quantile(cpu,.5),Quantile(cpu,.95),Quantile(gpu,.5),Quantile(gpu,.95),Quantile(completed,.5));
            std::fflush(stdout);
        }
    }
    return 0;
} catch(const std::exception& error) { std::fprintf(stderr,"FAILED: %s\n",error.what()); return 1; }
