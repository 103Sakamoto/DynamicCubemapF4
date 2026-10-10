#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cassert>
#include <iostream>
ID3D11DeviceContext* g_liveContext=nullptr;
bool g_liveEnabled=true; bool g_diagnosticsEnabled=true;
ULONGLONG g_logStart=GetTickCount64();
struct Pool { bool ready=false; unsigned cubes=0; Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; };
std::vector<Pool> g_universalPools;
std::unordered_map<int,int> g_universalViews,g_materialViewLookup;
std::unordered_map<ID3D11ShaderResourceView*,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> g_universalNonCubeViews;
std::uint64_t g_cacheHits=0,g_cacheMisses=0,g_cacheInserts=0,g_cacheEvictions=0;
std::vector<std::string> logs;
void Log(const std::string& s) { logs.push_back(s); }
#include "../Source/src/PerformanceDiagnostics.h"
int main() {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        device.GetAddressOf(),nullptr,context.GetAddressOf());
    assert(SUCCEEDED(hr)); g_liveContext=context.Get();
    D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=128; d.MipLevels=8; d.ArraySize=6; d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; assert(CacheTextureBytes(d)==1048560);
    d.Format=DXGI_FORMAT_BC1_UNORM; d.ArraySize=1; assert(CacheTextureBytes(d)==10936);
    d.Format=DXGI_FORMAT_UNKNOWN; assert(CacheTextureBytes(d)==0);
    for(int i=0;i<4096;++i) { PerfHookScope scope; }
    assert(g_perfHook.n==1);
    for(int i=0;i<4;++i) { g_perfNextSample=0; PerfBeginCapture(); assert(g_perfActive>=0); PerfStage(1); PerfStage(2); PerfEndCapture(); }
    g_perfNextSample=0; PerfBeginCapture(); assert(g_perfActive==-1 && g_perfSkipped==1);
    // The harness flushes its otherwise idle device; production diagnostics never flush.
    context->Flush();
    for(int i=0;i<1000 && g_perfGPU[0].n<4;++i) { g_perfNextPoll=0; PerfTick(GetTickCount64()); Sleep(1); }
    assert(g_perfGPU[0].n==4 && g_perfGPU[1].n==4 && g_perfGPU[2].n==4);
    g_perfNextSample=0; PerfBeginCapture(); PerfEndCapture(); context->Flush();
    for(int i=0;i<1000 && !g_perfInvalid;++i) { g_perfNextPoll=0; PerfTick(GetTickCount64()); Sleep(1); }
    assert(g_perfInvalid==1);
        d={}; d.Width=d.Height=16; d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,texture.GetAddressOf())));
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> a,b;
    assert(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,a.GetAddressOf())));
    assert(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,b.GetAddressOf())));
    g_universalNonCubeViews.emplace(a.Get(),a); g_universalNonCubeViews.emplace(b.Get(),b);
    PerfTick(g_perfLastReport+30001);
    assert(logs[0].find("entries=2 uniqueResources=1")!=std::string::npos);
    assert(logs[0].find("referencedLogicalMiB_est=0.000977 unknownResources=0")!=std::string::npos);
    assert(logs.size()==3 && logs[0].find("PERF CACHE")!=std::string::npos);
    assert(g_perfHook.n==0 && g_perfGPU[0].n==0);
        SetDiagnosticsEnabled(false);
    PerfBeginCapture(); { PerfHookScope scope; } PerfTick(GetTickCount64()+60000);
    assert(g_perfCaptureCalls==0 && g_perfHookCalls==0 && g_perfActive==-1);
    for(const auto& q:g_perfQueries) assert(!q.disjoint && !q.pending);
    SetDiagnosticsEnabled(true); assert(g_perfLastReport==0 && g_perfCaptureCalls==0);
    std::cout<<"PASS: WARP timestamp ring, no-slot drop, incomplete sample rejection, hook sampling, report reset\n";
}
