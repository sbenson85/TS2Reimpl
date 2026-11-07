// Differential tests for the lot-index and lot-inventory members of cTSNeighborhood written in lane CL: FlagLotTokens (0x476e80),
// RemoveLotReferences (0x4735e0), DoMessage (0x4738d0), FindLotByName (0x474df0), RenameLot (0x478d80), NextFreeLotNumber (0x472ad0),
// GetLotInventory (0x474530) and UnregisterRenderIds (0x475cb0). Fixture: tests/ts_services_fixture.h.
//
// Every test builds a Construct()-made neighbourhood and recording fakes from one seed, runs the original on one copy and ours on the
// other, and compares the return, the event log (every call a fake receives, with its arguments), the fakes' reference counts and
// words, and the neighbourhood's maps. The fake indices are chosen per test (ResetAll clears them between runs).
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "rz/cRZString.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <random>
#include <string>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "d"}, {2, "ddd"}, {3, "d"}, {4, "d"}, {5, "d"}, {6, "d"}, {7, "d"}, {8, "dddddd-d"}, {9, "dddddd-d"}, {10, "dddddd-d"}, {11, "dddddd-d"}, {12, "dddddd-d"}, {20, "dd"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

struct NbMem {
    alignas(8) uint8_t bytes[sizeof(Nb)];
    Nb *Get() { return reinterpret_cast<Nb *>(bytes); }
};

// The neighbourhood's own vtable for tests that call through `this`: the slots set here record into fake `kNbFake`.
constexpr unsigned kNbFake = 23;
void *gNbVT[80];
void *gFaceVT[12];
uint32_t gGroup = 0;
bool gReady = false;
uint32_t __fastcall NbSlot21(void *, void *, uint32_t x)
{
    uint32_t a[1] = {x};
    return detail::Dispatch(&F(kNbFake), 21, a, 1);
}
uint32_t __fastcall NbSlot57(void *, void *, uint32_t x)
{
    uint32_t a[1] = {x};
    return detail::Dispatch(&F(kNbFake), 57, a, 1);
}
uint32_t __fastcall FaceGroup(void *, void *)
{
    Ev(evNote);
    Ev(6);
    return gGroup;
}
void Ready()
{
    if (gReady) return;
    gNbVT[21] = reinterpret_cast<void *>(&NbSlot21);
    gNbVT[57] = reinterpret_cast<void *>(&NbSlot57);
    gFaceVT[6] = reinterpret_cast<void *>(&FaceGroup);
    gReady = true;
}

uint32_t Hash(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}
cRZString MakeString(const std::string &text)
{
    cRZString s{GameAddress<void>(cRZString::kVTable), std::string(text), 0};
    return s;
}

void CaptureNb(Blob &b, Nb *nb)
{
    b.push_back(nb->mField6C);
    b.push_back(nb->mByteC8);
    b.push_back(Norm(P(nb->mDelegate)));
    b.push_back(Norm(P(nb->mSecondOwner)));
    b.push_back(uint32_t(nb->mLots.size()));
    for (auto &entry : nb->mLots) {
        b.push_back(uint32_t(entry.first));
        b.push_back(Norm(P(entry.second.Get())));
    }
    b.push_back(uint32_t(nb->mTexts.size()));
    for (auto &entry : nb->mTexts) {
        b.push_back(Hash(entry.mText.Data()));
        b.push_back(Norm(P(entry.mHeld)));
    }
    for (auto *map : {&nb->mMembers1, &nb->mMembers2, &nb->mMembers3}) {
        b.push_back(uint32_t(map->size()));
        for (auto &entry : *map) {
            b.push_back(uint32_t(int32_t(entry.first)));
            b.push_back(Norm(P(entry.second.Get())));
        }
    }
    b.push_back(uint32_t(nb->mPlain.size()));
}

template <class Fn> bool Pair(difftest::Context &ctx, const char *what, uint32_t seed, Fn world)
{
    bool same = true;
    for (int route = 0; route < 2; ++route) {
        nbshape::gRoute = nbshape::Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            Ready();
            std::mt19937 rng(seed * 7919u + 17);
            Globals g;
            NbMem mem;
            std::memset(mem.bytes, 0, sizeof(mem.bytes));
            Nb *nb = mem.Get();
            nbshape::Build(ctx, side == 0, mem.bytes);
            nbshape::CopyOwn(gNbVT, 0); // the fakes are copies of the side's own vtables, patched
            nbshape::CopyOwn(gFaceVT, 1);
            gReady = false;
            Ready();
            AddRange(nb, sizeof(Nb), 0xe0000000);
            gGroup = 0x1000 + (rng() & 0xfff);
            Blob &b = blobs[side];
            const uint32_t r = world(g, nb, rng, side == 0);
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureNb(b, nb);
            CaptureFakes(b, kValueKinds);
            nbshape::Destroy(ctx, side == 0, nb);
        }
        same = Same(ctx, blobs[0], blobs[1], what, seed | (route ? 0x80000000u : 0)) && same;
    }
    nbshape::gRoute = nbshape::kVTableRoute;
    return same;
}

