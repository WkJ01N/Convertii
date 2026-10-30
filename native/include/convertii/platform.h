#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace cvt {
namespace fs = std::filesystem;
using Json = nlohmann::json;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Cancelled : Error { Cancelled() : Error("Cancelled.") {} };
class Handle {
 HANDLE value_ = nullptr;
public:
 Handle() = default; explicit Handle(HANDLE value) : value_(value) {}
 ~Handle() { reset(); }
 Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
 Handle(Handle&& other) noexcept : value_(other.release()) {}
 Handle& operator=(Handle&& other) noexcept { reset(other.release()); return *this; }
 HANDLE get() const { return value_; }
 explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
 HANDLE release() { auto value = value_; value_ = nullptr; return value; }
 void reset(HANDLE value = nullptr) { if (*this) CloseHandle(value_); value_ = value; }
};
std::string utf8(std::wstring_view text);
std::wstring wide(std::string_view text);
std::wstring quote(std::wstring_view value);
std::string read_text(const fs::path& path);
void write_text(const fs::path& path, std::string_view value);
std::string unique_id();
std::string sha256(const fs::path& path);
std::string hash_text(std::string_view text);
std::string current_sid();
fs::path executable_dir();
fs::path temp_root();
fs::path data_root();
std::string win_error(DWORD code = GetLastError());
void check_cancel(const std::atomic_bool* cancel);
struct ProcessResult { int code; std::string output, error; };
using LineCallback = std::function<void(const std::string&)>;
ProcessResult run(const fs::path& executable, const std::vector<std::wstring>& args,
 const std::atomic_bool* cancel = nullptr, LineCallback output = {}, unsigned timeout_ms = 0,
 const fs::path& working_directory = {});
struct Engine { fs::path ffmpeg, ffprobe; static Engine discover(); };
void atomic_write(const fs::path& path, const std::string& value);
fs::path publish_file(const fs::path& temporary, const fs::path& directory, const std::wstring& stem, const std::string& extension);
}
