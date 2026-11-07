#pragma once
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "simantics/ServiceRefVector.h" // ServiceRefVector: counted pointers in allocator-service storage
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: the counted object reference a map node holds

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// cTSNeighborhoodManager: the neighbourhoods the game knows. RTTI cTSNeighborhoodManager, bases cITSNeighborhoodManager at +0,
// cIGZPersistResource at +4, cIGZSerializable at +8. 0x44 bytes; 0x4a3f20 allocates and builds one. Game vtables 0xdcbb1c (21
// slots), 0xdcbb74 (12), 0xdcbba8 (6).
//
// Fields: a map from a 32-bit id to a counted neighbourhood, a map from a neighbourhood name (case
// insensitive) to a record holding a counted object and a list of counted neighbourhoods, the next id, a cursor over the id
// map (vf7 / vf8 walk the visible neighbourhoods), and a list of counted neighbourhoods.

// Tree node header of an MSVC 14.40 std::map node (the cursor points at one).
struct NeighborhoodTreeNode {
    NeighborhoodTreeNode *mLeft;
    NeighborhoodTreeNode *mParent;
    NeighborhoodTreeNode *mRight;
    char mColor;
    char mIsNil;
};

// The key of the name map: a cRZString (vtable, std::string, refcount; 0x20 bytes).
struct NeighborhoodName {
    const void *mVTable; // the game's cRZString vtable (0xd9b470)
    std::string mString;
    uint32_t mRefCount;

    explicit NeighborhoodName(const char *text);
    NeighborhoodName(const char *data, uint32_t length);
    NeighborhoodName(const NeighborhoodName &) = default;
};
static_assert(sizeof(NeighborhoodName) == 0x20);

// Orders names case-insensitively (_stricmp), as the game's tree does.
struct NeighborhoodNameLess {
    bool operator()(const NeighborhoodName &a, const NeighborhoodName &b) const;
};

// The value of the name map (16 bytes; node 0x40).
struct NeighborhoodNameEntry {
    void *mObject;            // +0x00 counted (Release is slot 2)
    ServiceRefVector mHoods;  // +0x04 counted neighbourhoods
    NeighborhoodNameEntry() : mObject(nullptr), mHoods{} {}
    NeighborhoodNameEntry(const NeighborhoodNameEntry &) = delete;
    NeighborhoodNameEntry &operator=(const NeighborhoodNameEntry &) = delete;
    // As the game's: destroys the list (0x1e6a40, an original), then releases the object.
    ~NeighborhoodNameEntry();
};
static_assert(sizeof(NeighborhoodNameEntry) == 16);

// Id -> counted neighbourhood (erasing a node releases the object, slot 2).
using HoodIdMap = std::map<uint32_t, NeighborDataRef>;
static_assert(sizeof(HoodIdMap) == 8);
using HoodNameMap = std::map<NeighborhoodName, NeighborhoodNameEntry, NeighborhoodNameLess>;
static_assert(sizeof(HoodNameMap) == 8);

// The manager's own interface (RTTI cITSNeighborhoodManager at +0): slots 3..19, then the virtual destructor as slot 20 (the
// scalar deleting destructor 0x4a1fb0). Neighbourhoods are passed as the objects' primary faces (void *: their interface is
// not declared here). Declared here until a second implementer appears.
class cITSNeighborhoodManager : public cIGZUnknown {
public:
    virtual void Load() = 0;                                                       // slot 3
    virtual void Clear() = 0;                                                      // slot 4
    virtual uint8_t Save() = 0;                                                    // slot 5 (AL: a raw byte, see Save)
    virtual cIGZPersistResource *GetPersistFace() = 0;                             // slot 6
    virtual void *First() = 0;                                                     // slot 7
    virtual void *Next() = 0;                                                      // slot 8
    virtual void *GetById(uint32_t id) = 0;                                        // slot 9
    virtual void *GetByName(const char *name) = 0;                                 // slot 10
    virtual void *FindAccepting(uint32_t value) = 0;                               // slot 11
    virtual bool GetNamedList(const char *name, ServiceRefVector *out) = 0;        // slot 12
    virtual int32_t CountNamed(const char *name, int32_t a, int32_t b) = 0;        // slot 13
    virtual bool Filter(ServiceRefVector *out, int32_t a, int32_t b) = 0;          // slot 14
    virtual int32_t FilterWith(ServiceRefVector *out, void *predicate) = 0;        // slot 15
    virtual void Open(const uint32_t *key, uint32_t extra) = 0;                    // slot 16
    virtual void NextFreeName(void *name) = 0;                                     // slot 17
    virtual bool Add(void *hood) = 0;                                              // slot 18
    virtual bool Remove(void *hood) = 0;                                           // slot 19
    virtual ~cITSNeighborhoodManager() = default;                                  // slot 20
};

