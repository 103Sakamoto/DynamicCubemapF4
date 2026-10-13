// SPDX-License-Identifier: GPL-3.0-only
#include "TintAnalysisTrace.h"
#include <cassert>
#include <cmath>
#include <iostream>

struct Clock {
    static inline std::int64_t ticks=0;
    static inline unsigned reads=0;
    static std::int64_t Now() { ++reads;return ticks; }
    static double Milliseconds(std::int64_t value) { return value/1000.0; }
};
using Recorder=TintTrace::Recorder<Clock>;
int main() {
    // Disabled gameplay reads no clock and allocates no trace statistics.
    { Recorder off(false);off.Step(TintTrace::Stage::Map);off.Finish();assert(off.sample.totalMs==0); }
    assert(Clock::reads==0);
    Recorder trace(true);
    Clock::ticks+=100;trace.Step(TintTrace::Stage::Map);
    Clock::ticks+=5390;trace.Step(TintTrace::Stage::MaterialStyle);
    Clock::ticks+=10;
    { Recorder::LogTimer logging(&trace);Clock::ticks+=500; }
    Clock::ticks+=20;trace.Step(TintTrace::Stage::Scan);
    Clock::ticks+=40;trace.Step(TintTrace::Stage::SourceView);
    Clock::ticks+=50;trace.Step(TintTrace::Stage::Scan);
    Clock::ticks+=60;trace.Finish();trace.Finish();
    assert(std::abs(trace.sample.totalMs-6.17)<1e-9);
    assert(trace.sample.ms[static_cast<unsigned>(TintTrace::Stage::Map)]==5.39);
    assert(std::abs(trace.sample.ms[static_cast<unsigned>(TintTrace::Stage::Scan)]-.10)<1e-9);
    assert(trace.sample.logMs==.5 && trace.sample.logWrites==1);
    double sum=0;for(auto ms:trace.sample.ms)sum+=ms;
    // Logger time is included in the enclosing stage, never added to total twice.
    assert(std::abs(sum-trace.sample.totalMs)<1e-9);
    auto reads=Clock::reads;{ Recorder::LogTimer outside(nullptr); }
    assert(Clock::reads==reads);
    TintTrace::Summary summary;
    trace.sample.atMs=100;trace.sample.pending=TintTrace::Kind::Array;
    trace.sample.pendingPool=0;trace.sample.pendingCubes=42;trace.sample.reset=1;
    summary.Observe(trace.sample);
    auto slow=summary.TakeSlow(100);
    assert(slow.find("TINT TRACE SLOW:")!=std::string::npos);
    assert(slow.find("Readback Map CPU=5.3900")!=std::string::npos);
    assert(slow.find("logger_ms=0.5000")!=std::string::npos);
    assert(slow.find("pending=array/0/42")!=std::string::npos);
    auto worse=trace.sample;worse.totalMs=9;worse.atMs=200;worse.reset=2;
    summary.Observe(worse);assert(summary.TakeSlow(200).empty());
    // A cheaper subsequent call cannot replace the suppressed peak.
    auto small=worse;small.totalMs=2;small.atMs=300;
    summary.Observe(small);assert(summary.TakeSlow(300).empty());
    assert(summary.Report(30099).empty());
    auto report=summary.Report(30100);
    assert(report.find("calls=3")!=std::string::npos);
    assert(report.find("WORST: call=2 reset=2 at_ms=200 CPU_total=9.0000")!=std::string::npos);
    assert(summary.TakeSlow(30100).find("CPU_total=9.0000")!=std::string::npos);
    assert(summary.Report(60100).empty());
    // Summaries cross source/reset transitions without losing the slow sample.
    ++summary.generation;small.reset=summary.generation;small.atMs=60101;summary.Observe(small);
    assert(!summary.Report(60101,true).empty());assert(!summary.slowPending);
    summary={};assert(summary.sequence==0 && summary.generation==0);
    Recorder waiting(true);Clock::ticks+=20;waiting.Step(TintTrace::Stage::Completion);
    waiting.sample.queryPolled=true;waiting.sample.queryHr=1;Clock::ticks+=30;waiting.Finish();
    assert(waiting.sample.ended==TintTrace::Stage::Completion);
    assert(waiting.sample.totalMs==.05);
    std::cout<<"PASS: exclusive substage accounting, logging subset, disabled clock gating, early completion, peak retention/rate limit and reset/disable reporting\n";
}
