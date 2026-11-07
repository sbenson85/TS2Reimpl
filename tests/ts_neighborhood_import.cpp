// Differential test for cTSNeighborhood's ImportFromContainer (the cITSDataContainer face's slot 5, 0x477d70), lane EG.
// Fixture: tests/ts_services_fixture.h.
//
// Fakes: 0 the container, 1 the import source, 2 the container's object 0x656b8f0c, 3 the id map, 4 the persist system (cell
// 0x10faf6c), 5 the source neighbourhood, 6 the resource manager, 7..8 the source lot inventories, 9 the framework,
// 10..13 business records, 14..17 this neighbourhood's inventories, 18..19 lots. The neighbourhood is built by Construct and
// carries a copy of the game's vtable with slots 37 (GetLotInventory) and 57 (GetLot) recording; the persist face's GetGroup is
// the game's folded getter (the key's group word, +0x94). Compared: the event log (keys by content), the fakes' counts and
// words, the records' entry stores, which service blocks were freed, and the lot inventory map (+0x78) by key and fake.
#include "ts_services_fixture.h"

#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <cstring>
#include <random>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "-ddd"}, {3, "d"}, {4, "dd"}, {7, "d"}, {8, "d"}, {10, "dd"}, {11, "dd"}, {12, "dd"}, {13, "dd"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

constexpr unsigned kSourceInv = 7, kFramework = 9, kRecords = 10, kRecordCount = 4, kInventories = 14, kLotInfos = 18;
using Lots = std::vector<uint32_t, GZServiceAllocator<uint32_t>>;
using Ids = std::vector<uint16_t, GZServiceAllocator<uint16_t>>;
struct Key {
    uint32_t type, group, instance, zero;
};
using Keys = std::vector<Key, GZServiceAllocator<Key>>;

void *gNbVT[77];
uint32_t gTokens[2][8];
uint32_t gEntries[kRecordCount][8][0x18];  // what each record's slot 25 / 31 hands out (by id % 8)
uint32_t gStore[kRecordCount][8][0x18];    // where slot 27 / 33 put entries (by id % 8)
unsigned gCreated;

void LogKey(uint32_t pointer)
{
    const auto *key = reinterpret_cast<const Key *>(uintptr_t(pointer));
    Ev(0x4b01);
    Ev(key->type);
    Ev(key->group);
    Ev(key->instance);
    Ev(key->zero);
}

void *__fastcall NbLotInventory(Nb *, void *, int32_t lot)
{
    Ev(0x3701);
    Ev(uint32_t(lot));
    return (lot & 3) == 0 ? nullptr : &F(kInventories + (lot & 3));
}
void *__fastcall NbGetLot(Nb *, void *, int32_t lot)
{
    Ev(0x5701);
    Ev(uint32_t(lot));
    return (lot % 3 == 0) ? nullptr : &F(kLotInfos + (lot & 1));
}

