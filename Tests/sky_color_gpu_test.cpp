// SPDX-License-Identifier: GPL-3.0-only
#define main ExistingSceneTests
#include "scene_fallback_gpu_test.cpp"
#undef main
#include <algorithm>
#include <limits>

constexpr UINT kCubeSize=Size;
bool g_liveEnabled=true,g_reprojectEnabled=false,g_diagnosticsEnabled=false;
bool g_captureShaderSeparateSky=false,g_haveHistoryOrigin=true;
int g_activeCellKind=1;
ID3D11ComputeShader* g_captureCS=nullptr;
ID3D11ComputeShader* g_inferLiveCS=nullptr;
ID3D11UnorderedAccessView* g_inferredUAV=nullptr;
ID3D11ShaderResourceView* g_defaultCubeSRV=nullptr;
ID3D11SamplerState* g_linearSampler=nullptr;
ComPtr<ID3D11Texture2D> g_skyColorTexture;
ComPtr<ID3D11UnorderedAccessView> g_skyColorUAV;
ComPtr<ID3D11ShaderResourceView> g_skyColorSRV,g_skyHistorySRV;
ComPtr<ID3D11ComputeShader> g_skyColorCS;
ID3D11Buffer* g_constants=nullptr;
ID3D11UnorderedAccessView* g_colorUAV=nullptr;
ID3D11UnorderedAccessView* g_rawUAV=nullptr;
ID3D11UnorderedAccessView* g_positionUAV=nullptr;
ID3D11UnorderedAccessView* g_coverageUAV=nullptr;
ComPtr<ID3D11UnorderedAccessView> g_skyHistoryUAV;
bool UseSeparateSkyHistory(){return g_reprojectEnabled && g_activeCellKind==1;}
ComPtr<ID3D11ShaderResourceView> AcquireNamedCellFallback(){return {};}
void Log(const char*){}
template<class F> void LogDiagnostic(F f){if(g_diagnosticsEnabled)f();}
void TagReflectionReplacement(ID3D11DeviceChild*){}
namespace DCFDiag {void BeginPass(const char*){} void EndPass(){}}
#include "sky_production.inc"
#include "capture_state.inc"

