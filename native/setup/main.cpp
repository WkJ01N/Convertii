#include <convertii/install.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <wrl/client.h>
#include <thread>
#include <memory>
#include <fstream>

namespace {
using namespace cvt;
Release release_info() {
 auto resource = FindResourceW(nullptr,MAKEINTRESOURCEW(101),RT_RCDATA); if (!resource) return {};
 auto size = SizeofResource(nullptr,resource); auto data = LockResource(LoadResource(nullptr,resource)); auto json = Json::parse(static_cast<const char*>(data),static_cast<const char*>(data)+size);
 return {json.value("Version","0.2.0"),json.value("FileName","Convertii-0.2.0-win-x64.zip"),json.value("Sha256",""),json.contains("DownloadUrl") && json["DownloadUrl"].is_string() ? json["DownloadUrl"].get<std::string>() : ""};
}
bool embedded_payload(const fs::path& file) {
 auto resource = FindResourceW(nullptr,MAKEINTRESOURCEW(102),RT_RCDATA); if (!resource) return false; auto size = SizeofResource(nullptr,resource); auto data = LockResource(LoadResource(nullptr,resource));
 Handle output(CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)); DWORD written; if (!output || !WriteFile(output.get(),data,size,&written,nullptr) || written != size) throw Error("Cannot unpack offline payload."); return true;
}
struct Setup {
 HWND window = nullptr, directory = nullptr, status = nullptr, install = nullptr, classic = nullptr, modern = nullptr, progress = nullptr; HFONT font = nullptr;
 std::thread worker; std::atomic_bool cancel{false}; bool busy = false, installed = false; bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE; Release release = release_info();
 ~Setup() { cancel = true; if (worker.joinable()) worker.join(); if (font) DeleteObject(font); }
 const wchar_t* t(const wchar_t* en,const wchar_t* zh) const { return chinese ? zh : en; }
 HWND make(const wchar_t* cls,const wchar_t* label,int id,int x,int y,int w,int h,DWORD style = 0) {
  int dpi = static_cast<int>(GetDpiForWindow(window)); auto scale = [dpi](int v) { return MulDiv(v,dpi,96); };
  auto control = CreateWindowExW(0,cls,label,WS_CHILD | WS_VISIBLE | style,scale(x),scale(y),scale(w),scale(h),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr); SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return control;
 }
 void build() {
  NONCLIENTMETRICSW metrics{sizeof(metrics)}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0); font = CreateFontIndirectW(&metrics.lfMessageFont);
  make(L"STATIC",t(L"Install Convertii (native Windows build)",L"安装 Convertii（原生 Windows 版本）"),0,16,16,550,24);
  make(L"STATIC",t(L"Installation folder",L"安装目录"),0,16,56,540,20);
  auto root = data_root().parent_path()/L"Programs"/L"Convertii"; directory = make(L"EDIT",root.c_str(),10,16,80,470,26,WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL); make(L"BUTTON",L"…",11,494,80,54,26,WS_TABSTOP);
  classic = make(L"BUTTON",t(L"Register Explorer context menu",L"注册资源管理器右键菜单"),12,16,124,530,26,WS_TABSTOP | BS_AUTOCHECKBOX); SendMessageW(classic,BM_SETCHECK,BST_CHECKED,0);
  modern = make(L"BUTTON",t(L"Windows 11 modern menu (trusted signature required)",L"Windows 11 新菜单（需受信任签名）"),13,16,158,530,26,WS_TABSTOP | BS_AUTOCHECKBOX); SendMessageW(modern,BM_SETCHECK,BST_CHECKED,0);
  make(L"STATIC",t(L"Current user only. Existing personal files will not be overwritten. GPL-3.0-only.",L"仅为当前用户安装，不覆盖个人文件。GPL-3.0-only。"),0,16,200,530,36);
  progress = make(PROGRESS_CLASSW,L"",0,16,248,530,18); SendMessageW(progress,PBM_SETRANGE32,0,1000);
  status = make(L"EDIT",L"",0,16,280,530,100,WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL);
  install = make(L"BUTTON",t(L"Install",L"安装"),14,436,394,110,30,WS_TABSTOP | BS_DEFPUSHBUTTON);
 }
 void start() {
  if (installed) { DestroyWindow(window); return; } if (busy) return;
  wchar_t target[32768]; GetWindowTextW(directory,target,32768); fs::path destination(target); bool use_classic = SendMessageW(classic,BM_GETCHECK,0,0)==BST_CHECKED, use_modern = SendMessageW(modern,BM_GETCHECK,0,0)==BST_CHECKED;
  if (worker.joinable()) worker.join(); cancel = false; busy = true; EnableWindow(install,FALSE); EnableWindow(directory,FALSE);
  worker = std::thread([this,destination,use_classic,use_modern] {
   auto root = temp_root()/wide("Setup-"+unique_id()); fs::create_directories(root); auto payload = root/wide(release.filename); std::string message; bool success = false;
   try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (!embedded_payload(payload)) {
     auto local = executable_dir()/wide(release.filename);
     if (fs::exists(local)) fs::copy_file(local,payload);
     else if (!release.url.empty()) download(release.url,payload,&cancel,[this](uint64_t received,uint64_t total) { PostMessageW(window,WM_APP+2,total ? static_cast<WPARAM>(received*1000/total) : 0,0); });
     else throw Error("No hosted URL configured. Place the matching portable ZIP beside this installer and retry.");
    }
    check_cancel(&cancel); message = install_archive(payload,destination,release,use_classic,use_modern); success = true;
   } catch (const winrt::hresult_error& e) { message = utf8(e.message().c_str()); }
   catch (const std::exception& e) { message = e.what(); }
   std::error_code error; fs::remove_all(root,error);
   auto output = std::make_unique<std::pair<bool,std::string>>(success,message); if (PostMessageW(window,WM_APP+1,0,reinterpret_cast<LPARAM>(output.get()))) output.release();
  });
 }
 static LRESULT CALLBACK proc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
  auto self = reinterpret_cast<Setup*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA)); if (message==WM_NCCREATE) { self = static_cast<Setup*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window = hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
  if (!self) return DefWindowProcW(hwnd,message,w,l);
  try {
   if (message==WM_CREATE) { self->build(); return 0; }
   if (message==WM_COMMAND && LOWORD(w)==14) { self->start(); return 0; }
   if (message==WM_COMMAND && LOWORD(w)==11 && !self->busy) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog; if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) { dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM); if (SUCCEEDED(dialog->Show(hwnd))) { Microsoft::WRL::ComPtr<IShellItem> item; dialog->GetResult(&item); PWSTR path; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { auto target = fs::path(path)/L"Convertii"; SetWindowTextW(self->directory,target.c_str()); CoTaskMemFree(path); } } } return 0;
   }
   if (message==WM_APP+1) { std::unique_ptr<std::pair<bool,std::string>> result(reinterpret_cast<std::pair<bool,std::string>*>(l)); self->busy = false; self->installed = result->first; auto description=(self->installed ? std::wstring(self->t(L"Installed. ",L"安装完成。")) : L"")+wide(result->second); SetWindowTextW(self->status,description.c_str()); EnableWindow(self->install,TRUE); EnableWindow(self->directory,TRUE); SetWindowTextW(self->install,self->installed ? self->t(L"Close",L"关闭") : self->t(L"Retry",L"重试")); if (self->installed) SendMessageW(self->progress,PBM_SETPOS,1000,0); return 0; }
   if (message==WM_APP+2) { SendMessageW(self->progress,PBM_SETPOS,w,0); return 0; }
   if (message==WM_CLOSE) { if (self->busy) { self->cancel = true; SetWindowTextW(self->status,self->t(L"Cancelling; installation commit will finish safely.",L"正在取消；若已进入安装提交阶段，将等待安全完成。")); return 0; } DestroyWindow(hwnd); return 0; }
   if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
  } catch (const std::exception& error) { MessageBoxW(hwnd,wide(error.what()).c_str(),L"Convertii Setup",MB_ICONERROR); }
  return DefWindowProcW(hwnd,message,w,l);
 }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
 using namespace cvt;
 try {
  winrt::init_apartment(winrt::apartment_type::single_threaded); SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int count; auto raw = CommandLineToArgvW(GetCommandLineW(),&count); std::vector<std::wstring> args; for (int i=1;i<count;++i) args.emplace_back(raw[i]); LocalFree(raw);
  if (args.size()==3 && args[0]==L"--create-zip") { create_archive(args[1],args[2]); return 0; }
  if (args.size()==2 && args[0]==L"--verify-payload") { auto release = release_info(); validate_archive(args[1],release); write_text(fs::path(args[1]+L".verified.txt"),"Native checksum and ZIP paths verified.\n"); return 0; }
  if ((args.size()==2 && args[0]==L"--uninstall") || (args.size()==3 && args[0]==L"--uninstall-worker")) {
   auto root = fs::absolute(args[1]); auto own = executable_dir();
   if (_wcsicmp(root.c_str(),own.c_str())==0) { auto temporary = temp_root()/wide("Uninstall-"+unique_id()+".exe"); fs::create_directories(temporary.parent_path()); fs::copy_file(own/L"ConvertiiSetup.exe",temporary); STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{}; auto command = quote(temporary.wstring())+L" --uninstall-worker "+quote(root.wstring())+L" "+std::to_wstring(GetCurrentProcessId()); if (!CreateProcessW(temporary.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) throw Error(win_error()); CloseHandle(process.hThread); CloseHandle(process.hProcess); return 0; }
   if(args.size()==3) { DWORD id=std::stoul(args[2]); Handle parent(OpenProcess(SYNCHRONIZE,FALSE,id)); if(parent && WaitForSingleObject(parent.get(),10000)!=WAIT_OBJECT_0) throw Error("Close the previous installer before uninstalling."); }
   if (MessageBoxW(nullptr,L"Uninstall Convertii? User settings will be preserved.",L"Convertii",MB_YESNO | MB_ICONQUESTION)!=IDYES) return 0;
   winrt::uninit_apartment(); winrt::init_apartment(winrt::apartment_type::multi_threaded); unregister_shell(root); uninstall_files(root); unregister_install(root); return 0;
  }
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&common); Setup setup;
  WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = Setup::proc; cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1); cls.lpszClassName = L"ConvertiiNativeSetup"; RegisterClassExW(&cls);
  auto dpi = GetDpiForSystem(); auto window = CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Convertii Setup",WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(580,dpi,96),MulDiv(480,dpi,96),nullptr,nullptr,instance,&setup); if (!window) throw Error(win_error()); ShowWindow(window,show);
  MSG message; while (GetMessageW(&message,nullptr,0,0)>0) if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } return 0;
 } catch (const std::exception& error) { if (GetCommandLineW() && wcsstr(GetCommandLineW(),L"--verify-payload")) return 1; MessageBoxW(nullptr,wide(error.what()).c_str(),L"Convertii Setup",MB_ICONERROR); return 1; }
}
