#pragma once
// cTSSimSystem (lane IV): the sim COM director's system service (id 0xdae3783f, priority -0xf4240). 0x170 bytes, /O2.
// Init registers its message ids, takes the behaviour, sub-system and constants objects from the COM framework, registers
// the sim cheats and the free-will / interrupt settings, seeds its generator and installs the ticking sub-system (class
// 0x2f7f3357); OnTick feeds the persist system's background loader from the pending resource keys and moves loaded
// resources into the cache (slot 72); DoMessage clears the pending keys (0xb2), follows the two settings and forwards the
// family / lot / neighbourhood removal messages to the script layer.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xdc2f30, 13 slots), +4 cRZUnknownMT (0xdc2f68,
// 6 slots), +0x1c cITSSimSystem (0xdc2f84, 78 slots, iid 0xdae3783e), +0x20 cIGZMessageTarget2 (0xdc30c0, iid 0x452294aa).
// The original's slot bodies take `this` as their face and reach the fields at face-relative offsets; ours are members.
//
// Standalone shape (docs/standalone-shape.md): the constructor 0x416810 (hooked: placement new) is called only by
// cTSSimCOMDirector::OnStart (0x416370, ours); the vtables are referenced only by the constructor and the destructor
// 0x416ce0, so every system carries ours and the slots have no adapters.
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <unordered_map>

class cIGZString;

using SimSystemRef = cRZAutoRefCount<cIGZUnknown>;

// The resource cache's hash: the sum of the key's first three words (0x418c88 computes it for Find128 0x1f5570). Not
// noexcept, as the game's: its clear() (Shutdown 0x41a984) is the bulk one MSVC uses for hashes that may throw.
struct SimResourceKeyHash {
    size_t operator()(const cGZPersistResourceKey &key) const { return key.type + key.group + key.instance; }
};
struct SimResourceKeyEqual {
    bool operator()(const cGZPersistResourceKey &a, const cGZPersistResourceKey &b) const
    {
        return a.type == b.type && a.group == b.group && a.instance == b.instance && a.resource == b.resource;
    }
};
// MSVC 14.40 std::unordered_map as in the game (0x20 bytes: max load factor, list, bucket vector, mask, max index; nodes
// 0x1c: links, key at +8, the counted resource at +0x18; erasing releases it: 0x4165c0).
using SimResourceCache = std::unordered_map<cGZPersistResourceKey, SimSystemRef, SimResourceKeyHash, SimResourceKeyEqual>;
static_assert(sizeof(SimResourceCache) == 0x20, "MSVC 14.40 unordered_map");
using SimResourceKeyList = std::list<cGZPersistResourceKey>; // nodes 0x18
static_assert(sizeof(SimResourceKeyList) == 8, "MSVC 14.40 list");

// A cRZString member (the hook-shaped string, built and destroyed by hand as the original's inlined code does).
struct SimSystemString {
    union {
        cRZString s;
    };
    SimSystemString() { s.Construct(); }
    SimSystemString(const SimSystemString &) = delete;
    SimSystemString &operator=(const SimSystemString &) = delete;
    ~SimSystemString() { s.Destruct(); }
};
static_assert(sizeof(SimSystemString) == 0x20, "cRZString");

// cRZRandom as a member (no constructor of its own here; the system's constructor builds it).
struct SimSystemRandom {
    union {
        cRZRandom r;
    };
    SimSystemRandom() {}
};
static_assert(sizeof(SimSystemRandom) == 0xc, "cRZRandom");

