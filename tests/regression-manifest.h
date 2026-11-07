// The single list of regressions. Each entry runs in its own process; see
// tests/README.md "Writing a regression" and regression-registry.h.
//
//   REGRESSION("kebab-name", PARALLEL, TestSomething(fixture))
//   REGRESSION("kebab-name", SERIAL(Regression::QuietCpu), TestBenchmark(fixture))
//   FOCUSED(...)  same, but only run on request (diagnostic probes).
//
// `fixture` is this process's private directory of file0.txt..file511.txt.
// PARALLEL is the expected declaration. SERIAL is accepted only when the test
// really uses a listed machine-wide resource; the harness verifies the claim.
#pragma once
#define PARALLEL 0u
#define SERIAL(resources) (static_cast<unsigned>(resources))
#define REGRESSION(name, concurrency, ...) {L"" name, concurrency, Regression::Gate::Release, \
    [](const std::wstring& fixture) { (void)fixture; __VA_ARGS__; }}
#define FOCUSED(name, concurrency, ...) {L"" name, concurrency, Regression::Gate::Focused, \
    [](const std::wstring& fixture) { (void)fixture; __VA_ARGS__; }}

namespace RegressionManifest {
// Private offscreen frame for probes that draw into a caller-owned window.
template<class Body> void WithFrame(const wchar_t* title, int width, int height, Body body) {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"probe fonts");
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",title,
        WS_POPUP,-32000,-32000,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame!=nullptr,"probe frame");
    body(frame);
    DestroyWindow(frame); g_writeFactory.Reset();
}
inline void FilesystemScope() {
    // These are identities only; no real volume/share is accessed.
    Check(IsTargetPath(L"Q:\\") && IsTargetPath(L"q:/ordinary/folder") &&
          IsTargetPath(L"Q:\\Windows.old") && IsTargetPath(L"\\\\scope-fixture\\share\\folder") &&
          !IsTargetPath(L"Q:relative") && !IsTargetPath(L"relative\\folder") &&
          !IsTargetPath(L"shell:unsupported") && !IsTargetPath(L""),
          "all-folder filesystem scope rejected a drive/share or accepted a non-filesystem identity");
    Check(IsSupportedLocation(kThisPcLocation) && IsSupportedLocation(kRecycleBinLocation) &&
          !IsSupportedLocation(kUnsupportedShellLocation),"filesystem scope changed supported namespace fallbacks");
    std::cout<<"PASS: filesystem scope accepts drive/share identities and rejects relative/unsupported locations\n";
}
inline void LiveSort(const std::wstring& fixture) {
    HWND frame=CreateWindowExW(0,L"STATIC",L"hidden-live-sort-frame",0,
        0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame!=nullptr,"live sort frame");
    TestLiveSorting(frame,fixture);
    DestroyWindow(frame);
}
inline void RowComposition() {
    WorkerComApartment apartment;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"row compositor fonts");
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private row fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,800,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame!=nullptr,"row compositor frame");
    TestRowComposition<ViewState>(frame); DestroyWindow(frame); g_writeFactory.Reset();
}
inline void MarqueeSelection(const std::wstring& fixture) {
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    cls.lpszClassName=L"CabinetWClass"; RegisterClassW(&cls);
    TestMarqueeSelection(fixture);
}
// Every schema option names a release test that asserts its effect.
void CheckOptionCoverage();
}

