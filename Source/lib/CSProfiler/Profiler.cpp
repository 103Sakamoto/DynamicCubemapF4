#include "Profiler.h"

#include <algorithm>
#include <unordered_map>

float Profiler::RollingHistory::GetAverage() const
{
	if (count == 0)
		return lastMs;
	float sum = 0.0f;
	for (uint32_t i = 0; i < count; i++)
		sum += history[i];
	return sum / static_cast<float>(count);
}

float Profiler::RollingHistory::GetPercentile(float p) const
{
	if (count == 0)
		return lastMs;

	thread_local std::vector<float> sorted;
	sorted.resize(count);
	for (uint32_t i = 0; i < count; i++)
		sorted[i] = history[i];
	std::sort(sorted.begin(), sorted.end());

	float idx = (p / 100.0f) * static_cast<float>(count - 1);
	uint32_t lo = static_cast<uint32_t>(idx);
	uint32_t hi = std::min(lo + 1, count - 1);
	float frac = idx - static_cast<float>(lo);
	return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

bool Profiler::Initialize(ID3D11Device* device, ID3D11DeviceContext* a_context)
{
	Release();

	context = a_context;

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	cpuTicksToMs = 1000.0 / static_cast<double>(freq.QuadPart);

	for (auto& frame : frames) {
		D3D11_QUERY_DESC disjointDesc{};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(device->CreateQuery(&disjointDesc, frame.disjoint.ReleaseAndGetAddressOf()))) { Release(); return false; }

		frame.timers.resize(kMaxTimers);
		for (auto& timer : frame.timers) {
			D3D11_QUERY_DESC tsDesc{};
			tsDesc.Query = D3D11_QUERY_TIMESTAMP;
			if (FAILED(device->CreateQuery(&tsDesc, timer.begin.ReleaseAndGetAddressOf()))) { Release(); return false; }
			if (FAILED(device->CreateQuery(&tsDesc, timer.end.ReleaseAndGetAddressOf()))) { Release(); return false; }
		}
		frame.activeCount = 0;
		frame.inFlight = false;
	}

	writeFrame = 0;
	readFrame = 0;
	framesSinceInit = 0;
	initialized = true;
	return true;
}

// DISPATCHTRACE BEGIN
bool Profiler::InitializePipelineStatistics()
{
    if (!initialized || !context || frameActive) return false;
    for (const auto& frame : frames) if (frame.inFlight) return false;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    Microsoft::WRL::ComPtr<ID3D11Query> queries[kFrameLatency];
    D3D11_QUERY_DESC desc{D3D11_QUERY_PIPELINE_STATISTICS, 0};
    for (auto& query : queries) if (FAILED(device->CreateQuery(&desc, &query))) return false;
    for (unsigned i=0; i<kFrameLatency; ++i) frames[i].pipeline=std::move(queries[i]);
    pipelineEnabled=true;
    return true;
}

void Profiler::BeginStatisticsPass(const std::string& name)
{
    if (!frameActive || passActive) return;
    BeginPass(name);
    auto& frame=frames[writeFrame];
    if (passActive && pipelineEnabled && frame.pipeline && frame.pipelineTimer<0) {
        frame.pipelineTimer=static_cast<int>(frame.activeCount);
        context->Begin(frame.pipeline.Get());
    }
}
// DISPATCHTRACE END

void Profiler::Release()
{
	EndFrame();
	frameActive = passActive = false;
	writeFrame = readFrame = framesSinceInit = 0;
	for (auto& frame : frames) {
		frame.disjoint = nullptr;
        // DISPATCHTRACE BEGIN
        frame.pipeline.Reset();frame.pipelineTimer=-1;
        // DISPATCHTRACE END
		frame.timers.clear();
		frame.activeCount = 0;
		frame.inFlight = false;
	}
	results.clear();
	knownTimers.clear();
	knownTimerIndex.clear();
	collectedFrames = 0;
	resultTag = 0; // POOLTRACE
	totalTimeMs = 0.0f;
	cpuTotalTimeMs = 0.0f;
	initialized = false;
    // DISPATCHTRACE BEGIN
    pipelineEnabled=false;resultAgeMs=0;
    // DISPATCHTRACE END
	context = nullptr;
}

