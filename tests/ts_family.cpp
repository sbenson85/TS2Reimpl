// Differential tests for cTSFamily: construction and destruction, QueryInterface/Release, the member id vector, the group
// maps, and the small accessors. The fake world is in ts_family_world.cpp.
#include "ts_family.h"
#include "free_recorder.h"
#include "simantics/ServiceRefVector.h"

using namespace tsfam;

namespace {
constexpr unsigned kSeeds = 300;
uint32_t Key(unsigned n) { return n; }

// The key a test asks for: sometimes one of the fakes' keys, sometimes a made-up one.
FamilyKey PickKey(World &w, uint32_t seed)
{
    std::mt19937 rng(seed + 991);
    if (rng() % 5) return w.objs[rng() % kObjects].key;
    return {0x10u + rng() % 2, 0x20u + rng() % 2, 0x30u + rng() % 3, 0x40u + rng() % 2};
}
} // namespace

// Every test below runs both routes (ts_family.h): `for (int route ...)` loops in the tests that build their own pairs.
DIFF_TEST(Family_Construct, 0x49b340)
{
    using Fn = Family *(__thiscall *)(void *);
    for (int route = 0; route < 2; ++route) {
        for (unsigned seed = 0; seed < 64; ++seed) {
            Guarded a, b;
            std::mt19937 rng(seed);
            for (auto &byte : reinterpret_cast<std::array<uint8_t, sizeof(a)> &>(a)) byte = uint8_t(rng());
            b = a;
            auto *ra = ctx.Original<Fn>(kConstructRva)(a.bytes);
            // Ours: our constructor (the Create hook's path), or the constructor hook's adapter.
            Family *rb = route == 0 ? ::new (b.bytes) Family : reinterpret_cast<Fn>(FAM_ADAPTER(Construct))(b.bytes);
            auto *left = reinterpret_cast<Family *>(a.bytes);
            auto *right = reinterpret_cast<Family *>(b.bytes);
            ++ctx.cases;
            if (ra != left || rb != right) ctx.Fail("construct route %d seed %u return", route, seed);
            // The map heads are separate heap blocks: links to themselves, black nil node. Compare those, then the rest of the bytes.
            struct RawNode { RawNode *left, *parent, *right; uint8_t color, isNil; };
            auto check = [&](Family *f) {
                bool ok = true;
                for (FamilyObjectMap *map : {&f->mGroup1First, &f->mGroup1Second, &f->mGroup2First, &f->mGroup2Second}) {
                    auto *head = *reinterpret_cast<RawNode **>(map);
                    ok &= head && head->left == head && head->parent == head && head->right == head && head->color == 1 && head->isNil == 1;
                    ::operator delete(head, 0x24);
                    *reinterpret_cast<RawNode **>(map) = nullptr;
                }
                auto *setHead = *reinterpret_cast<RawNode **>(&f->mIdSet);
                ok &= setHead && setHead->left == setHead && setHead->parent == setHead && setHead->right == setHead &&
                    setHead->color == 1 && setHead->isNil == 1;
                ::operator delete(setHead, 0x14);
                *reinterpret_cast<RawNode **>(&f->mIdSet) = nullptr;
                return ok;
            };
            // The uninitialised payload of the head nodes is not compared; everything else must match, padding included.
            // Checked (DL): the heads come from the STL's _Buyheadnode (constructor 0x49b340), which sets only links, colour, isnil.
            if (!check(left) || !check(right)) ctx.Fail("construct route %d seed %u head nodes", route, seed);
            // The vtable words: the game's on the left, ours on the right; then compared as codes, like every other test.
            uint32_t *wordsA = reinterpret_cast<uint32_t *>(a.bytes), *wordsB = reinterpret_cast<uint32_t *>(b.bytes);
            if (wordsA[0] != uint32_t(uintptr_t(GameVTable(0))) || wordsA[1] != uint32_t(uintptr_t(GameVTable(1))) ||
                wordsB[0] != uint32_t(uintptr_t(OurVTable(0))) || wordsB[1] != uint32_t(uintptr_t(OurVTable(1))) ||
                OurVTable(0) == GameVTable(0) || OurVTable(1) == GameVTable(1))
                ctx.Fail("construct route %d seed %u vtables", route, seed);
            for (uint32_t *words : {wordsA, wordsB}) words[0] = VtCode(words[0]), words[1] = VtCode(words[1]);
            if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("construct route %d seed %u bytes differ", route, seed);
            if (left->mRefCount != 0 || left->mField0C != 1 || left->mField68 != 0xffffffff || left->mModified != 1 || left->mFlag79 != 1)
                ctx.Fail("construct route %d seed %u fields", route, seed);
        }
    }
    gOurSide = true;
    gRoute = kVTableRoute;
    Build(W(), 1, 0);
    ++ctx.cases;
    if (!FacesWhereExpected()) ctx.Fail("faces not at +0 / +4");
}

