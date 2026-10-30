#include <convertii/platform.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <fstream>
#include <thread>
#include <array>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace cvt {
std::string utf8(std::wstring_view text) {
 if (text.empty()) return {};
 int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
 if (!size) throw Error("Invalid Unicode text.");
 std::string result(size, 0); WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr); return result;
}
std::wstring wide(std::string_view text) {
 if (text.empty()) return {};
 int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
 if (!size) throw Error("Invalid UTF-8 text.");
 std::wstring result(size, 0); MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size); return result;
}
std::wstring quote(std::wstring_view text) {
 std::wstring result = L"\""; unsigned slashes = 0;
 for (auto c : text) {
  if (c == L'\\') { ++slashes; continue; }
  if (c == L'\"') result.append(slashes * 2 + 1, L'\\'); else result.append(slashes, L'\\');
  result += c; slashes = 0;
 }
 result.append(slashes * 2, L'\\'); result += L'\"'; return result;
}
std::string read_text(const fs::path& path) {
 std::ifstream in(path, std::ios::binary); if (!in) throw Error("Cannot read: " + utf8(path.wstring()));
 std::string text((std::istreambuf_iterator<char>(in)), {});
 if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3); return text;
}
void write_text(const fs::path& path, std::string_view text) {
 std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(text.data(), text.size());
 if (!out) throw Error("Cannot write: " + utf8(path.wstring()));
}
std::string unique_id() { GUID id; if (FAILED(CoCreateGuid(&id))) throw Error("Cannot generate request ID."); wchar_t value[40]; StringFromGUID2(id, value, 40); return utf8(value); }
std::string win_error(DWORD code) {
 wchar_t* message = nullptr; FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
 auto result = message ? utf8(message) : "Windows error " + std::to_string(code); if (message) LocalFree(message); return result;
}
fs::path executable_dir() { std::wstring value(32768, 0); auto n = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size())); if (!n || n == value.size()) throw Error("Cannot locate application."); value.resize(n); return fs::path(value).parent_path(); }
fs::path temp_root() { wchar_t value[32768]; auto n = GetTempPathW(32768, value); if (!n || n >= 32768) throw Error("Cannot locate temporary folder."); return fs::path(value) / L"Convertii"; }
fs::path data_root() { wchar_t value[32768]; if (GetEnvironmentVariableW(L"CONVERTII_DATA_DIR", value, 32768)) return value; PWSTR path = nullptr; if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) throw Error("Cannot locate user data."); fs::path root = fs::path(path) / L"Convertii"; CoTaskMemFree(path); return root; }
std::string current_sid() {
 HANDLE raw; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) throw Error(win_error()); Handle token(raw);
 DWORD bytes = 0; GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes); std::vector<char> buffer(bytes);
 if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes)) throw Error(win_error());
 LPWSTR sid = nullptr; if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) throw Error(win_error());
 auto result = utf8(sid); LocalFree(sid); return result;
}
void check_cancel(const std::atomic_bool* cancel) { if (cancel && cancel->load()) throw Cancelled(); }
std::string sha256(const fs::path& path) {
 BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
 if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw Error("SHA-256 unavailable.");
 if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); throw Error("SHA-256 initialization failed."); }
 std::ifstream input(path, std::ios::binary); std::array<char, 65536> buffer; unsigned char digest[32];
 if (!input) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); throw Error("Cannot hash file."); }
 while (input) { input.read(buffer.data(), buffer.size()); if (input.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); throw Error("SHA-256 failed."); } }
 auto status = BCryptFinishHash(hash, digest, 32, 0); BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
 if (status < 0) throw Error("SHA-256 failed.");
 std::ostringstream out; out << std::uppercase << std::hex << std::setfill('0'); for (auto byte : digest) out << std::setw(2) << static_cast<unsigned>(byte); return out.str();
}
std::string hash_text(std::string_view text) {
 BCRYPT_ALG_HANDLE algorithm=nullptr; if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw Error("SHA-256 unavailable."); unsigned char digest[32];
 auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),static_cast<ULONG>(text.size()),digest,32); BCryptCloseAlgorithmProvider(algorithm,0); if(result<0) throw Error("SHA-256 failed.");
 std::ostringstream out; out<<std::uppercase<<std::hex<<std::setfill('0'); for(auto byte:digest) out<<std::setw(2)<<static_cast<unsigned>(byte); return out.str();
}
ProcessResult run(const fs::path& executable, const std::vector<std::wstring>& args,
 const std::atomic_bool* cancel, LineCallback callback, unsigned timeout, const fs::path& cwd) {
 check_cancel(cancel);
 SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; HANDLE r, w;
 if (!CreatePipe(&r, &w, &security, 0)) throw Error(win_error()); Handle out_read(r), out_write(w);
 if (!CreatePipe(&r, &w, &security, 0)) throw Error(win_error()); Handle err_read(r), err_write(w);
 SetHandleInformation(out_read.get(), HANDLE_FLAG_INHERIT, 0); SetHandleInformation(err_read.get(), HANDLE_FLAG_INHERIT, 0);
 Handle null_input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
 STARTUPINFOEXW info{}; info.StartupInfo.cb = sizeof(info); info.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; info.StartupInfo.wShowWindow = SW_HIDE;
 info.StartupInfo.hStdInput = null_input.get(); info.StartupInfo.hStdOutput = out_write.get(); info.StartupInfo.hStdError = err_write.get();
 SIZE_T attribute_size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size); std::vector<unsigned char> attributes(attribute_size);
 info.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
 if (!InitializeProcThreadAttributeList(info.lpAttributeList, 1, 0, &attribute_size)) throw Error(win_error());
 HANDLE inherited[] = {null_input.get(), out_write.get(), err_write.get()};
 if (!UpdateProcThreadAttribute(info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) { DeleteProcThreadAttributeList(info.lpAttributeList); throw Error(win_error()); }
 Handle job(CreateJobObjectW(nullptr, nullptr)); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { DeleteProcThreadAttributeList(info.lpAttributeList); throw Error(win_error()); }
 std::wstring command = quote(executable.wstring()); for (const auto& arg : args) command += L" " + quote(arg);
 PROCESS_INFORMATION process{};
 BOOL created = CreateProcessW(executable.is_absolute() ? executable.c_str() : nullptr, command.data(), nullptr, nullptr, TRUE,
  CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &info.StartupInfo, &process);
 auto error = GetLastError(); DeleteProcThreadAttributeList(info.lpAttributeList);
 if (!created) throw Error(win_error(error)); Handle child(process.hProcess), thread(process.hThread);
 if (!AssignProcessToJobObject(job.get(), child.get())) { TerminateProcess(child.get(), 1); throw Error(win_error()); }
 ResumeThread(thread.get()); out_write.reset(); err_write.reset();
 ProcessResult result{};
 auto drain = [](HANDLE pipe, std::string& text, LineCallback cb) {
  std::array<char, 4096> buffer; DWORD bytes; std::string pending;
  while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr) && bytes) {
   text.append(buffer.data(), bytes); if (text.size() > 4 * 1024 * 1024) text.erase(0, 2 * 1024 * 1024);
   if (cb) { pending.append(buffer.data(), bytes); size_t pos; while ((pos = pending.find('\n')) != std::string::npos) { auto line = pending.substr(0, pos); if (!line.empty() && line.back() == '\r') line.pop_back(); try { cb(line); } catch (...) {} pending.erase(0, pos + 1); } if (pending.size() > 65536) pending.clear(); }
  }
 };
 std::thread output_thread(drain, out_read.get(), std::ref(result.output), callback);
 std::thread error_thread(drain, err_read.get(), std::ref(result.error), LineCallback{});
 auto start = GetTickCount64(); bool timed_out = false;
 while (WaitForSingleObject(child.get(), 25) == WAIT_TIMEOUT) {
  if ((cancel && cancel->load()) || (timeout && GetTickCount64() - start >= timeout)) { timed_out = !(cancel && cancel->load()); TerminateJobObject(job.get(), 1); WaitForSingleObject(child.get(), INFINITE); break; }
 }
 // Kill any descendant retaining a pipe before joining readers.
 TerminateJobObject(job.get(), 1); output_thread.join(); error_thread.join();
 DWORD exit; GetExitCodeProcess(child.get(), &exit); result.code = static_cast<int>(exit); check_cancel(cancel);
 if (timed_out) throw Error("Process timed out."); return result;
}
Engine Engine::discover() {
 wchar_t value[32768]; fs::path root = executable_dir() / L"engine";
 if (GetEnvironmentVariableW(L"CONVERTII_ENGINE_DIR", value, 32768)) root = value;
 if (fs::exists(root / L"ffmpeg.exe") && fs::exists(root / L"ffprobe.exe")) return {root / L"ffmpeg.exe", root / L"ffprobe.exe"};
 return {L"ffmpeg.exe", L"ffprobe.exe"};
}
void atomic_write(const fs::path& path, const std::string& value) { fs::create_directories(path.parent_path()); auto temp = path; temp += wide("." + unique_id() + ".tmp"); write_text(temp, value); if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { fs::remove(temp); throw Error(win_error()); } }
fs::path publish_file(const fs::path& temporary, const fs::path& directory, const std::wstring& stem, const std::string& extension) {
 for (unsigned n = 0;; ++n) { auto path = directory / (stem + L"_convertii" + (n ? L" (" + std::to_wstring(n) + L")" : L"") + L"." + wide(extension));
  if (MoveFileW(temporary.c_str(), path.c_str())) return path; auto code = GetLastError(); if (code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS) throw Error(win_error(code)); }
}
}
