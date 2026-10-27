#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "wgc-source.h"
#include "video-writer.h"
using Microsoft::WRL::ComPtr;namespace fs=std::filesystem;
static HWND observed=nullptr;static unsigned hides=0,shows=0,stableHides=0;
// 窗口变矮时底部参数可以正常收起；始终可见的标题、导航、照片区不能被反复隐藏。
static void CALLBACK visibility(HWINEVENTHOOK,DWORD event,HWND h,LONG object,LONG,DWORD,DWORD){if(object==OBJID_WINDOW&&h!=observed&&GetAncestor(h,GA_ROOT)==observed){if(event==EVENT_OBJECT_HIDE){++hides;int id=GetDlgCtrlID(h);int stable[]={705,700,400,401,402,403,100,102,201,462};if(std::find(std::begin(stable),std::end(stable),id)!=std::end(stable))++stableHides;}if(event==EVENT_OBJECT_SHOW)++shows;}}
static void verify(HRESULT hr){if(FAILED(hr))throw hr;}
static void bmp(const fs::path& file,const BYTE* pixels,unsigned pitch,int w,int h){BITMAPFILEHEADER f{};f.bfType=0x4d42;f.bfOffBits=54;f.bfSize=54+w*h*4;BITMAPINFOHEADER b{};b.biSize=40;b.biWidth=w;b.biHeight=-h;b.biPlanes=1;b.biBitCount=32;std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<char*>(&f),14);out.write(reinterpret_cast<char*>(&b),40);for(int y=0;y<h;++y)out.write(reinterpret_cast<const char*>(pixels+y*pitch),w*4);}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);RoInitialize(RO_INIT_MULTITHREADED);int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<4)return 2;observed=reinterpret_cast<HWND>(wcstoull(args[1],nullptr,10));fs::path out=args[2];bool viewer=std::wstring(args[3])==L"viewer",recording=false,simulate=false;for(int i=4;i<argc;++i){std::wstring arg=args[i];if(arg==L"--video")recording=true;else if(arg==L"--resize-lifecycle")simulate=true;}LocalFree(args);fs::create_directories(out);
 HWINEVENTHOOK hook=nullptr;bool entered=false,exited=false;
 try{
  SetWindowPos(observed,HWND_TOP,60,60,1280,800,SWP_NOACTIVATE);Sleep(300);POINT origin{};ClientToScreen(observed,&origin);RECT frame{};verify(DwmGetWindowAttribute(observed,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame)));int ox=origin.x-frame.left,oy=origin.y-frame.top;RECT client{};GetClientRect(observed,&client);int rightInset=frame.right-origin.x-client.right,bottomInset=frame.bottom-origin.y-client.bottom;
  DWORD pid=0;GetWindowThreadProcessId(observed,&pid);hook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_HIDE,nullptr,visibility,pid,0,WINEVENT_OUTOFCONTEXT);
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;verify(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));ComPtr<IDXGIDevice> dxgi;verify(device.As(&dxgi));WgcSource capture;verify(capture.start(dxgi.Get(),observed));
  ComPtr<ID3D11Texture2D> staging;std::ofstream csv(out/L"frames.csv");csv<<"time100ns,width,height,photoPixels,foregroundPixels,photoLost,foregroundLost,previewPixels,previewLost\n";unsigned frames=0,badPhoto=0,badText=0,badPreview=0,saved=0,resizes=0;int expectedPhoto=0,expectedText=0,expectedPreview=0,pendingWidth=0,pendingHeight=0;LONGLONG first=-1,last=-1;double lastResize=0;LARGE_INTEGER frequency{},start{},now{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);VideoWriter video;if(recording)video.start((out/L"resize.mp4").c_str(),1280,832);
  for(;;){QueryPerformanceCounter(&now);double elapsed=double(now.QuadPart-start.QuadPart)/frequency.QuadPart;if(elapsed>=7)break;
   // 仅模拟系统进入和退出拖拽消息，不移动真实鼠标；确保最后一项尺寸生效后退出。
   if(simulate&&!viewer&&!entered&&elapsed>=1){DWORD_PTR result=0;if(!SendMessageTimeoutW(observed,WM_ENTERSIZEMOVE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result))throw HRESULT_FROM_WIN32(ERROR_TIMEOUT);entered=true;}
   if(simulate&&!viewer&&entered&&!exited&&elapsed>=6){RECT current{};DWORD_PTR result=0;if(GetWindowRect(observed,&current)&&current.right-current.left==pendingWidth&&current.bottom-current.top==pendingHeight&&SendMessageTimeoutW(observed,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&result)){if(!SendMessageTimeoutW(observed,WM_EXITSIZEMOVE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result))throw HRESULT_FROM_WIN32(ERROR_TIMEOUT);exited=true;}}
   // 只改变被测窗口的尺寸；采集以系统合成帧为准，不把缩放调用次数当帧率。
   RECT actual{};GetWindowRect(observed,&actual);if(elapsed>=1&&elapsed<6&&elapsed-lastResize>=1./30&&(!pendingWidth||(actual.right-actual.left==pendingWidth&&actual.bottom-actual.top==pendingHeight))){int n=int((elapsed-1)*30),phase=n%80;float t=phase<40?phase/40.f:(80-phase)/40.f;pendingWidth=1280-int(240*t);pendingHeight=800-int(150*t);SetWindowPos(observed,nullptr,0,0,pendingWidth,pendingHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);lastResize=elapsed;++resizes;}
   MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
   ComPtr<ID3D11Texture2D> source;LONGLONG stamp=0;ABI::Windows::Graphics::SizeInt32 size{};HRESULT hr=capture.next(&source,&stamp,&size);if(hr==S_FALSE){capture.wait();continue;}verify(hr);if(stamp==last)continue;last=stamp;if(first<0)first=stamp;D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);if(!staging){auto copy=desc;copy.BindFlags=0;copy.MiscFlags=0;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;copy.Usage=D3D11_USAGE_STAGING;verify(device->CreateTexture2D(&copy,nullptr,&staging));}context->CopyResource(staging.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE map{};verify(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
   int w=std::min(int(desc.Width),size.Width),h=std::min(int(desc.Height),size.Height);auto pixels=static_cast<const BYTE*>(map.pData);auto count=[&](RECT r,bool photo){int n=0;for(int y=std::max(0L,r.top);y<std::min(LONG(h),r.bottom);++y)for(int x=std::max(0L,r.left);x<std::min(LONG(w),r.right);++x){auto p=pixels+y*map.RowPitch+x*4;if(photo?(abs(int(p[2])-20)<8&&abs(int(p[1])-190)<8&&abs(int(p[0])-155)<8):(int(p[0])+p[1]+p[2]>450))++n;}return n;};
   int cx=(ox+w-rightInset)/2,cy=oy+44+(h-oy-bottomInset-44)/2;RECT photo=viewer?RECT{cx-40,cy-30,cx+40,cy+30}:RECT{ox+312,oy+206,ox+332,oy+226};RECT text=viewer?RECT{ox+12,oy+8,ox+112,oy+36}:RECT{ox+22,oy+24,ox+180,oy+54};int photoPixels=count(photo,true),textPixels=count(text,false);bool lostPhoto=false,lostText=false;
   // 使用采集帧自身的宽度确定右侧预览区域，不能拿采集之后的实时窗口坐标匹配旧帧。
   int previewPixels=0;bool lostPreview=false;if(!viewer){int center=oy+232;previewPixels=count(RECT{w-rightInset-220,center-20,w-rightInset-8,center+20},true);}
   if(frames==0||elapsed<1){expectedPhoto=std::max(expectedPhoto,photoPixels);expectedText=std::max(expectedText,textPixels);expectedPreview=std::max(expectedPreview,previewPixels);}else{lostPhoto=photoPixels<expectedPhoto*.85;lostText=textPixels<expectedText*.75;lostPreview=!viewer&&previewPixels<expectedPreview*.20;if(lostPhoto)++badPhoto;if(lostText)++badText;if(lostPreview)++badPreview;}
   if(frames==0||(saved<8&&(lostPhoto||lostText||lostPreview))){bmp(out/(L"frame-"+std::to_wstring(frames)+L".bmp"),pixels,map.RowPitch,w,h);if(lostPhoto||lostText||lostPreview)++saved;}if(recording)video.write(pixels,map.RowPitch,0,0,w,h,stamp);csv<<stamp<<","<<w<<","<<h<<","<<photoPixels<<","<<textPixels<<","<<lostPhoto<<","<<lostText<<","<<previewPixels<<","<<lostPreview<<"\n";++frames;context->Unmap(staging.Get(),0);
  }
  video.finish();bool valid=frames>=30&&expectedPhoto>100&&expectedText>30&&(!simulate||viewer||(entered&&exited));bool pass=valid&&badPhoto==0&&badText==0&&(viewer||(stableHides==0&&expectedPreview>100&&badPreview==0));std::ofstream(out/L"summary.json")<<"{\"passed\":"<<(pass?"true":"false")<<",\"validCapture\":"<<(valid?"true":"false")<<",\"simulatedLifecycle\":"<<(simulate&&!viewer?"true":"false")<<",\"resizeEntered\":"<<(entered?"true":"false")<<",\"resizeExited\":"<<(exited?"true":"false")<<",\"frames\":"<<frames<<",\"resizes\":"<<resizes<<",\"photoLostFrames\":"<<badPhoto<<",\"foregroundLostFrames\":"<<badText<<",\"previewLostFrames\":"<<badPreview<<",\"childHides\":"<<hides<<",\"childShows\":"<<shows<<",\"stableChildHides\":"<<stableHides<<",\"baselinePhotoPixels\":"<<expectedPhoto<<",\"baselineForegroundPixels\":"<<expectedText<<",\"baselinePreviewPixels\":"<<expectedPreview<<"}";if(entered&&!exited)PostMessageW(observed,WM_EXITSIZEMOVE,0,0);if(hook)UnhookWinEvent(hook);return pass?0:1;
 }catch(HRESULT hr){std::ofstream(out/L"error.txt")<<std::hex<<hr;if(entered&&!exited)PostMessageW(observed,WM_EXITSIZEMOVE,0,0);if(hook)UnhookWinEvent(hook);return 3;}catch(...){if(entered&&!exited)PostMessageW(observed,WM_EXITSIZEMOVE,0,0);if(hook)UnhookWinEvent(hook);return 4;}
}
