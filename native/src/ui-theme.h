#pragma once

// 设置窗口自行绘制颜色，不更改 Windows 或照片相框的配色。
struct UiPalette {
 COLORREF canvas=RGB(255,255,255),panel=RGB(247,250,248),sidebar=RGB(237,243,239),field=RGB(255,255,255);
 COLORREF text=RGB(37,56,46),muted=RGB(82,103,91),accent=RGB(46,113,97),tint=RGB(229,241,234),line=RGB(218,228,222),stage=RGB(229,237,232),onAccent=RGB(255,255,255);
 bool dark=false;
 static UiPalette make(bool dark){UiPalette p;p.dark=dark;if(dark){p.canvas=RGB(31,39,34);p.panel=RGB(37,47,41);p.sidebar=RGB(26,35,30);p.field=RGB(45,57,49);p.text=RGB(234,241,237);p.muted=RGB(169,188,177);p.accent=RGB(189,218,201);p.tint=RGB(52,76,62);p.line=RGB(65,83,71);p.stage=RGB(22,31,26);p.onAccent=RGB(25,51,36);}return p;}
};
static COLORREF uiMix(COLORREF a,COLORREF b,float t){return RGB(int(GetRValue(a)*(1-t)+GetRValue(b)*t),int(GetGValue(a)*(1-t)+GetGValue(b)*t),int(GetBValue(a)*(1-t)+GetBValue(b)*t));}
static void uiFill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
static void uiRound(HDC dc,RECT r,COLORREF fill,COLORREF line,int radius,int stroke=1){auto b=CreateSolidBrush(fill);auto p=CreatePen(PS_SOLID,stroke,line);auto oldB=SelectObject(dc,b),oldP=SelectObject(dc,p);RoundRect(dc,r.left,r.top,r.right,r.bottom,radius*2,radius*2);SelectObject(dc,oldB);SelectObject(dc,oldP);DeleteObject(b);DeleteObject(p);}
enum class UiGlyph{None,Photo,Palette,Play,App,Plus,Folder,Up,Down,Hide,Restore,Moon,Sun,Grid,List};
static void uiIcon(HDC dc,UiGlyph kind,RECT r,COLORREF color,float dpi){
 if(kind==UiGlyph::None)return;auto pen=CreatePen(PS_SOLID,std::max(1,int(std::lround(1.4f*dpi))),color);auto previous=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
 auto x=[&](float v){return r.left+int(v*(r.right-r.left)/24);};auto y=[&](float v){return r.top+int(v*(r.bottom-r.top)/24);};
 auto line=[&](float a,float b,float c,float d){MoveToEx(dc,x(a),y(b),nullptr);LineTo(dc,x(c),y(d));};
 auto box=[&](float a,float b,float c,float d){Rectangle(dc,x(a),y(b),x(c),y(d));};
 if(kind==UiGlyph::Photo){box(3,3,21,21);line(4,16,9,11);line(9,11,14,16);line(14,16,17,13);line(17,13,21,17);Ellipse(dc,x(14),y(6),x(17),y(9));}
 else if(kind==UiGlyph::Palette){Ellipse(dc,x(3),y(3),x(21),y(21));Ellipse(dc,x(6),y(8),x(8),y(10));Ellipse(dc,x(10),y(5),x(12),y(7));Ellipse(dc,x(15),y(8),x(17),y(10));line(12,21,13,16);line(13,16,18,16);}
 else if(kind==UiGlyph::Play){line(7,4,20,12);line(20,12,7,20);line(7,20,7,4);}
 else if(kind==UiGlyph::App){Ellipse(dc,x(6),y(6),x(18),y(18));Ellipse(dc,x(10),y(10),x(14),y(14));for(int i=0;i<8;++i){float a=i*3.1415926f/4;line(12+std::cos(a)*7,12+std::sin(a)*7,12+std::cos(a)*10,12+std::sin(a)*10);}}
 else if(kind==UiGlyph::Plus){line(12,4,12,20);line(4,12,20,12);}
 else if(kind==UiGlyph::Folder){line(3,7,9,7);line(9,7,11,10);line(11,10,21,10);line(21,10,21,20);line(21,20,3,20);line(3,20,3,7);}
 else if(kind==UiGlyph::Up||kind==UiGlyph::Down){bool up=kind==UiGlyph::Up;line(12,4,12,20);line(6,up?10:14,12,up?4:20);line(12,up?4:20,18,up?10:14);}
 else if(kind==UiGlyph::Hide){Ellipse(dc,x(3),y(6),x(21),y(18));line(3,3,21,21);}
 else if(kind==UiGlyph::Restore){Arc(dc,x(4),y(4),x(20),y(20),x(5),y(6),x(5),y(18));line(4,4,4,10);line(4,10,10,10);}
 else if(kind==UiGlyph::Moon){POINT points[]={{x(13),y(3)},{x(1),y(2)},{x(0),y(19)},{x(12),y(21)},{x(17),y(22)},{x(21),y(18)},{x(22),y(14)},{x(13),y(17)},{x(7),y(10)},{x(13),y(3)}};PolyBezier(dc,points,10);}
 else if(kind==UiGlyph::Sun){Ellipse(dc,x(7),y(7),x(17),y(17));for(int i=0;i<8;++i){float a=i*3.1415926f/4;line(12+std::cos(a)*8,12+std::sin(a)*8,12+std::cos(a)*11,12+std::sin(a)*11);}}
 else if(kind==UiGlyph::Grid){for(int i=0;i<4;++i)box(3+(i%2)*11,3+(i/2)*11,10+(i%2)*11,10+(i/2)*11);}
 else if(kind==UiGlyph::List){for(int i=0;i<3;++i){line(3,5+i*7,5,5+i*7);line(9,5+i*7,21,5+i*7);}}
 SelectObject(dc,previous);SelectObject(dc,oldBrush);DeleteObject(pen);
}
// 保留旧的覆盖式画法供后续对照；系统与应用交替绘制时会产生闪烁。
[[maybe_unused]] static void uiScrollLegacy(HWND h,const UiPalette& palette){SCROLLBARINFO info{sizeof(info)};if(!GetScrollBarInfo(h,OBJID_VSCROLL,&info)||(info.rgstate[0]&STATE_SYSTEM_INVISIBLE))return;RECT window{};GetWindowRect(h,&window);auto r=info.rcScrollBar;OffsetRect(&r,-window.left,-window.top);if(r.right<=r.left||r.bottom<=r.top)return;HDC dc=GetWindowDC(h);if(!dc)return;uiFill(dc,r,palette.panel);RECT thumb{r.left+4,r.top+info.xyThumbTop,r.right-4,r.top+info.xyThumbBottom};if(thumb.bottom>thumb.top)uiRound(dc,thumb,uiMix(palette.muted,palette.panel,.35f),uiMix(palette.muted,palette.panel,.35f),3);ReleaseDC(h,dc);}
// 交由系统原生滚动条负责绘制和交互，避免鼠标悬停时反复覆盖。
static void uiScroll(HWND,const UiPalette&){}
