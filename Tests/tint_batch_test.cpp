// SPDX-License-Identifier: GPL-3.0-only
#include "TintReports.h"
#include "TintAnalysisTrace.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using ULONGLONG=std::uint64_t;
ULONGLONG wallTime=100;
ULONGLONG GetTickCount64() { return wallTime; }
const ULONGLONG g_logStart=0;
std::mutex g_logMutex;
std::atomic_bool g_diagnosticsEnabled{false};
const char* kLogPath="Tests/bin/tint-batch-logger-test.log";
struct Clock {
    static inline std::int64_t now=0;
    static std::int64_t Now() { return ++now; }
    static double Milliseconds(std::int64_t ticks) { return ticks/1000.0; }
};
using TintTraceRecorder=TintTrace::Recorder<Clock>;
thread_local TintTraceRecorder* g_tintTraceCurrent=nullptr;
void TintTraceStep(TintTrace::Stage stage) { if(g_tintTraceCurrent)g_tintTraceCurrent->Step(stage); }
#include "production_tint_logger.inc"

std::string ReadLog() { std::ifstream f(kLogPath);return {std::istreambuf_iterator<char>(f),{}}; }
void RemoveLog() { std::filesystem::remove(kLogPath); }
void ReportAndReturn() { TintReportBatch batch(true);LogDiagnostic([] {return "early return";});return; }

struct ThreadWriter {
    static inline std::mutex mutex;
    static inline std::vector<std::string> writes;
    static inline std::atomic_uint timestamps=0;
    static std::uint64_t Timestamp() { ++timestamps;return 200; }
    static void Write(std::string_view bytes,unsigned messages) {
        assert(messages==2);
        std::lock_guard lock(mutex);writes.emplace_back(bytes);
    }
};
using ThreadBatch=TintReports::Batch<ThreadWriter>;
int main() {
    RemoveLog();
    int constructed=0;
    auto builder=[&]() -> std::string { ++constructed;throw std::runtime_error("must not format"); };
    {
        TintReportBatch batch(false);
        LogDiagnostic(builder);
        LogRecoveryDiagnostic("waiting for stream",builder);
    }
    assert(constructed==0 && !std::filesystem::exists(kLogPath));
    { TintReportBatch empty(true); }
    assert(!std::filesystem::exists(kLogPath));

    // Exercise the actual production logger and trace around 17 reload reports.
    g_diagnosticsEnabled=true;
    TintTraceRecorder recorder(true);g_tintTraceCurrent=&recorder;
    std::string expected;
    {
        TintReportBatch batch(true);
        for(int i=0;i<17;++i) {
            LogDiagnostic([i] {return "TINT REFRESH: cube="+std::to_string(i);});
            expected+="[+100ms] TINT REFRESH: cube="+std::to_string(i)+'\n';
        }
        assert(!std::filesystem::exists(kLogPath));
    }
    recorder.Finish();g_tintTraceCurrent=nullptr;
    assert(ReadLog()==expected);
    assert(recorder.sample.logWrites==1 && recorder.sample.logMessages==17);
    assert(recorder.sample.visited[static_cast<unsigned>(TintTrace::Stage::ReportFlush)]);
    assert(recorder.sample.totalMs>=recorder.sample.logMs);

    // Timestamps belong to each report, rather than the later flush time.
    RemoveLog();
    {
        TintReportBatch batch(true);wallTime=101;Log("earlier");
        wallTime=102;Log("later");wallTime=200;
    }
    assert(ReadLog()=="[+101ms] earlier\n[+102ms] later\n");wallTime=100;

    // Empty calls do not create a write even with tracing active.
    RemoveLog();TintTraceRecorder emptyRecorder(true);g_tintTraceCurrent=&emptyRecorder;
    { TintReportBatch batch(true); }
    emptyRecorder.Finish();g_tintTraceCurrent=nullptr;
    assert(emptyRecorder.sample.logWrites==0 && !std::filesystem::exists(kLogPath));

    ReportAndReturn();assert(ReadLog()=="[+100ms] early return\n");RemoveLog();
    try {
        TintReportBatch batch(true);
        LogDiagnostic([] {return "before unwind";});
        throw std::runtime_error("test unwind");
    } catch(const std::runtime_error&) {}
    assert(ReadLog()=="[+100ms] before unwind\n");RemoveLog();
    {
        TintReportBatch outer(true);Log("outer first");
        { TintReportBatch inner(true);Log("inner second"); }
        assert(!std::filesystem::exists(kLogPath));
        Log("outer third");
    }
    assert(ReadLog()=="[+100ms] outer first\n[+100ms] inner second\n[+100ms] outer third\n");RemoveLog();

    // Diagnostics toggled off during a call preserves already-admitted reports.
    {
        TintReportBatch batch(true);Log("already admitted");g_diagnosticsEnabled=false;
        LogDiagnostic(builder);Log("hidden ordinary report");Log("TINT FAIL: query");
    }
    assert(constructed==0);
    assert(ReadLog()=="[+100ms] already admitted\n[+100ms] TINT FAIL: query\n");RemoveLog();
    // Keep exactly the existing failure exception policy when diagnostics are off.
    for(const auto* failure:{"FAIL", "failed", "aborted", "unsupported"}) {
        assert(TintReports::RetainedFailure(failure));
        LogRecoveryDiagnostic(failure,[failure] {return std::string("recovery: ")+failure;});
    }
    assert(ReadLog()=="[+100ms] recovery: FAIL\n[+100ms] recovery: failed\n[+100ms] recovery: aborted\n[+100ms] recovery: unsupported\n");
    assert(!TintReports::RetainedFailure("ordinary"));RemoveLog();

    // A disabled batch does not take timestamps or emit buffered writes.
    { ThreadBatch batch(false);assert(!ThreadBatch::Append("hidden")); }
    assert(ThreadWriter::timestamps==0 && ThreadWriter::writes.empty());
    std::atomic_uint entered=0;
    auto worker=[&](const char* label) {
        ThreadBatch batch(true);assert(ThreadBatch::Append(label));++entered;
        while(entered.load()!=2)std::this_thread::yield();
        assert(ThreadBatch::Append(label));
    };
    std::thread first(worker,"first"),second(worker,"second");first.join();second.join();
    assert(ThreadWriter::writes.size()==2 && ThreadWriter::timestamps==4);
    for(const auto& write:ThreadWriter::writes)
        assert(write=="[+200ms] first\n[+200ms] first\n" || write=="[+200ms] second\n[+200ms] second\n");
    assert(!ThreadBatch::Append("no active scope"));
    std::cout<<"PASS: actual logger batches 17 ordered reports into one write; off/empty lazy paths, failure retention, early return/unwind, nested/thread isolation and trace accounting\n";
}
