#include "CacheDiagnostics.h"
// Included in the render-thread namespace after the binding state declarations.
struct PerfStats {
    unsigned n=0; double sum=0, maximum=0;
    void Add(double ms) { ++n; sum+=ms; maximum=std::max(maximum,ms); }
    std::string Text() const { return n ? std::to_string(sum/n)+"/"+std::to_string(maximum) : "NA"; }
};
LONGLONG PerfClock() { LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return v.QuadPart; }
double PerfMilliseconds(LONGLONG ticks) {
    static const double hz=[] { LARGE_INTEGER v{}; QueryPerformanceFrequency(&v); return double(v.QuadPart); }();
    return double(ticks)*1000.0/hz;
}
struct PerfQuerySlot {
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
    std::array<Microsoft::WRL::ComPtr<ID3D11Query>,4> stamps;
    bool pending=false, complete=false;
};
std::array<PerfQuerySlot,4> g_perfQueries;
PerfStats g_perfCPU[3],g_perfGPU[3],g_perfHook;
std::uint64_t g_perfHookCalls=0,g_perfCaptureCalls=0;
unsigned g_perfSkipped=0,g_perfInvalid=0;
int g_perfActive=-1; unsigned g_perfStage=0;
LONGLONG g_perfLastCPU=0;
ULONGLONG g_perfNextSample=0,g_perfNextPoll=0,g_perfLastReport=0;
bool g_perfQueryFailed=false;
std::size_t g_perfPeakPools=0,g_perfPeakViews=0;
struct PerfHookScope {
    LONGLONG start=0;
    PerfHookScope() { if (g_diagnosticsEnabled && (++g_perfHookCalls & 4095u)==0) start=PerfClock(); }
    ~PerfHookScope() { if(start) g_perfHook.Add(PerfMilliseconds(PerfClock()-start)); }
};
void PerfBeginCapture() {
    if(!g_diagnosticsEnabled || !g_liveEnabled) return;
    ++g_perfCaptureCalls;
    const auto now=GetTickCount64();
    if(now<g_perfNextSample || g_perfQueryFailed || !g_liveContext) return;
    g_perfNextSample=now+1000;
    for(unsigned i=0;i<g_perfQueries.size();++i) {
        auto& q=g_perfQueries[i]; if(q.pending) continue;
        if(!q.disjoint) {
            Microsoft::WRL::ComPtr<ID3D11Device> device; g_liveContext->GetDevice(device.GetAddressOf());
            D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            HRESULT hr=device->CreateQuery(&d,q.disjoint.GetAddressOf());
            d.Query=D3D11_QUERY_TIMESTAMP;
            for(auto& stamp:q.stamps) if(SUCCEEDED(hr)) hr=device->CreateQuery(&d,stamp.GetAddressOf());
            if(FAILED(hr)) { g_perfQueryFailed=true; Log("PERF: GPU query allocation failed; rendering unchanged."); return; }
        }
        g_perfActive=int(i); g_perfStage=0;
        g_liveContext->Begin(q.disjoint.Get());
        g_liveContext->End(q.stamps[0].Get());
        g_perfLastCPU=PerfClock(); return;
    }
    ++g_perfSkipped;
}
void PerfStage(unsigned stage) {
    if(g_perfActive<0) return;
    if(stage!=g_perfStage+1) return;
    const auto now=PerfClock(); g_perfCPU[stage-1].Add(PerfMilliseconds(now-g_perfLastCPU)); g_perfLastCPU=now;
    g_liveContext->End(g_perfQueries[g_perfActive].stamps[stage].Get());
    g_perfStage=stage;
}
void PerfEndCapture() {
    if(g_perfActive<0) return;
    auto& q=g_perfQueries[g_perfActive];
    if(g_perfStage==2) PerfStage(3);
    q.complete=g_perfStage==3;
    g_liveContext->End(q.disjoint.Get()); q.pending=true; g_perfActive=-1;
}
void PerfTick(ULONGLONG now) {
    if(!g_diagnosticsEnabled || !g_liveEnabled) return;
    if(!g_perfLastReport) g_perfLastReport=now;
    if(g_liveContext && now>=g_perfNextPoll) {
        g_perfNextPoll=now+1000;
        for(auto& q:g_perfQueries) {
            if(!q.pending) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
            HRESULT hr=g_liveContext->GetData(q.disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_FALSE) continue;
            if(FAILED(hr) || d.Disjoint || !d.Frequency || !q.complete) { q.pending=false; ++g_perfInvalid; continue; }
            UINT64 t[4]{}; bool ready=true, failed=false;
            for(unsigned j=0;j<4;++j) {
                hr=g_liveContext->GetData(q.stamps[j].Get(),&t[j],sizeof(t[j]),D3D11_ASYNC_GETDATA_DONOTFLUSH);
                ready=ready && hr==S_OK; failed=failed || FAILED(hr);
            }
            if(failed) { q.pending=false; ++g_perfInvalid; continue; }
            if(!ready) continue;
            q.pending=false;
            if(t[0]>t[1] || t[1]>t[2] || t[2]>t[3]) { ++g_perfInvalid; continue; }
            for(unsigned j=0;j<3;++j) g_perfGPU[j].Add(double(t[j+1]-t[j])*1000.0/d.Frequency);
        }
    }
    if(now-g_perfLastReport<30000) return;
    ReportNonCubeCache();
    const double seconds=double(now-g_perfLastReport)/1000.0;
    unsigned ready=0,cubes=0,pending=0; std::size_t bytes=0;
    for(const auto& q:g_perfQueries) pending+=q.pending;
    for(const auto& pool:g_universalPools) {
        ready+=pool.ready; cubes+=pool.cubes;
        if(pool.texture) { D3D11_TEXTURE2D_DESC d{}; pool.texture->GetDesc(&d);
            for(UINT m=0;m<d.MipLevels;++m) bytes+=std::size_t(std::max(1u,d.Width>>m))*std::max(1u,d.Height>>m)*d.ArraySize*8u;
        }
    }
    g_perfPeakPools=std::max(g_perfPeakPools,g_universalPools.size());
    g_perfPeakViews=std::max(g_perfPeakViews,g_universalViews.size());
    Log("PERF COUNTS: age_s="+std::to_string((now-g_logStart)/1000)+" enabled="+std::to_string(g_liveEnabled)+
        " pools="+std::to_string(g_universalPools.size())+" ready="+std::to_string(ready)+" cubes="+std::to_string(cubes)+
        " authoredViews="+std::to_string(g_universalViews.size())+" nonCubeViews="+std::to_string(g_universalNonCubeViews.size())+
        " replacementViews="+std::to_string(g_materialViewLookup.size())+" peakPools="+std::to_string(g_perfPeakPools)+
        " peakAuthoredViews="+std::to_string(g_perfPeakViews)+" poolTextureMiB_est="+std::to_string(double(bytes)/1048576.0)+
        " updates_per_s="+std::to_string(g_perfCaptureCalls/seconds)+" binds_per_s="+std::to_string(g_perfHookCalls/seconds));
    Log("PERF TIMING: mean/max_ms CPU_capture="+g_perfCPU[0].Text()+" CPU_infer="+g_perfCPU[1].Text()+
        " CPU_publish="+g_perfCPU[2].Text()+" GPU_capture="+g_perfGPU[0].Text()+" GPU_infer="+g_perfGPU[1].Text()+
        " GPU_publish="+g_perfGPU[2].Text()+" CPU_bind_with_driver="+g_perfHook.Text()+
        " hookSamples="+std::to_string(g_perfHook.n)+" gpuSamples="+std::to_string(g_perfGPU[0].n)+
        " pending="+std::to_string(pending)+" dropped="+std::to_string(g_perfSkipped)+" invalid="+std::to_string(g_perfInvalid));
    for(auto& v:g_perfCPU) v={}; for(auto& v:g_perfGPU) v={}; g_perfHook={};
    g_perfHookCalls=0; g_perfCaptureCalls=0; g_perfSkipped=0; g_perfInvalid=0; g_perfLastReport=now;
}


void SetDiagnosticsEnabled(bool enabled) {
    if(g_diagnosticsEnabled==enabled) return;
    // Runs between updates on the render thread; never wait for pending queries.
    PerfEndCapture();
    for(auto& q:g_perfQueries) q={};
    for(auto& s:g_perfCPU) s={}; for(auto& s:g_perfGPU) s={}; g_perfHook={};
    g_perfHookCalls=0; g_perfCaptureCalls=0; g_perfSkipped=0; g_perfInvalid=0;
    g_perfActive=-1; g_perfStage=0; g_perfNextSample=0; g_perfNextPoll=0; g_perfLastReport=0;
    g_perfQueryFailed=false; g_perfPeakPools=0; g_perfPeakViews=0;
    g_cacheHits=g_cacheMisses=g_cacheInserts=g_cacheEvictions=0;
    g_diagnosticsEnabled=enabled;
    if(enabled) Log("DIAGNOSTICS: enabled; timing and cache counters start fresh.");
}
