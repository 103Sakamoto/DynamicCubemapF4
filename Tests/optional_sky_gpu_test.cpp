// SPDX-License-Identifier: GPL-3.0-only
#define main SceneInferenceTests
#include "scene_fallback_gpu_test.cpp"
#undef main
#include <d3d11shader.h>
#include "OptionalSkyReference.h"

struct Constants {
    float view[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    float origin[4]{},previous[4]{};
    float mul[4]={2,-2,0,0},add[4]={-1,1,0,0};
    float proj[4][4]={{1,0,0,0},{0,1,0,0},{0,0,100,0},{0,0,0,1}};
    float viewport[4]={0,0,1,1};
    float clamp[4]={.5f/Size,.5f/Size,1-.5f/Size,1-.5f/Size};
    float depth[4]={0,0,float(Size),float(Size)},offset[4]{};
    float options[4]={0,2,.25f,0};
};
static_assert(sizeof(Constants)==256);
using Bytes=std::vector<unsigned char>;
Bytes ReadTexture(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* t) {
    D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);
    const UINT stride=desc.Format==DXGI_FORMAT_R32_UINT?4:desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:8;
    desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copy;Check(d->CreateTexture2D(&desc,nullptr,&copy));c->CopyResource(copy.Get(),t);
    Bytes result(Size*Size*6*stride);
    for(UINT face=0;face<6;++face) {
        D3D11_MAPPED_SUBRESOURCE map{};Check(c->Map(copy.Get(),face,D3D11_MAP_READ,0,&map));
        for(UINT y=0;y<Size;++y)memcpy(result.data()+(face*Size+y)*Size*stride,
            static_cast<const unsigned char*>(map.pData)+y*map.RowPitch,Size*stride);
        c->Unmap(copy.Get(),face);
    }
    return result;
}
std::array<Bytes,5> Capture(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11ComputeShader* cs,
                          ID3D11SamplerState* sampler,bool separate,Pixel color,float depth,bool guard=false) {
    D3D11_TEXTURE2D_DESC input{};input.Width=input.Height=Size;input.MipLevels=input.ArraySize=1;
    input.SampleDesc.Count=1;input.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    std::vector<Pixel> colors(Size*Size,color);D3D11_SUBRESOURCE_DATA cd{colors.data(),Size*16,0};
    ComPtr<ID3D11Texture2D> colorTexture,depthTexture;
    ComPtr<ID3D11ShaderResourceView> colorView,depthView;
    Check(d->CreateTexture2D(&input,&cd,&colorTexture));Check(d->CreateShaderResourceView(colorTexture.Get(),nullptr,&colorView));
    input.Format=DXGI_FORMAT_R32_FLOAT;std::vector<float> depths(Size*Size,depth);
    D3D11_SUBRESOURCE_DATA dd{depths.data(),Size*4,0};
    Check(d->CreateTexture2D(&input,&dd,&depthTexture));Check(d->CreateShaderResourceView(depthTexture.Get(),nullptr,&depthView));
    std::array<ComPtr<ID3D11Texture2D>,5> textures;
    std::array<ComPtr<ID3D11UnorderedAccessView>,5> views;
    ID3D11UnorderedAccessView* uavs[5]{};
    D3D11_TEXTURE2D_DESC out=input;out.ArraySize=6;out.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    const float zero[4]{},sentinel[4]={.5f,.25f,.125f,1};const UINT zeroUint[4]{};
    for(UINT i=0;i<5;++i) {
        out.Format=i==3?DXGI_FORMAT_R32_UINT:i==2?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        Check(d->CreateTexture2D(&out,nullptr,&textures[i]));Check(d->CreateUnorderedAccessView(textures[i].Get(),nullptr,&views[i]));
        uavs[i]=views[i].Get();
        if(i==3)c->ClearUnorderedAccessViewUint(uavs[i],zeroUint);
        else c->ClearUnorderedAccessViewFloat(uavs[i],i==4?sentinel:zero);
    }
    const auto skyBefore=ReadTexture(d,c,textures[4].Get());
    Constants constants;if(guard)constants.options[0]=1;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cb{&constants,0,0};ComPtr<ID3D11Buffer> buffer;Check(d->CreateBuffer(&bd,&cb,&buffer));
    auto* rawBuffer=buffer.Get();ID3D11ShaderResourceView* srvs[]={depthView.Get(),colorView.Get()};
    c->CSSetShader(cs,nullptr,0);c->CSSetConstantBuffers(0,1,&rawBuffer);c->CSSetSamplers(0,1,&sampler);
    c->CSSetShaderResources(0,2,srvs);
    // Direct capture must not write an unrelated UAV already bound in slot 4.
    c->CSSetUnorderedAccessViews(4,1,&uavs[4],nullptr);
    c->CSSetUnorderedAccessViews(0,separate?5:4,uavs,nullptr);
    c->Dispatch(Size/8,Size/8,6);c->Dispatch(Size/8,Size/8,6);
    ID3D11UnorderedAccessView* none[5]{};ID3D11ShaderResourceView* noSrvs[2]{};
    c->CSSetUnorderedAccessViews(0,5,none,nullptr);c->CSSetShaderResources(0,2,noSrvs);
    std::array<Bytes,5> result;
    for(UINT i=0;i<5;++i)result[i]=ReadTexture(d,c,textures[i].Get());
    if(!separate)Require(result[4]==skyBefore,"Direct capture wrote the disabled sky UAV");
    return result;
}
bool HasBinding(DCF4Shaders::Bytecode code,const char* name) {
    ComPtr<ID3D11ShaderReflection> reflection;
    Check(D3DReflect(code.data,code.size,__uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(reflection.GetAddressOf())));
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    return SUCCEEDED(reflection->GetResourceBindingDescByName(name,&binding));
}
int main() try {
    Require(HasBinding(DCF4Shaders::Capture,"SkyHistory"),"Reprojection capture lost sky binding");
    Require(!HasBinding(DCF4Shaders::CaptureDirect,"SkyHistory"),"Direct capture still contains sky binding");
    Require(HasBinding(DCF4Shaders::InferLive,"SkyHistoryTexture"),"Reprojection inference lost sky binding");
    Require(!HasBinding(DCF4Shaders::InferDirect,"SkyHistoryTexture"),"Direct inference still contains sky binding");
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));
    ComPtr<ID3D11ComputeShader> on,off,accepted;
    Check(d->CreateComputeShader(DCF4Shaders::Capture.data,DCF4Shaders::Capture.size,nullptr,&on));
    Check(d->CreateComputeShader(DCF4Shaders::CaptureDirect.data,DCF4Shaders::CaptureDirect.size,nullptr,&off));
    ComPtr<ID3DBlob> code,errors;
    Check(D3DCompile(AcceptedCapture,strlen(AcceptedCapture),"accepted",nullptr,nullptr,"main","cs_5_0",
                    D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors));
    Check(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&accepted));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;Check(d->CreateSamplerState(&sd,&sampler));
    struct Fixture { Pixel rgb;float depth;bool guard; };
    const Fixture fixtures[]={{{.5f,.25f,.125f,1},1,false},{{4,2,8,1},1,false},{{0,0,0,1},1,false},
        {{.001f,.002f,.004f,1},1,false},{{.5f,.25f,.125f,1},.5f,false},{{1,1,1,1},.005f,true}};
    for(const auto& fixture:fixtures) {
        auto a=Capture(d.Get(),c.Get(),on.Get(),sampler.Get(),true,fixture.rgb,fixture.depth,fixture.guard);
        auto old=Capture(d.Get(),c.Get(),accepted.Get(),sampler.Get(),true,fixture.rgb,fixture.depth,fixture.guard);
        auto b=Capture(d.Get(),c.Get(),off.Get(),sampler.Get(),false,fixture.rgb,fixture.depth,fixture.guard);
        Require(a==old,"Reprojection-on capture differs from accepted capture");
        for(UINT i=0;i<4;++i)Require(a[i]==b[i],"Shared capture or coverage changed with reprojection off");
        if(fixture.depth==1 && fixture.rgb[0]>.0001f) {
            const auto* p=reinterpret_cast<const UINT16*>(b[0].data());
            const UINT center=(5*Size*Size+64*Size+64)*4;
            Require(DirectX::PackedVector::XMConvertHalfToFloat(p[center+3])==.4375f,"Sky was not retained in shared history");
            Require(std::abs(DirectX::PackedVector::XMConvertHalfToFloat(p[center])-.4375f*.25f*fixture.rgb[0])<.001f,
                    "Capture calibration or temporal sky blend changed");
        }
    }
    std::cout<<"PASS: shader reflection removes both extra-sky bindings when off; accepted on capture exact; "
             <<"shared sky/HDR/dark/unavailable-sky/geometry/weapon capture, temporal blend, coverage and unrelated UAV unchanged\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
