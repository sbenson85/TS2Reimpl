// Differential tests for the event faces (src/gz/cEventFaces.h, lane RB): the image-free program's own vtables for
// nGZEvent::cNullEvent (0xd9ca78), cScalarEvent<unsigned long> (0xd9cb2c) and nGZEvent::cConnectionChangedEvent
// (0xda239c). Each test calls every slot by number through the game's table on an object with the game's vtable and
// through our own table (OwnNullEventVTable() and the others) on our face with the same fields, and compares what the
// slot answers or builds: the type and sizes, the fields after a copy, the copy built in caller memory (a guard past
// it), and the deleting destructor (flags 0 keeps the object; flags 1 frees it: both sides' operator delete recorded
// by shape::DeleteRecorder, the original's through the game's delete, ours through ours). None of our slots may point
// into the image.
// The copies' results are compared only where the original gives one (the connection event's 0x1aa6c4 returns the
// body); the null and scalar copies are void in the game (a bare `ret 4`, and 0x155080 whose eax holds the value).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h" // difftest::AllocationCounter

#include "game/game.h"
#include "gz/cEventFaces.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>

namespace {

constexpr size_t kRoom = 0x20; // the largest event (0x18) and a guard past it

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

// A slot through the object's own vtable.
template <class R, class... A> R Slot(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void *const *const *>(object))[slot])(object, args...);
}

// One face: the game's table, ours, the size and how our face is built from field bytes.
struct Face {
    const char *name;
    uint32_t vtable, size;
    const void *own;
    bool copyGivesBody; // the original's slots 1 and 2 return the body
};

void CheckOwnTable(difftest::Context &ctx, const Face &f)
{
    const void *const *ours = static_cast<const void *const *>(f.own);
    for (int i = 0; i < 7; ++i) {
        ++ctx.cases;
        const uintptr_t at = reinterpret_cast<uintptr_t>(ours[i]);
        if (at - ctx.image.Base() < ctx.image.Size()) ctx.Fail("%s: our vtable slot %d points into the image", f.name, i);
    }
}

// Our face over `memory` with the fields of `fields` (bytes 4..size) and the same bytes past it (the guard).
void MakeOurs(const Face &f, const uint8_t *fields, uint8_t *memory)
{
    std::memset(memory, 0, kRoom);
    if (f.size == 4) ::new (memory) rzface::cNullEvent;
    else if (f.size == 8) ::new (memory) rzface::cScalarEvent<unsigned long>(0);
    else ::new (memory) rzface::cConnectionChangedEvent;
    std::memcpy(memory + 4, fields + 4, kRoom - 4);
}

