// SPDX-License-Identifier: GPL-3.0-only
// Reuse the actual overlay, CS query ring, WARP helpers and embedded replication shader.
#define main ExistingOverlayTests
#include "overlay_gpu_test.cpp"
#undef main
#include <d3d11sdklayers.h>
struct Style {
    float tint[128][4]{};
    float intensity[128][4]{};
    unsigned cubes[128]{};
};
ComPtr<ID3D11Texture2D> MakeCube(ID3D11Device* d,UINT bind) {
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=8;td.ArraySize=6;
    td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
    td.BindFlags=bind;td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> result;check(d->CreateTexture2D(&td,nullptr,&result));return result;
}
std::vector<unsigned char> AllPixels(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);td.BindFlags=td.MiscFlags=0;
    td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;check(d->CreateTexture2D(&td,nullptr,&read));c->CopyResource(read.Get(),texture);
    std::vector<unsigned char> pixels;
    for(unsigned face=0;face<6;++face)for(unsigned mip=0;mip<8;++mip) {
        D3D11_MAPPED_SUBRESOURCE mapped{};const unsigned sub=D3D11CalcSubresource(mip,face,8);
        check(c->Map(read.Get(),sub,D3D11_MAP_READ,0,&mapped));
        unsigned n=128>>mip;
        for(unsigned y=0;y<n;++y) {
            const auto* p=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch;
            pixels.insert(pixels.end(),p,p+n*8);
        }
        c->Unmap(read.Get(),sub);
    }
    return pixels;
}
int main() {
    ExistingOverlayTests();
    HWND hwnd=CreateWindowW(L"DCF4HiddenTest",L"Dispatch trace test",WS_OVERLAPPEDWINDOW,0,0,1280,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(hwnd);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=1280;sd.BufferDesc.Height=1080;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=1;sd.OutputWindow=hwnd;sd.Windowed=TRUE;
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;ComPtr<IDXGISwapChain> swap;D3D_FEATURE_LEVEL level;
    auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&level,&ctx);
    if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING) {
        std::cout<<"Debug layer unavailable; continuing WARP validation\n";
        hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&level,&ctx);
    }
    check(hr);ComPtr<ID3D11InfoQueue> messages;dev.As(&messages);
    assert(DCFDiag::Prepare(dev.Get(),ctx.Get(),swap.Get()) && DCFDiag::poolTraceAvailable);
    assert(!DCFDiag::visible && !DCFDiag::showPreviews && DCFDiag::poolProfiler.HasPipelineStatistics());
    const float sentinel[]={.25f,.5f,.75f,1};
    ctx->ClearUnorderedAccessViewFloat(DCFDiag::previews[0].uav.Get(),sentinel);
    auto preview=ReadRGBA(dev.Get(),ctx.Get(),DCFDiag::previews[0].texture.Get());
    DCFDiag::nextPreview=0;DCFDiag::RenderOverlay(swap.Get());
    assert(preview==ReadRGBA(dev.Get(),ctx.Get(),DCFDiag::previews[0].texture.Get()));
    assert(!DCFDiag::nextPreview); // Showing the graph cannot dispatch hidden previews.
    if(messages)messages->ClearStoredMessages();

    auto input=MakeCube(dev.Get(),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    auto output=MakeCube(dev.Get(),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11ShaderResourceView> source;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;sv.Texture2DArray={0,8,0,6};
    check(dev->CreateShaderResourceView(input.Get(),&sv,&source));
    std::array<ComPtr<ID3D11UnorderedAccessView>,8> targets;
    for(unsigned mip=0;mip<8;++mip) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=sv.Format;uv.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;uv.Texture2DArray={mip,0,6};
        ComPtr<ID3D11UnorderedAccessView> original;
        check(dev->CreateUnorderedAccessView(input.Get(),&uv,&original));
        const float color[]={.15f+.025f*mip,.2f,.4f,1};ctx->ClearUnorderedAccessViewFloat(original.Get(),color);
        check(dev->CreateUnorderedAccessView(output.Get(),&uv,&targets[mip]));
    }
    Style style;style.tint[0][0]=.8f;style.tint[0][1]=1;style.tint[0][2]=.3f;style.tint[0][3]=.3f;style.intensity[0][0]=.4f;
    ComPtr<ID3D11Buffer> cb;D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(style);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{&style,0,0};check(dev->CreateBuffer(&bd,&initial,&cb));
    ComPtr<ID3D11ComputeShader> cs;check(dev->CreateComputeShader(DCF4Shaders::UniversalReplicate.data,DCF4Shaders::UniversalReplicate.size,nullptr,&cs));
    auto bind=[&] {
        auto* constants=cb.Get();ctx->CSSetConstantBuffers(0,1,&constants);
        ID3D11ShaderResourceView* inputs[]={source.Get(),source.Get()};
        ID3D11UnorderedAccessView* outputs[8]{};for(unsigned mip=0;mip<8;++mip)outputs[mip]=targets[mip].Get();
        ctx->CSSetShaderResources(0,2,inputs);ctx->CSSetUnorderedAccessViews(0,8,outputs,nullptr);
    };
    auto unbind=[&] {
        ID3D11ShaderResourceView* srvs[2]{};ID3D11UnorderedAccessView* uavs[8]{};
        ctx->CSSetShaderResources(0,2,srvs);ctx->CSSetUnorderedAccessViews(0,8,uavs,nullptr);
    };
    ctx->CSSetShader(cs.Get(),nullptr,0);bind();ctx->Dispatch(344,1,6);unbind();
    const auto reference=AllPixels(dev.Get(),ctx.Get(),output.Get());
    for(unsigned u=1;u<=36;++u) {
        DCFDiag::BeginUpdate();DCFDiag::BeginPass("Timing control");DCFDiag::EndPass();DCFDiag::BeginPass("Pool replication");
        DCFDiag::BeginPoolReplication(u%2,u*100,14);DCFDiag::EndPoolStateSave();
        for(unsigned i=0;i<14;++i) {
            DCFDiag::BeginPool(i,1,true,false,source.Get(),output.Get());
            const unsigned updated=(u%7==0 && i==DCFDiag::poolFrame->focus.slot)?0:1;
            DCFDiag::PoolSelection(false,updated);
            if(updated) {
                DCFDiag::PoolStep(PoolTrace::Step::Constants);
                DCFDiag::PoolStep(PoolTrace::Step::Bindings);bind();
                DCFDiag::PoolStep(PoolTrace::Step::Dispatch);ctx->Dispatch(344,1,6);
                DCFDiag::PoolStep(PoolTrace::Step::Unbind);unbind();
                DCFDiag::PoolStep(PoolTrace::Step::Commit);
            }
            DCFDiag::EndPool();
        }
        DCFDiag::BeginPoolStateRestore();DCFDiag::EndPoolReplication();DCFDiag::EndPass();DCFDiag::EndUpdate();ctx->Flush();Sleep(10);
    }
    assert(reference==AllPixels(dev.Get(),ctx.Get(),output.Get()));
    const auto tag=DCFDiag::poolProfiler.GetResultTag();auto* s=DCFDiag::poolPending.Find(tag);
    assert(s && s->poolReady && s->mainReady && s->atMs==tag*100 && s->reprojection==bool(tag%2));
    assert(!s->previews && !s->overlay && s->focus.pools==1 && s->focus.sourceFaces==6);
    assert(DCFDiag::poolSummary.counterSamples>0 && DCFDiag::poolSummary.counterMismatches==0);
    assert(DCFDiag::poolSummary.focusCoverage[13]>0 && !DCFDiag::poolPending.unmatched && !DCFDiag::poolPending.overwritten);
    assert(DCFDiag::poolProfiler.GetResults().size()<=Profiler::kMaxTimers && s->queryAgeMs>=0);
    for(unsigned u=0;u<5;++u) {
        DCFDiag::BeginUpdate();DCFDiag::BeginPass("Capture setup");DCFDiag::EndPass();DCFDiag::EndUpdate();
        assert(!DCFDiag::poolProfiler.IsRecordingFrame());ctx->Flush();Sleep(10);
    }
    assert(!DCFDiag::poolPending.unmatched && !DCFDiag::poolPending.overwritten);
    if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i) {
        SIZE_T size=0;check(messages->GetMessage(i,nullptr,&size));std::vector<char> bytes(size);
        auto* m=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());check(messages->GetMessage(i,m,&size));
        if(m->Severity==D3D11_MESSAGE_SEVERITY_ERROR || m->Severity==D3D11_MESSAGE_SEVERITY_CORRUPTION){std::cerr<<m->pDescription<<'\n';std::abort();}
        assert(m->ID!=D3D11_MESSAGE_ID_QUERY_BEGIN_DUPLICATE && m->ID!=D3D11_MESSAGE_ID_QUERY_BEGIN_ABANDONING_PREVIOUS_RESULTS &&
               m->ID!=D3D11_MESSAGE_ID_QUERY_END_ABANDONING_PREVIOUS_RESULTS && m->ID!=D3D11_MESSAGE_ID_QUERY_END_WITHOUT_BEGIN);
    }
    Profiler optional;assert(!optional.InitializePipelineStatistics());assert(optional.Initialize(dev.Get(),ctx.Get()));assert(!optional.HasPipelineStatistics());
    DCFDiag::poolProfiler.Release();DCFDiag::poolTraceAvailable=false;
    DCFDiag::BeginPoolReplication(false,0,1);assert(!DCFDiag::poolFrame);
    assert(DCFDiag::TakePoolReport().find("unavailable")!=std::string::npos && DCFDiag::TakePoolReport().empty());
    DCFDiag::Disable();assert(!DCFDiag::poolTraceAvailable && !DCFDiag::poolCPU.row && !DCFDiag::poolFrame && !DCFDiag::annotation);
    assert(DCFDiag::Prepare(dev.Get(),ctx.Get(),swap.Get()) && !DCFDiag::showPreviews && !DCFDiag::visible && DCFDiag::focusCursor==0);
    DCFDiag::Disable();ctx->ClearState();DestroyWindow(hwnd);
    std::cout<<"PASS: real WARP dispatch-only timestamp scopes and exact invocation counts, rotating 14-pool focus, skipped dispatch, exact-tag joins, bit-identical all-8-mip output with tracing ON, hidden previews, debug query validation, aborted publication and optional/disabled/reset paths\n";
}
