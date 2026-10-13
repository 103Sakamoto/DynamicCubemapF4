#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <string>
#include "PoolSpikeTrace.h" // POOLTRACE
namespace DCFDiag {
void Disable();
void ClearSkyPreview();
bool Prepare(ID3D11Device*,ID3D11DeviceContext*,IDXGISwapChain*);
void BeginUpdate();
void BeginPass(const char*);
void EndPass();
void EndUpdate();
std::string TakeSpikeReport();
std::string TakeTimingReport();
// POOLTRACE BEGIN
void BeginPoolReplication(bool reprojection,uint64_t atMs,unsigned pools);
void EndPoolStateSave();
void BeginPool(unsigned slot,unsigned cubes,bool standalone,bool dirty,
    ID3D11ShaderResourceView* source,ID3D11Texture2D* destination);
void PoolSelection(bool eligible,unsigned updated);
void PoolStep(PoolTrace::Step step);
void EndPool();
void BeginPoolStateRestore();
void EndPoolReplication();
std::string TakePoolReport();
// POOLTRACE END
void SetPreviews(ID3D11ShaderResourceView* capture,ID3D11ShaderResourceView* sky,
 ID3D11ShaderResourceView* fallback,ID3D11ShaderResourceView* inferred,
 ID3D11ShaderResourceView* output, unsigned mip);
void SetBindingStats(double meanMs, double maxMs, uint64_t samples, uint64_t calls);
}
