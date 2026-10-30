// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include <new>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include "convertii/media_types.h"

using Microsoft::WRL::ComPtr;
// These identifiers must match Register-Shell.ps1 and AppxManifest.xml.
static const CLSID CLSID_Modern = {0x7cc6bf21,0x8ee4,0x4376,{0xa6,0x7b,0xba,0xc4,0x23,0x6b,0xb1,0x10}};
static const CLSID CLSID_Legacy = {0x7cc6bf21,0x8ee4,0x4376,{0xa6,0x7b,0xba,0xc4,0x23,0x6b,0xb1,0x11}};
static HMODULE moduleHandle;
static std::atomic<long> objects{0};

// Menu entries. 0 and 1 ask for the small window, 2 for the full window and 3
// for "convert this folder".
enum Command { kCompress = 0, kConvert = 1, kFull = 2, kFolder = 3 };

static bool Chinese() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE; }
static const wchar_t* CompressTitle() { return Chinese() ? L"压缩大小" : L"Compress size"; }
static const wchar_t* ConvertTitle() { return Chinese() ? L"转换格式" : L"Convert format"; }
static const wchar_t* FullTitle() { return Chinese() ? L"打开完整窗口…" : L"Open full window..."; }
static const wchar_t* FolderTitle() { return Chinese() ? L"转换此文件夹…" : L"Convert this folder..."; }
static const wchar_t* TitleFor(int command) {
    switch (command) {
    case kCompress: return CompressTitle();
    case kConvert: return ConvertTitle();
    case kFolder: return FolderTitle();
    default: return FullTitle();
    }
}

static std::wstring AppPath() {
    wchar_t buffer[32768];
    DWORD n = GetModuleFileNameW(moduleHandle, buffer, ARRAYSIZE(buffer));
    if (!n || n == ARRAYSIZE(buffer)) return {};
    std::wstring path(buffer, n);
    return path.substr(0, path.find_last_of(L"\\/") + 1) + L"Convertii.exe";
}

// User settings live next to the application's settings.json, so the menu can
// follow the same switches the settings page writes without a second copy.
static std::wstring DataRoot() {
    wchar_t value[32768];
    if (GetEnvironmentVariableW(L"CONVERTII_DATA_DIR", value, ARRAYSIZE(value))) return value;
    PWSTR known = nullptr;
    std::wstring root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        root.assign(known);
        root += L"\\Convertii";
    }
    CoTaskMemFree(known);
    return root;
}

struct MenuConfig {
    bool cascade = true, icons = true, media_only = true, folders = true;
    bool compress = true, convert = true, full = true;
};

static MenuConfig LoadConfig() {
    MenuConfig config;
    auto root = DataRoot();
    if (root.empty()) return config;
    std::ifstream input((root + L"\\settings.json").c_str(), std::ios::binary);
    if (!input) return config;
    try {
        auto value = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
        if (!value.contains("shell") || !value["shell"].is_object()) return config;
        const auto& shell = value["shell"];
        config.cascade = shell.value("cascade", true);
        config.icons = shell.value("icons", true);
        config.media_only = shell.value("media_only", true);
        config.folders = shell.value("folders", true);
        config.compress = shell.value("compress", true);
        config.convert = shell.value("convert", true);
        config.full = shell.value("full", true);
    } catch (...) {
        // A half written settings file must never break the context menu.
    }
    return config;
}

static bool IsMediaPath(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return false;
    std::string extension;
    for (auto index = dot; index < path.size(); ++index) {
        auto character = path[index];
        if (character > 0x7f) return false;
        extension.push_back(static_cast<char>(character));
    }
    return cvt::is_media_extension(cvt::lower_extension(extension));
}

struct Selection {
    std::vector<std::wstring> files, folders;
};

static Selection ReadSelection(IShellItemArray* items) {
    Selection selection;
    if (!items) return selection;
    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return selection;
    for (DWORD index = 0; index < count; ++index) {
        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (FAILED(items->GetItemAt(index, &item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) continue;
        const auto attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) selection.folders.emplace_back(path);
            else selection.files.emplace_back(path);
        }
        CoTaskMemFree(path);
    }
    return selection;
}

