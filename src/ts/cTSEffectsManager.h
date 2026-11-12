#pragma once
// cTSEffectsManager (lane IT): the effects system's manager (factory 0xbc86f0, registered by cTSEffectsCOMDirector). It owns
// the effects collection (its base at +8: names, effects, descriptions, message aliases and triggers), the pool of visual
// effects (cTSVisualEffect, 400 bytes each; active and free index lists), the level of detail, the debug bits, the named
// object maps and the message routing (message triggers and aliases start effects).
//
// Standalone shape (docs/standalone-shape.md): a real C++ class over its three RTTI faces (cITSEffectsManager +0,
// cIGZMessageTarget2 +4, cTSEffectsCollection +8); the factory and constructor hooks give every manager our vtables. The
// slots not reimplemented yet forward to the original bodies (OriginalFunction, listed in the status entries'
// original_calls): the layouts are the same and the originals dispatch through our vtables.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZRandom.h"
#include "ts/CountedRef.h"
#include "ts/cTSEffectsCollection.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fx {

// A counted pointer to an object with virtual bases: the pointer is to the object; Release goes to its first virtual base
// (vbtable at +4, entry 1), the manager's slot 33 hands out the second (entry 2).
struct VirtualBaseRef {
    void *mObject = nullptr;
    void *Base(int entry) const
    {
        auto *object = static_cast<uint8_t *>(mObject);
        const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(object + 4);
        return object + 4 + vbtable[entry];
    }
    // Clears the slot, then releases what it held through its first virtual base.
    void Reset()
    {
        void *old = mObject;
        if (!old) return;
        mObject = nullptr;
        auto *object = static_cast<uint8_t *>(old);
        const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(object + 4);
        static_cast<cIGZUnknown *>(static_cast<void *>(object + 4 + vbtable[1]))->Release();
    }
    VirtualBaseRef() = default;
    VirtualBaseRef(const VirtualBaseRef &) = delete;
    VirtualBaseRef &operator=(const VirtualBaseRef &) = delete;
    ~VirtualBaseRef()
    {
        if (mObject) static_cast<cIGZUnknown *>(Base(1))->Release();
    }
};

// The manager's cRZCallbackTimer (0x28 bytes, vtable 0xe61bb8): built and destroyed by cRZCallbackTimer::Construct
// (0x9ccf32) and Destruct (0x9ccfa9).
struct CallbackTimerMember {
    uint8_t mRaw[0x28];
    CallbackTimerMember();
    ~CallbackTimerMember();
    CallbackTimerMember(const CallbackTimerMember &) = delete;
    CallbackTimerMember &operator=(const CallbackTimerMember &) = delete;
};

// The game's cRZString as the manager's path lists hold it (vtable, std::string, count 0x20). A copy takes the vtable of
// cRZString (whatever the source's), the text (data and size) and a zero count.
struct PathString {
    const void *mVTable;
    std::string mText;
    uint32_t mRefCount;
    PathString();
    explicit PathString(const char *text);
    PathString(const PathString &other);
    PathString &operator=(const PathString &) = delete;
};
static_assert(sizeof(PathString) == 0x20);

// One row of the lists ListActive and ListEffects fill (0x4c bytes; the default constructor 0xbc4640, the copy 0xbcc640, the
// destructor 0xbc5420): the effect's name, its description index (-1 for an active effect), its variant, the name the renderer
// gives it, and (for an active effect) the effect, counted. Copy and destructor only: a vector of them reallocates by
// copying, as the game's does.
struct EffectListEntry {
    PathString mName;       // +0x00
    int32_t mIndex;         // +0x20 -1
    uint32_t mVariant;      // +0x24 1
    PathString mResolved;   // +0x28 (its count word is the +0x44 the copy sets to 0)
    cIGZUnknown *mObject;   // +0x48
    EffectListEntry();
    EffectListEntry(const EffectListEntry &other);
    ~EffectListEntry();
    EffectListEntry &operator=(const EffectListEntry &) = delete;
};
static_assert(sizeof(EffectListEntry) == 0x4c);

