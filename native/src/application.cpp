#include "renderer.h"
#include "store.h"
#include "viewer.h"
#include "thumbnails.h"
#include "materials.h"
#include "photo-grid.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <future>
#include <deque>
#include <optional>
#include <tuple>
#include <wtsapi32.h>

constexpr UINT TrayMessage=WM_APP+20,RestoreMessage=WM_APP+21,WheelMessage=WM_APP+22;
constexpr int Settings=10,AddFiles=11,AddFolder=12,Pause=13,Previous=14,Next=15,Hide=16,Quit=17;
constexpr int NewList=20,RenameList=21,DeleteList=22,RemoveSource=23,SaveOptions=24,ShowPhoto=25,ExcludePhoto=26,MoveUp=27,MoveDown=28;
constexpr int PlaylistBox=100,SourceBox=101,PhotoBox=102,NameEdit=103,FrameCombo=104,RatioCombo=105,AnchorCombo=106,EffectCombo=107,OrderCombo=108,IntervalEdit=109,ThicknessEdit=110,RadiusEdit=111,TopCheck=112,LockCheck=113,StartupCheck=114;
constexpr int SearchEdit=115,ExcludedCheck=116;
constexpr int OpenViewer=29,SnapCheck=117,SnapMarginEdit=118;
constexpr int PickColor=30,ResetAppearance=31,ShadowEdit=119;
constexpr int FramePreview=201;
constexpr int PagePhotos=400,PageAppearance=401,PagePlayback=402,PageApplication=403;
constexpr int FrameCard=410,AnchorButton=420,ViewGrid=430,ViewList=431,PlaylistCompact=432;
constexpr int ThicknessSlider=440,RadiusSlider=441,ShadowSlider=442,LimitCheck=450,MaxWidthEdit=451,MaxHeightEdit=452,UseCurrentSize=453,FullscreenCheck=454,BatteryCheck=455;
constexpr int ManageLists=460,ManageSources=461;
static const char* frames[] = {"minimal","wood","metal","polaroid","borderless"};
static const char* ratios[] = {"free","auto","1:1","4:3","3:2","16:9","9:16"};
static const char* anchors[] = {"top-left","top-right","bottom-left","bottom-right"};
static const char* effects[] = {"fade","slide","zoom","none"};
static const char* orders[] = {"sequential","random"};
static LRESULT CALLBACK appProcedure(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK photoListProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
static LRESULT CALLBACK mouseWheelProcedure(int,WPARAM,LPARAM);
class Application;
static Application* app=nullptr;
static std::wstring controlText(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
static int comboIndex(const Json& c,const char* key,const char* const* values,int count){auto value=stringValue(c,key);for(int i=0;i<count;++i)if(value==values[i])return i;return 0;}

class Application {
public:
 Frame frame;Store store;PhotoViewer viewer;Thumbnails thumbnails;HWND settings=nullptr;HINSTANCE module=nullptr;HANDLE singleton=nullptr;HHOOK wheelHook=nullptr;int wheelRemainder=0;
 int photoDragStart=-1;POINT photoDragPoint{};bool photoDragging=false;
 std::wstring className;HFONT font=nullptr,heading=nullptr;HBRUSH background=nullptr,panel=nullptr;HICON icons[3]{};
 NOTIFYICONDATAW tray{};UINT taskbarCreated=0;bool trayAdded=false,quitting=false,refreshing=false,restoringHistory=false,noTray=false;
 std::vector<fs::path> history;size_t historyCursor=0;std::deque<size_t> bag;std::mt19937 random{std::random_device{}()};
 std::future<Store> scanFuture;double lastScan=0,lastDeviceCheck=0;unsigned lastFailure=0;double testSeconds=0,startTime=0,lastMetrics=0;fs::path metricsPath,dataPath;
 bool recovering=false,simulateDeviceLoss=false;
 uint64_t timerTicks=0,paintEvents=0;UINT mainTimerInterval=8;
 HWND frameClickDownWindow=nullptr,lastFrameClickWindow=nullptr;
 POINT frameClickDownPoint{},lastFrameClickPoint{};Bounds frameClickDownBounds;
 ULONGLONG frameClickReadyAt=0,lastFrameClickAt=0;bool frameWasShown=false;
 unsigned catalogueGeneration=0,scanGeneration=0;std::vector<fs::path> displayedPhotos;
 int settingsScrollX=0,settingsScrollY=0;
 #include "environment-v8.inc"
 #include "navigation-v8.inc"
 #include "settings-v8.inc"
 void scrollSettings(int bar,int request){
  SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(settings,bar,&info);int position=info.nPos;
  if(request==SB_LINEUP)position-=32;else if(request==SB_LINEDOWN)position+=32;else if(request==SB_PAGEUP)position-=info.nPage;else if(request==SB_PAGEDOWN)position+=info.nPage;else if(request==SB_THUMBTRACK)position=info.nTrackPos;else if(request==SB_TOP)position=0;else if(request==SB_BOTTOM)position=info.nMax;
  position=std::clamp(position,0,std::max(0,info.nMax-int(info.nPage)+1));int& previous=bar==SB_HORZ?settingsScrollX:settingsScrollY;int difference=previous-position;previous=position;SetScrollPos(settings,bar,position,TRUE);ScrollWindowEx(settings,bar==SB_HORZ?difference:0,bar==SB_VERT?difference:0,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);
 }
 void settingsViewport(){
  if(newSettings){layoutV8();return;}
  RECT client;GetClientRect(settings,&client);
  for(int bar:{SB_HORZ,SB_VERT}){SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE};info.nMin=0;info.nMax=bar==SB_HORZ?959:879;info.nPage=bar==SB_HORZ?client.right:client.bottom;SetScrollInfo(settings,bar,&info,TRUE);scrollSettings(bar,SB_ENDSCROLL);}
 }
 ~Application(){quitting=true;unregisterEnvironment();if(wheelHook)UnhookWindowsHookEx(wheelHook);viewer.closed={};viewer.close();if(settings&&IsWindow(settings))DestroyWindow(settings);if(frame.hwnd&&IsWindow(frame.hwnd))DestroyWindow(frame.hwnd);frame.hwnd=frame.inputHwnd=nullptr;if(scanFuture.valid())scanFuture.wait();if(trayAdded)Shell_NotifyIconW(NIM_DELETE,&tray);for(auto icon:icons)if(icon)DestroyIcon(icon);if(font)DeleteObject(font);if(heading)DeleteObject(heading);if(background)DeleteObject(background);if(panel)DeleteObject(panel);if(singleton)CloseHandle(singleton);app=nullptr;}
 HWND control(int id){return GetDlgItem(settings,id);}
 void save(){store.state["bounds"]={{"x",frame.bounds.x+frame.area.left},{"y",frame.bounds.y+frame.area.top},{"width",frame.bounds.w},{"height",frame.bounds.h}};store.state["global"]["preferredPhotoArea"]=frame.preferredPhotoArea;store.save();}
 void stopAfterFailure(HRESULT hr){
  // 先封锁重入和定时器；MessageBox 的嵌套消息循环曾让相同错误每 8 毫秒再弹一次。
  if(recovering)return;
  recovering=true;if(frame.hwnd)KillTimer(frame.hwnd,TimerId);
  HRESULT reason=frame.d3d?frame.d3d->GetDeviceRemovedReason():hr;
  bool deviceLost=hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET||hr==DXGI_ERROR_DEVICE_HUNG||hr==DXGI_ERROR_DRIVER_INTERNAL_ERROR||hr==D2DERR_RECREATE_TARGET;
  try{save();}catch(...){}
  try{std::ofstream log(dataPath/L"graphics-recovery.log",std::ios::app);log<<"failure hr=0x"<<std::hex<<static_cast<unsigned long>(hr)<<" reason=0x"<<static_cast<unsigned long>(reason)<<" software="<<frame.softwareRenderer<<" line="<<std::dec<<errorLine<<"\n";}catch(...){}
  if(deviceLost&&!frame.softwareRenderer){
   wchar_t path[32768]{};DWORD length=GetModuleFileNameW(nullptr,path,32768);
   if(length&&length<32768){
    std::wstring command=L"\""+std::wstring(path)+L"\" --software-renderer --recover-from "+std::to_wstring(GetCurrentProcessId())+L" --data-dir \""+dataPath.wstring()+L"\"";
    if(testSeconds>0)command+=L" --test-seconds "+std::to_wstring(testSeconds);
    if(noTray)command+=L" --no-tray";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    if(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)){CloseHandle(child.hThread);CloseHandle(child.hProcess);}
   }
  }
  PostQuitMessage(1);
 }
 void clearPicture(){frame.cancel();if(frame.current.visual)frame.well->RemoveVisual(frame.current.visual.Get());frame.current={};frame.hasPhoto=false;frame.index=0;frame.failedIndices.clear();frame.commit();SetWindowTextW(frame.hwnd,L"桌面相框 原生版");}
 void resetOrder(bool skipCurrent=true){bag.clear();std::vector<size_t> all;for(size_t i=0;i<frame.paths.size();++i)if(!skipCurrent||!frame.hasPhoto||i!=frame.index)all.push_back(i);if(stringValue(store.active()["config"],"order")=="random")std::shuffle(all.begin(),all.end(),random);if(!skipCurrent&&all.size()>1&&all.front()==frame.index)std::swap(all[0],all[1]);for(auto i:all)bag.push_back(i);}
 size_t following(){
  if(frame.paths.empty())return 0;
  if(stringValue(store.active()["config"],"order")!="random")return (frame.index+1)%frame.paths.size();
  if(bag.empty())resetOrder(false);return bag.empty()?frame.index:bag.front();
 }
 void onPresented(size_t i){
  if(i>=frame.paths.size())return;
  SetWindowTextW(frame.hwnd,(L"桌面相框 原生版 · "+frame.paths[i].filename().wstring()).c_str());
  auto it=std::find(bag.begin(),bag.end(),i);if(it!=bag.end())bag.erase(it);
  recordPresented(i);
  updateTray();if(settings){if(newSettings)photoGrid.current=frame.paths[i];InvalidateRect(control(PhotoBox),nullptr,FALSE);InvalidateRect(control(FramePreview),nullptr,FALSE);if(newSettings)for(int n=0;n<5;++n)InvalidateRect(control(FrameCard+n),nullptr,FALSE);}
 }
 void appearance(){
  auto& c=store.active()["config"];frame.cancel();resetNavigation();frame.effect=comboIndex(c,"transition",effects,4);frame.anchor=comboIndex(c,"anchor",anchors,4);frame.playInterval=numberValue(c,"interval",10);
  int ratio=comboIndex(c,"ratio",ratios,7);const float values[]={-1,0,1,4.f/3,1.5f,16.f/9,9.f/16};frame.fixedRatio=values[ratio];
  frame.limitAutoSize=boolValue(c,"limitAutoSize");frame.maxWidth=float(numberValue(c,"maxWidth",640));frame.maxHeight=float(numberValue(c,"maxHeight",640));
  int style=comboIndex(c,"frame",frames,5);float oldBorder=Border;Border=style==4?0:float(numberValue(c,"thickness",18));float difference=2*(Border-oldBorder);frame.bounds.w+=difference;frame.bounds.h+=difference;if(frame.anchor%2)frame.bounds.x-=difference;if(frame.anchor>=2)frame.bounds.y-=difference;frame.keepInWorkArea();frame.well->SetOffsetX(Border);frame.well->SetOffsetY(Border);
  auto pixels=frameMaterial(style,parseFrameColor(stringValue(c,"color","#203336")));frame.matWidth=frame.matHeight=256;frame.matSurface=frame.surface(256,256,pixels.data());frame.mat->SetContent(frame.matSurface.Get());
  float radius=float(numberValue(c,"radius",10));frame.cornerRadius=radius;frame.setShadow(float(numberValue(c,"shadow",30))/100);
  frame.outerClip->SetTopLeftRadiusX(radius);frame.outerClip->SetTopLeftRadiusY(radius);frame.outerClip->SetTopRightRadiusX(radius);frame.outerClip->SetTopRightRadiusY(radius);
  frame.outerClip->SetBottomLeftRadiusX(radius);frame.outerClip->SetBottomLeftRadiusY(radius);frame.outerClip->SetBottomRightRadiusX(radius);frame.outerClip->SetBottomRightRadiusY(radius);
  if(frame.hasPhoto){frame.bounds=frame.fitted(frame.current.ratio);frame.pictureGeometry(frame.current,frame.bounds,frame.bounds,false,true);}
  frame.applyGeometry(frame.bounds,false);frame.updateRegion(frame.bounds);frame.commit();frame.next=seconds()+frame.playInterval;
  if(settings)InvalidateRect(control(FramePreview),nullptr,FALSE);
  topmost();
 }
 void topmost(){HWND order=boolValue(store.state["global"],"alwaysOnTop")?HWND_TOPMOST:HWND_NOTOPMOST;for(HWND h:{frame.hwnd,frame.inputHwnd})if(h)SetWindowPos(h,order,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
 void activate(const std::string& id){
  bool exists=false;for(auto& p:store.state["playlists"])if(stringValue(p,"id")==id)exists=true;if(!exists)return;
  ++catalogueGeneration;clearPicture();resetNavigation();store.state["activePlaylistId"]=id;store.scan();frame.paths=store.photos;history.clear();historyCursor=0;restoringHistory=false;appearance();resetOrder();
  if(!frame.paths.empty())frame.request(stringValue(store.active()["config"],"order")=="random"?following():0);
  save();updateTray();if(settings)refreshSettings();
 }
 void switchByIndex(int i){if(i>=0&&size_t(i)<store.state["playlists"].size())activate(stringValue(store.state["playlists"][i],"id"));}
 void refreshCatalogue(Store scanned){
  if(stringValue(scanned.state,"activePlaylistId")!=stringValue(store.state,"activePlaylistId"))return;
  store.problems=std::move(scanned.problems);store.active()["photoOrder"]=scanned.active()["photoOrder"];
  if(scanned.photos!=store.photos){
   fs::path shown=frame.hasPhoto&&frame.index<frame.paths.size()?frame.paths[frame.index]:fs::path{};frame.cancel();frame.paths=scanned.photos;store.photos=std::move(scanned.photos);frame.failedIndices.clear();
   auto it=std::find(frame.paths.begin(),frame.paths.end(),shown);
   if(it!=frame.paths.end())frame.index=size_t(it-frame.paths.begin());else{clearPicture();if(!frame.paths.empty())frame.request(0);}
   resetNavigation();resetOrder();save();updateTray();
  }
  if(settings)updatePhotos();
 }
 void refreshTimer(){
  if(quitting||recovering||!frame.hwnd||!IsWindow(frame.hwnd))return;
  // 合成器自行推进动画；静止时不必持续以高频轮询整个应用。
  UINT interval=(frame.animating||viewer.job.valid()||(settings&&thumbnails.job.valid()))?16:((!frame.visible||frame.suspended)?1000:250);
  if(frame.visible&&!frame.paused&&!frame.suspended&&!frame.loading&&!frame.animating&&!frame.paths.empty()){
   double remaining=std::ceil((frame.next-seconds())*1000);
   interval=std::min(interval,UINT(std::clamp(remaining,16.,double(interval))));
  }
  if(interval==mainTimerInterval)return;
  if(!SetTimer(frame.hwnd,TimerId,interval,nullptr))throw E_FAIL;
  mainTimerInterval=interval;
 }
 void tick(){
  pollEnvironment();
  double now=seconds();if(frame.d3d&&now-lastDeviceCheck>=1){lastDeviceCheck=now;check(frame.d3d->GetDeviceRemovedReason());}
  viewer.tick();if(newSettings){auto visiblePhotos=photoGrid.visiblePaths();thumbnails.tick(photoGrid.hwnd,displayedPhotos,frame.hasPhoto&&frame.index<frame.paths.size()?frame.paths[frame.index]:fs::path{},&visiblePhotos,160*120*settingsDpi*settingsDpi);}else thumbnails.tick(settings?control(PhotoBox):nullptr,displayedPhotos,frame.hasPhoto&&frame.index<frame.paths.size()?frame.paths[frame.index]:fs::path{});frame.tick();if(testSeconds>0&&seconds()-startTime>testSeconds){exit();return;}
  if(frame.log&&!frame.benchmark&&seconds()-lastMetrics>=1){lastMetrics=seconds();frame.sample();}
  if(scanFuture.valid()&&!frame.animating&&scanFuture.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto result=scanFuture.get();if(scanGeneration==catalogueGeneration)refreshCatalogue(std::move(result));}
  if(seconds()-lastScan>5&&!scanFuture.valid()){
   lastScan=seconds();scanGeneration=catalogueGeneration;Json snapshot=store.state;
   scanFuture=std::async(std::launch::async,[snapshot]{Store scan;scan.state=snapshot;scan.scan();return scan;});
  }
  if(lastFailure!=frame.failed){lastFailure=frame.failed;updateTray();if(settings)updatePhotos();}
 }
 void paused(bool value){frame.paused=value;store.state["global"]["paused"]=value;if(value){frame.cancel();resetNavigation();}else frame.next=seconds()+frame.playInterval;save();updateTray();}
 void resetFrameClicks(bool waitForLaunchClick){frameClickDownWindow=lastFrameClickWindow=nullptr;lastFrameClickAt=0;frameClickReadyAt=waitForLaunchClick?GetTickCount64()+GetDoubleClickTime():0;}
 void frameMouseDown(HWND h,POINT screen){frameClickDownWindow=h;frameClickDownPoint=screen;frameClickDownBounds=frame.bounds;}
 void frameMouseUp(HWND h,POINT screen){
  if(frameClickDownWindow==h&&GetTickCount64()>=frameClickReadyAt&&
     std::abs(screen.x-frameClickDownPoint.x)<=GetSystemMetrics(SM_CXDOUBLECLK)/2&&
     std::abs(screen.y-frameClickDownPoint.y)<=GetSystemMetrics(SM_CYDOUBLECLK)/2&&
     std::abs(frame.bounds.x-frameClickDownBounds.x)<1&&std::abs(frame.bounds.y-frameClickDownBounds.y)<1&&
     std::abs(frame.bounds.w-frameClickDownBounds.w)<1&&std::abs(frame.bounds.h-frameClickDownBounds.h)<1){
   lastFrameClickWindow=h;lastFrameClickPoint=screen;lastFrameClickAt=GetTickCount64();
  }else{lastFrameClickWindow=nullptr;lastFrameClickAt=0;}
  frameClickDownWindow=nullptr;
 }
 bool frameDoubleClick(HWND h,POINT screen){
  ULONGLONG now=GetTickCount64();bool valid=h==frame.inputHwnd&&lastFrameClickWindow==h&&lastFrameClickAt>0&&now>=frameClickReadyAt&&
   now-lastFrameClickAt<=GetDoubleClickTime()&&
   std::abs(screen.x-lastFrameClickPoint.x)<=GetSystemMetrics(SM_CXDOUBLECLK)/2&&
   std::abs(screen.y-lastFrameClickPoint.y)<=GetSystemMetrics(SM_CYDOUBLECLK)/2;
  frameClickDownWindow=lastFrameClickWindow=nullptr;lastFrameClickAt=0;return valid;
 }
 void visible(bool show){if(!show||!frameWasShown||!environment.userVisible)resetFrameClicks(show);if(show)frameWasShown=true;environment.userVisible=show;if(!show)viewer.close();syncEnvironment();if(show)frame.next=seconds()+frame.playInterval;}
 void exit(){quitting=true;unregisterEnvironment();viewer.close();save();if(settings)DestroyWindow(settings);DestroyWindow(frame.hwnd);}
 void showViewer(){if(!frame.hasPhoto||frame.index>=frame.paths.size()||environment.autoHidden())return;frame.cancel();viewer.closed=[this]{environment.viewer=false;syncEnvironment();frame.next=seconds()+frame.playInterval;};viewer.open(module,frame.paths[frame.index],frame.workArea(frame.bounds),font);environment.viewer=true;syncEnvironment();}
 void endDrag(){
  bool moved=frame.dragging;frame.mouseUp();if(moved)frame.keepInWorkArea();auto work=frame.workArea(frame.bounds);float originX=float(work.left-frame.area.left),originY=float(work.top-frame.area.top),width=float(work.right-work.left),height=float(work.bottom-work.top);
  if(moved){frame.bounds.x=std::clamp(frame.bounds.x,originX,originX+std::max(0.f,width-frame.bounds.w));frame.bounds.y=std::clamp(frame.bounds.y,originY,originY+std::max(0.f,height-frame.bounds.h));}
  if(moved&&boolValue(store.state["global"],"snap",true)){
   float margin=float(numberValue(store.state["global"],"snapMargin",12));
   float insetX=std::min(margin,std::max(0.f,(width-frame.bounds.w)/2)),insetY=std::min(margin,std::max(0.f,(height-frame.bounds.h)/2));float left=originX+insetX,top=originY+insetY,right=originX+width-frame.bounds.w-insetX,bottom=originY+height-frame.bounds.h-insetY;
   int horizontal=-1,vertical=-1;if(std::abs(frame.bounds.x-left)<=16){frame.bounds.x=left;horizontal=0;}else if(std::abs(frame.bounds.x-right)<=16){frame.bounds.x=right;horizontal=1;}
   if(std::abs(frame.bounds.y-top)<=16){frame.bounds.y=top;vertical=0;}else if(std::abs(frame.bounds.y-bottom)<=16){frame.bounds.y=bottom;vertical=1;}
   if(horizontal>=0&&vertical>=0){frame.anchor=vertical*2+horizontal;store.active()["config"]["anchor"]=anchors[frame.anchor];if(settings)SendMessage(control(AnchorCombo),CB_SETCURSEL,frame.anchor,0);}
   frame.applyGeometry(frame.bounds,false);frame.updateRegion(frame.bounds);frame.commit();
  }if(moved){frame.applyGeometry(frame.bounds,false);frame.updateRegion(frame.bounds);frame.commit();}save();
 }
 void addSources(bool folder){
  ComPtr<IFileOpenDialog> dialog;check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST)|FOS_ALLOWMULTISELECT);
  if(FAILED(dialog->Show(settings?settings:frame.inputHwnd)))return;ComPtr<IShellItemArray> selected;check(dialog->GetResults(&selected));DWORD count=0;selected->GetCount(&count);
  for(DWORD i=0;i<count;++i){ComPtr<IShellItem> item;selected->GetItemAt(i,&item);PWSTR value=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))){auto& sources=store.active()["sources"];bool known=false;for(auto& s:sources)if(pathKey(fs::path(wide(stringValue(s,"path"))))==pathKey(value))known=true;if(!known)sources.push_back({{"type",folder?"folder":"file"},{"path",utf8(value)}});CoTaskMemFree(value);}}
  store.scan();Store result=store;refreshCatalogue(std::move(result));save();activate(stringValue(store.active(),"id"));
 }
 HICON makeIcon(int state){
  BITMAPV5HEADER info{};info.bV5Size=sizeof(info);info.bV5Width=32;info.bV5Height=-32;info.bV5Planes=1;info.bV5BitCount=32;info.bV5Compression=BI_BITFIELDS;info.bV5RedMask=0x00ff0000;info.bV5GreenMask=0x0000ff00;info.bV5BlueMask=0x000000ff;info.bV5AlphaMask=0xff000000;
  void* raw=nullptr;HDC screen=GetDC(nullptr);HBITMAP color=CreateDIBSection(screen,reinterpret_cast<BITMAPINFO*>(&info),DIB_RGB_COLORS,&raw,nullptr,0);ReleaseDC(nullptr,screen);if(!color)return CopyIcon(LoadIcon(nullptr,IDI_APPLICATION));
  auto pixels=static_cast<DWORD*>(raw);DWORD tone=state==2?0xff78837e:(state==1?0xffb07d32:0xff287a7f);
  for(int y=0;y<32;++y)for(int x=0;x<32;++x){bool outer=x>=3&&x<29&&y>=4&&y<28;bool rim=x<7||x>=25||y<8||y>=24;bool symbol=state==1?((x>=12&&x<15)||(x>=18&&x<21))&&y>=11&&y<22:(x>=12&&x<=21&&abs(y-16)<=(21-x)/2);pixels[y*32+x]=outer?(rim||symbol?tone:0xfff5f6f3):0;}
  BYTE mask[128]{};HBITMAP mono=CreateBitmap(32,32,1,1,mask);ICONINFO icon{TRUE,0,0,mono,color};HICON result=CreateIconIndirect(&icon);DeleteObject(mono);DeleteObject(color);return result;
 }
 void updateTray(){
  if(noTray||!frame.hwnd)return;
  std::wstring status=environmentStatus();auto name=wide(stringValue(store.active(),"name"));
  auto text=L"桌面相框 · "+name+L" · "+status+L" · "+std::to_wstring(frame.paths.size())+L" 张";
  tray.cbSize=sizeof(tray);tray.hWnd=frame.hwnd;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TrayMessage;tray.hIcon=icons[!frame.visible?2:(frame.paused?1:0)];wcsncpy_s(tray.szTip,text.c_str(),_TRUNCATE);
  if(!trayAdded)trayAdded=Shell_NotifyIconW(NIM_ADD,&tray);else Shell_NotifyIconW(NIM_MODIFY,&tray);
 }
 void menu(){
  HMENU m=CreatePopupMenu();auto title=wide(stringValue(store.active(),"name"))+L" · "+std::to_wstring(frame.paths.size())+L" 张照片";
  AppendMenuW(m,MF_STRING|MF_GRAYED,0,title.c_str());AppendMenuW(m,MF_SEPARATOR,0,nullptr);
  AppendMenuW(m,MF_STRING,Hide,environment.userVisible?L"隐藏相框":L"显示相框");
  AppendMenuW(m,MF_STRING,Pause,frame.paused?L"继续播放":L"暂停播放");
  AppendMenuW(m,MF_STRING|(historyCursor>0?0:MF_GRAYED),Previous,L"上一张");
  AppendMenuW(m,MF_STRING|(frame.paths.size()>1?0:MF_GRAYED),Next,L"下一张");
  AppendMenuW(m,MF_STRING|(frame.hasPhoto?0:MF_GRAYED),OpenViewer,L"查看大图…");
  AppendMenuW(m,MF_SEPARATOR,0,nullptr);
  HMENU lists=CreatePopupMenu();int i=0;for(auto& p:store.state["playlists"]){AppendMenuW(lists,MF_STRING|(stringValue(p,"id")==stringValue(store.state,"activePlaylistId")?MF_CHECKED:0),1000+i++,wide(stringValue(p,"name")).c_str());}AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(lists),L"切换播放列表");
  AppendMenuW(m,MF_STRING,Settings,L"照片与设置…");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,Quit,L"退出");
  POINT point;GetCursorPos(&point);SetForegroundWindow(frame.inputHwnd);int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,frame.inputHwnd,nullptr);DestroyMenu(m);PostMessage(frame.inputHwnd,WM_NULL,0,0);if(id>=1000)switchByIndex(id-1000);else command(id);
 }
 HWND createControl(const wchar_t* type,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0){auto window=CreateWindowExW(wcscmp(type,L"EDIT")==0||wcscmp(type,L"LISTBOX")==0?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,settings,reinterpret_cast<HMENU>(INT_PTR(id)),module,nullptr);SendMessage(window,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SetWindowTheme(window,L"Explorer",nullptr);return window;}
 void label(const wchar_t* text,int x,int y,int width=280){auto h=createControl(L"STATIC",text,0,x,y,width,24,SS_LEFT);SetWindowLongPtr(h,GWL_STYLE,GetWindowLongPtr(h,GWL_STYLE)&~WS_TABSTOP);}
 void combo(int id,std::initializer_list<const wchar_t*> entries,int x,int y,int width=254){auto h=createControl(L"COMBOBOX",L"",id,x,y,width,240,CBS_DROPDOWNLIST|WS_VSCROLL);for(auto s:entries)SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));}
 void showLegacySettings(){
  if(settings){ShowWindow(settings,SW_RESTORE);SetForegroundWindow(settings);return;}
  auto saved=objectValue(store.state,"settingsBounds");int w=int(numberValue(saved,"width",1040)),h=int(numberValue(saved,"height",720));RECT area=frame.workArea(frame.bounds);if(saved.contains("x")&&saved.contains("y")){RECT r{LONG(numberValue(saved,"x",area.left)),LONG(numberValue(saved,"y",area.top)),0,0};r.right=r.left+w;r.bottom=r.top+h;MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&monitor);area=monitor.rcWork;}w=std::clamp(w,std::min(800,int(area.right-area.left)),int(area.right-area.left));h=std::clamp(h,std::min(600,int(area.bottom-area.top)),int(area.bottom-area.top));settingsScrollX=settingsScrollY=0;
  int x=std::clamp(int(numberValue(saved,"x",area.left+40)),int(area.left),int(area.right)-w),y=std::clamp(int(numberValue(saved,"y",area.top+40)),int(area.top),int(area.bottom)-h);
  settings=CreateWindowExW(0,L"NativePhotoFrameSettings",L"桌面相框 " PHOTO_FRAME_VERSION_W L" · 照片与设置",WS_OVERLAPPEDWINDOW|WS_HSCROLL|WS_VSCROLL,x,y,w,h,nullptr,nullptr,module,nullptr);
  label(L"播放列表",24,22,160);createControl(L"LISTBOX",L"",PlaylistBox,24,56,186,350,LBS_NOTIFY|WS_VSCROLL);
  createControl(L"EDIT",L"",NameEdit,24,424,186,28,ES_AUTOHSCROLL);createControl(L"BUTTON",L"新建",NewList,24,462,88,30);createControl(L"BUTTON",L"改名",RenameList,122,462,88,30);createControl(L"BUTTON",L"删除列表…",DeleteList,24,504,186,30);
  label(L"照片来源",236,22,300);createControl(L"BUTTON",L"添加照片…",AddFiles,236,56,115,32);createControl(L"BUTTON",L"添加文件夹…",AddFolder,359,56,140,32);createControl(L"BUTTON",L"移除来源…",RemoveSource,506,56,124,32);
  createControl(L"LISTBOX",L"",SourceBox,236,102,394,110,LBS_NOTIFY|WS_HSCROLL|WS_VSCROLL);
  createControl(L"EDIT",L"",SearchEdit,236,228,244,28,ES_AUTOHSCROLL);SendMessageW(control(SearchEdit),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"按文件名搜索照片"));
  createControl(L"BUTTON",L"已排除",ExcludedCheck,494,228,136,28,BS_AUTOCHECKBOX);createControl(L"LISTBOX",L"",PhotoBox,236,264,394,240,LBS_NOTIFY|WS_VSCROLL|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS);SendMessage(control(PhotoBox),LB_SETITEMHEIGHT,0,76);SetWindowSubclass(control(PhotoBox),photoListProcedure,1,0);
  createControl(L"BUTTON",L"前移",MoveUp,236,518,70,30);createControl(L"BUTTON",L"后移",MoveDown,314,518,70,30);createControl(L"BUTTON",L"排除轮播…",ExcludePhoto,392,518,115,30);createControl(L"BUTTON",L"播放选中",ShowPhoto,515,518,115,30);
  label(L"列表外观与播放",654,22,320);label(L"相框样式",654,58);combo(FrameCombo,{L"极简",L"木质",L"金属",L"拍立得",L"无边框"},654,85);
  label(L"照片区域比例",654,122);combo(RatioCombo,{L"自由",L"自适应照片",L"1:1",L"4:3",L"3:2",L"16:9",L"9:16"},654,149);
  label(L"自适应固定角",654,186);combo(AnchorCombo,{L"左上角",L"右上角",L"左下角",L"右下角"},654,213);
  label(L"切换效果",654,250);combo(EffectCombo,{L"淡入淡出",L"水平滑动",L"轻微缩放",L"无动画"},654,277);
  combo(OrderCombo,{L"按顺序播放",L"随机播放"},654,320);label(L"间隔（秒）     边框（像素）     圆角",654,361,330);
  createControl(L"EDIT",L"10",IntervalEdit,654,390,70,28,ES_NUMBER);createControl(L"EDIT",L"18",ThicknessEdit,756,390,70,28,ES_NUMBER);createControl(L"EDIT",L"10",RadiusEdit,858,390,70,28,ES_NUMBER);
  createControl(L"BUTTON",L"置顶",TopCheck,654,438,92,26,BS_AUTOCHECKBOX);createControl(L"BUTTON",L"锁定位置",LockCheck,750,438,130,26,BS_AUTOCHECKBOX);createControl(L"BUTTON",L"登录后自启",StartupCheck,654,476,160,26,BS_AUTOCHECKBOX);
  createControl(L"BUTTON",L"应用设置",SaveOptions,654,520,254,34,BS_DEFPUSHBUTTON);createControl(L"STATIC",L"",200,236,570,394,64,SS_LEFT);
  label(L"内沿阴影（0–100%）",654,570,200);createControl(L"EDIT",L"30",ShadowEdit,858,568,70,28,ES_NUMBER);createControl(L"BUTTON",L"极简相框颜色…",PickColor,654,610,150,30);createControl(L"BUTTON",L"恢复默认外观",ResetAppearance,812,610,116,30);
  createControl(L"BUTTON",L"边缘吸附",SnapCheck,24,564,112,28,BS_AUTOCHECKBOX);createControl(L"EDIT",L"12",SnapMarginEdit,142,564,68,28,ES_NUMBER);label(L"距边缘（0–64 像素）",24,600,200);
  // 将预览放到外观栏顶部，打开设置即可看到当前照片和边框。
  EnumChildWindows(settings,[](HWND child,LPARAM data)->BOOL{auto parent=reinterpret_cast<HWND>(data);if(GetParent(child)!=parent)return TRUE;RECT r;GetWindowRect(child,&r);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);if(r.left>=650&&r.top>=58)SetWindowPos(child,nullptr,r.left,r.top+172,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return TRUE;},reinterpret_cast<LPARAM>(settings));
  label(L"当前照片 · 相框预览",654,54,274);createControl(L"STATIC",L"",FramePreview,654,82,274,128,SS_OWNERDRAW);
  refreshSettings();settingsViewport();ShowWindow(settings,SW_SHOW);ShowWindow(settings,SW_SHOW);SetForegroundWindow(settings);
 }
 void updatePhotos(){
  if(newSettings){updatePhotosV8();return;}
  if(!settings)return;int selected=int(SendMessage(control(PhotoBox),LB_GETCURSEL,0,0)),first=int(SendMessage(control(PhotoBox),LB_GETTOPINDEX,0,0));fs::path selectedPath=selected>=0&&size_t(selected)<displayedPhotos.size()?displayedPhotos[selected]:fs::path{};SendMessage(control(PhotoBox),WM_SETREDRAW,FALSE,0);SendMessage(control(PhotoBox),LB_RESETCONTENT,0,0);
  bool excluded=SendMessage(control(ExcludedCheck),BM_GETCHECK,0,0)==BST_CHECKED;auto query=controlText(control(SearchEdit));std::transform(query.begin(),query.end(),query.begin(),::towlower);displayedPhotos.clear();
  std::vector<fs::path> candidates=frame.paths;if(excluded){candidates.clear();for(auto& value:store.active()["excluded"])candidates.emplace_back(wide(value.get<std::string>()));}
  for(auto& path:candidates){auto name=path.filename().wstring(),lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),::towlower);if(!query.empty()&&lower.find(query)==std::wstring::npos)continue;displayedPhotos.push_back(path);auto found=std::find(frame.paths.begin(),frame.paths.end(),path);size_t i=size_t(found-frame.paths.begin());auto text=(frame.hasPhoto&&i==frame.index?L"▶  ":L"    ")+name;if(frame.failedIndices.contains(i))text=L"无法解码 · "+text;SendMessageW(control(PhotoBox),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
  SetWindowTextW(control(ExcludePhoto),excluded?L"恢复轮播":L"排除轮播…");for(int id:{MoveUp,MoveDown,ShowPhoto})EnableWindow(control(id),!excluded);
  auto selection=std::find(displayedPhotos.begin(),displayedPhotos.end(),selectedPath);if(selection!=displayedPhotos.end())SendMessage(control(PhotoBox),LB_SETCURSEL,selection-displayedPhotos.begin(),0);SendMessage(control(PhotoBox),LB_SETTOPINDEX,std::max(0,first),0);SendMessage(control(PhotoBox),WM_SETREDRAW,TRUE,0);InvalidateRect(control(PhotoBox),nullptr,TRUE);
  auto status=std::to_wstring(frame.paths.size())+L" 张可播放照片";
  if(frame.paths.empty())status=L"添加照片或文件夹，开始轮播。";
  if(!store.problems.empty())status+=L"  ·  "+store.problems.front();
  if(frame.failed)status+=L"  ·  无法解码的照片已跳过";SetWindowTextW(control(200),status.c_str());
 }
 void refreshSettings(){
  if(newSettings){refreshV8();return;}
  if(!settings)return;refreshing=true;SendMessage(control(PlaylistBox),LB_RESETCONTENT,0,0);int selected=0,i=0;
  for(auto& p:store.state["playlists"]){auto name=wide(stringValue(p,"name"));SendMessageW(control(PlaylistBox),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(stringValue(p,"id")==stringValue(store.state,"activePlaylistId"))selected=i;++i;}
  SendMessage(control(PlaylistBox),LB_SETCURSEL,selected,0);SetWindowTextW(control(NameEdit),wide(stringValue(store.active(),"name")).c_str());SendMessage(control(SourceBox),LB_RESETCONTENT,0,0);
  for(auto& source:store.active()["sources"]){auto text=(stringValue(source,"type")=="folder"?L"文件夹 · ":L"照片 · ")+wide(stringValue(source,"path"));SendMessageW(control(SourceBox),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
  auto& c=store.active()["config"];SendMessage(control(FrameCombo),CB_SETCURSEL,comboIndex(c,"frame",frames,5),0);SendMessage(control(RatioCombo),CB_SETCURSEL,comboIndex(c,"ratio",ratios,7),0);SendMessage(control(AnchorCombo),CB_SETCURSEL,comboIndex(c,"anchor",anchors,4),0);SendMessage(control(EffectCombo),CB_SETCURSEL,comboIndex(c,"transition",effects,4),0);SendMessage(control(OrderCombo),CB_SETCURSEL,comboIndex(c,"order",orders,2),0);
  for(auto [id,key]:std::initializer_list<std::pair<int,const char*>>{{IntervalEdit,"interval"},{ThicknessEdit,"thickness"},{RadiusEdit,"radius"},{ShadowEdit,"shadow"}})SetWindowTextW(control(id),std::to_wstring(int(numberValue(c,key,10))).c_str());
  for(auto [id,key]:std::initializer_list<std::pair<int,const char*>>{{TopCheck,"alwaysOnTop"},{LockCheck,"lockPosition"},{StartupCheck,"autoStart"}})SendMessage(control(id),BM_SETCHECK,boolValue(store.state["global"],key)?BST_CHECKED:BST_UNCHECKED,0);
  SendMessage(control(SnapCheck),BM_SETCHECK,boolValue(store.state["global"],"snap",true)?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(control(SnapMarginEdit),std::to_wstring(int(numberValue(store.state["global"],"snapMargin",12))).c_str());
  updatePhotos();refreshing=false;
  InvalidateRect(control(FramePreview),nullptr,FALSE);
 }
 bool startup(bool enable){
  HKEY key;LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(result!=ERROR_SUCCESS)return false;
  if(enable){wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);std::wstring command=L"\""+std::wstring(path)+L"\" --autostart";result=RegSetValueExW(key,L"DesktopPhotoFrameNative",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*sizeof(wchar_t)));}
  else {result=RegDeleteValueW(key,L"DesktopPhotoFrameNative");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}RegCloseKey(key);return result==ERROR_SUCCESS;
 }
 void reorderPhotos(int from,int to){
  if(from<0||to<0||size_t(from)>=displayedPhotos.size()||size_t(to)>=displayedPhotos.size()||from==to||SendMessage(control(ExcludedCheck),BM_GETCHECK,0,0)==BST_CHECKED)return;
  auto source=displayedPhotos[from],destination=displayedPhotos[to];auto& order=store.active()["photoOrder"];auto a=std::find(order.begin(),order.end(),Json(utf8(source.wstring()))),b=std::find(order.begin(),order.end(),Json(utf8(destination.wstring())));if(a==order.end()||b==order.end())return;
  int start=int(a-order.begin()),end=int(b-order.begin());if(start<end)std::rotate(order.begin()+start,order.begin()+start+1,order.begin()+end+1);else std::rotate(order.begin()+end,order.begin()+start,order.begin()+start+1);
  ++catalogueGeneration;Store scanned=store;scanned.scan();refreshCatalogue(std::move(scanned));save();updatePhotos();auto moved=std::find(displayedPhotos.begin(),displayedPhotos.end(),source);if(moved!=displayedPhotos.end())SendMessage(control(PhotoBox),LB_SETCURSEL,moved-displayedPhotos.begin(),0);
 }
 void options(){
  if(newSettings){applyV8(true);return;}
  auto& c=store.active()["config"];auto selection=[&](int id,int count){return std::clamp(int(SendMessage(control(id),CB_GETCURSEL,0,0)),0,count-1);};
  c["frame"]=frames[selection(FrameCombo,5)];c["ratio"]=ratios[selection(RatioCombo,7)];c["anchor"]=anchors[selection(AnchorCombo,4)];c["transition"]=effects[selection(EffectCombo,4)];c["order"]=orders[selection(OrderCombo,2)];
  c["interval"]=std::clamp(_wtoi(controlText(control(IntervalEdit)).c_str()),3,300);c["thickness"]=std::clamp(_wtoi(controlText(control(ThicknessEdit)).c_str()),6,64);c["radius"]=std::clamp(_wtoi(controlText(control(RadiusEdit)).c_str()),0,32);
  c["shadow"]=std::clamp(_wtoi(controlText(control(ShadowEdit)).c_str()),0,100);
  auto& g=store.state["global"];g["alwaysOnTop"]=SendMessage(control(TopCheck),BM_GETCHECK,0,0)==BST_CHECKED;g["lockPosition"]=SendMessage(control(LockCheck),BM_GETCHECK,0,0)==BST_CHECKED;
  bool enabled=SendMessage(control(StartupCheck),BM_GETCHECK,0,0)==BST_CHECKED;
  g["snap"]=SendMessage(control(SnapCheck),BM_GETCHECK,0,0)==BST_CHECKED;g["snapMargin"]=std::clamp(_wtoi(controlText(control(SnapMarginEdit)).c_str()),0,64);
  if(enabled!=boolValue(g,"autoStart")){if(!startup(enabled))throw std::runtime_error("无法更改登录自启，请检查 Windows 权限。");g["autoStart"]=enabled;}
  appearance();resetOrder();save();refreshSettings();updateTray();
 }
 void command(int id){
  if(newSettings&&(id==ShowPhoto||id==ExcludePhoto||id==MoveUp||id==MoveDown)){photoCommandV8(id);return;}
  if(id==Settings)showSettings();else if(id==OpenViewer)showViewer();else if(id==AddFiles||id==AddFolder)addSources(id==AddFolder);else if(id==Pause)paused(!frame.paused);else if(id==Hide)visible(!environment.userVisible);else if(id==Quit)exit();
  else if(id==Next)navigate(1);
  else if(id==Previous)navigate(-1);
  else if(id==NewList){auto p=defaultPlaylist(utf8(controlText(control(NameEdit))));if(stringValue(p,"name").empty())p["name"]="新播放列表";store.state["playlists"].push_back(p);store.state=Store::normalize(store.state);activate(stringValue(p,"id"));}
  else if(id==RenameList){auto name=controlText(control(NameEdit));if(!name.empty()){store.active()["name"]=utf8(name.substr(0,60));save();refreshSettings();updateTray();}}
  else if(id==DeleteList){if(store.state["playlists"].size()<2){MessageBoxW(settings,L"至少保留一个播放列表。",L"桌面相框",MB_OK);return;}if(MessageBoxW(settings,L"删除当前播放列表？仅移除配置和路径引用，不删除原图。",L"删除播放列表",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;auto active=stringValue(store.active(),"id");auto& lists=store.state["playlists"];for(auto it=lists.begin();it!=lists.end();++it)if(stringValue(*it,"id")==active){lists.erase(it);break;}activate(stringValue(lists[0],"id"));}
  else if(id==RemoveSource){int i=int(SendMessage(control(SourceBox),LB_GETCURSEL,0,0));auto& sources=store.active()["sources"];if(i>=0&&size_t(i)<sources.size()&&MessageBoxW(settings,L"移除此来源？不会删除原图。",L"移除来源",MB_YESNO|MB_DEFBUTTON2)==IDYES){sources.erase(sources.begin()+i);activate(stringValue(store.active(),"id"));}}
  else if(id==SaveOptions)options();
  else if(id==PickColor){static COLORREF custom[16]{};CHOOSECOLORW picker{sizeof(picker)};picker.hwndOwner=settings;picker.rgbResult=parseFrameColor(stringValue(store.active()["config"],"color"));picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&picker)){char value[8];snprintf(value,sizeof(value),"#%02X%02X%02X",GetRValue(picker.rgbResult),GetGValue(picker.rgbResult),GetBValue(picker.rgbResult));store.active()["config"]["color"]=value;appearance();save();}}
  else if(id==ResetAppearance){auto& c=store.active()["config"];c["thickness"]=18;c["radius"]=10;c["shadow"]=30;c["color"]="#203336";appearance();save();refreshSettings();}
  else if(id==ShowPhoto||id==ExcludePhoto||id==MoveUp||id==MoveDown){int selected=int(SendMessage(control(PhotoBox),LB_GETCURSEL,0,0));if(selected<0||size_t(selected)>=displayedPhotos.size())return;auto selectedPath=displayedPhotos[selected];auto path=utf8(selectedPath.wstring());
   if(SendMessage(control(ExcludedCheck),BM_GETCHECK,0,0)==BST_CHECKED){if(id==ExcludePhoto){auto& excluded=store.active()["excluded"];for(auto it=excluded.begin();it!=excluded.end();)if(pathKey(fs::path(wide(it->get<std::string>())))==pathKey(selectedPath))it=excluded.erase(it);else ++it;activate(stringValue(store.active(),"id"));}return;}
   auto found=std::find(frame.paths.begin(),frame.paths.end(),selectedPath);if(found==frame.paths.end())return;int i=int(found-frame.paths.begin());
   if(id==ShowPhoto)frame.request(i);
   else if(id==ExcludePhoto){if(MessageBoxW(settings,L"将此照片排除出当前列表的轮播？原图保持不变。",L"排除轮播",MB_YESNO|MB_DEFBUTTON2)==IDYES){store.active()["excluded"].push_back(path);activate(stringValue(store.active(),"id"));}}
   else {int other=i+(id==MoveUp?-1:1);if(other<0||size_t(other)>=frame.paths.size())return;auto& order=store.active()["photoOrder"];auto a=std::find(order.begin(),order.end(),Json(path)),b=std::find(order.begin(),order.end(),Json(utf8(frame.paths[other].wstring())));if(a!=order.end()&&b!=order.end())std::iter_swap(a,b);activate(stringValue(store.active(),"id"));SendMessage(control(PhotoBox),LB_SETCURSEL,other,0);}
  }
 }
 void wheel(int delta){
  wheelRemainder+=delta;
  while(wheelRemainder>=WHEEL_DELTA){wheelRemainder-=WHEEL_DELTA;command(Previous);}
  while(wheelRemainder<=-WHEEL_DELTA){wheelRemainder+=WHEEL_DELTA;command(Next);}
 }
 void init(HINSTANCE instance,const fs::path& data,const fs::path& legacy){
  module=instance;dataPath=data;store.load(data/L"settings.json",legacy);
  className=L"NativePhotoFrameHost-"+std::to_wstring(std::hash<std::wstring>{}(pathKey(data)));
  singleton=CreateMutexW(nullptr,FALSE,(L"Local\\"+className).c_str());if(!singleton)throw std::runtime_error("无法创建单实例锁");if(GetLastError()==ERROR_ALREADY_EXISTS){auto existing=FindWindowW(className.c_str(),nullptr);if(existing)PostMessage(existing,RestoreMessage,0,0);throw std::logic_error("already-running");}
  store.save();
  if(!legacy.empty()&&boolValue(store.state["global"],"autoStart")&&!startup(true)){store.state["global"]["autoStart"]=false;store.save();}
  WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=appProcedure;wc.hInstance=module;wc.hIcon=LoadIconW(module,MAKEINTRESOURCEW(1));wc.lpszClassName=className.c_str();wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);wc.lpszClassName=L"NativePhotoFrameSettings";wc.hbrBackground=nullptr;RegisterClassW(&wc);
  auto b=objectValue(store.state,"bounds");RECT wanted{int(numberValue(b,"x",120)),int(numberValue(b,"y",120)),0,0};wanted.right=wanted.left+int(numberValue(b,"width",480));wanted.bottom=wanted.top+int(numberValue(b,"height",360));MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&wanted,MONITOR_DEFAULTTONEAREST),&monitor);
  frame.area={GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};frame.area.right=frame.area.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);frame.area.bottom=frame.area.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
  float mw=float(monitor.rcWork.right-monitor.rcWork.left),mh=float(monitor.rcWork.bottom-monitor.rcWork.top);frame.bounds.w=std::clamp(float(wanted.right-wanted.left),std::min(160.f,mw),mw);frame.bounds.h=std::clamp(float(wanted.bottom-wanted.top),std::min(160.f,mh),mh);frame.bounds.x=std::clamp(float(wanted.left),float(monitor.rcWork.left),monitor.rcWork.right-frame.bounds.w)-frame.area.left;frame.bounds.y=std::clamp(float(wanted.top),float(monitor.rcWork.top),monitor.rcWork.bottom-frame.bounds.h)-frame.area.top;
  frame.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,className.c_str(),L"桌面相框 原生版",WS_POPUP,frame.area.left+int(frame.bounds.x),frame.area.top+int(frame.bounds.y),int(frame.bounds.w),int(frame.bounds.h),nullptr,nullptr,module,nullptr);check(frame.hwnd?S_OK:HRESULT_FROM_WIN32(GetLastError()));SetLayeredWindowAttributes(frame.hwnd,0,255,LWA_ALPHA);
  frame.inputHwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,className.c_str(),L"相框交互区域",WS_POPUP,frame.area.left+std::lround(frame.bounds.x),frame.area.top+std::lround(frame.bounds.y),std::max(1,int(std::lround(frame.bounds.w))+1),std::max(1,int(std::lround(frame.bounds.h))+1),nullptr,nullptr,module,nullptr);check(frame.inputHwnd?S_OK:HRESULT_FROM_WIN32(GetLastError()));SetLayeredWindowAttributes(frame.inputHwnd,0,1,LWA_ALPHA);
  Border=stringValue(store.active()["config"],"frame")=="borderless"?0:float(numberValue(store.active()["config"],"thickness",18));
  try{frame.initialize();}catch(HRESULT hr){
   if(hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET||hr==DXGI_ERROR_DEVICE_HUNG||hr==DXGI_ERROR_DRIVER_INTERNAL_ERROR||hr==D2DERR_RECREATE_TARGET){stopAfterFailure(hr);return;}
   throw;
  }
  frame.chooseNext=[this]{return following();};frame.presented=[this](size_t i){onPresented(i);};frame.paused=boolValue(store.state["global"],"paused");
  frame.preferredPhotoArea=float(numberValue(store.state["global"],"preferredPhotoArea",0));if(frame.preferredPhotoArea<=0){auto& c=store.active()["config"];float border=stringValue(c,"frame")=="borderless"?0:float(numberValue(c,"thickness",18));frame.preferredPhotoArea=std::max(1.f,(frame.bounds.w-2*border)*(frame.bounds.h-2*border));}
  sessionRegistered=WTSRegisterSessionNotification(frame.hwnd,NOTIFY_FOR_THIS_SESSION)!=FALSE;powerNotification=RegisterPowerSettingNotification(frame.hwnd,&GUID_ACDC_POWER_SOURCE,DEVICE_NOTIFY_WINDOW_HANDLE);readPower();
  if(frame.softwareRenderer){try{std::ofstream log(dataPath/L"graphics-recovery.log",std::ios::app);log<<"software renderer started\n";}catch(...){}}
  font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");background=CreateSolidBrush(RGB(245,246,243));panel=CreateSolidBrush(RGB(255,255,255));for(int i=0;i<3;++i)icons[i]=makeIcon(i);taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
  activate(stringValue(store.state,"activePlaylistId"));visible(true);wheelHook=SetWindowsHookExW(WH_MOUSE_LL,mouseWheelProcedure,module,0);if(!wheelHook)OutputDebugStringW(L"桌面相框：鼠标滚轮钩子不可用。\n");startTime=seconds();frame.runStart=startTime;mainTimerInterval=0;refreshTimer();
  if(!metricsPath.empty()){frame.log.open(metricsPath);frame.log<<"seconds,working_set_bytes,private_commit_bytes,transitions,decode_errors,dwm_result,dwm_frame,dwm_dropped,dwm_late,visible,inside_hit,outside_pass,gpu_local_bytes,gpu_nonlocal_bytes,paused,photo_index,loading,animating\n";frame.events.open(metricsPath.wstring()+L".events.csv");frame.events<<"qpc_begin,qpc_duration,anchor,effect,from_x,from_y,from_w,from_h,to_x,to_y,to_w,to_h,surface_upload_ms,prepared_age_ms\n";}
  if(frame.paths.empty())showSettings();
 }
};

