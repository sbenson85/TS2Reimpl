#pragma once
// nTSCASCore::cTSCASSystem: Create-a-Sim's top-level system service (service id 0x4cf22ab5, priority 0xffd23940; the sim
// COM director's class object 0x2cf22aa2 creates it for cITSCASSystem 0x2cf22a9b). 0x140 bytes, /O2. Lanes IU, JI.
//
// What it holds: three queues of sim-building jobs (+0xf8 new sims, +0x100 children of two parents, +0x108 outfit jobs),
// worked one step per OnTick through the job steppers (0x5083b0, 0x507600 and 0x508c60, all ours); the
// outcome of each finished job by sim id (+0x110); the sim being edited (+0xac, with the outfits it can wear at +0xb0);
// caches of what CAS asked the catalogs before (+0x2c skin-fragment queries by kind, +0x4c fragments by type, +0x6c / +0x8c
// keys by name, +0xd8 "is not the default" by resource group), and the free-will / detail settings CAS mode saves.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xdce5f4, 13 slots), +4 cRZUnknownMT (0xdce62c, 6),
// +0x1c nTSCASCore::cITSCASSystem (0xdce648, 40 slots), +0x20 cIGZMessageTarget2 (0xdce6ec, 4). The original's interface
// slot bodies take `this` as their face; ours are members (the compiler adjusts).
//
// Standalone shape (docs/standalone-shape.md): the creator 0x502860 (registered by address with the sim COM director) is
// the only caller of the constructor 0x4ff850; the vtables are referenced only by the constructor and the destructor
// 0x5006d0. Both hooks on: every system carries our vtables.
#include "cas/CASFragmentFilters.h"      // the filters the gathers hand the catalog
#include "cas/cTSSkinCatalog.h"          // SkinKey / SkinKeyLess (the resource key and the order the game's sets use)
#include "gz/cGZDBSegmentPackedFile.h"   // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZRandom.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace nTSCASCore {

using CASRef = cRZAutoRefCount<cIGZUnknown>;

// A catalog item a gather hands out (0x28 bytes; copy 0x50c7b0, destructor 0x500050): nine counted objects (the fragment,
// its outfit, the property set and what the gatherer 0x506c80 looked up for it) and a flag.
struct CASItem {
    CASRef refs[9];   // +0x00; +0x14 (refs[5]) is the property set the slot 25 filter reads
    bool flag;        // +0x24
};
static_assert(sizeof(CASItem) == 0x28, "CASItem");
// The gathers' out-parameter and the type cache's value (allocator service; growth 0x4fe430).
using CASItemVector = std::vector<CASItem, GZServiceAllocator<CASItem>>;

// A queued new sim (slot 5; node 0x38, copy 0x4ff570 = the implicit copy): stepped by 0x5083b0.
struct NewSimJob {
    uint32_t state;        // +0x00 the stepper's state (0 at the start)
    int32_t age;           // +0x04 slot 5's first argument (-1 -> 0)
    uint32_t arg2;         // +0x08
    uint32_t arg3;         // +0x0c
    uint32_t arg4;         // +0x10
    uint32_t outfitKind;   // +0x14 slot 5's sixth argument: the kind handed to the sim's slot 28 when done
    bool done;             // +0x18 set by the stepper when the sim was built
    CASRef sim;            // +0x1c the sim description the job builds (its slot 8: the sim id)
    CASRef ref20;          // +0x20 (its slot 28: the outfit source for the follow-up outfit job)
    CASRef ref24;          // +0x24
    std::list<CASRef> extras; // +0x28 slot 5's seventh argument, when given
};
static_assert(sizeof(NewSimJob) == 0x30, "NewSimJob");

