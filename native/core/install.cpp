#include <convertii/install.h>
#include <miniz.h>
#include <winhttp.h>
#include <fstream>
#include <set>
#include <algorithm>

namespace cvt {
struct ZipReader {
 FILE* file = nullptr; mz_zip_archive zip{};
 explicit ZipReader(const fs::path& path) { if (_wfopen_s(&file,path.c_str(),L"rb") || !file) throw Error("Cannot open payload ZIP."); if (!mz_zip_reader_init_cfile(&zip,file,0,0)) { fclose(file); file = nullptr; throw Error("Invalid payload ZIP."); } }
 ~ZipReader() { mz_zip_reader_end(&zip); if (file) fclose(file); }
};
static std::wstring lower(std::wstring value) { std::transform(value.begin(),value.end(),value.begin(),towlower); return value; }
static fs::path safe_relative(const std::string& name) {
 auto path = fs::path(wide(name)); if (path.empty() || path.is_absolute() || path.has_root_name() || name.find('\0') != std::string::npos || name.find(':') != std::string::npos) throw Error("Unsafe payload path.");
 for (auto& part : path) if (part == L".." || part == L".") throw Error("Unsafe payload path.");
 return path;
}
static void no_links(const fs::path& root) {
 if (!fs::exists(root)) return;
 if (GetFileAttributesW(root.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) throw Error("Directory links are not allowed in an installation.");
 for (auto& item : fs::recursive_directory_iterator(root)) if (GetFileAttributesW(item.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) throw Error("Installation contains a directory or file link.");
}
void validate_archive(const fs::path& archive, const Release& release) {
 if (release.checksum.size() != 64 || lower(wide(sha256(archive))) != lower(wide(release.checksum))) throw Error("Payload SHA-256 mismatch. Select the matching release ZIP.");
 ZipReader reader(archive); std::set<std::wstring> names;
 for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&reader.zip); ++i) {
  mz_zip_archive_file_stat stat; if (!mz_zip_reader_file_stat(&reader.zip,i,&stat)) throw Error("Invalid ZIP entry.");
  auto path = safe_relative(stat.m_filename); auto key = lower(path.lexically_normal().generic_wstring());
  if (!names.insert(key).second || ((stat.m_external_attr >> 16) & 0xf000) == 0xa000 || mz_zip_reader_is_file_encrypted(&reader.zip,i)) throw Error("Duplicate, linked or encrypted ZIP entry.");
 }
 for (auto required : {L"Convertii.exe",L"ConvertiiSetup.exe",L"ConvertiiShell.dll",L"engine/ffmpeg.exe",L"engine/ffprobe.exe"}) if (!names.contains(lower(required))) throw Error("Release payload is incomplete.");
}
void create_archive(const fs::path& root, const fs::path& archive) {
 no_links(root); FILE* file = nullptr; if (_wfopen_s(&file,archive.c_str(),L"w+b") || !file) throw Error("Cannot create ZIP."); mz_zip_archive zip{};
 if (!mz_zip_writer_init_cfile(&zip,file,0)) { fclose(file); throw Error("Cannot initialize ZIP."); }
 try {
  for (auto& e : fs::recursive_directory_iterator(root)) if (e.is_regular_file()) {
   FILE* input = nullptr; if (_wfopen_s(&input,e.path().c_str(),L"rb") || !input) throw Error("Cannot read payload file.");
   auto name = utf8(fs::relative(e.path(),root).generic_wstring());
   bool ok = mz_zip_writer_add_cfile(&zip,name.c_str(),input,e.file_size(),nullptr,nullptr,0,MZ_BEST_COMPRESSION,nullptr,0,nullptr,0) != 0; fclose(input); if (!ok) throw Error("Cannot compress payload file.");
  }
  if (!mz_zip_writer_finalize_archive(&zip)) throw Error("Cannot finalize ZIP.");
 } catch (...) { mz_zip_writer_end(&zip); fclose(file); throw; }
 mz_zip_writer_end(&zip); fclose(file);
}
static void extract(const fs::path& archive, const fs::path& root) {
 ZipReader reader(archive); fs::create_directory(root);
 for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&reader.zip); ++i) {
  mz_zip_archive_file_stat stat; mz_zip_reader_file_stat(&reader.zip,i,&stat); auto relative = safe_relative(stat.m_filename), path = root / relative;
  if (mz_zip_reader_is_file_a_directory(&reader.zip,i)) { fs::create_directories(path); continue; }
  fs::create_directories(path.parent_path()); Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)); if (!file) throw Error("Cannot create installed file: " + win_error());
  auto callback = [](void* opaque, mz_uint64, const void* buffer, size_t count) -> size_t { DWORD written = 0; if (count > MAXDWORD || !WriteFile(static_cast<HANDLE>(opaque),buffer,static_cast<DWORD>(count),&written,nullptr)) return 0; return written; };
  if (!mz_zip_reader_extract_to_callback(&reader.zip,i,callback,file.get(),0)) throw Error("ZIP extraction or CRC validation failed.");
 }
}
static Json owned_manifest(const fs::path& root) {
 auto marker = root / L"install-manifest.json"; auto j = Json::parse(read_text(marker));
 if (j.value("Product", "") != "Convertii" || !j.contains("Files") || !j["Files"].is_array()) throw Error("Not a Convertii installation.");
 for (auto& file : j["Files"]) safe_relative(file.get<std::string>()); return j;
}
static void delete_owned(const fs::path& root) {
 no_links(root); auto j = owned_manifest(root);
 std::vector<Handle> preflight;
 for (auto& file : j["Files"]) { auto path=root/safe_relative(file.get<std::string>()); if(!fs::exists(path)) continue;
  Handle handle(CreateFileW(path.c_str(),GENERIC_WRITE | DELETE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)); if(!handle) throw Error("Some files are locked. Close Convertii / restart Explorer, then retry. No owned files were removed."); preflight.push_back(std::move(handle));
 }
 for (auto& file : j["Files"]) { auto path = root/safe_relative(file.get<std::string>()); std::error_code error; fs::remove(path,error); if (error) throw Error("Some files are locked. Close Convertii / restart Explorer, then retry."); }
 preflight.clear();
 fs::remove(root/L"install-manifest.json"); std::vector<fs::path> dirs;
 for (auto& e : fs::recursive_directory_iterator(root)) if (e.is_directory()) dirs.push_back(e.path()); std::sort(dirs.rbegin(),dirs.rend());
 for (auto& dir : dirs) if (fs::is_empty(dir)) fs::remove(dir); if (fs::is_empty(root)) fs::remove(root);
}
std::string install_archive(const fs::path& archive, const fs::path& destination, const Release& release, bool classic, bool modern, InstallIntegration integration) {
 validate_archive(archive,release); auto root = fs::absolute(destination).lexically_normal(); if (root == root.root_path()) throw Error("Choose a dedicated application directory.");
 bool existed = fs::exists(root); no_links(root);
 if (existed && !fs::is_empty(root)) {
  if (!fs::exists(root/L"install-manifest.json")) throw Error("Folder contains personal files. Choose an empty folder or an existing Convertii installation.");
  auto old = owned_manifest(root); std::set<std::wstring> owned{L"install-manifest.json"}; for (auto& f : old["Files"]) owned.insert(lower(safe_relative(f.get<std::string>()).generic_wstring()));
  for (auto& f : fs::recursive_directory_iterator(root)) if (f.is_regular_file() && !owned.contains(lower(fs::relative(f.path(),root).generic_wstring()))) throw Error("Installation contains untracked files. Move personal files out before updating.");
  if(fs::exists(root/L"Convertii.exe")) { Handle app(CreateFileW((root/L"Convertii.exe").c_str(),GENERIC_WRITE | DELETE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)); if(!app) throw Error("Close the running Convertii application before updating."); }
 }
 fs::create_directories(root.parent_path()); auto stage = root; stage += wide(".stage-"+unique_id()); auto backup = root; backup += wide(".previous-"+unique_id()); bool swapped = false, committed = false;
 try {
  extract(archive,stage); Json files = Json::array(); for (auto& f : fs::recursive_directory_iterator(stage)) if (f.is_regular_file()) files.push_back(utf8(fs::relative(f.path(),stage).generic_wstring()));
  write_text(stage/L"install-manifest.json",Json{{"Product","Convertii"},{"Version",release.version},{"Files",files}}.dump(2));
  if (existed) fs::rename(root,backup);
  try { fs::rename(stage,root); swapped = true; } catch (...) { if (existed) fs::rename(backup,root); throw; }
  auto note = integration ? integration(root,release.version,classic,modern) : register_install(root,release.version,classic,modern); committed = true;
  if (fs::exists(backup)) { try { if (fs::is_empty(backup)) fs::remove(backup); else delete_owned(backup); } catch (...) { note+=" Previous files are still in use; backup retained at: "+utf8(backup.wstring()); } } return note;
 } catch (...) {
  if (swapped && !committed) { try { delete_owned(root); if (fs::exists(backup)) fs::rename(backup,root); } catch (...) { /* Keep backup if rollback is blocked. */ } }
  std::error_code error; fs::remove_all(stage,error); throw;
 }
}
void uninstall_files(const fs::path& directory, bool dry_run) {
 no_links(directory); owned_manifest(directory); if (dry_run) return; delete_owned(directory);
}
struct Internet { HINTERNET h = nullptr; explicit Internet(HINTERNET value) : h(value) { if (!h) throw Error(win_error()); } ~Internet() { WinHttpCloseHandle(h); } };
void download(const std::string& url_text, const fs::path& destination, const std::atomic_bool* cancel, std::function<void(uint64_t,uint64_t)> progress) {
 auto url = wide(url_text); URL_COMPONENTS parts{sizeof(parts)}; parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
 if (!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts) || parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength || parts.dwPasswordLength) throw Error("Release download requires an HTTPS URL.");
 std::wstring host(parts.lpszHostName,parts.dwHostNameLength), path(parts.lpszUrlPath,parts.dwUrlPathLength); if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
 Internet session(WinHttpOpen(L"Convertii/0.2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)); WinHttpSetTimeouts(session.h,15000,15000,15000,15000);
 Internet connection(WinHttpConnect(session.h,host.c_str(),parts.nPort,0)); Internet request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
 DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP; WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
 if (!WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request.h,nullptr)) throw Error(win_error());
 DWORD code = 0, bytes = sizeof(code); WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&bytes,nullptr); if (code != 200) throw Error("Download HTTP status " + std::to_string(code));
 wchar_t length[64]{}; bytes = sizeof(length); WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CONTENT_LENGTH,nullptr,length,&bytes,nullptr); uint64_t total = _wcstoui64(length,nullptr,10), received = 0;
 Handle file(CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)); if (!file) throw Error(win_error()); std::vector<char> buffer(65536); DWORD got;
 for (;;) { check_cancel(cancel); if (!WinHttpReadData(request.h,buffer.data(),static_cast<DWORD>(buffer.size()),&got)) throw Error(win_error()); if (!got) break; DWORD written; if (!WriteFile(file.get(),buffer.data(),got,&written,nullptr) || written != got) throw Error(win_error()); received += got; if (progress) progress(received,total); }
}
}
