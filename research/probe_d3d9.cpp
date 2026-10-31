#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

// Own hidden test resources only; no game process, input, registry or user files are changed.
int main() {
    SetProcessDPIAware();
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=instance;cls.lpszClassName=L"NavigationProbeCheck";
    if(!RegisterClassW(&cls)) return 1;
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){UnregisterClassW(cls.lpszClassName,instance);return 2;}
    bool passed=false;
    for(int fixed=0;fixed<2;++fixed) {
        HWND window=CreateWindowW(cls.lpszClassName,L"",fixed?WS_POPUP:WS_OVERLAPPEDWINDOW,
            0,0,64,64,nullptr,nullptr,instance,nullptr);
        if(!window){d3d->Release();UnregisterClassW(cls.lpszClassName,instance);return 3;}
        RECT client{};GetClientRect(window,&client);
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        if(fixed){pp.BackBufferWidth=64;pp.BackBufferHeight=64;}
        IDirect3DDevice9* device=nullptr;
        HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);
        std::printf("%s dpi=%u client=%ldx%ld hr=0x%08lX\n",fixed?"fixed":"original",GetDpiForWindow(window),client.right,client.bottom,static_cast<unsigned long>(hr));
        if(fixed) passed=SUCCEEDED(hr)&&device&&client.right==64&&client.bottom==64;
        if(device)device->Release();
        DestroyWindow(window);
    }
    d3d->Release();UnregisterClassW(cls.lpszClassName,instance);
    return passed?0:4;
}
