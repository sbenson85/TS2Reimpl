#pragma once
// The gather world of the FindBestAction tests: the score world (tests/simantics_autonomy_scoreworld.h, which has the check world and the
// person world below it) with the fakes the gather step (GatherInteractions 0x5155b0 and the interaction test sim's gather members
// 0x44fb90, 0x44fbc0, 0x44ff80, 0x4519c0, 0x44f760) reaches added: the director's interaction factory (through the COM director
// singleton's vtable in the image, so the original's 0x1ae18d and ours reach the same fake), the object module that hands out the
// interaction groups, the stack objects' interaction table, the person's pending queue, the interface query a stack object answers with
// another Sim, and the setup / flag slots of the new interactions.
//
// The check-tree runner (0x451f00 against ours), the allocator service (the game's own vtable over the game allocator) and the vector
// helpers (0x1d57e0) are real on both sides. The test sim, the list it appends to, the object table and the interaction groups are
// heap objects of the world's arena, described into the trace and torn down after each run.
#include "simantics_autonomy_scoreworld.h"

#include "rz/RZMemory.h"
#include "simantics/EdithObjectTestSim.h"
#include "simantics/ServiceRefVector.h"

#include <memory>
#include <unordered_set>

namespace {

// (All 24 fakes of the harness are taken: the gather roles share fakes whose slots do not meet.)
constexpr int kFramework = kSink;     // slot 3 (the director framework's factory)
constexpr int kTable = kHolder;       // slots 9, 12, 19 (a stack object's interaction table)
constexpr int kModule = kTarget;      // slots 48, 49 (the object module)
constexpr int kOtherPerson = kActor;  // the cITSPerson another Sim answers with (slots 132, 38 and the actor's)

constexpr uint32_t kPersonInterfaceId = 0x5a69d3d5;
constexpr uint32_t kGlobalMenuSim = 0x10da960;
constexpr uint32_t kGlobalMenuList = 0x10da964;
constexpr int kStackObjectSlots = 320; // (the stack objects answer slot 280)

// One interaction group the module hands out (a node of its list): a stack object and entry indexes of its table.
struct GatherNode {
    GatherNode *next;
    GatherNode *prev;
    void *stackObject;
    std::vector<uint32_t> indexes;
};
static_assert(sizeof(GatherNode) == 0x18, "GatherNode layout");

// Everything of a world that is not in its fixed memory.
struct GatherArena {
    alignas(16) uint8_t simBytes[sizeof(cEdithObjectTestSim)];
    std::vector<uint32_t> entryIndexes; // behind the sim's mEntryIndexes
    ServiceRefVector list;              // the list the direct tests append to
    std::vector<std::unique_ptr<GatherNode>> nodes;
    std::vector<std::pair<GatherNode *, GatherNode *>> ranges; // what the module's slot 48 hands out
    uint8_t *registryBytes = nullptr;
    void *objectTable[6] = {};
    uint32_t created = 0; // interactions the factory has made in the current run
    bool consistent = false; // the objects are what the search needs later: numbered by their place in the table, always resolved
    bool autonomy = false; // the run is an autonomy gather (the original does not look at the answer of the person interface query: it is never none)
    uint32_t ownerIds[6] = {};
    uint32_t ownerIdCount = 0;
    cEdithObjectTestSim *Sim() { return reinterpret_cast<cEdithObjectTestSim *>(simBytes); }
};

std::map<const WorldP *, std::unique_ptr<GatherArena>> gArenas;

GatherArena &ArenaOf(const WorldP &w)
{
    return *gArenas.at(&w);
}

// The stack objects' own vtables (slot 280 is past the harness's 256-slot ones).
void *gBigStackVt[kObjects][kStackObjectSlots];

void *gDirectorVt[16];
uint32_t gOwnerIdKeys[4];

// ---- fakes -----------------------------------------------------------------------------------------------------------------

// The COM director singleton's slot 12: the framework fake.
void *__fastcall DirectorFramework(void *, void *)
{
    return &gW->fk[kFramework];
}

// The framework's CreateInstance (class id, interface id, out): hands out the world's interactions in turn.
uint32_t __fastcall CreateInstanceFake(void *self, void *, uint32_t classId, uint32_t interfaceId, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 3u);
    gTrace.push_back(classId);
    gTrace.push_back(interfaceId);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    GatherArena &arena = ArenaOf(*gW);
    *out = &Interaction(*gW, arena.created++);
    return Scripted(Off(self), 3);
}

