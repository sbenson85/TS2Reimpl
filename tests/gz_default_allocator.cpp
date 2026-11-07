// Differential tests for cGZDefaultAllocator in the standalone shape (src/gz/cGZDefaultAllocator.h).
//
// The game has no constructor function for the class (built inline: vtable 0xda3c98, a zero count), so the original
// side's object is those two words on the heap. Our side's object comes from our constructor (`new`), and every call
// goes through its own vtable, in the slot where the game's vtable holds the RVA (checked with SlotOf).
// The global memory functions the slots forward to (RZMemory::Malloc / Free / Realloc, image and ours) are stood in by
// recorders; frees of the object itself are seen by FreeRecorder. Compared: the result, the out-parameter, the count, the
// vtable word (the game's or ours: one code), whether the object was freed, and the recorders' log.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZDefaultAllocator.h"
#include "rz/RZMemory.h"
#include "shape.h"
#include "code_patch.h"

#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kVTableRva = cGZDefaultAllocator::kVTable;
constexpr unsigned kSlots = 7;

std::string gLog;

void Logf(const char *fmt, uint32_t a, uint32_t b = 0)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    gLog += buf;
}

void *__cdecl StandMalloc(uint32_t bytes)
{
    Logf("malloc(%x);", bytes);
    return reinterpret_cast<void *>(0x5000 + bytes);
}
void __cdecl StandFree(void *mem)
{
    Logf("free(%x);", shape::P(mem));
}
void *__cdecl StandRealloc(void *mem, uint32_t bytes)
{
    Logf("realloc(%x,%x);", shape::P(mem), bytes);
    return reinterpret_cast<void *>(0x9000 + bytes);
}

struct MemoryStands {
    shape::StandIn malloc_{0x1be3b0, &RZMemory::Malloc, &StandMalloc};
    shape::StandIn free_{0x1be3a1, &RZMemory::Free, &StandFree};
    shape::StandIn realloc_{0x1be3c1, &RZMemory::Realloc, &StandRealloc};
};

void **OurVTable()
{
    static void **vt = [] {
        cGZDefaultAllocator *o = new cGZDefaultAllocator;
        void **v = shape::VTableAt(o, 0);
        delete o;
        return v;
    }();
    return vt;
}

// A vtable word as a code: 1 for the class's vtable (the game's or ours), the word otherwise.
uint32_t VtCode(uint32_t word)
{
    return word == shape::P(shape::GameVTable(kVTableRva)) || word == shape::P(OurVTable()) ? 1 : word;
}

template <class R, class... A> R CallSlot(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>(shape::VTableAt(object, 0)[slot])(object, args...);
}

// The side's object: the original's two words on the heap, or ours from our constructor.
void *Make(int side, uint32_t count)
{
    if (side == 0) {
        auto *w = static_cast<uint32_t *>(::operator new(8));
        w[0] = shape::P(shape::GameVTable(kVTableRva));
        w[1] = count;
        return w;
    }
    auto *o = new cGZDefaultAllocator;
    o->mRefCount = count;
    return o;
}

uint32_t PickCount(std::mt19937 &rng, int i)
{
    switch (i % 6) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 0xffffffffu;
    case 4: return 0x80000000u;
    default: return rng() % 8;
    }
}

struct Outcome {
    uint32_t result = 0, out = 0, count = 0, vt = 0;
    bool freed = false;
    size_t frees = 0;
    std::string log;
    bool operator==(const Outcome &o) const
    {
        return result == o.result && out == o.out && count == o.count && vt == o.vt && freed == o.freed && frees == o.frees &&
               log == o.log;
    }
};

constexpr uint32_t kUntouched = 0x5a5a5a5a;