// An alias key: (first, second) message words; hashed as first ^ second.
struct AliasKey {
    uint32_t first;
    uint32_t second;
    bool operator==(const AliasKey &other) const { return first == other.first && second == other.second; }
};
struct AliasKeyHash {
    size_t operator()(const AliasKey &key) const { return key.second ^ key.first; }
};

} // namespace fx

// The pool's effects (cTSVisualEffect, vtable 0xe8f7e0, 400 bytes; lane IT has not taken it to the shape): the slots and
// fields the manager uses.
class cITSVisualEffect : public cIGZUnknown {
public:
    virtual void Slot3(uint32_t) = 0; // 0xbeab90, ret 4
    virtual void Slot4(uint32_t) = 0; // 0xbeacd0, ret 4
    virtual bool Slot5() = 0; // IsRunning (0xbe92c0: the byte at +0x3b)
    virtual bool Slot6() = 0; // the byte at +0x3c (shared 0x78e3d0)
    virtual void SetSuspended(bool on) = 0;  // slot 7 (0x1c)
    virtual void SetHidden(bool on) = 0;     // slot 8 (0x20)
};
// Field views of a pool effect.
struct VisualEffectFields {
    static int32_t EffectIndex(const void *e) { return *reinterpret_cast<const int32_t *>(static_cast<const uint8_t *>(e) + 0x34); }
    static uint8_t Byte(const void *e, uint32_t offset) { return static_cast<const uint8_t *>(e)[offset]; }
    static uint32_t Flags44(const void *e) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(e) + 0x44); }
    static uint32_t RefCount(const void *e) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(e) + 0x10); }
    // The description's first word (+0x40 points at it).
    static uint32_t DescriptionFlags(const void *e)
    {
        return **reinterpret_cast<const uint32_t *const *>(static_cast<const uint8_t *>(e) + 0x40);
    }
};

