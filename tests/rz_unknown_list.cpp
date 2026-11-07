// Differential tests for src/rz/cRZUnknownList.*: the list of counted objects (cIGZUnknownList, vtable 0xda3008, 19 slots).
// The original side is an object built by the original constructor 0x1b905f and driven through the game's vtable; ours is built
// by our constructor adapter and driven through our own vtable, slot by slot (a slot-order mistake calls the wrong function).
//
// Slots covered by a test registered under another slot's RVA:
//   cRZUnknownList_Slots (0x1b9166): QueryInterface 0x1b9477, AddRef 0x15e520, Release 0x1b9500, Size 0x46390, GetItem 0x1b91e2,
//     ForEach 0x1b9166 (and its twin, slot 17), PrependAll 0x1b9369, PushFront 0x1b9424, AppendAll 0x1b92bc, PushBack 0x1b9333,
//     GetFront 0x1b922f, GetBack 0x1b9217, PopFront 0x1b9282, PopBack 0x1b9246, Clear 0x1b915b, Assign 0x1b9196, Clone 0x1b91ad,
//     the deleting destructor 0x1b910f;
//   cRZUnknownList_Adapters (0x1b905f): the constructor 0x1b905f, the destructor 0x1b909d, the direct PushBack 0x1b9333.
//
// The elements are fakes (five per side: the test holds one count each, even ones answer interface 0x55 too) that log every
// QueryInterface, AddRef and Release, so the order of the calls and the counts are compared as well as the results. The list's own
// state is read from its words (size, count, the node ring forward and back, the element in each node), not through a slot.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/cRZUnknownList.h"
#include "shape.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0x1b905f;
constexpr uint32_t kDtor = 0x1b909d;
constexpr uint32_t kPushBack = 0x1b9333;
constexpr uint32_t kGameVTable = 0xda3008;
constexpr uint32_t kSpecialIid = 0x55;
constexpr unsigned kSlots = 19;
constexpr unsigned kPool = 5;
constexpr uint32_t kUntouched = 0xface0000u;

// The elements' and fakes' traffic. Reserved up front: a regrowth inside a FreeRecorder window would count as a free of the call.
std::vector<std::string> gLog = [] {
    std::vector<std::string> v;
    v.reserve(4096);
    return v;
}();

std::string Fmt(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

struct Element : cIGZUnknown {
    int id = 0;
    bool special = false;
    uint32_t refs = 1; // the test's own hold
    bool QueryInterface(uint32_t iid, void **out) override
    {
        const bool ok = iid == 1 || (iid == kSpecialIid && special);
        gLog.push_back(Fmt("e%d.QI 0x%x %s", id, iid, ok ? "yes" : "no"));
        if (ok) {
            *out = this;
            AddRef();
        }
        return ok;
    }
    uint32_t AddRef() override
    {
        gLog.push_back(Fmt("e%d.AddRef -> %u", id, refs + 1));
        return ++refs;
    }
    uint32_t Release() override
    {
        gLog.push_back(Fmt("e%d.Release -> %u", id, refs - 1));
        return --refs;
    }
};

struct Pool {
    std::array<Element, kPool> e;
    Pool()
    {
        for (unsigned i = 0; i < kPool; ++i) {
            e[i].id = int(i);
            e[i].special = i % 2 == 0;
        }
    }
    std::string IdOf(const void *p) const
    {
        for (unsigned i = 0; i < kPool; ++i)
            if (p == static_cast<const cIGZUnknown *>(&e[i])) return Fmt("e%u", i);
        return p == nullptr ? "null" : "other";
    }
    std::string Refs() const
    {
        std::string s;
        for (const Element &x : e) s += Fmt("%u ", x.refs);
        return s;
    }
};

// The "other" list the bulk operations read: scripted elements, some indices failing their query.
struct FakeList : cIGZUnknownList {
    std::vector<Element *> items;
    std::vector<bool> fails;
    bool QueryInterface(uint32_t, void **) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    uint32_t Size() const override
    {
        gLog.push_back(Fmt("other.Size -> %zu", items.size()));
        return uint32_t(items.size());
    }
    bool GetItem(uint32_t index, uint32_t iid, void **out) override
    {
        gLog.push_back(Fmt("other.GetItem %u 0x%x", index, iid));
        if (index >= items.size() || fails[index]) return false;
        return items[index]->QueryInterface(iid, out);
    }
    bool ForEach(Visitor *, uint32_t, uint32_t) override { return false; }
    bool PrependAll(cIGZUnknownList *) override { return false; }
    bool PushFront(cIGZUnknown *) override { return false; }
    bool AppendAll(cIGZUnknownList *) override { return false; }
    bool PushBack(cIGZUnknown *) override { return false; }
    bool GetFront(uint32_t, void **) override { return false; }
    bool GetBack(uint32_t, void **) override { return false; }
    bool PopFront(uint32_t, void **) override { return false; }
    bool PopBack(uint32_t, void **) override { return false; }
    bool Clear() override { return false; }
    bool Assign(cIGZUnknownList *) override { return false; }
    bool Clone(cIGZUnknownList **) override { return false; }
    bool ForEachAgain(Visitor *, uint32_t, uint32_t) override { return false; }
};

struct Visitor : cIGZUnknownList::Visitor {
    const Pool *pool = nullptr;
    void vf0() override {}
    void vf1() override {}
    void vf2() override {}
    void Visit(cIGZUnknown *element, uint32_t a, uint32_t b) override
    {
        gLog.push_back(Fmt("visit %s 0x%x 0x%x", pool->IdOf(element).c_str(), a, b));
    }
};

template <class Fn> Fn Slot(void *object, unsigned slot) { return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot]); }

