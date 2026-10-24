#pragma once

// 纯计算策略与窗口消息分离，方便验证尺寸、动画和叠加暂停状态。
inline double adaptiveDuration(Bounds a,Bounds b,bool adaptive){
 if(!adaptive||(std::abs(a.w-b.w)<1&&std::abs(a.h-b.h)<1))return .42;
 double delta=std::max(std::abs(double(b.w-a.w))/std::max(1.f,a.w),std::abs(double(b.h-a.h))/std::max(1.f,a.h));
 return .28+.24*std::clamp(delta,0.,1.);
}
inline Bounds fitPhotoBounds(Bounds start,float ratio,float pixels,float border,int anchor,float availableW,float availableH){
 ratio=std::max(.0001f,ratio);pixels=std::max(1.f,pixels);
 float w=std::sqrt(pixels*ratio),h=std::sqrt(pixels/ratio);
 float scale=std::max(0.f,std::min({1.f,std::max(1.f,availableW-2*border)/w,std::max(1.f,availableH-2*border)/h}));
 Bounds result=start;result.w=std::min(availableW,std::round(w*scale+2*border));result.h=std::min(availableH,std::round(h*scale+2*border));
 if(anchor%2)result.x=start.x+start.w-result.w;if(anchor>=2)result.y=start.y+start.h-result.h;
 return result;
}
struct PlaybackEnvironment {
 bool userVisible=true,locked=false,sleeping=false,fullscreen=false,viewer=false,onBattery=false;
 bool autoHidden()const{return locked||sleeping||fullscreen;}
 bool visible()const{return userVisible&&!autoHidden();}
 bool suspended()const{return autoHidden()||viewer;}
};
