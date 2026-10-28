// A private digitizer supplies real Windows calibration to Ninput without
// injecting input, showing windows or touching the user's desktop/focus.
#pragma once
#include <windows.h>
#include <vector>
#include <algorithm>
struct TouchCalibration {
    HSYNTHETICPOINTERDEVICE synthetic=nullptr;
    HANDLE device=nullptr;
    RECT digitizer{},display{};
    TouchCalibration() {
        UINT32 count=0;
        if(!GetPointerDevices(&count,nullptr)) return;
        std::vector<POINTER_DEVICE_INFO> before(count);
        if(count && !GetPointerDevices(&count,before.data())) return;
        synthetic=CreateSyntheticPointerDevice(PT_TOUCH,10,POINTER_FEEDBACK_NONE);
        if(!synthetic || !GetPointerDevices(&count,nullptr)) return;
        std::vector<POINTER_DEVICE_INFO> after(count);
        if(!GetPointerDevices(&count,after.data())) return;
        for(const auto& candidate:after)
            if(candidate.pointerDeviceType==POINTER_DEVICE_TYPE_TOUCH &&
                std::none_of(before.begin(),before.end(),[&](const auto& old){return old.device==candidate.device;}))
                device=candidate.device;
        if(!device || !GetPointerDeviceRects(device,&digitizer,&display)) device=nullptr;
    }
    ~TouchCalibration() {if(synthetic) DestroySyntheticPointerDevice(synthetic);}
    TouchCalibration(const TouchCalibration&)=delete;
    TouchCalibration& operator=(const TouchCalibration&)=delete;
    POINT Himetric(POINT screen) const {
        return {digitizer.left+MulDiv(screen.x-display.left,digitizer.right-digitizer.left,display.right-display.left),
            digitizer.top+MulDiv(screen.y-display.top,digitizer.bottom-digitizer.top,display.bottom-display.top)};
    }
};
