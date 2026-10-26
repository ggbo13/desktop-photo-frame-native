 // 常用操作固定在视口内，照片和外观参数分别滚动。
 bool listManagerExpanded=false,sourcesExpanded=false,layingOutSettings=false;
 void layoutV9(){
  if(!settings||!newSettings||settingsControls.empty()||layingOutSettings)return;
  layingOutSettings=true;RECT client{};GetClientRect(settings,&client);
  float width=client.right/settingsDpi,height=client.bottom/settingsDpi;
  bool sidebar=width>=820;float x=sidebar?226:20,cw=std::max(280.f,width-x-20);
  float tabsY=sidebar?20:(listManagerExpanded?144:60),pageTop=tabsY+48;
  HWND focused=GetFocus();bool wasRefreshing=refreshing;refreshing=true;
  for(auto child:settingsControls)ShowWindow(child,SW_HIDE);
  float scrollTop=pageTop,scrollBottom=height-12,bodyBottom=scrollTop;
  std::vector<int> fixed;
  auto place=[&](int id,float a,float b,float w,float h,bool scrolling=false){
   HWND child=control(id);if(!child)return;if(scrolling)b-=settingsScrollY/settingsDpi;
   SetWindowPos(child,nullptr,px(a),px(b),px(w),px(h),SWP_NOZORDER|SWP_NOACTIVATE);
   if(scrolling){int top=std::max(0,px(scrollTop-b)),bottom=std::min(px(h),px(scrollBottom-b));if(bottom<=top)return;auto region=CreateRectRgn(0,top,px(w),bottom);if(!SetWindowRgn(child,region,FALSE))DeleteObject(region);}
   else{SetWindowRgn(child,nullptr,FALSE);fixed.push_back(id);}
   ShowWindow(child,SW_SHOWNA);
  };
  place(600,20,sidebar?18:20,sidebar?180:78,24);
  if(sidebar){
   place(PlaylistBox,20,52,184,std::max(100.f,height-236));place(NameEdit,20,height-166,184,30);
   place(NewList,20,height-126,86,30);place(RenameList,116,height-126,88,30);place(DeleteList,20,height-84,184,30);
  }else{
   place(PlaylistCompact,102,16,width-246,230);place(ManageLists,width-132,16,112,32);
   SetWindowTextW(control(ManageLists),listManagerExpanded?L"收起管理":L"管理列表");
   if(listManagerExpanded){place(NameEdit,20,58,width-40,30);place(NewList,20,98,88,30);place(RenameList,118,98,88,30);place(DeleteList,216,98,140,30);}
  }
  for(int i=0;i<4;++i)place(PagePhotos+i,x+i*cw/4,tabsY,cw/4-6,34);
  if(settingsPage==0){
   settingsScrollY=0;float y=pageTop;
   float button=std::min(150.f,(cw-16)/3);place(AddFiles,x,y,button,32);place(AddFolder,x+button+8,y,button,32);place(ManageSources,x+2*(button+8),y,button,32);y+=42;
   auto caption=(sourcesExpanded?L"收起来源 · ":L"管理来源 · ")+std::to_wstring(store.active()["sources"].size());SetWindowTextW(control(ManageSources),caption.c_str());
   if(sourcesExpanded){place(SourceBox,x,y,cw-128,72);place(RemoveSource,x+cw-120,y,120,32);y+=84;}
   if(cw>=490){place(SearchEdit,x,y,cw-240,30);place(ExcludedCheck,x+cw-230,y,100,30);place(ViewGrid,x+cw-126,y,58,30);place(ViewList,x+cw-62,y,62,30);y+=42;}
   else{place(SearchEdit,x,y,cw,30);y+=38;place(ExcludedCheck,x,y,100,30);place(ViewGrid,x+cw-126,y,58,30);place(ViewList,x+cw-62,y,62,30);y+=40;}
   float footer=height-82;place(PhotoBox,x,y,cw,std::max(40.f,footer-y-10));
   float buttonWidth=(cw-24)/4;int ids[]={MoveUp,MoveDown,ExcludePhoto,ShowPhoto};for(int i=0;i<4;++i)place(ids[i],x+i*(buttonWidth+8),footer,buttonWidth,32);
   place(200,x,footer+42,cw,28);bodyBottom=scrollBottom;
  }else if(settingsPage==1){
   float ax=x,aw=cw;
   bool split=cw>=640&&height-pageTop>=410;
   if(split){
    float previewWidth=std::clamp(cw*.32f,230.f,290.f);ax=x+previewWidth+24;aw=cw-previewWidth-24;
    place(602,x,pageTop,previewWidth,24);place(FramePreview,x,pageTop+28,previewWidth,174);
    place(620,x,pageTop+216,previewWidth,24);float cardWidth=(previewWidth-10)/2;
    for(int i=0;i<5;++i)place(FrameCard+i,x+(i%2)*(cardWidth+10),pageTop+248+(i/2)*96,cardWidth,88);
   }else{
    // 窄窗口将预览固定在顶部，下方只滚动参数。
    place(602,x,pageTop,160,48);place(FramePreview,x+170,pageTop,cw-170,102);
    int columns=cw>=480?5:3;float cardWidth=(cw-8*(columns-1))/columns;
    for(int i=0;i<5;++i)place(FrameCard+i,x+(i%columns)*(cardWidth+8),pageTop+110+(i/columns)*74,cardWidth,68);
    scrollTop=pageTop+118+((5+columns-1)/columns)*74;
   }
   float y=scrollTop;auto parameter=[&](int id,float a,float b,float w,float h){place(id,a,b,w,h,true);};
   parameter(603,ax,y,aw,24);y+=28;parameter(RatioCombo,ax,y,aw,230);y+=44;
   parameter(604,ax,y,aw,24);y+=30;float cornerWidth=(aw-10)/2;
   for(int i=0;i<4;++i)parameter(AnchorButton+i,ax+(i%2)*(cornerWidth+10),y+(i/2)*40,cornerWidth,32);y+=92;
   for(int i=0;i<3;++i){int labels[]={605,606,607},sliders[]={ThicknessSlider,RadiusSlider,ShadowSlider},edits[]={ThicknessEdit,RadiusEdit,ShadowEdit};
    parameter(labels[i],ax,y,aw,24);y+=26;parameter(sliders[i],ax,y,std::max(60.f,aw-82),30);parameter(edits[i],ax+aw-72,y,72,30);y+=42;
   }
   parameter(PickColor,ax,y,(aw-10)/2,32);parameter(ResetAppearance,ax+(aw+10)/2,y,(aw-10)/2,32);y+=48;
   parameter(LimitCheck,ax,y,aw,32);y+=40;float field=(aw-14)/2;
   parameter(608,ax,y,field,24);parameter(609,ax+field+14,y,field,24);y+=28;
   parameter(MaxWidthEdit,ax,y,field,30);parameter(MaxHeightEdit,ax+field+14,y,field,30);y+=42;
   parameter(UseCurrentSize,ax,y,std::min(190.f,aw),32);bodyBottom=y+44;
  }else{
   float y=pageTop;auto p=[&](int id,float a,float b,float w,float h){place(id,a,b,w,h,true);};
   if(settingsPage==2){
    for(auto [label,id,h]:std::initializer_list<std::tuple<int,int,int>>{{610,EffectCombo,230},{611,OrderCombo,230},{612,IntervalEdit,32}}){p(label,x,y,cw,24);y+=30;p(id,x,y,std::min(380.f,cw),h);y+=52;}p(613,x,y,cw,52);y+=64;p(Pause,x,y,190,34);bodyBottom=y+54;
   }else{
    p(614,x,y,cw,28);y+=38;for(int id:{TopCheck,LockCheck,StartupCheck,SnapCheck}){p(id,x,y,cw,30);y+=40;}p(615,x,y,170,30);p(SnapMarginEdit,x+180,y,90,30);y+=58;p(616,x,y,cw,28);y+=36;for(int id:{FullscreenCheck,BatteryCheck}){p(id,x,y,cw,32);y+=44;}p(617,x,y,cw,56);y+=66;p(618,x,y,cw,24);bodyBottom=y+40;
   }
  }
  // 固定区域遮住参数区滚出的子控件，避免 GDI 重绘时穿透。
  place(699,sidebar?x:0,0,sidebar?cw:width,scrollTop);SetWindowPos(control(699),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  for(int id:fixed)if(id!=699&&IsWindowVisible(control(id)))SetWindowPos(control(id),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=px(std::max(0.f,bodyBottom-scrollTop))-1;info.nPage=px(std::max(1.f,scrollBottom-scrollTop));int maximum=std::max(0,info.nMax-int(info.nPage)+1);int corrected=std::clamp(settingsScrollY,0,maximum);bool again=corrected!=settingsScrollY;settingsScrollY=corrected;info.nPos=corrected;SetScrollInfo(settings,SB_VERT,&info,TRUE);ShowScrollBar(settings,SB_VERT,maximum>0);ShowScrollBar(settings,SB_HORZ,FALSE);
  photoGrid.viewport();InvalidateRect(settings,nullptr,FALSE);if(focused&&IsChild(settings,focused)&&IsWindowVisible(focused))SetFocus(focused);refreshing=wasRefreshing;layingOutSettings=false;
  RECT after{};GetClientRect(settings,&after);if(again||after.right!=client.right)layoutV8();
 }
