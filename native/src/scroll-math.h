#pragma once
#include <algorithm>

// 光标在轨道外时限制在端点，横向位置不影响最终滚动值。
inline int scrollThumbValue(int cursorY,int grabOffset,int trackTop,int trackBottom,int maximum){
 int range=std::max(0,trackBottom-trackTop);
 int position=std::clamp(cursorY-grabOffset,trackTop,std::max(trackTop,trackBottom));
 return range?MulDiv(position-trackTop,std::max(0,maximum),range):0;
}