// The standalone shape (docs/standalone-shape.md): cITSNeighborhoodManager at +0, cIGZPersistResource at +4, cIGZSerializable
// at +8; the compiler emits the three vtables, the QI / Release adjustor thunks of the two secondary faces (the original's
// 0x4a54d7 / 0x4a5557 and 0x4a54df / 0x4a555f; their AddRef thunks 0x1f4daf / 0x1f4db7 are folded) and the RTTI. Creation: no
// constructor function; Create 0x4a3f20 (operator new 0x44, constructor inline; its address registered by a COM director) is
// the only site, and the game's vtables are referenced only there and by the deleting destructor 0x4a1fb0. The RVA hooks on the
// old slot functions are adapters (cTSNeighborhoodManagerAdapters).
class cTSNeighborhoodManager : public cITSNeighborhoodManager, public cIGZPersistResource, public cIGZSerializable {
public:
    static constexpr uint32_t kVTable = 0xdcbb1c;
    static constexpr uint32_t kPersistVTable = 0xdcbb74;
    static constexpr uint32_t kSerialVTable = 0xdcbba8;
    static constexpr uint32_t kClassId = 0xcc8a6a69;
    static constexpr uint32_t kStreamVersion = 0x11; // what 0xb39350 returns

    cTSNeighborhoodManager() = default;
    // Slot 20, 0x4a1fb0 (ret 4): destroys the list, the name map and the id map with the class vtables installed; bit 0 frees
    // the 0x44 bytes. Returns this. Defined out of line so the destructor stores the vtables (docs/standalone-shape.md, FI).
    ~cTSNeighborhoodManager() override;

    // 0x4a3f20: allocates 0x44 bytes and builds the manager; null when the allocation fails.
    static cTSNeighborhoodManager *__cdecl Create();

    // cIGZUnknown (all three faces; the secondary faces' QI / Release are the compiler's thunks)
    // 0x4a5480 (slot 0, ret 8): ids 0xcc8a6a62 this, 1 and 0xe4fda3d4 the serial face, 0x456b8f1d the persist face; counted
    // through slot 1.
    bool QueryInterface(uint32_t id, void **out) override;
    uint32_t AddRef() override { return ++mRefCount; } // slot 1, folded 0x15e520
    // 0x4a5540 (slot 2): counts above one go down by one; otherwise slot 20 (flags 1) and 0.
    uint32_t Release() override;

