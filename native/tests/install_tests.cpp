#include <convertii/install.h>
#include <iostream>
#include <miniz.h>
using namespace cvt;
int wmain(int argc, wchar_t** argv) {
 try {
  if (argc==5 && std::wstring_view(argv[1])==L"--verify-release") {
   auto expected=sha256(argv[2]);
   for (int i=3;i<5;++i) {
    auto module=LoadLibraryExW(argv[i],nullptr,LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) throw Error("Cannot inspect native installer resources.");
    auto config=FindResourceW(module,MAKEINTRESOURCEW(101),RT_RCDATA);
    if (!config) { FreeLibrary(module); throw Error("Installer release configuration missing."); }
    auto bytes=static_cast<const char*>(LockResource(LoadResource(module,config)));
    auto json=Json::parse(bytes,bytes+SizeofResource(module,config));
    auto checksum=json.value("Sha256",""); std::transform(checksum.begin(),checksum.end(),checksum.begin(),toupper);
    if (checksum!=expected) { FreeLibrary(module); throw Error("Installer checksum differs from portable ZIP."); }
    auto payload=FindResourceW(module,MAKEINTRESOURCEW(102),RT_RCDATA);
    if (i==4) {
     if (!payload) { FreeLibrary(module); throw Error("Offline installer payload missing."); }
     auto raw=static_cast<const char*>(LockResource(LoadResource(module,payload)));
     if (hash_text(std::string_view(raw,SizeofResource(module,payload)))!=expected) { FreeLibrary(module); throw Error("Embedded offline payload differs from portable ZIP."); }
    } else if (payload) { FreeLibrary(module); throw Error("Online installer unexpectedly contains the engine payload."); }
    FreeLibrary(module);
   }
   std::cout<<"PASS: Both native installer configurations match the portable ZIP; offline embedded bytes match SHA-256 and online contains no payload.\n"; return 0;
  }
  auto root=fs::absolute(fs::path(L"artifacts")/wide("native-install-tests-"+unique_id())); fs::create_directories(root); int count=0;
  auto check=[&](bool test,const char* name) { if(!test) throw Error(name); std::cout<<"PASS: "<<name<<'\n'; ++count; };
  auto rejected=[&](auto action,const char* name) { bool failed=false; try{action();}catch(const std::exception&){failed=true;} check(failed,name); };
  auto source=root/L"source"; fs::create_directories(source/L"engine"); for(auto name:{L"Convertii.exe",L"ConvertiiSetup.exe",L"ConvertiiShell.dll",L"engine/ffmpeg.exe",L"engine/ffprobe.exe"}) write_text(source/name,"old");
  auto archive=root/L"payload.zip"; create_archive(source,archive); Release release{"0.2.0","payload.zip",sha256(archive),""}; validate_archive(archive,release); check(true,"Pinned archive accepted");
  auto bad=release; bad.checksum=std::string(64,'0'); rejected([&]{validate_archive(archive,bad);},"Wrong checksum rejected");
  auto incomplete=root/L"incomplete"; fs::create_directory(incomplete); write_text(incomplete/L"Convertii.exe","only app"); auto incomplete_zip=root/L"incomplete.zip"; create_archive(incomplete,incomplete_zip); auto missing=release; missing.checksum=sha256(incomplete_zip); rejected([&]{validate_archive(incomplete_zip,missing);},"Incomplete payload rejected");
  auto malicious=[&](const wchar_t* filename,std::vector<std::string> names) {
   mz_zip_archive zip{}; if(!mz_zip_writer_init_heap(&zip,0,0)) throw Error("Cannot create adversarial fixture.");
   for(auto& name:names) if(!mz_zip_writer_add_mem(&zip,name.c_str(),"test",4,MZ_BEST_SPEED)) throw Error("Cannot add adversarial entry.");
   void* data=nullptr; size_t bytes=0; if(!mz_zip_writer_finalize_heap_archive(&zip,&data,&bytes)) throw Error("Cannot finalize fixture."); auto file=root/filename; write_text(file,std::string_view(static_cast<char*>(data),bytes)); mz_free(data); mz_zip_writer_end(&zip); return file;
  };
  auto traversal=malicious(L"traversal.zip",{"../escape.txt"}); auto unsafe=release; unsafe.checksum=sha256(traversal); rejected([&]{validate_archive(traversal,unsafe);},"ZIP traversal rejected");
  auto duplicate=malicious(L"duplicate.zip",{"Convertii.exe","CONVERTII.exe"}); unsafe.checksum=sha256(duplicate); rejected([&]{validate_archive(duplicate,unsafe);},"Case-insensitive ZIP duplicates rejected");
  auto absolute=malicious(L"absolute.zip",{"C:/escape.txt"}); unsafe.checksum=sha256(absolute); rejected([&]{validate_archive(absolute,unsafe);},"Absolute ZIP paths rejected");
  auto integration=[](const fs::path&,const std::string&,bool,bool){return std::string{};};
  auto personal=root/L"personal"; fs::create_directory(personal); write_text(personal/L"photo.jpg","personal"); rejected([&]{install_archive(archive,personal,release,false,false,integration);},"Personal directory refused"); check(read_text(personal/L"photo.jpg")=="personal","Personal data preserved");
  auto destination=root/L"Convertii"; install_archive(archive,destination,release,true,true,integration); check(fs::exists(destination/L"install-manifest.json"),"Ownership recorded");
  write_text(source/L"Convertii.exe","new"); auto newer=root/L"new.zip"; create_archive(source,newer); auto updated=release; updated.checksum=sha256(newer);
  rejected([&]{install_archive(newer,destination,updated,true,true,[](auto&,auto&,bool,bool)->std::string{throw Error("Simulated integration failure");});},"Integration failure rolls back"); check(read_text(destination/L"Convertii.exe")=="old","Old app restored");
  install_archive(newer,destination,updated,true,true,integration); check(read_text(destination/L"Convertii.exe")=="new","Owned upgrade succeeds");
  write_text(destination/L"personal.txt","keep"); rejected([&]{install_archive(archive,destination,release,false,false,integration);},"Update refuses untracked files"); check(read_text(destination/L"personal.txt")=="keep","Untracked data unchanged");
  uninstall_files(destination,true); check(fs::exists(destination/L"Convertii.exe"),"Uninstall dry run does not mutate");
  { Handle lock(CreateFileW((destination/L"ConvertiiShell.dll").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)); rejected([&]{uninstall_files(destination);},"Locked DLL prevents partial uninstall"); check(fs::exists(destination/L"Convertii.exe") && fs::exists(destination/L"install-manifest.json"),"Preflight preserves all files and manifest"); }
  uninstall_files(destination); check(fs::exists(destination/L"personal.txt") && !fs::exists(destination/L"Convertii.exe"),"Uninstall removes only owned files");
  rejected([&]{uninstall_files(personal);},"Uninstall refuses unowned directory");
  for(auto& e:fs::directory_iterator(root)) if(e.is_directory()) check(e.path().filename().wstring().find(L".stage-")==std::wstring::npos && e.path().filename().wstring().find(L".previous-")==std::wstring::npos,"No leftover install stage");
  std::cout<<count<<" native installer checks passed (no system registration).\n"; return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
