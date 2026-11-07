// Differential test for cTSNeighborhood::LoadLots (0x476390), lane CQ. Fixture: tests/ts_neighborhood_lots_d.h.
// Fakes: 1.. none; 3 persist system, 7 common implementation service (the original callee 0xc0b232 asks it), 11 neighbourhood info,
// 12 its persist resource, 14..17 the lot records the persist system hands out, 18..21 their base infos.
#include "ts_neighborhood_lots_d.h"

#include "simantics/ServiceRefVector.h"

#include <vector>

namespace {
using namespace svcfx;
using namespace lotsd;

constexpr unsigned kLotFirst = 14, kLotCount = 4, kBaseFirst = 18;
cRZString gBaseNames[kLotCount];
cRZString gFileNames[kFakes];
cRZString gPrefix2;
std::vector<std::string> gExisting;   // the file names the persist system "has"

uint32_t LotNumber2(Obj *o, uint32_t *) { return o->value[0]; }
uint32_t LotFormat(Obj *, uint32_t *a)   // FormatLotFileName(out, lot, info)
{
    reinterpret_cast<cRZString *>(a[0])->mString = "lot" + std::to_string(reinterpret_cast<Obj *>(a[1])->value[0]) + ".package";
    return 1;
}
uint32_t LotSetFile(Obj *self, uint32_t *a)
{
    Ev(Hash(reinterpret_cast<const cRZString *>(a[0])->Data()));
    gFileNames[self->id - 200].mString = reinterpret_cast<const cRZString *>(a[0])->mString;
    return 0;
}
uint32_t LotGetFile(Obj *self, uint32_t *) { return P(&gFileNames[self->id - 200]); }
uint32_t CommonText(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(a[1])->mString = "Docs/";
    return 1;
}
uint32_t ListKeys2(Obj *self, uint32_t *a)   // (vector, group, type)
{
    Ev(a[1]);
    Ev(a[2]);
    const uint32_t count = self->value[2];
    if (count) {
        auto *block = static_cast<uint32_t *>(RZMemory::Malloc(count * 16));
        for (uint32_t i = 0; i < count; ++i) {
            block[i * 4] = a[2];
            block[i * 4 + 1] = a[1];
            block[i * 4 + 2] = i;   // which lot record
            block[i * 4 + 3] = 0;
        }
        auto *vec = reinterpret_cast<ServiceBlockVector *>(a[0]);
        vec->mFirst = block;
        vec->mLast = block + count * 4;
        vec->mEnd = block + count * 4;
    }
    return count;
}
uint32_t GetRes(Obj *self, uint32_t *a)   // (key, type, out, 0, 0)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(a[0]);
    LogKey(key);
    const uint32_t i = key[2];
    if (!((self->value[0] >> i) & 1)) return 0;
    Obj &lot = F(kLotFirst + i);
    ++lot.refs;
    *reinterpret_cast<void **>(a[2]) = &lot;
    return 1;
}
uint32_t HasSegment(Obj *, uint32_t *a)   // (directory, file, mode)
{
    const std::string file = reinterpret_cast<const cRZString *>(a[1])->mString;
    Ev(Hash(reinterpret_cast<const cRZString *>(a[0])->Data()));
    Ev(Hash(file.c_str()));
    for (const std::string &name : gExisting)
        if (name == file) return 1;
    return 0;
}
uint32_t ResourceGroup(Obj *, uint32_t *) { return gGroup; }
} // namespace

DIFF_TEST(NeighborhoodLotsD_LoadLots, 0x476390)
{
    AllocatorScope allocator;
    using Fn = uint8_t(__thiscall *)(Nb *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        if (Pair(ctx, "LoadLots", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                gPrefix2 = MakeString("N001");
                const char *baseNames[] = {"Maple", "Oak", "maple", "Birch"};
                for (unsigned i = 0; i < kLotCount; ++i) gBaseNames[i] = MakeString(baseNames[(i + seed / 7) % 4]);
                F(kNbFake).Set(70, "ppp", 0, LotFormat);
                nb->mOwner = &F(11);
                F(11).Set(24, 0, P(&F(12)));
                F(11).Set(8, 0, P(&gPrefix2));
                F(12).Set(6, 0, 0, ResourceGroup);
                F(7).Set(4, "dpd-d", 0, CommonText);
                g.Set(0x10cc040, &F(7));
                g.Set(kPersistCell, &F(3));
                F(3).value[0] = rng() & 0xf;      // which records can be read
                F(3).value[2] = rng() % (kLotCount + 1);
                if (seed % 17 == 0) F(3).value[2] = 0;
                F(3).Set(36, "pdd", 0, ListKeys2);
                F(3).Set(5, "pdp--", 0, GetRes);
                F(3).Set(18, "pp-", 0, HasSegment);
                const uint32_t numbers[] = {3, 0, 4, 4, 9, 2, 0xffffffffu, 7};
                gExisting.clear();
                for (unsigned i = 0; i < kLotCount; ++i) {
                    Obj &lot = F(kLotFirst + i);
                    lot.value[0] = numbers[(i + seed) % 8];
                    lot.Set(18, 0, 0, LotNumber2);
                    lot.Set(30, "dd");
                    lot.Set(4, 0, P(&F(kBaseFirst + i)));
                    lot.Set(6, "p", 0, LotSetFile);
                    lot.Set(8, 0, 0, LotGetFile);
                    F(kBaseFirst + i).Set(19, 0, P(&gBaseNames[i]));
                    F(kBaseFirst + i).Set(23, "d");
                    const uint32_t how = rng() % 4;   // which file name exists
                    if (how == 0 || how == 2) gExisting.push_back("lot" + std::to_string(lot.value[0]) + ".package");
                    if (how == 1 || how == 2) gExisting.push_back(std::string("N001_") + baseNames[(i + seed / 7) % 4]);
                }
                for (unsigned i = 0; i < kFakes; ++i) gFileNames[i] = MakeString("");
                if (seed % 5 == 0) nb->mByteC8 = 1;
                if (rng() % 3 == 0) nb->mLots[int32_t(numbers[seed % 8])].Assign(&F(1));
                if (rng() % 4 == 0) {
                    cRZString taken = MakeString("Oak");
                    nb->mTexts.insert(NeighborhoodTextEntry(*taken.AsInterface(), &F(2)));
                }
                nb->mField6C = (rng() % 3 == 0) ? 0xfffffff0u : 1 + rng() % 6;
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 0, 0x476390, NB_ADAPTER(LoadLots)));
            }))
            ++ok;
    }
    std::printf("  LoadLots: %u worlds ok\n", ok);
}
