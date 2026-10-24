// Link verification against the installed Windhawk host API (no editor stubs).
#define WH_MOD_ID L"gpu-accelerated-explorer-view"
#ifndef WH_MOD_VERSION
#error Build with tests/run-regressions.ps1 to supply the selected mod version
#endif
#include <windhawk_api.h>
extern "C" {
__declspec(dllexport) BOOL Wh_ModInit();
__declspec(dllexport) void Wh_ModAfterInit();
__declspec(dllexport) void Wh_ModSettingsChanged();
__declspec(dllexport) void Wh_ModBeforeUninit();
__declspec(dllexport) void Wh_ModUninit();
}
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
#include GPU_OVERLAY_SOURCE
