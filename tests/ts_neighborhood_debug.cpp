// Differential tests for cTSNeighborhood's last game-code slots (lane EG): GetSyncTableCount (slot 31, 0x475330, a jmp to
// 0x4b8fd0), RunLotResetCommands (slot 17, 0x479cd0). Fixture: tests/ts_services_fixture.h.
#include "ts_services_fixture.h"

#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "rz/cRZString.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <cstring>
#include <random>
#include <string>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{3, "d"}, {4, "ddd"}, {5, "dd"}, {8, "d"}, {9, "d"}, {10, "d"}, {11, "d"}, {12, "d"}, {13, "d"}, {14, "dddd"}, {15, "dddd"}, {16, "dddd"}, {17, "dddd"}, {18, "dddd"}, {19, "dddd"}, {20, "d"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

uint32_t Hash(const char *text, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ uint8_t(text[i])) * 16777619u;
    return h;
}

// c's slot 54: the command text by content (it lives on the caller's stack).
uint32_t LogCommandText(Obj *, uint32_t *a)
{
    const auto *text = reinterpret_cast<const cRZString *>(uintptr_t(a[0]));
    Ev(P(text->mVTable));
    Ev(Hash(text->mString.data(), text->mString.size()));
    Ev(uint32_t(text->mString.size()));
    Ev(uint32_t(text->mString.capacity()));
    Ev(text->mRefCount);
    return 0x5a5a;
}
} // namespace

DIFF_TEST(NeighborhoodDebug_GetSyncTableCount, 0x475330)
{
    const nbshape::SyncTableGlobal syncTable(ctx);
    using Fn = uint32_t(__thiscall *)(Nb *);
    alignas(8) uint8_t bytes[sizeof(Nb)] = {};
    Nb *nb = reinterpret_cast<Nb *>(bytes);
    // The original's table is the game's vector (built on first use by 0x4b8ff0); ours is a constant table of the same entries.
    const uint32_t original = ctx.Original<Fn>(0x475330)(nb);
    const uint32_t ours = reinterpret_cast<Fn>(nbshape::Ours()[0][31])(nb); // our vtable's slot 31 (no hook: a 5-byte jmp)
    ++ctx.cases;
    if (original != ours || original != ctx.Original<uint32_t(__cdecl *)()>(0x4b8fd0)())
        ctx.Fail("count: original %u ours %u", original, ours);
}

DIFF_TEST(NeighborhoodDebug_RunLotResetCommands, 0x479cd0)
{
    using Fn = void(__thiscall *)(Nb *, void *, void *, void *);
    unsigned ok = 0;
    const char *texts[] = {"reset lot", "", "a command text longer than fifteen characters", "0123456789abcde"};
    for (uint32_t seed = 0; seed < 64; ++seed) {
        for (int route = 0; route < 2; ++route) {
            nbshape::gRoute = nbshape::Route(route);
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                ResetAll();
                std::mt19937 rng(seed * 7919u + 3);
                Globals g;
                const char *text = texts[seed % 4];
                g.Set(0x1011d18, text);
                g.SetWord(0x10daa30, rng());
                alignas(8) uint8_t bytes[sizeof(Nb)] = {};
                Nb *nb = reinterpret_cast<Nb *>(bytes);
                for (auto &b : bytes) b = uint8_t(rng());
                const Blob before(reinterpret_cast<uint32_t *>(bytes), reinterpret_cast<uint32_t *>(bytes + sizeof(bytes)));
                F(0).Set(72, 1, rng());
                F(1).Set(41, "d-dddd", rng());
                F(1).Set(42, 0, rng());
                F(1).Set(25, "d", rng());
                F(2).Set(54, "p----", 0, LogCommandText);
                // `this` unused (the bytes are random and must stay so): ours through our vtable's slot 17, or the adapter.
                if (side == 0) ctx.Original<Fn>(0x479cd0)(nb, &F(0), &F(1), &F(2));
                else if (nbshape::gRoute == nbshape::kAdapterRoute) cTSNeighborhoodAdapters::RunLotResetCommands(nb, nullptr, &F(0), &F(1), &F(2));
                else reinterpret_cast<Fn>(nbshape::Ours()[0][17])(nb, &F(0), &F(1), &F(2));
                Blob &b = blobs[side];
                b.insert(b.end(), gEv.begin(), gEv.end());
                CaptureFakes(b);
                // `this` is not touched.
                b.push_back(Blob(reinterpret_cast<uint32_t *>(bytes), reinterpret_cast<uint32_t *>(bytes + sizeof(bytes))) == before);
            }
            if (Same(ctx, blobs[0], blobs[1], "RunLotResetCommands", seed)) ++ok;
        }
        nbshape::gRoute = nbshape::kVTableRoute;
    }
    std::printf("  RunLotResetCommands: %u cases ok\n", ok);
}

