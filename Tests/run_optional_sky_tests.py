from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
toggle=block(new,'if (g_reprojectEnabled!=g_requestedReprojection)')
cell_change=block(new,'if (UpdateCellFallbackKind())')
fixture=r'''
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cassert>
#include <string>
#include <iostream>
#include <atomic>
using Microsoft::WRL::ComPtr;
constexpr UINT kCubeSize=128;
bool g_reprojectEnabled=false,g_requestedReprojection=false,g_reprojectFailed=false,g_haveHistoryOrigin=true;
int g_activeCellKind=0,g_requestedCellKind=0;
std::atomic_uint g_accumulatedFrames{123};
bool UpdateCellFallbackKind(){
 if(!g_requestedCellKind || g_activeCellKind==g_requestedCellKind)return false;
 g_activeCellKind=g_requestedCellKind;return true;
}
ComPtr<ID3D11Texture2D> g_skyHistoryTexture;
ComPtr<ID3D11UnorderedAccessView> g_skyHistoryUAV;
ComPtr<ID3D11ShaderResourceView> g_skyHistorySRV;
namespace DCFDiag {
 ComPtr<ID3D11ShaderResourceView> preview;
 void ClearSkyPreview(){preview.Reset();}
}
void Log(const std::string&){}
struct MockDevice {
 ID3D11Device* real;int fail=-1,calls=0;
 HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* d,const D3D11_SUBRESOURCE_DATA* p,ID3D11Texture2D** o){
  if(calls++==fail)return E_FAIL;return real->CreateTexture2D(d,p,o);
 }
 HRESULT CreateUnorderedAccessView(ID3D11Resource* r,const D3D11_UNORDERED_ACCESS_VIEW_DESC* d,ID3D11UnorderedAccessView** o){
  if(calls++==fail)return E_FAIL;return real->CreateUnorderedAccessView(r,d,o);
 }
 HRESULT CreateShaderResourceView(ID3D11Resource* r,const D3D11_SHADER_RESOURCE_VIEW_DESC* d,ID3D11ShaderResourceView** o){
  if(calls++==fail)return E_FAIL;return real->CreateShaderResourceView(r,d,o);
 }
};
'''
fixture+=block(new,'bool UseSeparateSkyHistory()')+'\n'
fixture+=block(new,'void ReleaseSkyHistory()')+'\n'
fixture+=block(new,'bool EnsureSkyHistoryResources(').replace('ID3D11Device* device','MockDevice* device')+'\n'
fixture+='void Toggle(){'+toggle+'}\n'
fixture+='void CellChange(){'+cell_change+'}\n'
fixture+=r'''
int main(){
 ComPtr<ID3D11Device> real;ComPtr<ID3D11DeviceContext> context;
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&real,nullptr,&context)));
 MockDevice d{real.Get()};
 for(int kind:{0,1,2})for(bool reprojection:{false,true}){
  if(kind==1 && reprojection)continue;
  g_activeCellKind=kind;g_reprojectEnabled=reprojection;
  assert(!UseSeparateSkyHistory());
  assert(EnsureSkyHistoryResources(&d) && d.calls==0 && !g_skyHistoryTexture);
 }
 g_activeCellKind=1;g_reprojectEnabled=false;
 for(int failure=0;failure<3;++failure){
  g_activeCellKind=1;
  g_requestedReprojection=true;g_reprojectFailed=true;g_haveHistoryOrigin=true;Toggle();
  assert(g_reprojectEnabled && !g_reprojectFailed && !g_haveHistoryOrigin);
  d.calls=0;d.fail=failure;
  assert(!EnsureSkyHistoryResources(&d) && d.calls==failure+1);
  assert(!g_skyHistoryTexture && !g_skyHistoryUAV && !g_skyHistorySRV);
  d.calls=0;d.fail=-1;g_haveHistoryOrigin=true;
  assert(EnsureSkyHistoryResources(&d) && d.calls==3 && !g_haveHistoryOrigin);
  assert(g_skyHistoryTexture && g_skyHistoryUAV && g_skyHistorySRV);
  D3D11_TEXTURE2D_DESC desc{};g_skyHistoryTexture->GetDesc(&desc);
  assert(desc.Width==128 && desc.Height==128 && desc.ArraySize==6 && desc.MipLevels==1 && desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT);
  auto* texture=g_skyHistoryTexture.Get();g_haveHistoryOrigin=true;
  assert(EnsureSkyHistoryResources(&d) && d.calls==3 && texture==g_skyHistoryTexture.Get() && g_haveHistoryOrigin);
  DCFDiag::preview=g_skyHistorySRV;
  g_requestedReprojection=false;g_reprojectFailed=true;Toggle();
  assert(!g_reprojectEnabled && !g_reprojectFailed && !g_haveHistoryOrigin);
  assert(!g_skyHistoryTexture && !g_skyHistoryUAV && !g_skyHistorySRV && !DCFDiag::preview);
  assert(EnsureSkyHistoryResources(&d) && d.calls==3 && !g_skyHistoryTexture);
 }
 // Reprojection must remain ON while the actual cell transition removes sky.
 g_requestedReprojection=true;Toggle();d.calls=0;d.fail=-1;
 assert(EnsureSkyHistoryResources(&d) && d.calls==3);
 DCFDiag::preview=g_skyHistorySRV;g_haveHistoryOrigin=true;
 g_requestedCellKind=2;CellChange();
 assert(g_reprojectEnabled && !UseSeparateSkyHistory());
 assert(!g_haveHistoryOrigin && g_accumulatedFrames==0);
 assert(!g_skyHistoryTexture && !g_skyHistoryUAV && !g_skyHistorySRV && !DCFDiag::preview);
 assert(EnsureSkyHistoryResources(&d) && d.calls==3);
 g_haveHistoryOrigin=true;g_accumulatedFrames=12;g_requestedCellKind=1;CellChange();
 assert(g_reprojectEnabled && UseSeparateSkyHistory() && !g_haveHistoryOrigin && g_accumulatedFrames==0);
 assert(EnsureSkyHistoryResources(&d) && d.calls==6);
 g_requestedCellKind=2;CellChange();
 assert(g_reprojectEnabled && !g_skyHistoryTexture && !g_skyHistoryUAV && !g_skyHistorySRV);
 // Save reload can reset cell classification before the new cell is sampled.
 g_requestedCellKind=1;CellChange();assert(EnsureSkyHistoryResources(&d) && d.calls==9);
 DCFDiag::preview=g_skyHistorySRV;g_activeCellKind=0;
 assert(!UseSeparateSkyHistory());
 assert(EnsureSkyHistoryResources(&d) && d.calls==9);
 assert(!g_skyHistoryTexture && !g_skyHistoryUAV && !g_skyHistorySRV && !DCFDiag::preview);
 g_requestedReprojection=false;Toggle();
 std::cout<<"PASS: sky only for confirmed exterior reprojection; disabled/unknown/interior startup skips allocation; all 3 allocation failures roll back and retry; reuse, both MCM and cell transitions, preserved interior reprojection, history reset and preview ownership release pass\n";
}
'''
temp=root/'Tests/temp';temp.mkdir(exist_ok=True)
source=temp/'sky_lifecycle_test.cpp';source.write_text(fixture)
exe=temp/'sky_lifecycle_test.exe'
subprocess.run(['cl','/nologo','/std:c++20','/EHsc','/MD','/DNOMINMAX',str(source),
                '/Fo:'+str(temp/'sky_lifecycle_test.obj'),'/Fe:'+str(exe),'d3d11.lib'],check=True,cwd=root)
subprocess.run([str(exe)],check=True,cwd=root)