// An interaction's SetUp (person, stack object, entry index, source kind, extra).
uint32_t __fastcall SetUpFake(void *self, void *, uint32_t person, uint32_t stackObject, uint32_t entryIndex, uint32_t kind, uint32_t extra)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(8u << 8 | 5u);
    for (uint32_t v : {person, stackObject, entryIndex, kind, extra}) gTrace.push_back(Norm(v));
    return 1;
}

// An interaction's SetName (a game string): the text.
uint32_t __fastcall SetNameFake(void *self, void *, const EmbeddedString *name)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(26u << 8 | 1u);
    gTrace.push_back(TextCode(name->mText));
    return 0;
}

// The table's slot 12 (position): an entry of the world.
uint32_t __fastcall TableEntryAtFake(void *self, void *, int32_t position)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(12u << 8 | 1u);
    gTrace.push_back(static_cast<uint32_t>(position));
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->fk[kT0 + static_cast<uint32_t>(position) % kEntries]));
}

// The table's slot 19 (a game string to fill with the table's name).
uint32_t __fastcall TableNameFake(void *self, void *, EmbeddedString *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    out->mText.assign("Table");
    return 1;
}

// The module's slot 48 (function, life stage, ranges): the arena's groups, as ranges of the module's list.
uint32_t __fastcall ModuleGroupsFake(void *self, void *, uint32_t function, uint32_t lifeStage, ServiceBlockVector *ranges)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(48u << 8 | 3u);
    gTrace.push_back(function);
    gTrace.push_back(lifeStage);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(ranges)));
    GatherArena &arena = ArenaOf(*gW);
    if (!arena.ranges.empty()) {
        const uint32_t count = static_cast<uint32_t>(arena.ranges.size());
        auto *block = static_cast<GatherNode **>(RZMemory::Malloc((count + 1) * 8));
        for (uint32_t i = 0; i < count; ++i) {
            block[i * 2] = arena.ranges[i].first;
            block[i * 2 + 1] = arena.ranges[i].second;
        }
        ranges->mFirst = block;
        ranges->mLast = block + count * 2;
        ranges->mEnd = block + (count + 1) * 2;
    }
    return 0;
}

// The module's slot 49 (block, life stage): fills the 16-dword function table.
uint32_t __fastcall ModuleTableFake(void *self, void *, uint32_t *out, uint32_t lifeStage)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(49u << 8 | 2u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    gTrace.push_back(lifeStage);
    for (int i = 0; i < 16; ++i) out[i] = Mix(gSeed, 49, i);
    return 0;
}

// The person's cITSPerson slot 150 (set, owner id): puts some owner ids into the set the gather filters by.
uint32_t __fastcall FillOwnerIdsFake(void *self, void *, OwnerIdSet *set, int32_t ownerId)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(150u << 8 | 2u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(set)));
    gTrace.push_back(static_cast<uint32_t>(ownerId));
    const GatherArena &arena = ArenaOf(*gW);
    for (uint32_t i = 0; i < arena.ownerIdCount; ++i) set->insert(arena.ownerIds[i]);
    return Scripted(Off(self), 150);
}

// The person's cITSPerson slot 10 (field, flag): Person Data. The autonomy level (field 0x24) is a small number, so the entries'
// thresholds decide; other fields answer from the script, in order.
uint32_t __fastcall PersonDataFake(void *self, void *, uint32_t field, uint32_t flag)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(10u << 8 | 2u);
    gTrace.push_back(field);
    gTrace.push_back(flag);
    if (field == 0x24) return (Mix(gSeed, 0x24, 7) & 0xffff0000u) | static_cast<uint16_t>(static_cast<int16_t>(Mix(gSeed, 0x24, 8) % 9) - 3);
    return Scripted(Off(self), 10);
}

