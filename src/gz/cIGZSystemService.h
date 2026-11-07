#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZSystemService: what the framework holds a service by (id 0x287fb697; 13 slots). Lane EC's structure pass
// (docs/subsystems/startup.md). The slot comments are cRZSystemService's (vtable 0xda3058): the defaults most services
// inherit, many of them 2..6-byte functions folded with unrelated classes. Slot order = declaration order (no overloads).
//
// The framework uses: GetServiceID and GetServicePriority when a service is added (AddSystemService 0x14234f keys its
// multimap by priority), Init / Shutdown in the band loops (cCTGFramework slots 41..46), SetServiceRunning through the
// start/stop messages, OnTick from cCTGFramework::OnTick 0xc2d427 (only running services), OnIdle from
// cGZFramework::OnIdle 0x142dbb (every N ticks), and GetServiceTickPriority to order the tick list (AddToTick 0x1424fe).
class cIGZSystemService : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x287fb697;

    virtual uint32_t GetServiceID() = 0;            // slot 3, folded 0x145770 (+0xc)
    virtual void SetServiceID(uint32_t id) = 0;     // slot 4, folded 0x149c60 (ret 4)
    virtual int32_t GetServicePriority() = 0;       // slot 5, folded 0x149cf0 (+0x10)
    virtual bool IsServiceRunning() = 0;            // slot 6, folded 0x156110 (byte +0x18)
    virtual void SetServiceRunning(bool running) = 0; // slot 7, folded 0x1b9690 (ret 4)
    virtual bool Init() = 0;                        // slot 8, default 0x144910 (true)
    virtual bool Shutdown() = 0;                    // slot 9, default 0x144910 (true)
    virtual bool OnTick(uint32_t counter) = 0;      // slot 10, default 0x148260 (true, ret 4)
    virtual bool OnIdle(uint32_t counter) = 0;      // slot 11, default 0x148260 (true, ret 4)
    virtual int32_t GetServiceTickPriority() = 0;   // slot 12, folded 0x149d00 (+0x14)
};
