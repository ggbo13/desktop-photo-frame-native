#pragma once
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <random>
#include <source_location>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <functional>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
constexpr UINT DecodedMessage = WM_APP + 1;
constexpr UINT TimerId = 1;
static float Border = 18.f;
constexpr double Duration = .42;
static LARGE_INTEGER Frequency{};
static double seconds() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) / Frequency.QuadPart; }
static thread_local unsigned errorLine=0;
static void check(HRESULT hr,std::source_location source=std::source_location::current()) { if (FAILED(hr)) {errorLine=source.line();throw hr;} }
static float ease(float t) { t=std::clamp(t,0.f,1.f); return t*t*(3-2*t); }
struct Bounds { float x=120,y=120,w=480,h=360; };
#include "frame-behavior.h"
struct Pixels { UINT w=0,h=0; float ratio=1; uint64_t token=0; size_t index=0; std::vector<BYTE> bytes; HRESULT error=S_OK; };
struct Picture {
    ComPtr<IDCompositionSurface> surface;
    ComPtr<IDCompositionVisual2> visual;
    ComPtr<IDCompositionScaleTransform> scale;
    ComPtr<IDCompositionEffectGroup> effect;
    UINT w=1,h=1;
    float ratio=1;
    double uploadMs=0,preparedAt=0;
    bool attached=false;
};
#include "image-loader.h"

class Frame {
public:
    static constexpr int ResizeLeft=1,ResizeRight=2,ResizeTop=4,ResizeBottom=8;
    HWND hwnd=nullptr,inputHwnd=nullptr;
    RECT area{};
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGIAdapter3> gpuAdapter;
    ComPtr<ID2D1Device> d2d;
    ComPtr<IDCompositionDesktopDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual2> carrierRoot,root,mat,well;
    Bounds carrierBounds;bool carrierReady=false;
    ComPtr<IDCompositionRectangleClip> outerClip,innerClip;
    ComPtr<IDCompositionScaleTransform> matScale;
    ComPtr<IDCompositionSurface> matSurface;
    float matWidth=1,matHeight=1,cornerRadius=10,shadowDepth=0;
    ComPtr<IDCompositionVisual2> shadowVisuals[4];ComPtr<IDCompositionScaleTransform> shadowScales[4];ComPtr<IDCompositionSurface> shadowSurfaces[4];
    Picture current,incoming;
    Bounds bounds,from,to;
    std::vector<fs::path> paths;
    std::unordered_set<size_t> failedIndices;
    size_t index=0, pendingIndex=0, incomingIndex=0;
    int anchor=3,effect=0;
    bool paused=false,visible=true,animating=false,hasPhoto=false,benchmark=false,loading=false,prefetching=false,queued=false;
    bool softwareRenderer=false;
    bool reduceMotion=false,limitAutoSize=false;
    float maxWidth=640,maxHeight=640,preferredPhotoArea=0;
    double transitionDuration=Duration;
    int slideDirection=1,requestDirection=1,pendingDirection=1;
    uint64_t requestTag=0,incomingTag=0,pendingTag=0,presentedTag=0;
    bool dragging=false,resizing=false;
    int resizeAxes=0;
    bool suspended=false;
    POINT mouseStart{};Bounds interactionStart;
    double begin=0,next=0,runStart=0,runSeconds=0,testInterval=.08;
    uint64_t version=0;
    unsigned transitions=0,failed=0;
    std::ofstream log,events;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false,requested=false;
    fs::path requestedPath;
    uint64_t requestedToken=0;
    size_t requestedIndex=0;
    UINT decodeMax=900;
    float requestedPixels=144000;
    std::unique_ptr<Pixels> ready;
    Picture prepared;
    size_t preparedIndex=0;
    bool hasPrepared=false;
    double playInterval=10;
    float fixedRatio=0;
    std::function<size_t()> chooseNext;
    std::function<void(size_t)> presented;
    size_t following(){return chooseNext?chooseNext():(index+1)%paths.size();}

