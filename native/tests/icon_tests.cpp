// Checks that a built executable carries the multi-size application icon that
// native/app/Convertii.ico packs, so a shortcut or the task bar never falls back
// to the generic Windows icon.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool ok,const std::string& what) {
 std::printf("%s %s\n",ok ? "ok" : "FAILED",what.c_str());
 if (!ok) ++failures;
}

std::string narrow(const wchar_t* value) {
 auto length = WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
 std::string text(static_cast<size_t>(length > 0 ? length - 1 : 0),'\0');
 if (length > 1) WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),length,nullptr,nullptr);
 return text;
}

} // namespace

int wmain(int argc,wchar_t** argv) {
 if (argc != 2) { std::printf("usage: IconTests <executable>\n"); return 2; }
 auto path = narrow(argv[1]);
 auto module = LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE);
 if (!module) {
  std::printf("FAILED cannot open %s (error %lu)\n",path.c_str(),GetLastError());
  return 1;
 }
 auto resource = FindResourceW(module,MAKEINTRESOURCEW(1),RT_GROUP_ICON);
 check(resource != nullptr,"executable carries an RT_GROUP_ICON with id 1");
 if (!resource) { std::printf("%s has no application icon at all\n",path.c_str()); return 1; }

 auto data = static_cast<const unsigned char*>(LockResource(LoadResource(module,resource)));
 auto size = SizeofResource(module,resource);
 check(data != nullptr && size >= 6,"icon group resource is readable");
 if (!data || size < 6) return 1;

 const auto count = *reinterpret_cast<const unsigned short*>(data + 4);
 check(count >= 6,"icon group has at least six sizes");
 check(size >= 6 + count * 14ull,"icon group holds a directory entry per size");

 std::set<int> widths;
 for (unsigned short index = 0; index < count; ++index) {
  auto entry = data + 6 + index * 14;
  if (6 + (index + 1) * 14ull > size) break;
  widths.insert(entry[0] == 0 ? 256 : entry[0]);
 }
 std::printf("sizes:");
 for (auto width : widths) std::printf(" %d",width);
 std::printf("\n");
 check(widths.count(16) == 1,"icon has a 16 px entry");
 check(widths.count(32) == 1,"icon has a 32 px entry");
 check(widths.count(48) == 1,"icon has a 48 px entry");
 check(widths.count(256) == 1,"icon has a 256 px entry");

 FreeLibrary(module);
 std::printf("%s\n",failures == 0 ? "icon checks passed" : "icon checks failed");
 return failures == 0 ? 0 : 1;
}
