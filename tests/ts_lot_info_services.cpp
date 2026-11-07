// Differential tests for the service-heavy cTSLotInfo members (src/ts/cTSLotInfo.*): 0x4c9970 (extra info object), 0x4cafe0
// (names into the lot resource), 0x4ca7f0 (lot info for a new lot). Fixture: tests/ts_services_fixture.h.
//
// The lot info is a zeroed 0xac-byte block with the game's five vtables and the base (+0x80) a recording fake; the app
// singleton, persist system, current-lot cache, second-language cache and director (framework) are fakes too. The helpers
// 0x55d500 / 0x55d4d0 (-> 0x54a290) run as original code on both sides against the app fake chain.
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "rz/cRZString.h"
#include "ts/cTSLotInfo.h"
#include "ts_lot_info_shape.h"

#include <random>

// The +8 face's function at `rva`, called the side's and route's way.
#define LOT_CALL(Fn, rva, adapter, ...)                                                                                        \
    lotshape::Call<Fn>(ctx, orig, lot, 1, rva, LOTINFO_ADAPTER_ADDRESS(adapter), __VA_ARGS__)

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "ddd"}, {2, "d"}, {3, "d"}, {4, "d"}, {5, "ddd----d"}, {6, "-ddd---d"}, {11, "d"}, {13, "d"}, {14, "d"}};
using namespace svcfx;

constexpr uint32_t kFace = 8;

struct LotMem {
    alignas(8) uint8_t bytes[sizeof(cTSLotInfo)];
    cTSLotInfo *Lot() { return reinterpret_cast<cTSLotInfo *>(bytes); }
};

// A zeroed block with the five vtables (the game's, or ours: `ours`, as our constructor stores them) and the base fake. Not built
// by either constructor: these members use none of the constructed members but the base (+0x80) and +0x84.
void BuildLot(LotMem &mem, void *base, bool ours)
{
    std::memset(mem.bytes, 0, sizeof(mem.bytes));
    cTSLotInfo *lot = mem.Lot();
    for (unsigned f = 0; f < lotshape::kFaces; ++f)
        *reinterpret_cast<uint32_t *>(mem.bytes + lotshape::kFaceOffset[f]) =
            ours ? lotshape::Ours().face[f] : lotshape::P(GameAddress<void>(lotshape::kGameVTable[f]));
    lot->mBase = base;
    AddRange(lot, sizeof(cTSLotInfo), 0xe0000000);
}

// The lot's words as CaptureLot logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kLotKinds[] = "------dd -------- -------- -------- pp";
void CaptureLot(Blob &b, cTSLotInfo *lot)
{
    const auto *w = reinterpret_cast<const uint32_t *>(lot);
    for (unsigned off : {0x18u, 0x1cu, 0x40u, 0x44u, 0x4cu, 0x80u, 0x84u, 0x94u, 0xa4u, 0xa8u}) svcfx::Field(b, w[off / 4], kLotKinds, off);
}

uint32_t RandomWord(Obj *o, uint32_t *) { return o->value[0] = o->value[0] * 1664525u + 1013904223u; }

// Logs the characters of the cRZString an argument points at.
void LogString(const cRZString *s)
{
    Ev(uint32_t(s->mString.size()));
    for (char c : s->mString) Ev(uint8_t(c));
}

// Logs the four key words an argument points at.
void LogKey(const uint32_t *key)
{
    for (int i = 0; i < 4; ++i) Ev(key[i]);
}

// ---- 0x4cafe0: names into the lot resource ----

enum Names { nBase, nPersist, nFramework, nResource, nCreated, nLanguage, nOld };

// persist slot 5 (key, iid, out, 0, 0): found -> the resource; not found -> maybe a stale object in *out (released by the code)
uint32_t NamesFind(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const uint32_t *>(a[0]));
    auto **out = reinterpret_cast<void **>(a[2]);
    if (o->value[0] == 1) {
        *out = &F(nResource);
        return 1;
    }
    if (o->value[0] == 2) *out = &F(nOld);
    return 0;
}
// framework slot 3 (class, iid, out): hands out the resource
uint32_t NamesCreate(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(a[2]) = &F(nResource);
    return 1;
}

