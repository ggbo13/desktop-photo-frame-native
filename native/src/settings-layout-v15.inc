 // 方案一：左侧导航、照片主区、固定预览；只有参数区使用父窗口滚动条。
 void layoutV8(){
  if(!settings||!newSettings||settingsControls.empty()||layingOutSettings)return;layingOutSettings=true;
  RECT client{};GetClientRect(settings,&client);float width=client.right/settingsDpi,height=client.bottom/settingsDpi;
  bool side=width>=820;float sideWidth=side?198:0,x=sideWidth+24,cw=std::max(280.f,width-x-24),header=side?24:116;
  sidebar15={0,0,px(sideWidth),px(height-32)};inspector15={};sidebarVisible15=side;inspectorVisible15=false;
  HWND focused=GetFocus();bool wasRefreshing=refreshing;refreshing=true;for(auto h:settingsControls)ShowWindow(h,SW_HIDE);
  float scrollTop=header+78,scrollBottom=height-46,bodyBottom=scrollTop;std::vector<int> fixed;
  auto place=[&](int id,float a,float b,float w,float h,bool scrolling=false){HWND child=control(id);if(!child)return;if(scrolling)b-=settingsScrollY/settingsDpi;SetWindowPos(child,nullptr,px(a),px(b),std::max(1,px(w)),std::max(1,px(h)),SWP_NOZORDER|SWP_NOACTIVATE);
   if(scrolling){int top=std::max(0,px(scrollTop-b)),bottom=std::min(px(h),px(scrollBottom-b));if(bottom<=top)return;auto clip=CreateRectRgn(0,top,px(w),bottom);if(!SetWindowRgn(child,clip,FALSE))DeleteObject(clip);}else{SetWindowRgn(child,nullptr,FALSE);fixed.push_back(id);}ShowWindow(child,SW_SHOWNA);};
  if(side){
   place(705,22,24,158,30);for(int i=0;i<4;++i)place(PagePhotos+i,14,74+i*46,170,42);
   place(600,22,282,154,24);float bottom=height-(listManagerExpanded?324:186);place(PlaylistBox,14,316,170,std::max(60.f,bottom-316));
   if(listManagerExpanded){place(NameEdit,14,height-308,170,36);place(RenameList,14,height-262,170,36);place(DeleteList,14,height-216,170,36);}
   place(NewList,14,height-164,170,38);place(ManageLists,14,height-118,170,34);place(ThemeToggle,14,height-73,170,36);
  }else{
   place(PlaylistCompact,20,14,width-190,260);place(ManageLists,width-160,14,96,36);place(ThemeToggle,width-58,14,40,36);
   for(int i=0;i<4;++i)place(PagePhotos+i,20+i*(width-40)/4,62,(width-40)/4-4,40);
   if(listManagerExpanded){place(NameEdit,20,112,width-40,34);float bw=(width-56)/3;place(NewList,20,154,bw,34);place(RenameList,28+bw,154,bw,34);place(DeleteList,36+2*bw,154,bw,34);header=204;scrollTop=header+78;}
  }
  place(700,x,header,cw,32);place(701,x,header+39,cw,24);place(200,x,height-29,cw,24);
  auto parameters=[&](float ax,float aw,float start){float y=start;auto p=[&](int id,float a,float b,float w,float h){place(id,a,b,w,h,true);};
   p(620,ax,y,aw,23);y+=31;float card=(aw-16)/5;for(int i=0;i<5;++i)p(FrameCard+i,ax+i*(card+4),y,card,70);y+=84;
   for(int i=0;i<3;++i){int labels[]={605,606,607},sliders[]={ThicknessSlider,RadiusSlider,ShadowSlider},edits[]={ThicknessEdit,RadiusEdit,ShadowEdit};p(labels[i],ax,y,aw-70,26);p(edits[i],ax+aw-64,y-3,64,30);y+=30;p(sliders[i],ax,y,aw,28);y+=44;}
   p(603,ax,y,aw,23);y+=30;p(RatioCombo,ax,y,aw,250);y+=48;p(604,ax,y,aw,23);y+=30;float corner=(aw-18)/4;for(int i=0;i<4;++i)p(AnchorButton+i,ax+i*(corner+6),y,corner,36);y+=52;
   p(PickColor,ax,y,aw,38);y+=48;p(ResetAppearance,ax,y,aw,34);y+=50;p(LimitCheck,ax,y,aw,34);y+=42;float field=(aw-12)/2;p(608,ax,y,field,23);p(609,ax+field+12,y,field,23);y+=28;p(MaxWidthEdit,ax,y,field,34);p(MaxHeightEdit,ax+field+12,y,field,34);y+=44;p(UseCurrentSize,ax,y,aw,36);return y+48;};
  if(settingsPage==0){
   bool preview=side&&cw>=720;float iw=std::clamp(cw*.29f,264.f,300.f),library=preview?cw-iw-28:cw;float y=header+78;
   // 窄窗口的顶部下拉框已经显示列表名，省去重复标题以保留照片高度。
   if(!side){ShowWindow(control(700),SW_HIDE);ShowWindow(control(701),SW_HIDE);y=header-8;}
   if(side&&cw>=650){place(700,x,header,cw-424,32);place(ManageSources,x+cw-416,header,112,38);place(AddFolder,x+cw-296,header,142,38);place(AddFiles,x+cw-146,header,146,38);}
   else{float bw=(cw-16)/3;place(AddFiles,x,y,bw,36);place(AddFolder,x+bw+8,y,bw,36);place(ManageSources,x+2*(bw+8),y,bw,36);y+=side?48:44;}
   SetWindowTextW(control(ManageSources),sourcesExpanded?L"收起来源":L"管理来源");
   if(sourcesExpanded){place(SourceBox,x,y,library-116,80);place(RemoveSource,x+library-108,y,108,36);y+=92;}
   float trailing=220;if(library<430)trailing=180;place(SearchEdit,x,y,library-trailing,38);place(ExcludedCheck,x+library-trailing+8,y,78,38);place(ViewGrid,x+library-92,y,42,38);place(ViewList,x+library-46,y,46,38);y+=side?54:46;
   place(PhotoBox,x,y,library,std::max(48.f,height-y-(side?104:90)));float bw=(library-24)/4;int actions[]={MoveUp,MoveDown,ExcludePhoto,ShowPhoto};for(int i=0;i<4;++i)place(actions[i],x+i*(bw+8),height-(side?88:76),bw,side?40:36);
   if(preview){float ix=x+library+28;inspector15={px(ix-16),px(header+76),px(width),px(height-33)};inspectorVisible15=true;place(602,ix,header+94,iw,24);place(FramePreview,ix,header+126,iw,164);place(702,ix,header+300,iw,24);scrollTop=header+342;bodyBottom=parameters(ix,iw,scrollTop);}
   else{settingsScrollY=0;bodyBottom=scrollBottom;}
  }else if(settingsPage==1){
   bool split=side&&cw>=640;float ax=x,aw=cw;
   if(split){aw=std::clamp(cw*.38f,294.f,340.f);ax=x+cw-aw;float pw=cw-aw-32;place(602,x,header+94,pw,24);place(FramePreview,x,header+132,pw,std::min(330.f,height-header-235));place(702,x,header+148+std::min(330.f,height-header-235),pw,30);inspector15={px(ax-18),px(header+76),px(width),px(height-33)};inspectorVisible15=true;scrollTop=header+96;}
   else if(height<620){float previewHeight=std::clamp(height-header-248,42.f,112.f);ShowWindow(control(701),SW_HIDE);place(602,x,header+48,140,24);place(FramePreview,x+150,header+44,cw-150,previewHeight);place(702,x,header+82,140,40);scrollTop=header+62+previewHeight;}
   else{place(602,x,header+78,cw,24);place(FramePreview,x,header+108,cw,132);place(702,x,header+246,cw,24);scrollTop=header+290;}
   bodyBottom=parameters(ax,aw,scrollTop);
  }else{
   float aw=std::min(680.f,cw),y=header+94;auto p=[&](int id,float a,float b,float w,float h){place(id,a,b,w,h,true);};scrollTop=y;
   if(settingsPage==2){for(auto [label,id,h]:std::initializer_list<std::tuple<int,int,int>>{{610,EffectCombo,250},{611,OrderCombo,250},{612,IntervalEdit,38}}){p(label,x,y,aw,24);y+=32;p(id,x,y,std::min(380.f,aw),h);y+=60;}p(613,x,y,aw,50);y+=66;p(Pause,x,y,190,40);bodyBottom=y+58;}
   else{p(619,x,y,aw,24);y+=32;p(ThemeCombo,x,y,std::min(320.f,aw),250);y+=64;p(614,x,y,aw,28);y+=40;for(int id:{TopCheck,LockCheck,StartupCheck,SnapCheck}){p(id,x,y,aw,34);y+=44;}p(615,x,y,174,34);p(SnapMarginEdit,x+184,y,86,34);y+=58;p(616,x,y,aw,28);y+=38;for(int id:{FullscreenCheck,BatteryCheck}){p(id,x,y,aw,36);y+=46;}p(617,x,y,aw,58);y+=72;p(618,x,y,aw,25);bodyBottom=y+46;}
  }
  SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=px(std::max(0.f,bodyBottom-scrollTop))-1;info.nPage=px(std::max(1.f,scrollBottom-scrollTop));int maximum=std::max(0,info.nMax-int(info.nPage)+1),corrected=std::clamp(settingsScrollY,0,maximum);bool again=corrected!=settingsScrollY;settingsScrollY=corrected;info.nPos=corrected;SetScrollInfo(settings,SB_VERT,&info,TRUE);ShowScrollBar(settings,SB_VERT,maximum>0);ShowScrollBar(settings,SB_HORZ,FALSE);
  // 参数区采用控件裁剪，不遮挡左侧照片与固定预览。
  for(int id:fixed)if(IsWindowVisible(control(id)))SetWindowPos(control(id),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  photoGrid.viewport();InvalidateRect(settings,nullptr,FALSE);if(focused&&IsChild(settings,focused)&&IsWindowVisible(focused))SetFocus(focused);refreshing=wasRefreshing;layingOutSettings=false;RECT after{};GetClientRect(settings,&after);if(again||after.right!=client.right)layoutV8();
 }