// 16-bit results come back with garbage in the upper half of eax.
uint32_t Wide16(Obj *o, uint32_t low)
{
    o->value[7] = o->value[7] * 1664525u + 1013904223u;
    return (o->value[7] & 0xffff0000u) | (low & 0xffff);
}

void LogKey(const void *key)
{
    const auto *w = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(w[i]);
}
uint32_t KeyArg0(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return 0;
}
uint32_t TextArg0(Obj *, uint32_t *a)
{
    Ev(Hash(reinterpret_cast<const cRZString *>(a[0])->Data()));
    return 0;
}

// ---- tokens and inventories: F(inv) with `count` tokens at gTokens[]; cursors are pointers into that array ----
constexpr unsigned kTokenFirst = 8, kTokenCount = 5;
void *gTokens[kTokenCount + 1];
uint32_t gTokenTotal = 0;

uint32_t InvFind(Obj *self, uint32_t *a) // FindToken(out, guid, start, flagValue, flagMask, backwards, category)
{
    void **out = reinterpret_cast<void **>(a[0]);
    int32_t i = int32_t(a[2]);
    if (i < 0) i = 0;
    for (; uint32_t(i) < self->value[0]; ++i)
        if (static_cast<Obj *>(gTokens[i])->value[0] == a[1]) break;
    *out = &gTokens[i];
    return a[0];
}
uint32_t InvBegin(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(a[0]) = &gTokens[0];
    return a[0];
}
uint32_t InvEnd(Obj *self, uint32_t *a)
{
    *reinterpret_cast<void **>(a[0]) = &gTokens[self->value[0]];
    return a[0];
}
uint32_t TokenCount(Obj *o, uint32_t *) { return Wide16(o, o->value[1]); }
uint32_t TokenProperty(Obj *o, uint32_t *a) { return Wide16(o, o->value[2 + (a[0] % 3)]); }
uint32_t TokenSetFlags(Obj *o, uint32_t *a)
{
    o->value[5] |= a[0];
    return 0;
}

void BuildInventory(Obj &inv, std::mt19937 &rng, uint32_t lot, uint32_t guidA, uint32_t guidB)
{
    inv.value[0] = rng() % (kTokenCount + 1);
    inv.Set(25, "pdd-d--", 0, InvFind);
    inv.Set(28, "p", 0, InvBegin);
    inv.Set(29, "p", 0, InvEnd);
    inv.Set(19, 0);
    for (unsigned t = 0; t < kTokenCount; ++t) {
        Obj &token = F(kTokenFirst + t);
        gTokens[t] = &token;
        const uint32_t pick = rng() % 4;
        token.value[0] = pick == 0 ? guidA : pick == 1 ? guidB : (rng() & 1 ? guidA : 0x1234);
        token.value[1] = (rng() % 5 == 0) ? 0xfffe : rng() % 4;       // the property count (16-bit)
        for (int p = 0; p < 3; ++p) token.value[2 + p] = (rng() % 3 == 0) ? (lot & 0xffff) : (rng() & 0xffff);
        token.value[5] = 0;
        token.Set(18, 0, 0, TokenCount);
        token.Set(21, "d", 0, TokenProperty);
        token.Set(9, "d", 0, TokenSetFlags);
    }
    gTokens[kTokenCount] = nullptr;
}
} // namespace

// ================= FlagLotTokens (0x476e80, cdecl) =================
DIFF_TEST(NeighborhoodLotsB_FlagLotTokens, 0x476e80)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t, int32_t, int32_t);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        if (Pair(ctx, "FlagLotTokens", seed, [&](Globals &, Nb *, std::mt19937 &rng, bool original) {
                const uint32_t lot = (seed & 1) ? rng() : (rng() & 0xffff);
                const int32_t indexes[] = {0, 0, 1, 2, -1, 0x10000, 0x10001};
                const int32_t index = indexes[seed % 7];
                BuildInventory(F(0), rng, lot, 0xeaa2454, 0x2eaa244c);
                const uint32_t guid = (seed % 3 == 0) ? 0x2eaa244c : 0xeaa2454;
                const uint8_t r = original ? ctx.Original<Fn>(0x476e80)(&F(0), guid, int32_t(lot), index)
                                           : FlagLotTokens(reinterpret_cast<ifaces::cITSInventory *>(&F(0)), guid, int32_t(lot), index);
                return uint32_t(r);
            }))
            ++ok;
    }
    std::printf("  FlagLotTokens: %u worlds ok\n", ok);
}

