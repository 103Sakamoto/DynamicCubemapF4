// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

// CPU-only detail for the existing Tint analysis pass. No GPU queries or waits.
namespace TintTrace {
enum class Stage : unsigned {
    Recovery, Resources, Shader, OutputBuffer, OutputUAV, ReadbackBuffer, Query,
    Tracking, SourceSync, Completion, EpochCheck, Map, ReferenceResult,
    MaterialMatch, MaterialStyle, CacheCommit, Unmap, PendingCleanup,
    Maintenance, Candidates, Scan, SourceView, ArrayStamp, SaveState, Dispatch,
    CopyAndQuery, RestoreState, Queued, ReportFlush, Count
};
inline constexpr std::array<const char*,static_cast<unsigned>(Stage::Count)> names{
    "Fallback recovery", "Probe resource check", "Probe shader creation",
    "Probe output buffer", "Probe output UAV", "Probe readback buffer", "Probe query creation",
    "Write tracking", "Reference sync", "Completion poll", "Epoch validation", "Readback Map",
    "Reference result", "Material matching", "Material classification/style", "Cache commit",
    "Readback Unmap", "Pending cleanup", "Reference maintenance", "Candidate construction", "Candidate scan",
    "Source SRV creation", "Array tracking/stamp", "Save CS state", "Probe dispatch",
    "Readback copy/query", "Restore CS state", "Queue bookkeeping", "Tint report flush"
};
enum class Kind { None, Exterior, Recovery, Direct, Array };
inline const char* KindName(Kind kind) {
    switch(kind) {
    case Kind::Exterior:return "exterior"; case Kind::Recovery:return "recovery";
    case Kind::Direct:return "direct"; case Kind::Array:return "array";
    default:return "none";
    }
}
struct Sample {
    std::uint64_t call=0, reset=0, atMs=0;
    double totalMs=0, logMs=0;
    std::array<double,names.size()> ms{};
    std::array<bool,names.size()> visited{};
    Stage ended=Stage::Recovery;
    Kind pending=Kind::None, queued=Kind::None;
    unsigned pendingPool=UINT32_MAX, queuedPool=UINT32_MAX;
    unsigned pendingCubes=0, processedCubes=0, queuedCubes=0, candidatesScanned=0;
    unsigned views=0, pools=0, logWrites=0, logMessages=0;
    bool queryPolled=false, mapped=false, stale=false, invalidated=false;
    std::int64_t queryHr=0;
};

// Clock is the existing PerfClock/PerfMilliseconds adapter in Main.cpp.
template<class Clock> class Recorder {
    bool enabled;
    std::int64_t begin=0, last=0;
    Stage current=Stage::Recovery;
public:
    Sample sample{};
    explicit Recorder(bool on):enabled(on) {
        if(enabled) { begin=last=Clock::Now(); sample.visited[0]=true; }
    }
    void Step(Stage next) {
        if(!enabled)return;
        auto now=Clock::Now(); sample.ms[static_cast<unsigned>(current)]+=Clock::Milliseconds(now-last);
        last=now; current=next; sample.visited[static_cast<unsigned>(next)]=true;
    }
    void Finish() {
        if(!enabled)return;
        auto now=Clock::Now(); sample.ms[static_cast<unsigned>(current)]+=Clock::Milliseconds(now-last);
        sample.totalMs=Clock::Milliseconds(now-begin); sample.ended=current; enabled=false;
    }
    class LogTimer {
        Recorder* recorder;
        std::int64_t start=0;
    public:
        explicit LogTimer(Recorder* value):recorder(value) { if(recorder)start=Clock::Now(); }
        ~LogTimer() { if(recorder) { recorder->sample.logMs+=Clock::Milliseconds(Clock::Now()-start); ++recorder->sample.logWrites; } }
    };
};

inline void Detail(std::ostringstream& out,const Sample& s) {
    out<<"call="<<s.call<<" reset="<<s.reset<<" at_ms="<<s.atMs<<" CPU_total="<<s.totalMs
       <<" ended=\""<<names[static_cast<unsigned>(s.ended)]<<"\""
       <<" pending="<<KindName(s.pending)<<"/"<<s.pendingPool<<"/"<<s.pendingCubes
       <<" queued="<<KindName(s.queued)<<"/"<<s.queuedPool<<"/"<<s.queuedCubes
       <<" processedCubes="<<s.processedCubes<<" scanned="<<s.candidatesScanned
       <<" views/pools="<<s.views<<"/"<<s.pools
       <<" queryPolled="<<s.queryPolled<<" queryHr="<<s.queryHr
       <<" mapped="<<s.mapped<<" stale="<<s.stale<<" invalidated="<<s.invalidated
       <<" logger_ms="<<s.logMs<<" logWrites="<<s.logWrites<<" logMessages="<<s.logMessages<<" logger_is_subset=1";
    for(unsigned i=0;i<names.size();++i)if(s.visited[i])out<<" ["<<names[i]<<" CPU="<<s.ms[i]<<" ms]";
}

// Retain every call in the summary, including calls suppressed by the 1 s log limit.
struct Summary {
    struct Stats { std::uint64_t n=0; double sum=0, maximum=0; void Add(double value) { ++n;sum+=value;maximum=std::max(maximum,value); } };
    Stats total, logging;
    std::array<Stats,names.size()> stages{};
    Sample worst{}, slow{};
    std::uint64_t sequence=0, generation=0, nextReport=0, lastSlowReport=0;
    bool slowPending=false;
    void Observe(Sample sample) {
        sample.call=++sequence;
        if(!total.n || sample.totalMs>worst.totalMs)worst=sample;
        total.Add(sample.totalMs);logging.Add(sample.logMs);
        for(unsigned i=0;i<names.size();++i)if(sample.visited[i])stages[i].Add(sample.ms[i]);
        if(sample.totalMs>=1.0 && (!slowPending || sample.totalMs>slow.totalMs)) { slow=sample;slowPending=true; }
        if(!nextReport)nextReport=sample.atMs+30000;
    }
    std::string TakeSlow(std::uint64_t now) {
        if(!slowPending || (lastSlowReport && now-lastSlowReport<1000))return {};
        std::ostringstream out;out<<std::fixed<<std::setprecision(4)<<"TINT TRACE SLOW: ";Detail(out,slow);
        slowPending=false;lastSlowReport=now;return out.str();
    }
    std::string Report(std::uint64_t now,bool force=false) {
        if(!total.n || (!force && now<nextReport))return {};
        std::ostringstream out;out<<std::fixed<<std::setprecision(4)
            <<"TINT TRACE SUMMARY: calls="<<total.n<<" CPU_mean/max="<<total.sum/total.n<<"/"<<total.maximum
            <<" logger_mean/max="<<logging.sum/logging.n<<"/"<<logging.maximum<<" logger_is_subset=1";
        for(unsigned i=0;i<names.size();++i)if(stages[i].n)out<<" ["<<names[i]<<" n="<<stages[i].n
            <<" CPU_mean/max="<<stages[i].sum/stages[i].n<<"/"<<stages[i].maximum<<"]";
        out<<" WORST: ";Detail(out,worst);
        total={};logging={};stages={};worst={};nextReport=now+30000;
        if(force)slowPending=false;
        return out.str();
    }
};
}