void NamesWorld(std::mt19937 &rng)
{
    F(nBase).Set(19, 0, 0x1000 + (rng() & 0xfff));
    F(nBase).Set(21, 0, 0x2000 + (rng() & 0xfff));
    F(nPersist).value[0] = rng() % 3 == 0 ? 0 : rng() % 4 == 0 ? 2 : 1;
    F(nPersist).Set(5, "pdp--", 0u, &NamesFind);
    F(nFramework).Set(3, "ddp", 1u, &NamesCreate);
    F(nResource).Set(3, 0, &F(nCreated));
    F(nResource).Set(10, "ddd", 0u);
    F(nResource).Set(21, 0, rng() % 2 ? 0u : 1u + rng() % 5);
    F(nCreated).Set(4, "p", 0u);
    F(nLanguage).Set(7, 0, rng() % 2 ? 1u : rng() % 4);
    F(nOld).Set(3, 0, 0u);
}

// Once per route (tests/ts_lot_info_shape.h): our side carries our vtables (vtable route) or the game's with the adapter's slot
// trapped (adapter route).
template <class Fn> bool Pair(difftest::Context &ctx, const char *what, uint32_t seed, Fn world)
{
    lotshape::Ours();
    bool same = true;
    for (int route = 0; route < 2; ++route) {
        lotshape::gRoute = lotshape::Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            std::mt19937 rng(seed * 7919u + 13);
            Globals g;
            LotMem mem;
            BuildLot(mem, &F(0), side == 1 && route == lotshape::kVTableRoute);
            lotshape::gTrapHits = 0;
            Blob &b = blobs[side];
            const uint32_t r = world(g, mem.Lot(), rng, side == 0);
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureLot(b, mem.Lot());
            CaptureFakes(b, kValueKinds);
            CaptureAllocated(b);
            b.push_back(uint32_t(lotshape::gTrapHits));
        }
        same = Same(ctx, blobs[0], blobs[1], what, seed + (route ? 0x80000000u : 0u)) && same;
    }
    return same;
}

} // namespace

DIFF_TEST(LotInfoServices_StoreNamesInResource, 0x4cafe0)
{
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "StoreNamesInResource", seed, [&](Globals &g, cTSLotInfo *lot, std::mt19937 &rng, bool orig) -> uint32_t {
            NamesWorld(rng);
            lot->mBase = &F(nBase);
            g.Set(kPersistCell, &F(nPersist));
            g.Set(kSecondLanguageCache, &F(nLanguage));
            gFrameworkIndex = nFramework;
            const uint32_t instance = rng(), group = rng();
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
            return LOT_CALL(Fn, 0x4cafe0, StoreNamesInResource, instance, group);
        });
}

namespace {
// ---- 0x4c9970: the extra info object ----

enum Extra { eApp, ePersist, eCurrent, eFinder, eFramework, eInfo, eInfoB, eX, eY, eZ, eW, eV, eU, eBase, eName };

// finder slot 3 (key, iid, out)
uint32_t FinderGet(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const uint32_t *>(a[0]));
    auto **out = reinterpret_cast<void **>(a[2]);
    if (o->value[0]) *out = &F(eInfo);
    return 0;
}
uint32_t FrameworkCreate(Obj *o, uint32_t *a)
{
    auto **out = reinterpret_cast<void **>(a[2]);
    if (o->value[0]) *out = &F(eInfoB);
    return 1;
}
uint32_t InfoSlot13(Obj *, uint32_t *a)
{
    LogString(reinterpret_cast<const cRZString *>(a[0]));
    return 0;
}
// slot 17 alternates: the first call answers value[1], later calls value[2]
uint32_t InfoSlot17(Obj *o, uint32_t *)
{
    return o->value[7]++ == 0 ? o->value[1] : o->value[2];
}
uint32_t InfoSlot4(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const uint32_t *>(a[0]));
    return 0;
}
// 0x55d500's chain: app slot 6 -> X slot 15 -> Y slot 29(arg) ; 0x54a290's: app slot 24 -> Z slot 14 -> W slot 18 -> V slot 29
// (a, iid, &out) -> U slot 9 (count), slot 11 (index) -> int16
uint32_t VLookup(Obj *o, uint32_t *a)
{
    if (!o->value[0]) return 0;
    *reinterpret_cast<void **>(a[2]) = &F(eU);
    return 1;
}