// Runs `call(object, &out)` on the side's object; `call` returns the result as a dword (masked by the caller's type).
template <class Call> Outcome Run(int side, uint32_t count, Call call)
{
    void *object = Make(side, count);
    void *out = reinterpret_cast<void *>(kUntouched);
    gLog.clear();
    gLog.reserve(4096); // the recorders append: no reallocation inside the recorded window
    Outcome o;
    {
        difftest::FreeRecorder rec;
        o.result = call(object, &out);
        rec.Stop();
        o.freed = rec.Freed(object);
        o.frees = rec.Count();
    }
    o.out = out == object ? 1 : shape::P(out);
    o.log = gLog;
    if (!o.freed) {
        o.count = shape::Word(object, 4);
        o.vt = VtCode(shape::Word(object, 0));
        ::operator delete(object);
    }
    return o;
}

void Report(difftest::Context &ctx, const char *what, uint32_t a1, uint32_t a2, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a == b) return;
    ctx.Fail("%s (0x%x, 0x%x): original r=%x out=%x count=%x vt=%x freed=%d/%zu log=%s; ours r=%x out=%x count=%x vt=%x "
             "freed=%d/%zu log=%s",
             what, a1, a2, a.result, a.out, a.count, a.vt, a.freed, a.frees, a.log.c_str(), b.result, b.out, b.count, b.vt,
             b.freed, b.frees, b.log.c_str());
}

uint32_t PickIid(std::mt19937 &rng, int i)
{
    switch (i % 5) {
    case 0: return cIGZAllocator::kIid;
    case 1: return 1;
    case 2: return 0;
    case 3: return cIGZAllocator::kIid + (rng() % 2 ? 1 : -1);
    default: return rng();
    }
}

} // namespace

// Every slot through our vtable, against the original at the slot's RVA (the game's vtable).
DIFF_TEST(GZDefaultAllocator_Slots, 0x1be4fa)
{
    MemoryStands stands;
    struct SlotRva {
        const char *name;
        uint32_t rva;
    };
    const SlotRva kRvas[] = {{"QueryInterface", 0x1be4fa}, {"AddRef", 0x148fb0},     {"Release", 0x1be540},
                             {"Allocate", 0x1be4c7},       {"Deallocate", 0x1be4d4}, {"Reallocate", 0x1be4e8},
                             {"DeletingDestructor", 0x1be557}};
    unsigned slot[7];
    for (unsigned s = 0; s < 7; ++s) {
        slot[s] = shape::SlotOf(kVTableRva, kSlots, kRvas[s].rva);
        if (slot[s] == ~0u) {
            ctx.Fail("%s 0x%x not in the game's vtable", kRvas[s].name, kRvas[s].rva);
            return;
        }
    }
    std::mt19937 rng(0x48560001);
    for (int i = 0; i < 600; ++i) {
        uint32_t count = PickCount(rng, i);
        // QueryInterface (al): the out-parameter is always given (the original writes it unchecked).
        uint32_t riid = PickIid(rng, i);
        auto qi = [&](int side) {
            return Run(side, count, [&](void *o, void **out) {
                uint32_t r = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x1be4fa)(o, riid, out)
                                       : CallSlot<bool>(o, slot[0], riid, out);
                return r & 0xff;
            });
        };
        Report(ctx, "QueryInterface", riid, count, qi(0), qi(1));
        auto addRef = [&](int side) {
            return Run(side, count, [&](void *o, void **) {
                return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x148fb0)(o) : CallSlot<uint32_t>(o, slot[1]);
            });
        };
        Report(ctx, "AddRef", count, 0, addRef(0), addRef(1));
        auto release = [&](int side) {
            return Run(side, count, [&](void *o, void **) {
                return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x1be540)(o) : CallSlot<uint32_t>(o, slot[2]);
            });
        };
        Report(ctx, "Release", count, 0, release(0), release(1));
        uint32_t bytes = i % 7 == 0 ? 0 : rng() % 0x10000;
        auto allocate = [&](int side) {
            return Run(side, count, [&](void *o, void **) {
                void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1be4c7)(o, bytes)
                                    : CallSlot<void *>(o, slot[3], bytes);
                return shape::P(r);
            });
        };
        Report(ctx, "Allocate", bytes, count, allocate(0), allocate(1));
        uint32_t mem = i % 3 == 0 ? 0 : rng();
        auto deallocate = [&](int side) {
            return Run(side, count, [&](void *o, void **) {
                if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x1be4d4)(o, reinterpret_cast<void *>(mem));
                else CallSlot<void>(o, slot[4], reinterpret_cast<void *>(mem));
                return 0u;
            });
        };
        Report(ctx, "Deallocate", mem, count, deallocate(0), deallocate(1));
        auto reallocate = [&](int side) {
            return Run(side, count, [&](void *o, void **) {
                void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x1be4e8)(o, reinterpret_cast<void *>(mem), bytes)
                                    : CallSlot<void *>(o, slot[5], reinterpret_cast<void *>(mem), bytes);
                return shape::P(r);
            });
        };
        Report(ctx, "Reallocate", mem, bytes, reallocate(0), reallocate(1));
        // Flags: bit 0 frees; the other bits are ignored.
        uint32_t flags = i % 4 == 3 ? rng() : i % 2;
        auto destroy = [&](int side) {
            return Run(side, count, [&](void *o, void **) {
                void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1be557)(o, flags)
                                    : CallSlot<void *>(o, slot[6], flags);
                return r == o ? 1u : shape::P(r);
            });
        };
        Report(ctx, "DeletingDestructor", flags, count, destroy(0), destroy(1));
    }
}

