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
struct Pixels { UINT w=0,h=0; float ratio=1; uint64_t token=0; size_t index=0; std::vector<BYTE> bytes; HRESULT error=S_OK; };
struct Picture {
    ComPtr<IDCompositionSurface> surface;
    ComPtr<IDCompositionVisual2> visual;
    ComPtr<IDCompositionScaleTransform> scale;
    ComPtr<IDCompositionEffectGroup> effect;
    UINT w=1,h=1;
};

class Frame {
public:
    HWND hwnd=nullptr,inputHwnd=nullptr;
    RECT area{};
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGIAdapter3> gpuAdapter;
    ComPtr<ID2D1Device> d2d;
    ComPtr<IDCompositionDesktopDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual2> root,mat,well;
    ComPtr<IDCompositionRectangleClip> outerClip,innerClip;
    ComPtr<IDCompositionScaleTransform> matScale;
    ComPtr<IDCompositionSurface> matSurface;
    Picture current,incoming;
    Bounds bounds,from,to;
    std::vector<fs::path> paths;
    std::unordered_set<size_t> failedIndices;
    size_t index=0, pendingIndex=0, incomingIndex=0;
    int anchor=3,effect=0;
    bool paused=false,visible=true,animating=false,hasPhoto=false,benchmark=false,loading=false,prefetching=false,queued=false;
    bool dragging=false,resizing=false;
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
    std::unique_ptr<Pixels> ready,cached;

