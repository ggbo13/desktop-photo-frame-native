#pragma once
#include <webp/decode.h>
#include <webp/demux.h>
#include <avif/avif.h>

static unsigned exifDirection(const BYTE* bytes,size_t size){
 if(size>=6&&memcmp(bytes,"Exif\0\0",6)==0){bytes+=6;size-=6;}if(size<8)return 1;bool little=bytes[0]=='I'&&bytes[1]=='I';if(!little&&!(bytes[0]=='M'&&bytes[1]=='M'))return 1;
 auto read=[&](size_t at,int count)->uint32_t{if(at>size||size-at<size_t(count))return 0;uint32_t value=0;for(int i=0;i<count;++i)value|=uint32_t(bytes[at+i])<<(8*(little?i:count-i-1));return value;};
 size_t offset=read(4,4);if(offset>size||size-offset<2)return 1;unsigned count=read(offset,2);for(unsigned i=0;i<count;++i){size_t entry=offset+2+size_t(i)*12;if(entry>size||size-entry<12)break;if(read(entry,2)==274&&read(entry+2,2)==3&&read(entry+4,4)==1){unsigned value=read(entry+8,2);return value>=1&&value<=8?value:1;}}return 1;
}
static void orientImage(LoadedImage& image,unsigned direction){
 if(direction<2||direction>8)return;UINT w=image.width,h=image.height,nw=direction>=5?h:w,nh=direction>=5?w:h;std::vector<BYTE> output(size_t(nw)*nh*4);
 for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){UINT nx=x,ny=y;switch(direction){case 2:nx=w-1-x;break;case 3:nx=w-1-x;ny=h-1-y;break;case 4:ny=h-1-y;break;case 5:nx=y;ny=x;break;case 6:nx=h-1-y;ny=x;break;case 7:nx=h-1-y;ny=w-1-x;break;case 8:nx=y;ny=w-1-x;break;}memcpy(output.data()+(size_t(ny)*nw+nx)*4,image.pixels.data()+(size_t(y)*w+x)*4,4);}
 image.pixels=std::move(output);image.width=nw;image.height=nh;if(direction>=5)std::swap(image.originalWidth,image.originalHeight);
}
static void imageSize(LoadedImage& image,UINT width,UINT height,double budget){if(!width||!height)throw E_INVALIDARG;image.originalWidth=width;image.originalHeight=height;double scale=std::min({1.,std::sqrt(budget/(double(width)*height)),8192./std::max(width,height)});image.width=std::max(1u,UINT(width*scale));image.height=std::max(1u,UINT(height*scale));image.pixels.resize(size_t(image.width)*image.height*4);}
static LoadedImage loadBundled(const fs::path& path,double budget,bool webp){
 LoadedImage image;std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);auto length=input.tellg();if(length<=0||length>512*1024*1024)throw E_INVALIDARG;std::vector<BYTE> bytes(static_cast<size_t>(length));input.seekg(0);if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw E_FAIL;
 if(webp){
  WebPDecoderConfig config{};if(!WebPInitDecoderConfig(&config)||WebPGetFeatures(bytes.data(),bytes.size(),&config.input)!=VP8_STATUS_OK)throw WINCODEC_ERR_BADIMAGE;
  imageSize(image,config.input.width,config.input.height,budget);WebPData data{bytes.data(),bytes.size()};std::unique_ptr<WebPDemuxer,decltype(&WebPDemuxDelete)> demux(WebPDemux(&data),WebPDemuxDelete);unsigned orientation=1;
  if(demux){WebPChunkIterator chunk{};if(WebPDemuxGetChunk(demux.get(),"EXIF",1,&chunk)){orientation=exifDirection(chunk.chunk.bytes,chunk.chunk.size);WebPDemuxReleaseChunkIterator(&chunk);}}
  if(config.input.has_animation){WebPAnimDecoderOptions options;WebPAnimDecoderOptionsInit(&options);options.color_mode=MODE_bgrA;options.use_threads=0;std::unique_ptr<WebPAnimDecoder,decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&data,&options),WebPAnimDecoderDelete);uint8_t* canvas=nullptr;int stamp=0;if(!decoder||!WebPAnimDecoderGetNext(decoder.get(),&canvas,&stamp))throw WINCODEC_ERR_BADIMAGE;
   ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICBitmap> bitmap;check(factory->CreateBitmapFromMemory(config.input.width,config.input.height,GUID_WICPixelFormat32bppPBGRA,config.input.width*4,config.input.width*config.input.height*4,canvas,&bitmap));ComPtr<IWICBitmapScaler> scaler;check(factory->CreateBitmapScaler(&scaler));check(scaler->Initialize(bitmap.Get(),image.width,image.height,WICBitmapInterpolationModeFant));check(scaler->CopyPixels(nullptr,image.width*4,UINT(image.pixels.size()),image.pixels.data()));
  }else{config.options.use_scaling=1;config.options.scaled_width=image.width;config.options.scaled_height=image.height;config.options.use_threads=0;config.output.colorspace=MODE_bgrA;config.output.is_external_memory=1;config.output.u.RGBA.rgba=image.pixels.data();config.output.u.RGBA.stride=image.width*4;config.output.u.RGBA.size=image.pixels.size();auto status=WebPDecode(bytes.data(),bytes.size(),&config);WebPFreeDecBuffer(&config.output);if(status!=VP8_STATUS_OK)throw WINCODEC_ERR_BADIMAGE;}
  orientImage(image,orientation);
 }else{
  std::unique_ptr<avifDecoder,decltype(&avifDecoderDestroy)> decoder(avifDecoderCreate(),avifDecoderDestroy);if(!decoder)throw E_OUTOFMEMORY;decoder->maxThreads=2;decoder->imageSizeLimit=100000000;decoder->codecChoice=AVIF_CODEC_CHOICE_DAV1D;
  if(avifDecoderSetIOMemory(decoder.get(),bytes.data(),bytes.size())!=AVIF_RESULT_OK||avifDecoderParse(decoder.get())!=AVIF_RESULT_OK||avifDecoderNextImage(decoder.get())!=AVIF_RESULT_OK)throw WINCODEC_ERR_BADIMAGE;
  auto source=decoder->image;unsigned angle=(source->transformFlags&AVIF_TRANSFORM_IROT)?source->irot.angle:0;bool mirrored=(source->transformFlags&AVIF_TRANSFORM_IMIR)!=0;unsigned axis=source->imir.axis;
  std::unique_ptr<avifImage,decltype(&avifImageDestroy)> view(avifImageCreateEmpty(),avifImageDestroy);if(!view)throw E_OUTOFMEMORY;
  avifCropRect crop{0,0,source->width,source->height};avifDiagnostics diagnostics{};if(source->transformFlags&AVIF_TRANSFORM_CLAP){if(!avifCropRectFromCleanApertureBox(&crop,&source->clap,source->width,source->height,&diagnostics))throw WINCODEC_ERR_BADIMAGE;}
  if(avifImageSetViewRect(view.get(),source,&crop)!=AVIF_RESULT_OK)throw WINCODEC_ERR_BADIMAGE;imageSize(image,view->width,view->height,budget);
  if(avifImageScale(view.get(),image.width,image.height,&diagnostics)!=AVIF_RESULT_OK)throw WINCODEC_ERR_BADIMAGE;
  avifRGBImage rgb;avifRGBImageSetDefaults(&rgb,view.get());rgb.depth=8;rgb.format=AVIF_RGB_FORMAT_BGRA;rgb.alphaPremultiplied=AVIF_TRUE;rgb.pixels=image.pixels.data();rgb.rowBytes=image.width*4;rgb.maxThreads=1;if(avifImageYUVToRGB(view.get(),&rgb)!=AVIF_RESULT_OK)throw WINCODEC_ERR_BADIMAGE;
  unsigned rotation[]={1,8,3,6};orientImage(image,rotation[angle%4]);if(mirrored)orientImage(image,axis?2:4);
 }return image;
}
