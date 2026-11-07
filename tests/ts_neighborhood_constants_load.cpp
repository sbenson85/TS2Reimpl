// cTSNeighborhoodConstants::Load (slot 3, 0x47a630), cTSConstantsClient slot 0 (0x4df3d0) and cTSAutonomyConstantsClient's
// deleting destructor (slot 4, 0x417030): original vs ours. Lane NU.
//
// Load runs on a heap object whose vtable is a test table (slot 0 hands out the segment, slot 1 the table id, with garbage in
// the upper half of eax). The float constants are the real class on each side (the original's on the original side, ours on
// ours); the persist system the table loads and saves through (cache cell 0x10faf6c), the file reference (slot 9), and the
// singleton that receives three of the numbers (cache cell 0x10cc290, slots 48 / 50 / 52) are fakes that log. The fake load
// fills the table through the table's own vtable with the constants' names (and others) and random values. Compared: the log,
// the object's words and the six globals the constants land in (0x10daa30..0x10daa48), and the blocks freed.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/Singleton_20ddfb.h"
#include "ts/cTSAutonomyConstantsClient.h"
#include "ts/cTSNeighborhoodConstants.h"

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Blob = std::vector<uint64_t>;
using Constants = cTSNeighborhoodConstants;

std::vector<int64_t> gLog;
std::mt19937 gRng;
uint32_t Next() { return gRng(); }
template <typename... T> void Log(T... values) { (gLog.push_back(static_cast<int64_t>(values)), ...); }
void ResetLog()
{
    gLog.clear();
    if (gLog.capacity() < 8192) gLog.reserve(8192); // the log must not allocate inside a recorded span
}
uint32_t Answer(bool ok) { return (Next() & 0xffffff00u) | (ok ? 1u + (Next() & 0x7e) : 0u); }

enum : uint32_t { evSegment = 1, evId, evLoad, evSave, evAdd, evSlot48, evSlot50, evSlot52, evFileRef, evSlot24, evSlot6 };

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t n) { ::operator delete(p, n); }
struct Patches {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<void *>(&GameDelete)};
};

constexpr const char *kNames[] = {"money for new family", "money per sim for college households", "minimum grade for college admission",
                                  "ResidentialLotInUniversity_AsRentalPropertyDepositFactor", "BusinessRankValue_RankMultiplier",
                                  "BusinessRankValue_EmployeeWageMultiplier", "new family start hour", "perfect family friend count",
                                  "weight of lot size in overall score", "fill value for layout history", "yard score multiplier",
                                  "upkeep penalty per dirty object", "upkeep penalty per broken object"};

uint32_t RandFloatBits(std::mt19937 &rng)
{
    static const uint32_t kSpecial[] = {0x7fc00000u, 0x7f800001u, 0xffc00001u, 0x00000001u, 0x80000000u, 0x00000000u,
                                        0x7f800000u, 0xff800000u, 0x3f800000u, 0x469c4000u, 0x4f000000u, 0xcf000000u, 0x4eff0000u};
    switch (rng() % 4) {
    case 0: return kSpecial[rng() % 13];
    case 1: return std::bit_cast<uint32_t>(static_cast<float>(int(rng() % 4001) - 2000) / 7.0f);
    default: return rng();
    }
}

// ---- the doubles ----

struct Fake {
    void **vt;
};
void *gObjectVT[8];
void *gPersistVT[80];
void *gFileRefVT[16];
void *gSingletonVT[80];
Fake gPersist, gFileRef, gSingleton;
void *gSegment = nullptr;
uint32_t gIdWord = 0;

