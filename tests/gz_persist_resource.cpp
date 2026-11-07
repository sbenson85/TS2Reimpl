// Differential tests for cGZPersistResource in the standalone shape (src/gz/cGZPersistResource.cpp, lane EL):
//   0x9b5fd0 constructor, 0x9b6030 destructor, 0x9b6050 deleting destructor (slot 12), 0x9b6260 Release (slot 2), and the folded
//   slots through our vtable: 0x437e30 QI, 0x99ba50 AddRef, 0x18a670 / 0x4f3d80 GetKey / SetKey, 0x145770 / 0x149cf0 / 0x149d00
//   the key getters, 0x1eeb70 / 0x1eec40 / 0x47d410 the setters, 0x147a30 the count.
// Original side: the game's constructor (the game's vtable 0xe603ac); ours: our constructor through the constructor hook's adapter,
// every slot through our vtable. The vtable word is compared as "the side's own". Adapters: on a game-built object whose vtable
// copy has traps (of the slot's arity) in the slots the adapters forward to, as the game's derived classes reach them.
#include "difftest.h"
#include "free_recorder.h"

#include "game/game.h"
#include "gz/cGZPersistResource.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Res = cGZPersistResource;
using Adapters = cGZPersistResourceAdapters;
using Blob = std::vector<uint32_t>;

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }
uint32_t GameWord(uint32_t rva) { return uint32_t(uintptr_t(GameAddress<void>(rva))); }
uint32_t Word(const void *p, uint32_t at = 0) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(p) + at); }

template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

uint32_t OurVTable()
{
    alignas(4) uint8_t raw[sizeof(Res)];
    ::new (raw) Res;
    return Word(raw);
}

// The side's object on `memory`: the game's constructor or ours (the constructor hook's adapter).
void *Build(int side, void *memory)
{
    return side == 0 ? Game<void *(__thiscall *)(void *)>(0x9b5fd0)(memory) : static_cast<void *>(Adapters::Construct(memory, nullptr));
}

// The object's words, the vtable word as 1 when it is the side's own (and ours is not the game's).
void Words(Blob &b, int side, const void *object)
{
    const uint32_t game = GameWord(Res::kOriginalVTable), ours = OurVTable();
    const uint32_t own = side == 0 ? game : ours;
    b.push_back(Word(object) == own && ours != game ? 1u : Word(object));
    for (uint32_t at = 4; at < sizeof(Res); at += 4) b.push_back(Word(object, at));
}

} // namespace

// Constructor 0x9b5fd0 on random-filled memory, then the destructor 0x9b6030 (the hook adapter on our side: it is what the game's
// derived classes call) or the deleting destructor 0x9b6050 (through each side's own slot 12) with flags 0 / 1.
DIFF_TEST(PersistResource_ConstructDestroy, 0x9b5fd0)
{
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        std::mt19937 rng(seed);
        uint8_t fill[sizeof(Res)];
        for (auto &c : fill) c = uint8_t(rng());
        uint32_t later[sizeof(Res) / 4];
        for (auto &w : later) w = rng();
        Blob runs[2];
        for (int side = 0; side < 2; ++side) {
            auto *p = static_cast<uint8_t *>(::operator new(sizeof(Res)));
            std::memcpy(p, fill, sizeof(fill));
            Blob &b = runs[side];
            b.push_back(Build(side, p) == p);
            Words(b, side, p);
            // Fields the destructor must leave alone.
            for (uint32_t at = 4; at < sizeof(Res); at += 4) *reinterpret_cast<uint32_t *>(p + at) = later[at / 4];
            difftest::FreeRecorder rec;
            if (seed % 3 == 0) {
                if (side == 0)
                    Game<void(__thiscall *)(void *)>(0x9b6030)(p);
                else
                    Adapters::Destruct(reinterpret_cast<Res *>(p), nullptr);
            } else {
                const uint32_t flags = seed & 1;
                void *x = side == 0 ? Game<void *(__thiscall *)(void *, uint32_t)>(0x9b6050)(p, flags) : CallSlot<void *>(p, 12, flags);
                b.push_back(x == p);
            }
            rec.Stop();
            const bool freed = rec.Freed(p);
            b.push_back(freed);
            if (!freed) {
                Words(b, side, p);
                ::operator delete(p);
            }
        }
        ++ctx.cases;
        if (runs[0] != runs[1]) ctx.Fail("seed %u differs (%zu/%zu words)", seed, runs[0].size(), runs[1].size());
    }
}

