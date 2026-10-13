#include "../Source/src/DiagnosticsOverlay.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
using Microsoft::WRL::ComPtr;
void check(HRESULT hr){if(FAILED(hr)){std::cerr<<"HRESULT "<<std::hex<<hr<<"\n";std::abort();}}
std::vector<unsigned char> ReadRGBA(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* src) {
 D3D11_TEXTURE2D_DESC td{};src->GetDesc(&td);td.BindFlags=td.MiscFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;check(d->CreateTexture2D(&td,nullptr,&staging));c->CopyResource(staging.Get(),src);
 D3D11_MAPPED_SUBRESOURCE m{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<unsigned char> b(td.Width*td.Height*4);
 for(UINT y=0;y<td.Height;++y)memcpy(b.data()+y*td.Width*4,(char*)m.pData+y*m.RowPitch,td.Width*4);c->Unmap(staging.Get(),0);return b;
}
int main(){
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DCF4HiddenTest";RegisterClassW(&wc);
 HWND hwnd=CreateWindowW(wc.lpszClassName,L"Hidden DCF4 test",WS_OVERLAPPEDWINDOW,0,0,1280,1080,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=1280;sd.BufferDesc.Height=1080;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=1;sd.OutputWindow=hwnd;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;ComPtr<IDXGISwapChain> swap;D3D_FEATURE_LEVEL level;
 check(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&level,&ctx));
 assert(!DCFDiag::active);assert(DCFDiag::Prepare(dev.Get(),ctx.Get(),swap.Get()));
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.MipLevels=2;td.ArraySize=6;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
 std::array<std::vector<float>,12> pixels;std::array<D3D11_SUBRESOURCE_DATA,12> data{};
 for(int f=0;f<6;++f)for(int m=0;m<2;++m){int i=f*2+m,n=8>>m;pixels[i].resize(n*n*4);for(int j=0;j<n*n;++j){pixels[i][j*4]=(m?0.02f:0.125f);pixels[i][j*4+1]=float(f+1)*0.03f;pixels[i][j*4+2]=0.1f;pixels[i][j*4+3]=0.5f;}data[i]={pixels[i].data(),UINT(n*16),0};}
 ComPtr<ID3D11Texture2D> cube;ComPtr<ID3D11ShaderResourceView> srv;check(dev->CreateTexture2D(&td,data.data(),&cube));check(dev->CreateShaderResourceView(cube.Get(),nullptr,&srv));
 DCFDiag::SetPreviews(srv.Get(),srv.Get(),srv.Get(),srv.Get(),srv.Get(),1);
 DCFDiag::useMaterialMip=false;DCFDiag::alpha=false;DCFDiag::UpdatePreviews();
 auto color=ReadRGBA(dev.Get(),ctx.Get(),DCFDiag::previews[0].texture.Get());int expected=int(std::pow(0.25,1.0/2.2)*255+0.5);assert(abs(int(color[0])-expected)<=1);
 // Face centers have distinct green values; validates all six face orientations.
 for(int f=0;f<6;++f){int x=f*64+32,y=32;int e=int(std::pow((f+1)*0.06,1.0/2.2)*255+0.5);assert(abs(int(color[(y*384+x)*4+1])-e)<=1);}
 DCFDiag::alpha=true;DCFDiag::nextPreview=0;DCFDiag::UpdatePreviews();auto a=ReadRGBA(dev.Get(),ctx.Get(),DCFDiag::previews[0].texture.Get());assert(abs(int(a[0])-128)<=1);
 DCFDiag::alpha=false;DCFDiag::useMaterialMip=true;DCFDiag::nextPreview=0;DCFDiag::UpdatePreviews();auto m=ReadRGBA(dev.Get(),ctx.Get(),DCFDiag::previews[0].texture.Get());assert(m[0]<color[0]);
 // Real timestamp queries, no GPU busy-wait inside the production profiler.
 const char* passes[]={"Timing control","Capture setup","History clear","Reprojection","Capture constants","Capture bindings","Capture dispatch","Capture restore","Capture mips","Inference","Output copy","Output mips","Pool setup","Pool replication","Tint analysis"};
 static_assert(std::size(passes)<=Profiler::kMaxTimers);
 for(int j=0;j<12;++j){DCFDiag::BeginUpdate();for(auto* name:passes){DCFDiag::BeginPass(name);if(std::string(name)!="Timing control")ctx->CopyResource(DCFDiag::previews[1].texture.Get(),DCFDiag::previews[0].texture.Get());DCFDiag::EndPass();}DCFDiag::EndUpdate();ctx->Flush();Sleep(10);}

 assert(DCFDiag::profiler.GetResults().size()==std::size(passes));
 assert(DCFDiag::spikeSummary.updates>0);
 auto summaryText=DCFDiag::spikeSummary.Report();assert(summaryText.find("Output copy")!=std::string::npos && summaryText.find("Output mips")!=std::string::npos);
 assert(DCFDiag::spikeSummary.updates==0);
 assert(!DCFDiag::profiler.GetResults().empty());for(auto& r:DCFDiag::profiler.GetResults()){assert(std::isfinite(r.avgMs));assert(r.avgMs>=0);}
 // Render the actual overlay offscreen for visual review, without opening a window.
 auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(DCFDiag::gui);
 ComPtr<ID3D11Texture2D> buffer;check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)));ComPtr<ID3D11RenderTargetView> rt;check(dev->CreateRenderTargetView(buffer.Get(),nullptr,&rt));auto* raw=rt.Get();ctx->OMSetRenderTargets(1,&raw,nullptr);float clear[4]={0.04f,0.05f,0.06f,1};ctx->ClearRenderTargetView(raw,clear);
 ctx->CSSetShader(DCFDiag::previewShader.Get(),nullptr,0);
 auto* oldSource=srv.Get();ctx->CSSetShaderResources(0,1,&oldSource);
 auto* cb=DCFDiag::previewConstants.Get();ctx->CSSetConstantBuffers(0,1,&cb);
 DCFDiag::nextPreview=0;DCFDiag::RenderOverlay(swap.Get());
 ComPtr<ID3D11ComputeShader> restored;ctx->CSGetShader(&restored,nullptr,nullptr);assert(restored.Get()==DCFDiag::previewShader.Get());
 ComPtr<ID3D11ShaderResourceView> restoredSRV;ctx->CSGetShaderResources(0,1,&restoredSRV);assert(restoredSRV.Get()==srv.Get());
 ComPtr<ID3D11Buffer> restoredCB;ctx->CSGetConstantBuffers(0,1,&restoredCB);assert(restoredCB.Get()==cb);
 ComPtr<ID3D11RenderTargetView> restoredRT;ctx->OMGetRenderTargets(1,&restoredRT,nullptr);assert(restoredRT.Get()==rt.Get());
 auto screen=ReadRGBA(dev.Get(),ctx.Get(),buffer.Get());std::ofstream out("Tests/overlay-review.rgba",std::ios::binary);out.write((char*)screen.data(),screen.size());out.close();ImGui::SetCurrentContext(previous);
 const ULONG beforeSkyRelease=srv->AddRef();srv->Release();
 DCFDiag::ClearSkyPreview();
 const ULONG afterSkyRelease=srv->AddRef();srv->Release();
 assert(beforeSkyRelease==afterSkyRelease+1 && !DCFDiag::sources[1] && DCFDiag::sources[0].Get()==srv.Get());
 DCFDiag::nextPreview=0;DCFDiag::UpdatePreviews();assert(!DCFDiag::sources[1]);
 DCFDiag::Disable();assert(!DCFDiag::active && !DCFDiag::gui && !DCFDiag::context && !DCFDiag::previews[0].texture);
 DCFDiag::BeginUpdate();DCFDiag::BeginPass("disabled");DCFDiag::EndPass();DCFDiag::EndUpdate();assert(DCFDiag::profiler.GetResults().empty());
 assert(DCFDiag::Prepare(dev.Get(),ctx.Get(),swap.Get()));DCFDiag::Disable();DestroyWindow(hwnd);
 Profiler::RollingHistory history;for(int i=0;i<400;++i)history.PushSample(float(i));assert(history.count==300);assert(abs(history.GetAverage()-249.5f)<0.01f);assert(history.GetPercentile(99)>395);
 SpikeSummary summary;
 std::vector<Profiler::TimerResult> synthetic(3);
 synthetic[0].name="Timing control";synthetic[0].valid=true;synthetic[0].gpuTimeMs=.3f;
 synthetic[1].name="Output mips";synthetic[1].valid=true;synthetic[1].gpuTimeMs=.4f;synthetic[1].cpuTimeMs=.002f;
 synthetic[2].name="Capture dispatch";synthetic[2].valid=false;synthetic[2].gpuTimeMs=99;
 summary.Observe(1,synthetic);summary.Observe(1,synthetic);
 assert(summary.updates==1 && summary.controlSpikes==1 && summary.gpuSpikeUpdates==1 && summary.gpuMultiStage==0 && summary.stages.size()==2);
 synthetic[2].valid=true;synthetic[2].gpuTimeMs=.5f;summary.Observe(2,synthetic);
 assert(summary.updates==2 && summary.gpuMultiStage==1);
 auto text=summary.Report();assert(text.find("completedUpdates=2")!=std::string::npos && text.find("emptyControlGpuSpikes=2")!=std::string::npos);
 summary.Observe(2,synthetic);assert(summary.updates==0);summary.Observe(3,synthetic);assert(summary.updates==1);
 std::cout<<"PASS: graphics state restoration; D3D11 WARP preview faces, confidence, mip selection, real queries, offscreen UI, disable/re-enable, rolling stats\n";
 return 0;
}