// ================= RemoveLotReferences (0x4735e0) =================
DIFF_TEST(NeighborhoodLotsB_RemoveLotReferences, 0x4735e0)
{
    using Fn = uint8_t(__thiscall *)(Nb *, int32_t);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        if (Pair(ctx, "RemoveLotReferences", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                // 1 lot, 2 its resource, 3 its persist context chain: 4 state controller, 5 context; 6.. sim inventories
                const int32_t number = int32_t(rng() % 50) - 10;
                F(1).value[0] = (seed % 4 == 0) ? 2 : 3;  // lot type
                F(1).Set(32, 0, 0, [](Obj *o, uint32_t *) { return o->value[0]; });
                F(1).Set(25, 0, P(&F(2)));
                F(1).Set(18, 0, uint32_t(number));
                F(2).Set(6, 0, 0x4000 + (rng() & 0xff));
                F(4).Set(34, "d", P(&F(5)));
                F(5).Set(6, "p", 0, KeyArg0);
                g.Set(kSlot23Cache, &F(4));
                for (int i = 0; i < 4; ++i) BuildInventory(F(6 + i % 2), rng, uint32_t(number), 0xeaa2454, 0x2eaa244c);
                if (rng() % 4) nb->mLots[number].Assign(&F(1));
                else nb->mLots[number];  // a null value
                for (int i = 0; i < 3; ++i) nb->mLots[number + 1 + i].Assign(&F(3));
                for (int i = 0; i < int(rng() % 4); ++i) {
                    if (rng() % 5) nb->mMembers3[int16_t(i * 5 + 1)].Assign(&F(6 + (i % 2)));
                    else nb->mMembers3[int16_t(i * 5 + 1)];
                }
                const int32_t asked = (rng() % 5 == 0) ? number + 100 : number;
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 0, 0x4735e0, NB_ADAPTER(RemoveLotReferences), asked));
            }))
            ++ok;
    }
    std::printf("  RemoveLotReferences: %u worlds ok\n", ok);
}

// ================= DoMessage (message face, 0x4738d0) =================
DIFF_TEST(NeighborhoodLotsB_DoMessage, 0x4738d0)
{
    using Fn = uint8_t(__thiscall *)(void *, void *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        if (Pair(ctx, "DoMessage", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool original) {
                // 1 message (QueryInterface), 2 the standard message, 3..6 lots, 7 base lot info, 8 second owner, 9.. inventories
                nbshape::VTableOf(nb, 0) = gNbVT;
                nbshape::VTableOf(nb, 1) = gFaceVT;
                F(kNbFake).Set(21, "d", 1);
                F(1).Set(0, "dp", 0, [](Obj *self, uint32_t *a) -> uint32_t {
                    if (!self->value[0]) return 0;
                    *reinterpret_cast<void **>(a[1]) = &F(2);
                    return 1;
                });
                F(1).value[0] = (seed % 9) != 0;
                const uint32_t types[] = {0x9e, 0xa4, 0xaf, 0x10, 0x9e, 0xa4, 0xaf};
                F(2).value[0] = types[seed % 7];
                F(2).value[1] = (seed % 5) != 0;
                const uint32_t family = 10 + rng() % 3;
                const uint32_t data = (F(2).value[0] == 0xa4) ? (0x12340000u | uint32_t(rng() % 4)) : family;
                F(2).value[2] = data;
                F(2).Set(4, 0, 0, [](Obj *o, uint32_t *) { return o->value[0]; });
                F(2).Set(8, 0, 0, [](Obj *o, uint32_t *) { return o->value[1]; });
                F(2).Set(9, 0, 0, [](Obj *o, uint32_t *) { return o->value[2]; });
                for (unsigned l = 0; l < 4; ++l) {
                    Obj &lot = F(3 + l);
                    lot.value[0] = 10 + rng() % 3;
                    lot.Set(33, 0, 0, [](Obj *o, uint32_t *) { return o->value[0]; });
                    lot.Set(34, 1);
                    lot.Set(4, 0, P(&F(7)));
                    nb->mLots[int32_t(l + 1)].Assign(&lot);
                }
                F(7).Set(23, "d");
                F(8).Set(13, "d");
                nb->mSecondOwner = &F(8);
                for (int i = 0; i < 4; ++i)
                    if (rng() % 3) nb->mMembers2[int16_t(i)].Assign(&F(9 + i));
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 3, 0x4738d0, NB_ADAPTER(DoMessage), &F(1)));
            }))
            ++ok;
    }
    std::printf("  DoMessage: %u worlds ok\n", ok);
}