namespace {

int gTrapHits = 0;

// A game-built object (the game's vtable) whose vtable is a copy with an arity-matched trap in the adapters' own slots
// (0 and 6): an adapter that dispatched virtually would land there (in game: its own hook, an endless loop).
void *gTrapVTable[kSlots];

void *MakeTrapped(uint32_t count)
{
    void **game = shape::GameVTable(kVTableRva);
    for (unsigned s = 0; s < kSlots; ++s) gTrapVTable[s] = game[s];
    gTrapVTable[0] = shape::TrapFor<shape::Count<gTrapHits>>(2);
    gTrapVTable[6] = shape::TrapFor<shape::Count<gTrapHits>>(1);
    auto *w = static_cast<uint32_t *>(::operator new(8));
    w[0] = shape::P(gTrapVTable);
    w[1] = count;
    return w;
}

} // namespace

// The hooks' adapters on game-built objects, against the original at the RVA.
DIFF_TEST(GZDefaultAllocator_Adapters, 0x1be557)
{
    std::mt19937 rng(0x48560002);
    for (int i = 0; i < 400; ++i) {
        uint32_t count = PickCount(rng, i);
        uint32_t riid = PickIid(rng, i);
        uint32_t flags = i % 2;
        for (int which = 0; which < 2; ++which) {
            Outcome o[2];
            for (int side = 0; side < 2; ++side) {
                void *object = MakeTrapped(count);
                void *out = reinterpret_cast<void *>(kUntouched);
                gTrapHits = 0;
                difftest::FreeRecorder rec;
                uint32_t r;
                if (which == 0) {
                    r = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x1be4fa)(object, riid, &out) & 0xff
                                  : cGZDefaultAllocatorAdapters::QueryInterface(static_cast<cGZDefaultAllocator *>(object), nullptr, riid, &out);
                } else {
                    void *p = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1be557)(object, flags)
                                        : cGZDefaultAllocatorAdapters::DeletingDestructor(static_cast<cGZDefaultAllocator *>(object), nullptr, flags);
                    r = p == object ? 1u : shape::P(p);
                }
                rec.Stop();
                o[side].result = r;
                o[side].out = out == object ? 1 : shape::P(out);
                o[side].freed = rec.Freed(object);
                o[side].frees = rec.Count();
                o[side].log = gTrapHits ? "trap" : "";
                if (!o[side].freed) {
                    uint32_t vt = shape::Word(object, 0);
                    o[side].vt = vt == shape::P(gTrapVTable) ? 2 : VtCode(vt);
                    o[side].count = shape::Word(object, 4);
                    ::operator delete(object);
                }
            }
            // The destructor stores the class's vtable (the game's in the original, ours in the adapter): one code (VtCode).
            Report(ctx, which == 0 ? "adapter QueryInterface" : "adapter DeletingDestructor", which == 0 ? riid : flags, count, o[0], o[1]);
        }
    }
}