// cITSSimSystem (iid 0xdae3783e): slots 3-77. "Folded" slots are bodies the original shares with other classes' slots.
class cITSSimSystem : public cIGZUnknown {
public:
    virtual cIGZUnknown *GetObjectSource() = 0;                  // 3 (folded 0x149cf0): +0x2c, not counted
    virtual bool GetTuning(uint32_t id, float *out) = 0;         // 4 (0x418ab0): ids 1000-1005
    virtual bool SetTuning(uint32_t id, float value) = 0;        // 5 (0x41a710)
    virtual cIGZUnknown *GetController() = 0;                    // 6 (folded 0x148ba0): +0x34
    virtual void SetController(cIGZUnknown *controller) = 0;     // 7 (0x41a8f0)
    virtual uint8_t GetFreeWill() = 0;                              // 8 (folded 0x418b70)
    virtual void SetFreeWill(uint8_t on) = 0;                       // 9 (0x41a7e0): the app's option 0x1e follows
    virtual uint8_t GetFlagA9() = 0;                                // 10 (0x418a50)
    virtual void SetFlagA9(uint8_t on) = 0;                         // 11 (0x41a680): option 0x2a
    virtual uint8_t GetFlagAA() = 0;                                // 12 (0x418b40)
    virtual void SetFlagAA(uint8_t on) = 0;                         // 13 (0x41a7b0): option 0x38
    virtual uint8_t GetInterrupt() = 0;                             // 14 (folded 0x418bc0)
    virtual void SetInterrupt(uint8_t on) = 0;                      // 15 (0x41a840)
    virtual uint8_t GetFlagAC() = 0;                                // 16 (folded 0x418a90)
    virtual void SetFlagAC(uint8_t on) = 0;                         // 17 (folded 0x41a700)
    virtual bool IsFlagADActive() = 0;                           // 18 (0x418b90): +0xad set and the counter at +0xb0 zero
    virtual void SetFlagAD(uint8_t on) = 0;                         // 19 (folded 0x41a820)
    virtual void PushB0() = 0;                                   // 20 (0x419be0): ++counter
    virtual void PopB0() = 0;                                    // 21 (0x41b360): --counter when not zero
    virtual uint8_t GetFlag44() = 0;                                // 22 (folded 0x3730d0)
    virtual void SetFlag44(uint8_t on) = 0;                         // 23 (folded 0x2bfcb0)
    virtual cRZString *GetStringB4() = 0;                        // 24 (folded 0x418be0)
    virtual bool CopyStringB4(cIGZString *out) = 0;              // 25 (0x418bf0): out's slot 17 (Copy)
    virtual void *SetStringB4(const cIGZString *text) = 0;       // 26 (0x41a890): the string's slot 16 (operator=)
    virtual cRZString *GetStringD4() = 0;                        // 27 (folded 0x418bd0)
    virtual void SetStringD4Text(const char *text) = 0;          // 28 (0x41a860): ignored when null
    virtual void *SetStringD4(const cIGZString *text) = 0;       // 29 (0x41a850): slot 16
    virtual int32_t GetTableA(uint32_t index) = 0;               // 30 (0x4190c0): +0x60[index], 0 past 8
    virtual int32_t GetTableB(uint32_t index) = 0;               // 31 (0x4190e0): +0x84[index]
    virtual bool GetSkillName(uint32_t skill, cIGZString **out) = 0; // 32 (0x41a260): string table 0x89, index 0x41ad10
    virtual bool GetString89_5B(cIGZString **out) = 0;           // 33 (0x41a240)
    virtual bool GetString89_5C(cIGZString **out) = 0;           // 34 (0x41a220)
    virtual bool GetStringAD_A(uint32_t flag, cIGZString **out) = 0; // 35 (0x41a390)
    virtual bool GetStringAE_A(uint32_t value, cIGZString **out) = 0; // 36 (0x41ac80)
    virtual bool GetStringAD_B(uint32_t flag, cIGZString **out) = 0; // 37 (0x41a2a0)
    virtual bool GetStringAE_B(uint32_t value, cIGZString **out) = 0; // 38 (0x41abf0)
    virtual uint8_t GetFlag43() = 0;                                // 39 (0x418c10)
    virtual void SetFlag43(uint8_t on) = 0;                         // 40 (0x41a8a0)
    virtual uint8_t GetFlag41() = 0;                                // 41 (0x418a70)
    virtual void SetFlag41(uint8_t on) = 0;                         // 42 (0x41a6e0)
    virtual uint8_t GetFlag42() = 0;                                // 43 (0x418a80)
    virtual void SetFlag42(uint8_t on) = 0;                         // 44 (0x41a6f0)
    virtual uint32_t GetWordF8() = 0;                            // 45 (folded 0x418b80)
    virtual void SetWordF8(uint32_t value) = 0;                  // 46 (0x41a810)
    virtual float GetFloatFC() = 0;                              // 47 (0x418c20)
    virtual void SetFloatFC(float value) = 0;                    // 48 (folded 0x41a8b0)
    virtual uint32_t GetWord100() = 0;                           // 49 (folded 0x418c40)
    virtual void SetWord100(uint32_t value) = 0;                 // 50 (0x41a8e0)
    virtual uint32_t GetWord104() = 0;                           // 51 (folded 0x254690)
    virtual void SetWord104(uint32_t value) = 0;                 // 52 (folded 0x41a8d0)
    virtual uint8_t GetFlag45() = 0;                                // 53 (folded 0x418a40)
    virtual void SetFlag45(uint8_t on) = 0;                         // 54 (0x41a670)
    virtual cRZRandom *GetRandom() = 0;                          // 55 (0x418c30)
    // 56-61: the behaviour (+0x30, cITSBehavior) slots 44, 45, 41, 42, 46, 47, tail-called.
    virtual uint32_t GetBaseIdForKind(uint32_t kind) = 0;        // 56 (0x4170c0)
    virtual uint32_t GetMaxIdForKind(uint32_t kind) = 0;         // 57 (0x4170e0)
    virtual void LoadBreakpoints(void *file) = 0;                // 58 (0x417100)
    virtual void BehaviorNothing() = 0;                          // 59 (0x417110)
    virtual void GetKindName(uint32_t kind, cIGZString *out) = 0; // 60 (0x4170d0)
    virtual uint32_t GetKindForId(uint32_t treeId) = 0;          // 61 (0x4170f0)
    virtual void PlaceOnLot(cIGZUnknown *definition) = 0;        // 62 (0x419bf0)
    virtual cIGZUnknown *GetRef120() = 0;                        // 63 (folded 0x418aa0)
    virtual void *GetNeighborhoodConstants() = 0;                // 64 (folded 0x147d00)
    virtual void *GetAutonomyConstants() = 0;                    // 65 (folded 0x15e550)
    virtual cIGZUnknown *GetRef124() = 0;                        // 66 (folded 0x418b60)
    virtual cIGZUnknown *GetSubsystem() = 0;                     // 67 (folded 0x4190b0)
    virtual uint8_t GetFlag164() = 0;                               // 68 (0x418bb0)
    virtual void SetFlag164(uint8_t on) = 0;                        // 69 (0x41a830)
    virtual bool Nothing70(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) = 0; // 70 (folded 0x18a5a0): false
    virtual bool GetObjectResource(cIGZUnknown *container, uint32_t type, uint32_t instance, uint32_t iid, void **out) = 0; // 71 (0x418ea0)
    virtual bool GetCachedResource(const cGZPersistResourceKey *key, uint32_t iid, void **out) = 0; // 72 (0x418c50)
    virtual void QueueResourcesOfType(uint32_t type, uint32_t unused) = 0; // 73 (0x41a460)
    virtual uint32_t AgeGroup(int32_t age) = 0;                  // 74 (0x417120)
    virtual void *GetStaticTable() = 0;                          // 75 (0x418b50): the image's 0x1014a18
    virtual uint8_t GetFlagF4() = 0;                                // 76 (folded 0x418a60)
    virtual void SetFlagF4(uint8_t on) = 0;                         // 77 (0x41a6b0): option 0x39
};

