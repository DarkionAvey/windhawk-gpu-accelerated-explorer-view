// Regressions for the 1.0.14 review fixes (M6, M8, M9). Private windows and
// real USER/GDI/NLS behavior only; no fabricated input or messages.

// M6: a rename-field snapshot is put back only at the scale it was taken.
static void TestRenameUnderlayScale(const std::wstring&) {
    ViewState state;
    state.frameHwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"underlay frame",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 320, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.frameHwnd != nullptr, "private frame");
    struct Cleanup { HWND hwnd; ~Cleanup() { DestroyWindow(hwnd); } } cleanup{state.frameHwnd};
    auto& r = state.rename;
    const auto arm = [&](int dpi) {
        ReleaseRenameUnderlay(r);
        const HDC screen = GetDC(nullptr);
        r.underlay = CreateCompatibleBitmap(screen, 40, 20);
        ReleaseDC(nullptr, screen);
        Check(r.underlay != nullptr, "snapshot bitmap");
        GetClientRect(state.frameHwnd, &r.underlayFrame);
        r.underlayRect = {0, 0, 40, 20};
        r.underlayDpi = dpi;
    };
    const RECT vacated{0, 0, 40, 20};
    arm(DpiOf(state.frameHwnd) + 48);
    RestoreRenameUnderlay(state, vacated);
    Check(!r.underlay && !r.underlayDpi, "a snapshot from another scale was kept for the vacated field");
    arm(DpiOf(state.frameHwnd));
    RestoreRenameUnderlay(state, vacated);
    Check(r.underlay != nullptr, "a current snapshot was discarded");
    ReleaseRenameUnderlay(r);
    std::cout << "PASS: rename underlay restores only at its own frame scale\n";
}

// M8: DefView's label editor is found whether or not it has keyboard focus.
static void TestNativeRenameEditorDiscovery(const std::wstring&) {
    ViewState state;
    state.nativeHwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"native view",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 320, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.nativeHwnd != nullptr, "private native view");
    struct Cleanup { HWND hwnd; ~Cleanup() { DestroyWindow(hwnd); } } cleanup{state.nativeHwnd};
    Check(!FindNativeRenameEditor(state), "a view without an editor reported one");
    HWND hidden = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 100, 20, state.nativeHwnd,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hidden && !FindNativeRenameEditor(state), "a hidden EDIT was taken for DefView's editor");
    HWND editor = CreateWindowExW(0, L"EDIT", L"name", WS_CHILD | WS_VISIBLE, 0, 30, 100, 20,
        state.nativeHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(editor != nullptr, "private editor");
    Check(GetFocus() != editor, "PRECONDITION: the editor has no keyboard focus");
    Check(FindNativeRenameEditor(state) == editor, "an unfocused DefView editor was not found");
    state.rename.editor = editor; // The mod's own field is never DefView's editor.
    Check(!FindNativeRenameEditor(state), "the GPU rename field was taken for DefView's editor");
    std::cout << "PASS: DefView's label editor is found without focus; hidden and own editors are not\n";
}

// 1.1.1 review: the undocumented shell32 template is filled without
// FormatMessageW; unexpected inserts give no notice instead of reading memory.
static void TestRenameNoticeTemplate(const std::wstring&) {
    const std::wstring cause = L"Access is denied.", name = L"Report (1).txt";
    const auto fill = [&](std::wstring_view format) { return SubstituteRenameNotice(format, cause, name); };
    Check(fill(L"Cannot rename %2!ls!: %1!ls!") == L"Cannot rename Report (1).txt: Access is denied.",
        "the current shell32 template was not filled");
    Check(fill(L"%1 / %2!s! / %2!ws! 100%%") == L"Access is denied. / Report (1).txt / Report (1).txt 100%",
        "plain and string-typed inserts or %% were not filled");
    for (const wchar_t* hostile : {L"Cannot rename %2: %1 %3", L"%2!d!: %1", L"Cannot rename %2", L"%1 only",
                                   L"%1 %2 %12", L"%1 %2!ls", L"%1 %2 %", L"%1 %2 %n", L"%0 %1 %2", L"no inserts"})
        Check(fill(hostile).empty(), "an unexpected rename-notice template produced text");
    std::cout << "PASS: rename notice fills only %1/%2 string inserts and %%, rejecting every other template\n";
}