// ================= FindLotByName (0x474df0) =================
DIFF_TEST(NeighborhoodLotsB_FindLotByName, 0x474df0)
{
    using Fn = void *(__thiscall *)(Nb *, const void *);
    const char *queries[] = {"Alpha", "alpha", "BETA", "gamma", "Delta Lot", "missing", "", "#12", "#", "#abc", "#7xyz", "12", "#-4",
                             "#  9", "zzz", "ALPHA", "Alph", "Alphaa"};
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 360; ++seed) {
        if (Pair(ctx, "FindLotByName", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool original) {
                nbshape::VTableOf(nb, 0) = gNbVT;
                F(kNbFake).Set(57, "d", 0, [](Obj *, uint32_t *a) { return 0x7700u + a[0]; });
                // GetLot's lots are tokens 0x7700 + the number: registered as an object of their own.
                svcfx::AddRange(reinterpret_cast<const void *>(uintptr_t(0x7600)), 0x200, 0x76000000);
                const char *names[] = {"Alpha", "beta", "GAMMA", "Delta Lot", "alphabet"};
                for (unsigned i = 0; i < 5; ++i) {
                    if (rng() % 6 == 0) continue;
                    cRZString name = MakeString(names[i]);
                    nb->mTexts.insert(NeighborhoodTextEntry(*name.AsInterface(), &F(1 + i)));
                }
                // The queries that start with '#' stand for names that start with the game's marker character (read from its data).
                std::string text = queries[seed % (sizeof(queries) / sizeof(queries[0]))];
                if (!text.empty() && text[0] == '#') text[0] = *GameAddress<const char>(0xda33f8);
                cRZString query = MakeString(text);
                void *r = nbshape::Call<Fn>(ctx, original, nb, 0, 0x474df0, NB_ADAPTER(FindLotByName), &query);
                return Norm(P(r));
            }))
            ++ok;
    }
    std::printf("  FindLotByName: %u worlds ok\n", ok);
}

// ================= RenameLot (0x478d80) =================
DIFF_TEST(NeighborhoodLotsB_RenameLot, 0x478d80)
{
    using Fn = uint8_t(__thiscall *)(Nb *, void *, const void *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 360; ++seed) {
        if (Pair(ctx, "RenameLot", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool original) {
                // 1 the lot, 2 its base info, 3.. other lots in the index
                const char *olds[] = {"Alpha", "beta", "Fresh"};
                cRZString oldName = MakeString(olds[seed % 3]);
                F(1).Set(4, 0, P(&F(2)));
                F(2).Set(19, 0, P(&oldName));
                F(2).Set(17, "p", 0, TextArg0);
                const char *others[] = {"Alpha", "beta", "gamma"};
                for (unsigned i = 0; i < 3; ++i) {
                    if (rng() % 5 == 0) continue;
                    cRZString name = MakeString(others[i]);
                    nb->mTexts.insert(NeighborhoodTextEntry(*name.AsInterface(), i == (seed % 3) && rng() % 2 ? &F(1) : &F(3 + i)));
                }
                const char *news[] = {"Alpha", "ALPHA", "delta", "Beta", "gamma", "Fresh", "fresh", "x", "alpha"};
                cRZString newName = MakeString(news[(seed / 3) % 9]);
                void *lot = (seed % 13 == 0) ? nullptr : &F(1);
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 0, 0x478d80, NB_ADAPTER(RenameLot), lot, &newName));
            }))
            ++ok;
    }
    std::printf("  RenameLot: %u worlds ok\n", ok);
}