// A stack object's TestFlags (mask): the answer depends on the mask, so each flag has its own.
uint32_t __fastcall TestFlagsFake(void *self, void *, uint32_t mask)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(25u << 8 | 1u);
    gTrace.push_back(mask);
    const uint32_t chance = mask == 0x10000 ? 8 : (mask == 0x2000 ? 60 : 40);
    const uint32_t h = Mix(gSeed, Off(self), mask);
    return (h % 100 < chance) ? (h & ~0xffu) | (1 + h % 255) : (h & ~0xffu);
}

// A stack object's QueryInterface: the person interface gives another Sim, the person itself or nothing; other interfaces as the score world.
uint32_t __fastcall GatherQueryFake(void *self, void *, uint32_t iid, void **out)
{
    if (iid != kPersonInterfaceId) return ScoreQueryFake(self, nullptr, iid, out);
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    uint32_t pick = Mix(gSeed, Off(self), 0xd5) % 8;
    if (ArenaOf(*gW).autonomy && pick >= 6) pick = 0;
    *out = pick < 5 ? static_cast<void *>(&gW->fk[kOtherPerson]) : (pick < 6 ? static_cast<void *>(gW->obj + 0x2f0) : nullptr);
    // (A success without an interface would crash the original; the answer is zero then.)
    const uint32_t success = Scripted(Off(self), 0x1000 | 0xd5);
    return *out ? success : 0u;
}

// A stack object's GetObjectKey / GetResolvedObject-style identity: the object itself.
uint32_t __fastcall ReturnSelfFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(95u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
}

// Slot 280 of a stack object: itself, another stack object, or nothing.
uint32_t __fastcall ResolvedObjectFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(280u << 8);
    uint32_t pick = Mix(gSeed, Off(self), 280) % 8;
    if (pick == 0 && ArenaOf(*gW).consistent) pick = 2;
    if (pick == 0) return 0;
    if (pick == 1) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->fk[kS0 + Mix(gSeed, Off(self), 281) % kObjects]));
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
}

// The tree sim's CallTree, as the check world's, but sometimes a primitive of the tree lists an interaction on the way (as the menu
// builder's list global lets the action-string primitive do).
ServiceRefVector *gTreeList = nullptr;
uint32_t __fastcall CallTreeAndList(void *self, void *, uint32_t owner, uint32_t id1a, uint32_t treeId, uint32_t id18, uint32_t id1c, const uint16_t *args,
                                    uint32_t result, uint32_t flag16)
{
    const uint32_t answer = CallTreeAndFlag(self, nullptr, owner, id1a, treeId, id18, id1c, args, result, flag16);
    if (gTreeList && Mix(gSeed, 0x7ee, gHitCount) % 5 == 0) gTreeList->PushBack(&Interaction(*gW, Mix(gSeed, 0x7ef, gHitCount)));
    return answer;
}

// ---- the world ---------------------------------------------------------------------------------------------------------------

struct GatherParams {
    bool nullStackObjects = false; // created interactions may lack a stack object
    bool consistent = false;       // the objects are numbered by their place in the table, which is complete, and always resolve
    bool selfInteraction = false;  // the last interaction stays on the person itself (the score world's; a check would run on the person)
    uint32_t maxGroups = 3;
};

// The state of the game globals the gather reaches, saved while a test runs.
struct GatherGlobals {
    AutonomyGlobals base;
    struct Word {
        uint32_t rva;
        uint32_t value;
    };
    std::vector<Word> words;
    explicit GatherGlobals(difftest::Context &)
    {
        for (uint32_t rva : {kGlobalMenuSim, kGlobalMenuList, 0x10c9390u, 0x10c93f4u, 0x10c95d4u, 0x10c95d8u})
            words.push_back({rva, *GameAddress<uint32_t>(rva)});
    }
    GatherGlobals(const GatherGlobals &) = delete;
    ~GatherGlobals()
    {
        for (const Word &word : words) *GameAddress<uint32_t>(word.rva) = word.value;
    }
};

// While alive, the allocator service cache of the image holds an object with the game's own service vtable over the game allocator.
struct AllocatorServiceObject {
    alignas(8) uint8_t object[16];
    AllocatorServiceObject() { std::memset(object, 0, sizeof(object)); }
};
AllocatorServiceObject gServiceObject;
void *ServiceObjectAddress()
{
    // (Filled when used: the image is not mapped yet while static objects are built.)
    const void *vtable = GameAddress<void>(0xda3cd4);
    std::memcpy(gServiceObject.object, &vtable, sizeof(vtable));
    return gServiceObject.object;
}