void ExtraWorld(std::mt19937 &rng)
{
    F(ePersist).Set(11, "p", 0x3000 + (rng() & 0xfff));
    F(eCurrent).Set(34, "d", rng() % 5 == 0 ? 0u : P(&F(eFinder)));
    F(eFinder).value[0] = rng() % 2;
    F(eFinder).Set(3, "pdp", 0u, &FinderGet);
    F(eFinder).Set(4, "p--d", 0u);
    F(eFramework).value[0] = rng() % 4 != 0;
    F(eFramework).Set(3, "ddp", 1u, &FrameworkCreate);
    for (unsigned i : {eInfo, eInfoB}) {
        Obj &info = F(i);
        info.Set(19, 0, 0x4000 + (rng() & 0xfff) + i);
        info.Set(18, "d-", 0u);
        info.value[1] = rng() % 2;
        info.value[2] = rng() % 3;
        info.Set(17, 0, 0u, &InfoSlot17);
        info.Set(22, "d-", 0u);
        info.Set(4, "p", 0u, &InfoSlot4);
        info.Set(13, "p", 0u, &InfoSlot13);
    }
    F(eApp).Set(6, 0, &F(eX));
    F(eX).Set(15, 0, &F(eY));
    F(eY).Set(29, "d", rng() % 4);
    F(eApp).Set(24, 0, &F(eZ));
    F(eZ).Set(14, "d", &F(eW));
    F(eW).Set(18, 0, &F(eV));
    F(eV).value[0] = rng() % 3 != 0;
    F(eV).Set(29, "ddp", 0u, &VLookup);
    F(eU).Set(9, 0, 3u);
    F(eU).Set(11, "d", rng() % 9);
    F(eApp).Set(23, 0, rng() % 4 == 0 ? 0u : P(&F(eCurrent)));
}
} // namespace

DIFF_TEST(LotInfoServices_GetOrCreateExtraInfo, 0x4c9970)
{
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "GetOrCreateExtraInfo", seed, [&](Globals &g, cTSLotInfo *lot, std::mt19937 &rng, bool orig) -> uint32_t {
            ExtraWorld(rng);
            // the lot info's own slot 8 (the name's address) and 19 (the base's name object) are the game's
            static const char *const kTexts[] = {"", "lot", "a name longer than sixteen chars", "x%d%s"};
            lot->mBase = &F(eBase);
            F(eBase).Set(19, 0, &F(eName));
            F(eName).Set(5, 0, P(kTexts[rng() % 4]));
            g.Set(kAppCell, &F(eApp));
            g.Set(kPersistCell, &F(ePersist));
            g.Set(kSlot23Cache, rng() % 2 ? &F(eCurrent) : nullptr);
            gFrameworkIndex = eFramework;
            if (rng() % 3 == 0) lot->mField84 = &F(eInfo);
            void *out = nullptr;
            const uint8_t create = uint8_t(rng() % 3 ? 1 : 0);
            using Fn = uint8_t(__thiscall *)(void *, void **, uint32_t);
            const uint32_t createWord = create | (rng() & 0xffffff00u); // only the low byte is the argument
            const uint32_t r = LOT_CALL(Fn, 0x4c9970, GetOrCreateExtraInfo, &out, createWord);
            Ev(Norm(P(out)));
            return r & 0xff;
        });
}

