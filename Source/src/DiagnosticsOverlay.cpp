#include "DiagnosticsOverlay.h"
#include "Profiler.h"
#include "SpikeSummary.h"
#include "LegitProfiler.h"
#include "imgui_impl_dx11.h"
#include <Windows.h>
#include <d3d11_1.h>
#include "../generated/EmbeddedShaders.h"
#include <detours/detours.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <memory>
#include <cstring>

namespace DCFDiag {
using Microsoft::WRL::ComPtr;
namespace {
std::recursive_mutex gate;
std::atomic_bool active{false};
Profiler profiler;
SpikeSummary spikeSummary;
// POOLTRACE BEGIN
Profiler poolProfiler;
PoolTrace::Pending poolPending;
PoolTrace::Summary poolSummary;
PoolTrace::Sample* poolFrame=nullptr;
bool poolTraceAvailable=false,poolFailureReported=false,traceNoticeReported=false;
bool focusPool=false,otherPass=false;
unsigned currentCubes=0,otherTimer=0;
uint64_t focusCursor=0,updateTag=0,poolSeenSequence=0,mainSeenSequence=0;
ComPtr<ID3DUserDefinedAnnotation> annotation;
bool mainEventOpen=false,poolEventOpen=false;
struct PoolClock {
    static inline double ticksToMs=0;
    static inline uint64_t frequency=0;
    static int64_t Now() {LARGE_INTEGER value;QueryPerformanceCounter(&value);return value.QuadPart;}
    static double Milliseconds(int64_t ticks) {return ticks*ticksToMs;}
};
PoolTrace::Recorder<PoolClock> poolCPU;
void InstallMarkers(Profiler& p,bool focused) {
    p.SetPerfEventCallbacks([focused](std::string_view name) {
        auto& open=focused?poolEventOpen:mainEventOpen;open=false;
        if(!annotation || !annotation->GetStatus())return;
        std::wstring event=L"DCF4::";event.append(name.begin(),name.end());
        event+=L" update="+std::to_wstring(updateTag);
        if(focused && poolFrame)event+=L" focusPool="+std::to_wstring(poolFrame->focus.slot);
        open=annotation->BeginEvent(event.c_str())>=0;
    },[focused](std::string_view) {
        auto& open=focused?poolEventOpen:mainEventOpen;
        if(open && annotation)annotation->EndEvent();open=false;
    });
}
void CollectPoolTrace() {
    const auto sequence=poolProfiler.GetSequence();
    if(!sequence || sequence==poolSeenSequence)return;
    poolSeenSequence=sequence;
    if(std::none_of(poolProfiler.GetResults().begin(),poolProfiler.GetResults().end(),[](const auto& r){return r.valid;}))return;
    auto* sample=poolPending.Find(poolProfiler.GetResultTag());if(!sample)return;
    bool valid=false;
    for(const auto& r:poolProfiler.GetResults())if(r.valid) {
        for(unsigned i=0;i<PoolTrace::timers;++i)if(r.name==PoolTrace::names[i]) {
            sample->passes[i]={true,r.gpuTimeMs,r.cpuTimeMs};valid=true;
            if(i==PoolTrace::dispatchTimer) {
                sample->counterValid=r.pipelineStatisticsValid;sample->invocations=r.csInvocations;
            }
            break;
        }
    }
    if(!valid)return;
    sample->queryAgeMs=poolProfiler.GetResultAgeMs();
    sample->poolReady=true;poolSummary.ObservePool(*sample);
}
void CollectMainTrace() {
    const auto sequence=profiler.GetSequence();
    if(!sequence || sequence==mainSeenSequence)return;
    mainSeenSequence=sequence;
    const auto tag=profiler.GetResultTag();
    auto& candidate=poolPending.entries[tag%poolPending.entries.size()];
    if(!tag || !candidate.metadata || candidate.tag!=tag)return;
    auto& sample=candidate;
    for(const auto& r:profiler.GetResults())if(r.valid) {
        if(r.name=="Pool replication") {sample.mainReady=true;sample.outerGpu=r.gpuTimeMs;sample.outerCpu=r.cpuTimeMs;}
        else if(r.name=="Timing control")sample.controlGpu=r.gpuTimeMs;
        else if(r.gpuTimeMs>sample.otherGpuMax) {sample.otherGpuMax=r.gpuTimeMs;sample.otherStage=r.name;}
    }
    if(sample.mainReady)poolSummary.ObserveMain(sample);
}
// POOLTRACE END
ULONGLONG nextTimingReport=0;
std::unique_ptr<ImGuiUtils::ProfilerGraph> gpuGraph,cpuGraph;
ImGuiContext* gui=nullptr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
IDXGISwapChain* target=nullptr; // borrowed; only compared, never dereferenced outside Present
DWORD renderThread=0;
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
PresentFn originalPresent=nullptr;
bool hooked=false, failed=false, visible=false, cpu=false, alpha=false, useMaterialMip=true;
bool keys[5]{};
bool showPreviews=false;
uint64_t seenSequence=0;
ULONGLONG nextPreview=0,lastUI=0;
unsigned materialMip=2;
double bindMean=0,bindMax=0; uint64_t bindSamples=0,bindCalls=0;
std::array<ComPtr<ID3D11ShaderResourceView>,5> sources;
const char* names[5]={"Capture (shared sky/geometry history)","Sky history","Fallback source (sky color outside; startup inside)","Inferred result","Final output (before material tint)"};
struct Preview { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
std::array<Preview,5> previews;
ComPtr<ID3D11ComputeShader> previewShader;
ComPtr<ID3D11Buffer> previewConstants;
ComPtr<ID3D11SamplerState> sampler;
const char* previewHLSL=R"(
TextureCube<float4> Cube:register(t0);
RWTexture2D<float4> Output:register(u0);
SamplerState Linear:register(s0);
cbuffer Params:register(b0) { float Mip; uint Alpha; uint Weighted; float Pad; };
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
 if(id.x>=384 || id.y>=64) return;
 uint face=id.x/64; float2 uv=2*((float2(id.x%64,id.y)+0.5)/64)-1; uv.y=-uv.y;
 float3 v;
 if(face==0)v=float3(1,uv.y,-uv.x); else if(face==1)v=float3(-1,uv.y,uv.x);
 else if(face==2)v=float3(uv.x,1,-uv.y); else if(face==3)v=float3(uv.x,-1,uv.y);
 else if(face==4)v=float3(uv.x,uv.y,1); else v=float3(-uv.x,uv.y,-1);
 float4 c=Cube.SampleLevel(Linear,normalize(v),Mip);
 if(Alpha!=0) { Output[id.xy]=float4(saturate(c.aaa),1); return; }
 if(Weighted!=0) {
  if(c.a<=0.0001) { float checker=((id.x/8+id.y/8)%2)?0.12:0.05; Output[id.xy]=float4(checker.xxx,1); return; }
  c.rgb/=c.a;
 }
 // Display only: common fixed exposure and gamma, never modifies source cubes.
 Output[id.xy]=float4(pow(saturate(max(c.rgb,0)),1.0/2.2),1);
})";

bool CreatePreviewResources() {
 const auto& code=DCF4Shaders::Preview;
 if(FAILED(device->CreateComputeShader(code.data,code.size,nullptr,&previewShader))) return false;
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 if(FAILED(device->CreateBuffer(&bd,nullptr,&previewConstants))) return false;
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(device->CreateSamplerState(&sd,&sampler))) return false;
 for(auto& p:previews) {
  D3D11_TEXTURE2D_DESC d{};d.Width=384;d.Height=64;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  if(FAILED(device->CreateTexture2D(&d,nullptr,&p.texture)) || FAILED(device->CreateShaderResourceView(p.texture.Get(),nullptr,&p.srv)) || FAILED(device->CreateUnorderedAccessView(p.texture.Get(),nullptr,&p.uav))) return false;
 }
 return true;
}
void UpdatePreviews() {
 auto now=GetTickCount64();if(now<nextPreview)return;nextPreview=now+200;
 ComPtr<ID3D11ComputeShader> oldShader;ID3D11ClassInstance* classes[256]{};UINT count=256;
 context->CSGetShader(&oldShader,classes,&count);
 ComPtr<ID3D11ShaderResourceView> oldSRV;ComPtr<ID3D11UnorderedAccessView> oldUAV;
 ComPtr<ID3D11Buffer> oldCB;ComPtr<ID3D11SamplerState> oldSampler;
 context->CSGetShaderResources(0,1,&oldSRV);context->CSGetUnorderedAccessViews(0,1,&oldUAV);
 context->CSGetConstantBuffers(0,1,&oldCB);context->CSGetSamplers(0,1,&oldSampler);
 context->CSSetShader(previewShader.Get(),nullptr,0);
 auto* cb=previewConstants.Get();auto* sm=sampler.Get();context->CSSetConstantBuffers(0,1,&cb);context->CSSetSamplers(0,1,&sm);
 for(unsigned i=0;i<5;++i) {
  if(!sources[i])continue;
  D3D11_SHADER_RESOURCE_VIEW_DESC desc{};sources[i]->GetDesc(&desc);
  if(desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURECUBE)continue;
  struct Params {float mip;UINT alpha;UINT weighted;float pad;} params{float(useMaterialMip?materialMip:0),UINT(alpha),UINT(i<2),0};
  // Inferred and sky resources have only mip zero; D3D clamps to their SRV range.
  context->UpdateSubresource(cb,0,nullptr,&params,0,0);
  auto* srv=sources[i].Get();auto* uav=previews[i].uav.Get();
  context->CSSetShaderResources(0,1,&srv);context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);context->Dispatch(48,8,1);
  srv=nullptr;uav=nullptr;context->CSSetShaderResources(0,1,&srv);context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
 }
 auto* srv=oldSRV.Get();auto* uav=oldUAV.Get();cb=oldCB.Get();sm=oldSampler.Get();
 context->CSSetShader(oldShader.Get(),classes,count);for(UINT i=0;i<count;++i)if(classes[i])classes[i]->Release();
 context->CSSetShaderResources(0,1,&srv);context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
 context->CSSetConstantBuffers(0,1,&cb);context->CSSetSamplers(0,1,&sm);
}
void AddGraphSample() {
 if(seenSequence==profiler.GetSequence())return;seenSequence=profiler.GetSequence();
 const uint32_t colors[]={legit::Colors::alizarin,legit::Colors::peterRiver,legit::Colors::emerald,legit::Colors::amethyst,legit::Colors::orange};
 for(unsigned mode=0;mode<2;++mode) {
  std::vector<legit::ProfilerTask> tasks;double offset=0;unsigned i=0;
  for(const auto& r:profiler.GetResults()) { double ms=r.valid?(mode?r.cpuTimeMs:r.gpuTimeMs):0; tasks.push_back({offset,offset+ms/1000.0,r.name,colors[i++%5]});offset+=ms/1000.0; }
  if(!tasks.empty())(mode?cpuGraph:gpuGraph)->LoadFrameData(tasks.data(),tasks.size());
 }
}
void DrawUI(float w,float h) {
 auto& io=ImGui::GetIO();io.DisplaySize={w,h};auto now=GetTickCount64();io.DeltaTime=lastUI?std::max(0.001f,float(now-lastUI)/1000):1.0f/60;lastUI=now;
 ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
 float scale=std::clamp(h/1080.0f,0.6f,1.4f);
 ImGui::SetNextWindowPos({12,12},ImGuiCond_Always);ImGui::SetNextWindowSize({std::min(w-24,1050*scale),std::min(h-24,1040*scale)},ImGuiCond_Always);
 ImGui::SetNextWindowBgAlpha(0.94f);
 ImGui::Begin("Dynamic Cubemap F4 - 1.5",nullptr,ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoCollapse);
 ImGui::SetWindowFontScale(scale);
 ImGui::TextUnformatted("Ctrl+F6 show/hide graph | Ctrl+F7 GPU/CPU | Ctrl+F10 previews (start OFF)");
 ImGui::Text("%s timings: per reflection update, not per game frame. Up to 300 samples.",cpu?"CPU submission":"GPU");
 ImGui::TextUnformatted("DispatchTrace: previews start OFF. Dispatch intervals include possible GPU waits; they are not pure busy time.");
 AddGraphSample();auto& graph=cpu?*cpuGraph:*gpuGraph;
 float avail=ImGui::GetContentRegionAvail().x;
 double totalNow=0; for(const auto& r:profiler.GetResults()) totalNow+=r.valid?(cpu?r.cpuTimeMs:r.gpuTimeMs)/1000.0:0;
 graph.RenderTimings(std::max(50.0f,avail-300*scale),300*scale,160*scale,0,std::max({0.00001f,graph.GetPeakFrameTime()*1.2f,float(totalNow)*1.2f}),scale);
 if(ImGui::BeginTable("timings",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
  ImGui::TableSetupColumn("Stage");ImGui::TableSetupColumn("Avg ms");ImGui::TableSetupColumn("P95 ms");ImGui::TableSetupColumn("P99 ms");ImGui::TableHeadersRow();
  for(const auto& r:profiler.GetResults()) {
   ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(r.name.c_str());
   ImGui::TableNextColumn();ImGui::Text("%.4f",cpu?r.cpuAvgMs:r.avgMs);ImGui::TableNextColumn();ImGui::Text("%.4f",cpu?r.cpuP95Ms:r.p95Ms);ImGui::TableNextColumn();ImGui::Text("%.4f",cpu?r.cpuP99Ms:r.p99Ms);
  }
  ImGui::EndTable();
 }
 if(profiler.GetResults().empty())ImGui::TextUnformatted("Waiting for completed GPU queries and reflection updates...");
 ImGui::Text("CPU binding hook (sampled, driver included): avg %.4f / max %.4f ms; %llu samples / %llu calls",bindMean,bindMax,(unsigned long long)bindSamples,(unsigned long long)bindCalls);
 if(showPreviews) {
 ImGui::Separator();ImGui::Text("Previews: %s | %s | 5 Hz | faces +X -X +Y -Y +Z -Z",alpha?"alpha/confidence":"RGB",useMaterialMip?"mip 2 (preview only)":"mip 0");
 ImGui::TextUnformatted("RGB: fixed gamma display, no auto exposure. Checkerboard = unobserved capture/sky.");
 float imageH=std::min({100*scale,avail/6,std::max(24.0f,(ImGui::GetContentRegionAvail().y-5*ImGui::GetTextLineHeightWithSpacing()-16)/5)});float imageW=imageH*6;
 for(unsigned i=0;i<5;++i) {
  ImGui::Text("%s%s",names[i],(i==1||i==3)?" (mip 0 only)":"");
  if(sources[i])ImGui::Image((ImTextureID)(intptr_t)previews[i].srv.Get(),{imageW,imageH});else ImGui::TextUnformatted("Not available yet");
 }
 } else ImGui::TextUnformatted("Cubemap previews OFF; dispatch trace is written to DynamicCubemapF4.log.");
 ImGui::End();ImGui::Render();
}
void RenderOverlay(IDXGISwapChain* chain) {
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> rtv;
    if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))) && SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&rtv))) {
     auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(gui);
     if(showPreviews)UpdatePreviews();D3D11_TEXTURE2D_DESC d{};buffer->GetDesc(&d);DrawUI(float(d.Width),float(d.Height));
     ID3D11RenderTargetView* oldRT[8]{};ComPtr<ID3D11DepthStencilView> oldDS;context->OMGetRenderTargets(8,oldRT,&oldDS);
     // ImGui DX11 restores its own pipeline state; explicitly preserve OM targets and tessellation stages.
     ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;ComPtr<ID3D11ComputeShader> cs;ID3D11ClassInstance* hc[256]{},*dc[256]{},*cc[256]{};UINT hn=256,dn=256,cn=256;
     context->HSGetShader(&hs,hc,&hn);context->DSGetShader(&ds,dc,&dn);context->CSGetShader(&cs,cc,&cn);
     auto* rt=rtv.Get();context->OMSetRenderTargets(1,&rt,nullptr);
     ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
     context->OMSetRenderTargets(8,oldRT,oldDS.Get());for(auto* v:oldRT)if(v)v->Release();
     context->HSSetShader(hs.Get(),hc,hn);context->DSSetShader(ds.Get(),dc,dn);context->CSSetShader(cs.Get(),cc,cn);
     for(UINT i=0;i<cn;++i)if(cc[i])cc[i]->Release();for(UINT i=0;i<hn;++i)if(hc[i])hc[i]->Release();for(UINT i=0;i<dn;++i)if(dc[i])dc[i]->Release();
     ImGui::SetCurrentContext(previous);
    }
}
HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* chain,UINT sync,UINT flags) {
 if(active.load(std::memory_order_relaxed) && !(flags&DXGI_PRESENT_TEST)) {
  std::lock_guard lock(gate);
  if(active && chain==target && GetCurrentThreadId()==renderThread && gui) {
   DXGI_SWAP_CHAIN_DESC desc{};chain->GetDesc(&desc);
   bool focus=GetForegroundWindow()==desc.OutputWindow;
   const int codes[]={VK_F6,VK_F7,VK_F8,VK_F9,VK_F10};
   for(int i=0;i<5;++i) {bool down=focus&&(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(codes[i])&0x8000);if(down&&!keys[i]) {if(i==0)visible=!visible;if(i==1)cpu=!cpu;if(i==2)useMaterialMip=!useMaterialMip;if(i==3)alpha=!alpha;if(i==4)showPreviews=!showPreviews;nextPreview=0;}keys[i]=down;}
   if(visible && focus) {
    RenderOverlay(chain);
   }
  }
 }
 return originalPresent(chain,sync,flags);
}
void ReleaseResources() {
 spikeSummary={};nextTimingReport=0;
 // POOLTRACE BEGIN
 poolCPU.Finish();poolFrame=nullptr;poolProfiler.Release();poolPending={};poolSummary={};
 poolTraceAvailable=poolFailureReported=traceNoticeReported=focusPool=otherPass=false;
 focusCursor=updateTag=poolSeenSequence=mainSeenSequence=0;
 // POOLTRACE END
 profiler.Release();annotation.Reset();mainEventOpen=poolEventOpen=false;showPreviews=false;sources={};previews={};previewShader.Reset();previewConstants.Reset();sampler.Reset();gpuGraph.reset();cpuGraph.reset();
 if(gui) {auto* prev=ImGui::GetCurrentContext();ImGui::SetCurrentContext(gui);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);ImGui::SetCurrentContext(prev==gui?nullptr:prev);gui=nullptr;}
 device.Reset();context.Reset();target=nullptr;seenSequence=0;lastUI=nextPreview=0;
}
}
void Disable() { std::lock_guard lock(gate);active=false;ReleaseResources();failed=false; }
bool Prepare(ID3D11Device* d,ID3D11DeviceContext* c,IDXGISwapChain* s) {
 std::lock_guard lock(gate);
 if(active && device.Get()==d && context.Get()==c && target==s)return true;
 if(failed || !d || !c || !s)return false;
 active=false;ReleaseResources();device=d;context=c;target=s;renderThread=GetCurrentThreadId();
 if(!hooked) {
  originalPresent=reinterpret_cast<PresentFn>((*reinterpret_cast<void***>(s))[8]);
  LONG e=DetourTransactionBegin();if(e==NO_ERROR)e=DetourUpdateThread(GetCurrentThread());
  if(e==NO_ERROR)e=DetourAttach(reinterpret_cast<PVOID*>(&originalPresent),reinterpret_cast<PVOID>(PresentHook));
  if(e!=NO_ERROR) {DetourTransactionAbort();failed=true;ReleaseResources();return false;}
  e=DetourTransactionCommit();if(e!=NO_ERROR){failed=true;ReleaseResources();return false;}hooked=true;
 }
 if(!profiler.Initialize(d,c) || !CreatePreviewResources()) {failed=true;ReleaseResources();return false;}
 // POOLTRACE BEGIN
 static_assert(PoolTrace::timers<=Profiler::kMaxTimers);
 poolTraceAvailable=poolProfiler.Initialize(d,c);
 if(poolTraceAvailable)poolProfiler.InitializePipelineStatistics();
 c->QueryInterface(IID_PPV_ARGS(&annotation));
 InstallMarkers(profiler,false);InstallMarkers(poolProfiler,true);
 LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);PoolClock::frequency=frequency.QuadPart;PoolClock::ticksToMs=1000.0/frequency.QuadPart;
 poolSummary.nextReport=GetTickCount64()+30000;
 // POOLTRACE END
 auto* prev=ImGui::GetCurrentContext();gui=ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().LogFilename=nullptr;
 ImFontConfig font; font.SizePixels=16; ImGui::GetIO().Fonts->AddFontDefault(&font);
 bool ok=ImGui_ImplDX11_Init(d,c);ImGui::StyleColorsDark();ImGui::SetCurrentContext(prev);
 if(!ok){failed=true;ReleaseResources();return false;}
 gpuGraph=std::make_unique<ImGuiUtils::ProfilerGraph>(300);cpuGraph=std::make_unique<ImGuiUtils::ProfilerGraph>(300);
 visible=false;showPreviews=false;std::fill(std::begin(keys),std::end(keys),false);nextTimingReport=GetTickCount64()+30000;active=true;return true;
}
void BeginUpdate(){if(!active)return;std::lock_guard l(gate);profiler.BeginFrame();profiler.SetFrameTag(++updateTag);if(poolTraceAvailable){poolProfiler.BeginFrame();CollectPoolTrace();poolProfiler.SetFrameTag(updateTag);}} // POOLTRACE
void BeginPass(const char* n){if(!active)return;std::lock_guard l(gate);profiler.BeginPass(n);}
void EndPass(){if(!active)return;std::lock_guard l(gate);profiler.EndPass();}
void EndUpdate(){if(!active)return;std::lock_guard l(gate);profiler.EndFrame();if(poolProfiler.IsRecordingFrame())poolProfiler.EndFrame();spikeSummary.Observe(profiler.GetSequence(),profiler.GetResults());CollectMainTrace();} // POOLTRACE
std::string TakeTimingReport() {
 if(!active)return {};std::lock_guard l(gate);
 const auto now=GetTickCount64();if(now<nextTimingReport)return {};nextTimingReport=now+30000;
 return spikeSummary.Report();
}
std::string TakeSpikeReport() {
 if(!active)return {};std::lock_guard l(gate);
 static uint64_t lastSequence=0;static ULONGLONG lastReport=0;
 auto sequence=profiler.GetSequence();if(sequence==lastSequence)return {};lastSequence=sequence;
 auto now=GetTickCount64();if(now-lastReport<1000)return {};
 std::ostringstream text;bool spike=false;
 for(const auto& r:profiler.GetResults()) if(r.valid && (r.gpuTimeMs>=0.2f || r.cpuTimeMs>=0.2f)) spike=true;
 if(!spike)return {};
 text<<"SPIKE: completed_update="<<sequence<<" overlay="<<visible;
 for(const auto& r:profiler.GetResults()) if(r.valid) text<<" ["<<r.name<<" GPU="<<r.gpuTimeMs<<" CPU="<<r.cpuTimeMs<<" ms]";
 lastReport=now;return text.str();
}
// POOLTRACE BEGIN
void BeginPoolReplication(bool reprojection,uint64_t atMs,unsigned pools) {
 if(!active)return;std::lock_guard l(gate);
 if(!poolTraceAvailable || poolFrame || !poolProfiler.IsRecordingFrame())return;
 poolProfiler.SetFrameTag(updateTag);poolFrame=&poolPending.Begin(updateTag);
 poolFrame->atMs=atMs;poolFrame->reprojection=reprojection;poolFrame->overlay=visible;
 poolFrame->submittedQpc=PoolClock::Now();
 poolFrame->previews=showPreviews;poolFrame->totalPools=pools;
 poolFrame->focus.slot=pools?unsigned(focusCursor++%pools):0;
 focusPool=otherPass=false;
 poolProfiler.BeginPass(PoolTrace::names[0]);poolProfiler.EndPass();
 poolProfiler.BeginPass(PoolTrace::names[1]);
}
void EndPoolStateSave() {if(!active)return;std::lock_guard l(gate);if(poolFrame)poolProfiler.EndPass();}
void BeginPool(unsigned slot,unsigned cubes,bool standalone,bool dirty,
    ID3D11ShaderResourceView* source,ID3D11Texture2D* destination) {
 if(!active)return;std::lock_guard l(gate);if(!poolFrame)return;
 currentCubes=cubes;focusPool=slot==poolFrame->focus.slot;
 if(focusPool) {
    if(otherPass){poolProfiler.EndPass();otherPass=false;}
    auto& row=poolFrame->focus;row.pools=1;row.cubes=cubes;row.dirty=dirty;row.standalone=standalone;
    row.source=reinterpret_cast<uintptr_t>(source);row.destination=reinterpret_cast<uintptr_t>(destination);
    if(source) {D3D11_SHADER_RESOURCE_VIEW_DESC d{};source->GetDesc(&d);row.format=d.Format;
        if(d.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DARRAY) {
            row.sourceMips=d.Texture2DArray.MipLevels;row.firstMip=d.Texture2DArray.MostDetailedMip;
            row.firstFace=d.Texture2DArray.FirstArraySlice;row.sourceFaces=d.Texture2DArray.ArraySize;
        }
    }
    poolCPU.Begin(&row);poolProfiler.BeginPass(PoolTrace::names[3]);
 } else {
    const unsigned timer=slot<poolFrame->focus.slot?2:9;
    if(!otherPass || otherTimer!=timer) {
        if(otherPass)poolProfiler.EndPass();
        poolProfiler.BeginPass(PoolTrace::names[timer]);otherPass=true;otherTimer=timer;
    }
 }
}
void PoolSelection(bool eligible,unsigned updated) {
 if(!active)return;std::lock_guard l(gate);if(!poolFrame)return;
 poolFrame->updated+=updated;poolFrame->reused+=currentCubes-updated;
 if(focusPool) {auto& row=poolFrame->focus;row.eligible=eligible;row.updated=updated;row.reused=row.cubes-updated;}
}
void PoolStep(PoolTrace::Step step) {
 if(!active)return;std::lock_guard l(gate);if(!poolFrame || !focusPool)return;
 poolCPU.Mark(step);poolProfiler.EndPass();
 if(step==PoolTrace::Step::Dispatch)poolProfiler.BeginStatisticsPass(PoolTrace::names[PoolTrace::dispatchTimer]);
 else poolProfiler.BeginPass(PoolTrace::names[3+unsigned(step)]);
}
void EndPool() {
 if(!active)return;std::lock_guard l(gate);if(!poolFrame)return;
 if(focusPool) {poolCPU.Finish();poolProfiler.EndPass();focusPool=false;}
}
void BeginPoolStateRestore() {
 if(!active)return;std::lock_guard l(gate);if(!poolFrame)return;
 if(otherPass){poolProfiler.EndPass();otherPass=false;}
 poolProfiler.BeginPass(PoolTrace::names[10]);
}
void EndPoolReplication() {
 if(!active)return;std::lock_guard l(gate);if(!poolFrame)return;
 poolProfiler.EndPass();poolProfiler.BeginPass(PoolTrace::names[11]);poolProfiler.EndPass();poolProfiler.EndFrame();
 poolFrame=nullptr;focusPool=false;
}
std::string TakePoolReport() {
 if(!active)return {};std::lock_guard l(gate);
 if(!poolTraceAvailable) {
    if(poolFailureReported)return {};poolFailureReported=true;
    return "DISPATCH TRACE: query resources unavailable; reflection rendering unchanged.";
 }
 std::string notice;
 if(!traceNoticeReported) {
    traceNoticeReported=true;
    notice="DISPATCH TRACE: rotating one focused pool per update; previews start OFF; dispatch GPU values are elapsed, not pure busy time; exact waits need a GPU timeline. pipelineCounters="+
        std::to_string(poolProfiler.HasPipelineStatistics())+" annotationAPI="+std::to_string(bool(annotation))+" qpcFrequency="+std::to_string(PoolClock::frequency);
 }
 const auto now=GetTickCount64();auto slow=poolSummary.TakeSlow(now);auto summary=poolSummary.Report(now,poolPending);
 if(!slow.empty()){if(!notice.empty())notice+='\n';notice+=slow;}
 if(!summary.empty()){if(!notice.empty())notice+='\n';notice+=summary;}
 return notice;
}
// POOLTRACE END
void ClearSkyPreview(){std::lock_guard l(gate);sources[1].Reset();}
void SetPreviews(ID3D11ShaderResourceView* c,ID3D11ShaderResourceView* s,ID3D11ShaderResourceView* f,ID3D11ShaderResourceView* i,ID3D11ShaderResourceView* o,unsigned m){if(!active)return;std::lock_guard l(gate);sources={c,s,f,i,o};materialMip=m;}
void SetBindingStats(double mean,double max,uint64_t samples,uint64_t calls){if(!active)return;std::lock_guard l(gate);bindMean=mean;bindMax=max;bindSamples=samples;bindCalls=calls;}
}
