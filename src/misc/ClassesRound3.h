#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

#include "rz/cRZIStream.h"
#include "rz/cRZString.h"
#include "simantics/cTSTreeTableEntry.h"

// Lane IC: the last virtual slots of nearly complete classes, round 3 (RVAs 1.18.0.187). Hook-shaped
// (docs/standalone-shape.md): the game builds these objects, so the members are hooked at their RVAs and store the
// game's vtables, as the originals do; only the fields the members touch are named. cTSTriple<short> is the exception:
// it is already a real class (src/simantics/cTSTreeTableEntry.h), so its deleting destructor gets an adapter.

// --- speed-built (src/misc/ClassesRound3.cpp) ---

// cTSTriple<short>'s deleting destructor 0x4311c0 (slot 0 of the base and of both advertisement vtables): the class's
// destructor (our vtable store) and the sized delete of its 12 bytes. Objects the game built still carry its vtables.
struct cTSTripleAdapters {
    using Self = cTSTriple<int16_t>;
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
};

namespace nTSWorld {
// A roof on a lot: its footprint is the rectangle between two corners (either order), widened by the eaves on the sides
// that have them. Which sides have eaves depends on the roof type (1 and 0x12 behave alike, 2 the other way round) and
// three flags: +0x25 (the orientation: tentative) and +0x26 / +0x27 (the end without eaves along x / along y: tentative).
struct tRoofObject {
    int32_t GetXVertexCount() const; // slot 0, 0x3f3e00: Round(2 * (max x - min x)) + 1 (half-tile steps)
    int32_t GetYVertexCount() const; // slot 1, 0x3f3e40: the same along y

    __declspec(noinline) float GetMaxX() const; // 0x3efff0
    __declspec(noinline) float GetMaxY() const; // 0x3f0060
    __declspec(noinline) float GetMinX() const; // 0x3f00d0
    __declspec(noinline) float GetMinY() const; // 0x3f0140

    const void *mVTable;  // +0
    uint32_t mUnknown4;   // +4
    float mX0;            // +8
    float mY0;            // +0xc
    uint32_t mUnknown10;  // +0x10
    float mX1;            // +0x14
    float mY1;            // +0x18
    uint32_t mUnknown1c;  // +0x1c
    int32_t mType;        // +0x20
    uint8_t mHasEaves;    // +0x24 (bytes tested against 0)
    uint8_t mFlag25;      // +0x25
    uint8_t mNoEavesX;    // +0x26
    uint8_t mNoEavesY;    // +0x27
    uint32_t mUnknown28[2]; // +0x28
    float mEavesX;        // +0x30
    float mEavesY;        // +0x34
};
static_assert(offsetof(tRoofObject, mType) == 0x20 && offsetof(tRoofObject, mEavesY) == 0x34, "layout");
} // namespace nTSWorld

namespace nTSSG {
// The scene graph's query strategy manager (0x50 bytes; the destructor 0x2c0a10 is still the original's).
struct cQueryStrategyManager {
    static constexpr uint32_t kIid = 0x6988a371;
    bool QueryInterface(uint32_t iid, void **out);              // slot 0, 0x2c10f0 (ret 8)
    uint32_t Release();                                         // slot 2, 0x2c1190: slot 21 (delete) at zero
    cQueryStrategyManager *DeletingDestructor(uint32_t flags); // slot 21, 0x2c0c00
    const void *mVTable;  // +0
    uint32_t mRefCount;   // +4
    uint8_t mRest[0x50 - 8];
};
static_assert(sizeof(cQueryStrategyManager) == 0x50, "layout: the size 0x2c0c00 frees");

// A frame-buffer readback (0xf8c bytes; the destructor 0x401fb0 is still the original's).
struct cImageReadback {
    uint32_t AddRef();                             // slot 1, 0x4020e0
    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0x402cb0 (ret 8): only id 1; out is not checked
    uint32_t Release();                            // slot 2, 0x403e70: destructor and delete when the count is 1
    const void *mVTable;
    uint8_t mUnknown4[0x4c - 4];
    uint32_t mRefCount; // +0x4c
    uint8_t mRest[0xf8c - 0x50];
};
static_assert(offsetof(cImageReadback, mRefCount) == 0x4c && sizeof(cImageReadback) == 0xf8c, "layout");
} // namespace nTSSG

// --- size-built (src/misc/ClassesRound3.size.cpp) ---

// The sound system's completion callback object: a vtable and a reference count (8 bytes).
struct cCompletionObj {
    static constexpr uint32_t kVTable = 0xe81100;
    static constexpr uint32_t kIid = 0x1b3637c6;
    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0xb2523a (ret 8): kIid, then 1
    uint32_t Release();                            // slot 2, 0xb25272: vtable store and delete at zero
    const void *mVTable; // +0
    uint32_t mRefCount;  // +4
};
static_assert(sizeof(cCompletionObj) == 8, "layout: the size 0xb25272 frees");