class cTSSimSystem : public rz::cRZSystemService, public cITSSimSystem, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xdc2f30;
    static constexpr uint32_t kVTable4 = 0xdc2f68;
    static constexpr uint32_t kSimVTable = 0xdc2f84;
    static constexpr uint32_t kMessageVTable = 0xdc30c0;
    static constexpr uint32_t kSize = 0x170;
    static constexpr uint32_t kServiceId = 0xdae3783f;
    static constexpr int32_t kPriority = int32_t(0xfff0bdc0);
    static constexpr uint32_t kIid = 0xdae3783e;            // cITSSimSystem
    static constexpr uint32_t kMessageTargetIid = 0x452294aa;

    cTSSimSystem();            // 0x416810
    ~cTSSimSystem() noexcept override; // 0x416ce0; deleting 0x417090 (+4 thunk 0x416fef)
    cTSSimSystem(const cTSSimSystem &) = delete;
    cTSSimSystem &operator=(const cTSSimSystem &) = delete;

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x41a610; thunks 0x41a64d (+4), 0x41a655 (+0x1c), 0x41a65d (+0x20)
    uint32_t AddRef() override;                              // folded 0x1cc320; thunks folded 0x1cc325 / 0x1cc32d / 0x1cc335
    uint32_t Release() override;                             // folded 0x1cd7a0; thunks folded 0x1cd7a5 / 0x1cd7ad / 0x1cd7b5

    // ---- cIGZSystemService ----
    bool Init() override;                      // 0x419100
    bool Shutdown() override;                  // 0x41a950
    bool OnTick(uint32_t counter) override;    // 0x419fb0
    bool OnIdle(uint32_t counter) override;    // 0x419f90

    // ---- cIGZMessageTarget2 (+0x20) ----
    bool DoMessage(void *message) override;    // 0x417280

    // ---- cITSSimSystem (+0x1c) ----
    cIGZUnknown *GetObjectSource() override { return mObjectSource.Get(); }
    bool GetTuning(uint32_t id, float *out) override;
    bool SetTuning(uint32_t id, float value) override;
    cIGZUnknown *GetController() override { return mController.Get(); }
    void SetController(cIGZUnknown *controller) override;
    uint8_t GetFreeWill() override { return mFreeWill; }
    void SetFreeWill(uint8_t on) override;
    uint8_t GetFlagA9() override { return mFlagA9; }
    void SetFlagA9(uint8_t on) override;
    uint8_t GetFlagAA() override { return mFlagAA; }
    void SetFlagAA(uint8_t on) override;
    uint8_t GetInterrupt() override { return mInterrupt; }
    void SetInterrupt(uint8_t on) override { mInterrupt = on; }
    uint8_t GetFlagAC() override { return mFlagAC; }
    void SetFlagAC(uint8_t on) override { mFlagAC = on; }
    bool IsFlagADActive() override { return mFlagAD && mCounterB0 == 0; }
    void SetFlagAD(uint8_t on) override { mFlagAD = on; }
    void PushB0() override { ++mCounterB0; }
    void PopB0() override;
    uint8_t GetFlag44() override { return mFlag44; }
    void SetFlag44(uint8_t on) override { mFlag44 = on; }
    cRZString *GetStringB4() override { return &mStringB4.s; }
    bool CopyStringB4(cIGZString *out) override;
    void *SetStringB4(const cIGZString *text) override;
    cRZString *GetStringD4() override { return &mStringD4.s; }
    void SetStringD4Text(const char *text) override;
    void *SetStringD4(const cIGZString *text) override;
    int32_t GetTableA(uint32_t index) override { return index <= 8 ? mTableA[index] : 0; }
    int32_t GetTableB(uint32_t index) override { return index <= 8 ? mTableB[index] : 0; }
    bool GetSkillName(uint32_t skill, cIGZString **out) override;
    bool GetString89_5B(cIGZString **out) override;
    bool GetString89_5C(cIGZString **out) override;
    bool GetStringAD_A(uint32_t flag, cIGZString **out) override;
    bool GetStringAE_A(uint32_t value, cIGZString **out) override;
    bool GetStringAD_B(uint32_t flag, cIGZString **out) override;
    bool GetStringAE_B(uint32_t value, cIGZString **out) override;
    uint8_t GetFlag43() override { return mFlag43; }
    void SetFlag43(uint8_t on) override { mFlag43 = on; }
    uint8_t GetFlag41() override { return mFlag41; }
    void SetFlag41(uint8_t on) override { mFlag41 = on; }
    uint8_t GetFlag42() override { return mFlag42; }
    void SetFlag42(uint8_t on) override { mFlag42 = on; }
    uint32_t GetWordF8() override { return mWordF8; }
    void SetWordF8(uint32_t value) override { mWordF8 = value; }
    float GetFloatFC() override { return mFloatFC; }
    void SetFloatFC(float value) override { mFloatFC = value; }
    uint32_t GetWord100() override { return mWord100; }
    void SetWord100(uint32_t value) override { mWord100 = value; }
    uint32_t GetWord104() override { return mWord104; }
    void SetWord104(uint32_t value) override { mWord104 = value; }
    uint8_t GetFlag45() override { return mFlag45; }
    void SetFlag45(uint8_t on) override { mFlag45 = on; }
    cRZRandom *GetRandom() override { return &mRandom.r; }
    uint32_t GetBaseIdForKind(uint32_t kind) override;
    uint32_t GetMaxIdForKind(uint32_t kind) override;
    void LoadBreakpoints(void *file) override;
    void BehaviorNothing() override;
    void GetKindName(uint32_t kind, cIGZString *out) override;
    uint32_t GetKindForId(uint32_t treeId) override;
    void PlaceOnLot(cIGZUnknown *definition) override;
    cIGZUnknown *GetRef120() override { return mRef120.Get(); }
    void *GetNeighborhoodConstants() override { return mNeighborhoodConstants; }
    void *GetAutonomyConstants() override { return mAutonomyConstants; }
    cIGZUnknown *GetRef124() override { return mRef124.Get(); }
    cIGZUnknown *GetSubsystem() override { return mSubsystem.Get(); }
    uint8_t GetFlag164() override { return mFlag164; }
    void SetFlag164(uint8_t on) override { mFlag164 = on; }
    bool Nothing70(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override { return false; }
    bool GetObjectResource(cIGZUnknown *container, uint32_t type, uint32_t instance, uint32_t iid, void **out) override;
    bool GetCachedResource(const cGZPersistResourceKey *key, uint32_t iid, void **out) override;
    void QueueResourcesOfType(uint32_t type, uint32_t unused) override;
    uint32_t AgeGroup(int32_t age) override;
    void *GetStaticTable() override;
    uint8_t GetFlagF4() override { return mFlagF4; }
    void SetFlagF4(uint8_t on) override;

    // 0x41b1d0 (__thiscall on the primary face, ret 0xc; `this` unused): string `index` of the string table (resource type 0x2026960b, group 0x7fa7dc7c,
    // instance table << 16 | index) as a new cRZString assigned to *out. False when the resource is not there.
    bool LoadTableString(uint32_t table, uint32_t index, cIGZString **out);
    // 0x41ad10 (__cdecl): the skill / motive kind to its string index (0 when unknown, -1 for two kinds).
    static int32_t __cdecl SkillStringIndex(uint32_t skill);

    cIGZMessageTarget2 *MessageTarget() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c, +0x20: the faces' vtables
    std::set<uint32_t> mMessageIds;          // 0x24 the five message ids (0xb2, 0xaf, 0xa4, 0xa6, 0xc6) Init registers
    SimSystemRef mObjectSource;              // 0x2c class 0x9b7ef292 (Init)
    SimSystemRef mBehavior;                  // 0x30 class 0x1b269450 (cITSBehavior; Init, dropped by Shutdown)
    SimSystemRef mController;                // 0x34 slot 7 (shut down through its slot 5 when replaced)
    void *mNeighborhoodConstants;            // 0x38 cTSNeighborhoodConstants (0x28 bytes, game class; deleted by Shutdown)
    void *mAutonomyConstants;                // 0x3c cTSAutonomyConstantsClient (0xc bytes, game class)
    uint8_t mUnknown40;                      // 0x40 never written
    uint8_t mFlag41;                         // 0x41
    uint8_t mFlag42;                         // 0x42
    uint8_t mFlag43;                         // 0x43
    uint8_t mFlag44;                         // 0x44
    uint8_t mFlag45;                         // 0x45
    uint8_t mUnknown46[2];                   // 0x46 never written
    float mTuning[6];                        // 0x48 ids 1000-1005 (0.002, 0.02, 0.1, 0.002, 0.02, 0.1)
    int32_t mTableA[9];                      // 0x60
    int32_t mTableB[9];                      // 0x84
    uint8_t mFreeWill;                       // 0xa8
    uint8_t mFlagA9;                         // 0xa9
    uint8_t mFlagAA;                         // 0xaa
    uint8_t mInterrupt;                      // 0xab
    uint8_t mFlagAC;                         // 0xac
    uint8_t mFlagAD;                         // 0xad
    uint8_t mUnknownAE[2];                   // 0xae never written
    uint32_t mCounterB0;                     // 0xb0
    SimSystemString mStringB4;               // 0xb4
    SimSystemString mStringD4;               // 0xd4
    uint8_t mFlagF4;                         // 0xf4
    uint8_t mUnknownF5[3];                   // 0xf5 never written
    uint32_t mWordF8;                        // 0xf8 (0x19)
    float mFloatFC;                          // 0xfc
    uint32_t mWord100;                       // 0x100
    uint32_t mWord104;                       // 0x104
    SimSystemRandom mRandom;                 // 0x108 seeded from the clock (constructor, Init)
    uint8_t mUnknown114[0xc];                // 0x114 never written
    SimSystemRef mRef120;                    // 0x120 class 0x1b5a2044 (Init, dropped by Shutdown)
    SimSystemRef mRef124;                    // 0x124 class 0x5b561442 (Init)
    SimSystemRef mResources;                 // 0x128 the resource manager (0x20deb5)
    SimSystemRef mDebugger;                  // 0x12c the debugger service (0x20ddda, may be null): bypasses the cache
    SimResourceCache mResourceCache;         // 0x130 key -> resource (slot 72)
    SimResourceKeyList mPendingLoads;        // 0x150 keys waiting for the background loader (slot 73; cleared by 0xb2)
    SimResourceKeyList mLoading;             // 0x158 keys handed to the loader (OnTick)
    int32_t mLoadingCount;                   // 0x160 how many are with the loader (at most 1000)
    uint8_t mFlag164;                        // 0x164
    uint8_t mUnknown165[3];                  // 0x165 never written
    SimSystemRef mSubsystem;                 // 0x168 class 0x2f7f3357: ticked by OnTick / OnIdle
    uint8_t mUnknown16C[4];                  // 0x16c never written
};