using WordFn = uint32_t(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using GetItemFn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **);
using IidOutFn = bool(__thiscall *)(void *, uint32_t, void **);
using ItemFn = bool(__thiscall *)(void *, cIGZUnknown *);
using ListFn = bool(__thiscall *)(void *, cIGZUnknownList *);
using CloneFn = bool(__thiscall *)(void *, cIGZUnknownList **);
using EachFn = bool(__thiscall *)(void *, cIGZUnknownList::Visitor *, uint32_t, uint32_t);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);

uint32_t Word(const void *object, uint32_t offset) { return shape::Word(object, offset); }

// The list's words, apart from the vtable word and the heap addresses: size, count, the elements in order, and whether the ring's
// back links agree with its forward ones.
std::string StateOf(const void *list, const Pool &pool)
{
    const uint8_t *head = reinterpret_cast<const uint8_t *>(uintptr_t(Word(list, 4)));
    const uint32_t size = Word(list, 8);
    std::string elements;
    bool linksOk = true;
    const uint8_t *prev = head;
    const uint8_t *node = reinterpret_cast<const uint8_t *>(uintptr_t(Word(head, 0)));
    uint32_t walked = 0;
    while (node != head && walked < 1000) {
        if (reinterpret_cast<const uint8_t *>(uintptr_t(Word(node, 4))) != prev) linksOk = false;
        elements += pool.IdOf(reinterpret_cast<const void *>(uintptr_t(Word(node, 8)))) + " ";
        prev = node;
        node = reinterpret_cast<const uint8_t *>(uintptr_t(Word(node, 0)));
        ++walked;
    }
    if (reinterpret_cast<const uint8_t *>(uintptr_t(Word(head, 4))) != prev) linksOk = false;
    return Fmt("size %u walked %u refs %u [%s] links %s", size, walked, Word(list, 0xc), elements.c_str(), linksOk ? "ok" : "BAD");
}

constexpr uint32_t kOffsets[1] = {0};

void *NewOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cRZUnknownList::kSize);
    std::memset(memory, 0xcd, cRZUnknownList::kSize);
    return ctx.Original<void *(__thiscall *)(void *)>(kCtor)(memory);
}
void *NewOurs()
{
    void *memory = ::operator new(cRZUnknownList::kSize);
    std::memset(memory, 0xcd, cRZUnknownList::kSize);
    return cRZUnknownListAdapters::Construct(memory, nullptr);
}

// One side: its elements and its list.
struct Side {
    Pool pool;
    void *list = nullptr;
    bool ours = false;
};

enum Kind {
    kPushBack_, kPushFront_, kGetItem, kGetFront, kGetBack, kPopFront, kPopBack, kClear, kAssign, kPrependAll, kAppendAll, kClone,
    kForEach, kForEachAgain, kQueryInterface, kAddRef, kSize, kKinds
};

struct Op {
    Kind kind = kSize;
    unsigned element = 0;
    uint32_t index = 0;
    uint32_t iid = 1;
    std::vector<unsigned> items; // the other list's elements
    std::vector<bool> fails;
    uint32_t a = 0, b = 0;
};

