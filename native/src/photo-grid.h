#pragma once

// 网格与列表共享一份路径选择集，仅绘制可见照片，不为每张照片创建窗口。
class PhotoGrid {
public:
 HWND hwnd=nullptr;HFONT font=nullptr;Thumbnails* thumbnails=nullptr;
 UiPalette palette;HFONT smallFont=nullptr;
 std::vector<fs::path> paths;std::set<std::wstring> selected;
 fs::path current;bool grid=true,excluded=false,dragging=false,mousePressed=false,pressCtrl=false,pressShift=false;
 int focus=-1,anchor=-1,scroll=0,drop=-1;POINT press{},dragPoint{};float dpi=1;
 static constexpr UINT_PTR DragScrollTimer=0x5A90;
 std::function<void(int)> moveSelection;
 std::function<void(int)> play;std::function<void(const std::vector<fs::path>&,const fs::path&)> reorder;std::function<void()> changed;
 int unit(float v)const{return std::lround(v*dpi);}
 int columns()const{RECT r{};GetClientRect(hwnd,&r);return grid?std::max(1,int(r.right)/unit(166)):1;}
 int rowHeight()const{return unit(grid?174:64);}
 RECT tile(int i)const{RECT r{};GetClientRect(hwnd,&r);int cols=columns(),width=r.right/cols,row=i/cols;return {i%cols*width+unit(4),row*rowHeight()-scroll+unit(4),(i%cols+1)*width-unit(4),(row+1)*rowHeight()-scroll-unit(4)};}
 int hit(int x,int y)const{RECT r{};GetClientRect(hwnd,&r);if(x<0||x>=r.right||y<0||y>=r.bottom||paths.empty())return -1;int cols=columns();int i=(y+scroll)/rowHeight()*cols+std::min(cols-1,x/std::max(1,int(r.right)/cols));return i<int(paths.size())?i:-1;}
 void viewport(){if(!hwnd)return;RECT r{};GetClientRect(hwnd,&r);int total=((int(paths.size())+columns()-1)/columns())*rowHeight();scroll=std::clamp(scroll,0,std::max(0,total-int(r.bottom)));SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMin=0;info.nMax=std::max(0,total-1);info.nPage=r.bottom;info.nPos=scroll;SetScrollInfo(hwnd,SB_VERT,&info,TRUE);InvalidateRect(hwnd,nullptr,FALSE);}
 void ensureFocus(){if(focus<0)return;auto r=tile(focus);RECT client;GetClientRect(hwnd,&client);if(r.top<0)scroll+=r.top-unit(4);if(r.bottom>client.bottom)scroll+=r.bottom-client.bottom+unit(4);viewport();}
 std::vector<fs::path> selectedPaths()const{std::vector<fs::path> result;for(auto& p:paths)if(selected.contains(pathKey(p)))result.push_back(p);return result;}
 std::vector<fs::path> visiblePaths()const{std::vector<fs::path> result;if(!hwnd||!IsWindowVisible(hwnd))return result;RECT r{};GetClientRect(hwnd,&r);int cols=columns(),first=std::max(0,scroll/rowHeight()-1)*cols,last=std::min(int(paths.size()),((scroll+int(r.bottom))/rowHeight()+2)*cols);for(int i=first;i<last;++i)result.push_back(paths[i]);return result;}
 void setPaths(std::vector<fs::path> value){fs::path focused=focus>=0&&focus<int(paths.size())?paths[focus]:fs::path{};paths=std::move(value);std::set<std::wstring> valid;for(auto& p:paths)valid.insert(pathKey(p));for(auto it=selected.begin();it!=selected.end();)if(!valid.contains(*it))it=selected.erase(it);else ++it;auto f=std::find(paths.begin(),paths.end(),focused);focus=f==paths.end()?-1:int(f-paths.begin());anchor=focus;viewport();}
 void select(int i,bool ctrl,bool shift){if(i<0||i>=int(paths.size()))return;if(shift&&anchor>=0){if(!ctrl)selected.clear();for(int j=std::min(anchor,i);j<=std::max(anchor,i);++j)selected.insert(pathKey(paths[j]));}else if(ctrl){auto key=pathKey(paths[i]);if(selected.contains(key))selected.erase(key);else selected.insert(key);anchor=i;}else{selected.clear();selected.insert(pathKey(paths[i]));anchor=i;}focus=i;InvalidateRect(hwnd,nullptr,FALSE);if(changed)changed();}
 void updateDrop(){
  RECT client{};GetClientRect(hwnd,&client);int x=std::clamp(int(dragPoint.x),0,std::max(0,int(client.right)-1)),y=std::clamp(int(dragPoint.y),0,std::max(0,int(client.bottom)-1));int i=hit(x,y);
  if(i<0)drop=paths.empty()?-1:int(paths.size());else{auto r=tile(i);bool after=grid?x>(r.left+r.right)/2:y>(r.top+r.bottom)/2;drop=i+(after?1:0);}
  InvalidateRect(hwnd,nullptr,FALSE);
 }
 void scrollDrag(){
  if(!dragging)return;RECT client{};GetClientRect(hwnd,&client);int edge=unit(28),step=unit(14),old=scroll;
  if(dragPoint.y<edge)scroll-=step;else if(dragPoint.y>client.bottom-edge)scroll+=step;
  if(old!=scroll){viewport();updateDrop();}
 }
 void create(HWND parent,HINSTANCE module,int id,HFONT f,Thumbnails& cache){font=f;thumbnails=&cache;WNDCLASSW wc{};wc.hInstance=module;wc.lpfnWndProc=procedure;wc.lpszClassName=L"NativePhotoGridV8";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);hwnd=CreateWindowExW(0,wc.lpszClassName,L"照片缩略图",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_CLIPSIBLINGS,0,0,100,100,parent,reinterpret_cast<HMENU>(INT_PTR(id)),module,this);}
 #include "photo-grid-paint-v15.inc"
 void paintLegacy(){PAINTSTRUCT ps;HDC screen=BeginPaint(hwnd,&ps);RECT client;GetClientRect(hwnd,&client);if(client.right<=0||client.bottom<=0){EndPaint(hwnd,&ps);return;}HDC dc=CreateCompatibleDC(screen);HBITMAP buffer=CreateCompatibleBitmap(screen,client.right,client.bottom);auto old=SelectObject(dc,buffer);FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
  if(paths.empty()){SetTextColor(dc,RGB(83,110,109));DrawTextW(dc,excluded?L"没有已排除的照片":L"添加照片或文件夹，或调整搜索条件",-1,&client,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
  int cols=columns(),first=std::max(0,scroll/rowHeight())*cols,last=std::min(int(paths.size()),((scroll+int(client.bottom))/rowHeight()+1)*cols);
  for(int i=first;i<last;++i){RECT box=tile(i);bool chosen=selected.contains(pathKey(paths[i])),shown=pathKey(paths[i])==pathKey(current);HBRUSH fill=CreateSolidBrush(chosen?RGB(221,239,237):RGB(245,246,243));FillRect(dc,&box,fill);DeleteObject(fill);
   RECT preview=box;InflateRect(&preview,-unit(7),-unit(7));if(grid)preview.bottom=box.bottom-unit(48);else preview.right=preview.left+unit(88);
   auto entry=thumbnails?thumbnails->find(paths[i]):nullptr;
   if(entry&&entry->bitmap){float scale=std::min(float(preview.right-preview.left)/entry->width,float(preview.bottom-preview.top)/entry->height);int w=int(entry->width*scale),h=int(entry->height*scale);HDC memory=CreateCompatibleDC(dc);auto previous=SelectObject(memory,entry->bitmap);SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,preview.left+(preview.right-preview.left-w)/2,preview.top+(preview.bottom-preview.top-h)/2,w,h,memory,0,0,entry->width,entry->height,SRCCOPY);SelectObject(memory,previous);DeleteDC(memory);}
   else{SetTextColor(dc,RGB(100,116,114));DrawTextW(dc,entry&&entry->failed?L"无法读取":L"载入中",-1,&preview,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
   RECT label=grid?RECT{box.left+unit(8),box.bottom-unit(43),box.right-unit(8),box.bottom-unit(23)}:RECT{preview.right+unit(14),box.top+unit(12),box.right-unit(8),box.top+unit(34)};
   SetTextColor(dc,RGB(32,51,54));auto name=paths[i].filename().wstring();DrawTextW(dc,name.c_str(),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
   OffsetRect(&label,0,unit(22));SetTextColor(dc,shown?RGB(40,122,127):RGB(94,111,110));const wchar_t* status=shown?L"● 正在显示":(excluded?L"已排除":(entry&&entry->failed?L"图片不可读":(chosen?L"已选中":L"双击播放")));DrawTextW(dc,status,-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);
   if(i==focus&&GetFocus()==hwnd){RECT focusRect=box;InflateRect(&focusRect,-2,-2);DrawFocusRect(dc,&focusRect);}if(dragging&&(drop==i||(drop==int(paths.size())&&i==int(paths.size())-1))){bool after=drop==int(paths.size());HPEN pen=CreatePen(PS_SOLID,unit(3),RGB(40,122,127));auto previous=SelectObject(dc,pen);if(grid){int x=after?box.right:box.left;MoveToEx(dc,x,box.top,nullptr);LineTo(dc,x,box.bottom);}else{int y=after?box.bottom:box.top;MoveToEx(dc,box.left,y,nullptr);LineTo(dc,box.right,y);}SelectObject(dc,previous);DeleteObject(pen);}
  }BitBlt(screen,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(buffer);DeleteDC(dc);EndPaint(hwnd,&ps);
 }
 static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l){auto self=reinterpret_cast<PhotoGrid*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(msg==WM_NCCREATE){self=static_cast<PhotoGrid*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,msg,w,l);
  // 回调只向父窗口提交命令，不让异常跨越系统窗口过程。
  try{
   if(msg==WM_GETDLGCODE)return DLGC_WANTARROWS|DLGC_WANTCHARS|((l&&reinterpret_cast<MSG*>(l)->wParam==VK_RETURN)?DLGC_WANTALLKEYS:0);
   if(msg==WM_PAINT){self->paint();return 0;}if(msg==WM_ERASEBKGND)return 1;
   if(msg==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(w));return 0;}if(msg==WM_NCPAINT){auto result=DefWindowProcW(h,msg,w,l);uiScroll(h,self->palette);return result;}
   if(msg==WM_SIZE){self->viewport();return 0;}
   if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS){InvalidateRect(h,nullptr,FALSE);return 0;}
   if(msg==WM_MOUSEWHEEL){self->scroll-=GET_WHEEL_DELTA_WPARAM(w)*self->rowHeight()/WHEEL_DELTA;self->viewport();return 0;}
   if(msg==WM_TIMER&&w==DragScrollTimer){self->scrollDrag();return 0;}
   if(msg==WM_SYSKEYDOWN&&(w==VK_UP||w==VK_DOWN)&&((GetKeyState(VK_MENU)&0x8000)||(l&(1L<<29)))){if(!self->excluded&&self->moveSelection)self->moveSelection(w==VK_UP?-1:1);return 0;}
   if(msg==WM_VSCROLL){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(h,SB_VERT,&info);switch(LOWORD(w)){case SB_LINEUP:self->scroll-=self->unit(32);break;case SB_LINEDOWN:self->scroll+=self->unit(32);break;case SB_PAGEUP:self->scroll-=info.nPage;break;case SB_PAGEDOWN:self->scroll+=info.nPage;break;case SB_THUMBTRACK:self->scroll=info.nTrackPos;break;}self->viewport();return 0;}
   if(msg==WM_LBUTTONDOWN){SetFocus(h);SetCapture(h);self->press={GET_X_LPARAM(l),GET_Y_LPARAM(l)};self->mousePressed=true;self->pressCtrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;self->pressShift=(GetKeyState(VK_SHIFT)&0x8000)!=0;int i=self->hit(self->press.x,self->press.y);if(i>=0){if(self->pressCtrl||self->pressShift||!self->selected.contains(pathKey(self->paths[i])))self->select(i,self->pressCtrl,self->pressShift);else self->focus=i;}return 0;}
   if(msg==WM_MOUSEMOVE&&self->mousePressed){int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l);if(!self->dragging&&!self->excluded&&!self->selected.empty()&&(abs(x-self->press.x)>self->unit(5)||abs(y-self->press.y)>self->unit(5))){self->dragging=true;SetTimer(h,DragScrollTimer,35,nullptr);}if(self->dragging){self->dragPoint={x,y};self->updateDrop();SetCursor(LoadCursor(nullptr,IDC_SIZEALL));}return 0;}
   if(msg==WM_LBUTTONUP){bool drag=self->dragging;int drop=self->drop;self->mousePressed=self->dragging=false;self->drop=-1;KillTimer(h,DragScrollTimer);ReleaseCapture();if(drag&&drop>=0&&drop<=int(self->paths.size())&&self->reorder){auto chosen=self->selectedPaths();auto target=drop<int(self->paths.size())?self->paths[drop]:fs::path{};self->reorder(chosen,target);}else if(!self->pressCtrl&&!self->pressShift)self->select(self->hit(GET_X_LPARAM(l),GET_Y_LPARAM(l)),false,false);InvalidateRect(h,nullptr,FALSE);return 0;}
   if(msg==WM_CAPTURECHANGED){self->mousePressed=self->dragging=false;self->drop=-1;KillTimer(h,DragScrollTimer);InvalidateRect(h,nullptr,FALSE);return 0;}
   if(msg==WM_LBUTTONDBLCLK){int i=self->hit(GET_X_LPARAM(l),GET_Y_LPARAM(l));if(i>=0&&self->play)self->play(i);return 0;}
   if(msg==WM_KEYDOWN){bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;int i=std::max(0,self->focus);if(w=='A'&&ctrl){self->selected.clear();for(auto& p:self->paths)self->selected.insert(pathKey(p));InvalidateRect(h,nullptr,FALSE);if(self->changed)self->changed();return 0;}if(w==VK_RETURN){if(self->play)self->play(self->focus);return 0;}if(w==VK_SPACE){self->select(i,true,false);return 0;}if(w==VK_LEFT)--i;else if(w==VK_RIGHT)++i;else if(w==VK_UP)i-=self->columns();else if(w==VK_DOWN)i+=self->columns();else if(w==VK_HOME)i=0;else if(w==VK_END)i=int(self->paths.size())-1;else return DefWindowProcW(h,msg,w,l);if(!self->paths.empty()){i=std::clamp(i,0,int(self->paths.size())-1);if(ctrl&&!shift){self->focus=i;if(self->changed)self->changed();}else self->select(i,ctrl,shift);self->ensureFocus();}return 0;}
   if(msg==WM_NCDESTROY){self->hwnd=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);}
  }catch(...){PostMessageW(GetParent(h),WM_APP+73,0,0);}
  return DefWindowProcW(h,msg,w,l);
 }
};
