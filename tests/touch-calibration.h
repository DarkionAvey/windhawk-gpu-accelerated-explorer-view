// A private digitizer supplies real Windows calibration to Ninput without
// injecting input, showing windows or touching the user's desktop/focus.
#pragma once
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cstdio>
struct TouchCalibration {
    HSYNTHETICPOINTERDEVICE synthetic=nullptr;
    HANDLE device=nullptr;
    RECT digitizer{},display{};
    const char* prerequisite="GetPointerDevices (before creation)";
    DWORD error=ERROR_SUCCESS;
    UINT32 beforeCount=0,afterCount=0;
    void Failed(const char* api) {prerequisite=api; error=GetLastError(); device=nullptr;}
    TouchCalibration() {
        // The device list is session-wide. Concurrent test processes would
        // otherwise each see the other's new digitizer in their before/after
        // difference and could adopt a device that its owner then destroys.
        // Only creation and identification are serialized, never the test.
        struct Discovery {
            HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\gpu-overlay-regression-digitizer-discovery");
            Discovery() {if(mutex) WaitForSingleObject(mutex,INFINITE);}
            ~Discovery() {if(mutex) {ReleaseMutex(mutex); CloseHandle(mutex);}}
        } discovery;
        UINT32 count=0;
        if(!GetPointerDevices(&count,nullptr)) {Failed(prerequisite); return;}
        std::vector<POINTER_DEVICE_INFO> before(count);
        if(count && !GetPointerDevices(&count,before.data())) {Failed(prerequisite); return;}
        before.resize(count); beforeCount=count;
        synthetic=CreateSyntheticPointerDevice(PT_TOUCH,10,POINTER_FEEDBACK_NONE);
        if(!synthetic) {Failed("CreateSyntheticPointerDevice"); return;}
        if(!GetPointerDevices(&count,nullptr)) {Failed("GetPointerDevices (after creation)"); return;}
        std::vector<POINTER_DEVICE_INFO> after(count);
        if(!GetPointerDevices(&count,after.data())) {Failed("GetPointerDevices (after creation)"); return;}
        after.resize(count); afterCount=count;
        for(const auto& candidate:after)
            if(candidate.pointerDeviceType==POINTER_DEVICE_TYPE_TOUCH &&
                std::none_of(before.begin(),before.end(),[&](const auto& old){return old.device==candidate.device;}))
                device=candidate.device;
        if(!device) {SetLastError(ERROR_NOT_FOUND); Failed("new private touch device discovery"); return;}
        if(!GetPointerDeviceRects(device,&digitizer,&display)) {Failed("GetPointerDeviceRects"); return;}
        if(digitizer.right<=digitizer.left || digitizer.bottom<=digitizer.top ||
            display.right<=display.left || display.bottom<=display.top) {
            SetLastError(ERROR_INVALID_DATA); Failed("nonempty digitizer/display calibration"); return;
        }
        prerequisite=nullptr;
    }
    bool Ready() const {
        if(!device) std::fprintf(stderr,"PRECONDITION: touch calibration requires %s; error=%lu devices-before=%u after=%u\n",
            prerequisite,error,beforeCount,afterCount);
        return device!=nullptr;
    }
    ~TouchCalibration() {if(synthetic) DestroySyntheticPointerDevice(synthetic);}
    TouchCalibration(const TouchCalibration&)=delete;
    TouchCalibration& operator=(const TouchCalibration&)=delete;
    POINT Himetric(POINT screen) const {
        return {digitizer.left+MulDiv(screen.x-display.left,digitizer.right-digitizer.left,display.right-display.left),
            digitizer.top+MulDiv(screen.y-display.top,digitizer.bottom-digitizer.top,display.bottom-display.top)};
    }
};
