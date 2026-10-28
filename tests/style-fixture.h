// Faults are thread-local and opt-in. Never alter Windows settings or Explorer.
#include <windows.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vssym32.h>
#include <dwmapi.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <map>
#include <tuple>
#include <string>
#include <cstring>
namespace StyleFixture {
inline thread_local bool enabled=false, highContrast=false, failApp=false, failSystem=false;
inline thread_local UINT dpi=96, wheel=3;
inline thread_local unsigned activations=0, fontQueries=0, fillQueries=0;
inline thread_local unsigned themeQueries=0, systemColorQueries=0, accentQueries=0;
inline thread_local unsigned visibilityQueries=0;
inline thread_local bool showHidden=false, showProtected=false;
inline thread_local bool singleClick=false;
inline thread_local DWORD iconUnderline=2;
inline thread_local std::wstring browserUnderline=L"yes";
inline thread_local unsigned clickQueries=0;
inline void WINAPI ShellSettings(LPSHELLSTATE value,DWORD mask,BOOL set) {
    if(!enabled || set) {::SHGetSetSettings(value,mask,set); return;}
    if(mask & (SSF_SHOWALLOBJECTS|SSF_SHOWSUPERHIDDEN)) ++visibilityQueries;
    if(mask & SSF_DOUBLECLICKINWEBVIEW) {++clickQueries; value->fDoubleClickInWebView=singleClick ? 0:-1;}
    if(mask & SSF_SHOWALLOBJECTS) value->fShowAllObjects=showHidden;
    if(mask & SSF_SHOWSUPERHIDDEN) value->fShowSuperHidden=showProtected;
}
inline LONG WINAPI ShellRegistry(LPCWSTR path,LPCWSTR name,LPDWORD type,void* value,
        LPDWORD bytes,BOOL ignore,void* fallback,DWORD fallbackBytes) {
    if(!enabled) return ::SHRegGetUSValueW(path,name,type,value,bytes,ignore,fallback,fallbackBytes);
    const void* source=nullptr; DWORD size=0,kind=0;
    if(wcscmp(name,L"IconUnderline")==0) {source=&iconUnderline; size=sizeof(iconUnderline); kind=REG_DWORD;}
    else if(wcscmp(name,L"Anchor Underline")==0) {source=browserUnderline.c_str(); size=DWORD((browserUnderline.size()+1)*sizeof(wchar_t)); kind=REG_SZ;}
    else return ::SHRegGetUSValueW(path,name,type,value,bytes,ignore,fallback,fallbackBytes);
    if(type) *type=kind;
    const auto capacity=*bytes; *bytes=size;
    if(capacity<size) return ERROR_MORE_DATA;
    memcpy(value,source,size); return ERROR_SUCCESS;
}
inline thread_local HRESULT accentStatus=S_OK;
inline thread_local DWORD accent=0xFF0078D7;
inline thread_local COLORREF window=RGB(255,255,255), text=RGB(0,0,0);
inline thread_local ABI::Windows::UI::Color appBg{255,24,24,24}, appFg{255,240,240,240};
inline thread_local LOGFONTW themeFont{}, systemFont{};
inline thread_local std::map<std::tuple<int,int,int,int>,COLORREF> colors;
inline HTHEME Theme(int id) {return reinterpret_cast<HTHEME>(INT_PTR(id));}
inline HTHEME WINAPI Open(HWND hwnd,LPCWSTR name) {
    if(!enabled) return ::OpenThemeData(hwnd,name);
    return Theme(wcscmp(name,L"Header")==0 ? 3 : 2);
}
inline HTHEME WINAPI Attached(HWND hwnd) {return enabled ? Theme(1) : ::GetWindowTheme(hwnd);}
inline HRESULT WINAPI Close(HTHEME theme) {return enabled ? S_OK : ::CloseThemeData(theme);}
inline HRESULT WINAPI Color(HTHEME theme,int part,int state,int property,COLORREF* value) {
    if(!enabled) return ::GetThemeColor(theme,part,state,property,value);
    ++themeQueries;
    if(property==TMT_FILLCOLOR) ++fillQueries;
    const auto found=colors.find({int(INT_PTR(theme)),part,state,property});
    // Deliberately write on failure, to catch callers that keep failed outputs.
    *value=found==colors.end() ? RGB(0,0,0) : found->second;
    return found==colors.end() ? E_FAIL : S_OK;
}
inline HRESULT WINAPI Font(HTHEME theme,HDC dc,int part,int state,int property,LOGFONTW* value) {
    if(!enabled) return ::GetThemeFont(theme,dc,part,state,property,value);
    ++fontQueries; *value=themeFont; return S_OK;
}
inline COLORREF WINAPI SystemColor(int index) {
    if(!enabled) return ::GetSysColor(index);
    ++systemColorQueries;
    return index==COLOR_WINDOW || index==COLOR_BTNFACE ? window :
        index==COLOR_HIGHLIGHT ? RGB(12,110,210) : text;
}
inline UINT WINAPI Dpi(HWND hwnd) {return enabled ? dpi : ::GetDpiForWindow(hwnd);}
inline BOOL Parameters(UINT action,void* value) {
    if(action==SPI_GETHIGHCONTRAST) {
        static_cast<HIGHCONTRASTW*>(value)->dwFlags=highContrast ? HCF_HIGHCONTRASTON : 0;
    } else if(action==SPI_GETNONCLIENTMETRICS) {
        static_cast<NONCLIENTMETRICSW*>(value)->lfMessageFont=systemFont;
    } else if(action==SPI_GETWHEELSCROLLLINES) *static_cast<UINT*>(value)=wheel;
    else return FALSE;
    return !failSystem;
}
inline BOOL WINAPI System(UINT action,UINT param,void* value,UINT flags) {
    return enabled && (action==SPI_GETHIGHCONTRAST || action==SPI_GETNONCLIENTMETRICS ||
        action==SPI_GETWHEELSCROLLLINES) ? Parameters(action,value) :
        ::SystemParametersInfoW(action,param,value,flags);
}
inline BOOL WINAPI SystemDpi(UINT action,UINT param,void* value,UINT flags,UINT density) {
    return enabled && action==SPI_GETNONCLIENTMETRICS ? Parameters(action,value) :
        ::SystemParametersInfoForDpi(action,param,value,flags,density);
}
inline HRESULT WINAPI Accent(DWORD* value,BOOL* opaque) {
    if(!enabled) return ::DwmGetColorizationColor(value,opaque);
    ++accentQueries;
    *value=accent; *opaque=TRUE; return accentStatus;
}
struct AppColors final : ABI::Windows::UI::ViewManagement::IUISettings3 {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override {
        *value=nullptr;
        if(iid!=__uuidof(IUnknown) && iid!=__uuidof(IInspectable) &&
            iid!=__uuidof(ABI::Windows::UI::ViewManagement::IUISettings3)) return E_NOINTERFACE;
        *value=static_cast<ABI::Windows::UI::ViewManagement::IUISettings3*>(this); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return 2;}
    ULONG STDMETHODCALLTYPE Release() override {return 1;}
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*,IID**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetColorValue(ABI::Windows::UI::ViewManagement::UIColorType type,
            ABI::Windows::UI::Color* value) override {
        *value=type==ABI::Windows::UI::ViewManagement::UIColorType_Background ? appBg : appFg;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_ColorValuesChanged(
            ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::ViewManagement::UISettings*,IInspectable*>*,
            EventRegistrationToken*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE remove_ColorValuesChanged(EventRegistrationToken) override {return E_NOTIMPL;}
};
inline HRESULT WINAPI Activate(HSTRING name,IInspectable** value) {
    if(!enabled) return ::RoActivateInstance(name,value);
    ++activations; *value=nullptr;
    if(failApp) return E_FAIL;
    static AppColors instance; *value=&instance; return S_OK;
}
struct Session {
    Session() {enabled=true; highContrast=failApp=failSystem=false; dpi=96; wheel=3;
        activations=fontQueries=fillQueries=0; accentStatus=S_OK; accent=0xFF0078D7;
        themeQueries=systemColorQueries=accentQueries=0;
        visibilityQueries=clickQueries=0; showHidden=showProtected=singleClick=false;
        iconUnderline=2; browserUnderline=L"yes";
        window=RGB(255,255,255); text=RGB(0,0,0);
        appBg={255,24,24,24}; appFg={255,240,240,240};
        themeFont={}; systemFont={}; colors.clear();}
    ~Session() {enabled=false; colors.clear();}
};
}
#define OpenThemeData StyleFixture::Open
#define GetWindowTheme StyleFixture::Attached
#define CloseThemeData StyleFixture::Close
#define GetThemeColor StyleFixture::Color
#define GetThemeFont StyleFixture::Font
#define GetSysColor StyleFixture::SystemColor
#define GetDpiForWindow StyleFixture::Dpi
#define SystemParametersInfoW StyleFixture::System
#define SystemParametersInfoForDpi StyleFixture::SystemDpi
#define DwmGetColorizationColor StyleFixture::Accent
#define RoActivateInstance StyleFixture::Activate
#define SHGetSetSettings StyleFixture::ShellSettings
#define SHRegGetUSValueW StyleFixture::ShellRegistry
