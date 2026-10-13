// The runner extracts the actual production routines into production_interop.inc.
// These mocks check CPU binding/ownership behavior, not the D3D driver or F4SE.
#include "ReflectionInterop.h"
#include "TintReports.h"
#include <array>
#include <atomic>
#include <climits>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
using Microsoft::WRL::ComPtr;
struct XMFLOAT4 {
    float x=0,y=0,z=0,w=0;
    XMFLOAT4()=default;
    XMFLOAT4(float a,float b,float c,float d):x(a),y(b),z(c),w(d) {}
};
struct ID3D11DeviceContext {
    ComPtr<ID3D11ShaderResourceView> slots[128];
    void PSGetShaderResources(UINT start,UINT count,ID3D11ShaderResourceView** out) {
        for(UINT i=0;i<count;++i) {
            out[i]=slots[start+i].Get(); if(out[i])out[i]->AddRef();
        }
    }
};
unsigned nativeCalls=0;
ID3D11ShaderResourceView* const* lastNativeArray=nullptr;
void BindNative(ID3D11DeviceContext* context,UINT start,UINT count,
                ID3D11ShaderResourceView* const* views) {
    ++nativeCalls;lastNativeArray=views;
    for(UINT i=0;i<count;++i) context->slots[start+i]=views ? views[i] : nullptr;
}
auto g_originalPSResources=&BindNative;
ID3D11DeviceContext* g_liveContext=nullptr;
ComPtr<ID3D11ShaderResourceView> g_liveSRV;
bool g_liveEnabled=true, g_liveReady=true;
bool g_liveCubemapSlots[128]{};
std::uint32_t g_liveCubemapSlotCount=0;
std::atomic<bool> g_gameplayReady{true};
std::vector<std::string> logs;
void Log(const std::string& s) { logs.push_back(s); }
bool UniversalFloatSampleFormat(int format) { return format==1; }
void SetLiveCubemapSlot(UINT slot,bool bound);
ID3D11ShaderResourceView* namedExterior=nullptr;
bool IsExteriorFallbackView(ID3D11ShaderResourceView* view) { return view==namedExterior; }
std::atomic_bool g_diagnosticsEnabled{true}; struct PerfHookScope { void BeginDriver(){} }; unsigned lookupCalls=0;
#include "production_interop.inc"

