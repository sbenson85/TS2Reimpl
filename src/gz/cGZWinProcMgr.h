#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <map>

// cGZWinProcMgr (lane KM): the window-procedure manager service (service id 0x17aa3541, priority 999999, interface
// 0x47aa34da). It creates window procedures through the COM object (class id -> cIGZUnknown) and attaches them to a
// window found by id through the window manager service (0xc2c2eb0f / cIGZWinMgr 0x22c2eb1f), then hands the window out
// queried for the caller's interface.
//
// Faces (RTTI order): +0 rz::cRZSystemService (cIGZSystemService, game vtable 0xeb8f40, 13 slots), +4 its cRZUnknownMT
// (0xeb8f78, 6), +0x1c cIGZWinProcMgr (0xeb8f94, 10). 0x2c bytes. The COM director registers the factory 0xcc70c0 for
// class 0x77aa34e6 (src/rz/COMDirectorData.cpp); it is the only creator (new 0x2c, the constructor 0xcc6cf0, the +0x1c
// face). With the factory hooked to Create, the game's vtables are on no object: the constructor 0xcc6cf0, the destructor
// 0xcc6d90 and every slot are in effect through it (db/status/cGZWinProcMgr.jsonl).

using WinProcRef = cRZAutoRefCount<cIGZUnknown>;

// cIGZWinProcMgr (iid 0x47aa34da), 10 slots.
class cIGZWinProcMgr : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x47aa34da;

    // Slots 3 and 4 hold the folded `mov al, 1; ret 4` (shared_vf_148260) in this class: they take one word and do
    // nothing. Shutdown calls slot 4 with each key of the (never filled) map until it is empty.
    virtual bool Slot3(uint32_t key) = 0; // slot 3
    virtual bool Slot4(uint32_t key) = 0; // slot 4
    // 0xcc7130: the COM object's GetClassObject (cIGZCOM slot 3, ret 0xc), tail-jumped to.
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out) = 0; // slot 5
    // 0xcc6ed0 (ret 0xc): a procedure of `classId` attached to the default window; the window queried for `iid`.
    virtual bool CreateProcForDefaultWindow(uint32_t classId, uint32_t iid, void **out) = 0; // slot 6
    // 0xcc6e00 (ret 0x10): the same for the window `windowId`.
    virtual bool CreateProcForWindow(uint32_t classId, uint32_t windowId, uint32_t iid, void **out) = 0; // slot 7
    // 0xcc7030 (ret 0xc): the given procedure attached to the default window; the window queried for `iid`.
    virtual bool AttachProcToDefaultWindow(cIGZUnknown *proc, uint32_t iid, void **out) = 0; // slot 8
    // 0xcc6fa0 (ret 0x10): the same for the window `windowId`.
    virtual bool AttachProcToWindow(cIGZUnknown *proc, uint32_t windowId, uint32_t iid, void **out) = 0; // slot 9
};

class cGZWinProcMgr : public rz::cRZSystemService, public cIGZWinProcMgr {
public:
    static constexpr uint32_t kSize = 0x2c;
    static constexpr uint32_t kServiceId = 0x17aa3541;
    static constexpr int32_t kPriority = 999999;
    static constexpr uint32_t kDefaultWindowId = 0x88963e39; // the window slots 6 and 8 use
    static constexpr uint32_t kWinMgrServiceId = 0xc2c2eb0f, kWinMgrIid = 0x22c2eb1f;
    static constexpr uint32_t kWinIid = 0x5386d516, kWinProcIid = 0x22e85d8e;

    cGZWinProcMgr();            // 0xcc6cf0
    ~cGZWinProcMgr() override;  // 0xcc6d90; deleting 0xcc6de0 (+4 slot 3, thunk 0xcc6dcd)

    cGZWinProcMgr(const cGZWinProcMgr &) = delete;
    cGZWinProcMgr &operator=(const cGZWinProcMgr &) = delete;

    // 0xcc70c0 (the factory the director registers): new, construct, the cIGZWinProcMgr face.
    static cIGZWinProcMgr *__cdecl Create();

    // cIGZUnknown (every face; +4 / +0x1c through the thunks 0xcc717f / 0xcc7187). AddRef / Release: cRZSystemService's.
    bool QueryInterface(uint32_t riid, void **out) override; // 0xcc7140
    uint32_t AddRef() override { return rz::cRZSystemService::AddRef(); }
    uint32_t Release() override { return rz::cRZSystemService::Release(); }

    // cIGZSystemService
    bool Shutdown() override; // 0xcc7190

    // cIGZWinProcMgr
    bool Slot3(uint32_t key) override; // folded 0x148260
    bool Slot4(uint32_t key) override; // folded 0x148260
    uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out) override;                     // 0xcc7130
    bool CreateProcForDefaultWindow(uint32_t classId, uint32_t iid, void **out) override;               // 0xcc6ed0
    bool CreateProcForWindow(uint32_t classId, uint32_t windowId, uint32_t iid, void **out) override;   // 0xcc6e00
    bool AttachProcToDefaultWindow(cIGZUnknown *proc, uint32_t iid, void **out) override;               // 0xcc7030
    bool AttachProcToWindow(cIGZUnknown *proc, uint32_t windowId, uint32_t iid, void **out) override;   // 0xcc6fa0

    // 0xcc71c0 (ret 8): a control of type `windowId` created by the control manager service 0xc2c2eb0f
    // (cIGZWinCtrlMgr::CreateControl(type, iid 1, out), slot 26) into `window`, released first; the default type
    // 0x88963e39 is a generic window. False when the service is missing (then `window` is left alone).
    bool FindWindow(uint32_t windowId, WinProcRef &window);
    // 0xcc7260 (ret 8): queries the window (0x5386d516) and the procedure (0x22e85d8e) and gives the window the procedure
    // (its slot 7). On a refusal the window's procedure is cleared (slot 7 with null) and the answer is false.
    bool AttachProc(cIGZUnknown *window, cIGZUnknown *proc);

    // +0x20: id -> counted object; Shutdown hands each id to slot 4 until none is left, the destructor releases what is
    // left (the node's value destructor 0x1421bb: pair<const uint32_t, ref>). Nothing in the image inserts (slots 3 / 4
    // are no-ops in this class).
    std::map<uint32_t, WinProcRef> mProcs;
    uint32_t mDefaultWindowId; // +0x28
};

static_assert(sizeof(cGZWinProcMgr) == cGZWinProcMgr::kSize, "cGZWinProcMgr size");
static_assert(offsetof(cGZWinProcMgr, mServiceId) == 0xc, "cGZWinProcMgr layout: the service part at +0");
static_assert(offsetof(cGZWinProcMgr, mProcs) == 0x20 && offsetof(cGZWinProcMgr, mDefaultWindowId) == 0x28,
              "cGZWinProcMgr layout");