// ================= DebugDrawFamilyArches (slot 66, 0x479de0) =================
//
// Fakes: 0 the SG system (cache 0x10cc458; slot 60 the debug draw, slot 40 the render id registry = none, so the original
// HighlightLot 0x472e10 both sides call stops after the lot's bounds), 1 the debug draw, 2 the app singleton, 3 the terrain,
// 4 the family manager, 5 the neighbour manager, 6 the family drawn, 8..13 families, 14..19 lots. The neighbourhood is built by
// Construct and carries a copy of the game's vtable with slots 57 (GetLot) and 67 (UnregisterRenderIds) recording.
namespace {
constexpr unsigned kFamiliesFirst = 8, kFamilyCount = 6, kLotsFirst = 14, kLotCount = 6;
using FamilyIds = std::vector<int16_t, GZServiceAllocator<int16_t>>;
using NeighborIds = std::vector<uint32_t, GZServiceAllocator<uint32_t>>;
void *gArchVT[77];
std::mt19937 gArchRng;

void *__fastcall ArchGetLot(Nb *, void *, int32_t number)
{
    Ev(0x5701);
    Ev(uint32_t(number));
    // lot numbers 1..kLotCount are known, the others are not
    return (number >= 1 && number <= int32_t(kLotCount)) ? &F(kLotsFirst + number - 1) : nullptr;
}
void __fastcall ArchUnregister(Nb *, void *) { Ev(0x6701); }
float __fastcall ArchHeight(Obj *self, void *, int32_t x, int32_t y)
{
    Ev(0x1001);
    Ev(uint32_t(x));
    Ev(uint32_t(y));
    return float(x) * 0.37f - float(y) * 1.25f + float(self->value[0]) * 0.001f;
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
void __fastcall ArchDraw(Obj *, void *, const float *from, float x, float y, float z, const char *material, float width,
                         uint32_t segments, uint32_t arg7, float angle)
{
    Ev(0x0701);
    for (int i = 0; i < 3; ++i) Ev(Bits(from[i]));
    Ev(Bits(x));
    Ev(Bits(y));
    Ev(Bits(z));
    Ev(P(material));
    Ev(Bits(width));
    Ev(segments);
    Ev(arg7);
    Ev(Bits(angle));
}
uint32_t LotBounds(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<int32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) out[i] = int32_t(self->value[i]);
    return 0;
}
uint32_t FillFamilyIds(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<FamilyIds *>(uintptr_t(a[0]));
    for (uint32_t i = 0; i < self->value[0]; ++i) out->push_back(int16_t(self->value[1] + i * 0x3001)); // some negative
    if (out->data()) gAllocated.push_back(out->data());
    return 0;
}
uint32_t FamilyById(Obj *self, uint32_t *a)
{
    const uint32_t index = (a[0] & 0xffff) % 8; // 6 / 7: no family
    return (self->value[2] >> index) & 1 && index < kFamilyCount ? P(&F(kFamiliesFirst + index)) : 0;
}
// The render id registry's slot 11 (&bounds, 0, text, lot): the highlight text by content; answers a new render id.
uint32_t RegisterHighlight(Obj *self, uint32_t *a)
{
    const auto *text = reinterpret_cast<const cRZString *>(uintptr_t(a[2]));
    Ev(0x1101);
    Ev(Hash(text->mString.data(), text->mString.size()));
    return 0x7000 + self->value[0]++;
}
uint32_t FamilyHasMember(Obj *self, uint32_t *a) { return 0xab00 | ((self->value[0] >> (a[0] % 16)) & 1); }
uint32_t FillRelatives(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<NeighborIds *>(uintptr_t(a[1]));
    for (uint32_t i = 0; i < self->value[0]; ++i) out->push_back(self->value[1] + i * 3);
    if (out->data()) gAllocated.push_back(out->data());
    return 0;
}
} // namespace