    // cITSNeighborhoodManager
    // 0x4a4160 (slot 3): loads the neighbourhoods (still the original's body, see the .cpp).
    void Load() override;
    // 0x4a5f00 (slot 4): calls slot 26 of every neighbourhood of the id map, empties both maps, restarts the ids at 1, puts
    // the cursor at the head, calls slot 26 of the list's entries, releases them and empties the list (storage kept).
    void Clear() override;
    // 0x4a5aa0 (slot 5): saves the neighbourhoods. Asks the persist system (slot 48) to collect the records under this's group
    // (persist face slot 6) and every shown neighbourhood's group (its slot 24 object's slot 6) into a list, closes the list
    // (slot 49), saves this (slot 16 with the persist face). If that worked: the framework's 0xcdcbe0e3 holder (when there is
    // one) hears of each group (slot 10, (group, 1)); each shown neighbourhood gets the key {0xac8a7a2e, group, 1, 0} (slot 24
    // object, slot 4) and saves itself (slot 27); the list is committed (persist slot 50 (list, 1)). Whether all of it
    // succeeded (AL: the callees' bytes and-ed together, not normalised).
    uint8_t Save() override;
    // Slot 6, folded 0x1c04e0: the persist face (null test included).
    cIGZPersistResource *GetPersistFace() override { return static_cast<cIGZPersistResource *>(this); }
    // 0x4a3f10 (slot 7): moves the cursor to the first id and returns slot 8 of this.
    void *First() override;
    // 0x4a5300 (slot 8): from the cursor, the first neighbourhood that does not report slot 14 (argument 1); the cursor ends
    // after it. Null at the end.
    void *Next() override;
    // 0x4a5140 (slot 9, ret 4): the neighbourhood with the id, or null.
    void *GetById(uint32_t id) override;
    // 0x4a5190 (slot 10, ret 4): the counted object of the name's record (case insensitive), or null.
    void *GetByName(const char *name) override;
    // 0x4a52a0 (slot 11, ret 4): the first neighbourhood (id order) whose slot 4 object accepts the value in slot 10 (value, 0).
    void *FindAccepting(uint32_t value) override;
    // 0x4a3bc0 (slot 12, ret 8): copies the name's list of neighbourhoods into `out` (not when it is that list); whether out
    // ends up non-empty. False when there is no such name.
    bool GetNamedList(const char *name, ServiceRefVector *out) override;
    // 0x4a5350 (slot 13, ret 12): how many of the name's neighbourhoods match: slot 18 equals `a` (or a is 0) and slot 22
    // equals `b` (or b is -1).
    int32_t CountNamed(const char *name, int32_t a, int32_t b) override;
    // 0x4a5df0 (slot 14, ret 12): the same match over this's own list, appended to `out`; with a = 0 and b = -1 a copy of the
    // whole list instead (unless out is that list). Whether out is non-empty.
    bool Filter(ServiceRefVector *out, int32_t a, int32_t b) override;
    // 0x4a4070 (slot 15, ret 8): appends the entries of this's list the predicate (its slot 0) accepts, or without a
    // predicate a copy of the whole list; returns how many entries out gained.
    int32_t FilterWith(ServiceRefVector *out, void *predicate) override;
    // 0x4a5cc0 (slot 16, ret 8): asks the common implementation for an option string, opens (persist system slot 21) a record
    // for the interface 0x656b8f0c with the string, sets the key (persist face slot 4), calls slot 3 and releases the holder.
    void Open(const uint32_t *key, uint32_t extra) override;
    // 0x4a3cf0 (slot 17, ret 4): writes the first free neighbourhood name into `name` (a cIGZString): formats N%03d into it
    // (slot 31, from 1 upwards) until no name record matches, then keeps counting while a directory of that name exists (the
    // path from 0xc0b362, DirectoryExists).
    void NextFreeName(void *name) override;
    // 0x4a2030 (slot 18, ret 4): adds a neighbourhood; false when its name record already holds it. Gives it a fresh id
    // (slot 11, from the next id) when it has none (slot 12 is -1, or 0 for anything but the tutorial) or its id is taken; maps
    // the id to it (unless the id is still mapped); files it under its name: a new record holds it, a main neighbourhood
    // (slot 18 is 1) replaces the record's object (the old one joins the list), others join the list. Then makes sure its two
    // directories exist (paths from 0xc0b232 / 0xc0b150, 0x1c5031) and posts message 0xc3 with a copy of its name. True.
    bool Add(void *hood) override;
    // 0x4a5740 (slot 19, ret 4): removes a neighbourhood (false for null): posts message 0xc4 with its name (a main one, slot
    // 18 is 1) or 0xc6 with its id, deletes its files (DeleteFiles, 0x4a2950), unmaps its id. A main neighbourhood takes its
    // name record and the ids of the record's list with it; another leaves the record's list. True.
    bool Remove(void *hood) override;

    // 0x4a6000 (thiscall, ret 4; this unused): whether the neighbourhood's package (its directory under the root, its name,
    // "_Neighborhood.package") exists. False for null. Only Load calls it.
    bool HasPackage(void *hood);

    // Lane FU (src/ts/cTSNeighborhoodManagerLoad.cpp).
    // 0x4a27a0 (thiscall, ret 0): makes the tutorial neighbourhood (framework class 0xac8a7a2e, interface 0xac8a7a29), names it
    // "Tutorial" (its slots 3 and 7), id 0 (slot 11), state (1, 1) (slot 13), keys it {0xac8a7a2e, group of "<name>_Neighborhood"
    // (persist system slot 11), 1, 0} (its slot 24 object's slot 4) and adds it (this's slot 18). Only Load calls it.
    void AddTutorial();
    // 0x4a2950 (thiscall, ret 4; this unused, as HasPackage): deletes a neighbourhood's files. A main one (slot 18 is 1): in each of
    // its four directories (paths 0xc0b29b, 0xc0b1b9, 0xc0b492, 0xc0b362 from its name's text) the packages' segments are
    // released (persist system slot 25: the full path in the first, the file name in the others; in the last also its
    // "visited" entries, 0x4a5570), then the directory tree is deleted (0x1c628f (dir, 2, 1), 0x1c5fa0 (dir, 1)). Another: its
    // record's file (persist database slot 34 with its key) gets the same release and "visited" cleanup by file name, and the
    // .png and .reia beside it are deleted when they exist. Only Remove calls it.
    void DeleteFiles(void *hood);