// ================= NextFreeLotNumber (0x472ad0) =================
DIFF_TEST(NeighborhoodLotsB_NextFreeLotNumber, 0x472ad0)
{
    using Fn = int32_t(__thiscall *)(Nb *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        if (Pair(ctx, "NextFreeLotNumber", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                // 1 state controller, 2 neighbourhood info, 3 persist system
                static cRZString prefix = MakeString("Hood");
                g.Set(kSlot23Cache, &F(1));
                g.Set(kPersistCell, &F(3));
                // 0xc0b232 (an original callee) asks the common implementation service (cached at 0x10cc040, slot 4) for the
                // user directory text and appends the name to it.
                g.Set(0x10cc040, &F(4));
                F(4).Set(4, "dpd-d", 0, [](Obj *, uint32_t *a) -> uint32_t {
                    reinterpret_cast<cRZString *>(a[1])->mString = "Docs/";
                    return 1;
                });
                F(1).Set(6, 0, P(&F(2)));
                F(2).Set(8, 0, P(&prefix));
                F(3).value[0] = rng() % 9;   // the first numbers that exist
                F(3).Set(18, "pp-", 0, [](Obj *o, uint32_t *a) -> uint32_t {
                    Ev(Hash(reinterpret_cast<const cRZString *>(a[0])->Data()));
                    Ev(Hash(reinterpret_cast<const cRZString *>(a[1])->Data()));
                    if (!o->value[0]) return 0;
                    --o->value[0];
                    return 1;
                });
                const uint32_t starts[] = {1, 2, 5, 100, 0xfffffffd, 0xffffffff, 0xfffffffe, 0};
                nb->mField6C = starts[seed % 8];
                if (seed % 8 >= 4) F(3).value[0] = 20;   // everything exists up to the wrap
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 0, 0x472ad0, NB_ADAPTER(NextFreeLotNumber)));
            }))
            ++ok;
    }
    std::printf("  NextFreeLotNumber: %u worlds ok\n", ok);
}

// ================= GetLotInventory (0x474530) =================
DIFF_TEST(NeighborhoodLotsB_GetLotInventory, 0x474530)
{
    using Fn = void *(__thiscall *)(Nb *, int32_t);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 500; ++seed) {
        if (Pair(ctx, "GetLotInventory", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool original) {
                // 1 delegate, 2 second owner, 3 lot, 10.. inventories (made by the framework 20)
                nbshape::VTableOf(nb, 1) = gFaceVT;
                gFrameworkIndex = 20;
                F(20).value[0] = rng() % 8;    // which creations fail (bit mask by call number)
                F(20).value[1] = 0;
                F(20).Set(3, "ddp", 0, [](Obj *self, uint32_t *a) -> uint32_t {
                    const uint32_t n = self->value[1]++;
                    void **out = reinterpret_cast<void **>(a[2]);
                    if ((self->value[0] >> (n % 3)) & 1) return 0;
                    Obj &made = F(10 + n % 8);
                    ++made.refs;
                    *out = &made;
                    return 1;
                });
                for (unsigned i = 0; i < 8; ++i) {
                    F(10 + i).Set(6, "dd");
                    F(10 + i).Set(45, "d");
                }
                F(1).Set(37, 1, P(&F(12)));
                F(2).Set(17, "d", P(&F(13)));
                F(3).Set(1, 0);
                if (rng() % 3 == 0) nb->mDelegate = &F(1);
                if (rng() % 3) nb->mSecondOwner = &F(2);
                const int32_t lots[] = {0, 0x10000, 5, -3, 0x20005, 7, 0x7fff, -0x8000};
                const int32_t lot = lots[seed % 8];
                if (rng() % 3 == 0) nb->mMembers1[int16_t(lot)].Assign(&F(14));
                if (rng() % 2) nb->mLots[int32_t(int16_t(lot))].Assign(&F(3));
                if (rng() % 4 == 0) nb->mMembers1[int16_t(9)].Assign(&F(15));
                void *r = nbshape::Call<Fn>(ctx, original, nb, 0, 0x474530, NB_ADAPTER(GetLotInventory), lot);
                return Norm(P(r));
            }))
            ++ok;
    }
    std::printf("  GetLotInventory: %u worlds ok\n", ok);
}

// ================= UnregisterRenderIds (0x475cb0) =================
DIFF_TEST(NeighborhoodLotsB_UnregisterRenderIds, 0x475cb0)
{
    using Fn = void(__thiscall *)(Nb *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        if (Pair(ctx, "UnregisterRenderIds", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                // 0 scene graph system, 1 debug draw, 2 render id registry
                g.Set(0x10cc458, &F(0));
                F(0).Set(60, 0, (seed % 5 == 0) ? 0 : P(&F(1)));
                F(0).Set(40, 0, (seed % 7 == 0) ? 0 : P(&F(2)));
                F(1).Set(5, 0);
                F(2).Set(12, "d");
                for (uint32_t i = 0; i < rng() % 6; ++i) nb->mPlain[rng() % 100] = rng();
                nbshape::Call<Fn>(ctx, original, nb, 0, 0x475cb0, NB_ADAPTER(UnregisterRenderIds));
                return 0;
            }))
            ++ok;
    }
    std::printf("  UnregisterRenderIds: %u worlds ok\n", ok);
}