    ~Frame() {
        { std::lock_guard lock(mutex); stopping=true; }
        wake.notify_all(); if(worker.joinable())worker.join();
    }
    void initialize() {
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,&level,&context));
        ComPtr<IDXGIDevice> dxgi; check(d3d.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter;if(SUCCEEDED(dxgi->GetAdapter(&adapter)))adapter.As(&gpuAdapter);
        ComPtr<ID2D1Factory1> factory;
        check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),nullptr,reinterpret_cast<void**>(factory.GetAddressOf())));
        check(factory->CreateDevice(dxgi.Get(),&d2d));
        check(DCompositionCreateDevice2(d2d.Get(),__uuidof(IDCompositionDesktopDevice),reinterpret_cast<void**>(composition.GetAddressOf())));
        check(composition->CreateTargetForHwnd(hwnd,TRUE,&target));
        check(composition->CreateVisual(&root)); check(target->SetRoot(root.Get()));
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
        check(composition->Commit());
        updateRegion(bounds);
        worker=std::thread([this]{decodeWorker();});
        next=seconds()+10;
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
        LARGE_INTEGER absolute;absolute.QuadPart=static_cast<LONGLONG>(begin*Frequency.QuadPart);
        value->SetAbsoluteBeginTime(absolute);
        value->AddCubic(0,a,0,float(3*(b-a)/(Duration*Duration)),float(-2*(b-a)/(Duration*Duration*Duration)));
        value->End(Duration,b);return value;
    }
    template<typename Object,typename Setter> void property(Object* object,Setter setter,float a,float b,bool animate) {
        // 由调用方使用明确的重载，保证全部属性共享同一个绝对起始时间。
        (void)object;(void)setter;(void)a;(void)b;(void)animate;
    }
    void applyGeometry(Bounds end,bool animate) {
        if(animate) {
            root->SetOffsetX(curve(from.x,end.x).Get());root->SetOffsetY(curve(from.y,end.y).Get());
            outerClip->SetRight(curve(from.w,end.w).Get());outerClip->SetBottom(curve(from.h,end.h).Get());
            matScale->SetScaleX(curve(from.w,end.w).Get());matScale->SetScaleY(curve(from.h,end.h).Get());
            innerClip->SetRight(curve(from.w-2*Border,end.w-2*Border).Get());innerClip->SetBottom(curve(from.h-2*Border,end.h-2*Border).Get());
        } else {
            root->SetOffsetX(end.x);root->SetOffsetY(end.y);outerClip->SetRight(end.w);outerClip->SetBottom(end.h);
            matScale->SetScaleX(end.w);matScale->SetScaleY(end.h);innerClip->SetRight(end.w-2*Border);innerClip->SetBottom(end.h-2*Border);
        }
    }
    void pictureGeometry(Picture& p,Bounds a,Bounds b,bool animate,bool entering) {
        float sa=std::max((a.w-2*Border)/p.w,(a.h-2*Border)/p.h);
        float sb=std::max((b.w-2*Border)/p.w,(b.h-2*Border)/p.h);
        if(effect==2&&entering)sa*=1.025f;
        float xa=(a.w-2*Border-p.w*sa)/2,xb=(b.w-2*Border-p.w*sb)/2;
        float ya=(a.h-2*Border-p.h*sa)/2,yb=(b.h-2*Border-p.h*sb)/2;
        if(effect==1) { if(entering)xa+=a.w-2*Border;else xb-=b.w-2*Border; }
        if(animate) {
            p.scale->SetScaleX(curve(sa,sb).Get());p.scale->SetScaleY(curve(sa,sb).Get());
            p.visual->SetOffsetX(curve(xa,xb).Get());p.visual->SetOffsetY(curve(ya,yb).Get());
            p.effect->SetOpacity(curve(entering&&effect!=1?0.f:1.f,1.f).Get());
        }else {
            p.scale->SetScaleX(sb);p.scale->SetScaleY(sb);
            p.visual->SetOffsetX((b.w-2*Border-p.w*sb)/2);p.visual->SetOffsetY((b.h-2*Border-p.h*sb)/2);p.effect->SetOpacity(1.f);
        }
    }
    void updateRegion(Bounds b) {
        int l=std::lround(b.x),t=std::lround(b.y),r=std::lround(b.x+b.w),bottom=std::lround(b.y+b.h);
        HRGN region=CreateRoundRectRgn(l,t,r+1,bottom+1,20,20);
        if(!SetWindowRgn(inputHwnd,region,FALSE))DeleteObject(region);
    }
    Bounds fitted(float ratio) {
        Bounds b=bounds;float pixels=(bounds.w-2*Border)*(bounds.h-2*Border);
        b.w=std::sqrt(pixels*ratio)+2*Border;b.h=std::sqrt(pixels/ratio)+2*Border;
        float availableW=anchor%2?bounds.x+bounds.w:float(area.right-area.left)-bounds.x;
        float availableH=anchor>=2?bounds.y+bounds.h:float(area.bottom-area.top)-bounds.y;
        float factor=std::min({1.f,(availableW-2*Border)/(b.w-2*Border),(availableH-2*Border)/(b.h-2*Border)});
        b.w=std::round((b.w-2*Border)*factor+2*Border);b.h=std::round((b.h-2*Border)*factor+2*Border);
        if(anchor%2)b.x=bounds.x+bounds.w-b.w;
        if(anchor>=2)b.y=bounds.y+bounds.h-b.h;
        return b;
    }
    size_t nextUsable(size_t after) {
        for(size_t n=1;n<=paths.size();++n){size_t candidate=(after+n)%paths.size();if(!failedIndices.contains(candidate))return candidate;}
        return paths.size();
    }
    void request(size_t i,bool preloadOnly=false) {
        if(i>=paths.size())return;
        if(failedIndices.contains(i))i=nextUsable(i);
        if(i>=paths.size())return;
        if(hasPhoto&&i==index&&!animating){next=seconds()+(benchmark?testInterval:10);return;}
        if(animating||loading){if(!preloadOnly){pendingIndex=i;queued=true;}return;}
        if(!preloadOnly&&cached&&cached->index==i){
            {std::lock_guard lock(mutex);ready=std::move(cached);}
            prefetching=false;receive();return;
        }
        if(preloadOnly&&cached&&cached->index==i)return;
        cached.reset();loading=true;prefetching=preloadOnly;
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
                    const WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,WICBitmapTransformOptions(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,WICBitmapTransformOptions(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
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
            request(replacement,wasPrefetch&&hasPhoto);next=seconds()+1;return;
        }
        if(prefetching){cached=std::move(pixels);prefetching=false;if(queued){queued=false;request(pendingIndex);}return;}
        // 解码期间收到更新的选图请求时，不把旧结果上传并展示一遍。
        if(queued&&pendingIndex!=pixels->index){size_t latest=pendingIndex;queued=false;request(latest);return;}
        incoming={};incoming.w=pixels->w;incoming.h=pixels->h;incoming.surface=surface(pixels->w,pixels->h,pixels->bytes.data());
        check(composition->CreateVisual(&incoming.visual));incoming.visual->SetContent(incoming.surface.Get());
        check(composition->CreateScaleTransform(&incoming.scale));incoming.visual->SetTransform(incoming.scale.Get());
        check(composition->CreateEffectGroup(&incoming.effect));incoming.visual->SetEffect(incoming.effect.Get());
        check(well->AddVisual(incoming.visual.Get(),TRUE,current.visual.Get()));
        from=bounds;to=fitted(pixels->ratio);incomingIndex=pixels->index;
        BOOL clientAnimation=TRUE;SystemParametersInfo(SPI_GETCLIENTAREAANIMATION,0,&clientAnimation,0);
        animating=hasPhoto&&effect!=3&&clientAnimation;
        // 提交同一批动画，给合成器预留两帧，不在开始时重新解码照片。
        begin=seconds()+.035;
        if(events)events<<std::setprecision(17)<<begin*Frequency.QuadPart<<","<<(animating?Duration*Frequency.QuadPart:0)<<","<<anchor<<","<<effect<<","<<from.x<<","<<from.y<<","<<from.w<<","<<from.h<<","<<to.x<<","<<to.y<<","<<to.w<<","<<to.h<<"\n";
        applyGeometry(to,animating);
        if(animating){
            // 点击窗口仅覆盖新旧画面的交集，不裁剪绘制窗口，也不挡住外部桌面。
            Bounds overlap{std::max(from.x,to.x),std::max(from.y,to.y),0,0};
            overlap.w=std::min(from.x+from.w,to.x+to.w)-overlap.x;
            overlap.h=std::min(from.y+from.h,to.y+to.h)-overlap.y;updateRegion(overlap);
        }
        if(hasPhoto)pictureGeometry(current,from,to,animating,false);
        pictureGeometry(incoming,from,to,animating,true);
        check(composition->Commit());
        if(!animating)finish();
    }
    void finish() {
        if(current.visual)well->RemoveVisual(current.visual.Get());
        current=std::move(incoming);incoming={};hasPhoto=true;bounds=to;animating=false;index=incomingIndex;
        applyGeometry(bounds,false);pictureGeometry(current,bounds,bounds,false,true);
        composition->Commit();updateRegion(bounds);++transitions;
        next=seconds()+(benchmark?testInterval:10);
        if(queued&&pendingIndex!=index){queued=false;request(pendingIndex);}
        else {queued=false;if(!paused&&visible&&paths.size()>1)request((index+1)%paths.size(),true);}
    }
    void cancel() {
        ++version;loading=false;pendingIndex=index;queued=false;prefetching=false;cached.reset();
        if(animating){
            // 中断时冻结在当前几何位置，避免暂停或拖动让相框跳回动画起点。
            float progress=ease(float((seconds()-begin)/Duration));
            bounds={from.x+(to.x-from.x)*progress,from.y+(to.y-from.y)*progress,from.w+(to.w-from.w)*progress,from.h+(to.h-from.h)*progress};
            well->RemoveVisual(incoming.visual.Get());incoming={};animating=false;
            applyGeometry(bounds,false);pictureGeometry(current,bounds,bounds,false,false);composition->Commit();updateRegion(bounds);}
    }
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
            float t=ease(float((now-begin)/Duration));
            Bounds visual{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.w+(to.w-from.w)*t,from.h+(to.h-from.h)*t};
            // 可见边缘完全由合成器绘制，不能用滞后的原生区域去裁剪。
            (void)visual;
            if(now>=begin+Duration+.025)finish();
        } else if(visible&&!paused&&!dragging&&!resizing&&!loading&&!paths.empty()&&now>=next) {
            if(benchmark){anchor=(transitions/12)%4;effect=(transitions/4)%3;}
            request((index+1)%paths.size());
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
    void mouseDown(int x,int y){
        cancel();mouseStart={x,y};interactionStart=bounds;
        resizing=x>bounds.x+bounds.w-16&&y>bounds.y+bounds.h-16;dragging=!resizing;SetCapture(inputHwnd);
    }
    void mouseMove(int x,int y){
        if(!dragging&&!resizing)return;
        if(dragging){bounds.x=std::clamp(interactionStart.x+x-mouseStart.x,0.f,float(area.right-area.left)-bounds.w);bounds.y=std::clamp(interactionStart.y+y-mouseStart.y,0.f,float(area.bottom-area.top)-bounds.h);}
        else {
            float ratio=(interactionStart.w-2*Border)/(interactionStart.h-2*Border);
            float maxWidth=std::min(float(area.right-area.left)-bounds.x,(float(area.bottom-area.top)-bounds.y-2*Border)*ratio+2*Border);
            bounds.w=std::clamp(interactionStart.w+x-mouseStart.x,std::min(240.f,maxWidth),maxWidth);bounds.h=(bounds.w-2*Border)/ratio+2*Border;
        }
        applyGeometry(bounds,false);if(hasPhoto)pictureGeometry(current,bounds,bounds,false,true);composition->Commit();updateRegion(bounds);
    }
    void mouseUp(){if(!dragging&&!resizing)return;dragging=false;resizing=false;ReleaseCapture();next=seconds()+10;if(hasPhoto&&!paused&&paths.size()>1)request((index+1)%paths.size(),true);}
    void menu() {
        HMENU menu=CreatePopupMenu();AppendMenu(menu,MF_STRING,1,L"添加照片…");AppendMenu(menu,MF_STRING,2,paused?L"继续播放":L"暂停");AppendMenu(menu,MF_STRING,3,L"下一张");
        const wchar_t* effects[]={L"淡入淡出",L"水平滑动",L"轻微缩放",L"无动画"};
        for(int i=0;i<4;++i)AppendMenu(menu,MF_STRING|(effect==i?MF_CHECKED:0),10+i,effects[i]);
        const wchar_t* anchors[]={L"固定左上角",L"固定右上角",L"固定左下角",L"固定右下角"};
        for(int i=0;i<4;++i)AppendMenu(menu,MF_STRING|(anchor==i?MF_CHECKED:0),20+i,anchors[i]);
        AppendMenu(menu,MF_STRING,99,L"退出性能样机");POINT p;GetCursorPos(&p);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);
        if(id==1)add();if(id==2){paused=!paused;if(paused)cancel();else next=seconds()+10;}if(id==3&&!paths.empty())request((index+1)%paths.size());
        if(id>=10&&id<14){cancel();effect=id-10;}if(id>=20&&id<24){cancel();anchor=id-20;}if(id==99)DestroyWindow(hwnd);
    }
};
static Frame* instance=nullptr;
static fs::path errorOutput;
static bool automated=false;
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    try {
        if(message==DecodedMessage){instance->receive();return 0;}
        if(message==WM_TIMER){instance->tick();return 0;}
        if(message==WM_RBUTTONUP){instance->menu();return 0;}
        if(message==WM_LBUTTONDOWN){instance->mouseDown(GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;}
        if(message==WM_MOUSEMOVE){instance->mouseMove(GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;}
        if(message==WM_LBUTTONUP){instance->mouseUp();return 0;}
        if(message==WM_KEYDOWN){if(w==VK_ESCAPE)DestroyWindow(hwnd);if(w==VK_RIGHT&&!instance->paths.empty())instance->request((instance->index+1)%instance->paths.size());if(w==VK_SPACE){instance->paused=!instance->paused;if(instance->paused)instance->cancel();}return 0;}
        if(message==WM_DESTROY){if(hwnd==instance->hwnd){KillTimer(hwnd,TimerId);DestroyWindow(instance->inputHwnd);PostQuitMessage(0);}return 0;}
        if(message==WM_PAINT&&hwnd==instance->inputHwnd){PAINTSTRUCT paint;HDC dc=BeginPaint(hwnd,&paint);FillRect(dc,&paint.rcPaint,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));EndPaint(hwnd,&paint);return 0;}
        if(message==WM_SHOWWINDOW&&hwnd==instance->hwnd){instance->visible=w!=0;if(!w){instance->cancel();ShowWindow(instance->inputHwnd,SW_HIDE);}else if(instance->inputHwnd)ShowWindow(instance->inputHwnd,SW_SHOWNA);}
        if(message==WM_ERASEBKGND)return 1;
    }catch(HRESULT hr){if(automated){std::ofstream(errorOutput)<<"line "<<errorLine<<" HRESULT "<<std::hex<<hr;DestroyWindow(hwnd);return 0;}wchar_t text[80];swprintf(text,80,L"原生样机错误：0x%08lX",static_cast<unsigned long>(hr));MessageBox(hwnd,text,L"性能样机",MB_ICONERROR);DestroyWindow(hwnd);return 0;}
    return DefWindowProc(hwnd,message,w,l);
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE,PWSTR,int) {
    QueryPerformanceFrequency(&Frequency);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        Frame frame;instance=&frame;int argc;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);fs::path output;
        for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--benchmark")frame.benchmark=true;else if(arg==L"--seconds"&&i+1<argc)frame.runSeconds=_wtof(argv[++i]);else if(arg==L"--scale"&&i+1<argc){float scale=std::clamp(float(_wtof(argv[++i])),1.f,1.5f);Border=18*scale;frame.bounds.x*=scale;frame.bounds.y*=scale;frame.bounds.w*=scale;frame.bounds.h*=scale;}else if(arg==L"--interval"&&i+1<argc)frame.testInterval=_wtof(argv[++i]);else if(arg==L"--output"&&i+1<argc)output=argv[++i];else frame.paths.emplace_back(arg);}LocalFree(argv);
        automated=frame.benchmark;errorOutput=output;errorOutput+=L".error";
        if(!output.empty()){frame.log.open(output);frame.log<<"seconds,working_set_bytes,private_commit_bytes,transitions,decode_errors,dwm_result,dwm_frame,dwm_dropped,dwm_late,visible,inside_hit,outside_pass,gpu_local_bytes,gpu_nonlocal_bytes,paused,photo_index,loading,animating\n";}
        if(!output.empty()){auto eventFile=output;eventFile+=L".events.csv";frame.events.open(eventFile);frame.events<<"qpc_begin,qpc_duration,anchor,effect,x0,y0,w0,h0,x1,y1,w1,h1\n";}
        WNDCLASS wc{};wc.lpfnWndProc=procedure;wc.hInstance=module;wc.lpszClassName=L"PhotoFrameNativePrototype";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClass(&wc);
        MONITORINFO info{sizeof(info)};GetMonitorInfo(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&info);frame.area=info.rcWork;
        frame.hwnd=CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT,wc.lpszClassName,L"桌面相框 2.0 性能样机",WS_POPUP,frame.area.left,frame.area.top,frame.area.right-frame.area.left,frame.area.bottom-frame.area.top,nullptr,nullptr,module,nullptr);
        if(!frame.hwnd)throw HRESULT_FROM_WIN32(GetLastError());
        SetLayeredWindowAttributes(frame.hwnd,0,255,LWA_ALPHA);
        frame.inputHwnd=CreateWindowEx(WS_EX_LAYERED|WS_EX_TOOLWINDOW,wc.lpszClassName,L"相框交互区域",WS_POPUP,frame.area.left,frame.area.top,frame.area.right-frame.area.left,frame.area.bottom-frame.area.top,nullptr,nullptr,module,nullptr);
        if(!frame.inputHwnd)throw HRESULT_FROM_WIN32(GetLastError());SetLayeredWindowAttributes(frame.inputHwnd,0,1,LWA_ALPHA);
        frame.initialize();ShowWindow(frame.hwnd,SW_SHOWNORMAL);ShowWindow(frame.hwnd,SW_SHOW);
        ShowWindow(frame.inputHwnd,SW_SHOWNOACTIVATE);ShowWindow(frame.inputHwnd,SW_SHOWNA);
        if(frame.benchmark){SetWindowPos(frame.inputHwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowPos(frame.hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
        frame.runStart=seconds();SetTimer(frame.hwnd,TimerId,8,nullptr);
        if(!frame.paths.empty())frame.request(0);
        MSG msg;while(GetMessage(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}
    }catch(HRESULT hr){result=1;if(automated)std::ofstream(errorOutput)<<"line "<<errorLine<<" HRESULT "<<std::hex<<hr;else {wchar_t text[80];swprintf(text,80,L"初始化失败：0x%08lX",static_cast<unsigned long>(hr));MessageBox(nullptr,text,L"原生性能样机",MB_ICONERROR);}}
    CoUninitialize();return result;
}
