#pragma once
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

// cTSUIServiceManager (lane KM): the game's UI service manager (service id 0x0bd09b0b, priority 0, interface 0xebd09b07).
// On the application's init message (0xc8) it creates eleven UI services through the COM object (each initialised and
// kept on its tick list, and held for the getters), sets up the UI (original functions), and posts message 0x8d504495;
// on the shutdown message (0xc9) it shuts the tick list's services down and drops them; message 0xd03ffba0 runs
// 0x740422. Every tick it ticks the listed services.
//
// Faces (RTTI order): +0 rz::cRZSystemService (cIGZSystemService, game vtable 0xdf5d24, 13 slots), +4 its cRZUnknownMT
// (0xdf5d5c, 6), +0x1c cITSUIServiceManager (0xdf5d78, 15), +0x20 cIGZMessageTarget2 (0xdf5db8, 4). 0x60 bytes. The
// COM director registers the factory 0x73f8af for class 0xebd09b02 (src/rz/COMDirectorData.cpp); it is the only creator
// (new 0x60, the constructor 0x73f21e, the +0x1c face). With it hooked to Create, the game's vtables are on no object:
// the constructor, the destructor 0x73f2d8 and every slot are in effect through it (db/status/cTSUIServiceManager.jsonl).

using UIServiceRef = cRZAutoRefCount<cIGZUnknown>;

// cITSUIServiceManager (iid 0xebd09b07), 15 slots: ten getters of the held services (the folded `mov eax, [ecx+N]`
// bodies), then the tick list's add and remove.
class cITSUIServiceManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xebd09b07;

    virtual cIGZUnknown *GetService0() = 0;  // slot 3, folded 0x149cf0 (+0x2c)
    virtual cIGZUnknown *GetService3() = 0;  // slot 4, folded 0x147d00 (+0x38)
    virtual cIGZUnknown *GetService2() = 0;  // slot 5, folded 0x148ba0 (+0x34)
    virtual cIGZUnknown *GetService4() = 0;  // slot 6, folded 0x15e550 (+0x3c)
    virtual cIGZUnknown *GetService5() = 0;  // slot 7, folded 0x15e970 (+0x40)
    virtual cIGZUnknown *GetService6() = 0;  // slot 8, folded 0x15f390 (+0x44)
    virtual cIGZUnknown *GetService7() = 0;  // slot 9, folded 0x147770 (+0x48)
    virtual cIGZUnknown *GetService8() = 0;  // slot 10, folded 0x1457d0 (+0x4c)
    virtual cIGZUnknown *GetService9() = 0;  // slot 11, folded 0x147800 (+0x50)
    virtual cIGZUnknown *GetService10() = 0; // slot 12, folded 0x1694c0 (+0x54)
    // 0x73f4b2 (ret 4): appends `service` (counted) to the tick list unless it is there (false then).
    virtual bool AddTickService(cIGZUnknown *service) = 0; // slot 13
    // 0x73fa94 (ret 4): removes `service` from the tick list, released (false when it is not there).
    virtual bool RemoveTickService(cIGZUnknown *service) = 0; // slot 14
};

