#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <filesystem>
using Microsoft::WRL::ComPtr;
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto module = LoadLibraryW(argv[1]);
    if (!module) return 3;
    auto getClass = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
    auto unload = reinterpret_cast<HRESULT(__stdcall*)()>(GetProcAddress(module, "DllCanUnloadNow"));
    if (!getClass || !unload) return 4;
    int result = [&] {
        CLSID modern, legacy;
        CLSIDFromString(L"{7CC6BF21-8EE4-4376-A67B-BAC4236BB110}", &modern);
        CLSIDFromString(L"{7CC6BF21-8EE4-4376-A67B-BAC4236BB111}", &legacy);
        ComPtr<IClassFactory> factory;
        if (FAILED(getClass(modern, IID_PPV_ARGS(&factory)))) return 5;
        ComPtr<IExplorerCommand> command;
        if (FAILED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)))) return 6;
        PWSTR title = nullptr; command->GetTitle(nullptr, &title);
        if (!title || wcscmp(title, L"Convertii") != 0) return 7;
        CoTaskMemFree(title);
        EXPCMDFLAGS flags; command->GetFlags(&flags);
        if (flags != ECF_HASSUBCOMMANDS) return 8;
        ComPtr<IEnumExplorerCommand> enumerator;
        if (FAILED(command->EnumSubCommands(&enumerator))) return 9;
        IExplorerCommand* children[2]{}; ULONG fetched = 0;
        if (enumerator->Next(2, children, &fetched) != S_OK || fetched != 2) return 10;
        for (auto child : children) { child->GetTitle(nullptr, &title); if (!title) return 11; CoTaskMemFree(title); child->Release(); }
        if (enumerator->Next(1, children, &fetched) != S_FALSE || fetched != 0) return 12;
        if (enumerator->Reset() != S_OK || enumerator->Skip(3) != S_FALSE) return 13;
        factory.Reset();
        if (FAILED(getClass(legacy, IID_PPV_ARGS(&factory)))) return 14;
        ComPtr<IShellExtInit> init;
        if (FAILED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&init)))) return 15;
        ComPtr<IContextMenu> menu; if (FAILED(init.As(&menu))) return 16;
        HMENU popup = CreatePopupMenu();
        auto hr = menu->QueryContextMenu(popup, 0, 1, 100, CMF_DEFAULTONLY);
        DestroyMenu(popup);
        if (hr != S_OK) return 17;
        if (unload() != S_FALSE) return 18;
        wchar_t location[32768]; GetFullPathNameW(argv[1], ARRAYSIZE(location), location, nullptr);
        std::wstring directory(location); directory.resize(directory.find_last_of(L"\\/"));
        auto fixture = directory + L"\\中文 selection.mp4";
        auto file = CreateFileW(fixture.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 20; CloseHandle(file);
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(fixture.c_str(), nullptr, &pidl, 0, nullptr))) return 21;
        std::vector<PCIDLIST_ABSOLUTE> ids(4000, pidl);
        ComPtr<IShellItemArray> selection;
        auto selectionResult = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()), ids.data(), &selection);
        auto parent = ILCloneFull(pidl);
        std::vector<PCUITEMID_CHILD> relativeIds(4000, ILFindLastID(pidl));
        ILRemoveLastID(parent);
        ComPtr<IDataObject> selectionData;
        auto dataResult = SHCreateDataObject(parent, static_cast<UINT>(relativeIds.size()), relativeIds.data(), nullptr, IID_PPV_ARGS(&selectionData));
        CoTaskMemFree(parent);
        CoTaskMemFree(pidl);
        if (FAILED(selectionResult) || FAILED(dataResult)) return 22;
        if (FAILED(init->Initialize(nullptr, selectionData.Get(), nullptr))) return 25;
        popup = CreatePopupMenu();
        auto legacyCount = menu->QueryContextMenu(popup, 0, 1, 100, 0);
        DestroyMenu(popup);
        if (HRESULT_CODE(legacyCount) != 2) return 26;
        auto capture = directory + L"\\captured.req"; DeleteFileW(capture.c_str());
        SetEnvironmentVariableW(L"CONVERTII_SHELL_TEST_CAPTURE", capture.c_str());
        enumerator->Reset(); IExplorerCommand* child = nullptr;
        enumerator->Next(1, &child, &fetched);
        auto invokeResult = child->Invoke(selection.Get(), nullptr); child->Release();
        if (FAILED(invokeResult)) return 23;
        for (int tries = 0; tries < 500 && GetFileAttributesW(capture.c_str()) == INVALID_FILE_ATTRIBUTES; ++tries) Sleep(10);
        std::ifstream input(std::filesystem::path(capture), std::ios::binary);
        std::string captured((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();
        auto bytes = WideCharToMultiByte(CP_UTF8, 0, fixture.data(), static_cast<int>(fixture.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, fixture.data(), static_cast<int>(fixture.size()), utf8.data(), bytes, nullptr, nullptr);
        std::string expected = "compress\n";
        for (int i = 0; i < 4000; ++i) expected += utf8 + "\n";
        if (captured != expected) return 24;
        DeleteFileW(capture.c_str());
        CMINVOKECOMMANDINFO invocation{}; invocation.cbSize = sizeof(invocation); invocation.lpVerb = MAKEINTRESOURCEA(1);
        if (FAILED(menu->InvokeCommand(&invocation))) return 27;
        for (int tries = 0; tries < 500 && GetFileAttributesW(capture.c_str()) == INVALID_FILE_ATTRIBUTES; ++tries) Sleep(10);
        std::ifstream legacyInput(std::filesystem::path(capture), std::ios::binary);
        std::string legacyCaptured((std::istreambuf_iterator<char>(legacyInput)), std::istreambuf_iterator<char>());
        legacyInput.close();
        DeleteFileW(fixture.c_str()); DeleteFileW(capture.c_str()); SetEnvironmentVariableW(L"CONVERTII_SHELL_TEST_CAPTURE", nullptr);
        expected.replace(0, 9, "convert\n");
        if (legacyCaptured != expected) return 28;
        return 0;
    }();
    if (unload() != S_OK) result = 19;
    FreeLibrary(module); CoUninitialize();
    if (result == 0) std::puts("PASS: Native COM activation, submenu enumeration, legacy interface, DLL lifetime and 4,000-file Unicode request delivery (no registry changes).");
    else std::printf("FAIL: Native test %d\n", result);
    return result;
}