DIFF_TEST(Family_Create, 0x49e180)
{
    // The creator (cdecl): operator new(0xc8), then the constructor. Ours is cTSFamily::Create (its hook's entry).
    using Fn = Family *(__cdecl *)();
    for (unsigned n = 0; n < 16; ++n) {
        Family *made[2] = {ctx.Original<Fn>(0x49e180)(), Family::Create()};
        ++ctx.cases;
        if (!made[0] || !made[1]) {
            ctx.Fail("Create returned null");
            return;
        }
        uint32_t words[2][0xc8 / 4];
        for (int side = 0; side < 2; ++side) {
            std::memcpy(words[side], made[side], sizeof(words[side]));
            // Heap addresses (vector, set and map heads: the set head at +0x9c, map heads +0xa8..+0xc0) stand for "allocated".
            for (unsigned off : {0x9cu, 0xa8u, 0xb0u, 0xb8u, 0xc0u}) words[side][off / 4] = words[side][off / 4] != 0;
            words[side][0] = VtCode(words[side][0]);
            words[side][1] = VtCode(words[side][1]);
        }
        if (uint32_t(uintptr_t(*reinterpret_cast<void **>(made[1]))) != uint32_t(uintptr_t(OurVTable(0))))
            ctx.Fail("Create: our object does not carry our vtable");
        // Heap garbage the constructors leave: +0x7a..+0x7b (untouched) and the empty texts' buffers after their first byte
        // (the STL's empty string writes only the terminator; the word at +4 of each text holds it in its low byte).
        for (int side = 0; side < 2; ++side) {
            words[side][0x78 / 4] &= 0x0000ffff;
            for (unsigned base : {0x14u, 0x34u}) {
                words[side][(base + 4) / 4] &= 0xff;
                for (unsigned i = (base + 8) / 4; i < (base + 0x14) / 4; ++i) words[side][i] = 0;
            }
        }
        if (std::memcmp(words[0], words[1], sizeof(words[0])) != 0) ctx.Fail("Create: bytes differ");
        for (Family *f : made) {
            f->Family::~Family();
            ::operator delete(f, sizeof(Family));
        }
    }
}

DIFF_TEST(Family_Destruct, 0x49b690)
{
    using Fn = void(__thiscall *)(Family *);
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        for (uint32_t seed = 0; seed < kSeeds; ++seed) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                gOurSide = side != 0;
                Build(W(), seed, 0xf);
                gTrapHits = 0;
                // Only the destructor's own effects are compared: the references it drops and the plain fields it leaves.
                Call<Fn>(ctx, side == 0, 0x49b690, FAM_ADAPTER(Destruct));
                blobs[side].push_back(uint32_t(gTrapHits));
                Capture(blobs[side], true);
                // The block is destroyed already: Build's cleanup would destroy it a second time. Rebuild as a fresh object.
                Reconstruct();
            }
            Same(ctx, blobs[0], blobs[1], "destruct", RouteSeed(seed));
        }
    }
}

