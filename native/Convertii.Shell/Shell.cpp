// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <atomic>
#include <string>
#include <vector>
#include <new>
#include <algorithm>

using Microsoft::WRL::ComPtr;
// These identifiers must match Register-Shell.ps1 and AppxManifest.xml.
static const CLSID CLSID_Modern = {0x7cc6bf21,0x8ee4,0x4376,{0xa6,0x7b,0xba,0xc4,0x23,0x6b,0xb1,0x10}};
static const CLSID CLSID_Legacy = {0x7cc6bf21,0x8ee4,0x4376,{0xa6,0x7b,0xba,0xc4,0x23,0x6b,0xb1,0x11}};
static HMODULE moduleHandle;
static std::atomic<long> objects{0};

static bool Chinese() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE; }
static const wchar_t* CompressTitle() { return Chinese() ? L"压缩大小" : L"Compress size"; }
static const wchar_t* ConvertTitle() { return Chinese() ? L"转换格式" : L"Convert format"; }
static const wchar_t* FullTitle() { return Chinese() ? L"打开完整窗口…" : L"Open full window..."; }
// Menu entries: 0 = compress in the quick window, 1 = convert in the quick
// window, 2 = the full window (queue, tabs and all options).
static constexpr int kCommands = 3;
static std::wstring AppPath() {
    wchar_t buffer[32768];
    DWORD n = GetModuleFileNameW(moduleHandle, buffer, ARRAYSIZE(buffer));
    if (!n || n == ARRAYSIZE(buffer)) return {};
    std::wstring path(buffer, n);
    return path.substr(0, path.find_last_of(L"\\/") + 1) + L"Convertii.exe";
}

static HRESULT Launch(IShellItemArray* items, bool convert, bool quick) {
    if (!items) return E_INVALIDARG;
    DWORD count = 0;
    HRESULT hr = items->GetCount(&count);
    if (FAILED(hr) || count == 0) return E_INVALIDARG;
    std::wstring request = convert ? L"convert" : L"compress";
    if (quick) request += L" quick";
    request += L'\n';
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            const auto attributes = GetFileAttributesW(path);
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                request += path;
                request += L'\n';
            }
            CoTaskMemFree(path);
        }
    }
    if (request.size() > 8 * 1024 * 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    wchar_t temp[32768];
    auto length = GetTempPathW(ARRAYSIZE(temp), temp);
    if (!length || length >= ARRAYSIZE(temp)) return E_FAIL;
    auto directory = std::wstring(temp) + L"Convertii";
    CreateDirectoryW(directory.c_str(), nullptr);
    directory += L"\\Requests";
    CreateDirectoryW(directory.c_str(), nullptr);
    GUID id;
    if (FAILED(CoCreateGuid(&id))) return E_FAIL;
    wchar_t guid[40]; StringFromGUID2(id, guid, ARRAYSIZE(guid));
    auto file = directory + L"\\" + guid + L".req";
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    int bytes = WideCharToMultiByte(CP_UTF8, 0, request.data(), static_cast<int>(request.size()), nullptr, 0, nullptr, nullptr);
    std::vector<char> utf8(bytes);
    WideCharToMultiByte(CP_UTF8, 0, request.data(), static_cast<int>(request.size()), utf8.data(), bytes, nullptr, nullptr);
    DWORD written;
    bool success = WriteFile(handle, utf8.data(), bytes, &written, nullptr) && written == static_cast<DWORD>(bytes);
    CloseHandle(handle);
    if (!success) { DeleteFileW(file.c_str()); return E_FAIL; }
    auto app = AppPath();
    auto command = L"\"" + app + L"\" --request \"" + file + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        auto error = HRESULT_FROM_WIN32(GetLastError());
        DeleteFileW(file.c_str()); return error;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return S_OK;
}

class ExplorerCommand;
class Enumerator final : public IEnumExplorerCommand {
    std::atomic<ULONG> refs{1};
    ULONG position = 0;
public:
    Enumerator() { ++objects; }
    ~Enumerator() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == IID_IUnknown || id == IID_IEnumExplorerCommand) { *value = static_cast<IEnumExplorerCommand*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP Next(ULONG count, IExplorerCommand** commands, ULONG* fetched) override;
    IFACEMETHODIMP Skip(ULONG count) override { auto remaining = static_cast<ULONG>(kCommands) - position; position += std::min(count, remaining); return count <= remaining ? S_OK : S_FALSE; }
    IFACEMETHODIMP Reset() override { position = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** result) override {
        if (!result) return E_POINTER;
        auto clone = new(std::nothrow) Enumerator(); if (!clone) return E_OUTOFMEMORY;
        clone->position = position; *result = clone; return S_OK;
    }
};