// The null input stream: every read fails and marks the stream failed (slot 18 returns the flag).
struct cRZNullIStream : cRZIStream {
    static constexpr uint32_t kVTable = 0xda2ae8;
    bool Skip(uint32_t count);                       // slot 3, 0x1b672b (ret 4)
    bool GetVoid(void *dst, uint32_t size);          // slot 17, 0x1b5f33 (ret 8)
    __declspec(noinline) void Destruct();            // 0x1b52c2: own vtable, then the base's destructor
    cRZNullIStream *DeletingDestructor(uint32_t flags); // slot 21, 0x1b57ad
    bool mFailed; // +0x10
};
static_assert(sizeof(cRZNullIStream) == 0x14, "layout: the size 0x1b57ad frees");

namespace nGZEvent {
// The connection-changed event: five words after the vtable (meaning not established).
struct cConnectionChangedEvent {
    static constexpr uint32_t kVTable = 0xda239c;
    static constexpr uint32_t kBaseVTable = 0xd9ca58; // cIEventHeader's
    static constexpr uint32_t kEventType = 0xdb27d6d4;
    cConnectionChangedEvent *DeletingDestructor(uint32_t flags);            // slot 0, 0x1aa749
    cConnectionChangedEvent *Assign(const cConnectionChangedEvent &other);  // slot 1 (stub 0x1aa849), 0x1aa6c4 (ret 4)
    uint32_t GetEventType() const;                                          // slot 3, 0x1aafc5
    void *CloneInto(void *dest) const;                                      // slot 6, 0x1aaab0 (ret 4)
    __declspec(noinline) cConnectionChangedEvent *CopyConstruct(const cConnectionChangedEvent &other); // 0x1aa5d2
    const void *mVTable;  // +0
    uint32_t mData[5];    // +4
};
static_assert(sizeof(cConnectionChangedEvent) == 0x18, "layout: the size 0x1aa749 frees");
} // namespace nGZEvent

// Inventory token filters (anonymous-namespace classes, one slot each, ret 4): token slot 8 is its flag word (bit 0 set
// and bits 1-2 clear: an ordinary owned object, tentative), slot 6 its GUID, slot 15 its count; the filter's +4 is the
// object manager (slot 14: the object for a GUID), the object's slot 4 its data, whose slot 3 reads a data field.
struct cTSFridgeTokenFilter {
    bool Accept(void *token); // 0x6fd791: object field 0x4b is 0x7c
    const void *mVTable;
    void *mObjectManager; // +4
};
struct cTSJuicerTokenFilter {
    bool Accept(void *token); // 0x700698: field 0x4b is 0x7c with field 0x4c 0, or field 0x4b is 0xaa
    const void *mVTable;
    void *mObjectManager; // +4
};
struct cTSLeftoverTokenFilter {
    bool Accept(void *token); // 0x700c64: count 1 and an object with data (field 0x4b and maybe 0x4c read, unused)
    const void *mVTable;
    void *mObjectManager; // +4
};
// The food judging filter: accepts a non-zero id once, taking it out of its map.
struct cTSFoodJudgingTokenFilter {
    bool Accept(int32_t id); // 0x7150cf
    const void *mVTable;
    uint8_t mUnknown4[0xc];
    std::map<int32_t, int32_t> mPending; // +0x10
};
static_assert(offsetof(cTSFoodJudgingTokenFilter, mPending) == 0x10, "layout");

// The sound system's hit semaphore (0x2c bytes): a count and a cRZString name. Release is speed-built (ClassesRound3.cpp).
struct cHitSem {
    static constexpr uint32_t kVTable = 0xe81864;
    static constexpr uint32_t kIid = 0xddcf3bd7;
    bool QueryInterface(uint32_t iid, void **out);  // slot 0, 0xb2e266 (ret 8): 1, then kIid
    uint32_t Release();                             // slot 2, 0xb2e3d0: slot 7 (delete) at count 1
    bool GiveName(void *target);                    // slot 4, 0xb2d992 (ret 4): target's slot 17 with the name
    cHitSem *DeletingDestructor(uint32_t flags);    // slot 7, 0xb2d2ba
    __declspec(noinline) void Destruct();           // 0xb2d191: vtable, then the name's
    const void *mVTable;  // +0
    uint32_t mRefCount;   // +4
    cRZString mName;      // +8
    uint32_t mUnknown28;  // +0x28
};
static_assert(sizeof(cHitSem) == 0x2c, "layout: the size 0xb2d2ba frees");

// The FTP client's low-level connection (0xb1c bytes; destructor 0x994ad5 and shutdown 0x998514 still the original's).
struct cRZFTPClientLowLevel {
    uint32_t Release();                                       // slot 2, 0x997ea6: above 1 decrements, else slot 4
    cRZFTPClientLowLevel *DeletingDestructor(uint32_t flags); // slot 3, 0x994c3e
    void Destroy();                                           // slot 4, 0x99766b: shutdown, then delete this (slot 3)
    const void *mVTable;  // +0
    uint32_t mRefCount;   // +4
    uint8_t mRest[0xb1c - 8];
};
static_assert(sizeof(cRZFTPClientLowLevel) == 0xb1c, "layout: the size 0x994c3e frees");

