#pragma once
// nGZGraphic4::cDisplayMonitorW32 (1.18.0.187; docs/subsystems/rendering.md, R2b; lane VU, milestone O-c): what the
// graphics layer knows of the monitor a device's adapter shows on. cDeviceDX::Init makes one through the COM (class
// 0x7b64ce01, the graphics director's factory 0x1545c0) and hands it IDirect3D9::GetAdapterMonitor; the device
// enumeration reads its physical aspect (slot 6) and the graphic system compares and resets its monitor (slots 7-8).
//
// Standalone shape (docs/standalone-shape.md): a real class in the original slot order, made only by Create (the factory,
// hooked at 0x1545c0), so every object the game makes carries our vtable and the image's 0xd9c978 is carried by none
// (its slot bodies are in effect through their callers). Slots 3, 4 and 6 are bodies folded across unrelated classes
// (shared_vf_154780, shared_vf_1547e0, shared_vf_1547a0) and 1, 2 and 8 the common AddRef / Release / getter bodies: ours
// only in our vtable, never hooked.
//
// Layout (0x48): +0 vtable, +4 reference count, +8 the monitor (HMONITOR), +0xc the device name (std::string, "UNKNOWN"
// until a monitor is set), +0x24 the monitor's rectangle (800 x 600 until set), +0x34 its work area, +0x44 the physical
// aspect (width / height in millimetres, 0 when unknown).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "gz/cIGZUnknown.h"

namespace nGZGraphic4 {

class cDisplayMonitorW32 : public cIGZUnknown {
public:
    static constexpr uint32_t kSize = 0x48;
    static constexpr uint32_t kClass = 0x7b64ce01;
    static constexpr uint32_t kIidDisplayMonitor = 0xfb64c519; // cIDisplayMonitor
    static constexpr uint32_t kIidOther = 0xfa12b663;          // also answered, as the same object

    // 0x1545c0 (cdecl): a new monitor, unreferenced (the COM's GetClassObject counts it), with no monitor set.
    static cDisplayMonitorW32 *__cdecl Create();

    cDisplayMonitorW32() = default;

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1547b0: ids 1, 0xfa12b663, 0xfb64c519
    uint32_t AddRef() override { return ++mRefCount; }        // shared 0x148fb0
    uint32_t Release() override;                              // shared 0x153f50: at zero the deleting destructor (slot 9)

    // ---- the monitor ----
    virtual const char *GetDeviceName() { return mDeviceName.c_str(); } // 3 (shared_vf_154780)
    virtual RECT *GetWorkRect(RECT *out);                               // 4 (shared_vf_1547e0, ret 4): returns out
    virtual RECT *GetMonitorRect(RECT *out);                            // 5 (0x154790, ret 4): returns out
    virtual float GetAspect() { return mAspect; }                       // 6 (shared_vf_1547a0)
    // 7 (0x154620, ret 4): takes the monitor and asks the system for its rectangles, device name and physical size
    // (GetMonitorInfoA through multimon.h's stub; the device context of the name for HORZSIZE / VERTSIZE).
    virtual void SetMonitor(HMONITOR monitor);
    virtual HMONITOR GetMonitor() { return mMonitor; } // 8 (shared 0x46390)
    virtual ~cDisplayMonitorW32() = default;            // 9 (0x154550, deleting)

    uint32_t mRefCount = 0;         // +4
    HMONITOR mMonitor = nullptr;    // +8
    std::string mDeviceName;        // +0xc
    RECT mMonitorRect;              // +0x24 (not set by the factory)
    RECT mWorkRect;                 // +0x34 (not set by the factory)
    float mAspect = 0.0f;           // +0x44
};
static_assert(sizeof(cDisplayMonitorW32) == cDisplayMonitorW32::kSize &&
                  offsetof(cDisplayMonitorW32, mDeviceName) == 0xc &&
                  offsetof(cDisplayMonitorW32, mMonitorRect) == 0x24 &&
                  offsetof(cDisplayMonitorW32, mWorkRect) == 0x34 && offsetof(cDisplayMonitorW32, mAspect) == 0x44,
              "cDisplayMonitorW32: 0x48 bytes");

} // namespace nGZGraphic4
