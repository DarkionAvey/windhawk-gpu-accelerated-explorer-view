// WH_EDITING normally discards host logs. Capture selection diagnostics and
// explicitly scoped frame summaries for the private native-I/O regression.
#include <windhawk_api.h>
#include <cstdarg>
#include <cwchar>
#include <string>
#include <vector>
#include <atomic>
#include <array>
#include <type_traits>
namespace SelectionLogFixture {
inline thread_local std::vector<std::wstring>* sink=nullptr;
// Diagnostics may also capture every "[GPU selection" decision line.
inline thread_local bool allSelection=false;
inline std::atomic<void(*)(PCWSTR)> frameSink{nullptr};
// Format every line, as Windhawk does once the user enables logging, so a
// runtime formatting fault crashes the test with its option context.
inline std::atomic<bool> formatAll{false};
struct FormatAll {
    bool previous=formatAll.exchange(true);
    ~FormatAll() {formatAll=previous;}
};
inline void Log(PCWSTR format,...) {
    const auto frames=frameSink.load(std::memory_order_acquire);
    const bool frame=frames && !std::wcsncmp(format,L"[GPU frames]",12);
    const bool captured=frame || (sink &&
        !std::wcsncmp(format,allSelection ? L"[GPU selection" : L"[GPU selection trace]",allSelection ? 14 : 21));
    if(!captured && !formatAll.load(std::memory_order_relaxed)) return;
    std::vector<wchar_t> buffer(16384);
    va_list args; va_start(args,format);
    const int count=std::vswprintf(buffer.data(),buffer.size(),format,args);
    va_end(args);
    if(count>=0 && captured) {
        if(frame) frames(buffer.data());
        else sink->emplace_back(buffer.data(),size_t(count));
    }
}
struct Session {
    std::vector<std::wstring>* previous=sink;
    explicit Session(std::vector<std::wstring>& lines) {sink=&lines;}
    ~Session() {sink=previous;}
};

// Windhawk formats Wh_Log with Microsoft wide-printf rules once the user
// enables logging: %s/%ls is a wide string, %S/%hs narrow, %d/%u/%x/%c/%lu
// 32-bit, %zu/%Iu size_t and %lld/%llu/%I64 64-bit. A mismatch can crash
// Explorer only while logging is on, so every call is checked at compile time.
namespace Format {
enum class Kind { Integer32, Integer64, Floating, Wide, Narrow, Pointer, Other };
template<class T> consteval Kind Classify() {
    using U=std::remove_cv_t<T>;
    if constexpr (std::is_enum_v<U>) return sizeof(U)<=4 ? Kind::Integer32 : Kind::Integer64;
    else if constexpr (std::is_integral_v<U>) return sizeof(U)<=4 ? Kind::Integer32 : Kind::Integer64;
    else if constexpr (std::is_floating_point_v<U>) return Kind::Floating;
    else if constexpr (std::is_pointer_v<U> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<U>>,wchar_t>) return Kind::Wide;
    else if constexpr (std::is_pointer_v<U> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<U>>,char>) return Kind::Narrow;
    else if constexpr (std::is_pointer_v<U> || std::is_null_pointer_v<U>) return Kind::Pointer;
    else return Kind::Other;
}
void MismatchedSpecifier();  // Deliberately not constexpr: reaching it fails compilation.
template<size_t Count>
consteval void Validate(const wchar_t* text,const std::array<Kind,Count>& kinds) {
    size_t next=0;
    const auto take=[&](bool accepted) {if(next>=Count || !accepted) MismatchedSpecifier(); ++next;};
    for(const wchar_t* p=text; *p; ++p) {
        if(*p!=L'%') continue;
        if(*++p==L'%') continue;
        while(*p==L'-' || *p==L'+' || *p==L' ' || *p==L'#' || *p==L'0') ++p;
        if(*p==L'*') {take(next<Count && kinds[next]==Kind::Integer32); ++p;}
        while(*p>=L'0' && *p<=L'9') ++p;
        if(*p==L'.') {
            ++p;
            if(*p==L'*') {take(next<Count && kinds[next]==Kind::Integer32); ++p;}
            while(*p>=L'0' && *p<=L'9') ++p;
        }
        enum {None,Short,Long,LongLong,Size,Wide,LongDouble} length=None;
        if(*p==L'h') {length=Short; if(*++p==L'h') ++p;}
        else if(*p==L'l') {length=Long; if(*++p==L'l') {length=LongLong; ++p;}}
        else if(*p==L'w') {length=Wide; ++p;}
        else if(*p==L'z' || *p==L't') {length=Size; ++p;}
        else if(*p==L'j') {length=LongLong; ++p;}
        else if(*p==L'L') {length=LongDouble; ++p;}
        else if(*p==L'I') {
            if(p[1]==L'6' && p[2]==L'4') {length=LongLong; p+=3;}
            else if(p[1]==L'3' && p[2]==L'2') {length=Long; p+=3;}
            else {length=Size; ++p;}
        }
        const Kind kind=next<Count ? kinds[next] : Kind::Other;
        switch(*p) {
        case L'd': case L'i': case L'u': case L'o': case L'x': case L'X': case L'c': case L'C':
            take(kind==(length==LongLong || length==Size ? Kind::Integer64 : Kind::Integer32)); break;
        case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': case L'a': case L'A':
            take(kind==Kind::Floating); break;
        case L's': take(kind==(length==Short ? Kind::Narrow : Kind::Wide)); break;
        case L'S': take(kind==(length==Long || length==Wide ? Kind::Wide : Kind::Narrow)); break;
        case L'p': take(kind==Kind::Pointer || kind==Kind::Wide || kind==Kind::Narrow); break;
        default: MismatchedSpecifier();  // %n, unknown or truncated specifiers.
        }
    }
    if(next!=Count) MismatchedSpecifier();  // More arguments than specifiers.
}
template<class... Args> struct Checked {
    const wchar_t* text;
    template<size_t N> consteval Checked(const wchar_t (&format)[N]) : text(format) {
        Validate(format,std::array<Kind,sizeof...(Args)>{Classify<std::decay_t<Args>>()...});
    }
};
template<class... Args> void Log(Checked<std::type_identity_t<Args>...> format, Args... args) {
    SelectionLogFixture::Log(format.text,args...);
}
}
}
#undef Wh_Log
#define Wh_Log SelectionLogFixture::Format::Log
