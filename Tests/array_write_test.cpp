#include <Windows.h>
#include <d3d11_1.h>
#include <detours/detours.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>
#include <iostream>
#include <cassert>
#include <iterator>
bool g_streamingRoutesVerified=true;
ID3D11DeviceContext* g_liveContext=nullptr;
std::string Ptr(const void* p) {return std::to_string(reinterpret_cast<uintptr_t>(p));}
void Log(const std::string& s) {std::cout<<s<<'\n';}
#include "../Source/src/TintReports.h"
template<class Builder> void LogDiagnostic(Builder&& builder) {
    TintReports::Diagnostic(true,std::forward<Builder>(builder),[](const std::string& s){Log(s);});
}
#include "../Source/src/ArrayWriteTracking.h"
int main() {
 using Microsoft::WRL::ComPtr;
 ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
 auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
 assert(SUCCEEDED(hr)); g_liveContext=context.Get();
 g_streamingRoutesVerified=false;assert(!ArrayWrites::Install(context.Get()));
 g_streamingRoutesVerified=true;ArrayWrites::attempted=false;
 assert(DetourTransactionBegin()==NO_ERROR);assert(!ArrayWrites::Install(context.Get()));
 assert(DetourTransactionAbort()==NO_ERROR);ArrayWrites::attempted=false;
 assert(ArrayWrites::Install(context.Get()));
 ComPtr<ID3D11DeviceContext1> c1; assert(SUCCEEDED(context.As(&c1)));
 D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=16;d.MipLevels=1;d.ArraySize=12;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
 ComPtr<ID3D11Texture2D> a,b; assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&a)));assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&b)));
 D3D11_SHADER_RESOURCE_VIEW_DESC vd{};vd.Format=d.Format;vd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;vd.TextureCubeArray.MipLevels=1;vd.TextureCubeArray.NumCubes=2;
 ComPtr<ID3D11ShaderResourceView> view;assert(SUCCEEDED(device->CreateShaderResourceView(a.Get(),&vd,&view)));
 assert(ArrayWrites::Track(view.Get()));
 auto prime=[&](){auto s=ArrayWrites::Snapshot(); assert(s.quiet); ArrayWrites::cached[view.Get()]=s; assert(ArrayWrites::Reuse(view.Get()));return s;};
 auto changed=[&](auto s){assert(!ArrayWrites::Current(s));assert(!ArrayWrites::Reuse(view.Get()));assert(ArrayWrites::active==0);};
 unsigned pixels[256]{};pixels[0]=0xff123456;auto s=prime();
 context->UpdateSubresource(b.Get(),0,nullptr,pixels,64,0);assert(ArrayWrites::Current(s));
 context->UpdateSubresource(a.Get(),0,nullptr,pixels,64,0);changed(s);
 auto readDesc=d;readDesc.ArraySize=1;readDesc.MiscFlags=0;readDesc.BindFlags=0;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> readback;assert(SUCCEEDED(device->CreateTexture2D(&readDesc,nullptr,&readback)));
 context->CopySubresourceRegion(readback.Get(),0,0,0,0,a.Get(),0,nullptr);
 D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
 assert(*static_cast<unsigned*>(mapped.pData)==pixels[0]);context->Unmap(readback.Get(),0);
 s=prime();context->CopyResource(a.Get(),b.Get());changed(s);
 s=prime();context->CopySubresourceRegion(a.Get(),0,0,0,0,b.Get(),0,nullptr);changed(s);
 s=prime();c1->UpdateSubresource1(a.Get(),0,nullptr,pixels,64,0,0);changed(s);
 s=prime();c1->CopySubresourceRegion1(a.Get(),0,0,0,0,b.Get(),0,nullptr,0);changed(s);
 auto msDesc=d;msDesc.SampleDesc.Count=2;msDesc.MiscFlags=0;ComPtr<ID3D11Texture2D> ms;
 assert(SUCCEEDED(device->CreateTexture2D(&msDesc,nullptr,&ms)));
 s=prime();context->ResolveSubresource(a.Get(),0,ms.Get(),0,d.Format);changed(s);
 s=prime();c1->DiscardResource(a.Get());changed(s);
 s=prime();c1->DiscardView(view.Get());changed(s);
 s=prime();c1->DiscardView1(view.Get(),nullptr,0);changed(s);
 // Replay invalidates even when the deferred implementation wasn't intercepted.
 ComPtr<ID3D11DeviceContext> deferred;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
 deferred->UpdateSubresource(a.Get(),0,nullptr,pixels,64,0);
 ComPtr<ID3D11CommandList> list;assert(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)));
 s=prime();context->ExecuteCommandList(list.Get(),FALSE);changed(s);
 s=prime();{ArrayWrites::WriteScope write(a.Get());assert(!ArrayWrites::Current(s));{ArrayWrites::WriteScope nested(a.Get());assert(ArrayWrites::active==2);}}changed(s);
 // Ineligible rendering resources must retain periodic analysis.
 d.BindFlags|=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> rt;ComPtr<ID3D11ShaderResourceView> rv;
 assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&rt)));assert(SUCCEEDED(device->CreateShaderResourceView(rt.Get(),&vd,&rv)));assert(!ArrayWrites::Track(rv.Get()));
 ArrayWrites::Reset();assert(ArrayWrites::trackedCount==0 && ArrayWrites::cached.empty());
 std::cout<<"PASS: real D3D11 upload/copy/discard/replay invalidation, unrelated writes, nested writes, unsafe resource exclusion and reset\n";
}
