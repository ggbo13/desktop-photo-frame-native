#pragma once
#include <charconv>

// 小纹理交由合成器缩放，避免换图过程中重新生成整张相框位图。
static std::vector<BYTE> frameMaterial(int style,COLORREF color){
 constexpr int size=256;std::vector<BYTE> pixels(size*size*4);
 for(int y=0;y<size;++y)for(int x=0;x<size;++x){float r=GetRValue(color),g=GetGValue(color),b=GetBValue(color);float noise=float((x*73+y*151+x*y*13)%31)/31.f-.5f;
  int dx=std::min(x,size-1-x),dy=std::min(y,size-1-y);
  // 木纹沿四条边分别延伸，最近边的切换自然形成四角斜接。
  if(style==1){float along=dy<=dx?x:y,across=dy<=dx?y:x;float grain=std::sin(across*.64f+std::sin(along*.024f)*1.7f)+.24f*std::sin(across*1.83f+along*.014f);float light=grain*4.5f+noise*2;r=119+light;g=84+light*.75f;b=54+light*.5f;}
  else if(style==2){float light=4*std::sin((x+y)*.012f)+std::sin(y*2.73f)*.7f+noise*.9f;r=164+light;g=171+light;b=175+light;}
  else if(style==3){r=249+noise*.8f;g=248+noise*.8f;b=244+noise*.8f;}
  // 外沿做克制的高光与暗边，内部保持原色；不增加绘图资源或动态刷新。
  float edge=float(std::min(dx,dy)),rim=std::max(0.f,1-edge/3.f);float light=(y<3||x<3?5.5f:-4.f)*rim;r+=light;g+=light;b+=light;
  size_t i=(y*size+x)*4;pixels[i]=BYTE(std::clamp(b,0.f,255.f));pixels[i+1]=BYTE(std::clamp(g,0.f,255.f));pixels[i+2]=BYTE(std::clamp(r,0.f,255.f));pixels[i+3]=255;
 }return pixels;
}
static COLORREF parseFrameColor(const std::string& value){unsigned r=32,g=51,b=54;if(value.size()==7&&value[0]=='#'){unsigned parsed=0;auto result=std::from_chars(value.data()+1,value.data()+7,parsed,16);if(result.ec==std::errc{}&&result.ptr==value.data()+7){r=parsed>>16;g=(parsed>>8)&255;b=parsed&255;}}return RGB(r,g,b);}
