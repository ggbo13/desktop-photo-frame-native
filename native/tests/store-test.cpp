#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <map>
#include <iostream>
namespace fs=std::filesystem;
#include "../src/store.h"
static int checks=0;
static void expect(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 try{
  if(argc<2)throw std::runtime_error("需要独立测试目录");fs::path root=argv[1];fs::create_directories(root/L"photos");
  auto raw=Json{{"unknown",{{"keep",42}}},{"sources",Json::array({{{"type","folder"},{"path",utf8((root/L"photos").wstring())}}})},{"settings",{{"interval",17},{"paused",true},{"frame","wood"},{"custom","preserved"}}}};
  {std::ofstream out(root/L"legacy.json");out<<raw.dump();}
  Store model;model.load(root/L"native/settings.json",root/L"legacy.json");
  expect(model.state["unknown"]["keep"]==42,"未知字段应保留");expect(model.state["nativeMigrationBackup"]==raw,"迁移备份应完整");expect(model.state["global"]["paused"]==true,"全局暂停应迁移");expect(model.active()["config"]["interval"]==17,"旧间隔应迁移");expect(model.active()["config"]["custom"]=="preserved","列表未知配置应保留");
  {std::ofstream(root/L"photos/b.jpg")<<"test";std::ofstream(root/L"photos/a.png")<<"test";}
  expect(model.scan()&&model.photos.size()==2,"目录扫描应列出图片");expect(model.photos[0].filename()==L"a.png","新照片应按名称排序");
  auto firstId=stringValue(model.active(),"id");auto second=defaultPlaylist("第二组");second["sources"]=model.active()["sources"];model.state["playlists"].push_back(second);model.state=Store::normalize(model.state);
  model.state["activePlaylistId"]=second["id"];model.active()["config"]["interval"]=60;model.scan();expect(model.photos.size()==2,"多个列表应共享来源");
  model.active()["excluded"].push_back(utf8((root/L"photos/a.png").wstring()));model.scan();expect(model.photos.size()==1,"排除仅影响当前列表");
  model.state["activePlaylistId"]=firstId;model.scan();expect(model.photos.size()==2&&model.active()["config"]["interval"]==17,"列表配置与排除应隔离");
  std::swap(model.active()["photoOrder"][0],model.active()["photoOrder"][1]);{std::ofstream(root/L"photos/0-new.bmp")<<"test";}model.scan();expect(model.photos[0].filename()==L"b.jpg"&&model.photos.back().filename()==L"0-new.bmp","新照片应追加到自定义顺序末尾");
  model.save();Store again;again.load(model.file);again.scan();expect(again.photos==model.photos,"重启后顺序应保留");expect(again.state["nativeMigrationBackup"]==raw,"重启后旧配置备份应保留");
  Json original;{std::ifstream in(root/L"legacy.json");in>>original;}expect(original==raw,"旧配置不得被修改");
  auto malformed=Store::normalize({{"playlists",Json::array({nullptr,{{"id","same"},{"config",{{"interval","bad"}}}},{{"id","same"}}})}});
  expect(malformed["playlists"].size()==2,"无效列表应跳过");expect(malformed["playlists"][0]["id"]!=malformed["playlists"][1]["id"],"重复标识应修复");expect(malformed["playlists"][0]["config"]["interval"]==10,"类型异常应使用默认值");
  std::cout<<checks<<" checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
 CoUninitialize();return 0;
}