    // cIGZPersistResource (+4; these run with this = the face): the record key at +0x34 (the face's +0x30). Folded bodies.
    cGZPersistResourceKey GetKey() override { return mKey; }                            // slot 3, 0x4a4060
    bool SetKey(const cGZPersistResourceKey &key) override { mKey = key; return true; } // slot 4, 0x4a5de0
    uint32_t GetType() override { return mKey.type; }                                   // slot 5, 0x1457d0
    uint32_t GetGroup() override { return mKey.group; }                                 // slot 6, 0x147800
    uint32_t GetInstance() override { return mKey.instance; }                           // slot 7, 0x1694c0
    bool SetType(uint32_t type) override { mKey.type = type; return true; }             // slot 8, 0x4711f0
    bool SetGroup(uint32_t group) override { mKey.group = group; return true; }         // slot 9, 0x471200
    bool SetInstance(uint32_t instance) override { mKey.instance = instance; return true; } // slot 10, 0x4a5dd0
    uint32_t GetRefCount() override { return mRefCount; }                               // slot 11, 0x46390

    // cIGZSerializable (+8)
    // 0x4a60d0 (slot 3, ret 4): writes 2 and 0x11 (slot 9 of the stream), whether the stream has no error (slot 18).
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4a54f0 (slot 4, ret 4): reads a version (slot 8); from version 2 on reads a second number (slot 9) and fails when it
    // is above 0x11; else whether the stream has no error (slot 18). A failed read leaves the old stack contents in the
    // variables: the version starts as the stream pointer, the second number as the face pointer.
    bool Read(ifaces::cIGZIStream *stream) override;
    // 0x4a4050 (slot 5).
    uint32_t GetGZCLSID() override;

    // +0x00 / +0x04 / +0x08: the three vtable pointers.
    uint32_t mRefCount = 0;           // +0x0c
    HoodIdMap mHoodsById;             // +0x10
    HoodNameMap mHoodsByName;         // +0x18
    uint32_t mNextId = 1;             // +0x20
    NeighborhoodTreeNode *mCursor = nullptr; // +0x24 a node of the id map (the head when the walk is over)
    ServiceRefVector mHoods{};        // +0x28 counted neighbourhoods
    cGZPersistResourceKey mKey{};     // +0x34 the persist face's key, zero when built

    NeighborhoodTreeNode *IdHead() { return *reinterpret_cast<NeighborhoodTreeNode **>(&mHoodsById); }
};
static_assert(offsetof(cTSNeighborhoodManager, mRefCount) == 0x0c);
static_assert(offsetof(cTSNeighborhoodManager, mHoodsById) == 0x10);
static_assert(offsetof(cTSNeighborhoodManager, mHoodsByName) == 0x18);
static_assert(offsetof(cTSNeighborhoodManager, mNextId) == 0x20);
static_assert(offsetof(cTSNeighborhoodManager, mCursor) == 0x24);
static_assert(offsetof(cTSNeighborhoodManager, mHoods) == 0x28);
static_assert(offsetof(cTSNeighborhoodManager, mKey) == 0x34);
static_assert(sizeof(cTSNeighborhoodManager) == 0x44);

namespace hoodmgr {
constexpr uint32_t kPersistFace = 4; // offsets of the secondary faces
constexpr uint32_t kSerialFace = 8;
} // namespace hoodmgr

// The RVA hooks' entry points (src/hook/tables/cTSNeighborhoodManager.cpp): every manager the game creates carries our vtables
// (the Create hook), so these run only where the game calls an old entry point or holds a manager built with its vtables.
// Secondary-face slots take the face pointer.
struct cTSNeighborhoodManagerAdapters {
    using Self = cTSNeighborhoodManager;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall HasPackage(Self *self, void *, void *hood);
    static void __fastcall AddTutorial(Self *self, void *);
    static void __fastcall DeleteFiles(Self *self, void *, void *hood);
    static void *__fastcall First(Self *self, void *);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
};
