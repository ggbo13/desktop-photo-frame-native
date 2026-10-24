#pragma once
#include <future>

struct LoadedImage {
 UINT width=0,height=0,originalWidth=0,originalHeight=0;
 std::vector<BYTE> pixels;HRESULT error=S_OK;
};
#ifdef PHOTO_FRAME_CODECS
#include "codecs.h"
#endif

// 在工作线程中按像素预算解码，方向修正与像素尺寸保持一致。
static LoadedImage loadImage(const fs::path& path,double budget){
 LoadedImage result;HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 try{
  check(initialized);
#ifdef PHOTO_FRAME_CODECS
  auto extension=path.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
  if(extension==L".webp"||extension==L".avif"){result=loadBundled(path,budget,extension==L".webp");CoUninitialize();return result;}
#endif
  ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
  ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
  check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));check(decoder->GetFrame(0,&frame));
  UINT w,h;check(frame->GetSize(&w,&h));if(!w||!h)throw E_INVALIDARG;
  result.originalWidth=w;result.originalHeight=h;
  double scale=std::min({1.,std::sqrt(budget/(double(w)*h)),8192./std::max(w,h)});
  result.width=std::max(1u,UINT(w*scale));result.height=std::max(1u,UINT(h*scale));
  ComPtr<IWICBitmapScaler> scaler;check(factory->CreateBitmapScaler(&scaler));check(scaler->Initialize(frame.Get(),result.width,result.height,WICBitmapInterpolationModeFant));
  ComPtr<IWICBitmapSource> source;check(scaler.As(&source));
  ComPtr<IWICMetadataQueryReader> metadata;PROPVARIANT orientation;PropVariantInit(&orientation);
  UINT direction=1;if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata))&&SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orientation))&&orientation.vt==VT_UI2)direction=orientation.uiVal;PropVariantClear(&orientation);
  const WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,WICBitmapTransformOptions(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,WICBitmapTransformOptions(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
  if(direction>=2&&direction<=8){ComPtr<IWICBitmapFlipRotator> rotated;check(factory->CreateBitmapFlipRotator(&rotated));check(rotated->Initialize(source.Get(),transforms[direction]));check(rotated.As(&source));if(direction>=5){std::swap(result.width,result.height);std::swap(result.originalWidth,result.originalHeight);}}
  ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter));check(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
  result.pixels.resize(size_t(result.width)*result.height*4);check(converter->CopyPixels(nullptr,result.width*4,UINT(result.pixels.size()),result.pixels.data()));
 }catch(HRESULT hr){result.error=hr;result.pixels.clear();}catch(...){result.error=E_OUTOFMEMORY;result.pixels.clear();}
 if(SUCCEEDED(initialized))CoUninitialize();return result;
}