DIFF_TEST(Family_DeletingDestructor, 0x49ba50)
{
    using Fn = Family *(__thiscall *)(Family *, uint32_t);
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        for (uint32_t seed = 0; seed < 200; ++seed) {
            for (uint32_t flags : {0u, 1u, 2u, 3u, 0xffffff00u, 0xffffff01u}) {
                Blob blobs[2];
                bool freed[2]{};
                for (int side = 0; side < 2; ++side) {
                    gOurSide = side != 0;
                    // The family under test lives on the heap, since the destructor may free it.
                    auto *heap = static_cast<Family *>(::operator new(sizeof(Family)));
                    Build(W(), seed, 0xf);
                    std::memcpy(heap, F(), sizeof(Family)); // plain copy of the constructed family: its containers are moved over
                    std::memset(F(), 0, sizeof(Family));    // the world's own copy must not destroy them again
                    Reconstruct();
                    InstallFakes(heap);
                    gTrapHits = 0;
                    difftest::FreeRecorder rec;
                    Family *result = CallOn<Fn>(ctx, heap, side == 0, 0, 0x49ba50, FAM_ADAPTER(DeletingDestructor), flags);
                    rec.Stop();
                    const bool heapFreed = rec.Freed(heap);
                    blobs[side].push_back(result == heap);
                    blobs[side].push_back(uint32_t(gTrapHits));
                    if (flags & 1) {
                        freed[side] = heapFreed;
                    } else {
                        Blob inner;
                        F()->Family::~Family();
                        std::memcpy(F(), heap, sizeof(Family)); // let Capture read it
                        Capture(inner, true);
                        blobs[side].insert(blobs[side].end(), inner.begin(), inner.end());
                        std::memset(F(), 0, sizeof(Family));
                        Reconstruct();
                        ::operator delete(heap, sizeof(Family));
                    }
                    for (const Obj &o : W().objs) blobs[side].push_back(uint32_t(o.refs));
                }
                if (freed[0] != freed[1]) ctx.Fail("deleting destructor seed %u flags %x: freed differs", RouteSeed(seed), flags);
                if ((flags & 1) && !freed[1]) ctx.Fail("deleting destructor seed %u flags %x: not freed", RouteSeed(seed), flags);
                Same(ctx, blobs[0], blobs[1], "deleting destructor", RouteSeed(seed));
            }
        }
    }
}

DIFF_TEST(Family_QueryInterface, 0x49f590)
{
    using Fn = uint8_t(__thiscall *)(Family *, uint32_t, void **);
    const uint32_t ids[] = {1, 0x7ab5bcbb, 0xe4fda3d4, 0x456b8f1d, 0, 2, 0x7ab5bcbc, 0xe4fda3d3, 0x456b8f1c, 0x456b8f1e, 0xffffffff, 0x80000000};
    for (uint32_t seed = 0; seed < 40; ++seed) {
        for (uint32_t id : ids) {
            Pair(ctx, "QueryInterface", seed, 0, [&](bool original) -> uint32_t {
                void *out = reinterpret_cast<void *>(0x1234567);
                const uint8_t r = Call<Fn>(ctx, original, 0x49f590, FAM_ADAPTER(QueryInterface), id, &out);
                W().events.push_back(Id(out) == 1 ? uint32_t(reinterpret_cast<uintptr_t>(out) - reinterpret_cast<uintptr_t>(F())) : Id(out));
                return r;
            });
        }
    }
}

