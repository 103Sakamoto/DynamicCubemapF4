// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Consumes completed results from the existing CS profiler. No queries or waits.
struct SpikeSummary {
    struct Stage {
        std::string name;
        uint64_t samples=0,gpuSpikes=0,cpuSpikes=0;
        double gpuSum=0,cpuSum=0,gpuMax=0,cpuMax=0;
    };
    uint64_t sequence=0, updates=0, controlSpikes=0, gpuSpikeUpdates=0, gpuMultiStage=0;
    std::vector<Stage> stages;
    template<class Results> void Observe(uint64_t completed,const Results& results) {
        if(completed==sequence || !completed) return;
        sequence=completed; ++updates;
        unsigned gpuWorkSpikes=0;
        for(const auto& r:results) {
            if(!r.valid) continue;
            auto it=std::find_if(stages.begin(),stages.end(),[&](const Stage& s){return s.name==r.name;});
            if(it==stages.end()) { stages.push_back({r.name}); it=stages.end()-1; }
            ++it->samples; it->gpuSum+=r.gpuTimeMs; it->cpuSum+=r.cpuTimeMs;
            it->gpuMax=std::max(it->gpuMax,double(r.gpuTimeMs)); it->cpuMax=std::max(it->cpuMax,double(r.cpuTimeMs));
            it->gpuSpikes+=r.gpuTimeMs>=0.2f; it->cpuSpikes+=r.cpuTimeMs>=0.2f;
            if(r.name=="Timing control") controlSpikes+=r.gpuTimeMs>=0.2f;
            else gpuWorkSpikes+=r.gpuTimeMs>=0.2f;
        }
        gpuSpikeUpdates+=gpuWorkSpikes>0; gpuMultiStage+=gpuWorkSpikes>1;
    }
    std::string Report() {
        if(!updates) return {};
        std::ostringstream out;
        out<<"SPIKE SUMMARY: completedUpdates="<<updates<<" gpuSpikeUpdates="<<gpuSpikeUpdates
           <<" multiStageGpuSpikes="<<gpuMultiStage<<" emptyControlGpuSpikes="<<controlSpikes<<" threshold_ms=0.2";
        for(const auto& s:stages) {
            out<<" ["<<s.name<<" n="<<s.samples<<" GPU_mean/max="<<s.gpuSum/s.samples<<"/"<<s.gpuMax
               <<" CPU_mean/max="<<s.cpuSum/s.samples<<"/"<<s.cpuMax<<" gpuSpikes="<<s.gpuSpikes<<" cpuSpikes="<<s.cpuSpikes<<"]";
        }
        const auto completed=sequence; *this={}; sequence=completed;
        return out.str();
    }
};
