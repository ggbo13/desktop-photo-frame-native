 PlaybackEnvironment environment;
 bool sessionRegistered=false,syncingEnvironment=false,testHooks=false;HPOWERNOTIFY powerNotification=nullptr;
 void unregisterEnvironment(){if(sessionRegistered){WTSUnRegisterSessionNotification(frame.hwnd);sessionRegistered=false;}if(powerNotification){UnregisterPowerSettingNotification(powerNotification);powerNotification=nullptr;}}
 double lastEnvironmentCheck=0;int fullscreenClearCount=0;
 void syncEnvironment(){
  if(syncingEnvironment||!frame.composition)return;syncingEnvironment=true;
  try{
   bool show=environment.visible(),suspend=environment.suspended();
   bool motion=environment.onBattery&&boolValue(store.state["global"],"batteryReduceMotion",true);
   if(environment.autoHidden()&&viewer.hwnd){viewer.close();environment.viewer=false;suspend=environment.suspended();}
   if((!show&&frame.visible)||(suspend&&!frame.suspended)||(motion&&!frame.reduceMotion)){frame.cancel();resetNavigation();}
   bool resumed=(show&&!frame.visible)||(!suspend&&frame.suspended);
   frame.visible=show;frame.suspended=suspend;frame.reduceMotion=motion;
   // 先显示绘图窗口，再显示较小的交互窗口，让相框区域稳定接收鼠标事件。
   for(HWND h:{frame.hwnd,frame.inputHwnd})if(h){ShowWindow(h,show?SW_SHOWNOACTIVATE:SW_HIDE);if(show)ShowWindow(h,SW_SHOWNOACTIVATE);}
   if(resumed){frame.next=seconds()+frame.playInterval;topmost();}
   updateTray();syncingEnvironment=false;
  }catch(...){syncingEnvironment=false;throw;}
 }
 void readPower(){SYSTEM_POWER_STATUS power{};if(GetSystemPowerStatus(&power)&&power.ACLineStatus!=255)environment.onBattery=power.ACLineStatus==0;}
 bool fullscreenWindow(HWND foreground){
  if(!foreground||!IsWindowVisible(foreground)||IsIconic(foreground))return false;
  DWORD process=0;GetWindowThreadProcessId(foreground,&process);if(process==GetCurrentProcessId())return false;
  if(foreground==GetShellWindow()||foreground==GetDesktopWindow())return false;
  wchar_t type[128]{};GetClassNameW(foreground,type,128);if(wcscmp(type,L"Progman")==0||wcscmp(type,L"WorkerW")==0||wcscmp(type,L"Shell_TrayWnd")==0)return false;
  if(GetWindowLongPtrW(foreground,GWL_STYLE)&WS_CAPTION)return false;
  RECT photo{LONG(frame.area.left+frame.bounds.x),LONG(frame.area.top+frame.bounds.y),LONG(frame.area.left+frame.bounds.x+frame.bounds.w),LONG(frame.area.top+frame.bounds.y+frame.bounds.h)};
  auto monitor=MonitorFromRect(&photo,MONITOR_DEFAULTTONEAREST);if(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST)!=monitor)return false;
  MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);RECT rect{};
  if(FAILED(DwmGetWindowAttribute(foreground,DWMWA_EXTENDED_FRAME_BOUNDS,&rect,sizeof(rect))))GetWindowRect(foreground,&rect);
  return rect.left<=info.rcMonitor.left+2&&rect.top<=info.rcMonitor.top+2&&rect.right>=info.rcMonitor.right-2&&rect.bottom>=info.rcMonitor.bottom-2;
 }
 bool fullscreenForeground(){return fullscreenWindow(GetForegroundWindow());}
 void pollEnvironment(){
  if(testHooks)return;
  double now=seconds();if(now-lastEnvironmentCheck<1)return;lastEnvironmentCheck=now;
  bool fullscreen=boolValue(store.state["global"],"pauseFullscreen",true)&&fullscreenForeground();bool before=environment.fullscreen;
  if(fullscreen){environment.fullscreen=true;fullscreenClearCount=0;}else if(++fullscreenClearCount>=2)environment.fullscreen=false;
  if(before!=environment.fullscreen)syncEnvironment();
 }
 void sessionEvent(WPARAM event){if(event==WTS_SESSION_LOCK)environment.locked=true;else if(event==WTS_SESSION_UNLOCK)environment.locked=false;else return;syncEnvironment();}
 void powerEvent(WPARAM event){if(event==PBT_APMSUSPEND)environment.sleeping=true;else if(event==PBT_APMRESUMEAUTOMATIC||event==PBT_APMRESUMESUSPEND)environment.sleeping=false;readPower();syncEnvironment();}
 std::wstring environmentStatus(){
  if(environment.locked)return L"锁屏暂停";if(environment.sleeping)return L"休眠暂停";if(environment.fullscreen)return L"全屏暂停";
  if(!environment.userVisible)return L"已隐藏";if(environment.viewer)return L"查看大图";if(frame.paused)return L"已暂停";
  return frame.reduceMotion?L"电池节能":L"播放中";
 }
