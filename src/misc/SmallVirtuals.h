#pragma once
#include <cstddef>
#include <cstdint>

#include "game/owned.h"
#include "rz/RZFrameworkHelpers.h"

// The last virtual slots of small classes (RVAs 1.18.0.187), each the only slot its class still lacked. Hook-shaped
// (docs/standalone-shape.md): the game builds these objects, so the members are hooked at their RVAs; only the fields
// the members touch are named.

// --- speed-built (src/misc/SmallVirtuals.cpp) ---

namespace nGZEvent {
// The empty event: a vtable and nothing else.
struct cNullEvent {
    static constexpr uint32_t kVTable = 0xd9ca78;
    static constexpr uint32_t kEventType = 0x7b256782;
    uint32_t GetEventType() const;       // slot 3, 0x157710: the type id
    void *CloneInto(void *dest) const;   // slot 6, 0x1559b0 (ret 4): constructs a cNullEvent at dest, returns dest
    const void *mVTable;                 // +0
};
static_assert(sizeof(cNullEvent) == 4, "layout");
} // namespace nGZEvent

namespace nTSSG {
// The scene-graph object filter: accepts the nodes whose query slot 48 answers false for the flag 0xf.
struct cTSSGObjectsFilter {
    // Slot 0, 0x3ac280 (ret 4). The original widens the result to all of eax (sete into a cleared register).
    uint32_t IsIncluded(void *node) const;
    const void *mVTable;               // +0
};

// A polygon of the scene graph's triangulated surfaces: slot 0 hands its id to the owner's slot 36 three times
// (meaning tentative: one call per corner).
struct cBasePolygon {
    void NotifyOwner(); // slot 0, 0x413360 (ret 0)
    const void *mVTable; // +0
    uint32_t mId;        // +4
    void *mOwner;        // +8
};
static_assert(offsetof(cBasePolygon, mOwner) == 8, "layout");
} // namespace nTSSG

namespace nGZGraphic4 {
// The graphics layer's exception: an error code and a message per code (our own literals, the image's texts).
// The slot's view of the class app/WinMain.h declares (with the owned vtable ts2_rva_d9cd70; ours is
// ts2_own_vt_d9cd70 in SmallVirtuals.cpp).
struct cError {
    const char *GetMessage() const; // slot 0, 0x15a900: codes 0, 1, 2 have their own text, others a generic one
    const void *mVTable;            // +0
    int32_t mCode;                  // +4
};
} // namespace nGZGraphic4

namespace nTSSG {
// Accepts a node unless it is an object imposter candidate (slot 25 with the id 0xe519c92c) whose slot 26 is non-zero.
struct cObjectImposterFilter {
    uint32_t IsIncluded(void *node) const; // slot 0, 0x3f8950 (ret 4): 1 or 0 in eax
    const void *mVTable;                   // +0
};
} // namespace nTSSG

// cBranchNodesIIDFilter's vtable (owned static: the image's in the hooked game; our ts2_own_vt_e7cf54 in SmallVirtuals.cpp,
// aliased in an image-free build).
TS2_OWNED_STATIC(const OwnedVTable, e7cf54);

namespace nRZSceneGraph {
// Branch node filters: by interface id (slot 25 of the node), and by a mask bit (slot 48) then interface id.
struct cBranchNodesIIDFilter {
    static constexpr uint32_t kVTable = 0xe7cf54; // the base's (what the destructor 0xaea7e0 leaves)
    uint32_t IsIncluded(void *node) const;        // slot 0, 0xaeacb0 (ret 4): node lacks the interface
    __declspec(noinline) void Destruct();         // 0xaea7e0: the base vtable store
    cBranchNodesIIDFilter *DeletingDestructor(uint32_t flags); // slot 1, 0xaea800
    const void *mVTable; // +0
    uint32_t mIID;       // +4
};
static_assert(sizeof(cBranchNodesIIDFilter) == 8, "layout: the size 0xaea800 frees");

struct cBranchNodesMaskBitAndIIDFilter {
    static constexpr uint32_t kVTable = 0xe7cf60;
    uint32_t IsIncluded(void *node) const; // slot 0, 0xaeacd0 (ret 4): 2 when the mask bit is set, else as above
    __declspec(noinline) void Destruct();  // 0xaea7f0
    cBranchNodesMaskBitAndIIDFilter *DeletingDestructor(uint32_t flags); // slot 1, 0xaea820
    const void *mVTable; // +0
    uint32_t mMaskBit;   // +4
    uint32_t mIID;       // +8
};
static_assert(sizeof(cBranchNodesMaskBitAndIIDFilter) == 0xc, "layout: the size 0xaea820 frees");
} // namespace nRZSceneGraph