int main() {
    using namespace RealisticReflections::Interop;
    ID3D11Texture2D authoredResource,liveResource,glassResource,resourceOnly,otherResource;
    ID3D11ShaderResourceView authored(&authoredResource),live(&liveResource),glass(&glassResource),
        glassViaResource(&resourceOnly),other(&otherResource),flat(&otherResource);
    flat.desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    ID3D11DeviceContext context;
    g_liveContext=&context; g_liveSRV=&live; g_namedReferenceReady=true; RebuildMaterialViewLookup();
    TagReflectionReplacement(&liveResource);
    assert(IsPeerReplacement(&live));
    assert(TagOwned(&glass)); assert(TagOwned(&resourceOnly));
    assert(!IsOwned(&authored)); assert(!IsOwned(nullptr));
    const int glassRefs=glass.refs, resourceViewRefs=glassViaResource.refs;
    const int resourceRefs=resourceOnly.refs;
    auto bind=[&](ID3D11ShaderResourceView* v) { Hook_PSResources(&context,8,1,&v); };
    auto current=[&] { return context.slots[8].Get(); };

    // Named exterior reference must become dynamic even without a material probe.
    namedExterior=&other;
    bind(&other); assert(current()==&other);
    g_liveReady=false; bind(&other); assert(current()==&other);
    g_liveReady=true; bind(&other); assert(current()==&live);
    assert(g_universalViews.at(&other).analyzed);
    assert(g_universalViews.at(&other).pool==UINT_MAX);
    UniversalRepair(true); assert(current()==&other);
    UniversalRepair(false); assert(current()==&live);
    bind(nullptr); namedExterior=nullptr;
    g_universalViews.erase(&other);
    // First bind registers authored data; a completed analysis enables substitution.
    bind(&authored); assert(current()==&authored);
    g_universalViews.at(&authored).analyzed=true;
    bind(&authored); assert(current()==&live); assert(g_liveCubemapSlotCount==1);
    const auto entries=g_universalViews.size();
    const auto nonCubes=g_universalNonCubeViews.size();
    const int cachedReads=authored.privateReads;
    const auto beforeLookups=lookupCalls;
    bind(&authored); assert(authored.privateReads==cachedReads);
    assert(lookupCalls==beforeLookups+1);

    // A peer's temporary override must not discard the authored restore target.
    for(int iteration=0;iteration<100;++iteration) {
        bind(&glass); assert(current()==&glass);
        assert(g_universalSlots[8].Get()==&authored);
        assert(g_liveCubemapSlotCount==0);
        UniversalRepair(false); assert(current()==&glass);
        UniversalRepair(true); assert(current()==&glass);
        bind(&live); assert(current()==&live);
        assert(g_liveCubemapSlotCount==1);
        UniversalRepair(true); assert(current()==&authored);
        UniversalRepair(false); assert(current()==&live);
    }
    assert(glass.refs==glassRefs);
    assert(g_universalViews.size()==entries && g_universalNonCubeViews.size()==nonCubes);

    // Resource-only ownership also bypasses registration and balances temporary refs.
    bind(&glassViaResource); assert(current()==&glassViaResource);
    bind(&live);
    assert(glassViaResource.refs==resourceViewRefs && resourceOnly.refs==resourceRefs);
    assert(g_universalViews.count(&glassViaResource)==0);

    // A changed authored texture supersedes the saved mapping normally.
    bind(&other); g_universalViews.at(&other).analyzed=true; bind(&other);
    bind(&glass); bind(&live); UniversalRepair(true); assert(current()==&other);
    // Explicit null and ordinary non-cube bindings still clear the mapping.
    bind(nullptr); assert(!g_universalSlots[8]); assert(!current());
    bind(&glass); assert(!g_universalSlots[8]); bind(nullptr);
    bind(&authored); bind(&flat); assert(!g_universalSlots[8]);
    assert(current()==&flat);
    // A warmed ordinary texture needs no replacement lookup or COM classification.
    const auto flatLookups=lookupCalls;const auto flatReads=flat.privateReads;
    for(unsigned i=0;i<10000;++i)bind(&flat);
    assert(lookupCalls==flatLookups && flat.privateReads==flatReads && current()==&flat);
    assert(!g_universalSlots[8] && g_liveCubemapSlotCount==0);
    // Tagged non-cube peer views must also remain outside the negative cache.
    ID3D11ShaderResourceView peerFlat(&glassResource);
    peerFlat.desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    assert(TagOwned(&peerFlat)); bind(&peerFlat); bind(nullptr);
    assert(g_universalNonCubeViews.count(&peerFlat)==0 && peerFlat.refs==1);

    // MCM disable restoration and enable/rebind sequence.
    bind(&authored); bind(&glass); bind(&live);
    UniversalRepair(true); g_liveEnabled=false;
    assert(current()==&authored);
    bind(&glass); assert(current()==&glass); bind(&authored);
    g_liveEnabled=true; bind(&authored); assert(current()==&live);

    // Ready standalone pools and arrays have the same restoration behavior.
    ID3D11ShaderResourceView pooled(&liveResource),array(&authoredResource);
    UniversalPool pool; pool.ready=true; pool.standalone=true; pool.view=&pooled;
    g_universalPools.push_back(pool); g_universalViews.at(&authored).pool=0; RebuildMaterialViewLookup();
    bind(&authored); assert(current()==&pooled);
    bind(&glass); bind(&pooled); UniversalRepair(true); assert(current()==&authored);
    array.desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    bind(&array); auto& arrayPool=g_universalPools[g_universalViews.at(&array).pool];
    arrayPool.view=&pooled; arrayPool.ready=true; arrayPool.exteriorMatchState=0; RebuildMaterialViewLookup();
    bind(&array); assert(current()==&pooled);
    bind(&glass); bind(&pooled); UniversalRepair(true); assert(current()==&array);


    // Tag failure is reported once without disabling the working renderer.
    // Exterior cubes must not fall back to the unscaled shared live view when
    // pool capacity is exhausted, and remain authored until publication.
    auto& exterior=g_universalViews.at(&authored);
    exterior.exteriorBrightness=true; exterior.pool=UINT_MAX;
    bind(&authored); assert(current()==&authored);
    exterior.pool=0; g_universalPools[0].ready=false;
    bind(&authored); assert(current()==&authored);
    g_universalPools[0].ready=true;
    bind(&authored); assert(current()==&pooled);
    bind(&glass); assert(current()==&glass);
    bind(&pooled); UniversalRepair(true); assert(current()==&authored);

    // Preserve every native call and each valid slot without initializing
    // unused scratch storage. Exercise the maximum-sized mixed binding.
    std::array<ID3D11ShaderResourceView*,128> mixed{};
    mixed.fill(&flat);
    auto calls=nativeCalls;
    Hook_PSResources(&context,0,128,mixed.data());
    assert(nativeCalls==calls+1 && lastNativeArray==mixed.data());
    for(auto& v:context.slots)assert(v.Get()==&flat);
    mixed[5]=&authored;mixed[120]=&glass;
    calls=nativeCalls;Hook_PSResources(&context,0,128,mixed.data());
    assert(nativeCalls==calls+1 && lastNativeArray!=mixed.data());
    for(unsigned i=0;i<128;++i)assert(context.slots[i].Get()==(i==5?&pooled:(i==120?&glass:&flat)));
    assert(g_universalSlots[5].Get()==&authored && g_liveCubemapSlotCount==1);
    calls=nativeCalls;Hook_PSResources(&context,0,128,nullptr);
    assert(nativeCalls==calls+1 && lastNativeArray==nullptr && g_liveCubemapSlotCount==0);
    for(auto& v:context.slots)assert(!v);
    calls=nativeCalls;Hook_PSResources(&context,0,0,nullptr);
    assert(nativeCalls==calls+1 && lastNativeArray==nullptr);
    for(auto& v:g_universalSlots)v.Reset();

        // Fill beyond the unchanged 1024-view budget; measure one eviction and a subsequent hit.
    bind(nullptr); ResetNonCubeCache();
    g_cacheHits=g_cacheMisses=g_cacheInserts=g_cacheEvictions=0;
    std::vector<std::unique_ptr<ID3D11ShaderResourceView>> ordinaryViews;
    for(unsigned i=0;i<1025;++i) {
        auto view=std::make_unique<ID3D11ShaderResourceView>(&otherResource);
        view->desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        bind(view.get()); ordinaryViews.push_back(std::move(view));
    }
    assert(g_universalNonCubeViews.size()==1024 && g_cacheEvictions==1 && g_cacheInserts==1025 && g_cacheMisses==1025);
    assert(g_cacheClockMax==1024 && g_cacheClockSteps==1024);
    bind(ordinaryViews.back().get()); assert(g_cacheHits==1 && g_cacheInserts==1025);
    g_diagnosticsEnabled=false; bind(ordinaryViews.back().get()); assert(g_cacheHits==1); g_diagnosticsEnabled=true;
        // After the first full sweep, view 1 is the next victim. Touch it so clock skips it.
    bind(ordinaryViews[1].get());
    auto extra=std::make_unique<ID3D11ShaderResourceView>(&otherResource);
    extra->desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; bind(extra.get());
    assert(g_universalNonCubeViews.count(ordinaryViews[1].get())==1);
    assert(g_universalNonCubeViews.count(ordinaryViews[2].get())==0);
    assert(g_universalNonCubeViews.size()==1024 && g_cacheEvictions==2);
    bind(nullptr); ResetNonCubeCache();
    assert(g_nonCubeHand==0);
    for(auto* view:g_nonCubeClock) assert(!view);
    for(const auto& view:ordinaryViews) assert(view->refs==1);
    assert(extra->refs==1); ordinaryViews.clear();
    ID3D11Texture2D failed; failed.failTag=true;
    auto before=logs.size(); TagReflectionReplacement(&failed); TagReflectionReplacement(&failed);
    assert(logs.size()==before+1 && !IsPeerReplacement(&failed) && g_liveEnabled);
    assert(glass.refs==glassRefs);
    bind(nullptr); g_liveSRV.Reset(); g_universalViews.clear(); ResetNonCubeCache();
    g_universalPools.clear(); RebuildMaterialViewLookup(); assert(g_materialViewLookup.empty()); pool.view.Reset();
    for(auto& slot:g_universalSlots)slot.Reset();
    assert(authored.refs==1 && glass.refs==1 && glassViaResource.refs==1);
    std::cout << "PASS: ownership, reference balance, repair, toggle and pool binding sequences\n";
}