Op RandomOp(std::mt19937 &rng)
{
    static const Kind weighted[] = {kPushBack_, kPushBack_, kPushBack_, kPushFront_, kPushFront_, kGetItem, kGetItem, kGetFront,
                                    kGetBack, kPopFront, kPopFront, kPopBack, kPopBack, kClear, kAssign, kPrependAll,
                                    kPrependAll, kAppendAll, kAppendAll, kClone, kForEach, kForEachAgain, kQueryInterface,
                                    kAddRef, kSize};
    Op op;
    op.kind = weighted[rng() % (sizeof(weighted) / sizeof(weighted[0]))];
    op.element = rng() % kPool;
    op.index = rng() % 8;
    static const uint32_t iids[] = {1, 1, kSpecialIid, kSpecialIid, 0x12e, 0, 2, 0x7fffffffu, 0xffffffffu};
    op.iid = iids[rng() % (sizeof(iids) / sizeof(iids[0]))];
    const unsigned n = rng() % 5;
    for (unsigned i = 0; i < n; ++i) {
        op.items.push_back(rng() % kPool);
        op.fails.push_back(rng() % 5 == 0);
    }
    op.a = rng();
    op.b = rng();
    return op;
}

// Runs one operation on one side; the result and the element traffic as text.
std::string Run(Side &s, const Op &op)
{
    gLog.clear();
    void *list = s.list;
    void *out = reinterpret_cast<void *>(uintptr_t(kUntouched));
    std::string result;
    FakeList other;
    for (unsigned i = 0; i < op.items.size(); ++i) {
        other.items.push_back(&s.pool.e[op.items[i]]);
        other.fails.push_back(op.fails[i]);
    }
    Visitor visitor;
    visitor.pool = &s.pool;
    auto OutText = [&]() -> std::string {
        if (out == reinterpret_cast<void *>(uintptr_t(kUntouched))) return "untouched";
        if (out == list) return "self";
        return s.pool.IdOf(out);
    };
    switch (op.kind) {
    case kPushBack_: result = Fmt("%d", Slot<ItemFn>(list, 9)(list, &s.pool.e[op.element])); break;
    case kPushFront_: result = Fmt("%d", Slot<ItemFn>(list, 7)(list, &s.pool.e[op.element])); break;
    case kGetItem: result = Fmt("%d", Slot<GetItemFn>(list, 4)(list, op.index, op.iid, &out)) + " " + OutText(); break;
    case kGetFront: result = Fmt("%d", Slot<IidOutFn>(list, 10)(list, op.iid, &out)) + " " + OutText(); break;
    case kGetBack: result = Fmt("%d", Slot<IidOutFn>(list, 11)(list, op.iid, &out)) + " " + OutText(); break;
    case kPopFront: result = Fmt("%d", Slot<IidOutFn>(list, 12)(list, op.iid, &out)) + " " + OutText(); break;
    case kPopBack: result = Fmt("%d", Slot<IidOutFn>(list, 13)(list, op.iid, &out)) + " " + OutText(); break;
    case kClear: result = Fmt("%d", Slot<BoolFn>(list, 14)(list)); break;
    case kAssign: result = Fmt("%d", Slot<ListFn>(list, 15)(list, &other)); break;
    case kPrependAll: result = Fmt("%d", Slot<ListFn>(list, 6)(list, &other)); break;
    case kAppendAll: result = Fmt("%d", Slot<ListFn>(list, 8)(list, &other)); break;
    case kClone: {
        cIGZUnknownList *copy = nullptr;
        const bool r = Slot<CloneFn>(list, 16)(list, reinterpret_cast<cIGZUnknownList **>(&copy));
        result = Fmt("%d copy: ", r);
        if (copy) {
            result += StateOf(copy, s.pool);
            const uint32_t released = Slot<WordFn>(copy, 2)(copy); // the copy's own count: 1, so this deletes it
            result += Fmt(" released %u", released);
        }
        break;
    }
    case kForEach: result = Fmt("%d", Slot<EachFn>(list, 5)(list, &visitor, op.a, op.b)); break;
    case kForEachAgain: result = Fmt("%d", Slot<EachFn>(list, 17)(list, &visitor, op.a, op.b)); break;
    case kQueryInterface: result = Fmt("%d", Slot<IidOutFn>(list, 0)(list, op.iid, &out)) + " " + OutText(); break;
    case kAddRef: result = Fmt("%u", Slot<WordFn>(list, 1)(list)); break;
    case kSize: result = Fmt("%u", Slot<WordFn>(list, 3)(list)); break;
    case kKinds: break;
    }
    result += " | ";
    for (const std::string &line : gLog) result += line + "; ";
    return result + "| " + StateOf(list, s.pool) + " | " + s.pool.Refs();
}

} // namespace

