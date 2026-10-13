// SPDX-License-Identifier: GPL-3.0-only
#include "../Source/src/PoolSpikeTrace.h"
#include <cassert>
#include <iostream>
struct Clock {
    static inline unsigned reads=0;
    static int64_t Now(){return ++reads;}
    static double Milliseconds(int64_t ticks){return ticks*0.01;}
};
int main() {
    PoolTrace::Recorder<Clock> cpu;
    cpu.Begin(nullptr);cpu.Mark(PoolTrace::Step::Dispatch);cpu.Finish();assert(Clock::reads==0);
    PoolTrace::Row row;cpu.Begin(&row);cpu.Mark(PoolTrace::Step::Constants);cpu.Finish();
    assert(row.cpu[0]==0.01 && row.cpu[1]==0.01);
    PoolTrace::Pending pending;
    auto& one=pending.Begin(1);one.atMs=100;one.reprojection=true;one.focus.updated=25;
    auto& two=pending.Begin(2);two.atMs=200;two.reprojection=false;two.focus.updated=8;
    assert(pending.Find(1)->reprojection && pending.Find(1)->focus.updated==25);
    assert(!pending.Find(2)->reprojection && pending.Find(2)->focus.updated==8);
    assert(!pending.Find(65)); // Completion is never paired with a different slot occupant.
    pending.Begin(65);assert(!pending.Find(1) && pending.overwritten==1);
    PoolTrace::Summary summary;
    auto& sample=*pending.Find(2);sample.poolReady=true;sample.passes[2]={true,0.3,0.005};
    summary.ObservePool(sample);assert(summary.detailed.n==1 && summary.slowPending);
    assert(summary.TakeSlow(500).empty());
    sample.mainReady=true;sample.outerGpu=0.8;sample.otherGpuMax=0.4;sample.otherStage="Capture mips";
    summary.ObserveMain(sample);
    auto report=summary.TakeSlow(1000);
    assert(report.find("submitted_update=2")!=std::string::npos && report.find("matched_main=1")!=std::string::npos);
    assert(report.find("other_stage=\"Capture mips\"")!=std::string::npos && report.find("unscoped_GPU=0.5")!=std::string::npos);
    sample.tag=3;sample.outerGpu=1.2;summary.Consider(sample);
    sample.tag=4;sample.outerGpu=0.5;summary.Consider(sample);
    assert(summary.slow.tag==3); // Keep the peak even when subsequent samples arrive inside the log limit.
    auto text=summary.Report(30000,pending);
    assert(text.find("completed=1")!=std::string::npos && text.find("outer_GPU=1.2")!=std::string::npos);
    assert(summary.detailed.n==0 && summary.TakeSlow(2000).find("submitted_update=3")!=std::string::npos);
    PoolTrace::Summary late;sample.mainReady=true;sample.outerGpu=1;late.ObservePool(sample);
    assert(late.outer.n==1); // The outer and detailed profilers can complete in either order.
    PoolTrace::Summary cpuOnly;
    sample={};sample.tag=5;sample.poolReady=sample.mainReady=true;sample.outerCpu=.33;
    sample.focus.pools=1;sample.focus.slot=127;sample.focus.updated=2;
    sample.counterValid=true;sample.invocations=sample.ExpectedInvocations();
    assert(sample.ExpectedInvocations()==264192);
    cpuOnly.ObservePool(sample);assert(cpuOnly.slowPending && cpuOnly.focusCoverage[127]==1);
    assert(cpuOnly.counterSamples==1 && !cpuOnly.counterMismatches);
    ++sample.invocations;cpuOnly.ObservePool(sample);assert(cpuOnly.counterMismatches==1);
    assert(cpuOnly.TakeSlow(1000).find("previews=0")!=std::string::npos);
    sample={};sample.tag=6;sample.poolReady=true;cpuOnly.ObservePool(sample);
    assert(cpuOnly.missingFocus==1 && cpuOnly.Report(30000,pending).find("counterMismatches=1")!=std::string::npos);
    assert(cpuOnly.counterSamples==0 && cpuOnly.focusCoverage[127]==0);
    pending={};summary={};assert(!pending.Find(2) && !summary.slowPending);
    std::cout<<"PASS: no disabled CPU clocks; exact tags, wrap/drop, either completion order, CPU-only peak retention, counter mismatches, focus coverage, rate limiting and reset\n";
}