void BuildGatherWorld(WorldP &w, uint32_t seed, uint32_t salt, const GatherParams &params)
{
    if (gArenas.size() >= 2) gArenas.clear(); // (the arenas of the last trial; a trial builds two worlds)
    BuildScoreWorld(w, seed, salt);
    std::mt19937 r(seed * 2654435761u + 0x9a7);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    QuietBuild quiet;
    PrimPersonObject *self = Person(w);

    std::unique_ptr<GatherArena> &arenaSlot = gArenas[&w];
    arenaSlot = std::make_unique<GatherArena>();
    GatherArena &arena = *arenaSlot;

    // The director chain: the singleton's vtable (slot 12), the framework (slot 3).
    gDirectorVt[12] = reinterpret_cast<void *>(&DirectorFramework);
    w.vtFk[kFramework][3] = reinterpret_cast<void *>(&CreateInstanceFake);
    RetValue(&Fk(w, kFramework), 3, r(), w);

    // The new interactions: the setup, name and flag slots, the string id (listed only when positive), the flag 0x10.
    for (int i = 0; i < kInteractions; ++i) {
        Fake &f = Fk(w, kI0 + i);
        f.vt[8] = reinterpret_cast<void *>(&SetUpFake);
        f.vt[26] = reinterpret_cast<void *>(&SetNameFake);
        for (int slot : {7}) Use(f.vt, slot, 2);
        for (int slot : {14, 44, 48}) Use(f.vt, slot, 1);
        for (int slot : {13, 41, 78}) Use(f.vt, slot, 0);
        OverrideValue(w, &f, 13, chance(80) ? 1 + r() % 0x7ffe : (chance(50) ? 0u : (0x8000u | r() % 0x7fff)) | (r() & 0xffff0000u));
        OverrideValue(w, &f, 41, NoiseAl(r, chance(20)));
        RetValue(&f, 78, NoiseAl(r, chance(40)), w);
        OverrideValue(w, &f, 39, NoiseAl(r, chance(65)));
        OverrideValue(w, &f, 19, chance(60) ? (r() & 0xffff0000u) | 1 : (r() & 0xffff0000u));
        OverrideValue(w, &f, 20, chance(35) ? (r() & 0xffff0000u) | 1 : (r() & 0xffff0000u));
        if (params.nullStackObjects && i >= kInteractions - 2) OverrideObject(w, &f, 16, nullptr);
        else if (!params.selfInteraction && i == kInteractions - 1) OverrideObject(w, &f, 16, &Fk(w, kS0 + r() % kObjects));
    }

    // The table entries: the autonomy threshold, the function test (their other slots are the score world's).
    for (int t = 0; t < kEntries; ++t) {
        Fake &f = Fk(w, kT0 + t);
        Use(f.vt, 56, 2);
        Use(f.vt, 87, 0);
        RetValue(&f, 87, static_cast<uint32_t>(static_cast<int32_t>(r() % 9) - 3), w);
        RetValue(&f, 56, NoiseAl(r, chance(35)), w);
        OverrideValue(w, &f, 18, chance(80) ? 0xffffffffu : 0x1u << (r() % 6));
        OverrideValue(w, &f, 39, NoiseAl(r, chance(10)));
    }

    // The stack objects: the identity slots, the table, the flag slots, a longer vtable for slot 280.
    for (int s = 0; s < kObjects; ++s) {
        Fake &f = Fk(w, kS0 + s);
        std::memcpy(gBigStackVt[s], w.vtFk[kS0 + s], sizeof(w.vtFk[0]));
        for (int slot = 256; slot < kStackObjectSlots; ++slot) gBigStackVt[s][slot] = nullptr;
        f.vt = gBigStackVt[s];
        f.vt[0] = reinterpret_cast<void *>(&GatherQueryFake);
        f.vt[25] = reinterpret_cast<void *>(&TestFlagsFake);
        f.vt[95] = reinterpret_cast<void *>(&ReturnSelfFake);
        f.vt[280] = reinterpret_cast<void *>(&ResolvedObjectFake);
        Use(f.vt, 87, 1);
        Use(f.vt, 188, 0);
        Use(f.vt, 13, 1);
        Use(f.vt, 47, 0);
        RetObject(&f, 87, chance(85) ? static_cast<const void *>(&Fk(w, kTable)) : nullptr, w);
        for (int i = 0; i < 8; ++i) EnqueueValue(&f, 188, NoiseAl(r, chance(i % 2 == 0 ? 92 : 40)), w);
        OverrideValue(w, &f, 13, chance(88) ? (r() & 0xffff0000u) : (r() & 0xffff0000u) | (1 + r() % 7));
        // The object numbers: usually the object's place in the object table, sometimes something the table does not have.
        const uint32_t numbers[] = {1u + s, 1u + s, 1u + s, 1u + s, 1u + s, 0u, 7u, 0xffffu};
        OverrideValue(w, &f, 47, Val16Noise(r) & 0xffff0000u | (params.consistent ? 1u + s : numbers[r() % 8]));
        OverrideValue(w, &f, 98, chance(35) ? 2u : r() % 5);
        OverrideValue(w, &f, 131, (r() & 0xffff0000u) | (chance(10) ? 0xffffu : r() % 4));
        OverrideValue(w, &f, 190, NoiseAl(r, chance(7)));
    }

    // The table.
    Fake &table = Fk(w, kTable);
    Use(table.vt, 9, 0);
    RetValue(&table, 9, r() % 4, w);
    table.vt[12] = reinterpret_cast<void *>(&TableEntryAtFake);
    table.vt[19] = reinterpret_cast<void *>(&TableNameFake);

    // The other Sim (what a stack object's person interface query gives): its current interaction (a world interaction) and life stage.
    Fake &other = Fk(w, kOtherPerson);
    Use(other.vt, 132, 0);
    Use(other.vt, 2, 0);
    Use(other.vt, 10, 2); // (the scoring asks a Sim stack object's person for its kind)
    RetValue(&other, 10, chance(30) ? r() & 0xffff0000u : Val16Noise(r) % 4, w);
    Use(w.vtPerson, 2, 0);
    RetObject(&other, 132, &Interaction(w, r()), w);

    // The person.
    self->mVTable[63] = gThunk[63][0]; // (the person's primary vtable is the score world's longer copy)
    RetObject(w.obj, 63, &Fk(w, kModule), w);
    self->mVTable[19] = gThunk[19][1];
    RetValue(w.obj, 19, chance(80) ? (r() & 0xffff0000u) | (r() % 4) : Edge16(r), w);
    self->mVTable[131] = gThunk[131][0];
    RetValue(w.obj, 131, (r() & 0xffff0000u) | (chance(10) ? 0xffffu : r() % 4), w);
    w.vtPerson[150] = reinterpret_cast<void *>(&FillOwnerIdsFake);
    RetValue(&self->mPersonVTable, 150, NoiseAl(r, chance(70)), w);
    w.vtPerson[10] = reinterpret_cast<void *>(&PersonDataFake);
    for (int i = 0; i < 4; ++i) EnqueueValue(&self->mPersonVTable, 10, chance(30) ? r() & 0xffff0000u : Val16Noise(r), w);
    Use(w.vtPerson, 52, 0);
    RetValue(&self->mPersonVTable, 52, chance(85) ? 0x1u << (r() % 6) : 0u, w);
    Use(w.vtPerson, 126, 0);
    Use(w.vtPerson, 127, 0);
    Use(w.vtPerson, 128, 1);
    Use(w.vtPerson, 129, 1);
    RetValue(&self->mPersonVTable, 126, (r() & 0xffff0000u) | (chance(20) ? 0u : (chance(50) ? 5 + r() % 5 : r() % 5)), w);
    RetValue(&self->mPersonVTable, 127, (r() & 0xffff0000u) | (chance(25) ? 5 + r() % 3 : r() % 5), w);
    RetValue(&self->mPersonVTable, 128, NoiseAl(r, chance(40)), w);
    for (int i = 0; i < 12; ++i) EnqueueObject(&self->mPersonVTable, 129, &Interaction(w, r()), w);
    Use(w.vtPerson, 38, 0);

    // The module.
    Fake &module = Fk(w, kModule);
    module.vt[48] = reinterpret_cast<void *>(&ModuleGroupsFake);
    module.vt[49] = reinterpret_cast<void *>(&ModuleTableFake);

    // The recorder the menu hands the runner: the cached service C (the scoring object fake) answers slot 63.
    Use(Fk(w, kScoringObject).vt, 63, 0);
    RetObject(&Fk(w, kScoringObject), 63, &Fk(w, kRecorder), w);

    // The object table (numbers 1..): the stack objects, with holes.
    arena.registryBytes = new uint8_t[sizeof(ObjectRegistry)]();
    for (int s = 0; s < 6; ++s) arena.objectTable[s] = s < kObjects && (params.consistent || !chance(12)) ? static_cast<void *>(&Fk(w, kS0 + s)) : nullptr;
    arena.consistent = params.consistent;
    auto *registry = reinterpret_cast<ObjectRegistry *>(arena.registryBytes);
    const uint32_t tableSize = params.consistent ? 6 : (chance(15) ? 2 : (chance(10) ? 0 : 6));
    registry->mObjectsBegin = arena.objectTable;
    registry->mObjectsEnd = arena.objectTable + tableSize;
    self->mObjectRegistry = registry;

    // The owner ids the gather accepts, and the interaction groups.
    if (chance(50)) {
        arena.ownerIdCount = 4;
        for (uint32_t i = 0; i < 4; ++i) arena.ownerIds[i] = i;
    } else {
        arena.ownerIdCount = r() % 5;
        for (uint32_t i = 0; i < arena.ownerIdCount; ++i) arena.ownerIds[i] = chance(10) ? 0xffffffffu : r() % 4;
    }
    const uint32_t rangeCount = r() % (params.maxGroups + 1);
    for (uint32_t i = 0; i < rangeCount; ++i) {
        const uint32_t count = chance(10) ? 0 : 1 + r() % 3;
        GatherNode *first = nullptr, *previous = nullptr;
        for (uint32_t n = 0; n < count; ++n) {
            auto node = std::make_unique<GatherNode>();
            node->next = nullptr;
            node->prev = previous;
            node->stackObject = &Fk(w, kS0 + r() % kObjects);
            const uint32_t indexCount = 1 + r() % 4;
            for (uint32_t k = 0; k < indexCount; ++k) node->indexes.push_back(r() % 6);
            if (previous) previous->next = node.get();
            else first = node.get();
            previous = node.get();
            arena.nodes.push_back(std::move(node));
        }
        arena.ranges.push_back({first, nullptr});
    }

    // The test sim and the list.
    cEdithObjectTestSim *sim = arena.Sim();
    std::memset(arena.simBytes, 0xa5, sizeof(arena.simBytes));
    sim->Construct();
    sim->mVTable = GameAddress<void *>(cEdithObjectTestSim::kVTable);
    sim->mPerson = &self->mPersonVTable;
    sim->mObjectKey = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Fk(w, kS0 + r() % kObjects)));
    sim->mFlagA = static_cast<uint8_t>(r() % 2);
    sim->mFlagB = static_cast<uint8_t>(r() % 4 == 0);
    const uint32_t indexCount = r() % 5;
    for (uint32_t i = 0; i < indexCount; ++i) arena.entryIndexes.push_back(r() % 6);
    sim->mEntryIndexes = &arena.entryIndexes;
    arena.list.mFirst = arena.list.mLast = arena.list.mEnd = nullptr;
    const uint32_t listSize = r() % 4;
    const uint32_t listCapacity = listSize + (chance(40) ? 0 : r() % 3);
    if (listCapacity) {
        arena.list.mFirst = static_cast<void **>(RZMemory::Malloc(listCapacity * 4));
        for (uint32_t i = 0; i < listSize; ++i) arena.list.mFirst[i] = &Interaction(w, r());
        arena.list.mLast = arena.list.mFirst + listSize;
        arena.list.mEnd = arena.list.mFirst + listCapacity;
    }
    // The interactions the world already holds in the person's pending list: the same kind of storage.
    self->mPending.mFirst = self->mPending.mLast = self->mPending.mEnd = nullptr;
    const uint32_t pendingSize = r() % 3;
    if (pendingSize) {
        const uint32_t capacity = pendingSize + (chance(50) ? 0 : 1);
        self->mPending.mFirst = static_cast<void **>(RZMemory::Malloc(capacity * 4));
        for (uint32_t i = 0; i < pendingSize; ++i) self->mPending.mFirst[i] = &Interaction(w, r());
        self->mPending.mLast = self->mPending.mFirst + pendingSize;
        self->mPending.mEnd = self->mPending.mFirst + capacity;
    }

    self->mResult830 = r();
    self->mFlag81c = static_cast<uint8_t>(chance(40));
    self->mFlag81d = static_cast<uint8_t>(chance(50));

    auto world = gBeforeRun;
    gBeforeRun = [=](WorldP &world2) {
        world(world2);
        GatherArena &a = ArenaOf(world2);
        a.created = 0;
        *GameAddress<void *>(0x10c9390) = gDirectorVt;
        *GameAddress<int32_t>(0x10c93f4) = INT32_MIN;
        *GameAddress<void *>(0x10c95d4) = ServiceObjectAddress();
        *GameAddress<int32_t>(0x10c95d8) = INT32_MIN;
        *GameAddress<void *>(kGlobalMenuSim) = nullptr;
        *GameAddress<void *>(kGlobalMenuList) = nullptr;
        Fk(world2, kTreeSim).vt[15] = reinterpret_cast<void *>(&CallTreeAndList);
        gTreeList = &a.list;
    };
}

