#pragma once
#include "image-loader.h"
#include <map>

class Thumbnails {
public:
 struct Entry{HBITMAP bitmap=nullptr;UINT width=0,height=0;size_t bytes=0;uint64_t used=0;bool failed=false;float requestedPixels=0;};
 std::map<fs::path,Entry> cache;std::future<LoadedImage> job;fs::path pending;unsigned generation=0,jobGeneration=0;uint64_t clock=0,loadsStarted=0;size_t bytes=0;float jobPixels=0;
 static constexpr size_t Budget=8*1024*1024;
 ~Thumbnails(){clear();if(job.valid())job.wait();}
 void clear(){++generation;for(auto& [path,item]:cache)if(item.bitmap)DeleteObject(item.bitmap);cache.clear();bytes=0;}
 Entry* find(const fs::path& path){auto it=cache.find(path);if(it==cache.end())return nullptr;it->second.used=++clock;return &it->second;}
 void tick(HWND list,const std::vector<fs::path>& paths,const fs::path& priority={},const std::vector<fs::path>* visiblePaths=nullptr,float targetPixels=160*100,float priorityPixels=0){
  if(job.valid()&&job.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto image=job.get();if(list&&jobGeneration==generation){Entry entry;entry.width=image.width;entry.height=image.height;entry.failed=FAILED(image.error);entry.used=++clock;
    entry.requestedPixels=jobPixels;if(!entry.failed){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.width;info.bmiHeader.biHeight=-LONG(image.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* raw=nullptr;entry.bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0);if(entry.bitmap){memcpy(raw,image.pixels.data(),image.pixels.size());entry.bytes=image.pixels.size();}else entry.failed=true;}
    auto previous=cache.find(pending);if(previous!=cache.end()){bytes-=previous->second.bytes;if(previous->second.bitmap)DeleteObject(previous->second.bitmap);}cache[pending]=entry;bytes+=entry.bytes;
    while(bytes>Budget||cache.size()>256){auto oldest=std::min_element(cache.begin(),cache.end(),[](auto& a,auto& b){return a.second.used<b.second.used;});bytes-=oldest->second.bytes;if(oldest->second.bitmap)DeleteObject(oldest->second.bitmap);cache.erase(oldest);}InvalidateRect(list,nullptr,FALSE);
    InvalidateRect(GetParent(list),nullptr,FALSE);
    PostMessageW(GetParent(list),WM_APP+74,0,0);
   }}
  if(!list||job.valid()||(paths.empty()&&priority.empty()))return;RECT client;GetClientRect(list,&client);int first=0,last=0;
  if(!visiblePaths){first=int(SendMessage(list,LB_GETTOPINDEX,0,0));int row=int(SendMessage(list,LB_GETITEMHEIGHT,0,0));last=std::min(int(paths.size()),first+int(client.bottom)/std::max(1,row)+2);}
  std::vector<fs::path> candidates;if(!priority.empty())candidates.push_back(priority);if(visiblePaths)candidates.insert(candidates.end(),visiblePaths->begin(),visiblePaths->end());else for(int i=std::max(0,first);i<last;++i)candidates.push_back(paths[i]);
  // 为大预览预留预算；照片密集时降低缩略图解码尺寸，防止可见照片互相挤出缓存后不断重载。
  float visibleBudget=Budget*.55f/4/std::max(size_t(1),candidates.size());
  for(auto& path:candidates){bool primary=path==priority&&priorityPixels>0;float wanted=primary?std::min(priorityPixels,Budget*.35f/4):std::min(targetPixels,visibleBudget);auto cached=find(path);if(cached&&(cached->failed||(cached->requestedPixels>=wanted*.8f&&(primary||cached->requestedPixels<=wanted*2))))continue;pending=path;jobGeneration=generation;jobPixels=wanted;++loadsStarted;job=std::async(std::launch::async,[path,wanted]{return loadImage(path,wanted);});break;}
 }
 void draw(const DRAWITEMSTRUCT& draw,const fs::path& path,HFONT font,bool current){
  bool selected=(draw.itemState&ODS_SELECTED)!=0;HBRUSH fill=CreateSolidBrush(selected?RGB(221,239,237):RGB(255,255,255));FillRect(draw.hDC,&draw.rcItem,fill);DeleteObject(fill);SetBkMode(draw.hDC,TRANSPARENT);SetTextColor(draw.hDC,RGB(32,51,54));SelectObject(draw.hDC,font);
  RECT preview{draw.rcItem.left+8,draw.rcItem.top+6,draw.rcItem.left+98,draw.rcItem.bottom-6};HBRUSH backing=CreateSolidBrush(RGB(245,246,243));FillRect(draw.hDC,&preview,backing);DeleteObject(backing);
  auto entry=find(path);if(entry&&entry->bitmap){float scale=std::min(float(preview.right-preview.left)/entry->width,float(preview.bottom-preview.top)/entry->height);int width=int(entry->width*scale),height=int(entry->height*scale);HDC memory=CreateCompatibleDC(draw.hDC);auto previous=SelectObject(memory,entry->bitmap);SetStretchBltMode(draw.hDC,HALFTONE);SetBrushOrgEx(draw.hDC,0,0,nullptr);StretchBlt(draw.hDC,preview.left+(preview.right-preview.left-width)/2,preview.top+(preview.bottom-preview.top-height)/2,width,height,memory,0,0,entry->width,entry->height,SRCCOPY);SelectObject(memory,previous);DeleteDC(memory);}
  else{auto text=entry&&entry->failed?L"无法读取":L"载入中";DrawTextW(draw.hDC,text,-1,&preview,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
  RECT label{preview.right+12,draw.rcItem.top+12,draw.rcItem.right-10,draw.rcItem.top+36};auto name=path.filename().wstring();DrawTextW(draw.hDC,name.c_str(),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
  label.top+=26;label.bottom+=26;SetTextColor(draw.hDC,RGB(83,110,109));DrawTextW(draw.hDC,current?L"正在显示":(entry&&entry->failed?L"图片失效或格式不可读":L"拖动排序 · 双击播放"),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);
  if(draw.itemState&ODS_FOCUS)DrawFocusRect(draw.hDC,&draw.rcItem);
 }
};
