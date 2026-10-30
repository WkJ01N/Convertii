#pragma once
#include <convertii/platform.h>

namespace cvt {
struct Release { std::string version = "0.2.0", filename = "Convertii-0.2.0-win-x64.zip", checksum, url; };
void validate_archive(const fs::path& archive, const Release& release);
void create_archive(const fs::path& directory, const fs::path& archive);
using InstallIntegration = std::function<std::string(const fs::path&,const std::string&,bool,bool)>;
std::string install_archive(const fs::path& archive, const fs::path& destination, const Release& release,
 bool classic, bool modern, InstallIntegration integration = {});
void uninstall_files(const fs::path& directory, bool dry_run = false);
std::string register_shell(const fs::path& directory, bool modern);
void unregister_shell(const fs::path& directory);
std::string register_install(const fs::path& directory, const std::string& version, bool classic, bool modern);
void unregister_install(const fs::path& directory);
void download(const std::string& url, const fs::path& destination, const std::atomic_bool* cancel,
 std::function<void(uint64_t,uint64_t)> progress = {});
}
