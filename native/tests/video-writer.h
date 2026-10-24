#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

// 录像与高频性能采样分开运行；只记录相框矩形，其他区域填充深灰色。
class VideoWriter {
    Microsoft::WRL::ComPtr<IMFSinkWriter> writer;
    DWORD stream=0;UINT width=0,height=0;LONGLONG first=-1,last=-1;
    static void require(HRESULT hr){if(FAILED(hr))throw hr;}
public:
    void start(const wchar_t* file,UINT w,UINT h){
        width=w;height=h;require(MFStartup(MF_VERSION));
        Microsoft::WRL::ComPtr<IMFAttributes> attributes;require(MFCreateAttributes(&attributes,2));
        attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE);
        attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE);
        require(MFCreateSinkWriterFromURL(file,nullptr,attributes.Get(),&writer));
        Microsoft::WRL::ComPtr<IMFMediaType> output,input;
        require(MFCreateMediaType(&output));output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);
        output->SetUINT32(MF_MT_AVG_BITRATE,8000000);output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,w,h);MFSetAttributeRatio(output.Get(),MF_MT_FRAME_RATE,60,1);MFSetAttributeRatio(output.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        require(writer->AddStream(output.Get(),&stream));require(MFCreateMediaType(&input));
        input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        input->SetUINT32(MF_MT_DEFAULT_STRIDE,w*4);
        MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,w,h);MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,60,1);MFSetAttributeRatio(input.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        require(writer->SetInputMediaType(stream,input.Get(),nullptr));require(writer->BeginWriting());
    }
    void write(const BYTE* source,UINT pitch,int x,int y,int w,int h,LONGLONG timestamp){
        if(!writer||w<1||h<1)return;
        if(first<0)first=timestamp;
        if(last>=0&&timestamp-last<160000)return;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;require(MFCreateMemoryBuffer(width*height*4,&buffer));
        BYTE* bytes=nullptr;require(buffer->Lock(&bytes,nullptr,nullptr));
        memset(bytes,24,width*height*4);
        // 输入类型明确指定正行跨度，按画面从上到下写入。
        for(int row=0;row<h&&y+row<int(height);++row)memcpy(bytes+size_t(y+row)*width*4+x*4,source+size_t(y+row)*pitch+x*4,size_t(std::min(w,int(width)-x))*4);
        require(buffer->Unlock());require(buffer->SetCurrentLength(width*height*4));
        Microsoft::WRL::ComPtr<IMFSample> sample;require(MFCreateSample(&sample));require(sample->AddBuffer(buffer.Get()));
        sample->SetSampleTime(timestamp-first);sample->SetSampleDuration(166667);require(writer->WriteSample(stream,sample.Get()));last=timestamp;
    }
    void finish(){if(writer){require(writer->Finalize());writer.Reset();MFShutdown();}}
};
