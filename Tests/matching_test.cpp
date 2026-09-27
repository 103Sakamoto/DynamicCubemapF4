#include <array>
#include <cmath>
#include <algorithm>
#include <climits>
#include <cassert>
#include <limits>
using UINT=unsigned;
struct XMFLOAT4 {float x,y,z,w;};
struct TintProbeResult {std::array<XMFLOAT4,96> signature{};std::array<XMFLOAT4,384> detail{};};
bool ExteriorSignatureMatches(const TintProbeResult& a,const TintProbeResult& b)
{
    // Reject missing/non-finite data. Tight per-channel tolerance permits only
    // numerical conversion noise; there is no color/saturation heuristic here.
    bool nonzero=false;
    for (std::size_t i=0;i<a.signature.size();++i) {
        const float av[]={a.signature[i].x,a.signature[i].y,a.signature[i].z,a.signature[i].w};
        const float bv[]={b.signature[i].x,b.signature[i].y,b.signature[i].z,b.signature[i].w};
        for (int c=0;c<4;++c) {
            if (!std::isfinite(av[c]) || !std::isfinite(bv[c]) ||
                std::abs(av[c]-bv[c])>0.00001f+0.00001f*std::max(std::abs(av[c]),std::abs(bv[c]))) return false;
            if (c<3 && bv[c]>0.00001f) nonzero=true;
        }
    }
    return nonzero;
}

bool ExteriorDetailedMatches(const TintProbeResult& a,const TintProbeResult& b)
{
    // Reject missing/non-finite data. Tight per-channel tolerance permits only
    // numerical conversion noise; there is no color/saturation heuristic here.
    if (!ExteriorSignatureMatches(a,b)) return false;
    bool nonzero=false;
    for (std::size_t i=0;i<a.detail.size();++i) {
        const float av[]={a.detail[i].x,a.detail[i].y,a.detail[i].z,a.detail[i].w};
        const float bv[]={b.detail[i].x,b.detail[i].y,b.detail[i].z,b.detail[i].w};
        for (int c=0;c<4;++c) {
            if (!std::isfinite(av[c]) || !std::isfinite(bv[c]) ||
                std::abs(av[c]-bv[c])>0.00001f+0.00001f*std::max(std::abs(av[c]),std::abs(bv[c]))) return false;
            if (c<3 && bv[c]>0.00001f) nonzero=true;
        }
    }
    return nonzero;
}

UINT FindExteriorMatch(const TintProbeResult* values,UINT cubes,const TintProbeResult& reference,UINT& count)
{
    count=0; UINT selected=UINT_MAX;
    for (UINT cube=0;cube<cubes;++cube)
        if (ExteriorDetailedMatches(values[cube],reference)) { selected=cube; ++count; }
    return count==1 ? selected : UINT_MAX;
}


int main(){
TintProbeResult r{};for(auto& v:r.signature)v={.2f,.3f,.4f,1};for(auto& v:r.detail)v={.1f,.4f,.7f,1};
TintProbeResult values[4]{r,r,r,r};UINT n=0;
assert(FindExteriorMatch(values,1,r,n)==0&&n==1);
assert(FindExteriorMatch(values,3,r,n)==UINT_MAX&&n==3);
for(unsigned i=0;i<3;++i)assert(ExteriorDetailedMatches(values[i],r));
values[1].detail[37].x=.9f;
assert(ExteriorSignatureMatches(values[1],r));assert(!ExteriorDetailedMatches(values[1],r));
values[2].signature[0].w=0;
assert(FindExteriorMatch(values,3,r,n)==0&&n==1);
values[0].detail[0].x=std::numeric_limits<float>::quiet_NaN();assert(!ExteriorDetailedMatches(values[0],r));
TintProbeResult black{};assert(!ExteriorDetailedMatches(black,black));
}