// cITSEffectsManager (vtable 0xe8cd4c, 45 slots). Names are lane IT's (from the bodies).
class cITSEffectsManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                           // slot 3 (0xbc8fa0)
    virtual bool Shutdown() = 0;                                       // slot 4 (0xbcaa00)
    virtual void SetPaths(const char *first, const char *second) = 0;  // slot 5 (ret 8)
    virtual void AddPaths(const char *first, const char *second) = 0;  // slot 6 (ret 8)
    virtual void SetFlag(uint32_t bit, bool on) = 0;                   // slot 7 (ret 8)
    virtual bool GetFlag(uint32_t bit) = 0;                            // slot 8 (ret 4)
    virtual void LoadEffects() = 0;                                    // slot 9 (0xbc9840; no result: the original leaves eax as it was)
    virtual void ParseResources(const void *keys) = 0;                 // slot 10 (ret 4): a vector of 16-byte resource keys
    virtual bool HasEffectVariant(const void *nameAndVariant) = 0;     // slot 11 (ret 4): a cRZString and a 16-bit variant
    virtual bool HasEffect(const char *name) = 0;                      // slot 12 (ret 4)
    virtual bool CreateEffect(uint32_t first, uint32_t second, void **out) = 0; // slot 13 (ret 0xc, 0xbc61e0)
    virtual bool CreateVisualEffect(const char *name, void **out) = 0; // slot 14 (ret 8, 0xbc6360)
    virtual void SetState(int32_t state) = 0;                          // slot 15 (ret 4)
    virtual void SetMaxDetail(uint32_t detail) = 0;                    // slot 16 (ret 4)
    virtual void SetDetail(float detail) = 0;                          // slot 17 (ret 4)
    virtual void SetTimeMode(int32_t mode) = 0;                        // slot 18 (ret 4)
    virtual void Update(float elapsed, float simElapsed) = 0;          // slot 19 (ret 8, 0xbc6fe0)
    virtual void UpdateMs(uint32_t elapsedMs, uint32_t simElapsedMs) = 0; // slot 20 (ret 8)
    virtual void *GetParams() = 0;                                     // slot 21 (+0xb34)
    virtual void *GetParams2() = 0;                                    // slot 22 (the same body)
    virtual int32_t GetState() = 0;                                    // slot 23 (folded 0x238f10)
    virtual void *GetBCC() = 0;                                        // slot 24
    virtual void SetSlotObject(int32_t index, cIGZUnknown *object) = 0; // slot 25 (ret 8)
    virtual cIGZUnknown *GetSlotObject(int32_t index) = 0;             // slot 26 (ret 4)
    virtual void SetNamedA(const char *name, cIGZUnknown *object) = 0; // slot 27 (ret 8)
    virtual cIGZUnknown *GetNamedA(const char *name) = 0;              // slot 28 (ret 4)
    virtual void SetNamedB(const char *name, cIGZUnknown *object) = 0; // slot 29 (ret 8)
    virtual cIGZUnknown *GetNamedB(const char *name) = 0;              // slot 30 (ret 4)
    virtual void SetDebugBit(uint32_t bit, bool on) = 0;               // slot 31 (ret 8)
    virtual bool GetDebugBit(uint32_t bit) = 0;                        // slot 32 (ret 4)
    virtual void *GetRendererBase() = 0;                               // slot 33
    virtual uint32_t GetEffectNameCount() = 0;                         // slot 34 (folded 0x148ba0: +0x18)
    virtual void PrintStats(void *out) = 0;                            // slot 35 (ret 4)
    virtual void ListActive(void *out, const char *filter) = 0;        // slot 36 (ret 8, 0xbc8760)
    virtual void ListEffects(void *out, const char *filter) = 0;       // slot 37 (ret 8, 0xbc8900)
    virtual int32_t CopyEffectNames(char **buffer, int32_t count, int32_t size, const char *filter) = 0; // slot 38 (ret 0x10)
    virtual void SetScene(cIGZUnknown *scene) = 0;                     // slot 39 (ret 4)
    virtual void SetCamera(cIGZUnknown *camera) = 0;                   // slot 40 (ret 4)
    virtual void SetCameraAndRefresh(cIGZUnknown *camera) = 0;         // slot 41 (ret 4)
    virtual cIGZUnknown *GetScene() = 0;                               // slot 42
    virtual cIGZUnknown *GetCamera() = 0;                              // slot 43
    virtual int32_t GetDebugValue() = 0;                               // slot 44
};

struct cTSVisualChild;
class cITSComponentEffect;

class cTSEffectsManager : public cITSEffectsManager, public cIGZMessageTarget2, public cTSEffectsCollection {
public:
    static constexpr uint32_t kOriginalVTable = 0xe8cd4c;
    static constexpr uint32_t kOriginalVTable4 = 0xe8ce04;
    static constexpr uint32_t kOriginalVTable8 = 0xe8ce18;
    static constexpr uint32_t kInterfaceId = 0xc97cf5cd;
    static constexpr uint32_t kMessageTargetId = 0x452294aa;
    static constexpr size_t kSize = 0xc2c;

    cTSEffectsManager();            // 0xbc4870
    ~cTSEffectsManager() override;  // 0xbc5500; slot 12 of the +8 face: scalar deleting destructor 0xbc5970 (thunk 0xbc591d)
    static cTSEffectsManager *Create(); // 0xbc86f0 (cdecl): new, null on failure

    // cIGZUnknown (all three faces)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xbc9ea0
    uint32_t AddRef() override;                              // folded 0x207d70: ++[+0x80]
    uint32_t Release() override;                             // 0xbca210

