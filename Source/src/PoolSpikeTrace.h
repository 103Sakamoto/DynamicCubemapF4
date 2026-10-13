// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>

namespace PoolTrace {
inline constexpr unsigned timers=12, dispatchTimer=6;
inline constexpr std::array<const char*,timers> names{
    "Pool control before", "Pool state save", "Other pools before",
    "Focus selection", "Focus constants", "Focus bindings", "Focus dispatch",
    "Focus unbind", "Focus commit", "Other pools after", "Pool state restore", "Pool control after"};
enum class Step : unsigned { Selection, Constants, Bindings, Dispatch, Unbind, Commit, Count };
inline constexpr std::array<const char*,6> stepNames{
    "Select/cache", "Constants", "Bindings", "Dispatch submit", "Unbind", "Cache commit"};
struct Metric { bool valid=false;double gpu=0,cpu=0; };
struct Row {
    unsigned pools=0,slot=0,cubes=0,updated=0,reused=0,dirty=0,eligible=0,standalone=0;
    std::uintptr_t source=0,destination=0;
    unsigned sourceMips=0,firstMip=0,firstFace=0,sourceFaces=0,format=0;
    std::array<double,6> cpu{};
};
struct Sample {
    std::uint64_t tag=0,atMs=0,submittedQpc=0;
    bool metadata=false,poolReady=false,mainReady=false,reprojection=false,overlay=false,previews=false;
    unsigned totalPools=0,updated=0,reused=0;
    Row focus{};
    std::array<Metric,timers> passes{};
    bool counterValid=false;
    std::uint64_t invocations=0;
    double queryAgeMs=0,outerGpu=0,outerCpu=0,otherGpuMax=0,controlGpu=0;
    std::string otherStage;
    std::uint64_t ExpectedInvocations() const {return std::uint64_t(344)*6*64*focus.updated;}
    double DetailGpu() const {
        double value=0;for(unsigned i=1;i<timers-1;++i)if(passes[i].valid)value+=passes[i].gpu;
        return value;
    }
    double Score() const {
        double value=std::max(DetailGpu(),mainReady?std::max(outerGpu,outerCpu):0.0);
        for(const auto& p:passes)if(p.valid)value=std::max(value,p.cpu);
        return value;
    }
};
struct Pending {
    std::array<Sample,64> entries{};
    std::uint64_t overwritten=0,unmatched=0;
    Sample& Begin(std::uint64_t tag) {
        auto& entry=entries[tag%entries.size()];
        if(entry.metadata && !entry.poolReady)++overwritten;
        entry={};entry.tag=tag;entry.metadata=true;return entry;
    }
    Sample* Find(std::uint64_t tag) {
        auto& entry=entries[tag%entries.size()];
        if(!tag || !entry.metadata || entry.tag!=tag){++unmatched;return nullptr;}
        return &entry;
    }
};
template<class Clock> struct Recorder {
    Row* row=nullptr;std::int64_t last=0;Step current=Step::Selection;
    void Begin(Row* value){row=value;if(row)last=Clock::Now();current=Step::Selection;}
    void Mark(Step next) {
        if(!row)return;const auto now=Clock::Now();
        row->cpu[unsigned(current)]+=Clock::Milliseconds(now-last);last=now;current=next;
    }
    void Finish(){if(row){Mark(current);row=nullptr;}}
};
inline void Detail(std::ostringstream& out,const Sample& s) {
    out<<"submitted_update="<<s.tag<<" at_ms="<<s.atMs<<" reprojection="<<s.reprojection
       <<" overlay="<<s.overlay<<" previews="<<s.previews<<" totalPools="<<s.totalPools
       <<" updated="<<s.updated<<" reused="<<s.reused<<" matched_main="<<s.mainReady
       <<" detailed_GPU="<<s.DetailGpu()<<" query_result_age_ms="<<s.queryAgeMs<<" submitted_qpc="<<s.submittedQpc;
    if(s.mainReady)out<<" outer_GPU="<<s.outerGpu<<" outer_CPU="<<s.outerCpu
       <<" unscoped_GPU="<<std::max(0.0,s.outerGpu-s.DetailGpu())
       <<" main_control_GPU="<<s.controlGpu<<" other_max_GPU="<<s.otherGpuMax
       <<" other_stage=\""<<s.otherStage<<"\"";
    out<<" dispatch_elapsed_GPU="<<(s.passes[dispatchTimer].valid?s.passes[dispatchTimer].gpu:0)
       <<" dispatch_submitted="<<s.passes[dispatchTimer].valid<<" counter_valid="<<s.counterValid
       <<" CSInvocations="<<s.invocations<<" expectedInvocations="<<s.ExpectedInvocations()
       <<" counter_mismatch="<<(s.counterValid && s.invocations!=s.ExpectedInvocations());
    for(unsigned i=0;i<timers;++i)if(s.passes[i].valid)
        out<<" ["<<names[i]<<" GPU="<<s.passes[i].gpu<<" CPU="<<s.passes[i].cpu<<" ms]";
    const auto& p=s.focus;
    out<<" {focus slot="<<p.slot<<" visited="<<p.pools<<" cubes="<<p.cubes
       <<" updated="<<p.updated<<" reused="<<p.reused<<" dirty="<<p.dirty
       <<" eligible="<<p.eligible<<" standalone="<<p.standalone<<" source="<<p.source
       <<" destination="<<p.destination<<" sourceMips="<<p.sourceMips
       <<" firstMip="<<p.firstMip<<" firstFace="<<p.firstFace<<" sourceFaces="<<p.sourceFaces<<" format="<<p.format;
    for(unsigned j=0;j<stepNames.size();++j)out<<" "<<stepNames[j]<<"_CPU="<<p.cpu[j];out<<"}";
}
struct Summary {
    struct Stats {
        std::uint64_t n=0,spikes=0;double sum=0,maximum=0;
        void Add(double ms){++n;sum+=ms;maximum=std::max(maximum,ms);spikes+=ms>=0.2;}
    };
    std::array<Stats,timers> gpu{},cpu{};
    Stats detailed,outer;
    std::array<unsigned,128> focusCoverage{};
    std::uint64_t counterSamples=0,counterMismatches=0,missingFocus=0;
    Sample worst{},slow{};bool slowPending=false;
    std::uint64_t nextReport=0,lastSlowReport=0;
    void Consider(const Sample& s) {
        if(!s.poolReady)return;
        if(!worst.tag || s.Score()>=worst.Score())worst=s;
        if(s.Score()>=0.2 && (!slowPending || s.Score()>=slow.Score())){slow=s;slowPending=true;}
        else if(slowPending && s.tag==slow.tag)slow=s;
    }
    void ObservePool(const Sample& s) {
        detailed.Add(s.DetailGpu());if(s.mainReady)outer.Add(s.outerGpu);
        if(s.focus.pools && s.focus.slot<focusCoverage.size())++focusCoverage[s.focus.slot];else ++missingFocus;
        if(s.counterValid){++counterSamples;counterMismatches+=s.invocations!=s.ExpectedInvocations();}
        for(unsigned i=0;i<timers;++i)if(s.passes[i].valid){gpu[i].Add(s.passes[i].gpu);cpu[i].Add(s.passes[i].cpu);}
        Consider(s);
    }
    void ObserveMain(const Sample& s){if(s.poolReady){outer.Add(s.outerGpu);Consider(s);}}
    std::string TakeSlow(std::uint64_t now) {
        if(!slowPending || now-lastSlowReport<1000)return {};
        std::ostringstream out;out<<"DISPATCH TRACE SPIKE: ";Detail(out,slow);
        lastSlowReport=now;slowPending=false;return out.str();
    }
    std::string Report(std::uint64_t now,const Pending& pending) {
        if(now<nextReport)return {};nextReport=now+30000;if(!detailed.n)return {};
        std::ostringstream out;out<<"DISPATCH TRACE SUMMARY: completed="<<detailed.n
           <<" matched_main="<<outer.n<<" metadataOverwritten="<<pending.overwritten
           <<" unmatchedTags="<<pending.unmatched<<" counterSamples="<<counterSamples
           <<" counterMismatches="<<counterMismatches<<" missingFocus="<<missingFocus<<" threshold_ms=0.2";
        for(unsigned i=0;i<timers;++i)if(gpu[i].n)out<<" ["<<names[i]<<" n="<<gpu[i].n
           <<" GPU_mean/max="<<gpu[i].sum/gpu[i].n<<"/"<<gpu[i].maximum
           <<" CPU_mean/max="<<cpu[i].sum/cpu[i].n<<"/"<<cpu[i].maximum
           <<" gpuSpikes="<<gpu[i].spikes<<" cpuSpikes="<<cpu[i].spikes<<"]";
        out<<" coverage=";for(unsigned i=0;i<focusCoverage.size();++i)if(focusCoverage[i])out<<i<<":"<<focusCoverage[i]<<",";
        out<<"\nDISPATCH TRACE WORST: ";Detail(out,worst);
        gpu={};cpu={};detailed={};outer={};worst={};focusCoverage={};counterSamples=counterMismatches=missingFocus=0;
        return out.str();
    }
};
}