// The persist face's adjustor thunks (0x49f5e2 QueryInterface, 0x1448e9 AddRef, 0x49fe9a Release): the game's on the original
// side, ours (compiler-made, no hook) through our vtable's +4 face on both routes.
DIFF_TEST(Family_PersistFaceThunks, 0x49f5e2)
{
    using QI = uint8_t(__thiscall *)(void *, uint32_t, void **);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        for (uint32_t id : {1u, 0x456b8f1du, 0xe4fda3d4u, 77u}) {
            Pair(ctx, "face QueryInterface", seed, 0, [&](bool original) -> uint32_t {
                void *out = reinterpret_cast<void *>(0x1234567);
                const uint8_t r = CallSlot<QI>(F(), original, 1, 0, id, &out);
                W().events.push_back(uint32_t(reinterpret_cast<uintptr_t>(out) - reinterpret_cast<uintptr_t>(F())));
                return r;
            });
        }
        Pair(ctx, "face AddRef", seed, 0, [&](bool original) -> uint32_t {
            return CallSlot<uint32_t(__thiscall *)(void *)>(F(), original, 1, 1);
        });
    }
}

DIFF_TEST(Family_Release, 0x49fe80)
{
    for (uint32_t seed = 0; seed < 100; ++seed) {
        for (uint32_t count : {0u, 1u, 2u, 3u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
            for (int face = 0; face < 2; ++face) {
                Pair(ctx, "Release", seed, 0, [&](bool original) -> uint32_t {
                    F()->mRefCount = count;
                    if (face == 0) return Call<uint32_t(__thiscall *)(Family *)>(ctx, original, 0x49fe80, FAM_ADAPTER(Release));
                    return CallSlot<uint32_t(__thiscall *)(void *)>(F(), original, 1, 2); // the thunk 0x49fe9a
                });
            }
        }
    }
}

DIFF_TEST(Family_GetGZCLSID, 0x49e250)
{
    Pair(ctx, "GetGZCLSID", 1, 0, [&](bool original) -> uint32_t {
        return Call<uint32_t(__thiscall *)(Family *)>(ctx, original, 0x49e250, FAM_ADAPTER(GetGZCLSID));
    });
    ++ctx.cases;
    if (cTSFamilyAdapters::GetGZCLSID(F(), nullptr) != 0x46414d49) ctx.Fail("GetGZCLSID");
}
DIFF_TEST(Family_SetFamilyId, 0x49efb0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t id = rng() % 3 ? rng() % 40000 : rng();
        Pair(ctx, "SetFamilyId", seed, 8, [&](bool original) -> uint32_t {
            return Call<uint8_t(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49efb0, FAM_ADAPTER(SetFamilyId), id);
        });
    }
}

DIFF_TEST(Family_AddRemoveMemberId, 0x49cc20)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed + 5);
        const uint32_t id = rng() % 3 ? 0x100 + rng() % 7 : rng();
        Pair(ctx, "AddMemberId", seed, 1, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49cc20, FAM_ADAPTER(AddMemberId), id);
            return 0;
        });
        Pair(ctx, "RemoveMemberId", seed, 1, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x4a03a0, FAM_ADAPTER(RemoveMemberId), id);
            return 0;
        });
    }
    // The vector grows past its capacity and keeps growing.
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Pair(ctx, "AddMemberId growth", seed, 1, [&](bool original) -> uint32_t {
            for (uint32_t i = 0; i < 40; ++i) {
                Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49cc20, FAM_ADAPTER(AddMemberId), 0x300 + i % 25);
            }
            return 0;
        });
    }
}

