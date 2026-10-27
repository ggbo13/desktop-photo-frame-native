 // 图片使用独立子窗口，Direct2D 提交不再覆盖顶部的原生按钮。
 HWND canvas=nullptr;
 void invalidate(){if(canvas)InvalidateRect(canvas,nullptr,FALSE);}
 static LRESULT CALLBACK canvasProcedure(HWND h,UINT message,WPARAM w,LPARAM l){
  auto self=reinterpret_cast<PhotoViewer*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<PhotoViewer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->canvas=h;}if(!self)return DefWindowProcW(h,message,w,l);
  try{
   if(message==WM_ERASEBKGND)return 1;
   if(message==WM_PAINT){self->paintCanvas();return 0;}
   if(message==WM_SIZE){UINT width=LOWORD(l),height=HIWORD(l);if(!width||!height)return 0;if(self->target){auto size=self->target->GetPixelSize();if(size.width!=width||size.height!=height){HRESULT hr=self->target->Resize(D2D1::SizeU(width,height));if(FAILED(hr)){self->bitmap.Reset();self->target.Reset();self->factory.Reset();self->pending=true;}}}if(self->fitting)self->fit();self->invalidate();
    // Resize 后缓冲内容无定义，在本次尺寸消息内补齐图片，避免等到下次空闲才绘制。
    UpdateWindow(h);return 0;}
   if(message==WM_MOUSEWHEEL)return SendMessage(self->hwnd,message,w,l);
   if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_MOUSEMOVE)return SendMessage(self->hwnd,message,w,MAKELPARAM(GET_X_LPARAM(l),GET_Y_LPARAM(l)+44));
   if(message==WM_NCDESTROY){self->canvas=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);}
  }catch(...){self->status=L"照片暂时无法显示，请关闭后重试。";return 0;}
  return DefWindowProcW(h,message,w,l);
 }
 void createCanvas(HINSTANCE module){WNDCLASSW wc{};wc.lpfnWndProc=canvasProcedure;wc.hInstance=module;wc.lpszClassName=L"NativePhotoFrameViewerCanvas";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);RECT r{};GetClientRect(hwnd,&r);if(!CreateWindowExW(0,wc.lpszClassName,L"照片画布",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,44,std::max(1L,r.right),std::max(1L,r.bottom-44),hwnd,nullptr,module,this))throw HRESULT_FROM_WIN32(GetLastError());}
 void resources(){if(target||!canvas)return;check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));RECT r{};GetClientRect(canvas,&r);auto properties=D2D1::RenderTargetProperties();properties.dpiX=properties.dpiY=96;check(factory->CreateHwndRenderTarget(properties,D2D1::HwndRenderTargetProperties(canvas,D2D1::SizeU(std::max(1L,r.right),std::max(1L,r.bottom))),&target));}
 void paint(){PAINTSTRUCT p{};HDC dc=BeginPaint(hwnd,&p);HBRUSH brush=CreateSolidBrush(RGB(23,31,33));FillRect(dc,&p.rcPaint,brush);DeleteObject(brush);EndPaint(hwnd,&p);}
 void paintCanvas(){PAINTSTRUCT p{};BeginPaint(canvas,&p);try{resources();target->BeginDraw();target->Clear(D2D1::ColorF(.09f,.12f,.13f));auto size=target->GetSize();if(bitmap){float width=originalWidth*zoom,height=originalHeight*zoom,cx=size.width/2+offsetX,cy=size.height/2+offsetY;target->DrawBitmap(bitmap.Get(),D2D1::RectF(cx-width/2,cy-height/2,cx+width/2,cy+height/2),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}HRESULT hr=target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){bitmap.Reset();target.Reset();factory.Reset();pending=true;}else check(hr);if(!status.empty()){SetBkMode(p.hdc,TRANSPARENT);SetTextColor(p.hdc,RGB(225,235,232));RECT r{};GetClientRect(canvas,&r);DrawTextW(p.hdc,status.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}}catch(...){EndPaint(canvas,&p);throw;}EndPaint(canvas,&p);}
