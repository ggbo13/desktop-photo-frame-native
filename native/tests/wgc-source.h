#pragma once
#include <roapi.h>
#include <winstring.h>
// MinGW 此版把 boolean 与 BYTE 映射为同一类型；本工具不使用该泛型实例。
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// 此工具链尚未声明新版接口；ABI 取自 Microsoft windows-rs 的生成定义。
struct CaptureSessionTiming : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinUpdateInterval(ABI::Windows::Foundation::TimeSpan*)=0;
    virtual HRESULT STDMETHODCALLTYPE put_MinUpdateInterval(ABI::Windows::Foundation::TimeSpan)=0;
};

// 保留系统采集帧的时间戳，不用轮询时间冒充呈现时间。
class WgcSource {
    using Pool=ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool;
    Microsoft::WRL::ComPtr<Pool> pool;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession> session;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame> frame;
    HANDLE waitTimer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    static void close(IUnknown* object){
        if(!object)return;
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IClosable> closable;
        if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&closable))))closable->Close();
    }
    template<class T> static HRESULT factory(const wchar_t* name,T** value){
        HSTRING text=nullptr;HRESULT hr=WindowsCreateString(name,UINT32(wcslen(name)),&text);
        if(SUCCEEDED(hr))hr=RoGetActivationFactory(text,__uuidof(T),reinterpret_cast<void**>(value));
        WindowsDeleteString(text);return hr;
    }
public:
    LONGLONG originalInterval=-1;HRESULT intervalResult=E_NOINTERFACE;
    ~WgcSource(){close(frame.Get());close(session.Get());close(pool.Get());if(waitTimer)CloseHandle(waitTimer);}
    void wait(){
        // Sleep(1) 可能被系统取整到约 15ms；本工具用局部高精度等待，不改全局计时器精度。
        LARGE_INTEGER due{};due.QuadPart=-10000;
        if(waitTimer&&SetWaitableTimer(waitTimer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(waitTimer,50);else Sleep(1);
    }
    HRESULT start(IDXGIDevice* device){
        using namespace ABI::Windows::Graphics;
        Microsoft::WRL::ComPtr<IGraphicsCaptureItemInterop> interop;
        HRESULT hr=factory(L"Windows.Graphics.Capture.GraphicsCaptureItem",interop.GetAddressOf());if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<Capture::IGraphicsCaptureItem> item;
        hr=interop->CreateForMonitor(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),IID_PPV_ARGS(&item));if(FAILED(hr))return hr;
        SizeInt32 size{};hr=item->get_Size(&size);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<IInspectable> wrapped;
        hr=CreateDirect3D11DeviceFromDXGIDevice(device,&wrapped);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<DirectX::Direct3D11::IDirect3DDevice> winrtDevice;
        hr=wrapped.As(&winrtDevice);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<Capture::IDirect3D11CaptureFramePoolStatics2> statics;
        hr=factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",statics.GetAddressOf());if(FAILED(hr))return hr;
        hr=statics->CreateFreeThreaded(winrtDevice.Get(),DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,6,size,&pool);if(FAILED(hr))return hr;
        hr=pool->CreateCaptureSession(item.Get(),&session);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<Capture::IGraphicsCaptureSession2> cursorSession;
        if(SUCCEEDED(session.As(&cursorSession)))cursorSession->put_IsCursorCaptureEnabled(FALSE);
        constexpr GUID timingId{0x67c0ea62,0x1f85,0x5061,{0x92,0x5a,0x23,0x9b,0xe0,0xac,0x09,0xcb}};
        Microsoft::WRL::ComPtr<CaptureSessionTiming> timing;
        intervalResult=session->QueryInterface(timingId,reinterpret_cast<void**>(timing.GetAddressOf()));
        if(SUCCEEDED(intervalResult)){
            ABI::Windows::Foundation::TimeSpan interval{};
            if(SUCCEEDED(timing->get_MinUpdateInterval(&interval)))originalInterval=interval.Duration;
            interval.Duration=0;intervalResult=timing->put_MinUpdateInterval(interval);
        }
        return session->StartCapture();
    }
    HRESULT next(ID3D11Texture2D** texture,LONGLONG* timestamp){
        // 下一次取帧前才归还上帧，保证 GPU 回读期间缓冲不会被复用。
        close(frame.Get());frame.Reset();
        HRESULT hr=pool->TryGetNextFrame(&frame);if(FAILED(hr)||!frame)return FAILED(hr)?hr:S_FALSE;
        ABI::Windows::Foundation::TimeSpan time{};
        hr=frame->get_SystemRelativeTime(&time);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> surface;
        hr=frame->get_Surface(&surface);if(FAILED(hr))return hr;
        Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        hr=surface.As(&access);if(FAILED(hr))return hr;
        hr=access->GetInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture));
        *timestamp=time.Duration;return hr;
    }
};
