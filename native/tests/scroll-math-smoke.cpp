#define NOMINMAX
#include <windows.h>
#include <cassert>
#include "scroll-math.h"

int main(){
 assert(scrollThumbValue(-100,5,100,300,1000)==0);
 assert(scrollThumbValue(500,5,100,300,1000)==1000);
 assert(scrollThumbValue(205,5,100,300,1000)==500);
 assert(scrollThumbValue(200,5,100,100,1000)==0);
 assert(scrollThumbValue(200,5,-300,-100,1000)==1000);
 return 0;
}