void *__fastcall ObjSegment(void *, void *)
{
    Log(evSegment);
    return gSegment;
}
uint32_t __fastcall ObjId(void *, void *)
{
    Log(evId);
    return gIdWord;
}
uint32_t __fastcall FileRefSlot9(Fake *, void *)
{
    const bool ok = Next() % 3 != 0;
    Log(evFileRef, ok);
    return Answer(ok);
}
uint32_t __fastcall PersistRead(Fake *, void *, void *resource, void *segment, uint32_t instance, uint32_t metadata)
{
    using Slot = void(__thiscall *)(void *, const char *, float);
    using Name = const char *(__thiscall *)(void *, int32_t);
    void **vt = *static_cast<void ***>(resource);
    Log(evLoad, segment == gSegment, instance, metadata, reinterpret_cast<Name>(vt[6])(resource, 0) == nullptr);
    if (Next() % 4 == 0) { // a complete table: every constant present, so nothing is added and the table stays clean
        std::mt19937 rng(Next());
        for (unsigned pick = 0; pick < 13; ++pick) {
            const uint32_t bits = RandFloatBits(rng);
            Log(evAdd, pick, bits);
            reinterpret_cast<Slot>(vt[8])(resource, kNames[pick], std::bit_cast<float>(bits));
        }
    }
    const unsigned adds = Next() % 15;
    for (unsigned i = 0; i < adds; ++i) {
        std::mt19937 rng(Next());
        const uint32_t pick = rng() % 16;
        const std::string name = pick < 13 ? std::string(kNames[pick]) : std::string("other ") + char('a' + rng() % 26);
        const uint32_t bits = RandFloatBits(rng);
        Log(evAdd, pick, bits);
        reinterpret_cast<Slot>(vt[8])(resource, name.c_str(), std::bit_cast<float>(bits));
    }
    return Next();
}
uint32_t __fastcall PersistWrite(Fake *, void *, void *resource, void *fileRef, uint32_t instance)
{
    using Name = const char *(__thiscall *)(void *, int32_t);
    void **vt = *static_cast<void ***>(resource);
    Log(evSave, fileRef == &gFileRef, instance, reinterpret_cast<Name>(vt[6])(resource, 12) != nullptr,
        reinterpret_cast<Name>(vt[6])(resource, 0) != nullptr);
    return Next();
}
void __fastcall Slot48(Fake *, void *, float value)
{
    Log(evSlot48, std::bit_cast<uint32_t>(value));
}
void __fastcall Slot50(Fake *, void *, uint32_t value) { Log(evSlot50, value); }
void __fastcall Slot52(Fake *, void *, uint32_t value) { Log(evSlot52, value); }

void Init()
{
    for (auto &s : gObjectVT) s = nullptr;
    gObjectVT[0] = reinterpret_cast<void *>(&ObjSegment);
    gObjectVT[1] = reinterpret_cast<void *>(&ObjId);
    for (auto &s : gPersistVT) s = nullptr;
    gPersistVT[72] = reinterpret_cast<void *>(&PersistWrite);
    gPersistVT[73] = reinterpret_cast<void *>(&PersistRead);
    gPersist.vt = gPersistVT;
    for (auto &s : gFileRefVT) s = nullptr;
    gFileRefVT[9] = reinterpret_cast<void *>(&FileRefSlot9);
    gFileRef.vt = gFileRefVT;
    for (auto &s : gSingletonVT) s = nullptr;
    gSingletonVT[48] = reinterpret_cast<void *>(&Slot48);
    gSingletonVT[50] = reinterpret_cast<void *>(&Slot50);
    gSingletonVT[52] = reinterpret_cast<void *>(&Slot52);
    gSingleton.vt = gSingletonVT;
}

// The cells the code reads its services from, restored afterwards.
struct Cells {
    void **persist = GameAddress<void *>(0x10faf6c);
    void **singleton = GameAddress<void *>(0x10cc290);
    uint32_t *globals = GameAddress<uint32_t>(0x10daa30);
    void *savedPersist = *persist, *savedSingleton = *singleton;
    uint32_t savedGlobals[7];
    Cells()
    {
        Init();
        std::memcpy(savedGlobals, globals, sizeof(savedGlobals));
        *persist = &gPersist;
        *singleton = &gSingleton;
    }
    ~Cells()
    {
        *persist = savedPersist;
        *singleton = savedSingleton;
        std::memcpy(globals, savedGlobals, sizeof(savedGlobals));
    }
};

} // namespace