uint32_t HandOut(Obj *self, uint32_t *out, unsigned target)
{
    const bool ok = (self->value[0] >> (target & 31)) & 1;
    if (ok) {
        *reinterpret_cast<void **>(uintptr_t(out[0])) = &F(target);
        ++F(target).refs;
    }
    return ok ? 0x101 : 0x100;
}
uint32_t ContainerQI(Obj *self, uint32_t *a) { Ev(a[0]); return HandOut(self, &a[1], 1); }
uint32_t ContainerEntry(Obj *self, uint32_t *a) { Ev(a[0]); return HandOut(self, &a[1], 2); }
uint32_t SourceLots(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<Lots *>(uintptr_t(a[0]));
    for (uint32_t i = 0; i < self->value[1]; ++i) out->push_back(self->value[2] + i * 5);
    if (out->data()) gAllocated.push_back(out->data());
    return 0;
}
uint32_t SourceRenumber(Obj *self, uint32_t *a)
{
    if (((self->value[3] >> (a[0] % 32)) & 1) == 0) return 0x200;
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = int32_t(a[0] * 3 + 1);
    return 1;
}
uint32_t IdMap(Obj *self, uint32_t *a)
{
    if ((self->value[0] >> (a[0] % 32)) & 1) {
        *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = (a[0] % 29) * 7 + 0x40;
        return 1;
    }
    return 0x300;
}
uint32_t PersistGet(Obj *self, uint32_t *a)
{
    LogKey(a[0]);
    const auto *key = reinterpret_cast<const Key *>(uintptr_t(a[0]));
    if (key->type == Nb::kClassId) return HandOut(self, &a[2], 5);
    return HandOut(self, &a[2], kRecords + key->instance % kRecordCount);
}
uint32_t PersistKeys(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<Keys *>(uintptr_t(a[0]));
    for (uint32_t i = 0; i < self->value[1]; ++i) out->push_back({a[2], a[1], 100 + i, 0});
    if (out->data()) gAllocated.push_back(out->data());
    return 0;
}
uint32_t KeyArg(Obj *, uint32_t *a) { LogKey(a[0]); return 1; }
template <unsigned Slot> uint32_t TokenIter(Obj *self, uint32_t *a)
{
    const unsigned side = self->id & 1;
    const unsigned count = (self->value[0] >> (Slot % 8)) & 7;
    uint32_t *at = &gTokens[side][(Slot & 1) ? count : 0];
    *reinterpret_cast<uint32_t **>(uintptr_t(a[0])) = at;
    return 0;
}
uint32_t Create(Obj *, uint32_t *a)
{
    Obj &made = F(kInventories + (gCreated++ % 4));
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &made;
    ++made.refs;
    return 1;
}
unsigned RecordIndex(Obj *self) { return self->id - 200 - kRecords; }
template <unsigned Shift> uint32_t RecordIds(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<Ids *>(uintptr_t(a[0]));
    const uint32_t count = ((self->value[0] >> Shift) & 31) + ((self->value[0] >> (Shift + 5)) & 1) * 16; // up to 47: rehashes, merged ids
    for (uint32_t i = 0; i < count; ++i) out->push_back(uint16_t(self->value[1] + i * 0x2f13 + Shift));
    if (out->data()) gAllocated.push_back(out->data());
    return 0;
}
uint32_t EntryAt(Obj *self, uint32_t *a) { return P(gEntries[RecordIndex(self)][a[0] % 8]); }
uint32_t StoreAt(Obj *self, uint32_t *a) { return P(gStore[RecordIndex(self)][a[0] % 8]); }
} // namespace

