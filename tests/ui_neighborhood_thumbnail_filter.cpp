// Differential test for src/ui/cNeighborhoodThumbnailFilter.* (standalone shape): the constructor 0x725594, every slot
// (AddKey 0x725957, IsKeyIncluded 0x72baaf, the deleting destructor 0x7258e3, and the folded QueryInterface / AddRef /
// Release), under random scripts.
//
// The original side's object comes from the constructor 0x725594 (the game's vtable), ours from the constructor hook's
// adapter (our vtable); every call goes through the object's own vtable slot. The vtable word is the only one
// normalised; the refcount, results, out-parameters and the set (its keys in order, from the head node) are compared
// after every call. The game's operator new / sized delete (0x824632 / 0x824624) stand in for ours; the deletes note the
// size the object was freed with.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "shape.h"
#include "ui/cNeighborhoodThumbnailFilter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>

namespace {

constexpr uint32_t kConstruct = 0x725594;
constexpr uint32_t kGameVTable = 0xdf39d4;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kGameSizedDelete = 0x824624;
constexpr uint32_t kOutSentinel = 0x5eed5eed;

constexpr uint32_t kSlotQueryInterface = 0;
constexpr uint32_t kSlotAddRef = 1;
constexpr uint32_t kSlotRelease = 2;
constexpr uint32_t kSlotIsKeyIncluded = 3;
constexpr uint32_t kSlotDestroy = 4;
constexpr uint32_t kSlotAddKey = 5;

// ---- the heap ----

void *gWatched = nullptr;
size_t gFreedSize = 0;
DWORD gWatchThread = 0;

void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}

void __cdecl NoteSizedDelete(void *p, size_t n)
{
    if (p != nullptr && p == gWatched && GetCurrentThreadId() == gWatchThread) {
        gFreedSize = n;
        gWatched = nullptr;
    }
    std::free(p);
}

struct Heap {
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{kGameSizedDelete, reinterpret_cast<const void *>(&NoteSizedDelete)};
    shape::CodePatch oursDelete{reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&::operator delete)),
                                reinterpret_cast<const void *>(&NoteSizedDelete)};
    Heap() { gWatchThread = GetCurrentThreadId(); }
    ~Heap() { gWatched = nullptr; }
    Heap(const Heap &) = delete;
    Heap &operator=(const Heap &) = delete;
};

// ---- the sides ----

void *OurVTable()
{
    static void *table = nullptr;
    if (table == nullptr) {
        alignas(8) static uint8_t memory[cNeighborhoodThumbnailFilter::kSize];
        auto *filter = ::new (memory) cNeighborhoodThumbnailFilter;
        table = shape::VTableAt(memory, 0);
        filter->~cNeighborhoodThumbnailFilter();
    }
    return table;
}

void *Build(difftest::Context &ctx, bool ours)
{
    void *memory = ::operator new(cNeighborhoodThumbnailFilter::kSize);
    std::memset(memory, 0xcd, cNeighborhoodThumbnailFilter::kSize);
    if (ours) return cNeighborhoodThumbnailFilterAdapters::Construct(memory, nullptr);
    return ctx.Original<void *(__thiscall *)(void *)>(kConstruct)(memory);
}

template <class Fn> Fn Slot(void *object, uint32_t slot)
{
    return reinterpret_cast<Fn>(shape::VTableAt(object, 0)[slot]);
}

std::string Dump(void *object, bool ours)
{
    const auto *filter = static_cast<const cNeighborhoodThumbnailFilter *>(object);
    const uint32_t own = shape::P(ours ? OurVTable() : shape::GameVTable(kGameVTable));
    char line[96];
    std::snprintf(line, sizeof line, "vt %s ref %u size %u:", shape::Word(object, 0) == own ? "own" : "OTHER",
                  filter->mRefCount, static_cast<uint32_t>(filter->mKeys.size()));
    std::string out = line;
    for (const auto &key : filter->mKeys) {
        std::snprintf(line, sizeof line, " %x/%x/%x/%x", key.type, key.group, key.instance, key.resource);
        out += line;
    }
    return out;
}

// ---- the script ----

const uint32_t kWords[] = {0, 1, 2, 0xffffffff, 0x80000000};
const uint32_t kIids[] = {1, cNeighborhoodThumbnailFilter::kInterfaceId, 0, 0x8865cd88, 0xa56c8cfd};