// 1.1.1 review: the GetDC/ReleaseDC and New-menu hooks are counted by the
// unload drain for exactly the duration of their call into the original.
namespace HookDrainProbe {
inline unsigned seen = 0;
inline unsigned Active(ApiHook hook) { return g_apiCallbacks[static_cast<size_t>(hook)].load(); }
}
static void TestHookUnloadDrain(const std::wstring&) {
    const auto getDc = std::exchange(g_getDcOriginal, +[](HWND hwnd) -> HDC {
        HookDrainProbe::seen = HookDrainProbe::Active(ApiHook::GetDC); return ::GetDC(hwnd); });
    const auto releaseDc = std::exchange(g_releaseDcOriginal, +[](HWND hwnd, HDC dc) -> int {
        HookDrainProbe::seen = HookDrainProbe::Active(ApiHook::ReleaseDC); return ::ReleaseDC(hwnd, dc); });
    const auto invoke = std::exchange(g_newMenuInvokeOriginal,
        +[](IContextMenu*, LPCMINVOKECOMMANDINFO) -> HRESULT {
            HookDrainProbe::seen = HookDrainProbe::Active(ApiHook::NewMenuInvoke); return S_OK; });
    struct Restore {
        decltype(getDc) a; decltype(releaseDc) b; decltype(invoke) c;
        ~Restore() { g_getDcOriginal = a; g_releaseDcOriginal = b; g_newMenuInvokeOriginal = c; }
    } restore{getDc, releaseDc, invoke};
    HookDrainProbe::seen = 0;
    const HDC dc = GetDcHook(nullptr);
    Check(dc && HookDrainProbe::seen == 1 && !HookDrainProbe::Active(ApiHook::GetDC),
        "GetDC hook was not counted by the unload drain for its call");
    HookDrainProbe::seen = 0;
    Check(ReleaseDcHook(nullptr, dc) == 1 && HookDrainProbe::seen == 1 && !HookDrainProbe::Active(ApiHook::ReleaseDC),
        "ReleaseDC hook was not counted by the unload drain for its call");
    HookDrainProbe::seen = 0;
    Check(SUCCEEDED(NewMenuInvokeHook(nullptr, nullptr)) && HookDrainProbe::seen == 1 &&
        !HookDrainProbe::Active(ApiHook::NewMenuInvoke),
        "New menu hook was not counted by the unload drain for its call");
    std::cout << "PASS: GetDC, ReleaseDC and New-menu hooks are held by the unload drain while they run\n";
}

// M9: highlight ranges are computed once per label and follow the query.
static void TestSearchHighlightCache(const std::wstring&) {
    ViewState state;
    const std::wstring name = L"Café report cafe.txt";
    const auto ranges = [&] {
        std::vector<DWRITE_TEXT_RANGE> out;
        CachedSearchHighlightRanges(state, name, out);
        return out;
    };
    const auto independent = [&](const std::vector<std::wstring>& terms) {
        std::vector<DWRITE_TEXT_RANGE> out;
        for (const auto& term : terms)
            for (int from = 0; from < int(name.size());) {
                int found = 0;
                const int at = FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,
                    FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC,
                    name.c_str() + from, int(name.size()) - from, term.c_str(), int(term.size()),
                    &found, nullptr, nullptr, 0);
                if (at < 0 || found <= 0) break;
                out.push_back({UINT32(from + at), UINT32(found)});
                from += at + found;
            }
        return out;
    };
    const auto same = [](const std::vector<DWRITE_TEXT_RANGE>& a, const std::vector<DWRITE_TEXT_RANGE>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
            return x.startPosition == y.startPosition && x.length == y.length; });
    };
    Check(ranges().empty() && state.searchHighlightCache.empty(), "a folder without a query cached highlights");
    state.searchHighlightTerms = {L"cafe"};
    const auto first = ranges();
    Check(first.size() == 2 && same(first, independent(state.searchHighlightTerms)),
        "cached ranges differ from Windows' linguistic search");
    Check(same(ranges(), first) && state.searchHighlightCache.size() == 1, "a repeated paint searched again");
    state.searchHighlightTerms = {L"report"};
    Check(same(ranges(), independent(state.searchHighlightTerms)) && state.searchHighlightCache.size() == 1,
        "a new query reused the previous query's ranges");
    for (int i = 0; i < 5000; ++i) {
        std::vector<DWRITE_TEXT_RANGE> out;
        CachedSearchHighlightRanges(state, L"report " + std::to_wstring(i), out);
    }
    Check(state.searchHighlightCache.size() <= 4096, "highlight cache grew without bound");
    std::cout << "PASS: search highlights resolved once per label, per query, within a bounded cache\n";
}