// The commands offered for one selection, in menu order.
static std::vector<int> CommandsFor(const Selection& selection, const MenuConfig& config) {
    std::vector<int> commands;
    if (selection.files.empty()) {
        if (config.folders && selection.folders.size() == 1) commands.push_back(kFolder);
        return commands;
    }
    if (config.media_only) {
        const auto media = std::any_of(selection.files.begin(), selection.files.end(), IsMediaPath);
        if (!media) return commands;
    }
    if (config.compress) commands.push_back(kCompress);
    if (config.convert) commands.push_back(kConvert);
    if (config.full) commands.push_back(kFull);
    return commands;
}

static HRESULT Launch(const Selection& selection, int command) {
    if (command == kFolder) {
        if (selection.folders.size() != 1) return E_INVALIDARG;
    } else if (selection.files.empty()) {
        return E_INVALIDARG;
    }
    std::wstring request = command == kConvert ? L"convert" : L"compress";
    if (command == kCompress || command == kConvert) request += L" quick";
    request += L'\n';
    if (command == kFolder) {
        request += selection.folders.front();
        request += L'\n';
    } else {
        for (const auto& file : selection.files) {
            request += file;
            request += L'\n';
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
    auto invocation = L"\"" + app + L"\" --request \"" + file + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.c_str(), invocation.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        auto error = HRESULT_FROM_WIN32(GetLastError());
        DeleteFileW(file.c_str()); return error;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return S_OK;
}

// A 16 pixel copy of the application icon for the classic (flat) menu.
static HBITMAP MenuIcon(int size) {
    static HBITMAP cached[2]{};
    static int cachedSize[2]{};
    static std::atomic<int> next{0};
    for (int index = 0; index < 2; ++index) {
        if (cachedSize[index] == size && cached[index]) return cached[index];
    }
    HICON icon = nullptr;
    if (!ExtractIconExW(AppPath().c_str(), 0, nullptr, &icon, 1) || !icon) return nullptr;
    auto screen = GetDC(nullptr);
    auto bitmap = CreateCompatibleBitmap(screen, size, size);
    auto memory = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (bitmap && memory) {
        auto previous = SelectObject(memory, bitmap);
        RECT area{0, 0, size, size};
        FillRect(memory, &area, reinterpret_cast<HBRUSH>(COLOR_MENU + 1));
        DrawIconEx(memory, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(memory, previous);
    }
    if (memory) DeleteDC(memory);
    DestroyIcon(icon);
    if (!bitmap) return nullptr;
    auto slot = next.fetch_add(1) % 2;
    if (cached[slot]) DeleteObject(cached[slot]);
    cached[slot] = bitmap;
    cachedSize[slot] = size;
    return bitmap;
}

class Enumerator final : public IEnumExplorerCommand {
    std::atomic<ULONG> refs{1};
    std::vector<int> commands;
    ULONG position = 0;
public:
    explicit Enumerator(std::vector<int> values) : commands(std::move(values)) { ++objects; }
    ~Enumerator() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == IID_IUnknown || id == IID_IEnumExplorerCommand) { *value = static_cast<IEnumExplorerCommand*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP Next(ULONG count, IExplorerCommand** result, ULONG* fetched) override;
    IFACEMETHODIMP Skip(ULONG count) override {
        auto remaining = static_cast<ULONG>(commands.size()) - position;
        position += std::min(count, remaining);
        return count <= remaining ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override { position = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** result) override {
        if (!result) return E_POINTER;
        auto clone = new(std::nothrow) Enumerator(commands); if (!clone) return E_OUTOFMEMORY;
        clone->position = position; *result = clone; return S_OK;
    }
};

class ExplorerCommand final : public IExplorerCommand {
    std::atomic<ULONG> refs{1};
    int command; // -1 for the parent entry
    MenuConfig config;
public:
    explicit ExplorerCommand(int value = -1) : command(value), config(LoadConfig()) { ++objects; }
    ~ExplorerCommand() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == IID_IUnknown || id == IID_IExplorerCommand) { *value = static_cast<IExplorerCommand*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    IFACEMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }
    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* title) override {
        return SHStrDupW(command < 0 ? L"Convertii" : TitleFor(command), title);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* icon) override {
        if (!config.icons) return E_NOTIMPL;
        return SHStrDupW((AppPath() + L",0").c_str(), icon);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* value) override { *value = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* name) override { *name = CLSID_Modern; name->Data1 += static_cast<unsigned long>(command + 1); return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        *state = ECS_HIDDEN;
        if (!items) return S_OK;
        const auto commands = CommandsFor(ReadSelection(items), config);
        if (commands.empty()) return S_OK;
        if (command < 0 || std::find(commands.begin(), commands.end(), command) != commands.end()) *state = ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        return Launch(ReadSelection(items), command);
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override { *flags = command < 0 ? ECF_HASSUBCOMMANDS : ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override {
        *commands = nullptr;
        if (command >= 0) return E_NOTIMPL;
        std::vector<int> list;
        if (config.compress) list.push_back(kCompress);
        if (config.convert) list.push_back(kConvert);
        if (config.full) list.push_back(kFull);
        if (config.folders) list.push_back(kFolder);
        *commands = new(std::nothrow) Enumerator(std::move(list)); return *commands ? S_OK : E_OUTOFMEMORY;
    }
};
HRESULT Enumerator::Next(ULONG count, IExplorerCommand** result, ULONG* fetched) {
    if (!result || (count != 1 && !fetched)) return E_POINTER;
    ULONG done = 0;
    while (done < count && position < static_cast<ULONG>(commands.size())) {
        result[done] = new(std::nothrow) ExplorerCommand(commands[position]);
        if (!result[done]) { if (fetched) *fetched = done; return E_OUTOFMEMORY; }
        ++done; ++position;
    }
    if (fetched) *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

class LegacyMenu final : public IShellExtInit, public IContextMenu {
    std::atomic<ULONG> refs{1};
    ComPtr<IShellItemArray> selection;
    Selection current;
    MenuConfig config = LoadConfig();
    std::vector<int> commands;
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
        commands.clear();
        current = Selection{};
        if (!data) return E_INVALIDARG;
        auto result = SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&selection));
        if (SUCCEEDED(result)) {
            current = ReadSelection(selection.Get());
            commands = CommandsFor(current, config);
        }
        return result;
    }
    IFACEMETHODIMP QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        if ((flags & CMF_DEFAULTONLY) || !selection || commands.empty()) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        const auto space = static_cast<UINT>(commands.size());
        if (last - first < space) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        if (config.cascade) {
            HMENU child = CreatePopupMenu();
            if (!child) return E_FAIL;
            for (size_t position = 0; position < commands.size(); ++position) {
                auto id = first + static_cast<UINT>(position);
                AppendMenuW(child, MF_STRING, id, TitleFor(commands[position]));
                if (config.icons && static_cast<int>(position) < 24) {
                    if (auto bitmap = MenuIcon(16)) SetMenuItemBitmaps(child, id, MF_BYCOMMAND, bitmap, bitmap);
                }
            }
            InsertMenuW(menu, index, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(child), L"Convertii");
            if (config.icons) {
                if (auto bitmap = MenuIcon(16)) {
                    for (UINT position = 0; position < static_cast<UINT>(GetMenuItemCount(menu)); ++position) {
                        wchar_t text[64]{};
                        GetMenuStringW(menu, position, text, ARRAYSIZE(text), MF_BYPOSITION);
                        if (wcscmp(text, L"Convertii") == 0) { SetMenuItemBitmaps(menu, position, MF_BYPOSITION, bitmap, bitmap); break; }
                    }
                }
            }
        } else {
            // Flat layout: the entries sit directly in the Explorer menu, the
            // way the classic 7-Zip menu inserts its own items.
            for (size_t position = 0; position < commands.size(); ++position) {
                auto id = first + static_cast<UINT>(position);
                InsertMenuW(menu, index + static_cast<UINT>(position), MF_BYPOSITION | MF_STRING, id, TitleFor(commands[position]));
                if (config.icons) {
                    if (auto bitmap = MenuIcon(16)) SetMenuItemBitmaps(menu, id, MF_BYCOMMAND, bitmap, bitmap);
                }
            }
        }
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, space);
    }
    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* info) override {
        if (!info || HIWORD(info->lpVerb)) return E_INVALIDARG;
        auto offset = LOWORD(info->lpVerb);
        if (offset >= static_cast<UINT>(commands.size())) return E_INVALIDARG;
        return Launch(current, commands[offset]);
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