DIFF_TEST(TSNeighborhoodConstants_Load, 0x47a630)
{
    Patches patches;
    Cells cells;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 17 + 3);
            auto *object = static_cast<uint32_t *>(::operator new(Constants::kObjectSize));
            for (int i = 0; i < 10; ++i) object[i] = uint32_t(rng());
            object[0] = uint32_t(uintptr_t(gObjectVT));
            gSegment = rng() % 5 == 0 ? nullptr : &gFileRef;
            gIdWord = rng();
            for (int i = 0; i < 7; ++i) cells.globals[i] = 0xa5a50000u + i;
            gRng.seed(seed + 41);
            ResetLog();
            difftest::FreeRecorder frees;
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x47a630)(object);
            else reinterpret_cast<Constants *>(object)->Load();
            frees.Stop();
            Blob &b = sides[side];
            b.assign(gLog.begin(), gLog.end());
            b.push_back(0xf7ee);
            b.insert(b.end(), object, object + 10);
            b.insert(b.end(), cells.globals, cells.globals + 7);
            size_t sizes[256];
            const size_t n = frees.SortedSizes(sizes, 256);
            b.push_back(n);
            for (size_t i = 0; i < n; ++i) b.push_back(sizes[i]);
            b.push_back(frees.Overflowed());
            ::operator delete(object, Constants::kObjectSize);
        }
        ++ctx.cases;
        if (sides[0] != sides[1]) {
            size_t i = 0;
            while (i < sides[0].size() && i < sides[1].size() && sides[0][i] == sides[1][i]) ++i;
            ctx.Fail("Load seed %u: differ at word %u (sizes %u/%u): %llx vs %llx", seed, unsigned(i), unsigned(sides[0].size()),
                     unsigned(sides[1].size()), i < sides[0].size() ? sides[0][i] : 0ull, i < sides[1].size() ? sides[1][i] : 0ull);
            break;
        }
    }
}

// ---- cTSConstantsClient slot 0: the cached singleton's slot 24, then that object's slot 6 ----

namespace {
struct Holder {
    void **vt;
    uint32_t id;
};
void *gFirstVT[40], *gSecondVT[8];
Holder gFirst, gSecond;
void *__fastcall FirstSlot24(Holder *self, void *)
{
    Log(evSlot24, self->id);
    return &gSecond;
}
uint32_t __fastcall SecondSlot6(Holder *self, void *)
{
    Log(evSlot6, self->id);
    return Next();
}
} // namespace

DIFF_TEST(TSConstantsClient_GetSegment, 0x4df3d0)
{
    void **cell = GameAddress<void *>(0x10cc288);
    void *saved = *cell;
    for (auto &s : gFirstVT) s = nullptr;
    gFirstVT[24] = reinterpret_cast<void *>(&FirstSlot24);
    for (auto &s : gSecondVT) s = nullptr;
    gSecondVT[6] = reinterpret_cast<void *>(&SecondSlot6);
    gFirst = Holder{gFirstVT, 0x111};
    gSecond = Holder{gSecondVT, 0x222};
    *cell = &gFirst;
    uint32_t dummy[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            ResetLog();
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x4df3d0)(dummy)
                                : reinterpret_cast<cTSConstantsClient *>(dummy)->GetSegment();
            sides[side].assign(gLog.begin(), gLog.end());
            sides[side].push_back(uintptr_t(r));
        }
        ++ctx.cases;
        if (sides[0] != sides[1]) ctx.Fail("GetSegment seed %u differs", seed);
    }
    *cell = saved;
}

// ---- cTSAutonomyConstantsClient's deleting destructor ----

DIFF_TEST(TSAutonomyConstantsClient_DeletingDestructor, 0x417030)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 rng(seed * 11 + 5);
        const uint32_t flags = rng() % 2 ? 1u : rng() % 2 ? 0x100u : 0u;
        uint32_t words[3];
        for (uint32_t &w : words) w = uint32_t(rng());
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            auto *object = static_cast<uint32_t *>(::operator new(cTSAutonomyConstantsClient::kObjectSize));
            std::memcpy(object, words, sizeof(words));
            difftest::FreeRecorder frees;
            void *result = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x417030)(object, flags)
                                     : static_cast<void *>(reinterpret_cast<cTSAutonomyConstantsClient *>(object)->DeletingDestructor(flags));
            frees.Stop();
            Blob &b = sides[side];
            b.push_back(result == object);
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[4];
            const size_t n = frees.SortedSizes(sizes, 4);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Freed(object));
            if ((flags & 1) == 0) {
                b.push_back(object[0] - uint32_t(uintptr_t(GameAddress<void>(cTSConstantsClient::kVTable))));
                b.insert(b.end(), object + 1, object + 3);
                ::operator delete(object, cTSAutonomyConstantsClient::kObjectSize);
            }
        }
        ++ctx.cases;
        if (sides[0] != sides[1]) {
            ctx.Fail("DeletingDestructor seed %u differs", seed);
            break;
        }
    }
}