// A queued child of two parents (slot 6; node 0x28): stepped by StepChildJob 0x507600.
struct ChildJob {
    uint32_t state;        // +0x00 the stepper's state (0 at the start)
    void *parentA;         // +0x04 (not counted; the factory's slot 16 gives its description)
    void *parentB;         // +0x08
    uint32_t kind;         // +0x0c the age kind bit (1, 2, 4, 8; the stepper's "1" is a baby's)
    bool done;             // +0x10 set by the stepper's last state
    CASRef sim;            // +0x14 the child's description (its slot 8: the sim id)
    CASRef ref18;          // +0x18 the description's slot 10 (its slot 28(3): the outfit source)
    CASRef ref1c;          // +0x1c the child's outfit object (the framework's class 0xac598eac), made in state 1
};
static_assert(sizeof(ChildJob) == 0x20, "ChildJob");

// A queued outfit job (slot 8, and the follow-up of a finished sim; node 0x58, constructor 0x4ff710, destructor 0x500180
// (the implicit one), copy in 0x50ca40): stepped by StepOutfitJob 0x508c60.
struct OutfitJob {
    uint32_t state = 0;        // +0x00
    int32_t step = -1;         // +0x04 (the stepper never reads it)
    uint32_t source;           // +0x08 constructor argument 1: the sim's id the factory (slot 17), the UI and dialogs take
    bool result = false;       // +0x0c the outcome filed by sim id when done
    uint32_t kind;             // +0x10 constructor argument 2: bit 0 the outfit set from the stored one (bits 1, 3, 4 how),
                               //       bits 0 / 1 the dialogs told
    uint32_t ageIndex = 0;     // +0x14 the outfit's ages (slot 22) as the UI's index (1 -> 0 .. 0x40 -> 7; others 3)
    uint32_t genderIndex = 0;  // +0x18 the outfit's genders (slot 23): 2 -> 0, others 1
    CASRef sim;                // +0x1c the description (its slot 8: the sim id; the factory's slot 17(source) when none)
    CASRef ref20;              // +0x20 the description's slot 10 (its slot 67's slot 11: the outfit resources' group)
    CASRef ref24;              // +0x24 the outfit object (loaded, iid 0xac598ea8, when none)
    bool hasKeyA = false;      // +0x28
    bool hasKeyB = false;      // +0x29
    SkinKey keyA = {};         // +0x2c the hair key the UI gets instead of the outfit's slot 29
    SkinKey keyB = {};         // +0x3c the key the UI gets instead of the outfit's slot 28
    uint8_t reloadRecord = 0;  // +0x4c slot 8's last argument (its low byte): the outfit's record (0x8ceec300) reloaded

    OutfitJob(uint32_t source_, uint32_t kind_) : source(source_), kind(kind_) {}
};
static_assert(sizeof(OutfitJob) == 0x50, "OutfitJob");

// A cached skin-fragment query (slot 23; node 0x68): the query's arguments and what slot 31 answered.
struct FragmentQuery {
    CASRef bin;                // +0x00 slot 23's first argument (counted)
    uint32_t kind;             // +0x04
    int32_t arg4;              // +0x08
    SkinKey keyA;              // +0x0c
    SkinKey keyB;              // +0x1c
    uint32_t args[4];          // +0x2c arguments 7..10
    SkinKey keyC;              // +0x3c (the zero key)
    uint8_t allowCustom;       // +0x4c AllowCustomContent's byte when asked
    CASItemVector items;       // +0x50
    int32_t count;             // +0x5c
};
static_assert(sizeof(FragmentQuery) == 0x60, "FragmentQuery");

// MSVC 14.40 std::unordered_map / list / set as in the game (unordered_map 0x20 bytes: max load factor, list, bucket
// vector, mask, max index; std::hash<uint32_t> / std::hash<std::string> are the game's FNV-1a).
using QueryCache = std::unordered_map<uint32_t, std::list<FragmentQuery>>;          // node 0x14; destructor 0x4ffc50
using TypeCache = std::unordered_map<uint32_t, CASItemVector>;                      // node 0x18; destructor 0x4ffbf0
using NamedKeys = std::unordered_map<std::string, std::set<SkinKey, SkinKeyLess>>;  // node 0x28; destructor 0x4ffcc0
using FlagCache = std::unordered_map<uint32_t, bool>;                               // node 0x10; destructor 0x15d6a0
static_assert(sizeof(QueryCache) == 0x20 && sizeof(TypeCache) == 0x20 && sizeof(NamedKeys) == 0x20, "unordered_map");
static_assert(sizeof(std::list<NewSimJob>) == 8 && sizeof(std::set<uint32_t>) == 8, "list / set");