DIFF_TEST(Family_MemberIdAccessors, 0x49ef80)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed + 9);
        for (int32_t index : {-1, 0, 1, 2, 5, 6, 7, 100, int32_t(0x80000000), int32_t(0x7fffffff)}) {
            Pair(ctx, "GetMemberIdSlot", seed, 1, [&](bool original) -> uint32_t {
                uint32_t *p = Call<uint32_t *(__thiscall *)(Family *, int32_t)>(ctx, original, 0x49ef80, FAM_ADAPTER(GetMemberIdSlot), index);
                return p ? uint32_t(p - F()->mMemberIds.data()) + 1 : 0;
            });
            Pair(ctx, "GetMemberId", seed, 1, [&](bool original) -> uint32_t {
                return Call<uint32_t(__thiscall *)(Family *, int32_t)>(ctx, original, 0x49dfa0, FAM_ADAPTER(GetMemberId), index);
            });
        }
        const uint32_t id = rng() % 3 ? 0x100 + rng() % 7 : rng();
        Pair(ctx, "FindMemberId", seed, 1, [&](bool original) -> uint32_t {
            uint32_t *p = Call<uint32_t *(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49f450, FAM_ADAPTER(FindMemberId), id);
            return p ? uint32_t(p - F()->mMemberIds.data()) + 1 : 0;
        });
        Pair(ctx, "HasMemberId", seed, 1, [&](bool original) -> uint32_t {
            return Call<uint8_t(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x4a0e30, FAM_ADAPTER(HasMemberId), id);
        });
    }
}

DIFF_TEST(Family_HasObjectInGroup, 0x49d870)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        World &w = W();
        for (int group = 0; group < 2; ++group) {
            Build(w, seed, 4);
            const FamilyKey key = PickKey(w, seed);
            Pair(ctx, "HasObjectInGroup", seed, 4, [&](bool original) -> uint32_t {
                if (group == 0)
                    return Call<uint8_t(__thiscall *)(Family *, const FamilyKey *)>(ctx, original, 0x49d870, FAM_ADAPTER(HasObjectInGroup1), &key);
                return Call<uint8_t(__thiscall *)(Family *, const FamilyKey *)>(ctx, original, 0x49d8e0, FAM_ADAPTER(HasObjectInGroup2), &key);
            });
        }
    }
}

DIFF_TEST(Family_AddObjectToGroup, 0x49ff30)
{
    for (uint32_t seed = 0; seed < 600; ++seed) {
        World &w = W();
        for (int group = 0; group < 2; ++group) {
            std::mt19937 rng(seed + 31);
            const unsigned which = rng() % (kObjects + 2); // two out of range pick null
            Pair(ctx, "AddObjectToGroup", seed, 4, [&](bool original) -> uint32_t {
                void *object = which < kObjects ? &w.objs[which] : nullptr;
                // Half of the cases ask for an object whose key is already in the second map.
                FamilyObjectMap &second = group == 0 ? F()->mGroup1Second : F()->mGroup2Second;
                if (seed % 2 == 0 && which < kObjects && !second.empty()) w.objs[which].key = second.begin()->first;
                if (group == 0) {
                    Call<void(__thiscall *)(Family *, void *)>(ctx, original, 0x49ff30, FAM_ADAPTER(AddObjectToGroup1), object);
                } else {
                    Call<void(__thiscall *)(Family *, void *)>(ctx, original, 0x4a02b0, FAM_ADAPTER(AddObjectToGroup2), object);
                }
                return 0;
            });
        }
    }
}

DIFF_TEST(Family_CollectObjectsOfGroup1, 0x49e270)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "CollectObjectsOfGroup1", seed, 4, [&](bool original) -> uint32_t {
            void *storage[32]{};
            ServiceRefVector vector{};
            vector.mFirst = vector.mLast = storage;
            vector.mEnd = storage + 32;
            Call<void(__thiscall *)(Family *, ServiceRefVector *)>(ctx, original, 0x49e270, FAM_ADAPTER(CollectObjectsOfGroup1), &vector);
            W().events.push_back(uint32_t(vector.mLast - vector.mFirst));
            for (void **p = vector.mFirst; p != vector.mLast; ++p) W().events.push_back(Id(*p));
            return 0;
        });
    }
}