static LRESULT CALLBACK mouseWheelProcedure(int code,WPARAM message,LPARAM data){
 if(code==HC_ACTION&&message==WM_MOUSEWHEEL&&app&&app->frame.visible){
  auto mouse=reinterpret_cast<MSLLHOOKSTRUCT*>(data);
  if(WindowFromPoint(mouse->pt)==app->frame.inputHwnd){
   int delta=static_cast<SHORT>(HIWORD(mouse->mouseData));
   PostMessageW(app->frame.hwnd,WheelMessage,WPARAM(delta),0);return 1;
  }
 }
 return CallNextHookEx(app?app->wheelHook:nullptr,code,message,data);
}

static LRESULT CALLBACK photoListProcedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
 if(app){
  if(msg==WM_LBUTTONDOWN){DWORD hit=DWORD(SendMessage(h,LB_ITEMFROMPOINT,0,l));SetFocus(h);SetCapture(h);app->photoDragStart=HIWORD(hit)?-1:LOWORD(hit);app->photoDragPoint={GET_X_LPARAM(l),GET_Y_LPARAM(l)};app->photoDragging=false;if(app->photoDragStart>=0)SendMessage(h,LB_SETCURSEL,app->photoDragStart,0);return 0;}
  if(msg==WM_MOUSEMOVE&&(w&MK_LBUTTON)&&app->photoDragStart>=0){if(abs(GET_X_LPARAM(l)-app->photoDragPoint.x)>6||abs(GET_Y_LPARAM(l)-app->photoDragPoint.y)>6)app->photoDragging=true;if(app->photoDragging)SetCursor(LoadCursor(nullptr,IDC_SIZEALL));return 0;}
  if(msg==WM_LBUTTONUP){bool dragging=app->photoDragging;int start=app->photoDragStart;app->photoDragging=false;app->photoDragStart=-1;DWORD hit=DWORD(SendMessage(h,LB_ITEMFROMPOINT,0,l));ReleaseCapture();if(dragging&&!HIWORD(hit))app->reorderPhotos(start,LOWORD(hit));else if(start>=0&&!HIWORD(hit))app->command(ShowPhoto);return 0;}
  if(msg==WM_LBUTTONDBLCLK){app->command(ShowPhoto);return 0;}
  if(msg==WM_CAPTURECHANGED){app->photoDragging=false;app->photoDragStart=-1;}
 }return DefSubclassProc(h,msg,w,l);
}