DIFF_TEST(NeighborhoodImport_ImportFromContainer, 0x477d70)
{
    using Fn = uint8_t(__thiscall *)(void *, void *);
    AllocatorScope allocator;
    unsigned ok = 0, imported = 0;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        for (int route = 0; route < 2; ++route) {
            nbshape::gRoute = nbshape::Route(route);
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                ResetAll();
                std::mt19937 rng(seed * 7919u + 5);
                Globals g;
                gFrameworkIndex = kFramework;
                gCreated = 0;
                alignas(8) uint8_t bytes[sizeof(Nb)] = {};
                Nb *nb = reinterpret_cast<Nb *>(bytes);
                nbshape::Build(ctx, side == 0, bytes);
                nb->mKey[1] = rng(); // the group the persist face's GetGroup answers
                const void *const *gameVT = nbshape::VTableOf(nb, 0); // the side's own
                for (unsigned i = 0; i < 77; ++i) gNbVT[i] = const_cast<void *>(gameVT[i]);
                gNbVT[37] = reinterpret_cast<void *>(&NbLotInventory);
                gNbVT[57] = reinterpret_cast<void *>(&NbGetLot);
                void **savedVT = nbshape::VTableOf(nb, 0);
                nbshape::VTableOf(nb, 0) = gNbVT;

                const uint32_t shape = rng();
                F(0).value[0] = (shape % 8 == 0) ? 0 : (shape % 8 == 1) ? 2 : 6; // QI fails / slot 12 fails / both answer
                F(0).Set(0, "dp", 0, ContainerQI);
                F(0).Set(12, "dp", 0, ContainerEntry);
                F(1).Set(31, 0, &F(3));
                F(1).Set(14, 0, rng());
                F(1).value[1] = rng() % 7;      // lots
                F(1).value[2] = rng() % 40;
                F(1).value[3] = rng();          // which lots renumber
                F(1).Set(23, "p", 0, SourceLots);
                F(1).Set(25, "dp", 0, SourceRenumber);
                F(3).value[0] = rng();
                F(3).Set(32, "dp", 0, IdMap);
                g.Set(kPersistCell, &F(4));
                F(4).value[0] = (shape % 8 == 2) ? 0 : (rng() | (1u << 5)); // bit 5: the source neighbourhood; 10..13 the records
                F(4).value[1] = rng() % 4;      // this group's old record keys
                F(4).Set(5, "pdp--", 0, PersistGet);
                F(4).Set(3, 0, &F(6));
                F(4).Set(36, "pdd", 0, PersistKeys);
                F(4).Set(17, "p", 0, KeyArg);
                F(4).Set(16, "p-", 1);
                F(6).Set(10, "p", 0, KeyArg);
                F(5).Set(37, "d", &F(kSourceInv + (rng() & 1)));
                for (unsigned s = 0; s < 2; ++s) {
                    Obj &inv = F(kSourceInv + s);
                    inv.value[0] = rng();
                    inv.Set(28, "p", 0, TokenIter<28>);
                    inv.Set(29, "p", 0, TokenIter<29>);
                    inv.Set(42, "p", 0, TokenIter<42>);
                    inv.Set(43, "p", 0, TokenIter<43>);
                    for (unsigned i = 0; i < 8; ++i) gTokens[s][i] = 0x9000 + s * 0x100 + i;
                }
                F(kFramework).Set(3, "ddp", 0, Create);
                for (unsigned r = 0; r < kRecordCount; ++r) {
                    Obj &record = F(kRecords + r);
                    record.value[0] = rng();
                    record.value[1] = rng();
                    record.Set(23, "p", 0, RecordIds<0>);
                    record.Set(29, "p", 0, RecordIds<10>);
                    record.Set(25, "d", 0, EntryAt);
                    record.Set(31, "d", 0, EntryAt);
                    record.Set(27, "d", 0, StoreAt);
                    record.Set(33, "d", 0, StoreAt);
                    record.Set(28, "d");
                    record.Set(34, "d");
                    record.Set(4, "p", 0, KeyArg);
                    for (auto &entry : gEntries[r])
                        for (auto &w : entry) w = rng();
                    std::memset(gStore[r], 0, sizeof(gStore[r]));
                }
                for (unsigned i = 0; i < 4; ++i) {
                    Obj &inv = F(kInventories + i);
                    inv.Set(6, "dd");
                    inv.Set(45, "d");
                    inv.Set(9, 1);
                    inv.Set(10, 0);
                    inv.Set(15, "d");
                    inv.Set(32, "d");
                }
                for (unsigned i = 0; i < 2; ++i) {
                    F(kLotInfos + i).Set(33, 0, rng() % 64);
                    F(kLotInfos + i).Set(34, "d");
                }
                void *container = (shape % 16 == 3) ? nullptr : &F(0);
                uint8_t result;
                result = nbshape::Call<Fn>(ctx, side == 0, nb, 2, 0x477d70, NB_ADAPTER(Import), container);
                Blob &b = blobs[side];
                b.push_back(result);
                if (side == 0 && result) ++imported;
                b.insert(b.end(), gEv.begin(), gEv.end());
                CaptureFakes(b, kValueKinds);
                CaptureAllocated(b);
                for (auto &record : gStore)
                    for (auto &entry : record)
                        for (uint32_t w : entry) b.push_back(w);
                for (const auto &[lot, inv] : nb->mMembers1) b.push_back(uint32_t(lot)), b.push_back(Norm(P(inv.Get())));
                nbshape::VTableOf(nb, 0) = savedVT;
                nbshape::Destroy(ctx, side == 0, nb);
            }
            if (Same(ctx, blobs[0], blobs[1], "ImportFromContainer", seed)) ++ok;
        }
        nbshape::gRoute = nbshape::kVTableRoute;
    }
    std::printf("  ImportFromContainer: %u worlds ok, %u imported\n", ok, imported);
}
