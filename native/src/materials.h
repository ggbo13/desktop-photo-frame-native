#pragma once
#include <charconv>

// 小纹理交由合成器缩放，避免换图过程中重新生成整张相框位图。
static std::vector<BYTE> frameMaterial(int style,COLORREF color){
 constexpr int size=256;std::vector<BYTE> pixels(size*size*4);
 for(int y=0;y<size;++y)for(int x=0;x<size;++x){float r=GetRValue(color),g=GetGValue(color),b=GetBValue(color);float noise=float((x*73+y*151+x*y*13)%31)/31.f-.5f;
  if(style==1){float grain=std::sin(x*.47f+std::sin(y*.018f)*2.4f)+.35f*std::sin(x*1.73f+y*.024f);float light=grain*7+noise*4;r=112+light;g=78+light*.75f;b=47+light*.48f;}
  else if(style==2){float light=12*std::sin((x+y)*.012f)+noise*3;r=156+light;g=163+light;b=164+light;}
  else if(style==3){r=244+noise*2;g=242+noise*2;b=231+noise*2;}
  size_t i=(y*size+x)*4;pixels[i]=BYTE(std::clamp(b,0.f,255.f));pixels[i+1]=BYTE(std::clamp(g,0.f,255.f));pixels[i+2]=BYTE(std::clamp(r,0.f,255.f));pixels[i+3]=255;
 }return pixels;
}
static COLORREF parseFrameColor(const std::string& value){unsigned r=32,g=51,b=54;if(value.size()==7&&value[0]=='#'){unsigned parsed=0;auto result=std::from_chars(value.data()+1,value.data()+7,parsed,16);if(result.ec==std::errc{}&&result.ptr==value.data()+7){r=parsed>>16;g=(parsed>>8)&255;b=parsed&255;}}return RGB(r,g,b);}