// A directory cache (an anonymous-namespace class of the persist code): the map at +4 is first passed to 0x1f4aa0
// (game code), then destroyed: it is a std::unordered_map<std::string, std::string> of the game's STL, whose
// destructor 0x1f1c00 is the library's (db/library-matches.jsonl), so ours is used.
struct cTSDirectoryCache {
    static constexpr uint32_t kVTable = 0xda6db0;
    cTSDirectoryCache *DeletingDestructor(uint32_t flags); // slot 0, 0x1f20a0; member functions 0x1f4aa0, then the map's destructor
    const void *mVTable;
    alignas(4) uint8_t mEntries[0x24 - 4]; // +4 std::unordered_map<std::string, std::string>
};
static_assert(sizeof(cTSDirectoryCache) == 0x24, "layout: the size 0x1f20a0 frees");

// --- size-built (src/misc/SmallVirtuals.size.cpp) ---

namespace cTSWinProcUITool {
// Finds the n-th object of a type: counts the objects of type mType (their slot 78) and accepts the mIndex-th one
// (counting from 0), or every one when mIndex is negative.
struct cSearchFilter {
    bool IsIncluded(void *object); // slot 0, 0x676aac (ret 4)
    const void *mVTable; // +0
    int32_t mType;       // +4
    int32_t mIndex;      // +8
    int32_t mCount;      // +0xc
};
} // namespace cTSWinProcUITool

// An anonymous-namespace filter of the UI: objects of type mType (slot 78) whose slot 87 answers true.
struct cTSAnchorFilter {
    bool IsIncluded(void *object) const; // slot 0, 0x74367d (ret 4)
    const void *mVTable; // +0
    int32_t mType;       // +4
};

// The terrain brush tool: slot 7 sets the brush position and its previous position to the same 8 bytes.
struct cTSUserToolTerrainBrush {
    struct Point {
        int32_t x, y;
    };
    cTSUserToolTerrainBrush *DeletingDestructor(uint32_t flags); // slot 0, 0x79bcea; destructor 0x79bcb0
    void SetPosition(const Point *point);                       // slot 7, 0x79c51d (ret 4)
    const void *mVTable;  // +0
    uint8_t _gap4[0x34];  // +4
    Point mPosition;      // +0x38
    Point mLastPosition;  // +0x40
    uint8_t _gap48[0x1c]; // +0x48
};
static_assert(offsetof(cTSUserToolTerrainBrush, mLastPosition) == 0x40, "layout");
static_assert(sizeof(cTSUserToolTerrainBrush) == 0x64, "layout: the size 0x79bcea frees");

// A socket error: the system error code and the code translated to the framework's.
struct cRZSocketError {
    void SetError(int32_t systemCode);  // slot 0, 0x99a83b (ret 4): translation 0x999e59 (original)
    // Slot 2, 0x99a0ac (ret 4): formats the error into the string `text` (its slot 31, cdecl, with the format string
    // at 0xe5e748, the system code and its text from 0x99a0d0, original); returns what slot 31 returns.
    uint32_t Describe(void *text);
    const void *mVTable;  // +0
    int32_t mCode;        // +4
    int32_t mSystemCode;  // +8
};

// cRZLock (0x142248, the deleting destructor) is rz/cRZLock.h's.

// The game tips filter (an anonymous-namespace class): accepts a tip whose category bit (its slot 4) is set in the mask.
struct cGameTipFilter {
    bool IsIncluded(void *tip) const; // slot 0, 0x6549fa (ret 4): false for a null tip
    const void *mVTable;              // +0
    uint32_t mCategoryMask;           // +4
};

// The Sim portrait control of the UI.
struct cTSUISimCtrl {
    cTSUISimCtrl *DeletingDestructor(uint32_t flags); // slot 0, 0x77f908; destructor 0x77f779
    // Slot 1, 0x7d4f42 (ret 8): holds the Sim (AddRef new, Release old) and, when `refresh` is set, calls its own
    // slot 3.
    void SetSim(void *sim, bool refresh);
    const void *mVTable; // +0
    RZUnknownSlot mSim;  // +4
};
static_assert(sizeof(cTSUISimCtrl) == 8, "layout: the size 0x77f908 frees");