static LRESULT CALLBACK appProcedure(HWND h,UINT msg,WPARAM w,LPARAM l){
 if(!app)return DefWindowProcW(h,msg,w,l);
 if(app->recovering&&msg!=WM_DESTROY)return DefWindowProcW(h,msg,w,l);
 try{
  if(h==app->settings&&app->newSettings){auto result=app->settingsMessageV8(msg,w,l);if(result)return *result;}
  if(msg==WM_WTSSESSION_CHANGE){app->sessionEvent(w);return 0;}
  if(msg==WM_POWERBROADCAST){app->powerEvent(w);return TRUE;}
  if(msg==WM_APP+60&&app->testHooks){app->environment.locked=(w&1)!=0;app->environment.sleeping=(w&2)!=0;app->environment.fullscreen=(w&4)!=0;app->environment.onBattery=(w&8)!=0;app->syncEnvironment();return 0;}
  if(msg==WM_APP+61&&app->testHooks&&app->newSettings){app->photoGrid.select(int(w),(l&1)!=0,(l&2)!=0);return 0;}
  if(msg==WM_APP+63&&app->testHooks&&app->newSettings){app->fontsV8(std::clamp(float(w)/100,1.f,2.f));app->settingsScrollY=0;app->layoutV8();return 0;}
  if(msg==WM_APP+65&&app->testHooks)return app->fullscreenWindow(reinterpret_cast<HWND>(l));
  if(msg==WM_APP+62&&app->testHooks){auto& f=app->frame;switch(w){case 0:return f.index;case 1:return std::lround(f.bounds.w);case 2:return std::lround(f.bounds.h);case 3:return std::lround(f.bounds.x);case 4:return std::lround(f.bounds.y);case 5:return app->history.size();case 6:return f.slideDirection;case 7:return (f.visible?1:0)|(f.paused?2:0)|(f.suspended?4:0)|(f.reduceMotion?8:0)|(f.animating?16:0)|(f.loading?32:0);case 8:return app->photoGrid.selected.size();case 9:return app->thumbnails.bytes;case 10:return std::lround(f.preferredPhotoArea);case 11:return app->historyCursor;case 12:return f.transitions;case 13:return app->fullscreenForeground();case 14:return app->timerTicks;case 15:return app->frame.compositionCommits;case 16:return app->paintEvents;case 17:return app->mainTimerInterval;case 20:return 1;case 23:return app->frame.hasPhoto;}return 0;}
  if(msg==app->taskbarCreated&&msg){app->trayAdded=false;app->updateTray();return 0;}
  if(msg==RestoreMessage){app->visible(true);if(app->settings)SetForegroundWindow(app->settings);return 0;}
  if(msg==TrayMessage){if(l==WM_LBUTTONUP)app->visible(true);if(l==WM_RBUTTONUP)app->menu();return 0;}
  if(msg==WheelMessage&&h==app->frame.hwnd){app->wheel(int(INT_PTR(w)));return 0;}
  if(msg==DecodedMessage){app->frame.receive();app->refreshTimer();return 0;}if(msg==WM_TIMER&&h==app->frame.hwnd){++app->timerTicks;if(app->simulateDeviceLoss&&seconds()-app->startTime>1){app->simulateDeviceLoss=false;throw DXGI_ERROR_DEVICE_REMOVED;}app->tick();app->refreshTimer();return 0;}
  if(msg==WM_COMMAND&&(h==app->frame.hwnd||h==app->frame.inputHwnd)){app->command(LOWORD(w));return 0;}
  if(msg==WM_COMMAND&&h==app->settings&&!app->refreshing){int id=LOWORD(w);bool appearanceChoice=id>=FrameCombo&&id<=OrderCombo&&HIWORD(w)==CBN_SELCHANGE;bool appearanceNumber=(id==ThicknessEdit||id==RadiusEdit||id==ShadowEdit)&&HIWORD(w)==EN_KILLFOCUS;if(appearanceChoice||appearanceNumber){app->options();return 0;}}
  if(msg==WM_COMMAND&&h==app->settings){int id=LOWORD(w);if((id==SearchEdit&&HIWORD(w)==EN_CHANGE)||(id==ExcludedCheck&&HIWORD(w)==BN_CLICKED)){if(!app->refreshing)app->updatePhotos();}else if(id==PlaylistBox&&HIWORD(w)==LBN_SELCHANGE&&!app->refreshing)app->switchByIndex(int(SendMessage(app->control(PlaylistBox),LB_GETCURSEL,0,0)));else if(id==PhotoBox&&HIWORD(w)==LBN_DBLCLK)app->command(ShowPhoto);else if(HIWORD(w)==BN_CLICKED&&id<100)app->command(id);return 0;}
  if(msg==WM_CLOSE){if(h==app->settings){RECT r;GetWindowRect(h,&r);app->store.state["settingsBounds"]={{"x",r.left},{"y",r.top},{"width",r.right-r.left},{"height",r.bottom-r.top}};app->save();DestroyWindow(h);app->settings=nullptr;app->thumbnails.clear();app->displayedPhotos.clear();}else app->visible(false);return 0;}
  if(msg==WM_MEASUREITEM){auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(l);if(item->CtlID==PhotoBox){item->itemHeight=76;return TRUE;}}
  if(msg==WM_DRAWITEM&&h==app->settings){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlID==PhotoBox&&item->itemID<app->displayedPhotos.size()){auto path=app->displayedPhotos[item->itemID];bool current=app->frame.hasPhoto&&app->frame.index<app->frame.paths.size()&&app->frame.paths[app->frame.index]==path;app->thumbnails.draw(*item,path,app->font,current);return TRUE;}}
  if(msg==WM_DRAWITEM&&h==app->settings){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlID==FramePreview){
   FillRect(item->hDC,&item->rcItem,app->background);auto& frame=app->frame;Thumbnails::Entry* photo=nullptr;if(frame.hasPhoto&&frame.index<frame.paths.size())photo=app->thumbnails.find(frame.paths[frame.index]);
   float scale=std::min(float(item->rcItem.right-item->rcItem.left-20)/frame.bounds.w,float(item->rcItem.bottom-item->rcItem.top-12)/frame.bounds.h);int width=int(frame.bounds.w*scale),height=int(frame.bounds.h*scale),x=(item->rcItem.right-width)/2,y=(item->rcItem.bottom-height)/2,border=int(Border*scale);
   auto& config=app->store.active()["config"];auto material=frameMaterial(comboIndex(config,"frame",frames,5),parseFrameColor(stringValue(config,"color")));BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=256;info.bmiHeader.biHeight=-256;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
   SaveDC(item->hDC);auto region=CreateRoundRectRgn(x,y,x+width+1,y+height+1,int(frame.cornerRadius*scale*2),int(frame.cornerRadius*scale*2));SelectClipRgn(item->hDC,region);DeleteObject(region);StretchDIBits(item->hDC,x,y,width,height,0,0,256,256,material.data(),&info,DIB_RGB_COLORS,SRCCOPY);
   if(photo&&photo->bitmap){int iw=width-2*border,ih=height-2*border;float imageScale=std::min(float(iw)/photo->width,float(ih)/photo->height);int pw=int(photo->width*imageScale),ph=int(photo->height*imageScale);HDC memory=CreateCompatibleDC(item->hDC);auto previous=SelectObject(memory,photo->bitmap);SetStretchBltMode(item->hDC,HALFTONE);StretchBlt(item->hDC,x+border+(iw-pw)/2,y+border+(ih-ph)/2,pw,ph,memory,0,0,photo->width,photo->height,SRCCOPY);SelectObject(memory,previous);DeleteDC(memory);}RestoreDC(item->hDC,-1);return TRUE;
  }}
  if(msg==WM_SIZE&&h==app->settings){app->settingsViewport();return 0;}
  if(msg==WM_MOUSEWHEEL&&h==app->settings){for(int i=0;i<3;++i)app->scrollSettings(SB_VERT,GET_WHEEL_DELTA_WPARAM(w)>0?SB_LINEUP:SB_LINEDOWN);return 0;}
  if(msg==WM_DISPLAYCHANGE&&h==app->frame.hwnd){auto& frame=app->frame;frame.cancel();float x=frame.bounds.x+frame.area.left,y=frame.bounds.y+frame.area.top;frame.area={GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};frame.area.right=frame.area.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);frame.area.bottom=frame.area.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);frame.bounds.x=x-frame.area.left;frame.bounds.y=y-frame.area.top;frame.keepInWorkArea();frame.carrierReady=false;frame.applyGeometry(frame.bounds,false);if(frame.hasPhoto)frame.pictureGeometry(frame.current,frame.bounds,frame.bounds,false,true);frame.updateRegion(frame.bounds);frame.commit();app->save();return 0;}
  if((msg==WM_HSCROLL||msg==WM_VSCROLL)&&h==app->settings){app->scrollSettings(msg==WM_HSCROLL?SB_HORZ:SB_VERT,LOWORD(w));return 0;}
  if(msg==WM_GETMINMAXINFO&&h==app->settings){auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={std::min(640L,app->frame.area.right-app->frame.area.left),std::min(480L,app->frame.area.bottom-app->frame.area.top)};return 0;}
  if(msg==WM_PAINT){if(h==app->frame.hwnd||h==app->frame.inputHwnd)++app->paintEvents;PAINTSTRUCT paint;HDC dc=BeginPaint(h,&paint);if(h==app->frame.inputHwnd)FillRect(dc,&paint.rcPaint,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));else if(h==app->settings){FillRect(dc,&paint.rcPaint,app->background);SetViewportOrgEx(dc,-app->settingsScrollX,-app->settingsScrollY,nullptr);RECT left{12,12,222,644},center{224,12,642,644},right{644,12,940,844};FillRect(dc,&left,app->panel);FillRect(dc,&center,app->panel);FillRect(dc,&right,app->panel);}EndPaint(h,&paint);return 0;}
  if((msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLORLISTBOX)&&h==app->settings){auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,RGB(32,51,54));SetBkColor(dc,RGB(255,255,255));return reinterpret_cast<LRESULT>(app->panel);}
  if(msg==WM_ERASEBKGND)return 1;
  if(h==app->frame.inputHwnd||h==app->frame.hwnd){
   // 鼠标消息采用屏幕坐标还原为合成画布坐标，交互窗口移动时拖拽仍保持连续。
   POINT pointer{GET_X_LPARAM(l),GET_Y_LPARAM(l)},screen=pointer;
   if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_LBUTTONDBLCLK||msg==WM_MOUSEMOVE){
    ClientToScreen(h,&screen);
    // 拖动时窗口原点也在变化，直接读取屏幕位置，避免排队消息中的旧客户区坐标造成回跳。
    if(msg==WM_MOUSEMOVE&&(app->frame.dragging||app->frame.resizing)){POINT current{};if(GetCursorPos(&current))screen=current;}
    pointer.x=screen.x-app->frame.area.left;pointer.y=screen.y-app->frame.area.top;
   }
   if(msg==WM_RBUTTONUP){app->menu();return 0;}
   if(msg==WM_LBUTTONDBLCLK){if(app->frameDoubleClick(h,screen))app->showViewer();return 0;}
   if(msg==WM_LBUTTONDOWN){if(h==app->frame.inputHwnd)app->frameMouseDown(h,screen);int x=pointer.x,y=pointer.y;bool locked=boolValue(app->store.state["global"],"lockPosition");if(!locked||app->frame.resizeHit(x,y)){app->frame.mouseDown(x,y,!locked);return 0;}}
   if(msg==WM_MOUSEMOVE){
    int axes=app->frame.resizing?app->frame.resizeAxes:app->frame.resizeHit(pointer.x,pointer.y);
    bool horizontal=axes&(Frame::ResizeLeft|Frame::ResizeRight),vertical=axes&(Frame::ResizeTop|Frame::ResizeBottom);
    LPCWSTR cursor=IDC_ARROW;
    if(horizontal&&vertical)cursor=((axes&Frame::ResizeLeft)&&(axes&Frame::ResizeTop))||((axes&Frame::ResizeRight)&&(axes&Frame::ResizeBottom))?IDC_SIZENWSE:IDC_SIZENESW;
    else if(horizontal)cursor=IDC_SIZEWE;else if(vertical)cursor=IDC_SIZENS;
    if(axes||!(w&MK_LBUTTON))SetCursor(LoadCursor(nullptr,cursor));
    app->frame.mouseMove(pointer.x,pointer.y);return 0;
   }
   if(msg==WM_LBUTTONUP){app->frameMouseUp(h,screen);app->endDrag();return 0;}
   if(msg==WM_KEYDOWN){if(w==VK_SPACE)app->command(Pause);if(w==VK_RIGHT)app->command(Next);if(w==VK_LEFT)app->command(Previous);if(w==VK_ESCAPE)app->visible(false);if(w=='S')app->showSettings();return 0;}
  }
  if(msg==WM_DESTROY&&h==app->frame.hwnd){app->unregisterEnvironment();KillTimer(h,TimerId);DestroyWindow(app->frame.inputHwnd);PostQuitMessage(0);return 0;}
 }catch(const std::exception& e){OutputDebugStringW(wide(e.what()).c_str());app->stopAfterFailure(E_FAIL);}catch(HRESULT hr){app->stopAfterFailure(hr);}
 return DefWindowProcW(h,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE,PWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);QueryPerformanceFrequency(&Frequency);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=0;
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
 try{
  PWSTR roaming=nullptr;check(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roaming));fs::path root=roaming;CoTaskMemFree(roaming);fs::path data=root/L"DesktopPhotoFrameNative",legacy=root/L"desktop-photo-frame"/L"settings.json";
  Application application;app=&application;DWORD recoverFrom=0;int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;++i){std::wstring arg=args[i];if(arg==L"--data-dir"&&i+1<argc){data=args[++i];legacy.clear();}else if(arg==L"--test-seconds"&&i+1<argc)application.testSeconds=_wtof(args[++i]);else if(arg==L"--metrics"&&i+1<argc)application.metricsPath=args[++i];else if(arg==L"--stress")application.frame.benchmark=true;else if(arg==L"--software-renderer")application.frame.softwareRenderer=true;else if(arg==L"--recover-from"&&i+1<argc)recoverFrom=wcstoul(args[++i],nullptr,10);else if(arg==L"--simulate-device-loss")application.simulateDeviceLoss=true;else if(arg==L"--test-hooks")application.testHooks=true;else if(arg==L"--no-tray")application.noTray=true;}LocalFree(args);
  // 自动化运行不登记系统托盘，避免临时构建和隔离安装目录留下用户历史记录。
  application.noTray=application.noTray||application.testHooks||application.testSeconds>0||application.frame.benchmark||application.simulateDeviceLoss;
  if(recoverFrom){HANDLE previous=OpenProcess(SYNCHRONIZE,FALSE,recoverFrom);if(previous){DWORD result=WaitForSingleObject(previous,30000);CloseHandle(previous);if(result!=WAIT_OBJECT_0)throw std::runtime_error("等待旧相框进程退出超时");}}
  application.init(module,data,legacy);MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
   if(application.viewer.hwnd&&(message.hwnd==application.viewer.hwnd||IsChild(application.viewer.hwnd,message.hwnd))){if(message.message==WM_KEYDOWN&&(message.wParam==VK_ESCAPE||message.wParam=='0'||message.wParam=='1')){SendMessage(application.viewer.hwnd,WM_KEYDOWN,message.wParam,message.lParam);continue;}if(IsDialogMessageW(application.viewer.hwnd,&message))continue;}
   if(application.settings&&application.newSettings&&message.message==WM_KEYDOWN&&message.wParam==VK_RETURN){int id=GetDlgCtrlID(message.hwnd);if(id==ThicknessEdit||id==RadiusEdit||id==ShadowEdit||id==IntervalEdit||id==MaxWidthEdit||id==MaxHeightEdit||id==SnapMarginEdit){application.applyV8(true);continue;}}
   if(application.settings&&IsDialogMessageW(application.settings,&message))continue;TranslateMessage(&message);DispatchMessageW(&message);application.refreshTimer();
  }code=int(message.wParam);app=nullptr;
 }catch(const std::logic_error& e){if(std::string(e.what())!="already-running"){MessageBoxW(nullptr,wide(e.what()).c_str(),L"桌面相框",MB_ICONERROR);code=1;}}
 catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"桌面相框启动失败",MB_ICONERROR);code=1;}
 catch(HRESULT hr){wchar_t message[100];swprintf(message,100,L"启动失败（0x%08lX）。",static_cast<unsigned long>(hr));MessageBoxW(nullptr,message,L"桌面相框",MB_ICONERROR);code=1;}
 CoUninitialize();return code;
}