static const Regression::Test kRegressions[] = {
    // Foundation and option bindings.
    REGRESSION("filesystem-scope", PARALLEL, RegressionManifest::FilesystemScope()),
    REGRESSION("llm-report", PARALLEL, TestLlmReport()),
    REGRESSION("render-quality", PARALLEL, TestRenderQuality()),
    REGRESSION("toggle-repaint", PARALLEL, TestToggleRepaint()),
    REGRESSION("pixel-snapping", PARALLEL, TestPixelSnapping()),
    REGRESSION("lifetime-races", PARALLEL, TestLifetimeRaces()),
    REGRESSION("dispatch-state-retirement", PARALLEL, TestDispatchStateRetirement()),
    REGRESSION("frame-hook-release", PARALLEL, TestFrameHookRelease()),
    REGRESSION("status-button-layout", PARALLEL, TestStatusButtonLayout()),
    REGRESSION("user-options", PARALLEL, TestUserOptions()),
    REGRESSION("option-coverage", PARALLEL, RegressionManifest::CheckOptionCoverage()),
    REGRESSION("option-effects-list", PARALLEL, OptionEffects::TestListLayouts(fixture)),
    REGRESSION("option-effects-grid", PARALLEL, OptionEffects::TestGridLayouts(fixture)),
    REGRESSION("option-effects-zoom", PARALLEL, OptionEffects::TestZoomInputs(fixture)),
    REGRESSION("option-effects-appearance", PARALLEL, OptionEffects::TestAppearance(fixture)),
    REGRESSION("option-live-sweep-details", PARALLEL,
        OptionEffects::TestLiveSweep(fixture,{{FVM_DETAILS,16}},L"details",true)),
    REGRESSION("option-live-sweep-list", PARALLEL, OptionEffects::TestLiveSweep(fixture,{{FVM_LIST,16}},L"list",false)),
    REGRESSION("option-live-sweep-icons", PARALLEL,
        OptionEffects::TestLiveSweep(fixture,{{FVM_SMALLICON,16},{FVM_ICON,48},{FVM_ICON,96}},L"icons",true)),
    // Touch, gestures and drag.
    REGRESSION("touch-input", PARALLEL, TestTouchInput(fixture)),
    REGRESSION("drag-preview", PARALLEL, TestDragPreview(fixture)),
    REGRESSION("overlay-stack-filter", PARALLEL, TestOverlayStackFilter()),
    REGRESSION("compositor-wait-errors", PARALLEL, TestCompositorWaitErrors()),
    REGRESSION("empty-frame-summary", PARALLEL, TestEmptyFrameSummary()),
    REGRESSION("drag-count-locale", PARALLEL, TestDragCountLocale()),
    REGRESSION("drag-preview-lifecycle", PARALLEL, TestDragPreviewLifecycle(fixture)),
    REGRESSION("additional-gestures", PARALLEL, TestAdditionalGestures(fixture)),
    REGRESSION("selection-checkboxes", PARALLEL, TestSelectionCheckboxes(fixture)),
    // Styles, refresh, columns and native views.
    REGRESSION("resolved-styles", PARALLEL, TestResolvedStyles()),
    REGRESSION("style-events", PARALLEL, TestStyleEvents(fixture)),
    REGRESSION("periodic-capacity-refresh", PARALLEL, TestPeriodicCapacityRefresh()),
    REGRESSION("tab-resume-refresh", PARALLEL, TestTabResumeRefresh(fixture)),
    REGRESSION("window-resume-refresh", PARALLEL, TestWindowResumeRefresh()),
    REGRESSION("unchanged-refresh-presentation", PARALLEL, TestUnchangedRefreshPresentation()),
    REGRESSION("clipping-preflight", PARALLEL, TestClippingPreflight()),
    REGRESSION("resize-auto-refit", PARALLEL, TestResizeAutoRefit()),
    REGRESSION("column-minimum-widths", PARALLEL, TestColumnMinimumWidths()),
    REGRESSION("office-lock-filtering", PARALLEL, TestOfficeLockFiltering(fixture)),
    REGRESSION("empty-snapshot-reconciliation", PARALLEL, TestEmptySnapshotReconciliation(fixture)),
    REGRESSION("namespace-item-mutations", PARALLEL, TestNamespaceItemMutations(fixture)),
    REGRESSION("native-suppression", PARALLEL, TestNativeSuppression()),
    REGRESSION("explorer-column-bridge", PARALLEL, TestExplorerColumnBridge(fixture)),
    // Workers, images and frame production.
    REGRESSION("publication-and-epochs", PARALLEL, TestPublicationAndEpochs()),
    REGRESSION("worker-apartment", PARALLEL, TestWorkerApartment()),
    REGRESSION("worker-startup-isolation", PARALLEL, TestWorkerStartupIsolation()),
    REGRESSION("thirty-independent-tabs", PARALLEL, TestThirtyIndependentTabs(fixture)),
    REGRESSION("cold-column-catalog", PARALLEL, TestColdColumnCatalog(fixture)),
    REGRESSION("deferred-column-capture", PARALLEL, TestDeferredColumnCapture(fixture)),
    REGRESSION("snapshot-selection-restoration", PARALLEL, TestSnapshotSelectionRestoration(fixture)),
    REGRESSION("executable-icons", PARALLEL, ProfileExecutableIcons()),
    REGRESSION("icon-queue-isolation", PARALLEL, TestIconQueueIsolation()),
    REGRESSION("adaptive-image-demand", PARALLEL, TestAdaptiveImageDemand()),
    REGRESSION("adaptive-image-pipeline", PARALLEL, TestAdaptiveImagePipeline()),
    REGRESSION("frame-budgets", PARALLEL, TestFrameBudgets()),
    REGRESSION("large-tile-backpressure", PARALLEL, TestLargeTileBackpressure()),
    REGRESSION("large-tile-sampling", PARALLEL, TestLargeTileSampling()),
    REGRESSION("dense-grid-drawing", PARALLEL, TestDenseGridDrawing()),
    REGRESSION("icon-alpha-quality", PARALLEL, TestIconAlphaQuality(fixture)),
    REGRESSION("native-icon-sizing", PARALLEL, TestNativeIconSizing(fixture)),
    REGRESSION("shell-overlays", PARALLEL, TestShellOverlays(fixture)),
    REGRESSION("pipeline-trace", PARALLEL, TestPipelineTrace()),
    REGRESSION("property-pool-isolation", PARALLEL, TestPropertyPoolIsolation(fixture)),
    REGRESSION("drive-space-refresh", PARALLEL, TestDriveSpaceRefresh()),
    REGRESSION("preview-and-layout", PARALLEL, TestPreviewAndLayout(fixture)),
    REGRESSION("preprocessing-benchmarks", SERIAL(Regression::QuietCpu), TestPreprocessingBenchmarks(fixture)),
    // Shell providers and navigation.
    REGRESSION("layout-watch-and-cache", PARALLEL, TestLayoutWatchAndCache(fixture)),
    REGRESSION("filesystem-mutation-delivery", PARALLEL, TestFilesystemMutationDelivery(fixture)),
    REGRESSION("shell-provider-contents", PARALLEL, TestShellProviderContents(fixture)),
    REGRESSION("provider-selection", PARALLEL, TestProviderSelection(fixture)),
    REGRESSION("initial-sorting", PARALLEL, TestInitialSorting(fixture)),
    REGRESSION("native-menu-interaction", PARALLEL, TestNativeMenuInteraction(L"filesystem")),
    REGRESSION("native-menu-archive", PARALLEL, TestNativeMenuInteraction(L"archive")),
    REGRESSION("native-menu-delegated", PARALLEL, TestNativeMenuInteraction(L"delegated")),
    REGRESSION("selection-ui-work", PARALLEL, TestSelectionUiWork(fixture)),
    REGRESSION("provider-navigation", PARALLEL, TestProviderNavigation(fixture)),
    REGRESSION("paste-focus", PARALLEL, TestPasteFocus(fixture)),
    REGRESSION("shell-file-labels", PARALLEL, TestShellFileLabels(fixture)),
    REGRESSION("native-multiple-rename", PARALLEL, TestNativeMultipleRename(fixture)),
    REGRESSION("click-rename", PARALLEL, TestClickRename(fixture)),
    REGRESSION("rename-text-weight", PARALLEL, TestRenameTextWeight(fixture)),
    REGRESSION("burst-row-continuity", PARALLEL, TestBurstRowContinuity(fixture)),
    REGRESSION("sort-details", PARALLEL, TestSortDetails(fixture)),
    REGRESSION("shell-size-details", PARALLEL, TestShellSizeDetails(fixture)),
    REGRESSION("item-tips", PARALLEL, TestItemTips(fixture)),
    REGRESSION("item-tip-relative-dates", PARALLEL, TestItemTipRelativeDates()),
    REGRESSION("cloud-item-tip", PARALLEL, TestCloudItemTip(fixture)),
    // Graphics and native view integration.
    REGRESSION("real-native-viewport", PARALLEL, TestRealNativeViewport(fixture)),
    REGRESSION("native-return-routes", PARALLEL, TestNativeReturnRoutes(fixture)),
    REGRESSION("native-return-unload", PARALLEL, TestNativeReturnUnload(fixture)),
    FOCUSED("native-return-routes-core", PARALLEL, TestNativeReturnRoutesCore(fixture)),
    FOCUSED("native-return-unload-core", PARALLEL, TestNativeReturnUnloadCore(fixture)),
    REGRESSION("type-search-input", PARALLEL, TestTypeSearchInput(fixture)),
    REGRESSION("private-fixture-release", PARALLEL, TestPrivateFixtureRelease(fixture)),
    REGRESSION("native-uia-batch", PARALLEL, TestNativeUiaBatch()),
    REGRESSION("real-native-uia-pressure", PARALLEL, TestRealNativeUiaPressure(fixture)),
    REGRESSION("graphics-isolation", PARALLEL, TestGraphicsIsolation(fixture)),
    REGRESSION("graphics-resource-lifecycle", PARALLEL, TestGraphicsResourceLifecycle()),
    // Presentation, rename and timing.
    REGRESSION("presentation-owners", PARALLEL, TestPresentationOwners()),
    REGRESSION("timing-readiness", PARALLEL, TestTimingReadiness(fixture)),
    REGRESSION("mirrored-window-coordinates", PARALLEL, TestMirroredWindowCoordinates(fixture)),
    REGRESSION("rename-sizing", PARALLEL, TestRenameSizing()),
    REGRESSION("rename-appearance", PARALLEL, TestRenameAppearance(fixture)),
    REGRESSION("rename-vertical-alignment", PARALLEL, TestRenameVerticalAlignment(fixture)),
    REGRESSION("rename-handoff", PARALLEL, TestRenameHandoff(fixture)),
    REGRESSION("editor-selection-ownership", PARALLEL, TestEditorSelectionOwnership(fixture)),
    REGRESSION("rename-reorder-readiness", PARALLEL, TestRenameReorderReadiness(fixture)),
    REGRESSION("unexpected-inputs", PARALLEL, TestUnexpectedInputs(fixture)),
    // Navigation, attachment and selection.
    REGRESSION("escape-clipboard", PARALLEL, TestEscapeClipboard()),
    REGRESSION("preview-input", PARALLEL, TestPreviewInput()),
    REGRESSION("native-startup-pipeline", PARALLEL, TestNativeStartupPipeline(fixture)),
    REGRESSION("cold-attach-wake", PARALLEL, TestColdAttachWake()),
    REGRESSION("background-tab-startup", PARALLEL, TestBackgroundTabStartup(fixture)),
    REGRESSION("attached-lifecycle", PARALLEL, TestAttachedLifecycle(fixture)),
    REGRESSION("type-search-prefix", PARALLEL, TestTypeSearchPrefix()),
    REGRESSION("type-search-border", PARALLEL, TestTypeSearchBorder(fixture)),
    REGRESSION("ui-feedback-audit", PARALLEL, TestUiFeedbackAudit(fixture)),
    REGRESSION("marquee-selection", PARALLEL, RegressionManifest::MarqueeSelection(fixture)),

    // Diagnostic probes, run only with --test or -Tests.
    FOCUSED("animation-continuity", PARALLEL, RegressionManifest::WithFrame(L"private continuity frame",800,480,
        [&](HWND frame) { TestColumnViewTransition(frame); TestIconGrid(frame,fixture); })),
    FOCUSED("drive-bars", PARALLEL, RegressionManifest::WithFrame(L"private drive bar frame",1000,600,
        [](HWND frame) { TestInlineDriveBars(frame); })),
    FOCUSED("dense-rows", PARALLEL, TestDenseRowPublication()),
    FOCUSED("native-viewport-cost", SERIAL(Regression::QuietCpu), TestNativeViewportCost(fixture)),
    FOCUSED("live-sort", PARALLEL, RegressionManifest::LiveSort(fixture)),
    FOCUSED("row-composition", PARALLEL, RegressionManifest::RowComposition()),
    FOCUSED("selection-ui-mode", PARALLEL, TestSelectionUiModeReads()),
    FOCUSED("scrollbar-input", PARALLEL, TestScrollbarPressRouting()),
    FOCUSED("gpu-memory", PARALLEL, TestGraphicsMemoryProbe()),
    FOCUSED("trace-cpu", PARALLEL, TestIdleTraceCpu()),
    FOCUSED("profile-identity-probe", PARALLEL, TestProfileIdentityProbe()),
    // Visible windows and real foreground on the user's desktop.
    FOCUSED("swipe-compositor", SERIAL(Regression::InputDesktop), TestSwipeCompositor()),
};

