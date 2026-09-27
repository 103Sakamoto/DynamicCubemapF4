#include <vector>
#include <cassert>
#include <climits>
struct Pool { bool standalone; };
bool preserve(unsigned g_tintProbePendingPool, bool standalone,bool usable,bool exteriorAtlasCube,bool colorful=false,bool consistent=true,bool unresolvedNeutral=false,bool uncertainExterior=false){
std::vector<Pool> g_universalPools{{standalone}};
                const bool unresolvedArrayData=g_tintProbePendingPool<g_universalPools.size() &&
                    !g_universalPools[g_tintProbePendingPool].standalone && !usable && !exteriorAtlasCube;
                const bool keepOriginal=(colorful && !consistent) || unresolvedNeutral || unresolvedArrayData || uncertainExterior;
return keepOriginal;}
int main(){
assert(preserve(0,false,false,false));
assert(!preserve(0,false,true,false));
assert(!preserve(0,false,false,true));
assert(!preserve(0,true,false,false));
assert(!preserve(UINT_MAX,false,false,false));
assert(preserve(0,false,true,false,true,false));
assert(preserve(0,false,false,false,false,true,true));
}
