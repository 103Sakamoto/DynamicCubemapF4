bool g_diagnosticsEnabled=true;
// CPU mocks only: verifies dispatch scope and draw forwarding, not GPU output.
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using UINT=unsigned;
struct Object { int refs=1; void Release() { assert(refs>1); --refs; } };
using ID3D11ComputeShader=Object;
using ID3D11ClassInstance=Object;
using ID3D11ShaderResourceView=Object;
using ID3D11UnorderedAccessView=Object;
using ID3D11Buffer=Object;
struct Handle { Object* p=nullptr; Object* Get() const { return p; } explicit operator bool() const { return p; } };
struct Context {
    Object shader, instance, srv[2], uav, cb, pixelCB;
    Object* currentShader=&shader;
    Object* currentSRVs[2]{&srv[0],&srv[1]};
    Object* currentUAV=&uav;
    Object* currentCB=&cb;
    Object* currentPixelCB=&pixelCB;
    Object* dispatchedSource=nullptr;
    Object* dispatchedTarget=nullptr;
    UINT dispatches=0,x=0,y=0,z=0,uploads=0,queries=0,sets=0;
    void CSGetShader(Object** s,Object** i,UINT* n) { *s=currentShader; ++(*s)->refs; i[0]=&instance; ++instance.refs; *n=1; }
    void CSGetShaderResources(UINT,UINT n,Object** s) { for(UINT i=0;i<n;++i) { s[i]=currentSRVs[i]; ++s[i]->refs; } }
    void CSGetUnorderedAccessViews(UINT,UINT,Object** u) { *u=currentUAV; ++(*u)->refs; }
    void CSGetConstantBuffers(UINT,UINT,Object** b) { *b=currentCB; ++(*b)->refs; }
    void CSSetShader(Object* s,Object* const*,UINT) { currentShader=s; }
    void CSSetShaderResources(UINT,UINT n,Object* const* s) { for(UINT i=0;i<n;++i) currentSRVs[i]=s[i]; }
    void CSSetUnorderedAccessViews(UINT,UINT,Object* const* u,const UINT*) { currentUAV=*u; }
    void CSSetConstantBuffers(UINT,UINT,Object* const* b) { currentCB=*b; }
    void UpdateSubresource(Object*,UINT,const void*,const void*,UINT,UINT) { ++uploads; }
    void Dispatch(UINT a,UINT b,UINT c) { ++dispatches; x=a;y=b;z=c; dispatchedSource=currentSRVs[0]; dispatchedTarget=currentUAV; }
    void PSGetConstantBuffers(UINT,UINT,Object** b) { ++queries; *b=currentPixelCB; if(*b) ++(*b)->refs; }
    void PSSetConstantBuffers(UINT,UINT,Object* const* b) { ++sets; currentPixelCB=*b; }
};
using ID3D11DeviceContext=Context;
Context context;
Context* g_liveContext=&context;
Object compute,constants,neutral;
Handle g_universalReplicateCS{&compute},g_rainNeutralFrameBuffer{&neutral};
Handle g_universalLiveMipSRVs[8];
struct UniversalPool {
    bool tintDirty=true;
    Handle tintConstants{&constants};
    int style=0;
    Handle originalMipSRVs[8],mipUAVs[8];
    UINT cubes=3;
};
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
int main() {
    Object sources[8],destinations[8];
    UniversalPool pool;
    for(UINT i=0;i<8;++i) { g_universalLiveMipSRVs[i].p=&sources[i]; pool.mipUAVs[i].p=&destinations[i]; }
    for(UINT mip : {0u,1u,2u,3u,4u,2u}) {
        const auto before=context.dispatches;
        UniversalReplicatePool(pool,mip);
        assert(context.dispatches==before+1);
        assert(context.x==((128u>>mip)+7)/8 && context.y==context.x && context.z==18);
        assert(context.dispatchedSource==&sources[mip] && context.dispatchedTarget==&destinations[mip]);
        assert(context.currentShader==&context.shader && context.currentUAV==&context.uav && context.currentCB==&context.cb);
        assert(context.currentSRVs[0]==&context.srv[0] && context.currentSRVs[1]==&context.srv[1]);
        assert(context.shader.refs==1 && context.instance.refs==1 && context.cb.refs==1 && context.uav.refs==1);
    }
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
    std::cout << "Selected-mip dispatch/state and rain fast-path tests passed\n";
}