namespace {
// ---- 0x4ca7f0: a new lot info for a lot ----

enum New {
    nwApp, nwPersist, nwLots, nwCurrent, nwNew, nwFound, nwRecord, nwList, nwHolder, nwSettings, nwTarget, nwExtra, nwBase,
    nwCommon, nwFramework, nwOtherA, nwOtherB, nwLoaded
};

const char *const kNewTexts[] = {"", "Lot 1", "A rather long lot description over 15", "x"};

void LogCText(const cRZString *s)
{
    Ev(uint32_t(s->mString.size()));
    for (char c : s->mString) Ev(uint8_t(c));
}
uint32_t FrameworkMake(Obj *o, uint32_t *a)
{
    auto **out = reinterpret_cast<void **>(a[2]);
    switch (a[0]) {
    case 0xbf999e7: *out = &F(nwNew); return 1;
    case 0xedbf32a7:
        if (o->value[0] & 1) return 0;
        *out = &F(nwList);
        return 1;
    case 0xb54e37d: *out = &F(nwHolder); return 1;
    case 0x6c589723: *out = &F(nwSettings); return 1;
    case 0xcdcbe0e3:
        if (o->value[0] & 2) *out = &F(nwExtra);
        return 1;
    }
    return 0;
}
uint32_t PersistName(Obj *o, uint32_t *a)
{
    if ((Norm(a[0]) & 0xf0000000u) == 0xe0000000u) return 0x1000 + o->value[1];
    const auto *text = reinterpret_cast<const cRZString *>(a[0]);
    return 0x2000 + uint32_t(text->mString.size());
}
uint32_t PersistListing(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(a[1]) = &F(nwFound);
    LogCText(reinterpret_cast<const cRZString *>(a[3]));
    LogCText(reinterpret_cast<const cRZString *>(a[4]));
    return 0;
}
void LogVector(const uint32_t *vec)
{
    const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(vec[0]));
    const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(vec[1]));
    Ev(uint32_t(last - first));
    for (const uint32_t *p = first; p != last; ++p) Ev(Norm(*p));
}
uint32_t PersistLogVector(Obj *, uint32_t *a)
{
    LogVector(reinterpret_cast<const uint32_t *>(a[0]));
    return 0;
}
// persist slot 48 (key, &others): fills the vector with counted fakes
uint32_t PersistOthers(Obj *o, uint32_t *a)
{
    const unsigned n = o->value[2] % 4;
    auto *vec = reinterpret_cast<void **>(a[1]);
    if (!n) return 0;
    auto **block = static_cast<void **>(RZMemory::Malloc(n * 4));
    gAllocated.push_back(block);
    for (unsigned i = 0; i < n; ++i) {
        Obj &other = F(i % 2 ? nwOtherB : nwOtherA);
        block[i] = &other;
        ++other.refs;
    }
    vec[0] = block;
    vec[1] = block + n;
    vec[2] = block + n;
    return 0;
}
uint32_t PersistLoaded(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(a[4]) = &F(nwLoaded);
    return 1;
}
uint32_t LotsHeading(Obj *o, uint32_t *a)
{
    static_cast<cRZString *>(reinterpret_cast<void *>(a[0]))->mString.assign(kNewTexts[o->value[0] % 4]);
    return 0;
}
uint32_t CommonListing(Obj *o, uint32_t *a)
{
    static_cast<cRZString *>(reinterpret_cast<void *>(a[1]))->mString.assign(kNewTexts[o->value[0] % 4]);
    return 1;
}
uint32_t FoundRecord(Obj *o, uint32_t *)
{
    const uint32_t call = o->value[7]++;
    return o->value[call & 1] ? P(&F(nwRecord)) : 0u;
}
uint32_t RecordBlocks(Obj *o, uint32_t *a)
{
    const unsigned n = o->value[2] % 4;
    auto *vec = reinterpret_cast<void **>(a[0]);
    if (!n) return 0;
    const unsigned total = n + o->value[3] % 2;
    auto *block = static_cast<uint8_t *>(RZMemory::Malloc(total * 16));
    gAllocated.push_back(block);
    AddRange(block, total * 16, 0xb1000000);
    std::memset(block, 0xab, total * 16);
    vec[0] = block;
    vec[1] = block + n * 16;
    vec[2] = block + total * 16;
    return 0;
}
uint32_t SettingsNotice(Obj *, uint32_t *a)
{
    LogCText(reinterpret_cast<const cRZString *>(a[0]));
    return 0;
}

// The new lot's SetRotation (slot 21) gets the whole register this lot's GetRotation (slot 22, `mov al, [ecx + 0x40]; ret`)
// returned in: the original pushes eax as it is (0x4cad7d: `mov eax, [eax + 0x58]; call eax; push eax`), so its upper bytes are
// the getter's code address (never inside a range Norm maps), ours zero-extends the byte. The slot takes a byte: only the low
// byte of that argument is compared (its call record: evCall, the object id, slot 21, one argument).
void MaskRotationArgument()
{
    for (size_t i = 0; i + 4 < gEv.size(); ++i)
        if (gEv[i] == evCall && gEv[i + 1] == F(nwNew).id && gEv[i + 2] == 21 && gEv[i + 3] == 1) gEv[i + 4] &= 0xff;
}
} // namespace

