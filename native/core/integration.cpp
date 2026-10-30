#include <convertii/install.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <VersionHelpers.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace cvt {
static const std::wstring clsid = L"{7CC6BF21-8EE4-4376-A67B-BAC4236BB111}";
static void set_reg(const std::wstring& path, const wchar_t* name, const std::wstring& value) { HKEY key; auto error = RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr); if (error) throw Error(win_error(error)); error = RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*2)); RegCloseKey(key); if (error) throw Error(win_error(error)); }
static void remove_reg(const std::wstring& path) { auto error = RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str()); if (error && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) throw Error(win_error(error)); }
static void remove_identity() {
 winrt::Windows::Management::Deployment::PackageManager manager;
 std::vector<winrt::hstring> packages;
 for (auto package : manager.FindPackagesForUser(L"")) if (package.Id().Name() == L"Convertii.Desktop") packages.push_back(package.Id().FullName());
 for (auto& name : packages) { auto result=manager.RemovePackageAsync(name).get(); if(result.ExtendedErrorCode().value<0) throw winrt::hresult_error(result.ExtendedErrorCode(),result.ErrorText()); }
}
static winrt::Windows::Foundation::Uri file_uri(const fs::path& path) {
 wchar_t value[32768]; DWORD size=32768; auto result=UrlCreateFromPathW(fs::absolute(path).c_str(),value,&size,0); if(FAILED(result)) throw Error("Cannot create package file URI."); return winrt::Windows::Foundation::Uri(value);
}
std::string register_shell(const fs::path& directory, bool modern) {
 if (!fs::exists(directory/L"Convertii.exe") || !fs::exists(directory/L"ConvertiiShell.dll")) throw Error("Convertii executable / shell library is missing.");
 set_reg(L"Software\\Classes\\CLSID\\"+clsid,nullptr,L"Convertii classic menu");
 set_reg(L"Software\\Classes\\CLSID\\"+clsid+L"\\InprocServer32",nullptr,(directory/L"ConvertiiShell.dll").wstring());
 set_reg(L"Software\\Classes\\CLSID\\"+clsid+L"\\InprocServer32",L"ThreadingModel",L"Apartment");
 set_reg(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\Convertii",nullptr,clsid);
 set_reg(L"Software\\Classes\\*\\shell\\Convertii",nullptr,L"Convertii"); set_reg(L"Software\\Classes\\*\\shell\\Convertii",L"LegacyDisable",L"");
 set_reg(L"Software\\Convertii\\Shell",nullptr,directory.wstring());
 std::string note = "Classic context menu registered.";
 try {
  remove_identity();
  OSVERSIONINFOEXW version{sizeof(version)}; version.dwBuildNumber=22000; auto condition=VerSetConditionMask(0,VER_BUILDNUMBER,VER_GREATER_EQUAL);
  if (modern && VerifyVersionInfoW(&version,VER_BUILDNUMBER,condition)) {
   auto package = directory/L"identity"/L"Convertii.Identity.msix"; if (!fs::exists(package)) throw Error("Signed identity package is missing.");
   winrt::Windows::Management::Deployment::PackageManager manager; winrt::Windows::Management::Deployment::AddPackageOptions options;
   options.ExternalLocationUri(file_uri(directory));
   auto result=manager.AddPackageByUriAsync(file_uri(package),options).get(); if(result.ExtendedErrorCode().value<0) throw winrt::hresult_error(result.ExtendedErrorCode(),result.ErrorText()); note += " Modern context menu registered.";
  }
 } catch (const winrt::hresult_error& error) { note += " Modern menu unavailable; classic remains active. " + utf8(error.message().c_str()); }
 catch (const std::exception& error) { note += " Modern menu unavailable; classic remains active. " + std::string(error.what()); }
 SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr); return note;
}
void unregister_shell(const fs::path& directory) {
 wchar_t registered[32768]{}; DWORD bytes = sizeof(registered); if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Convertii\\Shell",nullptr,RRF_RT_REG_SZ,nullptr,registered,&bytes) == ERROR_SUCCESS &&
  _wcsicmp(fs::absolute(directory).lexically_normal().c_str(),fs::path(registered).lexically_normal().c_str())) throw Error("Another Convertii directory owns the shell registration. Remove it from that installation.");
 remove_identity(); remove_reg(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\Convertii"); remove_reg(L"Software\\Classes\\*\\shell\\Convertii"); remove_reg(L"Software\\Classes\\CLSID\\"+clsid); remove_reg(L"Software\\Convertii\\Shell");
 SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
}
static fs::path shortcut_path() { PWSTR path = nullptr; if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs,0,nullptr,&path))) throw Error("Cannot locate Start menu."); auto result = fs::path(path)/L"Convertii.lnk"; CoTaskMemFree(path); return result; }
std::string register_install(const fs::path& directory, const std::string& version, bool classic, bool modern) {
 auto note = classic ? register_shell(directory,modern) : (unregister_shell(directory),std::string{});
 auto root = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Convertii";
 set_reg(root,L"DisplayName",L"Convertii"); set_reg(root,L"DisplayVersion",wide(version)); set_reg(root,L"Publisher",L"Convertii contributors"); set_reg(root,L"InstallLocation",directory.wstring()); set_reg(root,L"DisplayIcon",(directory/L"Convertii.exe").wstring());
 set_reg(root,L"UninstallString",quote((directory/L"ConvertiiSetup.exe").wstring())+L" --uninstall "+quote(directory.wstring()));
 Microsoft::WRL::ComPtr<IShellLinkW> link; if (FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))) throw Error("Cannot create Start menu shortcut.");
 link->SetPath((directory/L"Convertii.exe").c_str()); link->SetWorkingDirectory(directory.c_str()); Microsoft::WRL::ComPtr<IPersistFile> persist; link.As(&persist); if (FAILED(persist->Save(shortcut_path().c_str(),TRUE))) throw Error("Cannot save Start menu shortcut."); return note;
}
void unregister_install(const fs::path&) { remove_reg(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Convertii"); std::error_code error; fs::remove(shortcut_path(),error); }
}
