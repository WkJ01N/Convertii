#pragma once
#include <convertii/platform.h>
#include <mfmediaengine.h>
#include <dcomp.h>
#include <wrl/client.h>

namespace cvt {
constexpr UINT PLAYER_EVENT = WM_APP + 7;
class Player {
 Microsoft::WRL::ComPtr<IMFMediaEngine> engine_;
 Microsoft::WRL::ComPtr<IMFMediaEngineEx> extended_;
 Microsoft::WRL::ComPtr<IDCompositionDevice> composition_;
 Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
 Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
 HWND video_ = nullptr;
 bool started_ = false;
 unsigned generation_ = 0;
public:
 ~Player();
 void open(HWND video, HWND notify, const fs::path& file);
 void close();
 void ready();
 void resize();
 void play(); void pause(); void seek(double seconds);
 double position() const; double duration() const; bool paused() const;
 bool exists() const { return engine_ != nullptr; }
 bool is_ready() const { return engine_ && visual_; }
 bool accepts(LPARAM token) const { return engine_ && static_cast<unsigned>(token) == generation_; }
};
}
