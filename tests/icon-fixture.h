// Substitute only explicit synthetic icon paths. Real EXE profiling and all
// other Shell requests use the system API; HICON decoding/rendering stay real.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <string>
#include <string_view>
#include <propvarutil.h>
#include <propkey.h>

// One explicitly enabled synthetic volume. Its Shell store deliberately has
// old free space; the volume query supplies a newer value and can be blocked.
// No real volume, notification broadcast or user file is modified.
namespace CapacityFixture {
inline std::atomic<bool> enabled{false};
inline std::atomic<unsigned> calls{0}, uiCalls{0};
inline std::atomic<ULONGLONG> freeBytes{600};
inline DWORD owner = 0;
inline HANDLE gate = nullptr;
inline bool Matches(PCWSTR path) { return enabled && path && wcscmp(path,L"Z:\\")==0; }
inline void Observe() { if (GetCurrentThreadId()==owner) ++uiCalls; }
inline HRESULT Store(REFIID iid,void** output) {
    Observe(); IPropertyStore* store=nullptr;
    HRESULT hr=PSCreateMemoryPropertyStore(IID_PPV_ARGS(&store));
    if (FAILED(hr)) return hr;
    PROPVARIANT value{};
    InitPropVariantFromUInt64(1000,&value); store->SetValue(PKEY_Capacity,value); PropVariantClear(&value);
    InitPropVariantFromUInt64(800,&value); store->SetValue(PKEY_FreeSpace,value); PropVariantClear(&value);
    InitPropVariantFromString(L"Synthetic volume",&value); store->SetValue(PKEY_ItemNameDisplay,value); PropVariantClear(&value);
    InitPropVariantFromString(L"Local Disk",&value); store->SetValue(PKEY_ItemTypeText,value); PropVariantClear(&value);
    hr=store->QueryInterface(iid,output); store->Release(); return hr;
}
inline UINT WINAPI DriveType(PCWSTR path) {
    if (!Matches(path)) return ::GetDriveTypeW(path);
    Observe(); return DRIVE_FIXED;
}
inline BOOL WINAPI Space(PCWSTR path,PULARGE_INTEGER available,PULARGE_INTEGER total,PULARGE_INTEGER remaining) {
    if (!Matches(path)) return ::GetDiskFreeSpaceExW(path,available,total,remaining);
    Observe(); ++calls;
    WaitForSingleObject(gate,INFINITE);
    available->QuadPart=remaining->QuadPart=freeBytes.load(); total->QuadPart=1000; return TRUE;
}
struct Session {
    Session() { gate=CreateEventW(nullptr,TRUE,TRUE,nullptr); owner=GetCurrentThreadId(); calls=uiCalls=0; freeBytes=600; enabled=true; }
    ~Session() { enabled=false; if(gate) { SetEvent(gate); CloseHandle(gate); gate=nullptr; } }
};
}
#define GetDriveTypeW CapacityFixture::DriveType
#define GetDiskFreeSpaceExW CapacityFixture::Space

namespace IconFixture {
inline HANDLE gate = nullptr;
inline std::atomic<unsigned> blocked{0};
inline std::atomic<unsigned> transientCalls{0}, permanentCalls{0};
inline std::atomic<bool> enabled{false}, blockAll{false};
inline DWORD_PTR WINAPI FileInfo(LPCWSTR path, DWORD attributes, SHFILEINFOW* info, UINT size, UINT flags) {
    const bool synthetic = !(flags & SHGFI_PIDL) && path && std::wstring_view(path).starts_with(L"wh-icon-test:");
    if (enabled.load() && (flags & SHGFI_ICON) &&
        (synthetic || ((flags & SHGFI_USEFILEATTRIBUTES) && path &&
            std::wstring_view(path).starts_with(L"C:\\wh_icon_probe")))) {
        const std::wstring_view name(path);
        if (synthetic && name.find(L"transient") != name.npos && ++transientCalls == 1) return 0;
        if (synthetic && name.find(L"permanent") != name.npos) { ++permanentCalls; return 0; }
        if (synthetic && (blockAll.load() || name.find(L"slow") != name.npos)) {
            ++blocked;
            WaitForSingleObject(gate, INFINITE);
            --blocked;
        }
        info->hIcon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        return info->hIcon ? 1 : 0;
    }
    return ::SHGetFileInfoW(path, attributes, info, size, flags);
}
struct Session {
    Session() {
        gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        enabled = true; blockAll = false; transientCalls = permanentCalls = 0;
    }
    ~Session() { enabled = false; if (gate) SetEvent(gate); }
};
inline HRESULT WINAPI CreateItem(PCWSTR path, IBindCtx* context, REFIID iid, void** output) {
    if (enabled.load() && path && std::wstring_view(path).starts_with(L"wh-icon-test:")) {
        *output = nullptr;
        return E_INVALIDARG;
    }
    return ::SHCreateItemFromParsingName(path, context, iid, output);
}
}
#define SHGetFileInfoW IconFixture::FileInfo
#define SHCreateItemFromParsingName IconFixture::CreateItem

// Delays only explicit synthetic property paths. Real stores and all COM,
// dispatch, generation checks and result delivery remain the production path.
namespace PropertyFixture {
inline HANDLE gate = nullptr;
inline std::atomic<unsigned> blocked{0};
inline std::atomic<bool> enabled{false};
inline HRESULT WINAPI Store(PCWSTR path, IBindCtx* context, GETPROPERTYSTOREFLAGS flags,
                            REFIID iid, void** output) {
    if (CapacityFixture::Matches(path)) return CapacityFixture::Store(iid,output);
    if (enabled.load() && path && std::wstring_view(path).starts_with(L"wh-property-test:")) {
        if (std::wstring_view(path).find(L"slow") != std::wstring_view::npos) {
            ++blocked; WaitForSingleObject(gate, INFINITE); --blocked;
        }
        *output = nullptr;
        return E_NOTIMPL;
    }
    return ::SHGetPropertyStoreFromParsingName(path, context, flags, iid, output);
}
struct Session {
    Session() { gate = CreateEventW(nullptr, TRUE, FALSE, nullptr); enabled = true; }
    ~Session() { enabled = false; if (gate) SetEvent(gate); }
};
}
#define SHGetPropertyStoreFromParsingName PropertyFixture::Store

namespace NamespaceFixture {
inline thread_local bool countBindings = false;
inline thread_local unsigned bindings = 0;
inline HRESULT WINAPI CreateItem(PCIDLIST_ABSOLUTE pidl, REFIID iid, void** output) {
    if (countBindings) ++bindings;
    return ::SHCreateItemFromIDList(pidl, iid, output);
}
}
#define SHCreateItemFromIDList NamespaceFixture::CreateItem