void Profiler::BeginFrame()
{
	if (!initialized || !context || frameActive)
		return;

	CollectResults();

	auto& frame = frames[writeFrame];
	if (frame.inFlight) return; // Never overwrite an unresolved GPU query.
	frame.activeCount = 0;
	frame.tag = 0; // POOLTRACE
	frame.inFlight = true;
	frameActive = true;
	context->Begin(frame.disjoint.Get());
    // DISPATCHTRACE BEGIN
    frame.pipelineTimer=-1;QueryPerformanceCounter(&frame.submitted);
    // DISPATCHTRACE END
}

void Profiler::BeginPass(const std::string& name)
{
	if (!initialized || !context)
		return;

	if (!frameActive || passActive) return;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];
	passActive = true;
	timer.name = name;
	context->End(timer.begin.Get());
	QueryPerformanceCounter(&timer.cpuBegin);

	if (beginPerfEvent)
		beginPerfEvent(name);
}

void Profiler::EndPass()
{
	if (!initialized || !context || !frameActive || !passActive)
		return;

	auto& frame = frames[writeFrame];
	if (frame.activeCount >= kMaxTimers)
		return;

	auto& timer = frame.timers[frame.activeCount];

	LARGE_INTEGER cpuEnd;
	QueryPerformanceCounter(&cpuEnd);
	timer.cpuMs = static_cast<float>(static_cast<double>(cpuEnd.QuadPart - timer.cpuBegin.QuadPart) * cpuTicksToMs);

    // DISPATCHTRACE BEGIN
    if (frame.pipelineTimer==static_cast<int>(frame.activeCount)) context->End(frame.pipeline.Get());
    // DISPATCHTRACE END
	context->End(timer.end.Get());
	frame.activeCount++;
	passActive = false;

	if (endPerfEvent)
		endPerfEvent({});
}

void Profiler::EndFrame()
{
	if (!initialized || !context || !frameActive)
		return;

	EndPass();
	frameActive = false;
	context->End(frames[writeFrame].disjoint.Get());
	writeFrame = (writeFrame + 1) % kFrameLatency;
	framesSinceInit++;
}

