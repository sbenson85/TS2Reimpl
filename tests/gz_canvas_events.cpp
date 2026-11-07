// Differential tests for the window service's events (src/gz/cCanvasEvents.h, lane OQ): cCanvasEvent,
// cCanvasSizeMoveEvent, cMouseEvent and cKeyEvent.
// - CanvasEvents_GameSlots: the slot bodies each class has to itself, hooked on the game's vtables
//   (cCanvasEventsAdapters), against the originals, on objects with the game's vtable and seeded fields: the results,
//   the object's bytes, the destination's bytes (a guard word past it), and for the deleting destructors the freed
//   block (the game's delete 0x824624 stood in on both sides by a recorder).
// - CanvasEvents_OurSlots: our classes (our vtables) against the game's vtables (every slot, the folded ones included),
//   on the same fields: what each slot answers or builds.
// The copies' results are not compared: the originals are void (eax holds the last word copied, e.g. 0x155900
// `mov eax, [ecx+4]; mov [edx+4], eax; ret 4`, 0x155090 `mov eax, [eax+0x14]; mov [ecx+0x14], eax; ret 4`) and no
// caller reads it (the hook-shaped copies give the body, as our slots do).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h" // difftest::AllocationCounter

#include "game/game.h"
#include "gz/cCanvasEvents.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kHeaderVTable = 0xd9ca58; // nGZEvent::cIEventHeader's: what the base destructor leaves
constexpr size_t kRoom = 0x28;               // a destination: the largest event (0x20) and a guard past it

// One class: its game vtable, size and own slots (0: folded with other classes, not hooked).
struct Spec {
    const char *name;
    uint32_t vtable, size;
    uint32_t own[7];
    void *adapter[7];
};

const Spec kSpecs[] = {
    {"cCanvasEvent", cCanvasEvent::kGameVTable, 8, {0, 0, 0, 0x1576d0, 0, 0, 0x155900},
     {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&cCanvasEventsAdapters::CanvasEventType), nullptr, nullptr,
      reinterpret_cast<void *>(&cCanvasEventsAdapters::CanvasCloneInto)}},
    {"cCanvasSizeMoveEvent", cCanvasSizeMoveEvent::kGameVTable, 0x18, {0x154d10, 0x155090, 0x155090, 0x1576e0, 0x1559d0, 0, 0x155920},
     {reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveDeletingDestructor),
      reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveAssign), reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveAssign),
      reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveEventType), reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveSize),
      nullptr, reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveCloneInto)}},
    {"cMouseEvent", cMouseEvent::kGameVTable, 0x20, {0x154ee0, 0x1550d0, 0x1550d0, 0x157700, 0, 0, 0x155970},
     {reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseDeletingDestructor), reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseAssign),
      reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseAssign), reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseEventType),
      nullptr, nullptr, reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseCloneInto)}},
    {"cKeyEvent", cKeyEvent::kGameVTable, 0x14, {0, 0, 0, 0x1576f0, 0, 0, 0x155940},
     {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&cCanvasEventsAdapters::KeyEventType), nullptr, nullptr,
      reinterpret_cast<void *>(&cCanvasEventsAdapters::KeyCloneInto)}},
};

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

// Seeded fields past the vtable word (the caller's): random bytes, the mouse event's flag a real bool (the procedure
// writes 0 / 1), its three bytes after the flag whatever they are.
void Seed(std::mt19937 &gen, const Spec &s, uint8_t *object)
{
    for (size_t i = 4; i < s.size; ++i) object[i] = uint8_t(gen());
    if (s.size == 0x20) object[0x14] = uint8_t(gen() % 2);
}

// The freed block, as the game's delete hears it.
struct Freed {
    void *block = nullptr;
    uint32_t size = 0, calls = 0;
} gFreed;
void __cdecl RecordDelete(void *block, size_t size)
{
    gFreed.block = block;
    gFreed.size = uint32_t(size);
    ++gFreed.calls;
}

template <class R, class... A> R Thiscall(uint32_t rva, difftest::Context &ctx, void *self, A... args)
{
    return ctx.Original<R(__thiscall *)(void *, A...)>(rva)(self, args...);
}
template <class R, class... A> R Fastcall(void *f, void *self, A... args)
{
    return reinterpret_cast<R(__fastcall *)(void *, void *, A...)>(f)(self, nullptr, args...);
}

// A slot through the object's own vtable.
template <class R, class... A> R Slot(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void *const *const *>(object))[slot])(object, args...);
}

} // namespace

