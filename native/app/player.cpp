#include "player.h"
#include <mfapi.h>
#include <d3d11.h>
#include <d3d10.h>
#include <audioclient.h>

namespace cvt {
using Microsoft::WRL::ComPtr;
static void hr(HRESULT code) { if (FAILED(code)) throw Error("Media Foundation error: " + win_error(static_cast<DWORD>(code))); }
class Notify final : public IMFMediaEngineNotify {
 std::atomic<ULONG> refs_{1}; HWND window_; unsigned generation_;
public:
 Notify(HWND window,unsigned generation) : window_(window),generation_(generation) {}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override { if (!value) return E_POINTER; *value = nullptr; if (id == IID_IUnknown || id == __uuidof(IMFMediaEngineNotify)) { *value = static_cast<IMFMediaEngineNotify*>(this); AddRef(); return S_OK; } return E_NOINTERFACE; }
 ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
 ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
 HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR first, DWORD) override {
  if (event == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE) { SetEvent(reinterpret_cast<HANDLE>(first)); return S_OK; }
  PostMessageW(window_,PLAYER_EVENT,event,generation_); return S_OK;
 }
};
Player::~Player() { close(); }
void Player::open(HWND video, HWND notify, const fs::path& file) {
 close(); hr(MFStartup(MF_VERSION)); started_ = true; video_ = video;
 try {
  ComPtr<IMFAttributes> attributes; hr(MFCreateAttributes(&attributes,5)); ComPtr<Notify> events; events.Attach(new Notify(notify,generation_));
  hr(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK,events.Get()));
  ComPtr<ID3D11Device> device; UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  auto device_result = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr);
  if (FAILED(device_result)) hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr));
  ComPtr<ID3D10Multithread> multi; if (SUCCEEDED(device.As(&multi))) multi->SetMultithreadProtected(TRUE);
  ComPtr<IMFDXGIDeviceManager> manager; UINT token; hr(MFCreateDXGIDeviceManager(&token,&manager)); hr(manager->ResetDevice(device.Get(),token));
  hr(attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER,manager.Get())); hr(attributes->SetGUID(MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE,MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE_IE_EDGE)); hr(attributes->SetUINT32(MF_MEDIA_ENGINE_AUDIO_CATEGORY,AudioCategory_Media));
  ComPtr<IMFMediaEngineClassFactory> factory; hr(CoCreateInstance(CLSID_MFMediaEngineClassFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
  hr(factory->CreateInstance(0,attributes.Get(),&engine_)); hr(engine_.As(&extended_));
  BSTR source = SysAllocString(fs::absolute(file).c_str()); if (!source) throw Error("Out of memory."); auto result = engine_->SetSource(source); SysFreeString(source); hr(result); engine_->SetAutoPlay(FALSE); hr(engine_->Load()); resize();
 } catch (...) { close(); throw; }
}
void Player::ready() {
 if (!extended_ || visual_) return;
 hr(extended_->EnableWindowlessSwapchainMode(TRUE)); resize(); hr(DCompositionCreateDevice(nullptr,IID_PPV_ARGS(&composition_))); hr(composition_->CreateTargetForHwnd(video_,TRUE,&target_)); hr(composition_->CreateVisual(&visual_));
 HANDLE swapchain = nullptr; hr(extended_->GetVideoSwapchainHandle(&swapchain)); if (!swapchain) throw Error("Video swapchain unavailable.");
 ComPtr<IUnknown> surface; auto code = composition_->CreateSurfaceFromHandle(swapchain,&surface); CloseHandle(swapchain); hr(code); hr(visual_->SetContent(surface.Get())); hr(target_->SetRoot(visual_.Get())); hr(composition_->Commit());
}
void Player::close() { ++generation_; if (engine_) engine_->Shutdown(); visual_.Reset(); target_.Reset(); composition_.Reset(); extended_.Reset(); engine_.Reset(); if (started_) MFShutdown(); started_ = false; video_ = nullptr; }
void Player::resize() { if (!extended_ || !video_) return; RECT bounds; GetClientRect(video_,&bounds); MFVideoNormalizedRect source{0,0,1,1}; MFARGB background{0,0,0,255}; extended_->UpdateVideoStream(&source,&bounds,&background); }
void Player::play() { if (engine_) hr(engine_->Play()); }
void Player::pause() { if (engine_) engine_->Pause(); }
void Player::seek(double seconds) { if (engine_) hr(engine_->SetCurrentTime(std::max(0.,seconds))); }
double Player::position() const { return engine_ ? engine_->GetCurrentTime() : 0; }
double Player::duration() const { return engine_ ? engine_->GetDuration() : 0; }
bool Player::paused() const { return !engine_ || engine_->IsPaused(); }
}
