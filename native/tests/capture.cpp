#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include "wgc-source.h"
#include "video-writer.h"
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
constexpr unsigned CaptureWidth=1000,CaptureHeight=1000;
static void verify(HRESULT hr){if(FAILED(hr))throw hr;}
// 读取桌面合成后的真实像素，只分析样机的深灰色边框，避免用计时器当帧率。
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    RoInitialize(RO_INIT_MULTITHREADED);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc<3){LocalFree(argv);return 2;}fs::path out=argv[1];int duration=_wtoi(argv[2]);bool saveVideo=argc>=4&&std::wstring(argv[3])==L"--video";bool saveStills=argc<4;LocalFree(argv);
    fs::create_directories(out);
    try{
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        verify(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
        ComPtr<IDXGIDevice> dxgi;verify(device.As(&dxgi));ComPtr<IDXGIAdapter> adapter;verify(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIOutput> output;verify(adapter->EnumOutputs(0,&output));ComPtr<IDXGIOutput1> output1;verify(output.As(&output1));
        ComPtr<IDXGIOutputDuplication> duplication;HRESULT duplicateResult=output1->DuplicateOutput(device.Get(),&duplication);
        if(FAILED(duplicateResult)){
            // 混合显卡或虚拟输出下，默认设备未必对应实际主屏；逐一验证主屏所属适配器。
            ComPtr<IDXGIFactory1> factory;verify(adapter->GetParent(IID_PPV_ARGS(&factory)));
            std::ofstream diagnostics(out/"duplication-adapters.txt");
            for(UINT a=0;!duplication;++a){
                ComPtr<IDXGIAdapter1> candidate;if(factory->EnumAdapters1(a,&candidate)==DXGI_ERROR_NOT_FOUND)break;
                for(UINT o=0;!duplication;++o){
                    ComPtr<IDXGIOutput> screenOutput;if(candidate->EnumOutputs(o,&screenOutput)==DXGI_ERROR_NOT_FOUND)break;
                    DXGI_OUTPUT_DESC description{};verify(screenOutput->GetDesc(&description));
                    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfo(description.Monitor,&monitor);
                    diagnostics<<"adapter "<<a<<" output "<<o<<" attached "<<description.AttachedToDesktop<<" primary "<<bool(monitor.dwFlags&MONITORINFOF_PRIMARY)<<"\n";
                    if(!description.AttachedToDesktop||!(monitor.dwFlags&MONITORINFOF_PRIMARY))continue;
                    ComPtr<ID3D11Device> candidateDevice;ComPtr<ID3D11DeviceContext> candidateContext;
                    HRESULT hr=D3D11CreateDevice(candidate.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&candidateDevice,&level,&candidateContext);
                    ComPtr<IDXGIOutput1> candidateOutput;
                    if(SUCCEEDED(hr))hr=screenOutput.As(&candidateOutput);
                    if(SUCCEEDED(hr))hr=candidateOutput->DuplicateOutput(candidateDevice.Get(),&duplication);
                    diagnostics<<"result "<<std::hex<<hr<<std::dec<<"\n";
                    if(SUCCEEDED(hr)){device=candidateDevice;context=candidateContext;duplicateResult=hr;}
                }
            }
        }
        HDC screen=nullptr,memory=nullptr;HBITMAP dib=nullptr;void* dibPixels=nullptr;
        WgcSource wgc;HRESULT wgcResult=E_FAIL;
        if(FAILED(duplicateResult)){device.As(&dxgi);wgcResult=wgc.start(dxgi.Get());std::ofstream(out/"wgc-result.txt")<<std::hex<<wgcResult<<"\ninterval_result="<<wgc.intervalResult<<std::dec<<"\noriginal_interval_100ns="<<wgc.originalInterval;}
        bool useWgc=SUCCEEDED(wgcResult);
        if(FAILED(duplicateResult)&&!useWgc){
            std::ofstream(out/"capture-source.txt")<<"GDI screen pixels; timestamps are capture times, not hardware presentation times. DXGI error="<<std::hex<<duplicateResult;
            screen=GetDC(nullptr);memory=CreateCompatibleDC(screen);BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=CaptureWidth;bitmap.bmiHeader.biHeight=-int(CaptureHeight);bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;
            dib=CreateDIBSection(screen,&bitmap,DIB_RGB_COLORS,&dibPixels,nullptr,0);if(!dib)throw E_FAIL;SelectObject(memory,dib);
        }else std::ofstream(out/"capture-source.txt")<<(useWgc?"Windows Graphics Capture; timestamps are SystemRelativeTime converted to QPC ticks.":"DXGI desktop duplication; timestamps are LastPresentTime.");
        ComPtr<IWICImagingFactory> wic;verify(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
        std::ofstream csv(out/"frames.csv");csv<<"qpc,x,y,width,height,border_pixels,accumulated_frames\n";
        std::ofstream costs(out/"capture-costs.csv");costs<<"readback_ms,analysis_ms,save_ms\n";
        ComPtr<ID3D11Texture2D> staging;LARGE_INTEGER freq,start,now;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
        unsigned saved=0;double lastSave=-1;
        int previousWidth=0,previousDelta=0,previousHeight=0,previousHeightDelta=0,previousBorder=0;unsigned anomalies=0;
        // 编码画布按 256 像素对齐，避免硬件转换器对 RGB32 行跨度的隐式补齐。
        VideoWriter video;if(saveVideo)video.start((out/L"frame-recording.mp4").c_str(),1024,1024);
        for(;;){
            QueryPerformanceCounter(&now);double elapsed=double(now.QuadPart-start.QuadPart)/freq.QuadPart;if(elapsed>=duration)break;
            DXGI_OUTDUPL_FRAME_INFO info{};ComPtr<IDXGIResource> resource;
            D3D11_TEXTURE2D_DESC desc{};D3D11_MAPPED_SUBRESOURCE map{};
            if(useWgc){
                ComPtr<ID3D11Texture2D> source;LONGLONG time=0;HRESULT hr=wgc.next(&source,&time);
                if(hr==S_FALSE){wgc.wait();continue;}verify(hr);
                info.LastPresentTime.QuadPart=LONGLONG(double(time)*freq.QuadPart/10000000.);info.AccumulatedFrames=1;
                source->GetDesc(&desc);
                desc.Width=std::min(desc.Width,CaptureWidth);desc.Height=std::min(desc.Height,CaptureHeight);
                if(!staging){desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.Usage=D3D11_USAGE_STAGING;verify(device->CreateTexture2D(&desc,nullptr,&staging));}
                D3D11_BOX box{0,0,0,desc.Width,desc.Height,1};
                context->CopySubresourceRegion(staging.Get(),0,0,0,0,source.Get(),0,&box);verify(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
            }else if(duplication){
                HRESULT hr=duplication->AcquireNextFrame(100,&info,&resource);if(hr==DXGI_ERROR_WAIT_TIMEOUT)continue;verify(hr);
                ComPtr<ID3D11Texture2D> source;verify(resource.As(&source));source->GetDesc(&desc);
                if(!staging){desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.Usage=D3D11_USAGE_STAGING;verify(device->CreateTexture2D(&desc,nullptr,&staging));}
                context->CopyResource(staging.Get(),source.Get());verify(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
            }else{if(!BitBlt(memory,0,0,CaptureWidth,CaptureHeight,screen,0,0,SRCCOPY|CAPTUREBLT))throw HRESULT_FROM_WIN32(GetLastError());QueryPerformanceCounter(&info.LastPresentTime);desc.Width=CaptureWidth;desc.Height=CaptureHeight;map.pData=dibPixels;map.RowPitch=CaptureWidth*4;info.AccumulatedFrames=1;}
            LARGE_INTEGER readbackEnd,analysisEnd,saveEnd;QueryPerformanceCounter(&readbackEnd);
            int width=std::min(CaptureWidth,desc.Width),height=std::min(CaptureHeight,desc.Height);
            std::vector<BYTE> mask(size_t(width)*height);std::vector<int> queue;queue.reserve(40000);
            for(int y=0;y<height;++y){const BYTE* row=static_cast<BYTE*>(map.pData)+y*map.RowPitch;for(int x=0;x<width;++x){const BYTE* pixel=row+x*4;mask[y*width+x]=(abs(pixel[0]-41)<=1&&abs(pixel[1]-39)<=1&&abs(pixel[2]-32)<=1);}}
            int best=0,bx=0,by=0,bw=0,bh=0;
            for(int i=0;i<width*height;++i){
                if(mask[i]!=1)continue;queue.clear();queue.push_back(i);mask[i]=2;int left=i%width,right=left,top=i/width,bottom=top;
                for(size_t k=0;k<queue.size();++k){int at=queue[k],x=at%width,y=at/width;left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
                    int neighbors[]={x?at-1:-1,x+1<width?at+1:-1,y?at-width:-1,y+1<height?at+width:-1};
                    for(int n:neighbors)if(n>=0&&mask[n]==1){mask[n]=2;queue.push_back(n);}}
                if(int(queue.size())>best){best=int(queue.size());bx=left;by=top;bw=right-left+1;bh=bottom-top+1;}
            }
            QueryPerformanceCounter(&analysisEnd);
            int delta=bw-previousWidth;
            int heightDelta=bh-previousHeight;
            bool widthReversal=abs(delta)>2&&abs(previousDelta)>2&&delta*previousDelta<0;
            bool heightReversal=abs(heightDelta)>2&&abs(previousHeightDelta)>2&&heightDelta*previousHeightDelta<0;
            bool borderLoss=previousBorder>2000&&best<previousBorder*.95;
            bool suspicious=best>2000&&(widthReversal||heightReversal||borderLoss);
            previousDelta=delta;previousWidth=bw;previousHeightDelta=heightDelta;previousHeight=bh;previousBorder=best;
            if(saveVideo&&best>2000)video.write(static_cast<const BYTE*>(map.pData),map.RowPitch,bx,by,bw,bh,LONGLONG(double(info.LastPresentTime.QuadPart)*10000000./freq.QuadPart));
            csv<<info.LastPresentTime.QuadPart<<","<<bx<<","<<by<<","<<bw<<","<<bh<<","<<best<<","<<info.AccumulatedFrames<<"\n";
            bool anomaly=suspicious&&anomalies<30;
            if((saveStills&&best>2000&&elapsed-lastSave>.2&&saved<60)||anomaly){
                // 仅保存已识别的相框矩形，不保存周围桌面。
                std::vector<BYTE> pixels(size_t(bw)*bh*4);
                for(int y=0;y<bh;++y)memcpy(pixels.data()+size_t(y)*bw*4,static_cast<BYTE*>(map.pData)+(by+y)*map.RowPitch+bx*4,size_t(bw)*4);
                auto file=out/(anomaly?(L"anomaly-"+std::to_wstring(info.LastPresentTime.QuadPart)+L".png"):(L"frame-"+std::to_wstring(saved++)+L".png"));if(anomaly)++anomalies;
                ComPtr<IWICStream> stream;verify(wic->CreateStream(&stream));verify(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE));
                ComPtr<IWICBitmapEncoder> encoder;verify(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));verify(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
                ComPtr<IWICBitmapFrameEncode> frame;verify(encoder->CreateNewFrame(&frame,nullptr));verify(frame->Initialize(nullptr));verify(frame->SetSize(bw,bh));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;verify(frame->SetPixelFormat(&format));verify(frame->WritePixels(bh,bw*4,UINT(pixels.size()),pixels.data()));verify(frame->Commit());verify(encoder->Commit());lastSave=elapsed;
            }
            if(useWgc)context->Unmap(staging.Get(),0);else if(duplication){context->Unmap(staging.Get(),0);duplication->ReleaseFrame();}else Sleep(5);
            QueryPerformanceCounter(&saveEnd);
            costs<<1000.*(readbackEnd.QuadPart-now.QuadPart)/freq.QuadPart<<","<<1000.*(analysisEnd.QuadPart-readbackEnd.QuadPart)/freq.QuadPart<<","<<1000.*(saveEnd.QuadPart-analysisEnd.QuadPart)/freq.QuadPart<<"\n";
        }
        video.finish();
        if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);if(dib)DeleteObject(dib);
    }catch(HRESULT hr){std::ofstream(out/"error.txt")<<std::hex<<hr;return 1;}
    CoUninitialize();return 0;
}
