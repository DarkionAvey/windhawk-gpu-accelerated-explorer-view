// WH_EDITING normally discards host logs. Capture only the explicit selection
// diagnostic prefix so component tests can inspect/print real formatted records.
#include <windhawk_api.h>
#include <cstdarg>
#include <cwchar>
#include <string>
#include <vector>
namespace SelectionLogFixture {
inline thread_local std::vector<std::wstring>* sink=nullptr;
inline void Log(PCWSTR format,...) {
    if(!sink || std::wcsncmp(format,L"[GPU selection trace]",21)) return;
    std::vector<wchar_t> buffer(16384);
    va_list args; va_start(args,format);
    const int count=std::vswprintf(buffer.data(),buffer.size(),format,args);
    va_end(args);
    if(count>=0) sink->emplace_back(buffer.data(),size_t(count));
}
struct Session {
    std::vector<std::wstring>* previous=sink;
    explicit Session(std::vector<std::wstring>& lines) {sink=&lines;}
    ~Session() {sink=previous;}
};
}
#undef Wh_Log
#define Wh_Log SelectionLogFixture::Log