    ~Frame() {
        { std::lock_guard lock(mutex); stopping=true; }
        wake.notify_all(); if(worker.joinable())worker.join();
    }
    // 计数用于低负载回归，正常播放不输出高频日志。
    uint64_t compositionCommits=0;
    void commit(std::source_location source=std::source_location::current()){check(composition->Commit(),source);++compositionCommits;}
    void initialize() {
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level;
        // 硬件设备不可用时回退到 WARP，避免驱动重置后反复启动失败。
        HRESULT created=D3D11CreateDevice(nullptr,softwareRenderer?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,&level,&context);
        if(FAILED(created)&&!softwareRenderer){softwareRenderer=true;created=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,&level,&context);}
        check(created);
        ComPtr<IDXGIDevice> dxgi; check(d3d.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter;if(SUCCEEDED(dxgi->GetAdapter(&adapter)))adapter.As(&gpuAdapter);
        ComPtr<ID2D1Factory1> factory;
        check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),nullptr,reinterpret_cast<void**>(factory.GetAddressOf())));
        check(factory->CreateDevice(dxgi.Get(),&d2d));
        check(DCompositionCreateDevice2(d2d.Get(),__uuidof(IDCompositionDesktopDevice),reinterpret_cast<void**>(composition.GetAddressOf())));
        check(composition->CreateTargetForHwnd(hwnd,TRUE,&target));
        check(composition->CreateVisual(&carrierRoot));check(target->SetRoot(carrierRoot.Get()));
        check(composition->CreateVisual(&root));check(carrierRoot->AddVisual(root.Get(),TRUE,nullptr));
        check(composition->CreateRectangleClip(&outerClip)); check(root->SetClip(outerClip.Get()));
        outerClip->SetLeft(0.f);outerClip->SetTop(0.f);
        outerClip->SetTopLeftRadiusX(10); outerClip->SetTopLeftRadiusY(10);
        outerClip->SetTopRightRadiusX(10); outerClip->SetTopRightRadiusY(10);
        outerClip->SetBottomLeftRadiusX(10); outerClip->SetBottomLeftRadiusY(10);
        outerClip->SetBottomRightRadiusX(10); outerClip->SetBottomRightRadiusY(10);
        check(composition->CreateVisual(&mat)); check(root->AddVisual(mat.Get(),FALSE,nullptr));
        BYTE color[4]={41,39,32,255}; matSurface=surface(1,1,color); mat->SetContent(matSurface.Get());
        check(composition->CreateScaleTransform(&matScale)); mat->SetTransform(matScale.Get());
        check(composition->CreateVisual(&well)); check(root->AddVisual(well.Get(),TRUE,mat.Get()));
        well->SetOffsetX(Border);well->SetOffsetY(Border);
        check(composition->CreateRectangleClip(&innerClip));well->SetClip(innerClip.Get());
        innerClip->SetLeft(0.f);innerClip->SetTop(0.f);
        applyGeometry(bounds,false);
        commit();
        updateRegion(bounds);
        worker=std::thread([this]{decodeWorker();});
        next=seconds()+playInterval;
    }
    ComPtr<IDCompositionSurface> surface(UINT w,UINT h,const BYTE* data) {
        ComPtr<IDCompositionSurface> result;
        check(composition->CreateSurface(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&result));
        ComPtr<ID2D1DeviceContext> dc; POINT offset;
        check(result->BeginDraw(nullptr,__uuidof(ID2D1DeviceContext),reinterpret_cast<void**>(dc.GetAddressOf()),&offset));
        ComPtr<ID2D1Bitmap1> bitmap;
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hr=dc->CreateBitmap(D2D1::SizeU(w,h),data,w*4,&props,&bitmap);
        if(SUCCEEDED(hr)) {
            dc->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x),float(offset.y)));
            dc->Clear(D2D1::ColorF(0,0));
            dc->DrawBitmap(bitmap.Get(),D2D1::RectF(0,0,float(w),float(h)),1,D2D1_INTERPOLATION_MODE_LINEAR);
        }
        check(result->EndDraw());check(hr);return result;
    }
    ComPtr<IDCompositionAnimation> curve(float a,float b) {
        ComPtr<IDCompositionAnimation> value;check(composition->CreateAnimation(&value));
        value->AddCubic(0,a,0,float(3*(b-a)/(transitionDuration*transitionDuration)),float(-2*(b-a)/(transitionDuration*transitionDuration*transitionDuration)));
        value->End(transitionDuration,b);return value;
    }
    template<typename Object,typename Setter> void property(Object* object,Setter setter,float a,float b,bool animate) {
        // 由调用方使用明确的重载，保证全部属性共享同一个绝对起始时间。
        (void)object;(void)setter;(void)a;(void)b;(void)animate;
    }
    void ensureCarrier(Bounds a,Bounds b,bool reset=false){
        float left=std::floor(std::min(a.x,b.x))-1,top=std::floor(std::min(a.y,b.y))-1;
        float right=std::ceil(std::max(a.x+a.w,b.x+b.w))+1,bottom=std::ceil(std::max(a.y+a.h,b.y+b.h))+1;
        if(carrierReady&&!reset){
            left=std::min(left,carrierBounds.x);top=std::min(top,carrierBounds.y);
            right=std::max(right,carrierBounds.x+carrierBounds.w);bottom=std::max(bottom,carrierBounds.y+carrierBounds.h);
        }
        Bounds nextCarrier{left,top,std::max(1.f,right-left),std::max(1.f,bottom-top)};
        if(carrierReady&&nextCarrier.x==carrierBounds.x&&nextCarrier.y==carrierBounds.y&&nextCarrier.w==carrierBounds.w&&nextCarrier.h==carrierBounds.h)return;
        // 画布只容纳相框及换图包络，保留逻辑桌面坐标供固定角、拖动和多屏计算使用。
        // 换图期间复用已扩大的包络，结束时不再缩窗，避免每帧或每次收尾调整原生窗口。
        carrierBounds=nextCarrier;carrierReady=true;
        check(carrierRoot->SetOffsetX(-carrierBounds.x));check(carrierRoot->SetOffsetY(-carrierBounds.y));
        if(!SetWindowPos(hwnd,nullptr,area.left+int(left),area.top+int(top),int(nextCarrier.w),int(nextCarrier.h),SWP_NOZORDER|SWP_NOACTIVATE))throw E_FAIL;
    }
    void applyGeometry(Bounds end,bool animate) {
        ensureCarrier(animate?from:end,end,!hasPhoto||dragging||resizing);
        if(animate) {
            root->SetOffsetX(curve(from.x,end.x).Get());root->SetOffsetY(curve(from.y,end.y).Get());
            outerClip->SetRight(curve(from.w,end.w).Get());outerClip->SetBottom(curve(from.h,end.h).Get());
            matScale->SetScaleX(curve(from.w/matWidth,end.w/matWidth).Get());matScale->SetScaleY(curve(from.h/matHeight,end.h/matHeight).Get());
            innerClip->SetRight(curve(from.w-2*Border,end.w-2*Border).Get());innerClip->SetBottom(curve(from.h-2*Border,end.h-2*Border).Get());
        } else {
            root->SetOffsetX(end.x);root->SetOffsetY(end.y);outerClip->SetRight(end.w);outerClip->SetBottom(end.h);
            matScale->SetScaleX(end.w/matWidth);matScale->SetScaleY(end.h/matHeight);innerClip->SetRight(end.w-2*Border);innerClip->SetBottom(end.h-2*Border);
        }
        for(int i=0;i<4;++i)if(shadowVisuals[i]){
            auto geometry=[&](Bounds b){return i<2?Bounds{Border,i==0?Border:b.h-Border-shadowDepth,b.w-2*Border,shadowDepth}:Bounds{i==2?Border:b.w-Border-shadowDepth,Border,shadowDepth,b.h-2*Border};};auto a=geometry(from),b=geometry(end);
            if(animate){shadowVisuals[i]->SetOffsetX(curve(a.x,b.x).Get());shadowVisuals[i]->SetOffsetY(curve(a.y,b.y).Get());shadowScales[i]->SetScaleX(curve(a.w/(i<2?1:8),b.w/(i<2?1:8)).Get());shadowScales[i]->SetScaleY(curve(a.h/(i<2?8:1),b.h/(i<2?8:1)).Get());}
            else{shadowVisuals[i]->SetOffsetX(b.x);shadowVisuals[i]->SetOffsetY(b.y);shadowScales[i]->SetScaleX(b.w/(i<2?1:8));shadowScales[i]->SetScaleY(b.h/(i<2?8:1));}
        }
    }
    void setShadow(float strength){
        shadowDepth=Border>0?8.f:0.f;
        for(int i=0;i<4;++i){if(shadowVisuals[i])root->RemoveVisual(shadowVisuals[i].Get());shadowVisuals[i].Reset();shadowScales[i].Reset();shadowSurfaces[i].Reset();if(strength<=0||shadowDepth==0)continue;
            BYTE pixels[32]{};for(int p=0;p<8;++p){float distance=(i==1||i==3)?p/7.f:1-p/7.f;pixels[p*4+3]=BYTE(std::clamp(strength,0.f,1.f)*150*distance*distance);}
            shadowSurfaces[i]=surface(i<2?1:8,i<2?8:1,pixels);check(composition->CreateVisual(&shadowVisuals[i]));shadowVisuals[i]->SetContent(shadowSurfaces[i].Get());check(composition->CreateScaleTransform(&shadowScales[i]));shadowVisuals[i]->SetTransform(shadowScales[i].Get());root->AddVisual(shadowVisuals[i].Get(),TRUE,nullptr);
        }
    }
    void pictureGeometry(Picture& p,Bounds a,Bounds b,bool animate,bool entering) {
        float sa=std::max((a.w-2*Border)/p.w,(a.h-2*Border)/p.h);
        float sb=std::max((b.w-2*Border)/p.w,(b.h-2*Border)/p.h);
        if(fixedRatio!=0){sa=std::min((a.w-2*Border)/p.w,(a.h-2*Border)/p.h);sb=std::min((b.w-2*Border)/p.w,(b.h-2*Border)/p.h);}
        if(effect==2&&entering)sa*=1.025f;
        float xa=(a.w-2*Border-p.w*sa)/2,xb=(b.w-2*Border-p.w*sb)/2;
        float ya=(a.h-2*Border-p.h*sa)/2,yb=(b.h-2*Border-p.h*sb)/2;
        if(effect==1) { if(entering)xa+=slideDirection*(a.w-2*Border);else xb-=slideDirection*(b.w-2*Border); }
        if(animate) {
            p.scale->SetScaleX(curve(sa,sb).Get());p.scale->SetScaleY(curve(sa,sb).Get());
            p.visual->SetOffsetX(curve(xa,xb).Get());p.visual->SetOffsetY(curve(ya,yb).Get());
            p.effect->SetOpacity(curve(entering&&effect!=1?0.f:1.f,1.f).Get());
        }else {
            p.scale->SetScaleX(sb);p.scale->SetScaleY(sb);
            p.visual->SetOffsetX((b.w-2*Border-p.w*sb)/2);p.visual->SetOffsetY((b.h-2*Border-p.h*sb)/2);p.effect->SetOpacity(1.f);
        }
    }
    void setPictureStart(Picture& p,Bounds b,bool entering) {
        float scale=std::max((b.w-2*Border)/p.w,(b.h-2*Border)/p.h);
        if(fixedRatio!=0)scale=std::min((b.w-2*Border)/p.w,(b.h-2*Border)/p.h);
        if(effect==2&&entering)scale*=1.025f;
        float x=(b.w-2*Border-p.w*scale)/2,y=(b.h-2*Border-p.h*scale)/2;
        if(effect==1&&entering)x+=slideDirection*(b.w-2*Border);
        p.scale->SetScaleX(scale);p.scale->SetScaleY(scale);
        p.visual->SetOffsetX(x);p.visual->SetOffsetY(y);
        p.effect->SetOpacity(entering&&effect!=1?0.f:1.f);
    }
    void updateRegion(Bounds b) {
        int l=std::lround(b.x),t=std::lround(b.y),r=std::lround(b.x+b.w),bottom=std::lround(b.y+b.h);
        // 交互窗口只占相框所在区域，避免点击后被壁纸或桌宠误判为全屏程序。
        int width=std::max(1,r-l+1),height=std::max(1,bottom-t+1);
        SetWindowPos(inputHwnd,nullptr,area.left+l,area.top+t,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        HRGN region=CreateRoundRectRgn(0,0,width,height,int(cornerRadius*2),int(cornerRadius*2));
        if(!SetWindowRgn(inputHwnd,region,FALSE))DeleteObject(region);
    }
    Bounds fitted(float ratio) {
        if(fixedRatio==0){
            auto work=workArea(bounds);float aw=anchor%2?bounds.x+bounds.w-(work.left-area.left):(work.right-area.left)-bounds.x;
            float ah=anchor>=2?bounds.y+bounds.h-(work.top-area.top):(work.bottom-area.top)-bounds.y;
            if(limitAutoSize){aw=std::min(aw,std::max(2*Border+64,maxWidth));ah=std::min(ah,std::max(2*Border+64,maxHeight));}
            if(preferredPhotoArea<=0)preferredPhotoArea=std::max(1.f,(bounds.w-2*Border)*(bounds.h-2*Border));
            return fitPhotoBounds(bounds,ratio,preferredPhotoArea,Border,anchor,aw,ah);
        }
        if(fixedRatio<0)return bounds;
        if(fixedRatio>0)ratio=fixedRatio;
        Bounds b=bounds;float pixels=(bounds.w-2*Border)*(bounds.h-2*Border);
        b.w=std::sqrt(pixels*ratio)+2*Border;b.h=std::sqrt(pixels/ratio)+2*Border;
        auto work=workArea(bounds);float left=float(work.left-area.left),top=float(work.top-area.top),right=float(work.right-area.left),bottom=float(work.bottom-area.top);
        float availableW=anchor%2?bounds.x+bounds.w-left:right-bounds.x;
        float availableH=anchor>=2?bounds.y+bounds.h-top:bottom-bounds.y;
        float factor=std::min({1.f,(availableW-2*Border)/(b.w-2*Border),(availableH-2*Border)/(b.h-2*Border)});
        b.w=std::round((b.w-2*Border)*factor+2*Border);b.h=std::round((b.h-2*Border)*factor+2*Border);
        if(anchor%2)b.x=bounds.x+bounds.w-b.w;
        if(anchor>=2)b.y=bounds.y+bounds.h-b.h;
        return b;
    }
    RECT workArea(Bounds b) const {RECT screen{LONG(area.left+b.x),LONG(area.top+b.y),LONG(area.left+b.x+b.w),LONG(area.top+b.y+b.h)};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&screen,MONITOR_DEFAULTTONEAREST),&monitor);return monitor.rcWork;}
    void keepInWorkArea(){auto work=workArea(bounds);float width=float(work.right-work.left),height=float(work.bottom-work.top);float innerW=std::max(1.f,bounds.w-2*Border),innerH=std::max(1.f,bounds.h-2*Border);float factor=std::min({1.f,std::max(1.f,width-2*Border)/innerW,std::max(1.f,height-2*Border)/innerH});float w=innerW*factor+2*Border,h=innerH*factor+2*Border;if(anchor%2)bounds.x+=bounds.w-w;if(anchor>=2)bounds.y+=bounds.h-h;bounds.w=w;bounds.h=h;bounds.x=std::clamp(bounds.x,float(work.left-area.left),float(work.right-area.left)-w);bounds.y=std::clamp(bounds.y,float(work.top-area.top),float(work.bottom-area.top)-h);}
    size_t nextUsable(size_t after) {
        for(size_t n=1;n<=paths.size();++n){size_t candidate=(after+n)%paths.size();if(!failedIndices.contains(candidate))return candidate;}
        return paths.size();
    }
    void request(size_t i,bool preloadOnly=false,bool force=false,int direction=1,uint64_t tag=0) {
        if(preloadOnly&&(!visible||paused||suspended))return;
        if(i>=paths.size())return;
        if(failedIndices.contains(i))i=nextUsable(i);
        if(i>=paths.size())return;
        if(animating){if(!preloadOnly){pendingIndex=i;pendingDirection=direction;pendingTag=tag;queued=true;}return;}
        if(loading){if(preloadOnly)return;if(!prefetching&&pendingIndex==i){requestTag=tag;requestDirection=direction;return;}++version;loading=false;prefetching=false;}
        if(hasPhoto&&i==index&&!force){queued=false;next=seconds()+(benchmark?testInterval:playInterval);return;}
        if(!preloadOnly){requestDirection=direction;requestTag=tag;}
        if(!preloadOnly&&hasPrepared&&preparedIndex==i){
            incoming=std::move(prepared);hasPrepared=false;incomingIndex=i;startTransition();return;
        }
        if(preloadOnly&&hasPrepared&&preparedIndex==i)return;
        if(hasPrepared){discardPrepared();}
        loading=true;prefetching=preloadOnly;
        if(!preloadOnly){pendingIndex=i;queued=false;}
        {std::lock_guard lock(mutex);requestedPath=paths[i];requestedToken=++version;requestedIndex=i;requestedPixels=(bounds.w-2*Border)*(bounds.h-2*Border);requested=true;}
        wake.notify_one();
    }
    void decodeWorker() {
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        ComPtr<IWICImagingFactory> factory;
        HRESULT init=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
        for(;;) {
            fs::path path; uint64_t token;size_t item;float targetPixels;
            {std::unique_lock lock(mutex);wake.wait(lock,[this]{return stopping||requested;});if(stopping)break;
             path=requestedPath;token=requestedToken;item=requestedIndex;targetPixels=requestedPixels;requested=false;}
            auto result=std::make_unique<Pixels>();result->token=token;result->index=item;
#ifdef PHOTO_FRAME_CODECS
            auto extension=path.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
            if(extension==L".webp"||extension==L".avif"){
                auto decoded=loadImage(path,targetPixels);result->w=decoded.width;result->h=decoded.height;result->ratio=decoded.originalHeight?float(decoded.originalWidth)/decoded.originalHeight:1;result->error=decoded.error;result->bytes=std::move(decoded.pixels);
                {std::lock_guard lock(mutex);ready=std::move(result);}PostMessage(hwnd,DecodedMessage,0,0);continue;
            }
#endif
            try {
                check(init);ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
                check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
                check(decoder->GetFrame(0,&frame));UINT w,h;check(frame->GetSize(&w,&h));
                if(!w||!h)throw E_INVALIDARG;
                result->ratio=float(w)/h;
                double scale=std::min(1.,std::sqrt(double(targetPixels)/(double(w)*h)));
                result->w=std::max(1u,UINT(w*scale));result->h=std::max(1u,UINT(h*scale));
                ComPtr<IWICBitmapScaler> scaler;check(factory->CreateBitmapScaler(&scaler));
                check(scaler->Initialize(frame.Get(),result->w,result->h,WICBitmapInterpolationModeFant));
                ComPtr<IWICBitmapSource> source;check(scaler.As(&source));
                ComPtr<IWICMetadataQueryReader> metadata;PROPVARIANT orientation;PropVariantInit(&orientation);
                if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata))&&SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orientation))&&orientation.vt==VT_UI2){
                    const WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,WICBitmapTransformOptions(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,WICBitmapTransformOptions(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
                    if(orientation.uiVal>=2&&orientation.uiVal<=8){ComPtr<IWICBitmapFlipRotator> rotated;check(factory->CreateBitmapFlipRotator(&rotated));check(rotated->Initialize(source.Get(),transforms[orientation.uiVal]));check(rotated.As(&source));if(orientation.uiVal>=5){std::swap(result->w,result->h);result->ratio=1/result->ratio;}}
                }
                PropVariantClear(&orientation);
                ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter));
                check(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
                result->bytes.resize(size_t(result->w)*result->h*4);
                check(converter->CopyPixels(nullptr,result->w*4,UINT(result->bytes.size()),result->bytes.data()));
            } catch(HRESULT hr){result->error=hr;}catch(...){result->error=E_OUTOFMEMORY;}
            {std::lock_guard lock(mutex);ready=std::move(result);}
            PostMessage(hwnd,DecodedMessage,0,0);
        }
        factory.Reset();CoUninitialize();
    }
    void receive() {
        std::unique_ptr<Pixels> pixels;{std::lock_guard lock(mutex);pixels=std::move(ready);}
        if(!pixels||pixels->token!=version)return;
        loading=false;
        if(FAILED(pixels->error)){
            ++failed;failedIndices.insert(pixels->index);
            bool wasPrefetch=prefetching;prefetching=false;
            size_t replacement=queued?pendingIndex:nextUsable(pixels->index);queued=false;
            // 当前画面不变，跳过本轮已失败的来源，避免反复解码同一个坏文件。
            request(replacement,wasPrefetch&&hasPhoto,false,requestDirection,requestTag);next=seconds()+1;return;
        }
        if(prefetching){
            prepared=makePicture(*pixels);preparedIndex=pixels->index;prepared.effect->SetOpacity(0.f);
            check(well->AddVisual(prepared.visual.Get(),TRUE,current.visual.Get()));prepared.attached=true;
            commit();prepared.preparedAt=seconds();hasPrepared=true;prefetching=false;
            if(queued){queued=false;request(pendingIndex,false,false,pendingDirection,pendingTag);}return;
        }
        // 解码期间收到更新的选图请求时，不把旧结果上传并展示一遍。
        if(queued&&pendingIndex!=pixels->index){size_t latest=pendingIndex;queued=false;request(latest,false,false,pendingDirection,pendingTag);return;}
        incoming=makePicture(*pixels);incomingIndex=pixels->index;startTransition();
    }
    Picture makePicture(const Pixels& pixels) {
        double started=seconds();Picture picture;picture.w=pixels.w;picture.h=pixels.h;picture.ratio=pixels.ratio;picture.surface=surface(pixels.w,pixels.h,pixels.bytes.data());
        check(composition->CreateVisual(&picture.visual));picture.visual->SetContent(picture.surface.Get());
        check(composition->CreateScaleTransform(&picture.scale));picture.visual->SetTransform(picture.scale.Get());
        check(composition->CreateEffectGroup(&picture.effect));picture.visual->SetEffect(picture.effect.Get());picture.uploadMs=(seconds()-started)*1000;return picture;
    }
    void startTransition() {
        from=bounds;to=fitted(incoming.ratio);slideDirection=requestDirection;incomingTag=requestTag;
        transitionDuration=adaptiveDuration(from,to,fixedRatio==0);
        // 新照片先以动画起点提交；滑动效果的起点在照片裁剪区外，其他效果先保持透明。
        setPictureStart(incoming,from,true);
        if(!incoming.attached){check(well->AddVisual(incoming.visual.Get(),TRUE,current.visual.Get()));incoming.attached=true;}
        commit();
        BOOL clientAnimation=TRUE;SystemParametersInfo(SPI_GETCLIENTAREAANIMATION,0,&clientAnimation,0);
        animating=hasPhoto&&effect!=3&&clientAnimation&&!reduceMotion;
        // 让动画从合成器应用本次提交的第一帧开始，避免绝对时钟与显示帧错位。
        begin=seconds();
        if(events)events<<std::setprecision(17)<<begin*Frequency.QuadPart<<","<<(animating?transitionDuration*Frequency.QuadPart:0)<<","<<anchor<<","<<effect<<","<<from.x<<","<<from.y<<","<<from.w<<","<<from.h<<","<<to.x<<","<<to.y<<","<<to.w<<","<<to.h<<","<<incoming.uploadMs<<","<<(incoming.preparedAt>0?(begin-incoming.preparedAt)*1000:-1)<<"\n";
        applyGeometry(to,animating);
        if(animating){
            // 点击窗口仅覆盖新旧画面的交集，不裁剪绘制窗口，也不挡住外部桌面。
            Bounds overlap{std::max(from.x,to.x),std::max(from.y,to.y),0,0};
            overlap.w=std::min(from.x+from.w,to.x+to.w)-overlap.x;
            overlap.h=std::min(from.y+from.h,to.y+to.h)-overlap.y;updateRegion(overlap);
        }
        if(hasPhoto)pictureGeometry(current,from,to,animating,false);
        pictureGeometry(incoming,from,to,animating,true);
        commit();
        if(!animating)finish();
    }
    void finish() {
        if(current.visual)well->RemoveVisual(current.visual.Get());
        current=std::move(incoming);incoming={};hasPhoto=true;bounds=to;animating=false;index=incomingIndex;
        applyGeometry(bounds,false);pictureGeometry(current,bounds,bounds,false,true);
        commit();updateRegion(bounds);++transitions;
        presentedTag=incomingTag;if(presented)presented(index);
        next=seconds()+(benchmark?testInterval:playInterval);
        if(queued&&pendingIndex!=index){queued=false;request(pendingIndex,false,false,pendingDirection,pendingTag);}
        else {queued=false;if(!paused&&!suspended&&visible&&paths.size()>1)request(following(),true);}
    }
    void cancel() {
        ++version;loading=false;pendingIndex=index;queued=false;prefetching=false;discardPrepared();
        {std::lock_guard lock(mutex);requested=false;ready.reset();}
        if(animating){
            // 中断时冻结在当前几何位置，避免暂停或拖动让相框跳回动画起点。
            float progress=ease(float((seconds()-begin)/transitionDuration));
            bounds={from.x+(to.x-from.x)*progress,from.y+(to.y-from.y)*progress,from.w+(to.w-from.w)*progress,from.h+(to.h-from.h)*progress};
            well->RemoveVisual(incoming.visual.Get());incoming={};animating=false;
            applyGeometry(bounds,false);pictureGeometry(current,bounds,bounds,false,false);commit();updateRegion(bounds);}
    }
    void discardPrepared(){bool removed=hasPrepared&&prepared.attached&&prepared.visual;if(removed)well->RemoveVisual(prepared.visual.Get());prepared={};hasPrepared=false;if(removed&&composition)commit();}
    void sample() {
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        DWM_TIMING_INFO timing{};timing.cbSize=sizeof(timing);HRESULT hr=DwmGetCompositionTimingInfo(nullptr,&timing);
        Bounds hit=bounds;if(animating){hit.x=std::max(from.x,to.x);hit.y=std::max(from.y,to.y);hit.w=std::min(from.x+from.w,to.x+to.w)-hit.x;hit.h=std::min(from.y+from.h,to.y+to.h)-hit.y;}
        POINT inside{area.left+LONG(hit.x+hit.w/2),area.top+LONG(hit.y+hit.h/2)},outside{area.right-8,area.bottom-8};
        HWND insideWindow=WindowFromPoint(inside),outsideWindow=WindowFromPoint(outside);
        DXGI_QUERY_VIDEO_MEMORY_INFO local{},shared{};if(gpuAdapter){gpuAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local);gpuAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared);}
        log<<seconds()-runStart<<","<<memory.WorkingSetSize<<","<<memory.PrivateUsage<<","<<transitions<<","<<failed<<","<<hr<<","<<timing.cFrame<<","<<timing.cFramesDropped<<","<<timing.cFramesLate<<","<<IsWindowVisible(hwnd)<<","<<(insideWindow==inputHwnd)<<","<<(outsideWindow!=inputHwnd&&outsideWindow!=hwnd)<<","<<local.CurrentUsage<<","<<shared.CurrentUsage<<","<<paused<<","<<index<<","<<loading<<","<<animating<<"\n";log.flush();
    }
    void tick() {
        double now=seconds();
        if(animating) {
            float t=ease(float((now-begin)/transitionDuration));
            Bounds visual{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.w+(to.w-from.w)*t,from.h+(to.h-from.h)*t};
            // 可见边缘完全由合成器绘制，不能用滞后的原生区域去裁剪。
            (void)visual;
            if(now>=begin+transitionDuration+.025)finish();
        } else if(visible&&!paused&&!suspended&&!dragging&&!resizing&&!loading&&!paths.empty()&&now>=next) {
            if(benchmark){anchor=(transitions/12)%4;effect=(transitions/4)%3;}
            request(following());
        }
        if(benchmark) {
            static double lastSample=0;if(now-lastSample>=1){sample();lastSample=now;}
            if(runSeconds>0&&now-runStart>=runSeconds)DestroyWindow(hwnd);
        }
    }
    void add() {
        ComPtr<IFileOpenDialog> dialog;check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
        DWORD flags;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_ALLOWMULTISELECT|FOS_FILEMUSTEXIST);
        COMDLG_FILTERSPEC filter{L"照片",L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.avif"};dialog->SetFileTypes(1,&filter);
        if(SUCCEEDED(dialog->Show(hwnd))){ComPtr<IShellItemArray> items;dialog->GetResults(&items);DWORD n;items->GetCount(&n);
            for(DWORD i=0;i<n;++i){ComPtr<IShellItem> item;items->GetItemAt(i,&item);PWSTR name=nullptr;item->GetDisplayName(SIGDN_FILESYSPATH,&name);if(name){paths.emplace_back(name);CoTaskMemFree(name);}}
            if(!hasPhoto)request(0);}
    }
    // 四条边都可缩放，先限定在相框内，避免透明区域误显示缩放光标。
    int resizeHit(int x,int y) const {
        if(x<bounds.x||x>bounds.x+bounds.w||y<bounds.y||y>bounds.y+bounds.h)return 0;
        int handle=std::min(32,int(std::min(bounds.w,bounds.h)/3));
        int axes=0;
        bool left=x<=bounds.x+handle,right=x>=bounds.x+bounds.w-handle;
        bool top=y<=bounds.y+handle,bottom=y>=bounds.y+bounds.h-handle;
        if(left&&right)axes|=x-bounds.x<bounds.x+bounds.w-x?ResizeLeft:ResizeRight;
        else if(left)axes|=ResizeLeft;else if(right)axes|=ResizeRight;
        if(top&&bottom)axes|=y-bounds.y<bounds.y+bounds.h-y?ResizeTop:ResizeBottom;
        else if(top)axes|=ResizeTop;else if(bottom)axes|=ResizeBottom;
        return axes;
    }
    void mouseDown(int x,int y,bool allowMove=true){
        cancel();mouseStart={x,y};interactionStart=bounds;resizeAxes=resizeHit(x,y);
        resizing=resizeAxes!=0;dragging=!resizing&&allowMove;
        if(dragging||resizing)SetCapture(inputHwnd);
    }
    void mouseMove(int x,int y){
        if(!dragging&&!resizing)return;
        if(dragging){bounds.x=interactionStart.x+x-mouseStart.x;bounds.y=interactionStart.y+y-mouseStart.y;}
        else {
            auto start=interactionStart;
            auto work=workArea(start);
            float workLeft=float(work.left-area.left),workTop=float(work.top-area.top);
            float workRight=float(work.right-area.left),workBottom=float(work.bottom-area.top);
            float startRight=start.x+start.w,startBottom=start.y+start.h;
            float dx=float(x-mouseStart.x),dy=float(y-mouseStart.y);
            bool left=resizeAxes&ResizeLeft,right=resizeAxes&ResizeRight;
            bool top=resizeAxes&ResizeTop,bottom=resizeAxes&ResizeBottom;
            if(fixedRatio<0){
                float newLeft=start.x,newRight=startRight,newTop=start.y,newBottom=startBottom;
                float minW=std::min(2*Border+64.f,workRight-workLeft);
                float minH=std::min(2*Border+64.f,workBottom-workTop);
                if(left)newLeft=std::clamp(start.x+dx,workLeft,startRight-minW);
                if(right)newRight=std::clamp(startRight+dx,start.x+minW,workRight);
                if(top)newTop=std::clamp(start.y+dy,workTop,startBottom-minH);
                if(bottom)newBottom=std::clamp(startBottom+dy,start.y+minH,workBottom);
                bounds={newLeft,newTop,newRight-newLeft,newBottom-newTop};
            } else {
                float ratio=(start.w-2*Border)/(start.h-2*Border);
                float horizontal=left?-dx:(right?dx:0.f);
                float vertical=top?-dy*ratio:(bottom?dy*ratio:0.f);
                float widthDelta=left||right?horizontal:vertical;
                // 将角落拖动投影到等比例对角线，避免横纵方向相反时切换主轴导致尺寸跳变。
                if((left||right)&&(top||bottom))widthDelta=(horizontal*ratio*ratio+vertical)/(ratio*ratio+1);
                // 保存拖动意图的面积，工作区和用户上限只压缩本次显示尺寸。
                if(fixedRatio==0){float intendedWidth=std::clamp(start.w+widthDelta-2*Border,64.f,8192.f);preferredPhotoArea=intendedWidth*intendedWidth/std::max(.0001f,ratio);}
                float maxW=left?startRight-workLeft:(right?workRight-start.x:workRight-workLeft);
                float maxH=top?startBottom-workTop:(bottom?workBottom-start.y:workBottom-workTop);
                maxW=std::max(2*Border+1.f,std::min(maxW,(maxH-2*Border)*ratio+2*Border));
                float minW=std::min(2*Border+64.f,maxW);
                bounds.w=std::clamp(start.w+widthDelta,minW,maxW);
                bounds.h=(bounds.w-2*Border)/ratio+2*Border;
                // 只拖垂直边时允许相框横向挪动，贴着屏幕右侧也能放大。
                bounds.x=left?startRight-bounds.w:(right?start.x:std::clamp(start.x+(start.w-bounds.w)/2,workLeft,workRight-bounds.w));
                bounds.y=top?startBottom-bounds.h:(bottom?start.y:std::clamp(start.y+(start.h-bounds.h)/2,workTop,workBottom-bounds.h));
            }
        }
        if(resizing&&fixedRatio==0&&limitAutoSize){float factor=std::min({1.f,(std::max(2*Border+64,maxWidth)-2*Border)/(bounds.w-2*Border),(std::max(2*Border+64,maxHeight)-2*Border)/(bounds.h-2*Border)});float w=(bounds.w-2*Border)*factor+2*Border,h=(bounds.h-2*Border)*factor+2*Border;if(resizeAxes&ResizeLeft)bounds.x+=bounds.w-w;if(resizeAxes&ResizeTop)bounds.y+=bounds.h-h;bounds.w=w;bounds.h=h;}
        if(resizing&&fixedRatio!=0)preferredPhotoArea=std::max(1.f,(bounds.w-2*Border)*(bounds.h-2*Border));
        applyGeometry(bounds,false);if(hasPhoto)pictureGeometry(current,bounds,bounds,false,true);commit();updateRegion(bounds);
    }
    void mouseUp(){if(!dragging&&!resizing)return;bool refresh=resizing&&hasPhoto&&(bounds.w-2*Border)*(bounds.h-2*Border)>float(current.w)*current.h*1.5f;dragging=false;resizing=false;resizeAxes=0;ReleaseCapture();next=seconds()+playInterval;if(refresh)request(index,false,true);else if(hasPhoto&&!paused&&!suspended&&paths.size()>1)request(following(),true);}
    void menu() {
        HMENU menu=CreatePopupMenu();AppendMenu(menu,MF_STRING,1,L"添加照片…");AppendMenu(menu,MF_STRING,2,paused?L"继续播放":L"暂停");AppendMenu(menu,MF_STRING,3,L"下一张");
        const wchar_t* effects[]={L"淡入淡出",L"水平滑动",L"轻微缩放",L"无动画"};
        for(int i=0;i<4;++i)AppendMenu(menu,MF_STRING|(effect==i?MF_CHECKED:0),10+i,effects[i]);
        const wchar_t* anchors[]={L"固定左上角",L"固定右上角",L"固定左下角",L"固定右下角"};
        for(int i=0;i<4;++i)AppendMenu(menu,MF_STRING|(anchor==i?MF_CHECKED:0),20+i,anchors[i]);
        AppendMenu(menu,MF_STRING,99,L"退出性能样机");POINT p;GetCursorPos(&p);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);
        if(id==1)add();if(id==2){paused=!paused;if(paused)cancel();else next=seconds()+playInterval;}if(id==3&&!paths.empty())request(following());
        if(id>=10&&id<14){cancel();effect=id-10;}if(id>=20&&id<24){cancel();anchor=id-20;}if(id==99)DestroyWindow(hwnd);
    }
};
