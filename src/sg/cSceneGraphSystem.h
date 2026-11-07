#pragma once
// nGZSceneGraph::cSceneGraphSystem (lane OM, scenegraph N1 step 4): the scene graph's system service (service id 0xe5074b6b,
// priority 200000), class 0xe5074b6b of nGZSceneGraph's COM director. It keeps the set of live scene nodes (each
// cSceneNode adds itself in its constructor and removes itself in its destructor) and, from Init to Shutdown, the scene
// graph's managers: the singletons (material manager, texture manager, string pool: function-local statics their getters
// build), the scene graph factory (a static 0xa4d660 builds), and four COM objects plus the material parser that Init
// makes. Standalone shape:
//
//   class cSceneGraphSystem : public cISceneGraphSystemPrivate, public rz::cRZSystemService
//
//   +0x00 cISceneGraphSystemPrivate (vtable 0xe6c474, 15 slots)
//   +0x04 cRZSystemService (cIGZSystemService 0xe6c4b4, 13 slots: Init 8, Shutdown 9, OnTick 10 are the class's own), its
//         cRZUnknownMT at +0x08 (0xe6c4ec, 6 slots: 3 the deleting destructor through the thunk 0xa4c9d8), count +0x0c,
//         id +0x10, priority +0x14, tick priority +0x18, running +0x1c
//   +0x20 mSceneNodes      std::set<cISceneNode *> (head, size): the game's std::set is our toolchain's (0x14-byte nodes:
//                          left, parent, right, colour, nil flag, the value at +0x10), and so are its insert and erase
//   +0x28 mFactory         the scene graph factory's cISceneGraphFactory face (counted through its virtual cIGZUnknown)
//   +0x2c mRenderGroups    COM class 0x4973819d (cRenderGroupManager), interface 0xc9737f2c
//   +0x30 mDisplayList     COM class 0x49d02f15 (cDisplayList), interface 0x49d02f0b
//   +0x34 mFileParser      COM class 0x5cf9459a (cFileParser, ours), interface 0x1cf94583
//   +0x38 mMaterialParser  the cMaterialParser Init builds (0x304 bytes, constructor 0xacec90), its cIMaterialParser face
//   +0x3c mLODLevels       COM class 0x2c674b2c (cLODLevelsStore), interface 0x2c674b25
//
// The switch: the factory 0xa4cb40 (registered by address in the director's AddCls steps) is hooked to Create. The game's
// constructor 0xa4c6f0 has the factory as its one caller and the game's vtables are stored by nothing else (the
// constructor and the destructor 0xa4c840), so with the factory ours every slot body is in effect through callers. Init
// is also the bisect switch for the singletons and the material parser (they stay game code, built through their
// original getters and constructor).
//
// cSceneNodeIterator: the iterator GetSceneNodes (slot 3) hands out (12 bytes, vtable 0xe6c508, cIGZForwardIterator<
// cISceneNode *>; pool 0x10f617c, a cRZFastFixedAllocator2<0xc>). Made only by GetSceneNodes and its own Clone.
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cLODLevels.h" // cILODLevelsStore
#include "sg/cSceneGraphFactory.h" // cISceneGraphFactory: the factory's face (lane RC)
#include "sg/cIDisplayList.h"

#include <cstddef>
#include <cstdint>
#include <set>

namespace nGZCommandParser {
class cIFileParser;
}

