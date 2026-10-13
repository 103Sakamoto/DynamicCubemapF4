// SPDX-License-Identifier: GPL-3.0-only
// Reuse the existing WARP creation/error helpers; production pool functions are extracted.
#include <string>
#include <unordered_map>
#define main ExistingSceneTests
#include "scene_fallback_gpu_test.cpp"
#undef main
#include "PoolMipReference.h"

constexpr UINT kCubeMipCount=8;
struct UniversalStyleConstants {
    std::array<DirectX::XMFLOAT4,128> tint{};
    std::array<DirectX::XMFLOAT4,128> intensity{};
    std::array<UINT,128> updateCubes{};
};
struct UniversalPool {
    UINT cubes=0;
    bool standalone=false;
    UINT lastUpdatedCubes=0;
    ComPtr<ID3D11ShaderResourceView> originalView;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> originalMips;
    std::array<ComPtr<ID3D11UnorderedAccessView>,8> mipUAVs;
    UniversalStyleConstants style{};
    ComPtr<ID3D11Buffer> tintConstants;
    bool tintDirty=true;
};
ID3D11DeviceContext* g_liveContext=nullptr;
ComPtr<ID3D11ComputeShader> g_universalReplicateCS;
ComPtr<ID3D11ShaderResourceView> g_universalLiveMips;
bool g_streamingRoutesVerified=true;
namespace ArrayWrites {
struct Stamp { uint64_t writes=0,commands=0;bool quiet=false; };
bool installed=true;
ID3D11DeviceContext* installedContext=nullptr;
Stamp now{0,0,true};
std::unordered_map<ID3D11ShaderResourceView*,Stamp> cached;
Stamp Snapshot() { return now; }
#include "current_stamp.inc"
}
std::unordered_map<ID3D11ShaderResourceView*,ArrayWrites::Stamp> g_retainedPoolSources;
#include "retained_source.inc"
#include "production_pool.inc"
void ReplicateForTest(UniversalPool& pool) { UniversalReplicationScope state; UniversalDispatchPool(pool); }

