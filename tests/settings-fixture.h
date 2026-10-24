// Private Windhawk settings host. StringSetting uses these same ownership APIs.
// Outside a test session, retain WH_EDITING's original host stubs.
#include <windhawk_api.h>
#include <map>
#include <string>
#include <stdexcept>
namespace SettingsFixture {
inline const std::map<std::wstring,std::wstring>* values=nullptr;
inline std::map<std::wstring,unsigned> reads;
inline unsigned strings=0;
inline const std::wstring& Read(PCWSTR key) {
    auto found=values->find(key);
    if(found==values->end()) throw std::runtime_error("uncovered runtime setting");
    ++reads[key]; return found->second;
}
inline int Integer(PCWSTR key,...) {
    if(!values) return Wh_GetIntSetting(key);
    const auto& value=Read(key);
    return value==L"true" ? 1 : value==L"false" ? 0 : std::stoi(value);
}
inline PCWSTR String(PCWSTR key,...) {
    if(!values) return Wh_GetStringSetting(key);
    const auto& value=Read(key);
    auto copy=new wchar_t[value.size()+1];
    std::copy(value.begin(),value.end(),copy); copy[value.size()]=0; ++strings;
    return copy;
}
inline void Free(PCWSTR value) {
    if(!values) {Wh_FreeStringSetting(value); return;}
    delete[] value; --strings;
}
}
#define Wh_GetIntSetting SettingsFixture::Integer
#define Wh_GetStringSetting SettingsFixture::String
#define Wh_FreeStringSetting SettingsFixture::Free
// Parse the RAII wrapper with the substituted host APIs as well.
#include <windhawk_utils.h>