namespace nGZSceneGraph {

// Game classes the system holds, still game code (opaque here; their slots are called by number where named nowhere).
class cMaterialManager;   // the singleton 0xacc040 returns (vtable 0xe78514)
class cTextureManager;    // the singleton 0xade2e0 returns (vtable 0xe7c778)
class cStringPool;        // the singleton 0xadf8b0 returns (vtable 0xe7c844)
class cMaterialParser;    // 0x304 bytes, constructor 0xacec90

// The interfaces Init asks the COM framework for (only cIGZUnknown's slots are known by name here).
class cIRenderGroupManager : public cIGZUnknown {}; // class 0x4973819d (cRenderGroupManager), iid 0xc9737f2c
// cIDisplayList: sg/cIDisplayList.h (class 0x49d02f15, cDisplayList; iid 0x49d02f0b).

// The material parser's cIMaterialParser face (the parser's vtable at +0x2fc), reached through the parser's vbptr at +8;
// its cIGZUnknown and cIBaseParser are virtual bases through the face's vbptr at +4 (entries 1 and 2).
class cIMaterialParser;

class cISceneNodeIterator;

// What GetSceneNodes returns (by hidden pointer): a cursor (a clone of begin) and the two ends, each counted.
struct cSceneNodeRange {
    cISceneNodeIterator *mCurrent;
    cISceneNodeIterator *mBegin;
    cISceneNodeIterator *mEnd;
};
static_assert(sizeof(cSceneNodeRange) == 0xc, "cSceneNodeRange size");

// cIGZIterator / cIGZForwardIterator<cISceneNode *, cGZElementByValue<cISceneNode *>> (vtable 0xe6c508, 8 slots).
class cISceneNodeIterator : public cIGZUnknown {
public:
    virtual cISceneNodeIterator *Clone() = 0;                    // 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cISceneNodeIterator *other) = 0;  // 4: the same place
    virtual cISceneNode *Get() = 0;                              // 5
    virtual void Next() = 0;                                     // 6
};

// nGZSceneGraph::cISceneGraphSystem (iid 0x25074b4d): slots 3-12.
class cISceneGraphSystem : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x25074b4d;
    virtual cSceneNodeRange GetSceneNodes() = 0;              // 3 (hidden result pointer, ret 4)
    virtual cISceneGraphFactory *GetFactory() = 0;            // 4 (folded 0x15f390, +0x28)
    virtual cMaterialManager *GetMaterialManager() = 0;       // 5 (0xa4d030: a jump to the singleton's getter)
    virtual cTextureManager *GetTextureManager() = 0;         // 6 (0xa4d300)
    virtual cIRenderGroupManager *GetRenderGroups() = 0;      // 7 (folded 0x147770, +0x2c)
    virtual cStringPool *GetStringPool() = 0;                 // 8 (0xa4d2f0)
    virtual cIDisplayList *GetDisplayList() = 0;              // 9 (folded 0x1457d0, +0x30)
    virtual nGZCommandParser::cIFileParser *GetFileParser() = 0; // 10 (folded 0x147800, +0x34)
    virtual cIMaterialParser *GetMaterialParser() = 0;        // 11 (folded 0x1694c0, +0x38)
    virtual cILODLevelsStore *GetLODLevels() = 0;             // 12 (folded 0x1674b0, +0x3c)
};

// nGZSceneGraph::cISceneGraphSystemPrivate (iid 0x894978cd, the interface cSceneNode asks the framework for): 13 / 14.
class cISceneGraphSystemPrivate : public cISceneGraphSystem {
public:
    static constexpr uint32_t kIid = 0x894978cd;
    virtual void AddSceneNode(cISceneNode *node) = 0;    // 13 (0xa4ca40): into the set (once)
    virtual void RemoveSceneNode(cISceneNode *node) = 0; // 14 (0xa4d0b0): out of the set (0x17bc20, the set's erase)
};

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f617c); // cSceneNodeIterator's pool (operator new / delete)
class cSceneNodeIterator : public cISceneNodeIterator {
public:
    static constexpr uint32_t kSize = 0xc;
    static constexpr uint32_t kPool = 0x10f617c; // cRZFastFixedAllocator2<0xc> (pages of 0x800 bytes)
    using Position = std::set<cISceneNode *>::const_iterator;

    explicit cSceneNodeIterator(Position at) : mRefCount(0), mAt(at) {} // 0xa4c820 (from a tree position)
    cSceneNodeIterator(const cSceneNodeIterator &other) : mRefCount(0), mAt(other.mAt) {} // 0xa4c800 (count 0)
    cSceneNodeIterator &operator=(const cSceneNodeIterator &) = delete;
    virtual ~cSceneNodeIterator() = default; // 0xa4c920; slot 7 the deleting destructor 0xa4ca00

    // The class's pool (the game's 0xa4c930 / 0xa4c960, inline in the original): blocks of 0xc bytes.
    static void *operator new(size_t size) noexcept;
    static void operator delete(void *block) noexcept;

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x178b20: id 1 only
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x1a84c0: the deleting destructor at 0
    cISceneNodeIterator *Clone() override;                   // 0xa4cb10
    bool IsEqual(const cISceneNodeIterator *other) override; // shared 0x17b680
    cISceneNode *Get() override;                             // shared 0x17b670
    void Next() override;                                    // shared 0x17b700

