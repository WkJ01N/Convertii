#include "window.h"
#include <wincodec.h>
#include <winrt/base.h>
#include <psapi.h>
#include <iostream>
#include <chrono>

namespace cvt {
struct UiTestAccess {
 static void pump(unsigned milliseconds) { auto end=GetTickCount64()+milliseconds; do { MSG message; while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { if(message.message==WM_QUIT) continue; TranslateMessage(&message); DispatchMessageW(&message); } Sleep(5); } while(GetTickCount64()<end); }
 static void snapshot(HWND window,const fs::path& file) {
  RECT bounds; GetWindowRect(window,&bounds); int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top; HDC screen=GetDC(window), memory=CreateCompatibleDC(screen); HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height); auto old=SelectObject(memory,bitmap);
  if(!PrintWindow(window,memory,PW_RENDERFULLCONTENT)) SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory; auto check=[](HRESULT result){if(FAILED(result)) throw Error("WIC screenshot failed.");};
  check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))); Microsoft::WRL::ComPtr<IWICBitmap> source; check(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&source));
  Microsoft::WRL::ComPtr<IWICStream> stream; check(factory->CreateStream(&stream)); check(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)); Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder; check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)); check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame; check(encoder->CreateNewFrame(&frame,nullptr)); check(frame->Initialize(nullptr)); check(frame->WriteSource(source.Get(),nullptr)); check(frame->Commit()); check(encoder->Commit()); SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window,screen);
 }
 static int run() {
  auto root=fs::absolute(L"artifacts/native-ui-tests"); fs::create_directories(root); SetEnvironmentVariableW(L"CONVERTII_DATA_DIR",(root/L"settings").c_str()); SetEnvironmentVariableW(L"CONVERTII_CACHE_DIR",(root/L"cache").c_str());
  auto engine=Engine::discover(); auto fixture=root/L"中文 sample.mp4"; auto result=cvt::run(engine.ffmpeg,{L"-v",L"error",L"-y",L"-f",L"lavfi",L"-i",L"testsrc2=size=128x96:duration=2:rate=10",L"-c:v",L"libx264",fixture.wstring()}); if(result.code) throw Error(result.error);
  int checks=0; auto check=[&](bool condition,const char* name){if(!condition) throw Error(name); ++checks; std::cout<<"PASS: "<<name<<'\n';};
  for(bool chinese:{false,true}) {
   Settings settings; settings.chinese=chinese; settings.options.encoder="software"; Window window(GetModuleHandleW(nullptr),settings); auto started=GetTickCount64(); HWND hwnd=window.open(SW_HIDE); auto startup_ms=GetTickCount64()-started;
   PROCESS_MEMORY_COUNTERS_EX idle{}; GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&idle),sizeof(idle));
   SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW); SetLayeredWindowAttributes(hwnd,0,0,LWA_ALPHA); SetWindowPos(hwnd,HWND_BOTTOM,-30000,-30000,0,0,SWP_NOSIZE | SWP_NOACTIVATE); ShowWindow(hwnd,SW_SHOWNOACTIVATE); pump(50); check(!window.player_.exists(),"Empty UI does not create a player");
   for(int page=0;page<6;++page) { TabCtrl_SetCurSel(window.tabs_,page); window.change_tab(); pump(10); snapshot(hwnd,root/(std::wstring(chinese ? L"zh-" : L"en-")+std::to_wstring(page)+L".png")); }
   TabCtrl_SetCurSel(window.tabs_,0); window.change_tab();
   SetWindowPos(hwnd,nullptr,0,0,window.px(760),window.px(560),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); pump(20);
   SetFocus(window.control(210)); pump(10); bool horizontal=window.scroll_x_[0]>0;
   SetFocus(window.control(215)); pump(10); RECT field{},page_bounds{}; GetWindowRect(window.control(215),&field); GetClientRect(window.pages_[0],&page_bounds); MapWindowPoints(window.pages_[0],nullptr,reinterpret_cast<POINT*>(&page_bounds),2);
   if (!(horizontal && window.scroll_[0]>0 && field.top>=page_bounds.top && field.bottom<=page_bounds.bottom)) std::cout<<"Scroll diagnostics: horizontal="<<horizontal<<", y="<<window.scroll_[0]<<", field="<<field.top<<".."<<field.bottom<<", page="<<page_bounds.top<<".."<<page_bounds.bottom<<'\n';
   check(horizontal && window.scroll_[0]>0 && field.top>=page_bounds.top && field.bottom<=page_bounds.bottom,"Small-window forms scroll keyboard focus into view");
   SetWindowPos(hwnd,nullptr,0,0,window.px(930),window.px(780),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); pump(20);
   window.receive({{fixture,fixture},{}}); pump(300); auto deadline=GetTickCount64()+10000; while(!window.media_ && GetTickCount64()<deadline) pump(20); check(window.jobs_.size()==1 && window.media_.has_value(),"Unicode input, deduplication and async probe");
   if(!chinese) {
    auto request_root=temp_root()/L"Requests"; fs::create_directories(request_root); auto file=request_root/wide(unique_id()+".req"); std::string content="convert\n"; for(int i=0;i<4000;++i) content+=utf8(fixture.wstring())+'\n'; write_text(file,content); auto launch=parse_launch({L"--request",file.wstring()}); check(launch.files.size()==4000 && launch.action==Action::Convert && !fs::exists(file),"4,000-file Unicode request parsing and cleanup");
    bool rejected=false; try{parse_launch({L"--request",fixture.wstring()});}catch(const std::exception&){rejected=true;} check(rejected && fs::exists(fixture),"Untrusted request location rejected");
    SetEnvironmentVariableW(L"CONVERTII_IPC_TEST_SUFFIX",wide(unique_id()).c_str());
    { Broker primary; check(primary.primary,"Native single-instance mutex ownership"); primary.listen(hwnd); auto extra=root/L"ipc-extra.mp4"; fs::copy_file(fixture,extra,fs::copy_options::overwrite_existing); launch.files.back()=extra;
     { Broker secondary; check(!secondary.primary && secondary.send(launch),"4,000-file named-pipe handoff"); }
     auto until=GetTickCount64()+7000; while(window.jobs_.size()<2 && GetTickCount64()<until) pump(10); check(window.jobs_.size()==2,"IPC selection arrives as one deduplicated batch"); window.jobs_.pop_back(); window.update_list();
    } SetEnvironmentVariableW(L"CONVERTII_IPC_TEST_SUFFIX",nullptr);
   }
   auto options=window.options(); check(options.target_mb==20 && options.encoder=="software","Native control settings round trip");
   window.text(240,L"0.5"); window.text(241,L"1.5"); window.text(242,L"2"); auto edits=window.options(); check(edits.start==.5 && edits.end==1.5 && edits.speed==2,"Trim page edits share one processing model");
   window.choice(200,1); window.execute(false); window.text(202,L"7"); deadline=GetTickCount64()+30000; while(window.busy_ && GetTickCount64()<deadline) pump(20);
   check(!window.busy_ && window.jobs_[0].state==State::Completed,"Native UI batch completes"); check(window.jobs_[0].request.options.target_mb==20,"Running job keeps its parameter snapshot");
   PROCESS_MEMORY_COUNTERS_EX memory{}; GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
   Json metrics{{"native",true},{"language",chinese ? "zh-CN" : "en-US"},{"startup_ms",startup_ms},{"idle_private_bytes",idle.PrivateUsage},{"idle_working_set",idle.WorkingSetSize},{"elapsed_ms",GetTickCount64()-started},{"after_batch_private_bytes",memory.PrivateUsage}};
   if(!chinese) {
    TabCtrl_SetCurSel(window.tabs_,1); window.change_tab(); window.play(); pump(500); deadline=GetTickCount64()+7000; while(window.player_.exists() && (!std::isfinite(window.player_.duration()) || window.player_.duration()<=0) && GetTickCount64()<deadline) pump(20);
    if(window.player_.exists() && window.player_.duration()>0) { window.player_.seek(.5); window.player_.play(); pump(300); window.player_.pause(); check(window.player_.position()>=.4,"Media Foundation seek/play/pause"); metrics["playback"]=true; }
    else { metrics["playback"]=false; std::cout<<"LIMITATION: Media Foundation playback unavailable in this environment.\n"; }
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)); metrics["playback_private_bytes"]=memory.PrivateUsage; metrics["playback_working_set"]=memory.WorkingSetSize;
    window.stop_preview(); check(!window.player_.exists(),"Player resources released");
   }
   write_text(root/(chinese ? L"metrics-zh.json" : L"metrics-en.json"),metrics.dump(2));
   window.shutdown(); DestroyWindow(hwnd); pump(20);
  }
  std::cout<<checks<<" native UI checks passed.\n"; return 0;
 }
};
}
int wmain() {
 try { winrt::init_apartment(winrt::apartment_type::single_threaded); INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES | ICC_BAR_CLASSES}; InitCommonControlsEx(&controls); return cvt::UiTestAccess::run(); }
 catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