DIFF_TEST(NeighborhoodDebug_DebugDrawFamilyArches, 0x479de0)
{
    using Fn = void(__thiscall *)(Nb *, void *);
    AllocatorScope allocator;
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        for (int route = 0; route < 2; ++route) {
            nbshape::gRoute = nbshape::Route(route);
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                ResetAll();
                std::mt19937 rng(seed * 7919u + 11);
                Globals g;
                alignas(8) uint8_t bytes[sizeof(Nb)] = {};
                Nb *nb = reinterpret_cast<Nb *>(bytes);
                nbshape::Build(ctx, side == 0, bytes);
                const void *const *gameVT = nbshape::VTableOf(nb, 0); // the side's own
                for (unsigned i = 0; i < 77; ++i) gArchVT[i] = const_cast<void *>(gameVT[i]);
                gArchVT[57] = reinterpret_cast<void *>(&ArchGetLot);
                gArchVT[67] = reinterpret_cast<void *>(&ArchUnregister);
                void **savedVT = nbshape::VTableOf(nb, 0);
                nbshape::VTableOf(nb, 0) = gArchVT;

                const uint32_t shape = rng();
                g.Set(0x10cc458, &F(0));
                F(0).Set(60, 0, (shape % 16 == 0) ? nullptr : static_cast<void *>(&F(1)));
                F(0).Set(40, 0, (shape & 0x10) ? static_cast<void *>(&F(20)) : nullptr); // the render id registry
                F(20).Set(11, "p-pd", 0, RegisterHighlight);
                F(20).Set(12, 1);
                F(1).Set(6, 0);
                F(1).Set(8, 0);
                F(1).table[7] = reinterpret_cast<void *>(&ArchDraw);
                g.Set(kAppCell, (shape % 16 == 1) ? nullptr : static_cast<void *>(&F(2)));
                F(2).Set(11, 0, (shape % 16 == 2) ? nullptr : static_cast<void *>(&F(3)));
                F(2).Set(9, 0, (shape % 16 == 3) ? nullptr : static_cast<void *>(&F(4)));
                F(2).Set(10, 0, (shape % 16 == 4) ? nullptr : static_cast<void *>(&F(5)));
                F(3).value[0] = rng();
                F(3).table[10] = reinterpret_cast<void *>(&ArchHeight);
                F(4).value[0] = rng() % 6;                // family ids
                F(4).value[1] = rng() & 0xffff;
                F(4).value[2] = rng();                    // which ids have a family
                F(4).Set(19, "p", 0, FillFamilyIds);
                F(4).Set(11, "d", 0, FamilyById);
                F(5).value[0] = rng() % 7;                // relatives
                F(5).value[1] = rng();
                F(5).Set(27, "ppd", 0, FillRelatives);
                F(6).Set(42, 0, (shape % 16 == 5) ? 9 : 1 + rng() % kLotCount); // the family's home lot (9: unknown)
                for (unsigned i = 0; i < kFamilyCount; ++i) {
                    Obj &family = F(kFamiliesFirst + i);
                    family.value[0] = rng();
                    family.Set(29, "d", 0, FamilyHasMember);
                    family.Set(42, 0, (rng() % 5 == 0) ? 7 + rng() % 3 : 1 + rng() % kLotCount);
                }
                for (unsigned i = 0; i < kLotCount; ++i) {
                    Obj &lot = F(kLotsFirst + i);
                    const int32_t left = int32_t(rng() % 200) - 20, top = int32_t(rng() % 200) - 20;
                    lot.value[0] = uint32_t(left);
                    lot.value[1] = uint32_t(top);
                    lot.value[2] = uint32_t(left + int32_t(rng() % 40));
                    lot.value[3] = uint32_t(top + int32_t(rng() % 40));
                    lot.Set(12, "p", 0, LotBounds);
                }
                void *family = (shape % 16 == 6) ? nullptr : &F(6);
                nbshape::Call<Fn>(ctx, side == 0, nb, 0, 0x479de0, NB_ADAPTER(DebugDrawFamilyArches), family);
                Blob &b = blobs[side];
                b.insert(b.end(), gEv.begin(), gEv.end());
                CaptureFakes(b, kValueKinds);
                CaptureAllocated(b);
                b.push_back(uint32_t(nb->mPlain.size()));
                for (const auto &[lot, id] : nb->mPlain) b.push_back(lot), b.push_back(id);
                nbshape::VTableOf(nb, 0) = savedVT;
                nbshape::Destroy(ctx, side == 0, nb);
            }
            if (Same(ctx, blobs[0], blobs[1], "DebugDrawFamilyArches", seed)) ++ok;
        }
        nbshape::gRoute = nbshape::kVTableRoute;
    }
    std::printf("  DebugDrawFamilyArches: %u worlds ok\n", ok);
}
