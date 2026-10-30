#include "window.h"
#include <shellapi.h>
#include <sddl.h>
#include <winrt/base.h>
#include <fstream>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
 using namespace cvt;
 try {
  winrt::init_apartment(winrt::apartment_type::single_threaded); SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int count; auto raw = CommandLineToArgvW(GetCommandLineW(),&count); std::vector<std::wstring> args; for (int i=1;i<count;++i) args.emplace_back(raw[i]); LocalFree(raw);
  if (args.size() == 2 && args[0] == L"--check-engine") { auto engine = Engine::discover(); auto a = run(engine.ffmpeg,{L"-version"}), b = run(engine.ffprobe,{L"-version"}); write_text(args[1],Json{{"native",true},{"ffmpeg",utf8(engine.ffmpeg.wstring())},{"ffprobe",utf8(engine.ffprobe.wstring())},{"ExitCode",a.code},{"FfprobeExitCode",b.code}}.dump(2)); return a.code || b.code; }
  auto launch = parse_launch(args); Broker broker; if (!broker.primary) { if (!broker.send(launch)) throw Error("Cannot contact the running Convertii instance."); return 0; }
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&controls);
  Window window(instance,load_settings()); window.open(show); window.receive(launch); broker.listen(window.hwnd());
  MSG message; while (GetMessageW(&message,nullptr,0,0) > 0) if (!IsDialogMessageW(window.hwnd(),&message)) { TranslateMessage(&message); DispatchMessageW(&message); } return static_cast<int>(message.wParam);
 } catch (const winrt::hresult_error& e) { MessageBoxW(nullptr,e.message().c_str(),L"Convertii",MB_ICONERROR); return 1; }
 catch (const std::exception& e) { MessageBoxW(nullptr,wide(e.what()).c_str(),L"Convertii",MB_ICONERROR); return 1; }
}