void Run(difftest::Context &ctx, const Face &f, uint32_t seed)
{
    CheckOwnTable(ctx, f);
    if (!ctx.failures.empty()) return;
    std::mt19937 gen(seed);
    shape::DeleteRecorder deletes;
    const uint32_t vt = Word(GameAddress<void>(f.vtable));
    const uint32_t own = Word(f.own);
    for (unsigned round = 0; round < 400; ++round) {
        alignas(8) uint8_t game[kRoom], from[kRoom], ours[kRoom], oursFrom[kRoom];
        for (size_t i = 0; i < kRoom; ++i) game[i] = uint8_t(gen()), from[i] = uint8_t(gen());
        std::memcpy(game, &vt, 4);
        std::memcpy(from, &vt, 4);
        MakeOurs(f, game, ours);
        MakeOurs(f, from, oursFrom);
        if (std::memcmp(ours, &own, 4) != 0) ctx.Fail("%s: our face does not have our own table", f.name);

        // 3, 4, 5: the type and sizes.
        for (unsigned slot = 3; slot <= 5; ++slot) {
            ++ctx.cases;
            const uint32_t x = Slot<uint32_t>(game, slot), y = Slot<uint32_t>(ours, slot);
            if (x != y) ctx.Fail("%s slot %u round %u: %08x / %08x", f.name, slot, round, x, y);
        }

        // 6: a copy in caller memory (a guard past it), with its class's table.
        {
            ++ctx.cases;
            alignas(8) uint8_t gameCopy[kRoom], ourCopy[kRoom];
            const uint8_t fill = uint8_t(gen());
            std::memset(gameCopy, fill, kRoom);
            std::memset(ourCopy, fill, kRoom);
            Slot<void>(game, 6, static_cast<void *>(gameCopy));
            void *const made = Slot<void *>(ours, 6, static_cast<void *>(ourCopy));
            if (made != ourCopy) ctx.Fail("%s round %u: our copy is not built in the memory given", f.name, round);
            if (std::memcmp(gameCopy + 4, ourCopy + 4, kRoom - 4) != 0)
                ctx.Fail("%s round %u: the copies differ", f.name, round);
            if (std::memcmp(gameCopy, &vt, 4) != 0 || std::memcmp(ourCopy, &own, 4) != 0)
                ctx.Fail("%s round %u: a copy does not have its class's table", f.name, round);
        }

        // 1 and 2: the fields taken from another object of the class.
        for (unsigned slot = 1; slot <= 2; ++slot) {
            ++ctx.cases;
            alignas(8) uint8_t g[kRoom], o[kRoom];
            std::memcpy(g, game, kRoom);
            std::memcpy(o, ours, kRoom);
            void *const rg = Slot<void *>(g, slot, static_cast<const void *>(from));
            void *const ro = Slot<void *>(o, slot, static_cast<const void *>(oursFrom));
            if (std::memcmp(g + 4, o + 4, kRoom - 4) != 0) ctx.Fail("%s slot %u round %u: the copies differ", f.name, slot, round);
            if (std::memcmp(g, &vt, 4) != 0 || std::memcmp(o, &own, 4) != 0)
                ctx.Fail("%s slot %u round %u: a copy changed its table", f.name, slot, round);
            if (ro != o) ctx.Fail("%s slot %u round %u: our copy does not give its body", f.name, slot, round);
            if (f.copyGivesBody && rg != g) ctx.Fail("%s slot %u round %u: the original's copy gave %p", f.name, slot, round, rg);
        }

        // 0, flags 0: the object stays and the body is given back.
        {
            ++ctx.cases;
            alignas(8) uint8_t g[kRoom], o[kRoom];
            std::memcpy(g, game, kRoom);
            std::memcpy(o, ours, kRoom);
            deletes.Begin();
            const bool rg = Slot<void *>(g, 0, 0u) == static_cast<void *>(g);
            const bool ro = Slot<void *>(o, 0, 0u) == static_cast<void *>(o);
            deletes.End();
            if (rg != ro || deletes.Count() != 0)
                ctx.Fail("%s slot 0 flags 0 round %u: %d / %d (%d deletes)", f.name, round, rg, ro, deletes.Count());
            deletes.Release();
            if (std::memcmp(g + 4, o + 4, kRoom - 4) != 0) ctx.Fail("%s slot 0 flags 0 round %u: the bodies differ", f.name, round);
        }
        // 0, flags 1: freed (the original's through the game's delete, ours through ::operator delete).
        {
            ++ctx.cases;
            void *const hg = ::operator new(f.size);
            std::memcpy(hg, game, f.size);
            deletes.Begin();
            Slot<void *>(hg, 0, 1u);
            deletes.End();
            const bool gameFreed = deletes.Count() == 1 && deletes.Is(0, hg, f.size);
            const int gameCount = deletes.Count();
            deletes.Release();
            difftest::AllocationCounter counter; // ours allocates nothing it keeps
            void *const ho = ::operator new(f.size);
            std::memcpy(ho, ours, f.size);
            deletes.Begin();
            Slot<void *>(ho, 0, 1u);
            deletes.End();
            const bool ourFreed = deletes.Count() == 1 && deletes.Is(0, ho, f.size);
            const int ourCount = deletes.Count(), ourFromImage = deletes.FromImage();
            deletes.Release();
            counter.Stop();
            if (counter.Blocks() != 0 || counter.Bytes() != 0)
                ctx.Fail("%s slot 0 flags 1 round %u: ours left %lld blocks (%lld bytes) allocated", f.name, round,
                         counter.Blocks(), counter.Bytes());
            if (!gameFreed || !ourFreed)
                ctx.Fail("%s slot 0 flags 1 round %u: %d / %d deletes (each should free its object, size %u)", f.name,
                         round, gameCount, ourCount, f.size);
            if (ourFromImage != 0) ctx.Fail("%s slot 0 flags 1 round %u: ours reached the game's delete", f.name, round);
        }
        if (!ctx.failures.empty()) return;
    }
}

} // namespace

// The null event's table against 0xd9ca78 (slots 0x154e80, 0x300a0, 0x157710, 0x155a00, 0x6c6b0, 0x1559b0).
DIFF_TEST(EventFaces_NullEvent, 0x157710)
{
    Run(ctx, Face{"cNullEvent", 0xd9ca78, 4, nGZEvent::OwnNullEventVTable(), false}, 0x157710);
}

// The scalar event's table against 0xd9cb2c (slots 0x154ce0, 0x155080, 0x1576c0, 0x1559c0, 0x155a00, 0x1558e0).
DIFF_TEST(EventFaces_ScalarEvent, 0x1576c0)
{
    Run(ctx, Face{"cScalarEvent<unsigned long>", 0xd9cb2c, 8, nGZEvent::OwnScalarEventVTable(), false}, 0x1576c0);
}

// The connection event's table against 0xda239c (slots 0x1aa749, 0x1aa849, 0x1aafc5, 0x1aac22, 0x1aadf2, 0x1aaab0).
DIFF_TEST(EventFaces_ConnectionChangedEvent, 0x1aafc5)
{
    Run(ctx, Face{"cConnectionChangedEvent", 0xda239c, 0x18, nGZEvent::OwnConnectionChangedEventVTable(), true}, 0x1aafc5);
}