    uint32_t mRefCount; // +4
    Position mAt;       // +8 (the tree node)
};
static_assert(sizeof(cSceneNodeIterator) == cSceneNodeIterator::kSize, "cSceneNodeIterator size");
static_assert(offsetof(cSceneNodeIterator, mAt) == 8, "cSceneNodeIterator mAt");

class cSceneGraphSystem : public cISceneGraphSystemPrivate, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x40;
    static constexpr uint32_t kServiceId = 0xe5074b6b; // also the director's class id
    static constexpr int32_t kPriority = 200000;

    cSceneGraphSystem();           // 0xa4c6f0
    ~cSceneGraphSystem() override; // 0xa4c840; deleting 0xa4c9e0 (the +8 face's slot 3 through 0xa4c9d8)
    cSceneGraphSystem(const cSceneGraphSystem &) = delete;
    cSceneGraphSystem &operator=(const cSceneGraphSystem &) = delete;
    // 0xa4cb40, the COM director's creator: operator new(0x40) and the constructor.
    static cSceneGraphSystem *__cdecl Create();

    // cIGZUnknown (all three faces; +4 / +8 through the compiler's thunks: the game's 0xa4d09d / 0xa4d0a5 and the folded
    // AddRef / Release thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xa4d060: the two interface ids -> this, else the base's
    uint32_t AddRef() override;                              // folded 0x145240 (the base's)
    uint32_t Release() override;                             // folded 0x1a85b0 (the base's)

    // cISceneGraphSystem
    cSceneNodeRange GetSceneNodes() override;                // 0xa4d0c0
    cISceneGraphFactory *GetFactory() override;              // folded 0x15f390
    cMaterialManager *GetMaterialManager() override;         // 0xa4d030
    cTextureManager *GetTextureManager() override;           // 0xa4d300
    cIRenderGroupManager *GetRenderGroups() override;        // folded 0x147770
    cStringPool *GetStringPool() override;                   // 0xa4d2f0
    cIDisplayList *GetDisplayList() override;                // folded 0x1457d0
    nGZCommandParser::cIFileParser *GetFileParser() override; // folded 0x147800
    cIMaterialParser *GetMaterialParser() override;          // folded 0x1694c0
    cILODLevelsStore *GetLODLevels() override;               // folded 0x1674b0
    // cISceneGraphSystemPrivate
    void AddSceneNode(cISceneNode *node) override;    // 0xa4ca40
    void RemoveSceneNode(cISceneNode *node) override; // 0xa4d0b0

    // cIGZSystemService (+4)
    bool Init() override;                   // 0xa4cbb0
    bool Shutdown() override;               // 0xa4d190
    bool OnTick(uint32_t counter) override; // 0xa4d040: the material manager's slot 17

    std::set<cISceneNode *> mSceneNodes;              // +0x20
    cISceneGraphFactory *mFactory;                    // +0x28
    cIRenderGroupManager *mRenderGroups;              // +0x2c
    cIDisplayList *mDisplayList;                      // +0x30
    nGZCommandParser::cIFileParser *mFileParser;      // +0x34
    cIMaterialParser *mMaterialParser;                // +0x38
    cILODLevelsStore *mLODLevels;                     // +0x3c
};
static_assert(sizeof(cSceneGraphSystem) == cSceneGraphSystem::kSize, "cSceneGraphSystem size");
static_assert(offsetof(cSceneGraphSystem, mSceneNodes) == 0x20 && offsetof(cSceneGraphSystem, mFactory) == 0x28 &&
                  offsetof(cSceneGraphSystem, mRenderGroups) == 0x2c && offsetof(cSceneGraphSystem, mDisplayList) == 0x30 &&
                  offsetof(cSceneGraphSystem, mFileParser) == 0x34 &&
                  offsetof(cSceneGraphSystem, mMaterialParser) == 0x38 && offsetof(cSceneGraphSystem, mLODLevels) == 0x3c,
              "cSceneGraphSystem layout");

} // namespace nGZSceneGraph
