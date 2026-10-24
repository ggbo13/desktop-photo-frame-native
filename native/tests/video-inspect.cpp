#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <fstream>
#include <filesystem>
#include <vector>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr){if(FAILED(hr))throw hr;}
// 独立解码录像，确认文件可读及时间戳连续，并导出首帧供人工核对方向。
int wmain(int argc,wchar_t** argv){
 if(argc<3)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);
 std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 try{
  ComPtr<IMFAttributes> attributes;check(MFCreateAttributes(&attributes,1));attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
  ComPtr<IMFSourceReader> reader;check(MFCreateSourceReaderFromURL(argv[1],attributes.Get(),&reader));
  ComPtr<IMFMediaType> type;check(MFCreateMediaType(&type));type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
  check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.Get()));
  ComPtr<IMFMediaType> actual;check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual));UINT w=0,h=0;check(MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&w,&h));
   unsigned frames=0;LONGLONG previous=-1,maxGap=0,last=0;bool monotonic=true;
   std::ofstream samples(out/L"pixel-samples.csv");samples<<"time_100ns,red,green,blue\n";
  for(;;){
   DWORD flags=0;LONGLONG time=0;ComPtr<IMFSample> sample;check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&time,&sample));
   if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;if(!sample)continue;
   if(previous>=0){maxGap=std::max(maxGap,time-previous);if(time<=previous)monotonic=false;}previous=last=time;
   if(!frames){
    ComPtr<IMFMediaBuffer> buffer;check(sample->ConvertToContiguousBuffer(&buffer));BYTE* bytes=nullptr;DWORD length=0;LONG pitch=0;
    ComPtr<IMF2DBuffer> plane;bool locked2d=SUCCEEDED(buffer.As(&plane));
     if(locked2d)check(plane->Lock2D(&bytes,&pitch));
     else {check(buffer->Lock(&bytes,nullptr,&length));UINT stride=0;if(SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride)))pitch=LONG(stride);else pitch=LONG(w*4);if(pitch<0)bytes+=size_t(h-1)*(-pitch);}
     constexpr UINT sampleX=360,sampleY=300;
     if(sampleX<w&&sampleY<h){const BYTE* pixel=bytes+ptrdiff_t(sampleY)*pitch+sampleX*4;samples<<time<<","<<unsigned(pixel[2])<<","<<unsigned(pixel[1])<<","<<unsigned(pixel[0])<<"\n";}
     std::vector<BYTE> packed(size_t(w)*h*4);
    for(UINT row=0;row<h;++row)memcpy(packed.data()+size_t(h-1-row)*w*4,bytes+ptrdiff_t(row)*pitch,w*4);
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+w*h*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=w;info.biHeight=h;info.biPlanes=1;info.biBitCount=32;info.biSizeImage=w*h*4;
    std::ofstream image(out/L"first-frame.bmp",std::ios::binary);image.write(reinterpret_cast<char*>(&file),sizeof(file));image.write(reinterpret_cast<char*>(&info),sizeof(info));image.write(reinterpret_cast<char*>(packed.data()),packed.size());if(locked2d)plane->Unlock2D();else buffer->Unlock();
    }else{
     ComPtr<IMFMediaBuffer> buffer;check(sample->ConvertToContiguousBuffer(&buffer));BYTE* bytes=nullptr;DWORD length=0;LONG pitch=0;
     ComPtr<IMF2DBuffer> plane;bool locked2d=SUCCEEDED(buffer.As(&plane));
     if(locked2d)check(plane->Lock2D(&bytes,&pitch));
     else{check(buffer->Lock(&bytes,nullptr,&length));UINT stride=0;if(SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride)))pitch=LONG(stride);else pitch=LONG(w*4);if(pitch<0)bytes+=size_t(h-1)*(-pitch);}
     constexpr UINT sampleX=360,sampleY=300;
     if(sampleX<w&&sampleY<h){const BYTE* pixel=bytes+ptrdiff_t(sampleY)*pitch+sampleX*4;samples<<time<<","<<unsigned(pixel[2])<<","<<unsigned(pixel[1])<<","<<unsigned(pixel[0])<<"\n";}
     if(locked2d)plane->Unlock2D();else buffer->Unlock();
    }
   ++frames;
  }
  std::ofstream(out/L"video-summary.json")<<"{\"frames\":"<<frames<<",\"width\":"<<w<<",\"height\":"<<h<<",\"durationSeconds\":"<<last/10000000.<<",\"maxIntervalMs\":"<<maxGap/10000.<<",\"monotonic\":"<<(monotonic?"true":"false")<<"}";
 }catch(HRESULT hr){std::ofstream(out/L"error.txt")<<std::hex<<hr;return 1;}
 MFShutdown();CoUninitialize();return 0;
}
