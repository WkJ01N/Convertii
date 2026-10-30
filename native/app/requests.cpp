#include "window.h"
#include <sddl.h>
#include <sstream>
namespace cvt {
Launch parse_launch(const std::vector<std::wstring>& args) {
 Launch launch;
 if (args.size() >= 2 && args[0] == L"--request") {
  auto root = fs::absolute(temp_root()/L"Requests").lexically_normal(); auto file = fs::absolute(args[1]).lexically_normal();
  if (file.parent_path() != root || file.extension() != L".req" || fs::file_size(file) > 16*1024*1024 || (GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) throw Error("Invalid shell request location.");
  std::istringstream input(read_text(file)); std::string line; std::getline(input,line); launch.action = line.starts_with("convert") ? Action::Convert : Action::Compress;
  while (std::getline(input,line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); auto path = fs::path(wide(line)); if (fs::is_regular_file(path)) launch.files.push_back(path); } fs::remove(file);
 } else for (auto& arg : args) { if (arg == L"--compress") launch.action = Action::Compress; else if (arg == L"--convert") launch.action = Action::Convert; else if (fs::is_regular_file(arg)) launch.files.push_back(fs::absolute(arg)); }
 return launch;
}
Broker::Broker() {
 auto key = wide(hash_text(current_sid()).substr(0,20)); wchar_t suffix[256]{}; if(GetEnvironmentVariableW(L"CONVERTII_IPC_TEST_SUFFIX",suffix,256)) key+=suffix;
 pipe_ = L"\\\\.\\pipe\\Convertii-"+key; auto name = L"Local\\Convertii-"+key;
 SetLastError(0); mutex_.reset(CreateMutexW(nullptr,TRUE,name.c_str())); if (!mutex_) throw Error(win_error()); primary = GetLastError() != ERROR_ALREADY_EXISTS;
}
Broker::~Broker() { stop_ = true; if (listener_.joinable()) { while(WaitForSingleObject(listener_.native_handle(),25)==WAIT_TIMEOUT) { CancelSynchronousIo(listener_.native_handle()); Handle wake(CreateFileW(pipe_.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr)); } listener_.join(); } if (primary) ReleaseMutex(mutex_.get()); }
bool Broker::send(const Launch& launch) {
 Json files = Json::array(); for (auto& file : launch.files) files.push_back(utf8(file.wstring())); auto value = Json{{"Files",files},{"Operation",launch.action ? Json(static_cast<int>(*launch.action)) : Json(nullptr)}}.dump();
 for (int attempt = 0; attempt < 80; ++attempt) {
  Handle pipe(CreateFileW(pipe_.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr)); if (pipe) { DWORD sent; return WriteFile(pipe.get(),value.data(),static_cast<DWORD>(value.size()),&sent,nullptr) && sent == value.size(); } Sleep(100);
 } return false;
}
void Broker::listen(HWND window) {
 window_ = window;
 listener_ = std::thread([this] {
  PSECURITY_DESCRIPTOR descriptor = nullptr; auto sddl = L"D:P(A;;GA;;;"+wide(current_sid())+L")";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)) { OutputDebugStringW(L"Convertii: pipe security descriptor creation failed.\n"); return; }
  SECURITY_ATTRIBUTES sa{sizeof(sa),descriptor,FALSE};
  while (!stop_) {
   Handle pipe(CreateNamedPipeW(pipe_.c_str(),PIPE_ACCESS_INBOUND,PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,1,8192,8192,0,&sa)); if (!pipe) { OutputDebugStringW((L"Convertii: pipe creation failed: "+wide(win_error())).c_str()); break; }
   if (!ConnectNamedPipe(pipe.get(),nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) continue;
   std::string data; char buffer[8192]; DWORD bytes; auto deadline=GetTickCount64()+15000;
   while (!stop_) {
    DWORD available=0; if(!PeekNamedPipe(pipe.get(),nullptr,0,nullptr,&available,nullptr)) break;
    if(!available) { if(GetTickCount64()>=deadline) { data.clear(); break; } Sleep(10); continue; }
    if(!ReadFile(pipe.get(),buffer,std::min<DWORD>(available,sizeof(buffer)),&bytes,nullptr) || !bytes) break;
    data.append(buffer,bytes); if (data.size() > 16*1024*1024) { data.clear(); break; }
   }
   if (!stop_ && !data.empty()) try { auto json = Json::parse(data); auto launch = std::make_unique<Launch>(); for (auto& f : json.at("Files")) launch->files.push_back(wide(f.get<std::string>())); if (!json["Operation"].is_null()) launch->action = json["Operation"].get<Action>(); if (PostMessageW(window_,LAUNCH_EVENT,0,reinterpret_cast<LPARAM>(launch.get()))) launch.release(); } catch (...) {}
  }
  LocalFree(descriptor);
 });
}
}
