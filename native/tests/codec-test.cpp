#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include "../src/renderer.h"
#include "../vendor/json.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;fs::path root=argv[1];std::ifstream input(root/L"expected.json");nlohmann::json fixtures;input>>fixtures;int checks=0;
 for(auto& item:fixtures){auto path=root/fs::path(item["file"].get<std::string>());auto image=loadImage(path,16000);bool pass=SUCCEEDED(image.error)&&image.originalWidth==item["width"].get<UINT>()&&image.originalHeight==item["height"].get<UINT>()&&image.pixels.size()<=16000*4;
  if(pass){size_t pixel=(size_t(image.height/10)*image.width+image.width/10)*4;for(int c=0;c<3;++c)if(abs(int(image.pixels[pixel+2-c])-item["corner"][c].get<int>())>35)pass=false;}
  if(!pass){std::cerr<<"FAIL "<<item["file"]<<" hr="<<std::hex<<image.error<<std::dec<<" size="<<image.originalWidth<<"x"<<image.originalHeight<<"\n";return 1;}++checks;
 }
 if(SUCCEEDED(loadImage(root/L"broken.png",16000).error))return 1;++checks;
 std::cout<<checks<<" decode, orientation, pixel budget and invalid-file checks passed\n";return 0;
}
