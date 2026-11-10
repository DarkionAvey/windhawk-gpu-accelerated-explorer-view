// Capture this thread's actual layered-window submission, without desktop readback.
#include <windows.h>
#include <vector>
#include <algorithm>
namespace LayeredPixelsFixture {
inline thread_local bool enabled=false;
inline thread_local SIZE size{};
inline thread_local std::vector<DWORD> pixels;
inline BOOL WINAPI Update(HWND hwnd,HDC screen,POINT* position,SIZE* extent,HDC source,
        POINT* origin,COLORREF key,BLENDFUNCTION* blend,DWORD flags) {
    if(!enabled) return ::UpdateLayeredWindow(hwnd,screen,position,extent,source,origin,key,blend,flags);
    DIBSECTION bitmap{};
    if(!extent || !source || GetObjectW(GetCurrentObject(source,OBJ_BITMAP),sizeof(bitmap),&bitmap)!=sizeof(bitmap) ||
        !bitmap.dsBm.bmBits || bitmap.dsBm.bmBitsPixel!=32) return FALSE;
    size=*extent; pixels.resize(size_t(size.cx)*size.cy);
    // Reading DIB memory requires completion of this thread's GDI batch.
    GdiFlush();
    const auto* bits=static_cast<const BYTE*>(bitmap.dsBm.bmBits);
    for(int y=0;y<size.cy;++y) {
        // RenderItemTip submits a top-down DIB. GetObject reports its height
        // as positive even for a top-down bitmap; that is not its memory order.
        const int row=y;
        const auto* input=reinterpret_cast<const DWORD*>(bits+row*bitmap.dsBm.bmWidthBytes);
        std::copy_n(input,size.cx,pixels.data()+size_t(y)*size.cx);
    }
    return TRUE;
}
struct Session {
    Session() {enabled=true; pixels.clear(); size={};}
    ~Session() {enabled=false; pixels.clear();}
};
}
#define UpdateLayeredWindow LayeredPixelsFixture::Update