    // cITSEffectsManager
    bool Init() override;
    bool Shutdown() override;
    void SetPaths(const char *first, const char *second) override;
    void AddPaths(const char *first, const char *second) override;
    void SetFlag(uint32_t bit, bool on) override;
    bool GetFlag(uint32_t bit) override;
    void LoadEffects() override;
    void ParseResources(const void *keys) override;
    bool HasEffectVariant(const void *nameAndVariant) override;
    bool HasEffect(const char *name) override;
    bool CreateEffect(uint32_t first, uint32_t second, void **out) override;
    bool CreateVisualEffect(const char *name, void **out) override;
    void SetState(int32_t state) override;
    void SetMaxDetail(uint32_t detail) override;
    void SetDetail(float detail) override;
    void SetTimeMode(int32_t mode) override;
    void Update(float elapsed, float simElapsed) override;
    void UpdateMs(uint32_t elapsedMs, uint32_t simElapsedMs) override;
    void *GetParams() override;
    void *GetParams2() override;
    int32_t GetState() override;
    void *GetBCC() override;
    void SetSlotObject(int32_t index, cIGZUnknown *object) override;
    cIGZUnknown *GetSlotObject(int32_t index) override;
    void SetNamedA(const char *name, cIGZUnknown *object) override;
    cIGZUnknown *GetNamedA(const char *name) override;
    void SetNamedB(const char *name, cIGZUnknown *object) override;
    cIGZUnknown *GetNamedB(const char *name) override;
    void SetDebugBit(uint32_t bit, bool on) override;
    bool GetDebugBit(uint32_t bit) override;
    void *GetRendererBase() override;
    uint32_t GetEffectNameCount() override;
    void PrintStats(void *out) override;
    void ListActive(void *out, const char *filter) override;
    void ListEffects(void *out, const char *filter) override;
    int32_t CopyEffectNames(char **buffer, int32_t count, int32_t size, const char *filter) override;
    void SetScene(cIGZUnknown *scene) override;
    void SetCamera(cIGZUnknown *camera) override;
    void SetCameraAndRefresh(cIGZUnknown *camera) override;
    cIGZUnknown *GetScene() override;
    cIGZUnknown *GetCamera() override;
    int32_t GetDebugValue() override;

    // cIGZMessageTarget2
    bool DoMessage(void *message) override; // 0xbc66d0 (this = +4)

    // cITSEffectsCollection overrides (this = +8)
    void Clear() override; // 0xbc5d50
    void Link() override;  // 0xbc73d0

    // Non-virtual members.
    void ReleaseAllEffects();   // 0xbcb1a0
    void KillOrphans();         // 0xbc97f0 (lane YB, ts/cTSEffectCheats.cpp): the effects only the pool holds stopped
    void RefreshDetail();       // 0xbcb210
    void FlushPending();        // 0xbc59d0
    void ApplySettings();       // 0xbc6e10: the option globals read again (Update, Init)
    void CounterBlend();        // 0xbcb3f0
    void ActivityWalk();        // 0xbc6560
    void Reload();              // 0xbc6670
    bool HasEffectName(const std::string &name); // 0xbc8d90 (this = +8)
    cITSVisualEffect *AcquireEffect();           // 0xbc9ab0: a free or new pool effect, queued as pending
    int32_t FindEffectIndex(const std::string &key); // 0xbc8590: the usable variant of the name (lower case), else -1
    int32_t FindAliasIndex(uint32_t first, uint32_t second); // 0xbc8620: the same for a message-alias pair
    // 0xbcb4c0 (ret 0xc): the effect's children brought to the level of detail and debug bits: when reloading all dropped and
    // the effect's index found again; those no longer shown stopped and dropped (once they end, or at once with the stop-now
    // word or bits 0x80800 of `changedBits`), the rest blended on a level change; the components newly shown (or all, with
    // `activate` or reloading) made, placed and started; the stored events told again if any changed.
    void ApplyDetail(cITSVisualEffect *effect, bool activate, uint32_t changedBits);
    // 0xbc6050 (ret 0xc): the component's effect into `child`: a pool effect for type 0, else its description's. Chained
    // components (word bit 1) are linked through `link` (the previous one's Slot10), bit 2 ending the chain.
    void MakeChild(const EffectComponent &component, cTSVisualChild &child, cITSComponentEffect **link);
    // 0xbcb260 (ret 8): event 0x101 to the child: the component's three ranges at this detail, and at the next one.
    void SendChildBlend(const EffectComponent &component, cTSVisualChild &child);
    void ApplyParserState();                     // 0xbc9e20: the renderer's detail and the parser's message
    static void __stdcall SendText(const char *text); // 0xbca590 (ret 4, stdcall): the effects-text message (0x4ba66b69) to the message server
    static void __cdecl OnTimer(void *data);     // 0xbc7350 (cdecl, the data unused): the once-a-second message (0x697d01ed) to the server
    static cITSVisualEffect *NewVisualEffect();  // operator new(400) and the cTSVisualEffect constructor (0xbe86a0)