struct Constants {
    float view[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    float origin[4]{},previous[4]{};
    float mul[4]={2,-2,0,0},add[4]={-1,1,0,0};
    float proj[4][4]={{1,0,0,0},{0,1,0,0},{0,0,100,0},{0,0,0,1}};
    float viewport[4]={0,0,1,1};
    float clamp[4]={.5f/Size,.5f/Size,1-.5f/Size,1-.5f/Size};
    float depth[4]={0,0,float(Size),float(Size)},offset[4]{};
    float options[4]={1,2,.25f,0};
};
static_assert(sizeof(Constants)==256);
Pixel ReadSky(ID3D11Device* d,ID3D11DeviceContext* c) {
    D3D11_TEXTURE2D_DESC desc{};g_skyColorTexture->GetDesc(&desc);
    desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copy;Check(d->CreateTexture2D(&desc,nullptr,&copy));
    c->CopyResource(copy.Get(),g_skyColorTexture.Get());Pixel first{};
    for(UINT face=0;face<6;++face){D3D11_MAPPED_SUBRESOURCE map{};
        Check(c->Map(copy.Get(),face,D3D11_MAP_READ,0,&map));Pixel p{};memcpy(p.data(),map.pData,16);c->Unmap(copy.Get(),face);
        if(!face)first=p;else Require(p==first,"Sky cube faces diverged");}
    return first;
}
void Near(Pixel actual,Pixel expected,float epsilon=0.00001f) {
    for(UINT i=0;i<4;++i)Require(std::isfinite(actual[i])&&std::abs(actual[i]-expected[i])<=epsilon,"Sky-only color mismatch");
}
void ClearSky(ID3D11DeviceContext* c){const float zero[4]{};c->ClearUnorderedAccessViewFloat(g_skyColorUAV.Get(),zero);}
void Sample(ID3D11Device* d,ID3D11DeviceContext* c,const Image& colors,const std::vector<float>& depths,Constants constants={}) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=Size;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    D3D11_SUBRESOURCE_DATA rgb{colors.data(),Size*16,0};ComPtr<ID3D11Texture2D> color,depth;
    ComPtr<ID3D11ShaderResourceView> colorView,depthView;
    Check(d->CreateTexture2D(&desc,&rgb,&color));Check(d->CreateShaderResourceView(color.Get(),nullptr,&colorView));
    desc.Format=DXGI_FORMAT_R32_FLOAT;D3D11_SUBRESOURCE_DATA z{depths.data(),Size*4,0};
    Check(d->CreateTexture2D(&desc,&z,&depth));Check(d->CreateShaderResourceView(depth.Get(),nullptr,&depthView));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=256;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{&constants,0,0};ComPtr<ID3D11Buffer> buffer;Check(d->CreateBuffer(&bd,&data,&buffer));
    auto* cb=buffer.Get();ID3D11ShaderResourceView* srvs[]={depthView.Get(),colorView.Get()};
    c->CSSetShaderResources(0,2,srvs);c->CSSetConstantBuffers(0,1,&cb);
    // Exercise the actual production dispatch; it owns only the enclosing capture's slots.
    DispatchSkyColor(c);c->ClearState();
}
Image Evaluate(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11ComputeShader* shader,
               ID3D11SamplerState* sampler,const Cube& capture,const Cube& sky,const Cube& fallback,bool bind) {
    auto* extra=bind?g_skyColorSRV.Get():nullptr;c->CSSetShaderResources(3,1,&extra);
    auto result=Run(d,c,shader,sampler,capture,sky,fallback);extra=nullptr;c->CSSetShaderResources(3,1,&extra);return result;
}
void ProductionInferenceState(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11ComputeShader* shader,
    ID3D11SamplerState* sampler,const Cube& capture,const Cube& fallback) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=Size;desc.MipLevels=1;desc.ArraySize=6;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> out,callerOut;Check(d->CreateTexture2D(&desc,nullptr,&out));Check(d->CreateTexture2D(&desc,nullptr,&callerOut));
    ComPtr<ID3D11UnorderedAccessView> target,callerTarget;Check(d->CreateUnorderedAccessView(out.Get(),nullptr,&target));
    Check(d->CreateUnorderedAccessView(callerOut.Get(),nullptr,&callerTarget));
    g_captureCS=g_inferLiveCS=shader;g_inferredUAV=target.Get();g_defaultCubeSRV=fallback.view.Get();g_linearSampler=sampler;
    ID3D11ShaderResourceView* before[4]={fallback.view.Get(),capture.view.Get(),fallback.view.Get(),capture.view.Get()};
    c->CSSetShaderResources(0,4,before);c->CSSetShader(shader,nullptr,0);c->CSSetSamplers(0,1,&sampler);
    auto* caller=callerTarget.Get();c->CSSetUnorderedAccessViews(0,1,&caller,nullptr);
    Require(DispatchInference(c,capture.view.Get()),"Production inference failed");
    ID3D11ShaderResourceView* after[4]{};c->CSGetShaderResources(0,4,after);
    for(UINT i=0;i<4;++i){Require(after[i]==before[i],"Inference caller SRV state changed");if(after[i])after[i]->Release();}
    ComPtr<ID3D11UnorderedAccessView> restored;c->CSGetUnorderedAccessViews(0,1,&restored);
    Require(restored.Get()==callerTarget.Get(),"Caller UAV changed");
    ComPtr<ID3D11ComputeShader> restoredCS;UINT n=0;c->CSGetShader(&restoredCS,nullptr,&n);Require(restoredCS.Get()==shader,"Caller shader changed");
    ComPtr<ID3D11SamplerState> restoredSampler;c->CSGetSamplers(0,1,&restoredSampler);Require(restoredSampler.Get()==sampler,"Caller sampler changed");
    for(int cell:{0,2}){g_activeCellKind=cell;Require(!UseSkyColor(),"Non-exterior sky policy enabled");Require(DispatchInference(c,capture.view.Get()),"Non-exterior inference disabled");}
    g_activeCellKind=1;c->ClearState();
}
void CaptureState(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11SamplerState* sampler,const Cube& sentinel) {
    D3D11_TEXTURE2D_DESC input{};input.Width=input.Height=Size;input.MipLevels=input.ArraySize=input.SampleDesc.Count=1;
    input.BindFlags=D3D11_BIND_SHADER_RESOURCE;input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    Image colors(Size*Size,{.2f,.5f,1,1});D3D11_SUBRESOURCE_DATA rgb{colors.data(),Size*16,0};
    ComPtr<ID3D11Texture2D> color,depth;ComPtr<ID3D11ShaderResourceView> colorView,depthView;
    Check(d->CreateTexture2D(&input,&rgb,&color));Check(d->CreateShaderResourceView(color.Get(),nullptr,&colorView));
    input.Format=DXGI_FORMAT_R32_FLOAT;std::vector<float> depths(Size*Size,1);
    D3D11_SUBRESOURCE_DATA z{depths.data(),Size*4,0};Check(d->CreateTexture2D(&input,&z,&depth));Check(d->CreateShaderResourceView(depth.Get(),nullptr,&depthView));
    Constants constants;D3D11_BUFFER_DESC bd{};bd.ByteWidth=256;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cb{&constants,0,0};ComPtr<ID3D11Buffer> buffer,callerBuffer;
    Check(d->CreateBuffer(&bd,&cb,&buffer));Check(d->CreateBuffer(&bd,&cb,&callerBuffer));g_constants=buffer.Get();
    std::array<ComPtr<ID3D11Texture2D>,5> textures,callerTextures;
    std::array<ComPtr<ID3D11UnorderedAccessView>,5> views,callerViews;
    ID3D11UnorderedAccessView* caller[5]{};
    input.ArraySize=6;input.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    for(UINT i=0;i<5;++i){input.Format=i==3?DXGI_FORMAT_R32_UINT:i==2?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        Check(d->CreateTexture2D(&input,nullptr,&textures[i]));Check(d->CreateUnorderedAccessView(textures[i].Get(),nullptr,&views[i]));
        Check(d->CreateTexture2D(&input,nullptr,&callerTextures[i]));Check(d->CreateUnorderedAccessView(callerTextures[i].Get(),nullptr,&callerViews[i]));caller[i]=callerViews[i].Get();}
    g_colorUAV=views[0].Get();g_rawUAV=views[1].Get();g_positionUAV=views[2].Get();g_coverageUAV=views[3].Get();g_skyHistoryUAV=views[4];
    ComPtr<ID3D11ComputeShader> on,off;
    Check(d->CreateComputeShader(DCF4Shaders::Capture.data,DCF4Shaders::Capture.size,nullptr,&on));
    Check(d->CreateComputeShader(DCF4Shaders::CaptureDirect.data,DCF4Shaders::CaptureDirect.size,nullptr,&off));
    g_linearSampler=sampler;
    for(bool separate:{false,true}){
        g_reprojectEnabled=separate;g_captureCS=separate?on.Get():off.Get();
        const float zero[4]{};const UINT zeroUint[4]{};
        for(UINT i=0;i<5;++i){if(i==3)c->ClearUnorderedAccessViewUint(views[i].Get(),zeroUint);else c->ClearUnorderedAccessViewFloat(views[i].Get(),zero);}
        ClearSky(c);
        c->CSSetShader(off.Get(),nullptr,0);ID3D11ShaderResourceView* before[4]={sentinel.view.Get(),sentinel.view.Get(),sentinel.view.Get(),sentinel.view.Get()};
        c->CSSetShaderResources(0,4,before);c->CSSetUnorderedAccessViews(0,5,caller,nullptr);
        auto* oldCB=callerBuffer.Get();c->CSSetConstantBuffers(0,1,&oldCB);c->CSSetSamplers(0,1,&sampler);
        Require(ProductionCaptureState(c,depthView.Get(),colorView.Get()),"Production capture scope failed");
        ID3D11ShaderResourceView* after[4]{};c->CSGetShaderResources(0,4,after);
        for(UINT i=0;i<4;++i){Require(after[i]==before[i],"Capture SRV state changed");after[i]->Release();}
        ID3D11UnorderedAccessView* out[5]{};c->CSGetUnorderedAccessViews(0,5,out);
        for(UINT i=0;i<5;++i){Require(out[i]==caller[i],"Capture UAV state changed including unrelated slot 4");out[i]->Release();}
        ComPtr<ID3D11Buffer> restoredCB;c->CSGetConstantBuffers(0,1,&restoredCB);Require(restoredCB.Get()==oldCB,"Capture CB changed");
        ComPtr<ID3D11SamplerState> restoredSampler;c->CSGetSamplers(0,1,&restoredSampler);Require(restoredSampler.Get()==sampler,"Capture sampler changed");
        ComPtr<ID3D11ComputeShader> restoredShader;UINT count=0;c->CSGetShader(&restoredShader,nullptr,&count);Require(restoredShader.Get()==off.Get(),"Capture shader changed");
        Near(ReadSky(d,c),{.05f,.125f,.25f,1});c->ClearState();
    }
    g_reprojectEnabled=false;g_skyHistoryUAV.Reset();g_constants=nullptr;
}
int main() try {
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));
    for(int cell:{0,2}){g_activeCellKind=cell;Require(EnsureSkyColorResources(d.Get())&&!g_skyColorTexture,"Allocated sky in unknown/interior cell");}
    g_activeCellKind=1;g_liveEnabled=false;Require(EnsureSkyColorResources(d.Get())&&!g_skyColorTexture,"Allocated sky while disabled");
    g_liveEnabled=true;Require(EnsureSkyColorResources(d.Get()),"Sky allocation failed");Require(!g_haveHistoryOrigin,"New color not included in history reset");
    D3D11_TEXTURE2D_DESC desc{};g_skyColorTexture->GetDesc(&desc);Require(desc.Width==1&&desc.Height==1&&desc.ArraySize==6&&desc.MipLevels==1,"Full-size sky history restored");
    auto* identity=g_skyColorTexture.Get();g_haveHistoryOrigin=true;Require(EnsureSkyColorResources(d.Get())&&g_skyColorTexture.Get()==identity&&g_haveHistoryOrigin,"Unchanged resources rebuilt/reset");
    const Pixel blue={.2f,.5f,1,1},brown={10,2,.1f,1};
    Image pixels(Size*Size,brown);std::vector<float> depths(Size*Size,.5f);
    // Fixture deliberately has geometry in the upper half and visible sky below it.
    for(UINT y=20;y<90;++y)for(UINT x=0;x<Size;++x){pixels[y*Size+x]=blue;depths[y*Size+x]=1;}
    ClearSky(c.Get());Sample(d.Get(),c.Get(),pixels,depths);Near(ReadSky(d.Get(),c.Get()),{.05f,.125f,.25f,1});
    const auto old=ReadSky(d.Get(),c.Get());Sample(d.Get(),c.Get(),Image(Size*Size,brown),std::vector<float>(Size*Size,.5f));Near(ReadSky(d.Get(),c.Get()),old);
    for(Pixel rgb:{Pixel{.4f,.4f,.4f,1},Pixel{.001f,.002f,.004f,1},Pixel{4,2,8,1}}){
        ClearSky(c.Get());Sample(d.Get(),c.Get(),Image(Size*Size,rgb),std::vector<float>(Size*Size,1));Near(ReadSky(d.Get(),c.Get()),{rgb[0]*.25f,rgb[1]*.25f,rgb[2]*.25f,1});}
    for(Pixel invalid:{Pixel{},Pixel{-1,1,4,1},Pixel{std::numeric_limits<float>::quiet_NaN(),1,4,1},
                     Pixel{1,std::numeric_limits<float>::infinity(),4,1},Pixel{1e20f,1,4,1}}){
        ClearSky(c.Get());Sample(d.Get(),c.Get(),Image(Size*Size,invalid),std::vector<float>(Size*Size,1));Near(ReadSky(d.Get(),c.Get()),{});}
    for(float depth:{.005f,.5f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        ClearSky(c.Get());Sample(d.Get(),c.Get(),Image(Size*Size,blue),std::vector<float>(Size*Size,depth));Near(ReadSky(d.Get(),c.Get()),{});}
    ClearSky(c.Get());Constants crop;crop.depth[0]=crop.depth[1]=32;crop.depth[2]=crop.depth[3]=64;
    crop.viewport[0]=crop.viewport[1]=.25f;crop.viewport[2]=crop.viewport[3]=.5f;
    pixels=Image(Size*Size,brown);depths=std::vector<float>(Size*Size,.5f);
    for(UINT y=32;y<96;++y)for(UINT x=32;x<96;++x){pixels[y*Size+x]=blue;depths[y*Size+x]=1;}
    Sample(d.Get(),c.Get(),pixels,depths,crop);Near(ReadSky(d.Get(),c.Get()),{.05f,.125f,.25f,1});
    crop.depth[0]=100;crop.depth[2]=64;ClearSky(c.Get());Sample(d.Get(),c.Get(),pixels,depths,crop);Near(ReadSky(d.Get(),c.Get()),{});
    ClearSky(c.Get());Sample(d.Get(),c.Get(),Image(Size*Size,blue),std::vector<float>(Size*Size,1));
    const Pixel red={1,.2f,.1f,1};Sample(d.Get(),c.Get(),Image(Size*Size,red),std::vector<float>(Size*Size,1));
    Near(ReadSky(d.Get(),c.Get()),{.1f,.10625f,.19375f,1});
    ClearSky(c.Get());Sample(d.Get(),c.Get(),Image(Size*Size,blue),std::vector<float>(Size*Size,1));
    ComPtr<ID3D11ComputeShader> direct,live;Check(d->CreateComputeShader(DCF4Shaders::InferDirect.data,DCF4Shaders::InferDirect.size,nullptr,&direct));
    Check(d->CreateComputeShader(DCF4Shaders::InferLive.data,DCF4Shaders::InferLive.size,nullptr,&live));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;Check(d->CreateSamplerState(&sd,&sampler));
    const auto empty=MakeCube(d.Get(),c.Get(),Solid({})),fallback=MakeCube(d.Get(),c.Get(),Solid({.25f,.25f,.25f,1}));
    for(auto* shader:{direct.Get(),live.Get()}){
        Matches(Evaluate(d.Get(),c.Get(),shader,sampler.Get(),empty,empty,fallback,true),{.05f,.125f,.25f,0});
        for(Pixel rgb:{Pixel{.5f,.25f,.125f,1},Pixel{4,.25f,2,1},Pixel{0,0,0,1}}){
            const auto full=MakeCube(d.Get(),c.Get(),Solid(rgb));
            Require(Evaluate(d.Get(),c.Get(),shader,sampler.Get(),full,empty,fallback,true)==Evaluate(d.Get(),c.Get(),shader,sampler.Get(),full,empty,fallback,false),"Observed/HDR/black changed");}
        auto history=Solid({});for(UINT i=Size*Size;i<2*Size*Size;++i)history[i]={.5f,.5f,.5f,1};
        const auto partial=MakeCube(d.Get(),c.Get(),history);const auto filled=Evaluate(d.Get(),c.Get(),shader,sampler.Get(),partial,empty,fallback,true);
        const auto accepted=Evaluate(d.Get(),c.Get(),shader,sampler.Get(),partial,empty,fallback,false);
        for(UINT y=2;y<Size-2;++y)for(UINT x=2;x<Size-2;++x)Require(filled[Size*Size+y*Size+x]==accepted[Size*Size+y*Size+x],"Observed face changed");
        Require(filled[5*Size*Size+64*Size+64][2]>filled[5*Size*Size+64*Size+64][0],"Missing face still uses terrain gray");
    }
    const auto sky=MakeCube(d.Get(),c.Get(),Solid({.125f,.5f,1,1}));
    Require(Evaluate(d.Get(),c.Get(),live.Get(),sampler.Get(),empty,sky,fallback,true)==Evaluate(d.Get(),c.Get(),live.Get(),sampler.Get(),empty,sky,fallback,false),"Observed separate sky changed");
    ProductionInferenceState(d.Get(),c.Get(),direct.Get(),sampler.Get(),empty,fallback);
    CaptureState(d.Get(),c.Get(),sampler.Get(),empty);
    for(int cell:{0,2}){g_activeCellKind=cell;Require(EnsureSkyColorResources(d.Get())&&!g_skyColorSRV&&!g_skyColorUAV&&!g_skyColorTexture,"Sky resource retained in interior/unknown");}
    std::cout<<"PASS sky-only mixed scene, geometry/weapon/invalid rejection, cloudy/dark/HDR calibration, viewport offsets, no-sky retention/reset, temporal blend, exterior lifecycle, missing-face blue, observed radiance and separate sky exact, production capture/inference restoration\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