// --- speed-built (ClassesRound3.cpp) ---

namespace nPathPlanner {
// The path planner's straight-line heuristic: the 2D distance from (x, y) to the goal.
struct cDistancePathHeuristic {
    // Slot 0, 0x20d380 (ret 0x14): the two words after (x, y) are not read.
    float Estimate(float x, float y, uint32_t unused2, uint32_t unused3, const float *goal) const;
    const void *mVTable;
};
} // namespace nPathPlanner

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// A person's motive increment record (serialised): the motive, then two floats.
struct cTSPersonMotiveInc {
    bool Write(ifaces::cIGZOStream *stream) const; // slot 0, 0x5243a0 (ret 4): Sint32, Float32 twice; no error
    bool Read(ifaces::cIGZIStream *stream);        // slot 1, 0x51e8e0 (ret 4): the same order
    const void *mVTable;  // +0
    int16_t mMotive;      // +4
    uint16_t mPad6;
    float mValue8;        // +8
    float mValueC;        // +0xc
};
static_assert(sizeof(cTSPersonMotiveInc) == 0x10, "layout");

// A person's object record (serialised): an object reference written through the cached singleton's slot 26 object, a
// short and a flag.
struct cTSPersonObjectRecord {
    bool Write(ifaces::cIGZOStream *stream) const; // slot 0, 0x5243f0 (ret 4)
    bool Read(ifaces::cIGZIStream *stream);        // slot 1, 0x51e930 (ret 4)
    const void *mVTable;  // +0
    void *mObject;        // +4: a counted reference (AddRef slot 1, Release slot 2)
    int16_t mValue8;      // +8
    bool mFlagA;          // +0xa
};
static_assert(offsetof(cTSPersonObjectRecord, mFlagA) == 0xa, "layout");

namespace nTSSG {
// A display list render strategy's per-object state (0x64 bytes; the destructor 0x36f5c0 is still the original's).
struct cDisplayListDynamicRenderStrategyState {
    static constexpr uint32_t kIid = 0x6d548978;
    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0x373d20 (ret 8): 1 and kIid, null out refused
    uint32_t AddRef();                             // slot 1, 0x370cc0
    uint32_t Release();                            // slot 2, 0x373d50: above 1 decrements, else slot 3 (delete)
    cDisplayListDynamicRenderStrategyState *DeletingDestructor(uint32_t flags); // slot 3, 0x36fbb0
    const void *mVTable;
    uint8_t mUnknown4[0x60 - 4];
    uint32_t mRefCount; // +0x60
};
static_assert(sizeof(cDisplayListDynamicRenderStrategyState) == 0x64, "layout: the size 0x36fbb0 frees");
} // namespace nTSSG

// --- size-built (ClassesRound3.size.cpp) ---

// The image file filter (an anonymous-namespace class compiled into two files: both copies share these bodies, which
// nothing else references; AddRef / Release are the shared "return 1").
struct cTSImgFileFilter {
    static constexpr uint32_t kIid = 0x50edec76;
    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0x6f8f52 (ret 8): only kIid; null out refused
    bool Accept(void *file);                       // slot 3, 0x6f877e (ret 4): the inner filter's slot 10, if any
    const void *mVTable; // +0
    void *mInner;        // +4: another file filter (or null: nothing accepted)
};

// The FTP client (0x5c bytes; the destructor 0x994a69, shutdown 0x9984f3 still the original; DoMessage 0x99580e not done: its progress clamp does not byte-match and its callee 0x9980f2 blocks a difftest).
struct cRZFTPClient {
    static constexpr uint32_t kIid = 0x452294aa;
    static constexpr uint32_t kProgressMessage = 0x5738692f;
    bool QueryInterface(uint32_t iid, void **out);    // slot 0, 0x997c9e (ret 8): kIid, then 1
    uint32_t Release();                               // slot 2, 0x997e91: above 1 decrements, else slot 5
    cRZFTPClient *DeletingDestructor(uint32_t flags); // slot 4, 0x994c1f
    void Destroy();                                   // slot 5, 0x997654: shutdown, then delete this (slot 4)
    const void *mVTable;  // +0
    uint32_t mRefCount;   // +4
    uint8_t mUnknown8[8];
    void *mConnection10;  // +0x10: non-null while connected (tentative)
    uint8_t mUnknown14[0x58 - 0x14];
    uint8_t *mTransfer58; // +0x58: its +0xc is the transfer the progress goes to
};
static_assert(sizeof(cRZFTPClient) == 0x5c, "layout: the size 0x994c1f frees");
