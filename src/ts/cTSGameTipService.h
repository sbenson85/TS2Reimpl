#pragma once
// cTSGameTipService (lane KX): the game tip service (system service id 0x5181421b, priority 0; cITSGameTipService iid
// 0x5181420f). 0x30 bytes, /O2. RVAs 1.18.0.187. It keeps the game tips (cTSGameTip, src/ts/cTSGameTip.h: persist resources
// of type 0xb1827a47) of one package group and their texts (the STR# table 0x12d of that group, a cTSStringSet): Init
// registers the tip resource factory (src/ts/cTSGameTipResourceFactory.h) with the persist system's resource manager and
// loads; the group comes from the first tip record found, or from SetTipGroup, and without any tip record the service makes
// the default tips (CreateDefaultTips).
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xeb7624, 13 slots), +4 cRZUnknownMT (0xeb765c, 6:
// slot 3 the deleting destructor's thunk 0xcb5ad0), +0x1c cITSGameTipService (0xeb7678, 12 slots). The original's slot
// bodies take `this` as their face and reach the fields at face-relative offsets; ours are members.
//
// Standalone shape (docs/standalone-shape.md): the factory 0xcb5ce0 (hooked: `new` of ours) is registered by address with
// the app's COM director (0xc2bba4) and is the only caller of the constructor 0xcb59c0; the vtables are referenced only by
// the constructor and the destructor 0xcb5a60, so every object carries ours (db/status/cTSGameTipService.jsonl).
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount
#include "ts/cTSGameTip.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cIGZString;
namespace ifaces {
class cITSStringSet;
} // namespace ifaces

// GetTips' filter (the UI's anonymous-namespace cGameTipFilter, src/misc/SmallVirtuals.h): slot 0 (ret 4).
class cGameTipFilterIface {
public:
    virtual bool IsIncluded(cITSGameTip *tip) = 0;
};

// GetTips' output: counted tip faces, elements from the allocator service (growth 0xcb5680 copies them, counting each,
// then releases the old ones: cRZAutoRefCount has no move).
using GameTipRef = cRZAutoRefCount<cITSGameTip>;
using GameTipList = std::vector<GameTipRef, GZServiceAllocator<GameTipRef>>;
static_assert(sizeof(GameTipList) == 0xc, "MSVC 14.40 vector");

// cITSGameTipService (iid 0x5181420f): slots 3-11. Instances are the tip records' key instances in the service's group.
class cITSGameTipService : public cIGZUnknown {
public:
    virtual uint32_t GetTextCount() = 0;                                  // 3 (0xcb5e70)
    virtual bool GetTip(uint32_t instance, cITSGameTip **out) = 0;        // 4 (0xcb5dd0, ret 8)
    virtual uint32_t GetTips(GameTipList *out, cGameTipFilterIface *filter, bool append) = 0; // 5 (0xcb5e90, ret 0xc)
    virtual bool GetText(uint32_t id, cIGZString *out) = 0;               // 6 (0xcb5e40, ret 8)
    virtual cITSGameTip *CreateTip() = 0;                                 // 7 (0xcb5b30): not counted
    virtual bool DeleteTip(uint32_t instance) = 0;                        // 8 (0xcb5c70, ret 4)
    virtual uint8_t SetTipTexts(cITSGameTip *tip, const cIGZString *text0, const cIGZString *text1,
                             const cIGZString *text2) = 0;                // 9 (0xcb6490, ret 0x10)
    virtual uint8_t SaveTip(cITSGameTip *tip) = 0;                           // 10 (0xcb63e0, ret 4)
    virtual void SetTipGroup(uint32_t group) = 0;                         // 11 (0xcb6dc0, ret 4)
};

class cTSGameTipService : public rz::cRZSystemService, public cITSGameTipService {
public:
    static constexpr uint32_t kVTable = 0xeb7624;
    static constexpr uint32_t kVTable4 = 0xeb765c;
    static constexpr uint32_t kTipServiceVTable = 0xeb7678;
    static constexpr uint32_t kSize = 0x30;
    static constexpr uint32_t kServiceId = 0x5181421b;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kIid = 0x5181420f; // cITSGameTipService
    static constexpr uint32_t kStringSetType = 0x53545223; // 'STR#'
    static constexpr uint32_t kStringSetInstance = 0x12d;
    static constexpr uint32_t kStringSetIid = 0xfb4cb0de;  // cITSStringSet

    cTSGameTipService();                    // 0xcb59c0
    ~cTSGameTipService() noexcept override; // 0xcb5a60 (deleting 0xcb5b00; +4 thunk 0xcb5ad0)
    cTSGameTipService(const cTSGameTipService &) = delete;
    cTSGameTipService &operator=(const cTSGameTipService &) = delete;

    // 0xcb5ce0 (__cdecl): the COM director's factory: a new service (null when new failed).
    static cTSGameTipService *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xcb6320; thunks 0xcb6351 (+4) and 0xcb6359 (+0x1c)
    uint32_t AddRef() override;                              // folded (the base's)
    uint32_t Release() override;                             // folded (the base's)

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xcb6170
    bool Shutdown() override; // 0xcb6de0

    // ---- cITSGameTipService (+0x1c) ----
    uint32_t GetTextCount() override;
    bool GetTip(uint32_t instance, cITSGameTip **out) override;
    uint32_t GetTips(GameTipList *out, cGameTipFilterIface *filter, bool append) override;
    bool GetText(uint32_t id, cIGZString *out) override;
    cITSGameTip *CreateTip() override;
    bool DeleteTip(uint32_t instance) override;
    uint8_t SetTipTexts(cITSGameTip *tip, const cIGZString *text0, const cIGZString *text1, const cIGZString *text2) override;
    uint8_t SaveTip(cITSGameTip *tip) override;
    void SetTipGroup(uint32_t group) override;

    cITSGameTipService *TipService() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSGameTipService face's vtable
    cRZAutoRefCount<ifaces::cITSStringSet> mStrings; // +0x20 the group's STR# 0x12d (a private copy, LoadStrings)
    cRZAutoRefCount<cIGZUnknown> mSegment;           // +0x24 where tips are saved (nothing here sets it: null)
    uint32_t mGroup;                                 // +0x28 the tips' package group (0: none yet)
    uint32_t mNextInstance;                          // +0x2c the instance the next new tip gets

private:
    bool HasTip(uint32_t instance); // 0xcb5d50 (ret 4)
    void Load();                    // 0xcb66a0
    void LoadStrings();             // 0xcb6250
    void CreateDefaultTips();       // 0xcb67e0
};

static_assert(sizeof(cTSGameTipService) == cTSGameTipService::kSize, "new 0x30 in the factory");
static_assert(offsetof(cTSGameTipService, mStrings) == 0x20 && offsetof(cTSGameTipService, mSegment) == 0x24 &&
                  offsetof(cTSGameTipService, mGroup) == 0x28 && offsetof(cTSGameTipService, mNextInstance) == 0x2c,
              "cTSGameTipService layout");

// 0xb39020 (__cdecl): the game tip service's cITSGameTipService face, cached in the global 0x10faf8c (the cached getter
// shape of src/rz/ServiceGetters.h).
cITSGameTipService *GetGameTipService();