    using EffectRef = fx::CountedRef<cITSVisualEffect>;
    using UnknownRef = fx::CountedRef<cIGZUnknown>;

    // +0x00 / +0x04 / +0x08 the faces (+0x08 the collection, 0x74 bytes)
    bool mInitialized;                                   // +0x7c
    uint32_t mRefCount;                                  // +0x80
    int32_t mState;                                      // +0x84
    uint32_t mFlags;                                     // +0x88 three bits (slots 7 / 8)
    uint8_t mByte8c;                                     // +0x8c
    bool mMaxDetail;                                     // +0x8d (slots 16 / 17)
    bool mNoFlush;                                       // +0x8e (FlushPending does nothing while set)
    cRZRandomSuper mRandom;                              // +0x90 seeded 0x1105
    bool mByteA58;                                       // +0xa58 (Update)
    bool mReloading;                                     // +0xa59 (Reload)
    int32_t mWordA5C;                                    // +0xa5c 3
    int32_t mTimeMode;                                   // +0xa60 1 (slot 18)
    uint32_t mWordA64;                                   // +0xa64
    float mMaxElapsed;                                   // +0xa68 (Update: a cap on the elapsed time when positive)
    std::set<uint32_t> mMessageIds;                      // +0xa6c the messages DoMessage handles itself
    std::vector<EffectRef> mPool;                        // +0xa74
    std::vector<uint32_t> mPending;                      // +0xa80 pool indices to activate
    std::vector<uint32_t> mActive;                       // +0xa8c
    std::vector<uint32_t> mFree;                         // +0xa98
    UnknownRef mParser;                                  // +0xaa4
    fx::VirtualBaseRef mRenderer;                        // +0xaa8
    std::list<fx::PathString> mPathsA;                   // +0xaac
    std::list<fx::PathString> mPathsB;                   // +0xab4
    int32_t mDetail;                                     // +0xabc -1
    int32_t mPreviousDetail;                             // +0xac0 -1
    float mDetailLevel;                                  // +0xac4 -1
    float mPreviousDetailLevel;                          // +0xac8 -1
    uint32_t mDetailIsMax;                               // +0xacc
    float mDetailBias;                                   // +0xad0
    uint32_t mDebugBits;                                 // +0xad4 (slot 32 reads)
    uint32_t mPreviousDebugBits;                         // +0xad8
    uint32_t mWantedDebugBits;                           // +0xadc (slot 31 writes)
    uint8_t mUnsetAE0[0x18];                             // +0xae0 not written by the constructor
    uint32_t mWordAF8;                                   // +0xaf8 2
    uint32_t mWordAFC;                                   // +0xafc
    uint32_t mWordB00;                                   // +0xb00 (stats: active)
    uint32_t mCounters[12];                              // +0xb04 cleared by Update
    float mParams[3];                                    // +0xb34 1, 1, 1000 (slots 21 / 22 return their address)
    float mDetailFraction;                               // +0xb40
    uint32_t mWordsB44[3];                               // +0xb44
    uint16_t mHalfB50;                                   // +0xb50
    uint8_t mByteB52;                                    // +0xb52
    uint32_t mWordB54;                                   // +0xb54
    fx::CallbackTimerMember mTimer;                      // +0xb58
    std::unordered_map<fx::AliasKey, uint32_t, fx::AliasKeyHash> mAliases; // +0xb80 (Link)
    std::unordered_map<uint32_t, uint32_t> mTriggers;    // +0xba0 message to trigger index (Link)
    std::vector<UnknownRef> mCheats;                     // +0xbc0
    UnknownRef mRefBCC;                                  // +0xbcc
    UnknownRef mSlotObjects[2];                          // +0xbd0 (slots 25 / 26)
    std::unordered_map<std::string, UnknownRef> mNamedA; // +0xbd8 (slots 27 / 28)
    std::unordered_map<std::string, UnknownRef> mNamedB; // +0xbf8 (slots 29 / 30)
    UnknownRef mRefC18;                                  // +0xc18
    UnknownRef mScene;                                   // +0xc1c
    UnknownRef mCamera;                                  // +0xc20
    int32_t mDebugValue[2];                              // +0xc24 -1, -1
};
static_assert(sizeof(cTSEffectsManager) == cTSEffectsManager::kSize, "cTSEffectsManager layout");
static_assert(offsetof(cTSEffectsManager, mInitialized) == 0x7c && offsetof(cTSEffectsManager, mRefCount) == 0x80 &&
                  offsetof(cTSEffectsManager, mRandom) == 0x90 && offsetof(cTSEffectsManager, mByteA58) == 0xa58 &&
                  offsetof(cTSEffectsManager, mMessageIds) == 0xa6c && offsetof(cTSEffectsManager, mPool) == 0xa74 &&
                  offsetof(cTSEffectsManager, mParser) == 0xaa4 && offsetof(cTSEffectsManager, mPathsA) == 0xaac &&
                  offsetof(cTSEffectsManager, mDetail) == 0xabc && offsetof(cTSEffectsManager, mUnsetAE0) == 0xae0 &&
                  offsetof(cTSEffectsManager, mCounters) == 0xb04 && offsetof(cTSEffectsManager, mParams) == 0xb34 &&
                  offsetof(cTSEffectsManager, mHalfB50) == 0xb50 && offsetof(cTSEffectsManager, mTimer) == 0xb58 &&
                  offsetof(cTSEffectsManager, mAliases) == 0xb80 && offsetof(cTSEffectsManager, mTriggers) == 0xba0 &&
                  offsetof(cTSEffectsManager, mCheats) == 0xbc0 && offsetof(cTSEffectsManager, mSlotObjects) == 0xbd0 &&
                  offsetof(cTSEffectsManager, mNamedA) == 0xbd8 && offsetof(cTSEffectsManager, mNamedB) == 0xbf8 &&
                  offsetof(cTSEffectsManager, mRefC18) == 0xc18 && offsetof(cTSEffectsManager, mDebugValue) == 0xc24,
              "cTSEffectsManager members");