// nTSCASCore::cITSCASSystem (iid 0x2cf22a9b): slots 3-39. Arity from each original's `ret N`.
class cITSCASSystem : public cIGZUnknown {
public:
    virtual uint32_t GetEditedSimId() = 0;                                       // 3 (folded 0x145770): +0x28
    virtual void SetEditedSimId(uint32_t id) = 0;                                // 4 (folded 0x149c60)
    // 5 (0x5011d0): a new sim's description (kind -1: from the age 0..3) and its job, queued or (`now`'s low byte) worked
    // to the end with its outfit job; the sim's id, -1 when it failed.
    virtual int32_t CreateSim(int32_t age, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t kind, uint32_t outfitKind,
                              void *extra, uint32_t now) = 0;
    // 6 (0x500e50): the same for a child of two parents (kind -1: a parent's, drawn at random).
    virtual int32_t CreateChild(void *parentA, void *parentB, uint32_t kind, uint32_t now) = 0;
    virtual bool SetEditedSim(void *sim) = 0;                                    // 7 (0x50b590)
    virtual bool RunOutfitJob(uint32_t source, uint32_t kind, const SkinKey *keyA, const SkinKey *keyB,
                              uint32_t flag) = 0;                                // 8 (0x50b750)
    // 9 (0x502490): the outfit of the object a sim id names checked part by part (FixOutfitPart 0x502120 for each body
    // bit and kind; bit 0x100 also a random hair outfit) and, when anything changed, saved; whether it was (true also
    // when the object's slot 23 or 0x54b250 say it needs none). `pickOne`'s low byte: one match per part, not the first.
    virtual bool FixOutfitParts(uint32_t simId, uint32_t pickOne) = 0;
    // 10 (0x5009b0): the special outfit a key names applied to a sim (by id) and shown; false when nothing was found.
    virtual bool ApplySpecialOutfit(const SkinKey *key, uint32_t simId, uint32_t flag) = 0;
    virtual bool IsIdle() = 0;                                                   // 11 (0x5008b0): no job queued
    virtual bool GetResult(uint32_t simId, bool *out) = 0;                       // 12 (0x50b3d0)
    virtual bool IsNotDefault(const SkinKey *key) = 0;                           // 13 (0x504aa0)
    // 14 (0x50a9f0): a not-default content resource's keys (its 0x3b2271bb face's list) registered with the resource
    // service or dropped by the persist system, the caches they touch emptied and the fragment types they change gathered
    // again; false when the key is the default or names no such resource.
    virtual bool RefreshContent(const SkinKey *key) = 0;
    virtual bool IsCustomOrNotDefault(const SkinKey *key) = 0;                   // 15 (0x500cd0)
    virtual bool Slot16(const SkinKey *key) = 0;                                 // 16 (0x50a980)
    virtual bool ResetCaches() = 0;                                              // 17 (0x500d00)
    virtual bool FillTypeCaches() = 0;                                           // 18 (0x506a10)
    // 19 (0x501dd0): the first bin the catalog lists whose name (its string resource's entry) is `name` (case ignored),
    // asked for `iid`; whether it had one.
    virtual bool FindBinByName(const char *name, uint32_t iid, void **out) = 0;
    // 20 (0x506ee0): the sim's name lists (its 44 first and last names in the language's string set) drawn from the name
    // tables for the description's age, or emptied; false without a sim, a description or the sim's info.
    virtual bool FillNames(void *sim, void *description) = 0;
    virtual void ReloadOutfits(void *sim) = 0;                                   // 21 (0x500d30)
    // 22 (0x504910): slot 23 without a bin, into a vector of its own (a8 = a3, a9 = a4, a10 = -2): any fragment.
    virtual bool HasFragments(uint32_t kind, int32_t a4, uint32_t a8, uint32_t a9, const SkinKey *keyA,
                              const SkinKey *keyB, uint32_t a7) = 0;
    // 23 (0x5039b0): eleven dwords (`ret 0x2c`; the game's callers, e.g. 0x757705, push eleven). The body reads ten and
    // passes nine of them on to slot 31; `unused` (+0x30) is never read. Declared with ten, the forwarder left the
    // caller's stack 4 bytes off (lane JJ: the Create-a-Sim crash in 0x5fe69f).
    virtual int32_t QueryFragments(void *bin, CASItemVector *out, uint32_t kind, int32_t a4, const SkinKey *keyA,
                                   const SkinKey *keyB, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10,
                                   uint32_t unused) = 0;
    virtual bool LoadResource(const SkinKey *key, uint32_t iid, void **out) = 0; // 24 (thunk 0x50a970 -> 0x502dc0)
    virtual bool FilterFragments(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t flags,
                                 const SkinKey *keyA, const SkinKey *keyB, uint32_t type) = 0; // 25 (0x50a590)
    virtual bool GetCosmetics(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles,
                              const SkinKey *keyB, const SkinKey *keyA, const SkinKey *keyC, uint32_t parts,
                              uint32_t flags) = 0; // 26 (0x50a0c0): cTSCosmeticsFragFilter
    virtual bool GetArchetypes(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5) = 0; // 27 (0x509f60)
    virtual bool GetModifiers(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5) = 0;  // 28 (0x50a4f0)
    virtual bool GetSkinTones(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5,
                              uint32_t a6) = 0;                                  // 29 (0x50a790)
    virtual bool GetBodyOptions(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6) = 0;                                // 30 (0x50a000)
    // 31 (0x50a840): eleven dwords (`ret 0x2c`); the keys are compared with a skin part's (cTSBinSkinFragFilter).
    virtual bool GetSkinFragments(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                                  const SkinKey *keyB, uint32_t styles, uint32_t categories, uint32_t parts,
                                  uint32_t allowedFlags, const SkinKey *keyC) = 0;
    // 32 (0x502e40): a skin part for body-part bits that fits the masks and keys: the part itself or a stand-in (not
    // counted; none without a part or keyB).
    virtual void *FindSkinPart(uint32_t parts, void *part, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                               const SkinKey *keyB, uint32_t strict) = 0;
    virtual bool ApplyParts(void *target, const SkinKey *key, uint32_t part) = 0; // 33 (0x505de0)
    // 34 (0x503580): the same for a mesh overlay (either key may be none).
    virtual void *FindMeshOverlay(uint32_t parts, void *overlay, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                                  const SkinKey *keyB) = 0;
    // 35 (0x505fb0): the sim's mesh-overlay keys for the part bit (`parts` 13: every bit but 0xc80) rewritten without the
    // overlays whose mesh id (slot 6) is `excludeId` (a zero key) or the id of the mesh `key` names, which is then added
    // with the overlay FindMeshOverlay picks; whether `key` named a mesh overlay (type 8).
    virtual bool ApplyMeshOverlay(void *sim, const SkinKey *key, uint32_t parts, uint32_t excludeId) = 0;
    // 36 (0x501730): the sim's face shown in CAS (`show`'s low byte: with its thumbnail / text); whether its id was.
    virtual bool ShowFace(void *sim, uint32_t show) = 0;
    virtual bool Slot37(uint32_t a1, void *sim, uint32_t ages, uint32_t genders) = 0; // 37 (0x501d20)
    // 38 (0x504230): with flags bit 1, whether the "face" part of the sim's first part key that has one names (its
    // properties' cafOverlay<i> entries, a GUID before a comma) an overlay the resource service lists for the sim's ages,
    // genders, kind and keys, and that overlay's slot 3 says yes (slot 18). Other non-zero flags: the sim's part keys
    // asked for every kind and body part (slot 42), and false.
    virtual bool FindFaceOverlay(void *sim, uint32_t flags) = 0;
    // 39 (0x5028d0): a random outfit of an object definition's group for the ages / genders (uncounted; null for none).
    virtual void *PickRandomOutfit(uint32_t definitionId, uint32_t seed, uint32_t ages, uint32_t genders) = 0;
};

