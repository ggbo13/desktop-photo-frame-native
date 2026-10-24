#pragma once
#include "version.h"
#include "../vendor/json.hpp"
#include <shlobj.h>
#include <set>
#include <fstream>
using Json=nlohmann::json;
inline std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
inline std::string stringValue(const Json& j,const char* key,std::string fallback=""){auto i=j.find(key);return i!=j.end()&&i->is_string()?i->get<std::string>():fallback;}
inline bool boolValue(const Json& j,const char* key,bool fallback=false){auto i=j.find(key);return i!=j.end()&&i->is_boolean()?i->get<bool>():fallback;}
inline double numberValue(const Json& j,const char* key,double fallback){auto i=j.find(key);return i!=j.end()&&i->is_number()?i->get<double>():fallback;}
inline std::wstring pathKey(const fs::path& p){auto s=p.lexically_normal().wstring();std::transform(s.begin(),s.end(),s.begin(),::towlower);return s;}
inline std::string newId(){GUID id{};CoCreateGuid(&id);wchar_t value[40];StringFromGUID2(id,value,40);return utf8(value);}
inline Json objectValue(const Json& j,const char* key){auto i=j.find(key);return i!=j.end()&&i->is_object()?*i:Json::object();}
inline Json defaultPlaylist(const std::string& name="我的照片"){return {{"id",newId()},{"name",name},{"sources",Json::array()},{"photoOrder",Json::array()},{"excluded",Json::array()},{"config",Json::object()}};}

