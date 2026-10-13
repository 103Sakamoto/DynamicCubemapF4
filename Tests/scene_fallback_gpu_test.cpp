// SPDX-License-Identifier: GPL-3.0-only
// Exercises production embedded inference on D3D11 WARP, never in the game.
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../Source/generated/EmbeddedShaders.h"
#include "SceneFallbackReference.h"
#include "SceneCleanupReference.h"

using Microsoft::WRL::ComPtr;
using Pixel=std::array<float,4>;
using Image=std::vector<Pixel>;
constexpr UINT Size=128, Mips=8;
void Check(HRESULT hr) { if(FAILED(hr))throw std::runtime_error("D3D11 failure"); }
void Require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
Pixel Weighted(Pixel rgb,float confidence) { return {rgb[0]*confidence,rgb[1]*confidence,rgb[2]*confidence,confidence}; }
Image Solid(Pixel p) { return Image(Size*Size*6,p); }
struct Cube { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> view; };
Cube MakeCube(ID3D11Device* device,ID3D11DeviceContext* context,const Image& pixels) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width=d.Height=Size;d.MipLevels=Mips;d.ArraySize=6;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE|D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Cube cube;Check(device->CreateTexture2D(&d,nullptr,&cube.texture));
    D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=d.Format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
    s.TextureCube.MipLevels=Mips;Check(device->CreateShaderResourceView(cube.texture.Get(),&s,&cube.view));
    std::vector<UINT16> packed(Size*Size*4);
    for(UINT face=0;face<6;++face) {
        for(UINT i=0;i<Size*Size;++i)for(UINT channel=0;channel<4;++channel)
            packed[i*4+channel]=DirectX::PackedVector::XMConvertFloatToHalf(pixels[face*Size*Size+i][channel]);
        context->UpdateSubresource(cube.texture.Get(),D3D11CalcSubresource(0,face,Mips),nullptr,packed.data(),Size*8,0);
    }
    context->GenerateMips(cube.view.Get());return cube;
}
Image Run(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11ComputeShader* shader,
          ID3D11SamplerState* sampler,const Cube& capture,const Cube& sky,const Cube& fallback) {
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=Size;d.MipLevels=1;d.ArraySize=6;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> output;Check(device->CreateTexture2D(&d,nullptr,&output));
    ComPtr<ID3D11UnorderedAccessView> target;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&target));
    ID3D11ShaderResourceView* views[]={capture.view.Get(),sky.view.Get(),fallback.view.Get()};
    auto* rawTarget=target.Get();context->CSSetShader(shader,nullptr,0);context->CSSetSamplers(0,1,&sampler);
    context->CSSetShaderResources(0,3,views);context->CSSetUnorderedAccessViews(0,1,&rawTarget,nullptr);
    context->Dispatch(Size/8,Size/8,6);
    ID3D11ShaderResourceView* nullViews[3]{};ID3D11UnorderedAccessView* nullTarget=nullptr;
    context->CSSetShaderResources(0,3,nullViews);context->CSSetUnorderedAccessViews(0,1,&nullTarget,nullptr);
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&d,nullptr,&staging));
    context->CopyResource(staging.Get(),output.Get());Image result(Size*Size*6);
    for(UINT face=0;face<6;++face) {
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(staging.Get(),face,D3D11_MAP_READ,0,&mapped));
        for(UINT y=0;y<Size;++y) {
            const auto* row=reinterpret_cast<const UINT16*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
            for(UINT x=0;x<Size;++x)for(UINT channel=0;channel<4;++channel)
                result[(face*Size+y)*Size+x][channel]=DirectX::PackedVector::XMConvertHalfToFloat(row[x*4+channel]);
        }
        context->Unmap(staging.Get(),face);
    }
    return result;
}
void Matches(const Image& result,Pixel expected,float tolerance=0.002f) {
    for(const auto& pixel:result) {
        for(float value:pixel)Require(std::isfinite(value),"Nonfinite inference output");
        for(UINT channel=0;channel<3;++channel)
            Require(std::abs(pixel[channel]-expected[channel])<=tolerance,"Captured radiance/color mismatch");
    }
}
int main() try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    // Reuse the embedded-shader creation check for every shader shipped in the DLL.
    for(const auto& item:DCF4Shaders::All) {
        if(!strcmp(item.name,"ImGuiVertex")) {
            ComPtr<ID3D11VertexShader> shader;Check(device->CreateVertexShader(item.code.data,item.code.size,nullptr,&shader));
        } else if(!strcmp(item.name,"ImGuiPixel")) {
            ComPtr<ID3D11PixelShader> shader;Check(device->CreatePixelShader(item.code.data,item.code.size,nullptr,&shader));
        } else {
            ComPtr<ID3D11ComputeShader> shader;Check(device->CreateComputeShader(item.code.data,item.code.size,nullptr,&shader));
        }
    }
    ComPtr<ID3D11ComputeShader> live,previous;
    Check(device->CreateComputeShader(DCF4Shaders::InferLive.data,DCF4Shaders::InferLive.size,nullptr,&live));
    D3D_SHADER_MACRO macros[]={{"LIVE_LINEAR_OUTPUT","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> reference,errors;
    HRESULT hr=D3DCompile(PreviousInference,strlen(PreviousInference),"previous",macros,nullptr,"main","cs_5_0",
                         D3DCOMPILE_ENABLE_STRICTNESS,0,&reference,&errors);
    if(FAILED(hr)&&errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());Check(hr);
    Check(device->CreateComputeShader(reference->GetBufferPointer(),reference->GetBufferSize(),nullptr,&previous));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;Check(device->CreateSamplerState(&sd,&sampler));
    const Pixel scene={0.5f,0.25f,0.125f,1}, tint={0.03125f,0.0078125f,0.0625f,1};
    const auto empty=MakeCube(device.Get(),context.Get(),Solid({}));
    const auto fallback=MakeCube(device.Get(),context.Get(),Solid(tint));
    auto evaluate=[&](const Cube& capture,const Cube& sky,ID3D11ComputeShader* shader) {
        return Run(device.Get(),context.Get(),shader,sampler.Get(),capture,sky,fallback);
    };
    const auto startup=evaluate(empty,empty,live.Get());Matches(startup,tint);
    Require(startup==evaluate(empty,empty,previous.Get()),"Empty-history fallback changed");
    const auto full=MakeCube(device.Get(),context.Get(),Solid(scene));
    const auto complete=evaluate(full,empty,live.Get());Matches(complete,scene);
    Require(complete==evaluate(full,empty,previous.Get()),"Fully observed reflection changed");
    const Pixel hdr={4,0.25f,2,1};const auto bright=MakeCube(device.Get(),context.Get(),Solid(hdr));
    const auto hdrResult=evaluate(bright,empty,live.Get());Matches(hdrResult,hdr);
    Require(hdrResult==evaluate(bright,empty,previous.Get()),"Captured HDR was altered or clamped");
    auto partial=Solid({});for(UINT i=Size*Size;i<2*Size*Size;++i)partial[i]=scene;
    const auto oneFace=MakeCube(device.Get(),context.Get(),partial);
    const auto filled=evaluate(oneFace,empty,live.Get());Matches(filled,scene);
    const auto oldFilled=evaluate(oneFace,empty,previous.Get());
    Require(oldFilled[5*Size*Size][0]<filled[5*Size*Size][0]*0.1f,"Fixture did not reproduce weak colored fallback");
    auto sparse=Solid({});sparse[Size*Size+83*Size+24]=Weighted(scene,0.25f);
    const auto oneTexel=MakeCube(device.Get(),context.Get(),sparse);Matches(evaluate(oneTexel,empty,live.Get()),scene,0.003f);
    auto black=Solid({});for(UINT i=Size*Size;i<2*Size*Size;++i)black[i]={0,0,0,1};
    const auto dark=MakeCube(device.Get(),context.Get(),black);Matches(evaluate(dark,empty,live.Get()),{});
    const Pixel skyColor={0.125f,0.5f,1,1};const auto sky=MakeCube(device.Get(),context.Get(),Solid(skyColor));
    const auto skyResult=evaluate(empty,sky,live.Get());Matches(skyResult,skyColor);
    Require(skyResult==evaluate(empty,sky,previous.Get()),"Observed sky changed");
    const auto layered=evaluate(oneFace,sky,live.Get());
    Require(layered==evaluate(oneFace,sky,previous.Get()),"Geometry/sky priority changed");
    for(UINT face=0;face<6;++face) {
        const Pixel expected=face==1?scene:skyColor;
        const auto& pixel=layered[face*Size*Size+64*Size+64];
        for(UINT channel=0;channel<3;++channel)Require(std::abs(pixel[channel]-expected[channel])<0.002f,"Geometry/sky priority incorrect");
    }
    // Compare each previously shipped inference variant against the single
    // production shader over sparse, covered, HDR, sky and no-history fixtures.
    const char* weights[]={nullptr,"1.0","0.125","0.25","0.375","0.5","0.625","0.75","0.875"};
    for(auto weight:weights) {
        D3D_SHADER_MACRO oldMacros[]={{"LIVE_LINEAR_OUTPUT","1"},{"FALLBACK_LOCK_WEIGHT",weight},{nullptr,nullptr}};
        if(!weight)oldMacros[1]={nullptr,nullptr};
        ComPtr<ID3DBlob> oldCode,oldErrors;
        Check(D3DCompile(AcceptedInference,strlen(AcceptedInference),"accepted",oldMacros,nullptr,"main","cs_5_0",
                         D3DCOMPILE_ENABLE_STRICTNESS,0,&oldCode,&oldErrors));
        ComPtr<ID3D11ComputeShader> accepted;
        Check(device->CreateComputeShader(oldCode->GetBufferPointer(),oldCode->GetBufferSize(),nullptr,&accepted));
        const Cube* captures[]={&empty,&full,&bright,&oneFace,&oneTexel,&dark,&empty,&oneFace};
        const Cube* skies[]={&empty,&empty,&empty,&empty,&empty,&empty,&sky,&sky};
        for(UINT i=0;i<8;++i)
            Require(evaluate(*captures[i],*skies[i],accepted.Get())==evaluate(*captures[i],*skies[i],live.Get()),
                    "Cleanup differs from accepted SceneFallback01 GPU output");
    }
    // Interiors retain a sampleable neutral startup resource, without a brown
    // asset. Once a capture exists, startup color has no effect on the result.
    const auto neutral=MakeCube(device.Get(),context.Get(),Solid({0.25f,0.25f,0.25f,1}));
    Matches(Run(device.Get(),context.Get(),live.Get(),sampler.Get(),empty,empty,neutral),{0.25f,0.25f,0.25f,1});
    Require(Run(device.Get(),context.Get(),live.Get(),sampler.Get(),oneTexel,empty,neutral)==evaluate(oneTexel,empty,live.Get()),
            "Interior placeholder affects captured lighting");
    ComPtr<ID3D11ComputeShader> direct;
    Check(device->CreateComputeShader(DCF4Shaders::InferDirect.data,DCF4Shaders::InferDirect.size,nullptr,&direct));
    const Cube unbound{};
    const Cube* captures[]={&empty,&full,&bright,&oneFace,&oneTexel,&dark};
    for(const auto* capture:captures) {
        const auto output=Run(device.Get(),context.Get(),direct.Get(),sampler.Get(),*capture,unbound,fallback);
        Require(output==evaluate(*capture,empty,live.Get()),"Direct inference differs from shared-history-only inference");
        Require(output==evaluate(*capture,sky,direct.Get()),"Direct shader read the disabled sky layer");
    }
    Matches(Run(device.Get(),context.Get(),direct.Get(),sampler.Get(),sky,unbound,fallback),skyColor);
    std::cout<<"PASS: all 12 embedded shaders create; all nine accepted inference variants produce exact matching GPU outputs; "
             <<"empty/full/HDR/sparse/black/sky/geometry priority and neutral interior startup; direct inference with null sky and valid captured sky pass\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