class cTSCASSystem : public rz::cRZSystemService, public cITSCASSystem, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xdce5f4;
    static constexpr uint32_t kVTable4 = 0xdce62c;
    static constexpr uint32_t kCASVTable = 0xdce648;
    static constexpr uint32_t kMessageVTable = 0xdce6ec;
    static constexpr uint32_t kSize = 0x140;
    static constexpr uint32_t kServiceId = 0x4cf22ab5;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0x2cf22a9b;              // cITSCASSystem
    static constexpr uint32_t kMessageTargetIid = 0x452294aa;

    cTSCASSystem();                    // 0x4ff850
    ~cTSCASSystem() noexcept override; // 0x5006d0 (deleting; +4 thunk 0x50067a)
    cTSCASSystem(const cTSCASSystem &) = delete;
    cTSCASSystem &operator=(const cTSCASSystem &) = delete;

    // 0x502860 (__cdecl): the sim COM director's creator: a new system (its +0 face; null when new failed).
    static cTSCASSystem *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x50a910; thunks 0x50a94d (+4), 0x50a955 (+0x1c), 0x50a95d (+0x20)
    uint32_t AddRef() override;                              // folded 0x1cc320 (thunks 0x1cc325 / 0x1cc32d / 0x1cc335)
    uint32_t Release() override;                             // folded 0x1cd7a0 (thunks 0x1cd7a5 / 0x1cd7ad / 0x1cd7b5)

    // ---- cIGZSystemService ----
    bool Init() override;                   // 0x5049d0
    bool Shutdown() override;               // 0x50bb10
    bool OnTick(uint32_t counter) override; // 0x506720

    // ---- cIGZMessageTarget2 (+0x20) ----
    bool DoMessage(void *message) override; // 0x5015c0

    // ---- cITSCASSystem (+0x1c) ----
    uint32_t GetEditedSimId() override { return mEditedSimId; }
    void SetEditedSimId(uint32_t id) override { mEditedSimId = id; }
    int32_t CreateSim(int32_t age, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t kind, uint32_t outfitKind, void *extra,
                      uint32_t now) override;
    int32_t CreateChild(void *parentA, void *parentB, uint32_t kind, uint32_t now) override;
    bool SetEditedSim(void *sim) override;
    bool RunOutfitJob(uint32_t source, uint32_t kind, const SkinKey *keyA, const SkinKey *keyB, uint32_t flag) override;
    bool FixOutfitParts(uint32_t simId, uint32_t pickOne) override;
    bool ApplySpecialOutfit(const SkinKey *key, uint32_t simId, uint32_t flag) override;
    bool IsIdle() override;
    bool GetResult(uint32_t simId, bool *out) override;
    bool IsNotDefault(const SkinKey *key) override;
    bool RefreshContent(const SkinKey *key) override;
    bool IsCustomOrNotDefault(const SkinKey *key) override;
    bool Slot16(const SkinKey *key) override;
    bool ResetCaches() override;
    bool FillTypeCaches() override;
    bool FindBinByName(const char *name, uint32_t iid, void **out) override;
    bool FillNames(void *sim, void *description) override;
    void ReloadOutfits(void *sim) override;
    bool HasFragments(uint32_t kind, int32_t a4, uint32_t a8, uint32_t a9, const SkinKey *keyA, const SkinKey *keyB,
                      uint32_t a7) override;
    int32_t QueryFragments(void *bin, CASItemVector *out, uint32_t kind, int32_t a4, const SkinKey *keyA,
                           const SkinKey *keyB, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10,
                           uint32_t unused) override;
    bool LoadResource(const SkinKey *key, uint32_t iid, void **out) override;
    bool FilterFragments(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t flags,
                         const SkinKey *keyA, const SkinKey *keyB, uint32_t type) override;
    bool GetCosmetics(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles, const SkinKey *keyB,
                      const SkinKey *keyA, const SkinKey *keyC, uint32_t parts, uint32_t flags) override;
    bool GetArchetypes(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5) override;
    bool GetModifiers(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5) override;
    bool GetSkinTones(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override;
    bool GetBodyOptions(void *bin, CASItemVector *out, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override;
    bool GetSkinFragments(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                          const SkinKey *keyB, uint32_t styles, uint32_t categories, uint32_t parts, uint32_t allowedFlags,
                          const SkinKey *keyC) override;
    void *FindSkinPart(uint32_t parts, void *part, uint32_t ages, uint32_t genders, const SkinKey *keyA, const SkinKey *keyB,
                       uint32_t strict) override;
    bool ApplyParts(void *target, const SkinKey *key, uint32_t part) override;
    void *FindMeshOverlay(uint32_t parts, void *overlay, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                          const SkinKey *keyB) override;
    bool ApplyMeshOverlay(void *sim, const SkinKey *key, uint32_t parts, uint32_t excludeId) override;
    bool ShowFace(void *sim, uint32_t show) override;
    bool Slot37(uint32_t a1, void *sim, uint32_t ages, uint32_t genders) override;
    bool FindFaceOverlay(void *sim, uint32_t flags) override;
    void *PickRandomOutfit(uint32_t definitionId, uint32_t seed, uint32_t ages, uint32_t genders) override;

    // 0x5008e0 (__cdecl): the settings' "allowCustomContent" byte (1 without a settings service or the setting). Hooked:
    // game code (the forwarded slots) calls it.
    static uint8_t __cdecl AllowCustomContent();

    // ---- the gathers (non-slot members, `this` the +0 face) ----
    // 0x509b90: the fragments of a type the filter accepts, from the type cache (filled first when the type is missing),
    // or (cache off) from the catalog's slot 30 over the type's bins. Whether any. Hooked (the filter slots call it).
    bool GatherByType(CASItemVector *out, uint32_t type, cITSFragEntryFilter *filter);
    // 0x50a1d0: the fragments of one bin the filter (each once without one) accepts (the catalog's slot 32). Hooked.
    bool GatherFromBin(CASItemVector *out, cIGZUnknown *bin, cITSFragEntryFilter *filter);
    // 0x50bc00: the type cache's entry for a type, from the catalog's slot 30 (each fragment once). Hooked (0x503e20).
    void FillTypeCache(uint32_t type);
    // 0x506c80: the item for a fragment of a type: the resource its key names in the type's place (two tries for type 8;
    // type 0x20000 by the key's own type), then the fragment and whether its slot 27 is zero.
    bool GatherItem(uint32_t type, cIGZUnknown *fragment, CASItem *item);
    // 0x502c00: the resource a key names, loaded into the item's place for the key's type. Hooked (0x503e20).
    bool LoadByKeyType(const SkinKey *key, CASItem *item);

    // ---- CAS mode (DoMessage's; lane JS) ----
    // 0x50b960 (424 B): in CAS mode; the settings' "freeWill" saved (its raw byte) and, when set, turned off; the app
    // options' 0x21 saved and given bits 6; the display settings' "ObjectDetail" / "Reflection" saved and set to 3 / 1.
    void EnterCASMode();
    // 0x50b830 (293 B): out of CAS mode; "freeWill" back on when it was, option 0x21 and the display settings restored.
    void LeaveCASMode();

    // ---- the job steppers (CreateSim / CreateChild / OnTick / RunOutfitJob; lane JT) ----
    // 0x5083b0 (2081 B, ret 4): one step of a queued new sim (states 0..7): its outfit object made and set up, the
    // description's ages / genders / data, the extras matched against the outfit, the parts applied, the description
    // filed with the app's tables, the names and the outfit's name, the app's hook. True while steps are left; state 7
    // marks the job done and answers false (as does no app).
    bool StepNewSimJob(NewSimJob *job);
    // 0x507600 (3440 B, ret 4; lane JU): one step of a queued child of two parents (states 0..6): the parents' outfits
    // queued for loading, the child's description data and its outfit object, the description filed with the app's tables
    // (with its hair, face and genes from the parents' look), the outfit's tidy-up, its name from the string tables and the
    // parents' last name, the app's world told. True while steps are left; state 6 marks the job done and answers false.
    bool StepChildJob(ChildJob *job);
    // 0x508c60 (3752 B, ret 4; lane JV): one step of a queued outfit job (states 0..7): the description, its outfit
    // object and the UI's view of it (keys, thumbnail, name), the stored outfit's stale part keys dropped and (kind bit 0)
    // the outfit set from it; the parts the stored outfit lists put on the outfit body part by part; the outfit saved;
    // the dialogs told; the result from the stored outfit; the framework's class 0xcdcbe0e3 told. True while steps are left; state 7 answers
    // false (as does a description without a usable source in state 0, with the result false).
    bool StepOutfitJob(OutfitJob *job);

    // ---- non-slot members still game code (called through OriginalFunction) ----
    // 0x503e20 (1035 B; only ApplySpecialOutfit calls it): the special-outfit items for a key (its resource, iid 0x6c4f3599,
    // of type 0x20000) and a sim's outfit, through a cTSSpecialOutfitFragFilter (from the type cache, or the bin catalog's
    // slot 32 over the key's bin). Whether the key named such a resource.
    bool SpecialOutfitItems(const SkinKey *key, void *outfit, uint32_t flag, CASItemVector *out);
    cIGZMessageTarget2 *MessageTarget() { return this; }
    cITSCASSystem *Face() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c, +0x20: the faces' vtables
    bool mCachesFilled;            // 0x24 FillTypeCaches ran (cleared by ResetCaches)
    bool mCacheTypes;              // 0x25 the type cache is used (true)
    uint8_t mPad26[2];             // 0x26 never written
    uint32_t mEditedSimId;         // 0x28 the edited sim's id (its slot 41; -1 when none)
    QueryCache mQueries;           // 0x2c kind -> the skin-fragment queries answered (slot 23)
    TypeCache mTypes;              // 0x4c type -> the fragments gathered (0x509b90)
    NamedKeys mNamedKeysA;         // 0x6c
    NamedKeys mNamedKeysB;         // 0x8c
    CASRef mEditedSim;             // 0xac the sim being edited (slot 7)
    std::list<void *> mEditedOutfits; // 0xb0 its outfits that pass its slot 29 (not counted)
    cRZRandom mRandom;             // 0xb8 seeded from the clock (constructor, Init)
    uint8_t mUnusedC4[0xc];        // 0xc4 never written
    std::set<uint32_t> mMessageIds; // 0xd0 0xd9, 0xda, 0xdb, 0xb5, 0x91a4f0d0 (Init registers each)
    FlagCache mNotDefault;         // 0xd8 resource group -> IsNotDefault's answer
    std::list<NewSimJob> mNewSims; // 0xf8
    std::list<ChildJob> mChildren; // 0x100
    std::list<OutfitJob> mOutfitJobs; // 0x108
    FlagCache mResults;            // 0x110 sim id -> its finished job's outcome (slot 12)
    int32_t mObjectDetail;         // 0x130 the "ObjectDetail" setting CAS mode saved (3)
    int32_t mReflection;           // 0x134 the "Reflection" setting (0)
    uint8_t mFreeWill;             // 0x138 the "freeWill" setting's raw byte before CAS mode (1)
    bool mInCASMode;               // 0x139
    uint16_t mSavedOption21;       // 0x13a the app's option 0x21 before CAS mode
    uint32_t mUnused13C;           // 0x13c never written
};

static_assert(sizeof(cTSCASSystem) == cTSCASSystem::kSize, "cTSCASSystem size");
static_assert(offsetof(cTSCASSystem, mCachesFilled) == 0x24, "mCachesFilled");
static_assert(offsetof(cTSCASSystem, mEditedSimId) == 0x28, "mEditedSimId");
static_assert(offsetof(cTSCASSystem, mQueries) == 0x2c, "mQueries");
static_assert(offsetof(cTSCASSystem, mTypes) == 0x4c, "mTypes");
static_assert(offsetof(cTSCASSystem, mNamedKeysA) == 0x6c, "mNamedKeysA");
static_assert(offsetof(cTSCASSystem, mNamedKeysB) == 0x8c, "mNamedKeysB");
static_assert(offsetof(cTSCASSystem, mEditedSim) == 0xac, "mEditedSim");
static_assert(offsetof(cTSCASSystem, mEditedOutfits) == 0xb0, "mEditedOutfits");
static_assert(offsetof(cTSCASSystem, mRandom) == 0xb8, "mRandom");
static_assert(offsetof(cTSCASSystem, mMessageIds) == 0xd0, "mMessageIds");
static_assert(offsetof(cTSCASSystem, mNotDefault) == 0xd8, "mNotDefault");
static_assert(offsetof(cTSCASSystem, mNewSims) == 0xf8, "mNewSims");
static_assert(offsetof(cTSCASSystem, mChildren) == 0x100, "mChildren");
static_assert(offsetof(cTSCASSystem, mOutfitJobs) == 0x108, "mOutfitJobs");
static_assert(offsetof(cTSCASSystem, mResults) == 0x110, "mResults");
static_assert(offsetof(cTSCASSystem, mObjectDetail) == 0x130, "mObjectDetail");
static_assert(offsetof(cTSCASSystem, mFreeWill) == 0x138, "mFreeWill");
static_assert(offsetof(cTSCASSystem, mSavedOption21) == 0x13a, "mSavedOption21");

// 0x501f80 (__cdecl; hooked: game code called it from slot 38): the first part of a model whose name list holds `name`
// (without case), handed out counted with that list; both null when none.
void __cdecl FindNamedPart(void *model, const char *name, void **part, void **names);

// 0x502120 (__cdecl; only slot 9 calls it): an outfit's part keys of a kind for a body bit (its slot 42) kept when its slot
// 101 accepts them and each part (persist, 0xebcf3e20) fits the outfit's ages / genders (slots 22 / 23), the bit and the
// kind: false. Else (no keys, or a misfit) a part the resource service picks through a cTSOutfitMatchSkinFilter for the
// outfit (slot 38, one match, when `pickOne`'s low byte is set; else slot 12's first) set on the outfit (slot 73); whether
// one was.
bool __cdecl FixOutfitPart(void *outfit, uint32_t kind, uint32_t bit, uint32_t pickOne);

// The hooks' entry points: the constructor (placement new of ours) for the creator's call; the creator itself is hooked
// directly (Create).
struct cTSCASSystemAdapters {
    static cTSCASSystem *__fastcall Construct(void *memory, void *edx); // 0x4ff850
    // 0x502dc0 (the body behind slot 24's thunk): also called directly by 0x502c00 and 0x506c80 (game code), `this` the
    // system's +0 face.
    static bool __fastcall LoadResource(cTSCASSystem *self, void *edx, const SkinKey *key, uint32_t iid, void **out);
    static bool __fastcall GatherByType(cTSCASSystem *self, void *edx, CASItemVector *out, uint32_t type,
                                        cITSFragEntryFilter *filter);                                   // 0x509b90
    static bool __fastcall GatherFromBin(cTSCASSystem *self, void *edx, CASItemVector *out, cIGZUnknown *bin,
                                         cITSFragEntryFilter *filter);                                  // 0x50a1d0
    static void __fastcall FillTypeCache(cTSCASSystem *self, void *edx, uint32_t type);               // 0x50bc00
    static bool __fastcall LoadByKeyType(cTSCASSystem *self, void *edx, const SkinKey *key, CASItem *item); // 0x502c00
};

} // namespace nTSCASCore