DIFF_TEST(Family_SmallAccessors, 0x49eff0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed + 77);
        const uint32_t value = rng() % 3 ? rng() % 2 : rng();
        // GetFlag79 (4 bytes) has no hook: through the vtables only.
        Pair(ctx, "GetFlag79", seed, 0, [&](bool original) -> uint32_t {
            return CallSlot<uint8_t(__thiscall *)(Family *)>(F(), original, 0, 22);
        });
        Pair(ctx, "SetFlag79", seed, 0, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x4a0c70, FAM_ADAPTER(SetFlag79), value);
            return 0;
        });
        const uint32_t lot = rng() % 3 ? rng() % 5 : rng();
        Pair(ctx, "IsFamilyLot", seed, 0, [&](bool original) -> uint32_t {
            return Call<uint8_t(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49f000, FAM_ADAPTER(IsFamilyLot), lot);
        });
        // Flag bit accessors: set with garbage in the upper bits of the argument, read back.
        struct Bit { uint32_t setRva, getRva; void *set, *get; };
        const Bit bits[] = {{0x4a0d70, 0x49eec0, FAM_ADAPTER(SetFlagBit0), FAM_ADAPTER(GetFlagBit0)},
                            {0x4a0d40, 0x49eeb0, FAM_ADAPTER(SetFlagBit3), FAM_ADAPTER(GetFlagBit3)},
                            {0x4a0d10, 0x49eea0, FAM_ADAPTER(SetFlagBit1), FAM_ADAPTER(GetFlagBit1)},
                            {0x4a0df0, 0x49f4b0, FAM_ADAPTER(SetFlagBit2), FAM_ADAPTER(GetFlagBit2)}};
        for (const Bit &bit : bits) {
            const uint32_t arg = rng() % 2 ? (rng() & ~0xffu) | (rng() % 2) : rng();
            Pair(ctx, "flag bit", seed, 0, [&](bool original) -> uint32_t {
                Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, bit.setRva, bit.set, arg);
                return Call<uint8_t(__thiscall *)(Family *)>(ctx, original, bit.getRva, bit.get);
            });
        }
    }
}

DIFF_TEST(Family_IdSet, 0x49c5d0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed + 3);
        const uint32_t id = rng() % 4 ? rng() % 9 : rng();
        Pair(ctx, "AddToIdSet", seed, 2, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49c5d0, FAM_ADAPTER(AddToIdSet), id);
            return 0;
        });
        Pair(ctx, "ClearIdSet", seed, 2, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *)>(ctx, original, 0x49fed0, FAM_ADAPTER(ClearIdSet));
            return 0;
        });
    }
}

DIFF_TEST(Family_SetNameAndDescription, 0x4a0dd0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed + 11);
        std::string text;
        for (unsigned i = 0, n = rng() % 40; i < n; ++i) text.push_back(char('a' + rng() % 26));
        for (int which = 0; which < 2; ++which) {
            Pair(ctx, "SetName", seed, 8, [&](bool original) -> uint32_t {
                cRZString source;
                source.Construct(text.c_str());
                if (which == 0) {
                    Call<void(__thiscall *)(Family *, const void *)>(ctx, original, 0x4a0dd0, FAM_ADAPTER(SetName), &source);
                } else {
                    Call<void(__thiscall *)(Family *, const void *)>(ctx, original, 0x4a0c50, FAM_ADAPTER(SetDescription), &source);
                }
                source.Destruct();
                return 0;
            });
        }
    }
}

namespace {
void *gDirectorVT[16];
void *gFrameworkVT[16];
Obj gFramework;
void *__fastcall DirectorSlot12(void *, void *) { Ev({evFramework, 12}); return &gFramework; }
void __fastcall FrameworkSlot7(Obj *, void *, uint32_t *out)
{
    Ev({evFramework, 7});
    *out = 0xabc000 + uint32_t(W().events.size());
}
} // namespace