// The hooked slots (0x1576d0, 0x155900, 0x154d10, 0x155090, 0x1576e0, 0x1559d0, 0x155920, 0x154ee0, 0x1550d0, 0x157700,
// 0x155970, 0x1576f0, 0x155940) against the adapters, on the game's objects.
DIFF_TEST(CanvasEvents_GameSlots, 0x155900)
{
    std::mt19937 gen(0x155900);
    // The game's delete stood in by a recorder on both sides: the original calls 0x824624, ours ::operator delete
    // (the host's, the game's heap in game); the recorder is our side's delete only through this patch.
    shape::CodePatch del{0x824624, reinterpret_cast<const void *>(&RecordDelete)};
    for (const Spec &s : kSpecs) {
        for (unsigned round = 0; round < 400; ++round) {
            alignas(8) uint8_t a[kRoom], b[kRoom], other[kRoom], da[kRoom], db[kRoom];
            std::memset(a, 0, sizeof(a));
            const uint32_t vt = Word(GameAddress<void>(s.vtable));
            std::memcpy(a, &vt, 4);
            Seed(gen, s, a);
            for (size_t i = s.size; i < kRoom; ++i) a[i] = uint8_t(gen());
            std::memcpy(b, a, kRoom);
            std::memcpy(other, &vt, 4);
            for (size_t i = 4; i < kRoom; ++i) other[i] = uint8_t(gen());
            if (s.size == 0x20) other[0x14] = uint8_t(gen() % 2);
            const uint8_t fill = uint8_t(gen());
            for (unsigned slot = 0; slot < 7; ++slot) {
                if (!s.own[slot]) continue;
                ++ctx.cases;
                std::memset(da, fill, kRoom);
                std::memset(db, fill, kRoom);
                uint32_t ra = 0, rb = 0;
                switch (slot) {
                case 0: {
                    // flags 0: the object stays (its vtable word the header's); flags 1: it is freed (a heap copy).
                    const uint32_t flags = gen() % 2;
                    if (flags) {
                        void *const ha = ::operator new(s.size);
                        std::memcpy(ha, a, s.size);
                        gFreed = {};
                        ra = Word(Thiscall<void *>(s.own[0], ctx, ha, flags)) == Word(ha);
                        const Freed fa = gFreed;
                        gFreed = {};
                        // Ours frees through ::operator delete (the recorder hears only the original): its block is
                        // allocated under a counter, which must balance (the block freed, nothing else allocated).
                        difftest::AllocationCounter counter;
                        void *const hb = ::operator new(s.size);
                        std::memcpy(hb, a, s.size);
                        rb = Word(Fastcall<void *>(s.adapter[0], hb, flags)) == Word(hb);
                        counter.Stop();
                        const Freed fb = gFreed;
                        if (fa.calls != 1 || fa.block != ha || fa.size != s.size)
                            ctx.Fail("%s slot 0 round %u: the original freed %u blocks (size %u)", s.name, round, fa.calls, fa.size);
                        if (fb.calls != 0) ctx.Fail("%s slot 0 round %u: ours reached the game's delete", s.name, round);
                        if (counter.Blocks() != 0 || counter.Bytes() != 0)
                            ctx.Fail("%s slot 0 round %u: ours left %lld blocks (%lld bytes) allocated", s.name, round,
                                     counter.Blocks(), counter.Bytes());
                        ::operator delete(ha);
                    } else {
                        ra = Word(Thiscall<void *>(s.own[0], ctx, a, flags)) == Word(a);
                        rb = Word(Fastcall<void *>(s.adapter[0], b, flags)) == Word(b);
                        uint32_t header = Word(GameAddress<void>(kHeaderVTable)), left = 0;
                        std::memcpy(&left, b, 4);
                        if (left != header) ctx.Fail("%s slot 0 round %u: ours left vtable %08x", s.name, round, left);
                    }
                    break;
                }
                case 1:
                case 2:
                    // The copy into the destination (a game object of the class) from `other`.
                    std::memcpy(da, other, s.size);
                    std::memcpy(db, other, s.size);
                    std::memcpy(da, a, 4);
                    std::memcpy(db, a, 4);
                    Thiscall<void>(s.own[slot], ctx, da, static_cast<const void *>(a));
                    rb = Word(Fastcall<void *>(s.adapter[slot], db, static_cast<const void *>(b))) == Word(db);
                    ra = 1;
                    break;
                case 3:
                case 4:
                    ra = Thiscall<uint32_t>(s.own[slot], ctx, a);
                    rb = Fastcall<uint32_t>(s.adapter[slot], b);
                    break;
                case 6:
                    Thiscall<void>(s.own[6], ctx, a, static_cast<void *>(da));
                    rb = Word(Fastcall<void *>(s.adapter[6], b, static_cast<void *>(db))) == Word(db);
                    ra = 1;
                    break;
                }
                if (ra != rb) ctx.Fail("%s slot %u round %u: result %08x / %08x", s.name, slot, round, ra, rb);
                if (std::memcmp(a, b, kRoom) != 0) ctx.Fail("%s slot %u round %u: the object differs", s.name, slot, round);
                if (std::memcmp(da, db, kRoom) != 0) {
                    size_t at = 0;
                    while (da[at] == db[at]) ++at;
                    ctx.Fail("%s slot %u round %u: the destination differs at %zu (%02x / %02x)", s.name, slot, round, at, da[at],
                             db[at]);
                }
                if (!ctx.failures.empty()) return;
            }
        }
    }
}