enum Op { kOpAdd, kOpIncluded, kOpQuery, kOpAddRef, kOpRelease, kOps };

struct Outcome {
    uint32_t result = 0, out = 0;
    std::string state;
};

Outcome Run(void *filter, bool ours, Op op, const uint32_t *key, uint32_t iid)
{
    Outcome o;
    switch (op) {
    case kOpAdd:
        Slot<void(__thiscall *)(void *, const uint32_t *)>(filter, kSlotAddKey)(filter, key);
        break;
    case kOpIncluded:
        o.result = Slot<uint8_t(__thiscall *)(void *, const uint32_t *)>(filter, kSlotIsKeyIncluded)(filter, key);
        break;
    case kOpQuery: {
        void *out = reinterpret_cast<void *>(static_cast<uintptr_t>(kOutSentinel));
        o.result = Slot<uint8_t(__thiscall *)(void *, uint32_t, void **)>(filter, kSlotQueryInterface)(filter, iid, &out);
        o.out = out == filter ? 0x5e1f : shape::P(out);
        break;
    }
    case kOpAddRef:
        o.result = Slot<uint32_t(__thiscall *)(void *)>(filter, kSlotAddRef)(filter);
        break;
    case kOpRelease:
        o.result = Slot<uint32_t(__thiscall *)(void *)>(filter, kSlotRelease)(filter);
        break;
    default:
        break;
    }
    o.state = Dump(filter, ours);
    return o;
}

// The end: 0 the deleting destructor with flags 1, 1 with flags 0 (then freed by hand), 2 Release at count 0 or 1.
struct End {
    uint32_t returned = 0;
    size_t freed = 0;
};

End Finish(void *filter, int kind, uint32_t count)
{
    End e;
    gWatched = filter;
    gFreedSize = 0;
    if (kind == 2) {
        static_cast<cNeighborhoodThumbnailFilter *>(filter)->mRefCount = count;
        e.returned = Slot<uint32_t(__thiscall *)(void *)>(filter, kSlotRelease)(filter);
    } else {
        e.returned = Slot<void *(__thiscall *)(void *, uint32_t)>(filter, kSlotDestroy)(filter, kind == 0 ? 1 : 0) == filter
                         ? 0x5e1f
                         : 1;
    }
    e.freed = gFreedSize;
    gWatched = nullptr;
    if (kind == 1) ::operator delete(filter);
    return e;
}

} // namespace

DIFF_TEST(NeighborhoodThumbnailFilter_Script, 0x725957)
{
    Heap heap;
    std::mt19937 r(0x7258);
    for (int c = 0; c < 1500 && ctx.failures.empty(); ++c) {
        ++ctx.cases;
        void *sides[2] = {Build(ctx, false), Build(ctx, true)};
        if (Dump(sides[0], false) != Dump(sides[1], true))
            ctx.Fail("case %d: constructed\n  %s\n  %s", c, Dump(sides[0], false).c_str(), Dump(sides[1], true).c_str());
        const int steps = static_cast<int>(r() % 40);
        for (int k = 0; k < steps && ctx.failures.empty(); ++k) {
            Op op = static_cast<Op>(r() % kOps);
            uint32_t key[4];
            for (uint32_t &word : key) word = kWords[r() % (r() % 4 == 0 ? 5 : 3)];
            const uint32_t iid = kIids[r() % 5];
            if (op == kOpRelease && static_cast<cNeighborhoodThumbnailFilter *>(sides[0])->mRefCount <= 1) op = kOpAddRef;
            const Outcome a = Run(sides[0], false, op, key, iid);
            const Outcome b = Run(sides[1], true, op, key, iid);
            if (a.result != b.result || a.out != b.out)
                ctx.Fail("case %d step %d op %d: original %08x %08x, ours %08x %08x", c, k, op, a.result, a.out, b.result,
                         b.out);
            if (a.state != b.state) ctx.Fail("case %d step %d op %d: state\n  %s\n  %s", c, k, op, a.state.c_str(), b.state.c_str());
        }
        const int kind = static_cast<int>(r() % 3);
        const uint32_t count = r() % 2;
        const End a = Finish(sides[0], kind, count);
        const End b = Finish(sides[1], kind, count);
        if (a.returned != b.returned || a.freed != b.freed)
            ctx.Fail("case %d end %d: original %08x freed 0x%zx, ours %08x freed 0x%zx", c, kind, a.returned, a.freed,
                     b.returned, b.freed);
    }
}
