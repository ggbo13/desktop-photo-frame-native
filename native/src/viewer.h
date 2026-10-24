#pragma once
#include "image-loader.h"

class PhotoViewer {
public:
 HWND hwnd=nullptr;ComPtr<ID2D1Factory> factory;ComPtr<ID2D1HwndRenderTarget> target;ComPtr<ID2D1Bitmap> bitmap;
 std::future<LoadedImage> job;fs::path requestedPath;unsigned generation=0,jobGeneration=0;
 UINT originalWidth=0,originalHeight=0;float zoom=1,offsetX=0,offsetY=0;bool fitting=true,dragging=false,pending=false;POINT last{};
 std::wstring status;std::function<void()> closed;
 ~PhotoViewer(){if(hwnd)DestroyWindow(hwnd);if(job.valid())job.wait();}
 static LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l){
  auto self=reinterpret_cast<PhotoViewer*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<PhotoViewer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->hwnd=h;}
  if(!self)return DefWindowProcW(h,message,w,l);
  try{
   if(message==WM_PAINT){self->paint();return 0;}
   if(message==WM_ERASEBKGND)return 1;
   if(message==WM_SIZE){if(self->target)self->target->Resize(D2D1::SizeU(LOWORD(l),HIWORD(l)));if(self->fitting)self->fit();InvalidateRect(h,nullptr,FALSE);return 0;}
   if(message==WM_COMMAND){if(LOWORD(w)==1)self->fit();else if(LOWORD(w)==2){self->zoom=1;self->offsetX=self->offsetY=0;self->fitting=false;}else if(LOWORD(w)==3)self->close();if(self->hwnd)InvalidateRect(h,nullptr,FALSE);return 0;}
   if(message==WM_KEYDOWN){if(w==VK_ESCAPE)self->close();else if(w=='0')self->fit();else if(w=='1'){self->zoom=1;self->fitting=false;self->offsetX=self->offsetY=0;InvalidateRect(h,nullptr,FALSE);}return 0;}
   if(message==WM_MOUSEWHEEL){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);float cx=r.right/2.f,cy=44+(r.bottom-44)/2.f;float next=std::clamp(self->zoom*std::pow(1.15f,GET_WHEEL_DELTA_WPARAM(w)/120.f),.1f,8.f);float factor=next/self->zoom;self->offsetX=(self->offsetX-(p.x-cx))*factor+(p.x-cx);self->offsetY=(self->offsetY-(p.y-cy))*factor+(p.y-cy);self->zoom=next;self->fitting=false;InvalidateRect(h,nullptr,FALSE);return 0;}
   if(message==WM_LBUTTONDOWN&&GET_Y_LPARAM(l)>44){self->dragging=true;self->last={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(h);SetFocus(h);return 0;}
   if(message==WM_MOUSEMOVE&&self->dragging){self->offsetX+=GET_X_LPARAM(l)-self->last.x;self->offsetY+=GET_Y_LPARAM(l)-self->last.y;self->last={GET_X_LPARAM(l),GET_Y_LPARAM(l)};self->fitting=false;InvalidateRect(h,nullptr,FALSE);return 0;}
   if(message==WM_LBUTTONUP){self->dragging=false;ReleaseCapture();return 0;}
   if(message==WM_CAPTURECHANGED){self->dragging=false;return 0;}
   if(message==WM_CLOSE){self->close();return 0;}
   if(message==WM_NCDESTROY){self->hwnd=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);}
  }catch(...){self->status=L"照片暂时无法显示，请关闭后重试。";}
  return DefWindowProcW(h,message,w,l);
 }
 void open(HINSTANCE module,const fs::path& path,RECT area,HFONT font){
  if(hwnd){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);return;}
  WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=module;wc.lpszClassName=L"NativePhotoFrameViewer";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
  int width=std::min(1100L,area.right-area.left),height=std::min(800L,area.bottom-area.top);
  CreateWindowExW(0,wc.lpszClassName,(L"查看大图 · "+path.filename().wstring()).c_str(),WS_OVERLAPPEDWINDOW,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,nullptr,nullptr,module,this);
  if(!hwnd)throw HRESULT_FROM_WIN32(GetLastError());
  int x=12;for(auto [id,title]:std::initializer_list<std::pair<int,const wchar_t*>>{{1,L"适应窗口"},{2,L"原始尺寸"},{3,L"关闭预览"}}){auto button=CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP,x,8,100,28,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),module,nullptr);SendMessage(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);x+=112;}
  ++generation;requestedPath=path;pending=true;fitting=true;originalWidth=originalHeight=0;offsetX=offsetY=0;status=L"正在载入照片…";ShowWindow(hwnd,SW_SHOW);ShowWindow(hwnd,SW_SHOW);SetForegroundWindow(hwnd);
 }
 void close(){if(!hwnd)return;++generation;pending=false;auto window=hwnd;DestroyWindow(window);bitmap.Reset();target.Reset();factory.Reset();originalWidth=originalHeight=0;if(closed)closed();}
 void resources(){if(target)return;check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));RECT r;GetClientRect(hwnd,&r);auto properties=D2D1::RenderTargetProperties();properties.dpiX=properties.dpiY=96;check(factory->CreateHwndRenderTarget(properties,D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(std::max(1L,r.right),std::max(1L,r.bottom))),&target));}
 void tick(){
  if(job.valid()&&job.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto image=job.get();if(hwnd&&jobGeneration==generation){if(SUCCEEDED(image.error)){resources();check(target->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),image.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&bitmap));originalWidth=image.originalWidth;originalHeight=image.originalHeight;status=L"";fit();}else status=L"无法读取此照片。请检查文件或图片格式。";InvalidateRect(hwnd,nullptr,FALSE);}}
  if(hwnd&&pending&&!job.valid()){pending=false;jobGeneration=generation;auto path=requestedPath;job=std::async(std::launch::async,[path]{return loadImage(path,32000000);});}
 }
 void fit(){if(!hwnd||!originalWidth||!originalHeight)return;RECT r;GetClientRect(hwnd,&r);zoom=std::clamp(std::min(float(r.right)/originalWidth,float(std::max(1L,r.bottom-44))/originalHeight),.1f,8.f);offsetX=offsetY=0;fitting=true;InvalidateRect(hwnd,nullptr,FALSE);}
 void paint(){
  PAINTSTRUCT p;BeginPaint(hwnd,&p);try{resources();target->BeginDraw();target->Clear(D2D1::ColorF(.09f,.12f,.13f));auto size=target->GetSize();target->PushAxisAlignedClip(D2D1::RectF(0,44,size.width,size.height),D2D1_ANTIALIAS_MODE_ALIASED);
   if(bitmap){float w=originalWidth*zoom,h=originalHeight*zoom,cx=size.width/2+offsetX,cy=44+(size.height-44)/2+offsetY;target->DrawBitmap(bitmap.Get(),D2D1::RectF(cx-w/2,cy-h/2,cx+w/2,cy+h/2),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}target->PopAxisAlignedClip();HRESULT hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){bitmap.Reset();target.Reset();factory.Reset();pending=true;}
   if(!status.empty()){SetBkMode(p.hdc,TRANSPARENT);SetTextColor(p.hdc,RGB(225,235,232));RECT r;GetClientRect(hwnd,&r);r.top=44;DrawTextW(p.hdc,status.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
  }catch(...){EndPaint(hwnd,&p);throw;}EndPaint(hwnd,&p);
 }
};