namespace {

// One of our events built from a game object's fields.
nGZEvent::cIEventBody *Ours(const Spec &s, const uint8_t *f, void *memory)
{
    auto w = [&](size_t at) {
        int32_t v;
        std::memcpy(&v, f + at, 4);
        return v;
    };
    switch (s.size) {
    case 8: return ::new (memory) cCanvasEvent(uint32_t(w(4)));
    case 0x18: return ::new (memory) cCanvasSizeMoveEvent(cCanvasRect{w(4), w(8), w(0xc), w(0x10)}, uint32_t(w(0x14)));
    case 0x20: {
        float lines;
        std::memcpy(&lines, f + 0x1c, 4);
        return ::new (memory) cMouseEvent(uint32_t(w(4)), w(8), w(0xc), uint32_t(w(0x10)), f[0x14] != 0, w(0x18), lines);
    }
    default: return ::new (memory) cKeyEvent(uint32_t(w(4)), w(8), w(0xc), uint32_t(w(0x10)));
    }
}

// The bytes a slot's answer is compared by: the fields (the mouse event's three bytes after its flag are never
// written by the procedure or read by anyone: the copies take the byte alone, 0x1550d0 / 0x155970 `mov al, [esi+0x14];
// mov [ecx+0x14], al`).
std::vector<uint8_t> Fields(const Spec &s, const uint8_t *object)
{
    std::vector<uint8_t> bytes(object + 4, object + s.size);
    if (s.size == 0x20) std::memset(bytes.data() + 0x15 - 4, 0, 3);
    return bytes;
}

} // namespace

// Our classes through our vtables against the game's vtables (all seven slots, the folded ones 0x154ce0, 0x155080,
// 0x1559c0, 0x155a00, 0x154eb0, 0x1550b0, 0x1559e0, 0x1561d0, 0x1559f0, 0x1561e0 included) on the same fields.
DIFF_TEST(CanvasEvents_OurSlots, 0x1576d0)
{
    std::mt19937 gen(0x1576d0);
    for (const Spec &s : kSpecs) {
        for (unsigned round = 0; round < 400; ++round) {
            ++ctx.cases;
            alignas(8) uint8_t game[kRoom], from[kRoom], gameCopy[kRoom], ourCopy[kRoom];
            alignas(8) uint8_t oursMemory[kRoom], oursFrom[kRoom];
            const uint32_t vt = Word(GameAddress<void>(s.vtable));
            std::memset(game, 0, kRoom);
            std::memcpy(game, &vt, 4);
            Seed(gen, s, game);
            std::memset(from, 0, kRoom);
            std::memcpy(from, &vt, 4);
            Seed(gen, s, from);
            nGZEvent::cIEventBody *const ours = Ours(s, game, oursMemory);
            nGZEvent::cIEventBody *const oursSource = Ours(s, from, oursFrom);
            // 3, 4, 5: the type and sizes.
            for (unsigned slot = 3; slot <= 5; ++slot) {
                const uint32_t x = Slot<uint32_t>(game, slot), y = Slot<uint32_t>(ours, slot);
                if (x != y) ctx.Fail("%s slot %u round %u: %08x / %08x", s.name, slot, round, x, y);
            }
            if (Fields(s, game) != Fields(s, reinterpret_cast<uint8_t *>(ours)))
                ctx.Fail("%s round %u: our object does not hold the fields", s.name, round);
            // 6: a copy in caller memory, of the same class (its own vtable) and fields.
            std::memset(gameCopy, 0xcd, kRoom);
            std::memset(ourCopy, 0xcd, kRoom);
            Slot<void>(game, 6, static_cast<void *>(gameCopy));
            void *const made = Slot<void *>(ours, 6, static_cast<void *>(ourCopy));
            if (made != ourCopy) ctx.Fail("%s round %u: our copy is not built in the memory given", s.name, round);
            if (Fields(s, gameCopy) != Fields(s, ourCopy) ||
                std::memcmp(gameCopy + s.size, ourCopy + s.size, kRoom - s.size) != 0)
                ctx.Fail("%s round %u: the copies differ", s.name, round);
            if (std::memcmp(gameCopy, &vt, 4) != 0 || std::memcmp(ourCopy, ours, 4) != 0)
                ctx.Fail("%s round %u: a copy does not have its source's vtable", s.name, round);
            // 1 and 2: the fields taken from another object of the class.
            for (unsigned slot = 1; slot <= 2; ++slot) {
                alignas(8) uint8_t g[kRoom];
                std::memcpy(g, game, kRoom);
                Slot<void>(g, slot, static_cast<const void *>(from));
                Slot<void *>(ours, slot, static_cast<const nGZEvent::cIEventBody *>(oursSource));
                if (Fields(s, g) != Fields(s, reinterpret_cast<uint8_t *>(ours)))
                    ctx.Fail("%s slot %u round %u: the copies differ", s.name, slot, round);
                Ours(s, game, oursMemory); // back to the first fields
            }
            if (!ctx.failures.empty()) return;
        }
    }
}
