#include <thread>
// Read-only Windows integration: the selection accent is the accent Windows
// shows in Settings (UISettings UIColorType_Accent), including Automatic
// accents. DWM colorization is the title-bar color and can differ (darkened
// for automatic accents, or stale). No system setting is changed.
static bool WindowsAccentForTest(COLORREF& accent, HRESULT* status = nullptr) {
    using namespace ABI::Windows::UI::ViewManagement;
    constexpr wchar_t name[] = L"Windows.UI.ViewManagement.UISettings";
    HSTRING_HEADER header{}; HSTRING className = nullptr;
    ComPtr<IInspectable> instance; ComPtr<IUISettings3> colors;
    ABI::Windows::UI::Color value{};
    HRESULT hr = WindowsCreateStringReference(name, ARRAYSIZE(name)-1, &header, &className);
    if (SUCCEEDED(hr)) hr = ::RoActivateInstance(className, &instance);
    if (SUCCEEDED(hr)) hr = instance.As(&colors);
    if (SUCCEEDED(hr)) hr = colors->GetColorValue(UIColorType_Accent, &value);
    if (status) *status = hr;
    if (FAILED(hr) || value.A != 255) return false;
    accent = RGB(value.R, value.G, value.B);
    return true;
}

static void TestWindowsAccent() {
    COLORREF expected = CLR_INVALID;
    Check(WindowsAccentForTest(expected), "Windows UISettings accent is unavailable");
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) expected = ::GetSysColor(COLOR_HIGHLIGHT);
    DWORD dwm = 0; BOOL opaque = FALSE;
    const bool haveDwm = SUCCEEDED(::DwmGetColorizationColor(&dwm, &opaque)) && (dwm >> 24);
    const COLORREF title = RGB((dwm>>16)&255, (dwm>>8)&255, dwm&255);

    const bool dynamic = g_dynamicAccent.exchange(true);
    const auto selection = g_selectionRgb.load(), known = g_systemAccent.load();
    const COLORREF sentinel = RGB(1,2,3);
    // UI-thread path: a fresh process with no accent read yet.
    g_systemAccent = CLR_INVALID; g_selectionRgb = sentinel;
    RefreshThemeColor();
    Check(g_selectionRgb == expected, "selection accent differs from the Windows accent");
    // Settings-load path on a thread with no COM/WinRT initialization: the
    // accent comes from UISettings when Windows allows activation there, and
    // otherwise from the last real accent, never from DWM colorization.
    g_selectionRgb = sentinel;
    HRESULT threadActivation = E_FAIL;
    std::thread raw([&] {
        COLORREF ignored = CLR_INVALID;
        WindowsAccentForTest(ignored, &threadActivation);
        RefreshThemeColor();
    });
    raw.join();
    Check(g_selectionRgb == expected, "settings thread replaced the Windows accent");

    g_dynamicAccent = dynamic; g_selectionRgb = selection; g_systemAccent = known;
    const auto hex = [](COLORREF c) {
        char text[8]; snprintf(text, sizeof(text), "%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        return std::string(text);
    };
    std::cout << "PASS: selection follows the Windows accent #" << hex(expected) <<
        " on the UI and settings paths (DWM colorization " << (haveDwm ? "#" + hex(title) : std::string("n/a")) <<
        (haveDwm && title != expected ? ", differs: discriminating" : ", same: not discriminating on this machine") <<
        "; raw-thread UISettings " << (SUCCEEDED(threadActivation) ? "available" : "unavailable, cached accent used") << ")\n";
}
