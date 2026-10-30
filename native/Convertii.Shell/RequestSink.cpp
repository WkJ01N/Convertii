// Test-only target. Never copied into a release bundle.
#include <windows.h>
#include <shellapi.h>
#include <string>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    wchar_t capture[32768];
    int result = 1;
    if (args && count == 3 && wcscmp(args[1], L"--request") == 0 &&
        GetEnvironmentVariableW(L"CONVERTII_SHELL_TEST_CAPTURE", capture, ARRAYSIZE(capture))) {
        auto temporary = std::wstring(capture) + L".tmp";
        result = CopyFileW(args[2], temporary.c_str(), FALSE) && MoveFileExW(temporary.c_str(), capture, MOVEFILE_REPLACE_EXISTING) ? 0 : 2;
        DeleteFileW(args[2]);
    }
    LocalFree(args);
    return result;
}