DIFF_TEST(Family_GetUniqueId, 0x49d590)
{
    // GetDirectorFrameworkObject (0x1ae18d) reads the director static at 0x10c9390 (its guard at 0x10c93f4 marks it built).
    void *savedDirector = *ctx.image.At<void *>(0x10c9390);
    const int32_t savedGuard = *ctx.image.At<int32_t>(0x10c93f4);
    gDirectorVT[12] = reinterpret_cast<void *>(&DirectorSlot12);
    gFrameworkVT[7] = reinterpret_cast<void *>(&FrameworkSlot7);
    gFramework.vt = gFrameworkVT;
    *ctx.image.At<void *>(0x10c9390) = gDirectorVT;
    *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed + 21);
        const uint32_t create = rng() % 3 ? rng() % 2 : rng();
        Pair(ctx, "GetUniqueId", seed, 0, [&](bool original) -> uint32_t {
            return Call<uint32_t(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49d590, FAM_ADAPTER(GetUniqueId), create);
        });
    }
    *ctx.image.At<void *>(0x10c9390) = savedDirector;
    *ctx.image.At<int32_t>(0x10c93f4) = savedGuard;
}

// The slots no other test calls (folded getters and setters, the persist face's accessors), through each side's own vtable: the
// game's on the original side, our pristine one on ours. Pointer results are compared as offsets into the family.
DIFF_TEST(Family_Slots, 0x49e250)
{
    struct SlotCase { unsigned face, slot, args; bool pointer; };
    const SlotCase cases[] = {
        {0, 1, 0, false},  {0, 31, 0, true},  {0, 37, 0, true},  {0, 39, 0, true},  {0, 41, 0, false}, {0, 42, 0, false},
        {0, 43, 1, false}, {0, 44, 0, false}, {0, 45, 1, false}, {0, 46, 0, false}, {0, 47, 1, false}, {0, 50, 1, false},
        {0, 51, 0, false}, {0, 52, 1, false}, {0, 55, 0, false}, {0, 56, 1, false}, {0, 57, 0, false}, {0, 58, 1, false},
        {1, 5, 0, false},  {1, 6, 0, false},  {1, 7, 0, false},  {1, 8, 1, false},  {1, 9, 1, false},  {1, 10, 1, false},
        {1, 11, 0, false},
    };
    for (uint32_t seed = 0; seed < 60; ++seed) {
        std::mt19937 rng(seed + 404);
        const uint32_t value = rng();
        for (const SlotCase &c : cases) {
            Pair(ctx, "slot", seed * 100 + c.face * 50 + c.slot, 0, [&](bool original) -> uint32_t {
                W().events.push_back(c.face * 100 + c.slot);
                if (c.args == 0) {
                    const uint32_t r = CallSlot<uint32_t(__thiscall *)(void *)>(F(), original, c.face, c.slot);
                    if (c.pointer) return r ? r - uint32_t(uintptr_t(F())) + 1 : 0;
                    // Setters return garbage in eax (the argument); getters' results are compared whole.
                    return r;
                }
                const uint32_t r = CallSlot<uint32_t(__thiscall *)(void *, uint32_t)>(F(), original, c.face, c.slot, value);
                // The family setters (+0 face) are void: their eax is not part of the contract. The persist setters answer AL 1.
                return c.face == 0 ? 0 : r & 0xff;
            });
        }
        // The persist face's key: GetKey (slot 3, hidden result pointer) and SetKey (slot 4, AL 1).
        Pair(ctx, "persist GetKey", seed, 0, [&](bool original) -> uint32_t {
            uint32_t key[4] = {1, 2, 3, 4};
            auto *r = CallSlot<uint32_t *(__thiscall *)(void *, uint32_t *)>(F(), original, 1, 3, key);
            W().events.insert(W().events.end(), key, key + 4);
            return r == key;
        });
        Pair(ctx, "persist SetKey", seed, 0, [&](bool original) -> uint32_t {
            const uint32_t key[4] = {value, value ^ 0x5555, value + 7, ~value};
            return CallSlot<uint8_t(__thiscall *)(void *, const uint32_t *)>(F(), original, 1, 4, key);
        });
    }
}