// Historical focused flags map to manifest entries.
struct RegressionAlias { const wchar_t* flag; std::vector<const wchar_t*> tests; };
static const RegressionAlias kRegressionAliases[] = {
    {L"--shell-size", {L"shell-size-details"}}, {L"--item-tips", {L"item-tips"}},
    {L"--icon-sizing", {L"native-icon-sizing"}}, {L"--icon-alpha", {L"icon-alpha-quality"}},
    {L"--animation-continuity", {L"animation-continuity"}}, {L"--drive-bars", {L"drive-bars"}},
    {L"--dense-rows", {L"dense-rows"}}, {L"--dense-grid", {L"dense-grid-drawing"}},
    {L"--large-tiles", {L"large-tile-backpressure", L"large-tile-sampling"}},
    {L"--adaptive-images", {L"adaptive-image-demand", L"adaptive-image-pipeline"}},
    {L"--unexpected-inputs", {L"unexpected-inputs"}},
    {L"--llm-report", {L"llm-report", L"filesystem-mutation-delivery"}},
    {L"--render-quality", {L"render-quality", L"toggle-repaint", L"pixel-snapping"}},
    {L"--pipeline-trace", {L"pipeline-trace"}},
    {L"--frame-report", {L"frame-hook-release", L"dispatch-state-retirement", L"rename-handoff"}},
    {L"--lifetime", {L"lifetime-races", L"dispatch-state-retirement", L"frame-hook-release"}},
    {L"--namespace-mutations", {L"namespace-item-mutations", L"native-startup-pipeline"}},
    {L"--office-locks", {L"office-lock-filtering"}}, {L"--empty-snapshot", {L"empty-snapshot-reconciliation"}},
    {L"--status-buttons", {L"status-button-layout"}}, {L"--drive-refresh", {L"drive-space-refresh"}},
    {L"--smart-refresh", {L"tab-resume-refresh", L"window-resume-refresh", L"unchanged-refresh-presentation",
        L"sort-details", L"drive-space-refresh"}},
    {L"--column-minimum", {L"column-minimum-widths"}}, {L"--column-bridge", {L"explorer-column-bridge"}},
    {L"--options", {L"user-options", L"clipping-preflight", L"resize-auto-refit", L"option-effects-list",
        L"option-effects-grid", L"option-effects-zoom", L"option-effects-appearance", L"option-coverage",
        L"option-live-sweep-details", L"option-live-sweep-list", L"option-live-sweep-icons"}},
    {L"--styles", {L"resolved-styles", L"style-events", L"periodic-capacity-refresh"}},
    {L"--live-sort", {L"live-sort"}}, {L"--selection-ui", {L"selection-ui-work"}},
    {L"--selection-ui-mode", {L"selection-ui-mode"}},
    {L"--selection-ui-navigation", {L"selection-ui-work", L"provider-navigation"}},
    {L"--selection", {L"marquee-selection"}}, {L"--rtl-windows", {L"mirrored-window-coordinates"}},
    {L"--gpu-memory", {L"gpu-memory"}}, {L"--gpu-lifecycle", {L"graphics-resource-lifecycle"}},
    {L"--trace-cpu", {L"trace-cpu"}}, {L"--native-uia", {L"native-uia-batch", L"real-native-uia-pressure"}},
    {L"--native-viewport", {L"real-native-viewport", L"native-uia-batch", L"real-native-uia-pressure"}},
    {L"--profile-identity-probe", {L"profile-identity-probe"}},
    {L"--native-context-menu", {L"native-menu-interaction", L"native-menu-archive", L"native-menu-delegated"}},
    {L"--preprocessing", {L"preprocessing-benchmarks"}}, {L"--initial-sort", {L"initial-sorting"}},
    {L"--provider-selection", {L"provider-selection"}}, {L"--provider-navigation", {L"provider-navigation"}},
    {L"--paste-focus", {L"paste-focus"}}, {L"--shell-provider", {L"shell-provider-contents"}},
    {L"--filesystem", {L"shell-provider-contents", L"filesystem-mutation-delivery", L"burst-row-continuity"}},
    {L"--timing", {L"timing-readiness"}}, {L"--presentation", {L"presentation-owners"}},
    {L"--row-composition", {L"row-composition"}},
    {L"--rename", {L"rename-sizing", L"rename-appearance", L"rename-handoff", L"editor-selection-ownership",
        L"rename-reorder-readiness", L"rename-vertical-alignment"}},
    {L"--scrollbar-input", {L"scrollbar-input"}}, {L"--swipe-compositor", {L"swipe-compositor"}},
    {L"--touch", {L"touch-input"}}, {L"--additional-gestures", {L"additional-gestures"}},
    {L"--new-tabs", {L"background-tab-startup", L"cold-attach-wake"}},
    {L"--checkboxes", {L"selection-checkboxes"}},
    {L"--graphics", {L"graphics-isolation", L"timing-readiness"}},
    {L"--navigation", {L"attached-lifecycle", L"ui-feedback-audit"}},
};

namespace RegressionManifest {
void CheckOptionCoverage() {
    std::set<std::wstring> schema,covered;
    for(const auto& option:OptionCatalog::Schema()) schema.insert(option.key);
    for(const auto& entry:OptionEffects::kCoverage) {
        Check(covered.insert(entry.key).second,"option listed twice in OptionEffects::kCoverage");
        Check(schema.contains(entry.key),"OptionEffects::kCoverage names an option that is not in the settings schema");
        const auto test=std::find_if(std::begin(kRegressions),std::end(kRegressions),
            [&](const Regression::Test& candidate) {return !wcscmp(candidate.name,entry.test);});
        Check(test!=std::end(kRegressions) && test->gate==Regression::Gate::Release,
            "option coverage names a test that is not in the release gate");
    }
    for(const auto& key:schema) if(!covered.contains(key))
        throw std::runtime_error("option "+Regression::Narrow(key)+" has no effect test in OptionEffects::kCoverage");
    // Every option must also be sweepable: the live sweep derives its values.
    for(const auto& option:OptionCatalog::Schema()) Check(OptionEffects::ProbeValues(option).size()>=2,"option without probe values");
    std::cout<<"PASS: all "<<schema.size()<<" options have release effect coverage and live-sweep probe values\n";
}
}
