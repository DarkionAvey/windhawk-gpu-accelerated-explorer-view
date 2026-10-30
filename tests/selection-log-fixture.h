// WH_EDITING normally discards host logs. Capture selection diagnostics and
// explicitly scoped frame summaries for the private native-I/O regression.
#include <windhawk_api.h>
#include <cstdarg>
#include <cwchar>
#include <string>
#include <vector>
#include <atomic>
namespace SelectionLogFixture {
inline thread_local std::vector<std::wstring>* sink=nullptr;
inline std::atomic<void(*)(PCWSTR)> frameSink{nullptr};
inline void Log(PCWSTR format,...) {
    const auto frames=frameSink.load(std::memory_order_acquire);
    const bool frame=frames && !std::wcsncmp(format,L"[GPU frames]",12);
    if(!frame && (!sink || std::wcsncmp(format,L"[GPU selection trace]",21))) return;
    std::vector<wchar_t> buffer(16384);
    va_list args; va_start(args,format);
    const int count=std::vswprintf(buffer.data(),buffer.size(),format,args);
    va_end(args);
    if(count>=0) {
        if(frame) frames(buffer.data());
        else sink->emplace_back(buffer.data(),size_t(count));
    }
}
struct Session {
    std::vector<std::wstring>* previous=sink;
    explicit Session(std::vector<std::wstring>& lines) {sink=&lines;}
    ~Session() {sink=previous;}
};
}
#undef Wh_Log
#define Wh_Log SelectionLogFixture::Log