// The RVA hooks' entry points (src/hook/tables/cTSEffectsManager.cpp): this in ecx = the face the original expects (+0 for
// the manager's slots and members, +8 for the collection face's), our members called without virtual dispatch.
struct cTSEffectsManagerAdapters {
    using Self = cTSEffectsManager;
    using Face8 = cTSEffectsCollection;
    using Face4 = cIGZMessageTarget2;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall Init(Self *self, void *);
    static bool __fastcall Shutdown(Self *self, void *);
    static void __fastcall SetPaths(Self *self, void *, const char *first, const char *second);
    static void __fastcall AddPaths(Self *self, void *, const char *first, const char *second);
    static void __fastcall SetFlag(Self *self, void *, uint32_t bit, bool on);
    static bool __fastcall GetFlag(Self *self, void *, uint32_t bit);
    static void __fastcall ParseResources(Self *self, void *, const void *keys);
    static bool __fastcall HasEffectVariant(Self *self, void *, const void *nameAndVariant);
    static bool __fastcall HasEffect(Self *self, void *, const char *name);
    static void __fastcall SetState(Self *self, void *, int32_t state);
    static void __fastcall SetMaxDetail(Self *self, void *, uint32_t detail);
    static void __fastcall SetDetail(Self *self, void *, float detail);
    static void __fastcall SetTimeMode(Self *self, void *, int32_t mode);
    static void __fastcall UpdateMs(Self *self, void *, uint32_t elapsedMs, uint32_t simElapsedMs);
    static void *__fastcall GetParams(Self *self, void *);
    static void *__fastcall GetBCC(Self *self, void *);
    static void __fastcall SetSlotObject(Self *self, void *, int32_t index, cIGZUnknown *object);
    static cIGZUnknown *__fastcall GetSlotObject(Self *self, void *, int32_t index);
    static void __fastcall SetNamedA(Self *self, void *, const char *name, cIGZUnknown *object);
    static cIGZUnknown *__fastcall GetNamedA(Self *self, void *, const char *name);
    static void __fastcall SetNamedB(Self *self, void *, const char *name, cIGZUnknown *object);
    static cIGZUnknown *__fastcall GetNamedB(Self *self, void *, const char *name);
    static void __fastcall SetDebugBit(Self *self, void *, uint32_t bit, bool on);
    static bool __fastcall GetDebugBit(Self *self, void *, uint32_t bit);
    static void *__fastcall GetRendererBase(Self *self, void *);
    static void __fastcall PrintStats(Self *self, void *, void *out);
    static int32_t __fastcall CopyEffectNames(Self *self, void *, char **buffer, int32_t count, int32_t size, const char *filter);
    static void __fastcall SetScene(Self *self, void *, cIGZUnknown *scene);
    static void __fastcall SetCamera(Self *self, void *, cIGZUnknown *camera);
    static void __fastcall SetCameraAndRefresh(Self *self, void *, cIGZUnknown *camera);
    static cIGZUnknown *__fastcall GetScene(Self *self, void *);
    static cIGZUnknown *__fastcall GetCamera(Self *self, void *);
    static int32_t __fastcall GetDebugValue(Self *self, void *);
    static void __fastcall Clear(Face8 *face, void *);
    static void __fastcall Link(Face8 *face, void *);
    static bool __fastcall HasEffectName(Face8 *face, void *, const std::string &name);
    static void __fastcall ReleaseAllEffects(Self *self, void *);
    static void __fastcall RefreshDetail(Self *self, void *);
    static void __fastcall FlushPending(Self *self, void *);
    static void __fastcall Reload(Self *self, void *);
    static void __fastcall ApplySettings(Self *self, void *);
    static void __fastcall CounterBlend(Self *self, void *);
    static void __fastcall ActivityWalk(Self *self, void *);
    static void __fastcall Update(Self *self, void *, float elapsed, float simElapsed);
    static void __fastcall LoadEffects(Self *self, void *);
    static bool __fastcall CreateEffect(Self *self, void *, uint32_t first, uint32_t second, void **out);
    static bool __fastcall CreateVisualEffect(Self *self, void *, const char *name, void **out);
    static void __fastcall ListActive(Self *self, void *, void *out, const char *filter);
    static void __fastcall ListEffects(Self *self, void *, void *out, const char *filter);
    static cITSVisualEffect *__fastcall AcquireEffect(Self *self, void *);
    static int32_t __fastcall FindEffectIndex(Self *self, void *, const std::string &key);
    static int32_t __fastcall FindAliasIndex(Self *self, void *, uint32_t first, uint32_t second);
    static void __fastcall ApplyParserState(Self *self, void *);
    static bool __fastcall DoMessage(Face4 *face, void *, void *message);
};