struct Store {
 Json state;fs::path file;std::vector<fs::path> photos;std::vector<std::wstring> problems;bool backedUp=false;
 static Json normalize(Json raw){
  if(!raw.is_object())raw=Json::object();
  Json lists=Json::array();std::set<std::string> ids;
  if(raw.contains("playlists")&&raw["playlists"].is_array())for(auto item:raw["playlists"]){
   if(!item.is_object())continue;
   std::string id=stringValue(item,"id");if(id.empty()||ids.contains(id))id=newId();ids.insert(id);item["id"]=id;
   std::string name=stringValue(item,"name","未命名列表");if(name.empty())name="未命名列表";item["name"]=name;
   lists.push_back(item);
  }
  if(lists.empty()){auto p=defaultPlaylist();if(raw.contains("sources"))p["sources"]=raw["sources"];p["config"]=objectValue(raw,"settings");lists.push_back(p);}
  for(auto& p:lists){
   Json valid=Json::array();std::set<std::wstring> seen;
   if(p.contains("sources")&&p["sources"].is_array())for(auto source:p["sources"]){
    if(!source.is_object())continue;auto type=stringValue(source,"type"),value=stringValue(source,"path");
    if((type=="file"||type=="folder")&&!value.empty()&&seen.insert(pathKey(fs::path(wide(value)))).second)valid.push_back(source);
   }
   p["sources"]=valid;
   for(auto key:{"photoOrder","excluded"}){Json paths=Json::array();if(p.contains(key)&&p[key].is_array())for(auto v:p[key])if(v.is_string())paths.push_back(v);p[key]=paths;}
   auto c=objectValue(p,"config");
   auto choice=[&](const char* key,std::initializer_list<const char*> allowed,const char* fallback){auto value=stringValue(c,key,fallback);if(std::find(allowed.begin(),allowed.end(),value)==allowed.end())value=fallback;c[key]=value;};
   choice("frame",{"minimal","wood","metal","polaroid","borderless"},"minimal");choice("ratio",{"free","auto","1:1","4:3","3:2","16:9","9:16"},"free");
   choice("anchor",{"top-left","top-right","bottom-left","bottom-right"},"top-left");choice("transition",{"fade","slide","zoom","none"},"fade");choice("order",{"sequential","random"},"sequential");
   c["interval"]=int(std::clamp(numberValue(c,"interval",10),3.,300.));c["thickness"]=int(std::clamp(numberValue(c,"thickness",18),6.,64.));c["radius"]=int(std::clamp(numberValue(c,"radius",10),0.,32.));
   c["shadow"]=int(std::clamp(numberValue(c,"shadow",30),0.,100.));c["color"]=stringValue(c,"color","#203336");p["config"]=c;
   c["limitAutoSize"]=boolValue(c,"limitAutoSize");double minimum=stringValue(c,"frame")=="borderless"?64:2*numberValue(c,"thickness",18)+64;
   c["maxWidth"]=int(std::clamp(numberValue(c,"maxWidth",640),minimum,8192.));c["maxHeight"]=int(std::clamp(numberValue(c,"maxHeight",640),minimum,8192.));p["config"]=c;
  }
  raw["playlists"]=lists;std::string active=stringValue(raw,"activePlaylistId");bool exists=false;for(auto& p:lists)if(stringValue(p,"id")==active)exists=true;
  if(!exists)raw["activePlaylistId"]=lists[0]["id"];
  auto global=raw.contains("global")?objectValue(raw,"global"):objectValue(raw,"settings");
  for(auto key:{"paused","alwaysOnTop","lockPosition","autoStart"})global[key]=boolValue(global,key);
  global["snap"]=boolValue(global,"snap",true);global["snapMargin"]=int(std::clamp(numberValue(global,"snapMargin",12),0.,64.));
  global["pauseFullscreen"]=boolValue(global,"pauseFullscreen",true);global["batteryReduceMotion"]=boolValue(global,"batteryReduceMotion",true);
  global["photoView"]=stringValue(global,"photoView")=="list"?"list":"grid";
  global["preferredPhotoArea"]=std::clamp(numberValue(global,"preferredPhotoArea",0),0.,67108864.);
  raw["global"]=global;raw["nativeSchemaVersion"]=1;raw["applicationVersion"]=PHOTO_FRAME_VERSION;return raw;
 }
 Json& active(){auto id=stringValue(state,"activePlaylistId");for(auto& p:state["playlists"])if(stringValue(p,"id")==id)return p;return state["playlists"][0];}
 void load(const fs::path& target,const fs::path& legacy={}){
  file=target;Json raw=Json::object();
  if(fs::exists(file)){std::ifstream in(file);in>>raw;}
  else if(!legacy.empty()&&fs::exists(legacy)){
   std::ifstream in(legacy);in>>raw;
   // 原数据同时作为嵌入备份保留；旧文件从不写入。
   Json backup=raw;raw["nativeMigrationBackup"]=backup;raw["nativeMigrationSource"]=utf8(legacy.wstring());
  }
  state=normalize(raw);
 }
 void save(){
  fs::create_directories(file.parent_path());
  if(!backedUp&&fs::exists(file)){auto backup=file;backup+=L".backup-"+std::to_wstring(GetTickCount64());fs::copy_file(file,backup);backedUp=true;}
  auto temp=file;temp+=L".pending-"+std::to_wstring(GetCurrentProcessId());
  {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("无法写入配置");out<<state.dump(2);out.flush();if(!out)throw std::runtime_error("配置写入失败");}
  if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法提交配置");
 }
 static bool supported(const fs::path& p){auto ext=p.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);return std::set<std::wstring>{L".jpg",L".jpeg",L".png",L".bmp",L".gif",L".tif",L".tiff",L".webp",L".avif",L".heic"}.contains(ext);}
 bool scan(){
  std::map<std::wstring,fs::path> found;problems.clear();auto add=[&](const fs::path& p){if(supported(p))found.emplace(pathKey(p),p);};
  for(auto& source:active()["sources"]){fs::path p(wide(stringValue(source,"path")));std::error_code ec;
   if(stringValue(source,"type")=="file"){if(fs::is_regular_file(p,ec))add(p);else problems.push_back(L"无法读取文件："+p.wstring());}
   else {if(!fs::is_directory(p,ec)){problems.push_back(L"无法读取文件夹："+p.wstring());continue;}
    fs::recursive_directory_iterator it(p,fs::directory_options::skip_permission_denied,ec),end;
    while(it!=end){if(it->is_regular_file(ec))add(it->path());it.increment(ec);if(ec){problems.push_back(L"部分目录不可读取："+p.wstring());break;}}
   }
  }
  std::set<std::wstring> known,excluded;for(auto& x:active()["excluded"])excluded.insert(pathKey(fs::path(wide(x.get<std::string>()))));
  auto order=active()["photoOrder"];for(auto& x:order)known.insert(pathKey(fs::path(wide(x.get<std::string>()))));
  for(auto& [key,p]:found)if(known.insert(key).second)order.push_back(utf8(p.wstring()));active()["photoOrder"]=order;
  std::vector<fs::path> next;std::set<std::wstring> emitted;
  for(auto& x:order){auto key=pathKey(fs::path(wide(x.get<std::string>())));if(found.contains(key)&&!excluded.contains(key)&&emitted.insert(key).second)next.push_back(found[key]);}
  bool changed=next!=photos;photos=std::move(next);return changed;
 }
};