void Profiler::CollectResults()
{
	if (framesSinceInit < kFrameLatency)
		return;

	readFrame = writeFrame;
	auto& frame = frames[readFrame];
	if (!frame.inFlight)
		return;

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
	HRESULT hr = context->GetData(frame.disjoint.Get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	if (hr == S_FALSE) return;
	if (FAILED(hr)) { frame.inFlight = false; return; }
	// Check every timestamp before committing the batch.
	if (!disjointData.Disjoint && disjointData.Frequency) {
		for (uint32_t i=0; i<frame.activeCount; ++i) {
			UINT64 v;
			for (auto* q : {frame.timers[i].begin.Get(), frame.timers[i].end.Get()}) {
				auto ready=context->GetData(q,&v,sizeof(v),D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (ready==S_FALSE) return;
				if (FAILED(ready)) { frame.inFlight=false; return; }
			}
		}
	}

    // DISPATCHTRACE BEGIN
    D3D11_QUERY_DATA_PIPELINE_STATISTICS pipelineData{};
    bool pipelineValid=false;
    if (!disjointData.Disjoint && disjointData.Frequency && frame.pipelineTimer>=0) {
        const auto ready=context->GetData(frame.pipeline.Get(),&pipelineData,sizeof(pipelineData),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (ready==S_FALSE) return;
        pipelineValid=ready==S_OK; // A failed optional counter never discards valid timestamps.
    }
    LARGE_INTEGER collected;QueryPerformanceCounter(&collected);
    resultAgeMs=static_cast<double>(collected.QuadPart-frame.submitted.QuadPart)*cpuTicksToMs;
    // DISPATCHTRACE END
	frame.inFlight = false;
	collectedFrames++;
	resultTag = frame.tag; // POOLTRACE

	struct ActiveTimerData
	{
		float gpuMs = 0.0f;
		float cpuMs = 0.0f;
	};
	std::unordered_map<std::string, ActiveTimerData> activeTimers;
	float activeTotalMs = 0.0f;
	float activeCpuTotalMs = 0.0f;

	if (!disjointData.Disjoint && disjointData.Frequency) {
		double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);

		for (uint32_t i = 0; i < frame.activeCount; i++) {
			auto& timer = frame.timers[i];
			UINT64 tsBegin = 0, tsEnd = 0;

			if (context->GetData(timer.begin.Get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			if (context->GetData(timer.end.Get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;

			if (tsEnd < tsBegin) continue;
			float ms = static_cast<float>(static_cast<double>(tsEnd - tsBegin) * ticksToMs);
			auto& entry = activeTimers[timer.name];
			entry.gpuMs += ms;
			entry.cpuMs += timer.cpuMs;
			activeTotalMs += ms;
			activeCpuTotalMs += timer.cpuMs;

			auto [it, inserted] = knownTimerIndex.try_emplace(timer.name, knownTimers.size());
			if (inserted) {
				KnownTimer kt;
				kt.name = timer.name;
				knownTimers.push_back(std::move(kt));
			}
			auto& known = knownTimers[it->second];
			known.gpu.PushSample(ms);
			known.cpu.PushSample(timer.cpuMs);
			known.lastSampleFrame = collectedFrames;
		}
	}

	RetireStaleTimers();

	totalTimeMs = activeTotalMs;
	cpuTotalTimeMs = activeCpuTotalMs;

	results.clear();
	results.reserve(knownTimers.size());
	for (const auto& known : knownTimers) {
		TimerResult result;
		result.name = known.name;
		auto it = activeTimers.find(known.name);
		if (it != activeTimers.end()) {
			result.gpuTimeMs = it->second.gpuMs;
			result.cpuTimeMs = it->second.cpuMs;
		} else {
			result.gpuTimeMs = known.gpu.lastMs;
			result.cpuTimeMs = known.cpu.lastMs;
		}
		result.avgMs = known.gpu.GetAverage();
		result.p95Ms = known.gpu.GetPercentile(95.0f);
		result.p99Ms = known.gpu.GetPercentile(99.0f);
		result.cpuAvgMs = known.cpu.GetAverage();
		result.cpuP95Ms = known.cpu.GetPercentile(95.0f);
		result.cpuP99Ms = known.cpu.GetPercentile(99.0f);
		result.valid = it != activeTimers.end();
        // DISPATCHTRACE BEGIN
        if (result.valid && pipelineValid && frame.pipelineTimer>=0 &&
            frame.timers[frame.pipelineTimer].name==known.name) {
            result.pipelineStatisticsValid=true;result.csInvocations=pipelineData.CSInvocations;
        }
        // DISPATCHTRACE END
		result.historyBuffer = known.gpu.history;
		result.historyHead = known.gpu.head;
		result.historyCount = known.gpu.count;
		results.push_back(std::move(result));
	}
}
void Profiler::RetireStaleTimers()
{
	const size_t before = knownTimers.size();
	std::erase_if(knownTimers, [this](const KnownTimer& known) {
		return collectedFrames - known.lastSampleFrame >= kTimerRetireFrames;
	});
	if (knownTimers.size() != before)
		RebuildTimerIndex();
}

void Profiler::RebuildTimerIndex()
{
	knownTimerIndex.clear();
	for (size_t i = 0; i < knownTimers.size(); i++)
		knownTimerIndex[knownTimers[i].name] = i;
}
