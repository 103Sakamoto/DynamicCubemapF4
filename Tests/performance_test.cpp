bool g_diagnosticsEnabled=true;
// CPU mocks only: verifies dispatch scope and draw forwarding, not GPU output.
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using UINT=unsigned;
constexpr UINT kCubeMipCount=8;
struct Object { int refs=1; void Release() { assert(refs>1); --refs; } };
using ID3D11ComputeShader=Object;
using ID3D11ClassInstance=Object;
using ID3D11ShaderResourceView=Object;
using ID3D11UnorderedAccessView=Object;
using ID3D11Buffer=Object;
struct Handle { Object* p=nullptr; Object* Get() const { return p; } explicit operator bool() const { return p; } };
struct Context {
    Object shader, instance, srv[2], uav[8], cb, pixelCB;
    Object* currentShader=&shader;
    Object* currentSRVs[2]{&srv[0],&srv[1]};
    Object* currentUAVs[8]{&uav[0],&uav[1],&uav[2],&uav[3],&uav[4],&uav[5],&uav[6],&uav[7]};
    Object* currentCB=&cb;
    Object* currentPixelCB=&pixelCB;
    Object* dispatchedSource=nullptr;
    Object* dispatchedTargets[8]{};
    UINT stateSaves=0;
    UINT dispatches=0,x=0,y=0,z=0,uploads=0,queries=0,sets=0;
    void CSGetShader(Object** s,Object** i,UINT* n) { ++stateSaves; *s=currentShader; ++(*s)->refs; i[0]=&instance; ++instance.refs; *n=1; }
    void CSGetShaderResources(UINT,UINT n,Object** s) { for(UINT i=0;i<n;++i) { s[i]=currentSRVs[i]; ++s[i]->refs; } }
    void CSGetUnorderedAccessViews(UINT,UINT n,Object** u) { for(UINT i=0;i<n;++i) {u[i]=currentUAVs[i];if(u[i])++u[i]->refs;} }
    void CSGetConstantBuffers(UINT,UINT,Object** b) { *b=currentCB; ++(*b)->refs; }
    void CSSetShader(Object* s,Object* const*,UINT) { currentShader=s; }
    void CSSetShaderResources(UINT,UINT n,Object* const* s) { for(UINT i=0;i<n;++i) currentSRVs[i]=s[i]; }
    void CSSetUnorderedAccessViews(UINT,UINT n,Object* const* u,const UINT*) { for(UINT i=0;i<n;++i)currentUAVs[i]=u[i]; }
    void CSSetConstantBuffers(UINT,UINT,Object* const* b) { currentCB=*b; }
    void UpdateSubresource(Object*,UINT,const void*,const void*,UINT,UINT) { ++uploads; }
    void Dispatch(UINT a,UINT b,UINT c) { ++dispatches;x=a;y=b;z=c;dispatchedSource=currentSRVs[0];for(UINT i=0;i<8;++i)dispatchedTargets[i]=currentUAVs[i]; }
    void PSGetConstantBuffers(UINT,UINT,Object** b) { ++queries; *b=currentPixelCB; if(*b) ++(*b)->refs; }
    void PSSetConstantBuffers(UINT,UINT,Object* const* b) { ++sets; currentPixelCB=*b; }
};
using ID3D11DeviceContext=Context;
Context context;
Context* g_liveContext=&context;
Object compute,constants,neutral;
Handle g_universalReplicateCS{&compute},g_rainNeutralFrameBuffer{&neutral};
Handle g_universalLiveMips;
struct UniversalPool {
    bool tintDirty=true;
    bool standalone=true;
    Handle originalView;
    UINT lastUpdatedCubes=0;
    Handle tintConstants{&constants};
    struct { struct Tint { float w=-1; }; std::array<Tint,128> tint{}; std::array<UINT,128> updateCubes{}; } style;
    Handle originalMips,mipUAVs[8];
    UINT cubes=3;
};
bool CanReuseRetainedPoolSource(ID3D11ShaderResourceView*) { return false; }
void CommitRetainedPoolSource(ID3D11ShaderResourceView*) {}
bool g_liveEnabled=true,g_latestMappedValid=true;
std::atomic<bool> g_gameplayReady{true};
UINT g_liveCubemapSlotCount=1;
struct { struct { float x=0,y=0; } iblDesaturation; } g_latestMappedFrame;
std::atomic<Object*> g_enginePerFrameIdentity{&context.pixelCB};
struct { UINT draws=0,noLive=0,neutral=0,noBuffer=0,wrongBuffer=0,unavailable=0,applied=0; } g_rainStats;
UINT audits=0,prepares=0;
void AuditRainBindings(Context*) { ++audits; }
bool PrepareRainNeutralFrameBuffer(Context*) {
    ++prepares;
    const auto& d=g_latestMappedFrame.iblDesaturation;
    return g_latestMappedValid && std::isfinite(d.x) && std::isfinite(d.y) && (d.x>0.00001f || d.y>0.00001f);
}
#include "production_performance.inc"
void ReplicateForTest(UniversalPool& pool) { UniversalReplicationScope state; UniversalDispatchPool(pool); }
int main() {
    Object source,destinations[8];
    UniversalPool pool;
    g_universalLiveMips.p=&source;
    for(UINT i=0;i<8;++i) pool.mipUAVs[i].p=&destinations[i];
    for(UINT run=0;run<6;++run) {
        const auto before=context.dispatches;
        ReplicateForTest(pool);
        assert(context.dispatches==before+1);
        assert(context.x==344 && context.y==1 && context.z==18);
        assert(context.dispatchedSource==&source);
        for(UINT i=0;i<8;++i) {
            assert(context.dispatchedTargets[i]==&destinations[i]);
            assert(context.currentUAVs[i]==&context.uav[i] && context.uav[i].refs==1);
        }
        assert(context.currentShader==&context.shader && context.currentCB==&context.cb);
        assert(context.currentSRVs[0]==&context.srv[0] && context.currentSRVs[1]==&context.srv[1]);
        assert(context.shader.refs==1 && context.instance.refs==1 && context.cb.refs==1);
    }
    assert(context.uploads==1);
    // Nine pools: preserve dispatch count/output selection, save caller state only once.
    auto saves=context.stateSaves, before=context.dispatches;
    { UniversalReplicationScope state;
      for(unsigned i=0;i<9;++i) UniversalDispatchPool(pool);
    }
    assert(context.stateSaves==saves+1 && context.dispatches==before+9);
    assert(context.dispatchedSource==&source);
    for(UINT i=0;i<8;++i)assert(context.dispatchedTargets[i]==&destinations[i] && context.currentUAVs[i]==&context.uav[i] && context.uav[i].refs==1);
    assert(context.currentShader==&context.shader && context.currentCB==&context.cb);
    assert(context.currentSRVs[0]==&context.srv[0] && context.currentSRVs[1]==&context.srv[1]);
    assert(context.shader.refs==1 && context.instance.refs==1 && context.cb.refs==1);
    assert(context.uploads==1);
    UINT draws=0;
    auto draw=[&] { ++draws; };
    DrawWithRainNeutralB12(&context,draw);
    assert(draws==1 && audits==1 && context.queries==0 && prepares==0);
    g_latestMappedFrame.iblDesaturation.x=0.5f;
    DrawWithRainNeutralB12(&context,[&] { ++draws; assert(context.currentPixelCB==&neutral); });
    assert(draws==2 && context.queries==1 && context.sets==2 && context.currentPixelCB==&context.pixelCB && context.pixelCB.refs==1);
    g_latestMappedValid=false;
    DrawWithRainNeutralB12(&context,draw);
    assert(draws==3 && context.queries==2 && context.sets==2);
    g_latestMappedValid=true;
    g_latestMappedFrame.iblDesaturation.x=std::numeric_limits<float>::quiet_NaN();
    DrawWithRainNeutralB12(&context,draw);
    assert(draws==4 && context.queries==3 && context.sets==2);
    g_latestMappedFrame.iblDesaturation.x=0;
    g_latestMappedFrame.iblDesaturation.y=0.5f;
    DrawWithRainNeutralB12(&context,draw);
    assert(draws==5 && context.queries==4 && context.sets==4);
    g_liveCubemapSlotCount=0;
    DrawWithRainNeutralB12(&context,draw);
    assert(draws==6 && context.queries==4);
    g_liveEnabled=false;
    DrawWithRainNeutralB12(&context,draw);
    assert(draws==7 && context.queries==4 && context.pixelCB.refs==1);
    // Null caller UAV slots must also restore correctly.
    context.currentUAVs[3]=nullptr;
    ReplicateForTest(pool);
    assert(!context.currentUAVs[3]);
    context.currentUAVs[3]=&context.uav[3];
    std::cout << "Full-chain single-dispatch/state restoration and unchanged rain fast-path tests passed\n";
}
