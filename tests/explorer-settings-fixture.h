// Tests never depend on the user's Explorer View settings. Compact view reads
// a per-thread fixture value (off unless a test sets it); every other Shell
// registry read goes to Windows unchanged.
#include <windows.h>
#include <shlwapi.h>
namespace ExplorerSettingsFixture {
inline thread_local bool compact=false;
inline LSTATUS WINAPI GetUSValue(LPCWSTR key,LPCWSTR value,DWORD* type,void* data,DWORD* bytes,
                                 BOOL ignoreHkcu,void* fallback,DWORD fallbackBytes) {
    if(value && _wcsicmp(value,L"UseCompactMode")==0) {
        if(!bytes || *bytes<sizeof(DWORD) || !data) return ERROR_MORE_DATA;
        *static_cast<DWORD*>(data)=compact ? 1u : 0u; *bytes=sizeof(DWORD);
        if(type) *type=REG_DWORD;
        return ERROR_SUCCESS;
    }
    return ::SHRegGetUSValueW(key,value,type,data,bytes,ignoreHkcu,fallback,fallbackBytes);
}
}
#define SHRegGetUSValueW ExplorerSettingsFixture::GetUSValue