// ---- describing and tearing down -------------------------------------------------------------------------------------------

void DescribeList(const ServiceRefVector &list, std::vector<uint32_t> &out)
{
    out.push_back(0x115e0000u | (list.mFirst != nullptr));
    out.push_back(static_cast<uint32_t>(list.mEnd - list.mFirst));
    out.push_back(static_cast<uint32_t>(list.mLast - list.mFirst));
    for (void *const *p = list.mFirst; p != list.mLast; ++p) out.push_back(*p ? Off(*p) : 0u);
}

void FreeList(ServiceRefVector &list)
{
    if (list.mFirst) RZMemory::Free(list.mFirst);
    list.mFirst = list.mLast = list.mEnd = nullptr;
}

// Describes what the gather left (the lists, the sim, the globals) into the trace and tears the arena and the check world down.
void FinishGatherWorld(WorldP &w, bool describeSim)
{
    GatherArena &arena = ArenaOf(w);
    PrimPersonObject *self = Person(w);
    std::vector<uint32_t> d;
    d.push_back(0x6a7e0000u);
    DescribeList(arena.list, d);
    DescribeList(self->mPending, d);
    if (describeSim) {
        const cEdithObjectTestSim *sim = arena.Sim();
        d.push_back(Norm(reinterpret_cast<uint32_t>(sim->mPerson)));
        d.push_back(Norm(sim->mObjectKey));
        d.push_back(sim->mFlagA | (static_cast<uint32_t>(sim->mFlagB) << 8));
    }
    const void *menuSim = *GameAddress<void *>(kGlobalMenuSim), *menuList = *GameAddress<void *>(kGlobalMenuList);
    d.push_back(menuSim == arena.Sim() ? 1u : (menuSim ? 2u : 0u));
    d.push_back(menuList == &arena.list ? 1u : (menuList == &self->mPending ? 3u : (menuList ? 2u : 0u)));
    d.push_back(Norm(*GameAddress<uint32_t>(0x10da968)));
    gTrace.insert(gTrace.end(), d.begin(), d.end());
    // (The score world's state: the recent-action vector and the tree flag cache, both empty here unless a scoring step ran.)
    std::vector<uint32_t> score;
    DescribeVector(self->mRecentActions, score);
    auto *flagCache = GameAddress<std::map<uint64_t, uint8_t>>(0x10dac2c);
    score.push_back(static_cast<uint32_t>(flagCache->size()));
    gTrace.insert(gTrace.end(), score.begin(), score.end());
    flagCache->~map();
    std::memset(flagCache, 0, sizeof(*flagCache));
    Retire(self->mRecentActions);
    FreeList(arena.list);
    FreeList(self->mPending);
    delete[] arena.registryBytes;
    arena.registryBytes = nullptr;
    self->mObjectRegistry = nullptr;
    gTreeList = nullptr;
    FinishCheckWorld(w);
}

void ReleaseArena(const WorldP &w)
{
    gArenas.erase(&w);
}

} // namespace