class ExplorerCommand final : public IExplorerCommand {
    std::atomic<ULONG> refs{1};
    int action; // -1 parent, 0 compress, 1 convert
public:
    explicit ExplorerCommand(int value = -1) : action(value) { ++objects; }
    ~ExplorerCommand() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == IID_IUnknown || id == IID_IExplorerCommand) { *value = static_cast<IExplorerCommand*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* title) override {
        const wchar_t* value = L"Convertii";
        if (action == 0) value = CompressTitle();
        else if (action == 1) value = ConvertTitle();
        else if (action == 2) value = FullTitle();
        return SHStrDupW(value, title);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* icon) override { return SHStrDupW((AppPath() + L",0").c_str(), icon); }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* text) override { *text = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* name) override { *name = CLSID_Modern; name->Data1 += action + 1; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        DWORD count = 0; *state = items && SUCCEEDED(items->GetCount(&count)) && count ? ECS_ENABLED : ECS_HIDDEN; return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override { return Launch(items, action == 1, action != 2); }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override { *flags = action == -1 ? ECF_HASSUBCOMMANDS : ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override {
        *commands = nullptr;
        if (action != -1) return E_NOTIMPL;
        *commands = new(std::nothrow) Enumerator(); return *commands ? S_OK : E_OUTOFMEMORY;
    }
};
HRESULT Enumerator::Next(ULONG count, IExplorerCommand** commands, ULONG* fetched) {
    if (!commands || (count != 1 && !fetched)) return E_POINTER;
    ULONG done = 0;
    while (done < count && position < static_cast<ULONG>(kCommands)) {
        commands[done] = new(std::nothrow) ExplorerCommand(static_cast<int>(position));
        if (!commands[done]) { if (fetched) *fetched = done; return E_OUTOFMEMORY; }
        ++done; ++position;
    }
    if (fetched) *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

class LegacyMenu final : public IShellExtInit, public IContextMenu {
    std::atomic<ULONG> refs{1};
    ComPtr<IShellItemArray> selection;
public:
    LegacyMenu() { ++objects; }
    ~LegacyMenu() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == IID_IUnknown || id == IID_IShellExtInit) *value = static_cast<IShellExtInit*>(this);
        else if (id == IID_IContextMenu) *value = static_cast<IContextMenu*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE, IDataObject* data, HKEY) override {
        selection.Reset();
        return data ? SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&selection)) : E_INVALIDARG;
    }
    IFACEMETHODIMP QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        if ((flags & CMF_DEFAULTONLY) || last - first < static_cast<UINT>(kCommands) || !selection) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        HMENU child = CreatePopupMenu();
        if (!child) return E_FAIL;
        AppendMenuW(child, MF_STRING, first, CompressTitle());
        AppendMenuW(child, MF_STRING, first + 1, ConvertTitle());
        AppendMenuW(child, MF_STRING, first + 2, FullTitle());
        InsertMenuW(menu, index, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(child), L"Convertii");
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, kCommands);
    }
    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* info) override {
        if (HIWORD(info->lpVerb)) return E_INVALIDARG;
        auto command = LOWORD(info->lpVerb);
        return command < static_cast<UINT>(kCommands) ? Launch(selection.Get(), command == 1, command != 2) : E_INVALIDARG;
    }
    IFACEMETHODIMP GetCommandString(UINT_PTR, UINT, UINT*, LPSTR, UINT) override { return E_NOTIMPL; }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
    bool legacy;
public:
    explicit Factory(bool value) : legacy(value) { ++objects; }
    ~Factory() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == IID_IUnknown || id == IID_IClassFactory) { *value = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID id, void** value) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        IUnknown* object = legacy ? static_cast<IUnknown*>(static_cast<IShellExtInit*>(new(std::nothrow) LegacyMenu())) : static_cast<IUnknown*>(new(std::nothrow) ExplorerCommand());
        if (!object) return E_OUTOFMEMORY;
        auto result = object->QueryInterface(id, value); object->Release(); return result;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override { if (lock) ++objects; else --objects; return S_OK; }
};

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID id, void** result) {
    if (clsid != CLSID_Modern && clsid != CLSID_Legacy) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new(std::nothrow) Factory(clsid == CLSID_Legacy);
    if (!factory) return E_OUTOFMEMORY;
    auto hr = factory->QueryInterface(id, result); factory->Release(); return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return objects == 0 ? S_OK : S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { moduleHandle = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
