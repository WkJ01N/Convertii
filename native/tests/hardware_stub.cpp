#include <convertii/platform.h>
#include <fstream>
#include <iostream>
int wmain(int count,wchar_t** arguments) {
 try {
  std::vector<std::wstring> args; bool hardware=false,lavfi=false;
  for(int i=1;i<count;++i) { args.emplace_back(arguments[i]); if(args.back()==L"h264_nvenc" || args.back()==L"h264_qsv" || args.back()==L"h264_amf") hardware=true; if(args.back()==L"lavfi") lavfi=true; }
  if(hardware) { if(wchar_t log[32768]; GetEnvironmentVariableW(L"CONVERTII_TEST_PROBE_LOG",log,32768)) std::ofstream(log,std::ios::app)<<"probe\n"; if(lavfi) return 0; std::cerr<<"Device lost (simulated)\n"; return 1; }
  wchar_t executable[32768]; if(!GetEnvironmentVariableW(L"CONVERTII_TEST_FFMPEG",executable,32768)) return 2;
  auto result=cvt::run(executable,args); std::cout<<result.output; std::cerr<<result.error; return result.code;
 }catch(const std::exception& e){std::cerr<<e.what();return 3;}
}