class cTSUIServiceManager : public rz::cRZSystemService, public cITSUIServiceManager, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kSize = 0x60;
    static constexpr uint32_t kServiceId = 0x0bd09b0b;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kUIServiceIid = 0x0bd09a11; // what InitService asks the COM object for
    static constexpr uint32_t kMessage2Iid = 0x65297976;  // cIGZMessage2
    static constexpr uint32_t kMsgAppInit = 0xc8, kMsgAppShutdown = 0xc9, kMsgD03ffba0 = 0xd03ffba0;
    static constexpr uint32_t kMsgUIReady = 0x8d504495; // posted at the end of the init message
    static constexpr unsigned kServices = 11;

    // The held services: class id and interface, in member order (+0x2c ... +0x54).
    struct ServiceIds {
        uint32_t classId, iid;
    };
    static constexpr ServiceIds kServiceIds[kServices] = {
        {0x4bc50824, 0x4bc50829}, {0xccfd8d09, 0x0bd09a11}, {0x6c1a728b, 0x8c1a728f}, {0xec278fac, 0xec278fb1},
        {0xac33863b, 0x8c33862f}, {0xac758b38, 0xac758b34}, {0x6ca009a2, 0xeca00997}, {0x8cad101a, 0xacad101f},
        {0x4ea0b483, 0x4ea0b488}, {0x6d3c721e, 0x6d3c7223}, {0x4cd9da94, 0x4cd9da98},
    };

    cTSUIServiceManager();            // 0x73f21e
    ~cTSUIServiceManager() noexcept override; // 0x73f2d8; deleting 0x73f3ff (+4 slot 3, thunk 0x73f3f7)

    cTSUIServiceManager(const cTSUIServiceManager &) = delete;
    cTSUIServiceManager &operator=(const cTSUIServiceManager &) = delete;

    // 0x73f8af (the factory the director registers): new, construct, the cITSUIServiceManager face.
    static cITSUIServiceManager *__cdecl Create();

    // cIGZUnknown (every face; +4 / +0x1c / +0x20 through the thunks 0x73fa7c / 0x73fa84 / 0x73fa8c)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x73fa3f
    uint32_t AddRef() override { return rz::cRZSystemService::AddRef(); }
    uint32_t Release() override { return rz::cRZSystemService::Release(); }

    // cIGZSystemService
    bool Init() override;                   // 0x73f8f2
    bool Shutdown() override;               // 0x740697
    bool OnTick(uint32_t counter) override; // 0x73fa1d

    // cITSUIServiceManager
    cIGZUnknown *GetService0() override { return mServices[0].Get(); }
    cIGZUnknown *GetService3() override { return mServices[3].Get(); }
    cIGZUnknown *GetService2() override { return mServices[2].Get(); }
    cIGZUnknown *GetService4() override { return mServices[4].Get(); }
    cIGZUnknown *GetService5() override { return mServices[5].Get(); }
    cIGZUnknown *GetService6() override { return mServices[6].Get(); }
    cIGZUnknown *GetService7() override { return mServices[7].Get(); }
    cIGZUnknown *GetService8() override { return mServices[8].Get(); }
    cIGZUnknown *GetService9() override { return mServices[9].Get(); }
    cIGZUnknown *GetService10() override { return mServices[10].Get(); }
    bool AddTickService(cIGZUnknown *service) override;    // 0x73f4b2
    bool RemoveTickService(cIGZUnknown *service) override; // 0x73fa94

    // cIGZMessageTarget2
    bool DoMessage(void *message) override; // 0x73f524

    // 0x73f41e (ret 0xc): the COM object's class `classId` as a UI service (0x0bd09a11); when it initialises (its slot 3)
    // it joins the tick list and is queried for `iid` into `out`. The answer is that query's.
    bool InitService(uint32_t classId, uint32_t iid, void **out);
    // 0x744844 (__cdecl): the object held in the cell 0x10de488, when set: its slot 5, then the cell cleared.
    static void ReleaseCell10de488();

    std::list<UIServiceRef> mTickServices;   // +0x24 (counted: the node value destructor 0x21450d releases)
    UIServiceRef mServices[kServices];       // +0x2c
    std::set<uint32_t> mMessageTypes;        // +0x58 (0xc8, 0xc9, 0xd03ffba0: registered with the message server by Init)
};

static_assert(sizeof(cTSUIServiceManager) == cTSUIServiceManager::kSize, "cTSUIServiceManager size");
static_assert(offsetof(cTSUIServiceManager, mServiceId) == 0xc, "cTSUIServiceManager layout: the service part at +0");
static_assert(offsetof(cTSUIServiceManager, mTickServices) == 0x24 && offsetof(cTSUIServiceManager, mServices) == 0x2c &&
                  offsetof(cTSUIServiceManager, mMessageTypes) == 0x58,
              "cTSUIServiceManager layout");