static_assert(sizeof(cTSSimSystem) == cTSSimSystem::kSize, "cTSSimSystem size");
static_assert(offsetof(cTSSimSystem, mMessageIds) == 0x24, "mMessageIds");
static_assert(offsetof(cTSSimSystem, mObjectSource) == 0x2c, "mObjectSource");
static_assert(offsetof(cTSSimSystem, mNeighborhoodConstants) == 0x38, "mNeighborhoodConstants");
static_assert(offsetof(cTSSimSystem, mFlag41) == 0x41, "mFlag41");
static_assert(offsetof(cTSSimSystem, mTuning) == 0x48, "mTuning");
static_assert(offsetof(cTSSimSystem, mTableA) == 0x60, "mTableA");
static_assert(offsetof(cTSSimSystem, mTableB) == 0x84, "mTableB");
static_assert(offsetof(cTSSimSystem, mFreeWill) == 0xa8, "mFreeWill");
static_assert(offsetof(cTSSimSystem, mCounterB0) == 0xb0, "mCounterB0");
static_assert(offsetof(cTSSimSystem, mStringB4) == 0xb4, "mStringB4");
static_assert(offsetof(cTSSimSystem, mStringD4) == 0xd4, "mStringD4");
static_assert(offsetof(cTSSimSystem, mFlagF4) == 0xf4, "mFlagF4");
static_assert(offsetof(cTSSimSystem, mWordF8) == 0xf8, "mWordF8");
static_assert(offsetof(cTSSimSystem, mRandom) == 0x108, "mRandom");
static_assert(offsetof(cTSSimSystem, mRef120) == 0x120, "mRef120");
static_assert(offsetof(cTSSimSystem, mResourceCache) == 0x130, "mResourceCache");
static_assert(offsetof(cTSSimSystem, mPendingLoads) == 0x150, "mPendingLoads");
static_assert(offsetof(cTSSimSystem, mLoading) == 0x158, "mLoading");
static_assert(offsetof(cTSSimSystem, mLoadingCount) == 0x160, "mLoadingCount");
static_assert(offsetof(cTSSimSystem, mFlag164) == 0x164, "mFlag164");
static_assert(offsetof(cTSSimSystem, mSubsystem) == 0x168, "mSubsystem");

// The constructor hook's entry point (placement new of ours).
struct cTSSimSystemAdapters {
    static cTSSimSystem *__fastcall Construct(void *memory, void *edx); // 0x416810
};