DIFF_TEST(cRZUnknownList_Slots, 0x1b9166)
{
    // The constructor's table word on both sides: the game's vtable for the original's, our class's for ours.
    {
        Side a, b;
        a.list = NewOriginal(ctx);
        b.list = NewOurs();
        b.ours = true;
        ++ctx.cases;
        if (shape::P(*static_cast<void **>(a.list)) != shape::P(shape::GameVTable(kGameVTable)))
            ctx.Fail("the original's constructor stores 0x%x, not the vtable 0xda3008", Word(a.list, 0));
        ++ctx.cases;
        if (shape::P(*static_cast<void **>(b.list)) != shape::P(shape::Ours<cRZUnknownList>(kOffsets).face[0]))
            ctx.Fail("our constructor stores 0x%x, not our class's table", Word(b.list, 0));
        ++ctx.cases;
        if (StateOf(a.list, a.pool) != StateOf(b.list, b.pool) || Word(a.list, 0xc) != 0 || Word(b.list, 8) != 0)
            ctx.Fail("fresh lists differ: [%s] / [%s]", StateOf(a.list, a.pool).c_str(), StateOf(b.list, b.pool).c_str());
        ::operator delete(a.list);
        ::operator delete(b.list);
    }

    std::mt19937 rng(0x1b9166);
    for (int sequence = 0; sequence < 600 && ctx.failures.size() < 10; ++sequence) {
        Side a, b;
        a.list = NewOriginal(ctx);
        b.list = NewOurs();
        b.ours = true;
        // The owner's count: the list is released at the end through slot 2.
        Slot<WordFn>(a.list, 1)(a.list);
        Slot<WordFn>(b.list, 1)(b.list);
        const int steps = 1 + int(rng() % 30);
        bool differed = false;
        for (int step = 0; step < steps && !differed; ++step) {
            const Op op = RandomOp(rng);
            const std::string ra = Run(a, op);
            const std::string rb = Run(b, op);
            ++ctx.cases;
            if (ra != rb) {
            {
                size_t at = 0;
                while (at < ra.size() && at < rb.size() && ra[at] == rb[at]) ++at;
                const size_t from = at > 150 ? at - 150 : 0;
                ctx.Fail("sequence %d step %d kind %d element %u index %u iid 0x%x: differ at %zu: original [...%s] / ours [...%s]",
                         sequence, step, int(op.kind), op.element, op.index, op.iid, at, ra.substr(from, 300).c_str(),
                         rb.substr(from, 300).c_str());
                differed = true;
            }
            }
        }
        // The end: the last Release deletes the list (its elements released), or the deleting destructor directly.
        const int ending = int(rng() % 3);
        std::string endings[2];
        bool freed[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            Side &s = side == 0 ? a : b;
            gLog.clear();
            difftest::FreeRecorder rec;
            std::string r;
            if (ending == 0) {
                const uint32_t one = 1;
                std::memcpy(static_cast<uint8_t *>(s.list) + 0xc, &one, 4);
                r =Fmt("release %u", Slot<WordFn>(s.list, 2)(s.list));
            } else if (ending == 1) {
                const void *back = Slot<DeletingFn>(s.list, 18)(s.list, 1);
                r = Fmt("deleting %d", back == s.list);
            } else {
                const uint32_t three = 3;
                std::memcpy(static_cast<uint8_t *>(s.list) + 0xc, &three, 4);
                const uint32_t first = Slot<WordFn>(s.list, 2)(s.list);
                const uint32_t second = Slot<WordFn>(s.list, 2)(s.list);
                const uint32_t third = Slot<WordFn>(s.list, 2)(s.list);
                r = Fmt("release x3 %u %u %u", first, second, third);
            }
            rec.Stop();
            freed[side] = rec.Freed(s.list);
            frees[side] = rec.Count();
            for (const std::string &line : gLog) r += "; " + line;
            endings[side] = r + " | " + s.pool.Refs();
        }
        ++ctx.cases;
        if (differed) continue;
        if (endings[0] != endings[1] || freed[0] != freed[1] || frees[0] != frees[1] || !freed[0])
            ctx.Fail("sequence %d ending %d: original [%s] freed %d (%zu frees) / ours [%s] freed %d (%zu frees)", sequence, ending,
                     endings[0].c_str(), freed[0], frees[0], endings[1].c_str(), freed[1], frees[1]);
    }

    // Release over counts: 0 and 1 delete, higher counts decrement, and the extremes.
    for (uint32_t refs : {0u, 1u, 2u, 3u, 0x80000000u, 0xffffffffu}) {
        std::string result[2];
        bool freed[2];
        for (int side = 0; side < 2; ++side) {
            Side s;
            s.list = side == 0 ? NewOriginal(ctx) : NewOurs();
            Slot<ItemFn>(s.list, 9)(s.list, &s.pool.e[0]);
            Slot<ItemFn>(s.list, 9)(s.list, &s.pool.e[1]);
            std::memcpy(static_cast<uint8_t *>(s.list) + 0xc, &refs, 4);
            gLog.clear();
            difftest::FreeRecorder rec;
            result[side] = Fmt("%u", Slot<WordFn>(s.list, 2)(s.list));
            rec.Stop();
            freed[side] = rec.Freed(s.list);
            if (!freed[side]) {
                result[side] += " " + StateOf(s.list, s.pool);
                // The survivor is released down to its end: count 1, then Release deletes it (elements released).
                const uint32_t one = 1;
                std::memcpy(static_cast<uint8_t *>(s.list) + 0xc, &one, 4);
                Slot<WordFn>(s.list, 2)(s.list);
            }
            result[side] += " | " + s.pool.Refs();
        }
        ++ctx.cases;
        if (result[0] != result[1] || freed[0] != freed[1])
            ctx.Fail("Release at count 0x%x: original [%s] freed %d / ours [%s] freed %d", refs, result[0].c_str(), freed[0],
                     result[1].c_str(), freed[1]);
    }

    // QueryInterface over interface ids: 1 and 0x12e answer, everything else does not (out untouched).
    std::vector<uint32_t> iids = {0, 1, 2, 0x12e, 0x12f, 0x12d, 0x1000012e};
    for (int bit = 0; bit < 32; ++bit) {
        iids.push_back(0x12eu ^ (1u << bit));
        iids.push_back(1u ^ (1u << bit));
    }
    for (int i = 0; i < 40; ++i) iids.push_back(rng());
    for (uint32_t iid : iids) {
        std::string result[2];
        for (int side = 0; side < 2; ++side) {
            Side s;
            s.list = side == 0 ? NewOriginal(ctx) : NewOurs();
            Op op;
            op.kind = kQueryInterface;
            op.iid = iid;
            result[side] = Run(s, op);
            const uint32_t one = 1; // a successful query added one count; the list is deleted through Release at count 1
            std::memcpy(static_cast<uint8_t *>(s.list) + 0xc, &one, 4);
            Slot<WordFn>(s.list, 2)(s.list);
        }
        ++ctx.cases;
        if (result[0] != result[1]) ctx.Fail("QueryInterface 0x%x: [%s] / [%s]", iid, result[0].c_str(), result[1].c_str());
    }
}

