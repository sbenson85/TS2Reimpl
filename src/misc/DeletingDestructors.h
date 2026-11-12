#pragma once
#include <cstddef>
#include <cstdint>

#include "game/owned.h"

// Deleting destructors (slot 0, __thiscall, ret 4) of classes whose other virtual slots are already done (RVAs
// 1.18.0.187). Each runs the class's destructor, frees sizeof(class) with the sized operator delete when bit 0 of the
// flags is set, and returns this. Two kinds:
//   - interfaces and empty bases whose destructor is only the vtable store of the class itself (inline here);
//   - classes whose destructor is a function of its own: called through OriginalFunction until that class is taken
//     on (its RVA is in the member's comment and in the status entry's original_calls).
//
// Hook-shaped (docs/standalone-shape.md): only the sizes are established, from the frees. The game builds these
// objects, so the members are hooked at their RVAs and store the game's vtables, as the originals do. When a class is
// taken to the standalone shape, its member moves to its own files (and becomes the compiler's deleting destructor).

// --- speed-built (src/misc/DeletingDestructors.cpp) ---

// The scene graph's "all branch nodes" filter (an anonymous-namespace class; slot 1 of its vtable 0xe687c0).
struct cBranchNodesNoFilter {
    static constexpr uint32_t kVTable = 0xe687c0;
    cBranchNodesNoFilter *DeletingDestructor(uint32_t flags); // 0xa35070
    const void *mVTable; // +0
};
static_assert(sizeof(cBranchNodesNoFilter) == 4, "layout: the size 0xa35070 frees");

// cIEventHeader's table (owned: an image-free program aliases it to ts2_own_vt_d9ca58, src/gz/cVector4fEvent.cpp).
TS2_OWNED_STATIC(const OwnedVTable, d9ca58);

namespace nGZEvent {
struct cIEventHeader {
    static constexpr uint32_t kVTable = 0xd9ca58;
    cIEventHeader *DeletingDestructor(uint32_t flags); // 0x154e80
    const void *mVTable; // +0
};
static_assert(sizeof(cIEventHeader) == 4, "layout: the size 0x154e80 frees");
} // namespace nGZEvent

namespace nTSCASCore {
struct cNameMap {
    static constexpr uint32_t kVTable = 0xeba0d4;
    cNameMap *DeletingDestructor(uint32_t flags); // 0xce0920
    const void *mVTable; // +0
};
static_assert(sizeof(cNameMap) == 4, "layout: the size 0xce0920 frees");

struct cTSCASLockedObjectManager {
    cTSCASLockedObjectManager *DeletingDestructor(uint32_t flags); // 0xcfe720; destructor 0xcfe450
    const void *mVTable;
    uint8_t mRest[0xc - 4];
};
static_assert(sizeof(cTSCASLockedObjectManager) == 0xc, "layout: the size 0xcfe720 frees");
} // namespace nTSCASCore

namespace nPathPlanner {
struct cAStarSearch {
    cAStarSearch *DeletingDestructor(uint32_t flags); // 0x20ac60; destructor 0x20ab60
    const void *mVTable;
    uint8_t mRest[0x38 - 4];
};
static_assert(sizeof(cAStarSearch) == 0x38, "layout: the size 0x20ac60 frees");
} // namespace nPathPlanner

struct cSLTokenFieldBase {
    cSLTokenFieldBase *DeletingDestructor(uint32_t flags); // 0x9b9f20; destructor 0x9b9bb0
    const void *mVTable;
    uint8_t mRest[0x2c - 4];
};
static_assert(sizeof(cSLTokenFieldBase) == 0x2c, "layout: the size 0x9b9f20 frees");

// nGZDrawSoftRender::cSpanRenderBase (0x8c92b0): moved to src/gz/nGZDrawSoftRender.* (lane NS).

struct cTSDelauneyMesh {
    cTSDelauneyMesh *DeletingDestructor(uint32_t flags); // 0xb54680; destructor 0xb545f0
    const void *mVTable;
    uint8_t mRest[0x7c - 4];
};
static_assert(sizeof(cTSDelauneyMesh) == 0x7c, "layout: the size 0xb54680 frees");

// cTSBaseParticlesDescription, cTSMetaParticlesDescription and cTSParticlesDescription (0xbfbb30, 0xbed3f0, 0xbe2bb0):
// moved to src/ts/cTSParticlesDescription.* (lane IX).

// nTSSG::cFloorColorGrid (0x3c4870): moved to src/sg/cFloorColorGrid.* (coordinator, after lane 261109i).

struct cTSSliceImposterCreator {
    cTSSliceImposterCreator *DeletingDestructor(uint32_t flags); // 0xb51410; destructor 0xb51300
    const void *mVTable;
    uint8_t mRest[0x88 - 4];
};
static_assert(sizeof(cTSSliceImposterCreator) == 0x88, "layout: the size 0xb51410 frees");

// --- size-built (src/misc/DeletingDestructors.size.cpp) ---

struct HTTPMessage {
    HTTPMessage *DeletingDestructor(uint32_t flags); // 0x98da4f; destructor 0x98d879
    const void *mVTable;
    uint8_t mRest[0xb4 - 4];
};
static_assert(sizeof(HTTPMessage) == 0xb4, "layout: the size 0x98da4f frees");

struct TickerMessage {
    TickerMessage *DeletingDestructor(uint32_t flags); // 0x960123; destructor 0x9600ac
    const void *mVTable;
    uint8_t mRest[0x4c - 4];
};
static_assert(sizeof(TickerMessage) == 0x4c, "layout: the size 0x960123 frees");

