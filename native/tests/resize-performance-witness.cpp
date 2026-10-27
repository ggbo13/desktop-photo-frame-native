#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include "wgc-source.h"

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;

struct Slot {
 LONGLONG scheduled=0,issued=0,rectangleReached=0,applied=0;
 int width=0,height=0;
 bool superseded=false;
};
struct Captured {
 LONGLONG timestamp=0,observed=0;
 int width=0,height=0,windowWidth=0,windowHeight=0;
};

static LONGLONG qpc(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
static void verify(HRESULT hr){if(FAILED(hr))throw hr;}
struct WinRtScope {~WinRtScope(){RoUninitialize();}};

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(FAILED(RoInitialize(RO_INIT_MULTITHREADED)))return 2;WinRtScope runtime;
 int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(argc<3){if(args)LocalFree(args);return 2;}
 HWND window=reinterpret_cast<HWND>(wcstoull(args[1],nullptr,10));fs::path out=args[2];bool simulateLifecycle=true;int rate=30;for(int i=3;i<argc;++i){std::wstring value=args[i];if(value==L"--no-resize-lifecycle")simulateLifecycle=false;else if(value.starts_with(L"--rate="))rate=_wtoi(value.c_str()+7);}LocalFree(args);if(rate!=30&&rate!=60)return 2;
 fs::create_directories(out);
 try{
  if(!IsWindow(window))throw E_INVALIDARG;
  MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))throw HRESULT_FROM_WIN32(GetLastError());
  int workWidth=monitor.rcWork.right-monitor.rcWork.left,workHeight=monitor.rcWork.bottom-monitor.rcWork.top;
  int availableWidth=workWidth-40,availableHeight=workHeight-40;UINT dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
  int minimumWidth=std::min(MulDiv(640,dpi,96),workWidth),minimumHeight=std::min(MulDiv(480,dpi,96),workHeight);
  int width=std::min(1280,availableWidth),height=std::min(800,availableHeight);
  if(width-minimumWidth<40)width=availableWidth;if(height-minimumHeight<40)height=availableHeight;
  if(width<minimumWidth||height<minimumHeight||(width-minimumWidth<40&&height-minimumHeight<40))throw HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  if(!SetWindowPos(window,HWND_TOP,monitor.rcWork.left+20,monitor.rcWork.top+20,width,height,SWP_NOACTIVATE))throw HRESULT_FROM_WIN32(GetLastError());
  Sleep(300);
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
  verify(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
  ComPtr<IDXGIDevice> dxgi;verify(device.As(&dxgi));WgcSource capture;verify(capture.start(dxgi.Get(),window));
  LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);const LONGLONG started=qpc();
  const int total=rate*5,period=rate*8/3,spanWidth=std::min(240,width-minimumWidth),spanHeight=std::min(150,height-minimumHeight);
  std::vector<Slot> slots(total);std::vector<Captured> frames;frames.reserve(15000);
  int next=0,queued=-1,inflight=-1,skipped=0,issued=0,applied=0,overflow=0;LONGLONG lastProbe=0,enteredQpc=0,exitedQpc=0;bool timedOut=false,windowGone=false,lifecycleError=false;DWORD setError=0;
  while(true){
   LONGLONG now=qpc(),elapsed=now-started;if(elapsed>=8*frequency.QuadPart)break;
   if(!IsWindow(window)){windowGone=true;break;}
   // 仅模拟系统发送的进入/退出通知，不注入鼠标，也不声称进入真实模态拖拽循环。
   if(simulateLifecycle&&!enteredQpc&&elapsed>=frequency.QuadPart){DWORD_PTR result=0;if(!SendMessageTimeoutW(window,WM_ENTERSIZEMOVE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result)){lifecycleError=true;break;}enteredQpc=qpc();now=enteredQpc;elapsed=now-started;}
   // 固定时间槽始终推进；尚未提交的中间目标只保留最新一个，避免跨进程消息堆积。
   while(next<total&&elapsed>=frequency.QuadPart+(LONGLONG(next)*frequency.QuadPart)/rate){
    if(queued>=0){slots[queued].superseded=true;++skipped;}
    int phase=next%period;double fraction=double(phase<period/2?phase:period-phase)/(period/2);
    auto& slot=slots[next];slot.scheduled=started+frequency.QuadPart+(LONGLONG(next)*frequency.QuadPart)/rate;slot.width=width-int(spanWidth*fraction);slot.height=height-int(spanHeight*fraction);queued=next++;
   }
   RECT rect{};if(!GetWindowRect(window,&rect)){windowGone=true;break;}
   if(inflight>=0){auto& old=slots[inflight];if(rect.right-rect.left==old.width&&rect.bottom-rect.top==old.height){if(!old.rectangleReached)old.rectangleReached=now;if(now-lastProbe>=frequency.QuadPart/120){lastProbe=now;DWORD_PTR result=0;if(SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2,&result)){old.applied=qpc();++applied;inflight=-1;}}}if(inflight>=0&&now-old.issued>2*frequency.QuadPart){timedOut=true;break;}}
   if(inflight<0&&queued>=0){auto& nextSlot=slots[queued];nextSlot.issued=now;if(!SetWindowPos(window,nullptr,0,0,nextSlot.width,nextSlot.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)){setError=GetLastError();break;}inflight=queued;queued=-1;++issued;}
   if(simulateLifecycle&&enteredQpc&&!exitedQpc&&next==total&&queued<0&&inflight<0){DWORD_PTR result=0;if(!SendMessageTimeoutW(window,WM_EXITSIZEMOVE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result)){lifecycleError=true;break;}exitedQpc=qpc();}
   ComPtr<ID3D11Texture2D> texture;LONGLONG timestamp=0;ABI::Windows::Graphics::SizeInt32 size{};
   HRESULT hr=capture.next(&texture,&timestamp,&size);if(hr==S_FALSE){capture.wait();continue;}verify(hr);
   // 仅记录窗口合成帧元数据，不复制整张 GPU 纹理或采集桌面其他程序。
   if(frames.size()<15000)frames.push_back({timestamp,qpc(),size.Width,size.Height,rect.right-rect.left,rect.bottom-rect.top});else ++overflow;
  }
  if(simulateLifecycle&&enteredQpc&&!exitedQpc)PostMessageW(window,WM_EXITSIZEMOVE,0,0);
  const LONGLONG ended=qpc();RECT finalRect{};bool hasRect=GetWindowRect(window,&finalRect)!=FALSE;
  bool finalApplied=next==total&&slots.back().applied!=0&&hasRect&&finalRect.right-finalRect.left==slots.back().width&&finalRect.bottom-finalRect.top==slots.back().height;
  std::ofstream slotFile(out/L"slots.csv");slotFile<<"slot,scheduled_qpc,issued_qpc,rectangle_qpc,applied_qpc,width,height,superseded\n";for(int i=0;i<next;++i){const auto& s=slots[i];slotFile<<i<<","<<s.scheduled<<","<<s.issued<<","<<s.rectangleReached<<","<<s.applied<<","<<s.width<<","<<s.height<<","<<s.superseded<<"\n";}
  std::ofstream frameFile(out/L"frames.csv");frameFile<<"time100ns,observed_qpc,width,height,window_width,window_height\n";for(const auto& f:frames)frameFile<<f.timestamp<<","<<f.observed<<","<<f.width<<","<<f.height<<","<<f.windowWidth<<","<<f.windowHeight<<"\n";
  bool valid=!timedOut&&!windowGone&&!setError&&!overflow&&!lifecycleError&&(!simulateLifecycle||(enteredQpc&&exitedQpc))&&finalApplied&&next==total&&frames.size()>=30;
  std::ofstream report(out/L"capture.json");report<<"{\"valid\":"<<(valid?"true":"false")<<",\"source\":\"Windows Graphics Capture window frames\",\"qpcFrequency\":"<<frequency.QuadPart<<",\"startedQpc\":"<<started<<",\"activeStartQpc\":"<<started+frequency.QuadPart<<",\"activeEndQpc\":"<<started+6*frequency.QuadPart<<",\"endedQpc\":"<<ended<<",\"simulatedLifecycle\":"<<(simulateLifecycle?"true":"false")<<",\"enteredQpc\":"<<enteredQpc<<",\"exitedQpc\":"<<exitedQpc<<",\"lifecycleError\":"<<(lifecycleError?"true":"false")<<",\"requestedRateHz\":"<<rate<<",\"plannedSlots\":"<<next<<",\"issuedSlots\":"<<issued<<",\"appliedSlots\":"<<applied<<",\"supersededSlots\":"<<skipped<<",\"frameCount\":"<<frames.size()<<",\"frameOverflow\":"<<overflow<<",\"lastTargetApplied\":"<<(finalApplied?"true":"false")<<",\"timedOut\":"<<(timedOut?"true":"false")<<",\"windowGone\":"<<(windowGone?"true":"false")<<",\"setWindowError\":"<<setError<<",\"initialWidth\":"<<width<<",\"initialHeight\":"<<height<<",\"minimumWidth\":"<<minimumWidth<<",\"minimumHeight\":"<<minimumHeight<<",\"spanWidth\":"<<spanWidth<<",\"spanHeight\":"<<spanHeight<<",\"windowDpi\":"<<dpi<<",\"workWidth\":"<<workWidth<<",\"workHeight\":"<<workHeight<<",\"captureIntervalResult\":"<<static_cast<long>(capture.intervalResult)<<"}";
  return valid?0:1;
 }catch(HRESULT hr){std::ofstream(out/L"error.txt")<<std::hex<<static_cast<unsigned long>(hr);return 3;}
 catch(...){std::ofstream(out/L"error.txt")<<"unknown error";return 4;}
}
