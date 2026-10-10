#include <algorithm>
#include <cassert>
#include <cmath>
#include <climits>
#include <iostream>
#include <limits>
#include <vector>
using UINT=unsigned;
struct Probe { float r,g,b,saturation,consistency; bool detailed=false,coarse=false; };
struct Pool { bool standalone=false; };
std::vector<Pool> g_universalPools{{false}};
UINT g_tintProbePendingPool=0;
bool g_exteriorReferenceValid=true;
Probe g_exteriorReference{};
bool ExteriorDetailedMatches(const Probe& v,const Probe&) { return v.detailed; }
bool ExteriorSignatureMatches(const Probe& v,const Probe&) { return v.coarse; }
struct XMFLOAT4 { float x,y,z,w; XMFLOAT4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct Result { float intensity; XMFLOAT4 tint; bool keepOriginal; };
#include "production_exterior_balance.inc"
int main() {
    Probe exterior{0.072524f,0.072524f,0.072524f,0.2f,0.95f,true,true};
    // Both duplicate candidates retain energy attenuation without color styling.
    for(int i=0;i<2;++i) {
        auto r=evaluate(exterior,2);
        assert(r.intensity==1.0f);
        assert(r.tint.w==-1 && !r.keepOriginal);
    }
    auto ordinary=exterior; ordinary.detailed=false; ordinary.coarse=false;
    auto r=evaluate(ordinary,1);
    assert(r.tint.w==0.2f && std::abs(r.intensity-0.269302f)<1e-6f);
    ordinary.saturation=0.02f;
    r=evaluate(ordinary,1); assert(r.tint.w==0.02f);
    ordinary.saturation=0.9f; ordinary.consistency=0.1f;
    r=evaluate(ordinary,1); assert(r.keepOriginal && r.tint.w==-2 && r.intensity==1);
    ordinary.coarse=true; r=evaluate(ordinary,1); assert(r.keepOriginal);
    // Exterior energy does not depend on authored hue consistency.
    exterior.consistency=0.1f;
    r=evaluate(exterior,2); assert(r.tint.w==-1 && r.intensity==1.0f);
    exterior.r=exterior.g=exterior.b=0;
    r=evaluate(exterior,2); assert(r.intensity==1 && std::isfinite(r.intensity));
    exterior.r=std::numeric_limits<float>::quiet_NaN();
    r=evaluate(exterior,2); assert(r.intensity==1);
    assert(std::abs(UniversalAuthoredIntensity(0.004806f,true,true)-0.069325f)<0.00001f); assert(UniversalAuthoredIntensity(0.0001f,true,true)==0.02f); assert(UniversalAuthoredIntensity(4.0f,true,true)==1.0f); std::cout<<"PASS: duplicate exterior color/energy separation, ordinary materials, invalid luminance\n";
}