ComPtr<ID3D11Texture2D> Texture(ID3D11Device* device,UINT size,UINT mips,UINT layers)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width=d.Height=size;d.MipLevels=mips;d.ArraySize=layers;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> result;Check(device->CreateTexture2D(&d,nullptr,&result));return result;
}
void Fill(ID3D11DeviceContext* context,ID3D11Texture2D* texture,UINT seed)
{
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    for(UINT face=0;face<d.ArraySize;++face)for(UINT mip=0;mip<d.MipLevels;++mip) {
        const UINT size=std::max(1u,d.Width>>mip);
        std::vector<UINT16> data(size*size*4);
        for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x) {
            Pixel p={float((x+3*y+face*11+mip*7+seed)%97)/16,
                     float((x+y+face*3+mip+seed)%31)/32,
                     float((2*x+y+face+mip*5+seed)%67)/32,
                     float((x+y+face+mip)%13)/16};
            if((x+y+seed)%53==0)p={0,0,0,p[3]};
            if((x+2*y+seed)%127==0)p[0]=-0.125f;
            for(UINT c=0;c<4;++c)data[(y*size+x)*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(p[c]);
        }
        context->UpdateSubresource(texture,D3D11CalcSubresource(mip,face,d.MipLevels),nullptr,data.data(),size*8,0);
    }
}
ComPtr<ID3D11ShaderResourceView> ArrayView(ID3D11Device* device,ID3D11Texture2D* texture,
    UINT firstMip,UINT count,UINT firstFace,UINT faces)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    v.Texture2DArray={firstMip,count,firstFace,faces};
    ComPtr<ID3D11ShaderResourceView> result;Check(device->CreateShaderResourceView(texture,&v,&result));return result;
}
UniversalPool Pool(ID3D11Device* device,UINT cubes)
{
    UniversalPool pool;pool.cubes=cubes;pool.texture=Texture(device,128,8,cubes*6);
    for(UINT mip=0;mip<8;++mip) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        v.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray={mip,0,cubes*6};
        Check(device->CreateUnorderedAccessView(pool.texture.Get(),&v,&pool.mipUAVs[mip]));
    }
    // Untinted, tinted, retained-authored and dim-eye material policies.
    for(UINT cube=0;cube<cubes;++cube) {
        switch(cube%4) {
        case 0: pool.style.tint[cube]={1,1,1,-1};pool.style.intensity[cube]={0.25f,0,0,0};break;
        case 1: pool.style.tint[cube]={0.3f,0.7f,1,0};pool.style.intensity[cube]={0.8f,0,0,0};break;
        case 2: pool.style.tint[cube]={1,1,1,-2};pool.style.intensity[cube]={1,0,0,0};break;
        case 3: pool.style.tint[cube]={0.7f,0.4f,0.2f,0};pool.style.intensity[cube]={0.01f,0,0,0};break;
        }
    }
    D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(UniversalStyleConstants);
    b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    Check(device->CreateBuffer(&b,nullptr,&pool.tintConstants));return pool;
}
std::vector<UINT16> ReadAll(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&d,nullptr,&staging));
    context->CopyResource(staging.Get(),texture);std::vector<UINT16> result;
    for(UINT face=0;face<d.ArraySize;++face)for(UINT mip=0;mip<d.MipLevels;++mip) {
        const UINT size=std::max(1u,d.Width>>mip);
        D3D11_MAPPED_SUBRESOURCE mapped{};const UINT sub=D3D11CalcSubresource(mip,face,d.MipLevels);
        Check(context->Map(staging.Get(),sub,D3D11_MAP_READ,0,&mapped));
        for(UINT y=0;y<size;++y) {
            auto* row=reinterpret_cast<const UINT16*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
            result.insert(result.end(),row,row+size*4);
        }
        context->Unmap(staging.Get(),sub);
    }
    return result;
}
int main() try
{
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    g_liveContext=context.Get();
    ArrayWrites::installedContext=context.Get();
    Check(device->CreateComputeShader(DCF4Shaders::UniversalReplicate.data,DCF4Shaders::UniversalReplicate.size,nullptr,&g_universalReplicateCS));
    ComPtr<ID3DBlob> code,errors;
    Check(D3DCompile(AcceptedPoolMipShader,strlen(AcceptedPoolMipShader),"accepted-pool",nullptr,nullptr,"main","cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors));
    ComPtr<ID3D11ComputeShader> reference;
    Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&reference));
    const auto live=Texture(device.Get(),128,8,6);Fill(context.Get(),live.Get(),1);
    g_universalLiveMips=ArrayView(device.Get(),live.Get(),0,8,0,6);
    UINT fixtures=0,comparisons=0;
    for(const auto config:std::array<std::array<UINT,4>,5>{{{1,128,8,0},{4,64,7,2},{4,256,9,0},{4,8,1,0},{4,32,3,1}}}) {
        const auto [cubes,originalSize,originalLevels,firstMip]=config;
        const UINT firstFace=cubes==1?0:6;
        const auto original=Texture(device.Get(),originalSize,originalLevels,cubes*6+firstFace);
        Fill(context.Get(),original.Get(),17);
        auto actual=Pool(device.Get(),cubes),expected=Pool(device.Get(),cubes),caller=Pool(device.Get(),1);
        actual.originalMips=ArrayView(device.Get(),original.Get(),firstMip,originalLevels-firstMip,firstFace,cubes*6);
        actual.originalView=actual.originalMips;
        actual.standalone=cubes==1;
        ArrayWrites::cached.clear();g_retainedPoolSources.clear();ArrayWrites::now={0,0,true};
        auto* cb=actual.tintConstants.Get();
        ID3D11ShaderResourceView* savedViews[]={g_universalLiveMips.Get(),actual.originalMips.Get()};
        ID3D11UnorderedAccessView* savedTargets[8]{};
        for(UINT mip=0;mip<8;++mip)savedTargets[mip]=caller.mipUAVs[mip].Get();
        context->CSSetShader(reference.Get(),nullptr,0);context->CSSetShaderResources(0,2,savedViews);
        context->CSSetUnorderedAccessViews(0,8,savedTargets,nullptr);context->CSSetConstantBuffers(0,1,&cb);
        ReplicateForTest(actual);
        Require(!actual.tintDirty,"Tint upload did not commit");
        ID3D11ComputeShader* restoredShader=nullptr;context->CSGetShader(&restoredShader,nullptr,nullptr);
        Require(restoredShader==reference.Get(),"Caller shader not restored");restoredShader->Release();
        ID3D11UnorderedAccessView* restoredTargets[8]{};context->CSGetUnorderedAccessViews(0,8,restoredTargets);
        for(UINT mip=0;mip<8;++mip) {
            Require(restoredTargets[mip]==savedTargets[mip],"Caller UAV slot not restored");restoredTargets[mip]->Release();
        }
        ID3D11ShaderResourceView* restoredViews[2]{};context->CSGetShaderResources(0,2,restoredViews);
        for(UINT i=0;i<2;++i){Require(restoredViews[i]==savedViews[i],"Caller SRV not restored");restoredViews[i]->Release();}
        ID3D11Buffer* restoredCB=nullptr;context->CSGetConstantBuffers(0,1,&restoredCB);
        Require(restoredCB==cb,"Caller CB not restored");restoredCB->Release();
        ID3D11UnorderedAccessView* nullTargets[8]{};context->CSSetUnorderedAccessViews(0,8,nullTargets,nullptr);
        auto compareReference=[&] {
        context->CSSetShader(reference.Get(),nullptr,0);context->CSSetConstantBuffers(0,1,&cb);
        // Execute the accepted selected-mip shader eight times, as VanillaMip01 did.
        for(UINT mip=0;mip<8;++mip) {
            const auto source=ArrayView(device.Get(),live.Get(),mip,1,0,6);
            const auto authored=ArrayView(device.Get(),original.Get(),std::min(firstMip+mip,originalLevels-1),1,firstFace,cubes*6);
            ID3D11ShaderResourceView* views[]={source.Get(),authored.Get()};
            auto* target=expected.mipUAVs[mip].Get();
            context->CSSetShaderResources(0,2,views);context->CSSetUnorderedAccessViews(0,1,&target,nullptr);
            const UINT size=128>>mip;context->Dispatch((size+7)/8,(size+7)/8,cubes*6);
            context->CSSetUnorderedAccessViews(0,8,nullTargets,nullptr);
        }
        const auto before=ReadAll(device.Get(),context.Get(),expected.texture.Get());
        const auto after=ReadAll(device.Get(),context.Get(),actual.texture.Get());
        Require(before.size()==after.size(),"Readback shape mismatch");
        for(std::size_t i=0;i<before.size();++i) {
            if(before[i]!=after[i]) {
                std::cerr<<"Fixture "<<fixtures<<" value "<<i<<": "<<before[i]<<" vs "<<after[i]<<'\n';
                throw std::runtime_error("Full-chain texels differ from accepted per-mip shader");
            }
        }
        ++comparisons;
        };
        Require(actual.lastUpdatedCubes==cubes,"First publication omitted a cube");
        compareReference();
        auto verifyUpdate=[&](UINT updated,UINT seed) {
            context->ClearState();Fill(context.Get(),live.Get(),seed);
            ReplicateForTest(actual);
            Require(actual.lastUpdatedCubes==updated,"Unexpected cube update count");
            compareReference();
        };
        auto cacheCurrent=[&] { ArrayWrites::cached[actual.originalView.Get()]=ArrayWrites::now; };
        const auto dynamicStyle=actual.style;
        // Analysis completion alone is not proof that the output copied that source epoch.
        cacheCurrent();
        Require(!CanReuseRetainedPoolSource(actual.originalView.Get()),"Analysis reused before a verified publication");
        verifyUpdate(cubes,2);
        Require(CanReuseRetainedPoolSource(actual.originalView.Get()),"Verified publication not retained");
        verifyUpdate(cubes==1?1:3,3);
        for(UINT cube=0;cube<cubes;++cube) actual.style.tint[cube].w=-2;
        actual.tintDirty=true;verifyUpdate(cubes,4);
        verifyUpdate(cubes==1?1:0,5); // Entirely static arrays need no dispatch.
        // A fresh analysis cannot hide a write since the previous publication.
        context->ClearState();Fill(context.Get(),original.Get(),31);++ArrayWrites::now.writes;cacheCurrent();
        Require(!CanReuseRetainedPoolSource(actual.originalView.Get()),"Fresh analysis hid an unpublished write");
        verifyUpdate(cubes,6);
        verifyUpdate(cubes==1?1:0,7);
        // Stale analysis and overlapping updates must fall back to full replication.
        context->ClearState();Fill(context.Get(),original.Get(),39);++ArrayWrites::now.writes;
        verifyUpdate(cubes,8);cacheCurrent();verifyUpdate(cubes,9);
        ArrayWrites::now.quiet=false;verifyUpdate(cubes,10);
        ArrayWrites::now.quiet=true;cacheCurrent();verifyUpdate(cubes,11);
        ++ArrayWrites::now.commands;cacheCurrent();verifyUpdate(cubes,12);
        // Losing verified tracking or changing immediate context cannot reuse static output.
        g_streamingRoutesVerified=false;verifyUpdate(cubes,13);
        g_streamingRoutesVerified=true;verifyUpdate(cubes,14);
        ArrayWrites::installed=false;verifyUpdate(cubes,15);
        ArrayWrites::installed=true;verifyUpdate(cubes,16);
        ArrayWrites::installedContext=nullptr;verifyUpdate(cubes,17);
        ArrayWrites::installedContext=context.Get();verifyUpdate(cubes,18);
        ArrayWrites::cached.clear();verifyUpdate(cubes,19);cacheCurrent();verifyUpdate(cubes,20);
        // Restore authored tint policies; the packed update map must expand then compact.
        actual.style=dynamicStyle;actual.tintDirty=true;verifyUpdate(cubes,21);
        verifyUpdate(cubes==1?1:3,22);
        // Verify the game-facing cube/array descriptor can access all eight mips.
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        v.ViewDimension=cubes==1?D3D11_SRV_DIMENSION_TEXTURECUBE:D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        if(cubes==1)v.TextureCube={0,8};else v.TextureCubeArray={0,8,0,cubes};
        ComPtr<ID3D11ShaderResourceView> view;Check(device->CreateShaderResourceView(actual.texture.Get(),&v,&view));
        ++fixtures;
    }
    ID3D11ShaderResourceView* nullViews[2]{};context->CSSetShaderResources(0,2,nullViews);
    context->ClearState();g_universalLiveMips.Reset();g_universalReplicateCS.Reset();g_liveContext=nullptr;
    std::cout<<"PASS: "<<fixtures<<" WARP fixtures / "<<comparisons<<" updates; every half-float channel of all 8 mips bit-identical to accepted shader; retained reuse, all-static zero dispatch, publication/write/command/tint/tracking invalidation, standalone refresh, source size/mip clamp/face offset and all 8 caller UAV slots pass\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