DIFF_TEST(LotInfoServices_CreateNewLotInfo, 0x4ca7f0)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 800; ++seed)
        Pair(ctx, "CreateNewLotInfo", seed, [&](Globals &g, cTSLotInfo *lot, std::mt19937 &rng, bool orig) -> uint32_t {
            lot->mOriginX = int32_t(rng() % 300);
            lot->mOriginY = int32_t(rng() % 300);
            lot->mRotation = uint8_t(rng() % 4);
            lot->mBase = &F(nwBase);
            F(nwBase).Set(10, 0, 0x7000 + (rng() & 0xff));
            const bool early = rng() % 8 == 0;
            F(nwApp).Set(14, 0, 0u);
            F(nwApp).Set(6, 0, rng() % 16 == 0 ? 0u : P(&F(nwLots)));
            F(nwApp).Set(23, 0, early && rng() % 2 ? 0u : P(&F(nwCurrent)));
            F(nwPersist).value[1] = rng() & 0xff;
            F(nwPersist).value[2] = rng();
            F(nwPersist).Set(11, "p", 0u, &PersistName);
            F(nwPersist).Set(22, "dpdppdd", 0u, &PersistListing);
            F(nwPersist).Set(49, "p", 0u, &PersistLogVector);
            F(nwPersist).Set(50, "pd", 0u, &PersistLogVector);
            F(nwPersist).Set(52, 0, 0u);
            F(nwPersist).Set(48, "dp", 0u, &PersistOthers);
            F(nwPersist).Set(53, 0, 0u);
            F(nwPersist).Set(38, "ppd", 0u);
            F(nwPersist).Set(69, "pd-pp-", 1u, &PersistLoaded);
            F(nwLots).value[0] = rng();
            F(nwLots).Set(68, 0, 0x100 + (rng() & 0xff));
            F(nwLots).Set(69, "pdd", 0u, &LotsHeading);
            F(nwLots).Set(61, "p", 0u);
            F(nwCurrent).Set(6, 0, 0x200 + (rng() & 0xff));
            F(nwNew).Set(17, "d", 0u);
            F(nwNew).Set(6, "p", 0u);
            F(nwNew).Set(9, "dd", 0u);
            F(nwNew).Set(21, "d", 0u);
            F(nwNew).Set(3, "p", 0u);
            F(nwFound).value[0] = rng() % 5 != 0;
            F(nwFound).value[1] = rng() % 5 != 0;
            F(nwFound).Set(3, 0, 0u, &FoundRecord);
            F(nwRecord).value[2] = rng();
            F(nwRecord).value[3] = rng();
            F(nwRecord).Set(19, "p-", 0u, &RecordBlocks);
            F(nwRecord).Set(26, "p", 0u);
            F(nwList).Set(4, "dd", 0u);
            F(nwList).Set(3, 1, 0x300 + (rng() & 0xff));
            F(nwHolder).Set(6, "p-", 0u);
            F(nwSettings).Set(23, "d", 0u);
            F(nwSettings).Set(17, "d", 0u);
            F(nwSettings).Set(20, "p", 0u, &SettingsNotice);
            F(nwSettings).Set(9, "d", 0u);
            F(nwSettings).Set(22, "d", 0u);
            F(nwSettings).Set(37, 0, &F(nwTarget));
            F(nwTarget).Set(4, "p", 0u);
            F(nwExtra).Set(3, 0, 0u);
            F(nwExtra).Set(10, "dd", 0u);
            F(nwCommon).value[0] = rng();
            F(nwCommon).Set(4, "dpd-d", 0u, &CommonListing);
            F(nwFramework).value[0] = rng();
            F(nwFramework).Set(3, "ddp", 1u, &FrameworkMake);
            g.Set(kAppCell, &F(nwApp));
            g.Set(kPersistCell, &F(nwPersist));
            g.Set(0x10cc040, &F(nwCommon));
            gFrameworkIndex = nwFramework;
            void *out = nullptr;
            const uint32_t flag = rng();
            using Fn = uint8_t(__thiscall *)(void *, void **, uint32_t);
            const uint32_t r = LOT_CALL(Fn, 0x4ca7f0, CreateNewLotInfo, &out, flag);
            MaskRotationArgument();
            Ev(Norm(P(out)));
            return r & 0xff;
        });
}