// ---- the adapters (the hooks: constructor, destructor, direct PushBack) --------------------------------------------------

namespace {

uint32_t gTrapHits = 0;

// Words each slot's `ret` pops, for arity-matched traps.
constexpr unsigned kSlotWords[kSlots] = {2, 0, 0, 0, 3, 3, 1, 1, 1, 1, 2, 2, 2, 2, 0, 1, 1, 3, 1};

uint32_t TrapHit(void *, unsigned) { return ++gTrapHits; }

// A table of traps for the object's vtable word: a virtual dispatch on `this` from an adapter lands here (in the game it would
// be the hook of that slot, an endless loop).
void **TrapTable()
{
    static void *table[kSlots];
    static bool done = false;
    if (!done) {
        for (unsigned s = 0; s < kSlots; ++s) table[s] = shape::TrapFor<&TrapHit>(kSlotWords[s]);
        done = true;
    }
    return table;
}

constexpr uint32_t kGameRva[1] = {kGameVTable};

// What the +0 word is, as a code: the game's table, ours, or the word as it is.
uint32_t VtWord(const void *object)
{
    return shape::VtCode(Word(object, 0), 1, shape::GameTables{kGameRva}, shape::Ours<cRZUnknownList>(kOffsets).face);
}

} // namespace

DIFF_TEST(cRZUnknownList_Adapters, 0x1b905f)
{
    // Constructor: the same words (apart from the table, which is ours), the memory beyond the object untouched.
    {
        uint8_t ma[cRZUnknownList::kSize + 8], mb[cRZUnknownList::kSize + 8];
        std::memset(ma, 0xcd, sizeof(ma));
        std::memset(mb, 0xcd, sizeof(mb));
        void *a = ctx.Original<void *(__thiscall *)(void *)>(kCtor)(ma);
        void *b = cRZUnknownListAdapters::Construct(mb, nullptr);
        Pool pool;
        ++ctx.cases;
        if (a != ma || b != mb) ctx.Fail("the constructors return %p / %p for %p / %p", a, b, static_cast<void *>(ma), static_cast<void *>(mb));
        if (StateOf(ma, pool) != StateOf(mb, pool))
            ctx.Fail("constructed lists differ: [%s] / [%s]", StateOf(ma, pool).c_str(), StateOf(mb, pool).c_str());
        if (VtWord(ma) != shape::kFaceCode || VtWord(mb) != shape::kFaceCode)
            ctx.Fail("constructor table words: 0x%x (original) / 0x%x (ours)", Word(ma, 0), Word(mb, 0));
        for (unsigned i = cRZUnknownList::kSize; i < sizeof(ma); ++i)
            if (ma[i] != 0xcd || mb[i] != 0xcd) ctx.Fail("a constructor wrote past the object (byte %u)", i);
        ctx.Original<void(__thiscall *)(void *)>(kDtor)(ma);
        cRZUnknownListAdapters::Destruct(static_cast<cRZUnknownList *>(static_cast<void *>(mb)), nullptr);
    }

    // PushBack and the destructor: the original's body on a list with the game's table, ours on an object built by the
    // original constructor with the trap table over the vtable word (a dispatch through it counts a hit).
    std::mt19937 rng(0x1b9333);
    for (int round = 0; round < 400; ++round) {
        Side a, b;
        a.list = NewOriginal(ctx);
        b.list = NewOriginal(ctx);
        gTrapHits = 0;
        *reinterpret_cast<void ***>(b.list) = TrapTable();
        const unsigned pushes = rng() % 6;
        for (unsigned i = 0; i < pushes; ++i) {
            const unsigned e = rng() % kPool;
            gLog.clear();
            const bool ra = ctx.Original<ItemFn>(kPushBack)(a.list, &a.pool.e[e]);
            std::string la = Fmt("%d", ra);
            for (const std::string &line : gLog) la += " " + line + ";";
            gLog.clear();
            const bool rb = cRZUnknownListAdapters::PushBack(static_cast<cRZUnknownList *>(b.list), nullptr, &b.pool.e[e]);
            std::string lb = Fmt("%d", rb);
            for (const std::string &line : gLog) lb += " " + line + ";";
            ++ctx.cases;
            if (la != lb || StateOf(a.list, a.pool) != StateOf(b.list, b.pool) || a.pool.Refs() != b.pool.Refs())
                ctx.Fail("round %d PushBack of e%u: original [%s] %s / ours [%s] %s", round, e, la.c_str(),
                         StateOf(a.list, a.pool).c_str(), lb.c_str(), StateOf(b.list, b.pool).c_str());
        }
        // The destructor.
        std::string ends[2];
        size_t frees[2];
        uint32_t words[2];
        for (int side = 0; side < 2; ++side) {
            Side &s = side == 0 ? a : b;
            gLog.clear();
            difftest::FreeRecorder rec;
            if (side == 0)
                ctx.Original<void(__thiscall *)(void *)>(kDtor)(s.list);
            else
                cRZUnknownListAdapters::Destruct(static_cast<cRZUnknownList *>(s.list), nullptr);
            rec.Stop();
            frees[side] = rec.Count();
            words[side] = VtWord(s.list);
            for (const std::string &line : gLog) ends[side] += line + "; ";
            ends[side] += "| " + s.pool.Refs();
        }
        ++ctx.cases;
        if (ends[0] != ends[1] || frees[0] != frees[1])
            ctx.Fail("round %d destructor: original [%s] %zu frees / ours [%s] %zu frees", round, ends[0].c_str(), frees[0],
                     ends[1].c_str(), frees[1]);
        ++ctx.cases;
        if (words[0] != words[1]) ctx.Fail("round %d: the word at +0 after the destructor: 0x%x / 0x%x", round, words[0], words[1]);
        ++ctx.cases;
        if (gTrapHits != 0) ctx.Fail("round %d: an adapter dispatched through the object's vtable (%u hits)", round, gTrapHits);
        ::operator delete(a.list);
        ::operator delete(b.list);
        if (ctx.failures.size() >= 10) return;
    }
}