// The process memory statistics object: its destructor (0x1bab4e) only stores its base's vtable 0xda31e0.
struct cRZProcessMemoryInfo {
    static constexpr uint32_t kBaseVTable = 0xda31e0;
    __declspec(noinline) void Destruct();                    // 0x1bab4e
    cRZProcessMemoryInfo *DeletingDestructor(uint32_t flags); // 0x1babf3
    const void *mVTable;
    uint8_t mRest[0x18 - 4];
};
static_assert(sizeof(cRZProcessMemoryInfo) == 0x18, "layout: the size 0x1babf3 frees");

// Its destructor (0x7d86ef) only stores its base's vtable 0xe02c94.
struct cTSRampGenerator {
    static constexpr uint32_t kBaseVTable = 0xe02c94;
    __declspec(noinline) void Destruct();                // 0x7d86ef
    cTSRampGenerator *DeletingDestructor(uint32_t flags); // 0x7d86f6
    const void *mVTable;
    uint8_t mRest[0x14 - 4];
};
static_assert(sizeof(cTSRampGenerator) == 0x14, "layout: the size 0x7d86f6 frees");

// UI controls of the Sim panels.
struct cTSUIDaysOfWeekCtrl {
    cTSUIDaysOfWeekCtrl *DeletingDestructor(uint32_t flags); // 0x77f3c5; destructor 0x77f370
    const void *mVTable;
    uint8_t mRest[0x2c - 4];
};
static_assert(sizeof(cTSUIDaysOfWeekCtrl) == 0x2c, "layout");

struct cTSUILearnedBehaviorsListCtrl {
    cTSUILearnedBehaviorsListCtrl *DeletingDestructor(uint32_t flags); // 0x78007b; destructor 0x780021
    const void *mVTable;
    uint8_t mRest[0x3c - 4];
};
static_assert(sizeof(cTSUILearnedBehaviorsListCtrl) == 0x3c, "layout");

struct cTSUISimViewCtrl {
    cTSUISimViewCtrl *DeletingDestructor(uint32_t flags); // 0x7c3f82; destructor 0x7c3f2c
    const void *mVTable;
    uint8_t mRest[0xc - 4];
};
static_assert(sizeof(cTSUISimViewCtrl) == 0xc, "layout");

struct cTSUISimpleThumbnail {
    cTSUISimpleThumbnail *DeletingDestructor(uint32_t flags); // 0x7c5cfa; destructor 0x7c5ca4
    const void *mVTable;
    uint8_t mRest[0xc - 4];
};
static_assert(sizeof(cTSUISimpleThumbnail) == 0xc, "layout");

struct cTSUISkillsCtrl {
    cTSUISkillsCtrl *DeletingDestructor(uint32_t flags); // 0x7c3555; destructor 0x7c3514
    const void *mVTable;
    uint8_t mRest[0x2c - 4];
};
static_assert(sizeof(cTSUISkillsCtrl) == 0x2c, "layout");

struct cTSUITraitsCtrl {
    cTSUITraitsCtrl *DeletingDestructor(uint32_t flags); // 0x7c6acb; destructor 0x7c6a8a
    const void *mVTable;
    uint8_t mRest[0x18 - 4];
};
static_assert(sizeof(cTSUITraitsCtrl) == 0x18, "layout");

// Also cTSUISeasonMultiButton's slot 0.
struct cTSUIMultiButton {
    cTSUIMultiButton *DeletingDestructor(uint32_t flags); // 0x7d45fa; destructor 0x7dc59a
    const void *mVTable;
    uint8_t mRest[0x10 - 4];
};
static_assert(sizeof(cTSUIMultiButton) == 0x10, "layout");

// The UI modes (live, build, buy, CAS, neighbourhood...): cITSUIModeObject and its twelve modes, each a vtable whose
// only slot is the deleting destructor. Each mode's destructor ends by storing the cITSUIModeObject vtable.
struct cITSUIModeObject {
    static constexpr uint32_t kVTable = 0xde1f84;
    cITSUIModeObject *DeletingDestructor(uint32_t flags); // 0x61aa47
    const void *mVTable; // +0
};
static_assert(sizeof(cITSUIModeObject) == 4, "layout: the size 0x61aa47 frees");

#define TS2_UI_MODE(Name, Size)                                                                                        \
    struct Name : cITSUIModeObject {                                                                                   \
        Name *DeletingDestructor(uint32_t flags);                                                                      \
        uint8_t mRest[Size - 4];                                                                                       \
    };                                                                                                                 \
    static_assert(sizeof(Name) == Size, "layout: the size its deleting destructor frees")

// Live and Painting hold no fields of their own (4 bytes); their RVAs are in DeletingDestructors.size.cpp.
struct cTSUIModeLive : cITSUIModeObject {
    cTSUIModeLive *DeletingDestructor(uint32_t flags);
};
struct cTSUIModePainting : cITSUIModeObject {
    cTSUIModePainting *DeletingDestructor(uint32_t flags);
};
TS2_UI_MODE(cTSUIModeAlbum, 8);
TS2_UI_MODE(cTSUIModeBuild, 0xc);
TS2_UI_MODE(cTSUIModeBuy, 0xc);
TS2_UI_MODE(cTSUIModeCAS, 8);
TS2_UI_MODE(cTSUIModeCinematic, 0x34);
TS2_UI_MODE(cTSUIModeNeighborhood, 8);
TS2_UI_MODE(cTSUIModeNone, 0x10);
TS2_UI_MODE(cTSUIModeOptions, 8);
TS2_UI_MODE(cTSUIModeReward, 0xc);
#undef TS2_UI_MODE
static_assert(sizeof(cTSUIModeLive) == 4 && sizeof(cTSUIModePainting) == 4, "layout");
