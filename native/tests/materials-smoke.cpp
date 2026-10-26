#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include "../src/materials.h"
int main(){
 auto minimal=frameMaterial(0,RGB(32,51,54));
 // 外沿细微提亮、纸白中性，避免旧版平板色与偏黄纸色。
 if(minimal[1*4+2]==minimal[(128*256+128)*4+2]){std::cerr<<"missing outer rim finish\n";return 1;}
 auto paper=frameMaterial(3,RGB(0,0,0));if(paper[(128*256+128)*4]<240){std::cerr<<"paper is too yellow\n";return 1;}
 for(int style=0;style<5;++style){auto p=frameMaterial(style,RGB(32,51,54));if(p.size()!=256*256*4)return 2;for(size_t i=3;i<p.size();i+=4)if(p[i]!=255)return 3;}
 std::cout<<"material finish and opacity passed\n";
}
