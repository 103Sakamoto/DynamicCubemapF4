#include <cassert>
#include <mutex>
#include <vector>
#include <map>
#include <string>
struct View{};
struct Ptr {View* p{};View* Get()const{return p;}explicit operator bool()const{return p!=nullptr;}void Reset(){p=nullptr;}};
struct TintProbeResult{bool valid;};
bool ExteriorDetailedMatches(const TintProbeResult&a,const TintProbeResult&b){return a.valid&&b.valid;}
unsigned long long GetTickCount64(){return 20000;}
void Log(const char*){}
std::mutex g_cellFallbackMutex;
Ptr g_referenceCandidate,g_tintProbePendingView,g_candidateOriginal,g_namedCellFallback[1],exteriorView,g_exteriorReferenceView;
bool g_tintProbePendingCandidate=true,g_exteriorReferenceValid=false,g_namedReferenceReady=false;
TintProbeResult g_exteriorReference{};
unsigned long long g_exteriorReferenceNext=0;
std::vector<int> g_tintProbeDone,g_tintProbeNext;
struct Entry{bool analyzed=true;}; std::map<int,Entry> g_universalViews;
struct Pool{int exteriorMatchState=1;bool ready=true;};std::vector<Pool> g_universalPools;
struct State{int n=5;};State g_invalidReference;
void Complete(const TintProbeResult* values){
            if (g_tintProbePendingCandidate) {
                if (g_referenceCandidate && g_tintProbePendingView.Get()==g_referenceCandidate.Get()) {
                    const bool valid=ExteriorDetailedMatches(values[0],values[0]);
                    bool committed=false;
                    if (valid && !g_exteriorReferenceValid) {
                        std::lock_guard lock(g_cellFallbackMutex);
                        if (g_namedCellFallback[0].Get()==g_candidateOriginal.Get()) {
                            g_namedCellFallback[0]=g_referenceCandidate;
                            committed=true;
                        }
                    }
                    if (committed) {
                        exteriorView=g_referenceCandidate; g_exteriorReferenceView=exteriorView;
                        g_exteriorReference=values[0]; g_exteriorReferenceValid=true; g_namedReferenceReady=true;
                        g_exteriorReferenceNext=GetTickCount64()+1000;
                        g_tintProbeDone.clear(); g_tintProbeNext.clear();
                        for (auto& [view,entry]:g_universalViews) entry.analyzed=false;
                        for (auto& pool:g_universalPools) { pool.exteriorMatchState=-3; pool.ready=false; }
                        g_invalidReference={};
                        Log("FALLBACK: validated recovery reference installed; rematching cubemaps.");
                    } else {
                        Log(valid ? "FALLBACK: recovery candidate discarded; native reference changed or recovered." :
                            "FALLBACK: recovery candidate invalid; existing reference retained.");
                    }
                    g_referenceCandidate.Reset(); g_candidateOriginal.Reset();
                }
            }
}
int main(){
 View native,candidate,newNative;
 auto setup=[&](){g_referenceCandidate={&candidate};g_tintProbePendingView={&candidate};g_candidateOriginal={&native};g_namedCellFallback[0]={&native};g_exteriorReferenceValid=false;g_namedReferenceReady=false;g_universalViews={{1,{true}}};g_universalPools={{1,true}};g_tintProbeDone={1};};
 TintProbeResult valid{true},invalid{false};
 setup();Complete(&invalid);assert(g_namedCellFallback[0].Get()==&native);assert(!g_referenceCandidate);
 setup();g_namedCellFallback[0]={&newNative};Complete(&valid);assert(g_namedCellFallback[0].Get()==&newNative);
 setup();g_exteriorReferenceValid=true;Complete(&valid);assert(g_namedCellFallback[0].Get()==&native);
 setup();g_referenceCandidate.Reset();Complete(&valid);assert(g_namedCellFallback[0].Get()==&native);
 setup();Complete(&valid);assert(g_namedCellFallback[0].Get()==&candidate);assert(g_namedReferenceReady&&g_exteriorReferenceValid);assert(g_tintProbeDone.empty());assert(!g_universalViews[1].analyzed);assert(!g_universalPools[0].ready&&g_universalPools[0].exteriorMatchState==-3);assert(!g_referenceCandidate);
}
