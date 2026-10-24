// Synthetic English HWND fixtures must not acquire the desktop's UI language.
// Native window styles and real USER mirroring remain unmodified. Tests can
// explicitly switch the fallback without changing the user's Windows settings.
#include <windows.h>
namespace UiLocaleFixture {
inline thread_local LANGID language=MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);
inline LANGID WINAPI Language() { return language; }
}
#define GetUserDefaultUILanguage UiLocaleFixture::Language