// Every slot 0-11 through each side's own vtable: QueryInterface (folded 0x437e30: ids 1 and 0x456b8f1d, the out-parameter as
// "this" / untouched), AddRef (folded 0x99ba50), Release 0x9b6260 (at 0 the object deletes itself), the key slots and the count.
// Compared: results, out-parameters, the object's words, frees.
DIFF_TEST(PersistResource_Slots, 0x437e30)
{
    const uint32_t ids[] = {1, 0x456b8f1d, 0, 0x456b8f1c, 0x4bb0d269};
    for (int slot = 0; slot < 12; ++slot)
        for (uint32_t count : {0u, 1u, 2u, 0x7fffffffu, 0xffffffffu})
            for (uint32_t arg = 0; arg < 5; ++arg) {
                std::mt19937 rng(slot * 100 + arg);
                const uint32_t key[4] = {rng(), rng(), rng(), rng()};
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    auto *p = static_cast<uint8_t *>(::operator new(sizeof(Res)));
                    Build(side, p);
                    std::memcpy(p + 0xc, key, sizeof(key));
                    *reinterpret_cast<uint32_t *>(p + 4) = count;
                    *reinterpret_cast<uint32_t *>(p + 8) = 0x8888 + arg;
                    Blob &b = runs[side];
                    difftest::FreeRecorder rec;
                    switch (slot) {
                    case 0: {
                        void *out = reinterpret_cast<void *>(0x5151);
                        b.push_back(CallSlot<uint8_t>(p, 0, ids[arg], &out));
                        b.push_back(out == p ? 1u : out == reinterpret_cast<void *>(0x5151) ? 2u : 3u);
                        break;
                    }
                    case 3: {
                        uint32_t got[4] = {};
                        b.push_back(CallSlot<uint32_t *>(p, 3, got) == got);
                        b.insert(b.end(), got, got + 4);
                        break;
                    }
                    case 4: {
                        const uint32_t next[4] = {arg, arg * 3, ~arg, 0x44};
                        b.push_back(CallSlot<uint8_t>(p, 4, next));
                        break;
                    }
                    case 8:
                    case 9:
                    case 10: b.push_back(CallSlot<uint8_t>(p, slot, 0x1000 + arg)); break;
                    default: b.push_back(CallSlot<uint32_t>(p, slot)); break;
                    }
                    rec.Stop();
                    const bool freed = rec.Freed(p);
                    b.push_back(freed);
                    if (!freed) {
                        Words(b, side, p);
                        ::operator delete(p);
                    }
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) ctx.Fail("slot %d count %08x arg %u differs", slot, count, arg);
            }
}

// The adapters on a game-built object (the game's derived classes call the constructor and destructor RVAs directly and jump to
// Release): a copy of the game's vtable with traps in slot 2 (Release, no arguments) and slot 12 (the deleting destructor, one),
// except that Release legitimately calls slot 12: a recording delete there for the Release cases. The original side calls the
// RVA, ours the adapter. Compared: results, the trap / delete log, frees, the object's words.
namespace {
Blob gLog;
uint32_t __fastcall Trap0(void *, void *) { gLog.push_back(0x7a7a0000); return 0; }
void *__fastcall Trap1(void *self, void *, uint32_t) { gLog.push_back(0x7a7a0001); return self; }
void *__fastcall RecordingDelete(void *self, void *, uint32_t flags)
{
    gLog.insert(gLog.end(), {0xde1u, flags});
    if (flags & 1) ::operator delete(self);
    return self;
}
void *gTrapVT[13];
} // namespace

DIFF_TEST(PersistResource_Adapters, 0x9b6260)
{
    std::memcpy(gTrapVT, GameAddress<void>(Res::kOriginalVTable), sizeof(gTrapVT));
    gTrapVT[2] = reinterpret_cast<void *>(&Trap0);
    for (int which = 0; which < 3; ++which) // 0 Release, 1 Destruct, 2 DeletingDestructor
        for (uint32_t count : {0u, 1u, 2u, 5u, 0x80000000u})
            for (uint32_t flags : {0u, 1u}) {
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    auto *p = static_cast<uint8_t *>(::operator new(sizeof(Res)));
                    Game<void *(__thiscall *)(void *)>(0x9b5fd0)(p); // the game's, on both sides
                    gTrapVT[12] = which == 0 ? reinterpret_cast<void *>(&RecordingDelete) : reinterpret_cast<void *>(&Trap1);
                    *reinterpret_cast<void **>(p) = gTrapVT;
                    *reinterpret_cast<uint32_t *>(p + 4) = count;
                    *reinterpret_cast<uint32_t *>(p + 0x10) = 0x1234 + count;
                    auto *self = reinterpret_cast<Res *>(p);
                    gLog.clear();
                    Blob &b = runs[side];
                    difftest::FreeRecorder rec;
                    switch (which) {
                    case 0:
                        b.push_back(side == 0 ? Game<uint32_t(__thiscall *)(void *)>(0x9b6260)(p) : Adapters::Release(self, nullptr));
                        break;
                    case 1:
                        side == 0 ? Game<void(__thiscall *)(void *)>(0x9b6030)(p) : Adapters::Destruct(self, nullptr);
                        break;
                    default:
                        b.push_back((side == 0 ? Game<void *(__thiscall *)(void *, uint32_t)>(0x9b6050)(p, flags)
                                               : static_cast<void *>(Adapters::DeletingDestructor(self, nullptr, flags))) == p);
                        break;
                    }
                    rec.Stop();
                    const bool freed = rec.Freed(p);
                    b.push_back(freed);
                    b.insert(b.end(), gLog.begin(), gLog.end());
                    if (!freed) {
                        // The trap copy (untouched by Release) or the side's own base vtable (stored by the destructors).
                        const uint32_t word = Word(p);
                        b.push_back(word == uint32_t(uintptr_t(gTrapVT)) ? 3u : 0u);
                        Words(b, side, p);
                        ::operator delete(p);
                    }
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) ctx.Fail("adapter %d count %08x flags %u differs", which, count, flags);
            }
}
