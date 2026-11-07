// Differential tests for cGZRegistry's construction, destruction, its tree helpers and its service slots, in the standalone
// shape (lane KD; slots 3..7, the registry operations on the tree, are tests/services_b.cpp). The original side is an object
// from the original constructor 0xc6cb27 (the game's vtables), ours one from our constructor (the hook's adapter: our
// vtables), every slot called through the side's own vtable; objects are compared word by word with the vtable words mapped
// to codes (tests/service_shape.h). The destructor tests give both sides a tree of random shape whose nodes hold heap-sized
// strings, and check every node block and string block is freed on both sides, right after each side's own call.
//
// RVAs: the constructor 0xc6cb27, the destructor 0xc6cba4, the deleting destructor 0xc6cbd4 and its +8 thunk 0xc6cbcc,
// QueryInterface 0xc6cdfd and its thunks 0xc6ce1f (+4) / 0xc6ce27 (+8); the folded slots our vtables now hold: AddRef
// 0x145240, Release 0x1a85b0 (and their thunks), and cRZSystemService's.
#include "difftest.h"
#include "free_recorder.h"
#include "service_shape.h"
#include "gz/cGZRegistry.h"

#include <windows.h>

#include <cstring>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0xc6cb27, kDtor = 0xc6cba4, kDeleting = 0xc6cbd4, kDeletingThunk = 0xc6cbcc;
constexpr uint32_t kDestroyValue = 0xc6cb8a;

// The frees of the call under test: BeginFrees() right before it starts a FreeRecorder (replacing the
// previous one); Live() stops it and asks it (exact, unlike HeapValidate on a block that may be reused).
std::optional<difftest::FreeRecorder> gFrees;
void BeginFrees()
{
    gFrees.reset();
    gFrees.emplace();
}

bool Live(const void *p)
{
    gFrees->Stop();
    return !gFrees->Freed(p);
}

// Fixed-size list of the blocks a tree owns (no allocation while measuring).
struct BlockList {
    const void *ptrs[400];
    uint32_t count = 0;
    void Add(const void *p)
    {
        if (count < 400) ptrs[count++] = p;
    }
    uint64_t LiveBits() const
    {
        uint64_t bits = 0;
        for (uint32_t i = 0; i < count && i < 64; ++i) bits |= static_cast<uint64_t>(Live(ptrs[i])) << i;
        return bits;
    }
    bool AnyLive() const
    {
        for (uint32_t i = 0; i < count; ++i) if (Live(ptrs[i])) return true;
        return false;
    }
};

void AddString(BlockList &blocks, const cRZString &s)
{
    if (s.mString.capacity() > 15) blocks.Add(s.mString.data());
}

std::string RandomText(std::mt19937 &rng)
{
    return std::string(rng() % 3 == 0 ? rng() % 12 : 16 + rng() % 40, static_cast<char>('a' + rng() % 26));
}

// A binary tree of `count` nodes of random shape under a new head node (links only; the colours
// are irrelevant to destruction). Returns the head and records every block.
cGZRegistryNode *BuildTree(std::mt19937 &rng, uint32_t count, BlockList &blocks)
{
    auto *head = static_cast<cGZRegistryNode *>(::operator new(sizeof(cGZRegistryNode)));
    std::memset(head, 0, sizeof(*head));
    head->mLeft = head->mParent = head->mRight = head;
    head->mColor = 1;
    head->mIsNil = 1;
    blocks.Add(head);
    std::vector<cGZRegistryNode *> nodes;
    for (uint32_t i = 0; i < count; ++i) {
        auto *n = static_cast<cGZRegistryNode *>(::operator new(sizeof(cGZRegistryNode)));
        std::memset(n, 0, sizeof(*n));
        n->mLeft = n->mRight = head;
        n->mIsNil = 0;
        n->mKey.Construct(RandomText(rng).c_str());
        n->mFirst.Construct(RandomText(rng).c_str());
        n->mSecond.Construct(RandomText(rng).c_str());
        blocks.Add(n);
        AddString(blocks, n->mKey);
        AddString(blocks, n->mFirst);
        AddString(blocks, n->mSecond);
        if (nodes.empty()) {
            head->mParent = n;
            n->mParent = head;
        } else {
            // Attach to a random node with a free side.
            for (;;) {
                cGZRegistryNode *p = nodes[rng() % nodes.size()];
                bool left = rng() % 2;
                cGZRegistryNode *&slot = left ? p->mLeft : p->mRight;
                if (slot != head) continue;
                slot = n;
                n->mParent = p;
                break;
            }
        }
        nodes.push_back(n);
    }
    return head;
}

struct Pair {
    cGZRegistry *a;
    cGZRegistry *b;
};

Pair MakePair(difftest::Context &ctx)
{
    Pair p{static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry))), static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry)))};
    std::memset(p.a, 0xCD, sizeof(*p.a));
    std::memset(p.b, 0xCD, sizeof(*p.b));
    ctx.Original<cGZRegistry *(__thiscall *)(void *)>(kCtor)(p.a);
    cGZRegistryAdapters::Construct(p.b, nullptr);
    return p;
}

constexpr uint32_t kFaceOffset[3] = {0, 4, 8};
constexpr uint32_t kGameVt[3] = {cGZRegistry::kVTable, cGZRegistry::kSystemServiceVTable, cGZRegistry::kUnknownVTable};

// Each side's code map: the game's and our vtable of each face as one code per face, the destroyed service part's; the
// side's own head node as one more code.
struct Maps {
    svc::CodeMap a, b;
    explicit Maps(difftest::Context &ctx)
    {
        auto *ours = static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry)));
        cGZRegistryAdapters::Construct(ours, nullptr);
        for (uint32_t f = 0; f < 3; ++f) {
            a.AddRva(kGameVt[f], f);
            b.AddRva(kGameVt[f], f);
            b.Add(svc::VTableAt(ours, kFaceOffset[f]), f);
        }
        ::operator delete(ours->mTree.mHead);
        ::operator delete(ours);
        svc::AddServiceBaseCodes(a, b, 0x10);
        (void)ctx;
    }
    std::string Compare(const cGZRegistry *x, const cGZRegistry *y) const
    {
        svc::CodeMap ma = a, mb = b;
        ma.Add(x->mTree.mHead, 0x100);
        mb.Add(y->mTree.mHead, 0x100);
        return svc::Diff(svc::Words(x, sizeof(cGZRegistry), ma), svc::Words(y, sizeof(cGZRegistry), mb));
    }
};

// The original constructor's registry with a populated tree on each side (the empty head leaks).
void Populate(Pair &p, uint32_t seed, uint32_t count, BlockList &blocksA, BlockList &blocksB)
{
    std::mt19937 ra(seed), rb(seed);
    p.a->mTree.mHead = BuildTree(ra, count, blocksA);
    p.b->mTree.mHead = BuildTree(rb, count, blocksB);
    p.a->mTree.mSize = p.b->mTree.mSize = count;
}

} // namespace

DIFF_TEST(cGZRegistry_Construct, 0xc6cb27)
{
    auto orig = ctx.Original<cGZRegistry *(__thiscall *)(void *)>(kCtor);
    Maps maps(ctx);
    for (int fill : {0xCD, 0x00, 0xFF}) {
        cGZRegistry *a = static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry)));
        cGZRegistry *b = static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry)));
        std::memset(a, fill, sizeof(*a));
        std::memset(b, fill, sizeof(*b));
        cGZRegistry *ra = orig(a), *rb = cGZRegistryAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("fill %02x: return value is not this", fill);
        // Everything matches word for word, the vtable words and each side's own head pointer (a heap address) as codes.
        const std::string diff = maps.Compare(a, b);
        if (!diff.empty() || a->mTree.mSize != 0) ctx.Fail("fill %02x: object differs at %s", fill, diff.c_str());
        for (uint32_t f = 0; f < 3; ++f) {
            if (svc::VTableAt(a, kFaceOffset[f]) != GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: original's face %u vtable", fill, f);
            if (svc::VTableAt(b, kFaceOffset[f]) == GameAddress<void *>(kGameVt[f])) ctx.Fail("fill %02x: our face %u has the game's vtable", fill, f);
        }
        // The head node: links to itself, black, nil. Only these bytes are written.
        cGZRegistryNode *ha = a->mTree.mHead, *hb = b->mTree.mHead;
        if (ha->mLeft != ha || ha->mParent != ha || ha->mRight != ha || ha->mColor != 1 || ha->mIsNil != 1) ctx.Fail("fill %02x: original head node unexpected", fill);
        if (hb->mLeft != hb || hb->mParent != hb || hb->mRight != hb || hb->mColor != ha->mColor || hb->mIsNil != ha->mIsNil) ctx.Fail("fill %02x: our head node differs", fill);
        if (a->mServiceId != cGZRegistry::kServiceId || a->mPriority != cGZRegistry::kPriority) ctx.Fail("fill %02x: service id or priority", fill);
        ::operator delete(ha);
        ::operator delete(hb);
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(cGZRegistryNode_Destroy, 0xc6cb8a)
{
    // 0xc6cb8a (value destructor, ecx = the key at node + 0x10).
    auto destroyValue = ctx.Original<void(__thiscall *)(void *)>(kDestroyValue);
    std::mt19937 rng(0x7502);
    for (int run = 0; run < 100; ++run) {
        BlockList blocksA, blocksB;
        std::mt19937 ra(run), rb(run);
        cGZRegistryNode *head = nullptr;
        (void)head;
        auto make = [&](std::mt19937 &r, BlockList &blocks) {
            auto *n = static_cast<cGZRegistryNode *>(::operator new(sizeof(cGZRegistryNode)));
            std::memset(n, 0xCD, sizeof(*n));
            n->mKey.Construct(RandomText(r).c_str());
            n->mFirst.Construct(RandomText(r).c_str());
            n->mSecond.Construct(RandomText(r).c_str());
            blocks.Add(n);
            AddString(blocks, n->mKey);
            AddString(blocks, n->mFirst);
            AddString(blocks, n->mSecond);
            return n;
        };
        cGZRegistryNode *a = make(ra, blocksA), *b = make(rb, blocksB);
        BeginFrees();
        destroyValue(&a->mKey);
        uint64_t liveA = blocksA.LiveBits();
        BeginFrees();
        b->Entry().Destruct(); // same `this` as the original: the key
        uint64_t liveB = blocksB.LiveBits();
        ++ctx.cases;
        // Everything but the node block itself (bit 0) is freed.
        if (liveA != liveB || (liveA & ~1ull) != 0) ctx.Fail("run %d: value destruction frees differ: %llx/%llx", run, liveA, liveB);
        if (a->mKey.mVTable != b->mKey.mVTable || a->mFirst.mVTable != b->mFirst.mVTable || a->mSecond.mVTable != b->mSecond.mVTable
            || a->mKey.mString.capacity() != b->mKey.mString.capacity() || a->mSecond.mString.size() != b->mSecond.mString.size()) {
            ctx.Fail("run %d: strings after destruction differ", run);
        }
        ::operator delete(a);
        ::operator delete(b);
    }
}

// The destructor 0xc6cba4 (called directly; ours: our destructor, no dispatch), the deleting destructor 0xc6cbd4 (on the
// primary pointer) and its +8 thunk 0xc6cbcc (ours: our +8 vtable's slot 3 for both), flags 0..3, trees of 0..40 nodes.
DIFF_TEST(cGZRegistry_Destruct, 0xc6cba4)
{
    Maps maps(ctx);
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    std::mt19937 rng(0x7501);
    for (int run = 0; run < 150; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        Pair p = MakePair(ctx);
        BlockList blocksA, blocksB;
        uint32_t count = run % 10 == 0 ? 0 : run % 4 == 0 ? 1 + rng() % 40 : rng() % 7;
        ::operator delete(p.a->mTree.mHead);
        ::operator delete(p.b->mTree.mHead);
        Populate(p, run, count, blocksA, blocksB);
        const uint32_t seed[4] = {rng(), rng(), rng(), rng() % 5}; // service id, priorities, count
        for (cGZRegistry *r : {p.a, p.b}) {
            r->mServiceId = seed[0];
            r->mPriority = static_cast<int32_t>(seed[1]);
            r->mTickPriority = static_cast<int32_t>(seed[2]);
            r->mRefCount = seed[3];
        }
        BeginFrees();
        void *ra = route == 0 ? (dtor(p.a), p.a) : route == 1 ? deleting(p.a, flags) : thunk(reinterpret_cast<char *>(p.a) + 8, flags);
        const bool freedA = !Live(p.a);
        uint64_t liveA = blocksA.LiveBits();
        bool anyA = blocksA.AnyLive();
        BeginFrees();
        void *rb = p.b;
        if (route == 0) {
            p.b->cGZRegistry::~cGZRegistry();
        } else {
            const uint32_t args[1] = {flags};
            rb = reinterpret_cast<void *>(svc::CallWords(svc::VTableAt(p.b, 8)[3], reinterpret_cast<char *>(p.b) + 8, 1, args));
        }
        const bool freedB = !Live(p.b);
        uint64_t liveB = blocksB.LiveBits();
        bool anyB = blocksB.AnyLive();
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("route %d flags %u: return value is not the object", route, flags);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("route %d flags %u: object freed %d / %d", route, flags, freedA, freedB);
        if (anyA || anyB || liveA != liveB) ctx.Fail("%u nodes: blocks left allocated original %d ours %d", count, anyA, anyB);
        if (!freedA && !freedB) {
            p.a->mTree.mHead = p.b->mTree.mHead = nullptr; // freed above
            const std::string diff = maps.Compare(p.a, p.b);
            if (!diff.empty()) ctx.Fail("route %d, %u nodes: state after destruction differs at %s", route, count, diff.c_str());
        }
        if (!freedA) ::operator delete(p.a);
        if (!freedB) ::operator delete(p.b);
    }
}

namespace {

struct Slot {
    unsigned face, slot, words;
    svc::Ret ret;
    const char *name;
};

// Every slot but the registry operations (tests/services_b.cpp) and the deleting destructor (the destructor test).
const Slot kSlotTable[] = {
    {0, 0, 2, svc::kByte, "QueryInterface"},       {0, 1, 0, svc::kWord, "AddRef"},
    {0, 2, 0, svc::kWord, "Release"},
    {1, 0, 2, svc::kByte, "+4 QueryInterface"},    {1, 1, 0, svc::kWord, "+4 AddRef"},
    {1, 2, 0, svc::kWord, "+4 Release"},           {1, 3, 0, svc::kWord, "GetServiceID"},
    {1, 4, 1, svc::kVoid, "SetServiceID"},         {1, 5, 0, svc::kWord, "GetServicePriority"},
    {1, 6, 0, svc::kByte, "IsServiceRunning"},     {1, 7, 1, svc::kVoid, "SetServiceRunning"},
    {1, 8, 0, svc::kByte, "Init"},                 {1, 9, 0, svc::kByte, "Shutdown"},
    {1, 10, 1, svc::kByte, "OnTick"},              {1, 11, 1, svc::kByte, "OnIdle"},
    {1, 12, 0, svc::kWord, "GetServiceTickPriority"},
    {2, 0, 2, svc::kByte, "+8 QueryInterface"},    {2, 1, 0, svc::kWord, "+8 AddRef"},
    {2, 2, 0, svc::kWord, "+8 Release"},           {2, 4, 0, svc::kWord, "RemoveRef"},
    {2, 5, 0, svc::kWord, "RefCount"},
};

constexpr uint32_t kIds[] = {cGZRegistry::kInterfaceId, 0x287fb697, 1, 0, 2, 0xa237613b, 0xa237613d, 0xffffffff, 0x089b7dc8, 0x25076b9e};

} // namespace

// Every slot of the three faces but the registry operations and the deleting destructor, through each side's own vtable
// (a Release at a count of 1 or below deletes the object: its empty tree's head is freed with it).
DIFF_TEST(cGZRegistry_Slots, 0xc6cdfd)
{
    Maps maps(ctx);
    std::mt19937 rng(0x7e65);
    void *const kUntouched = reinterpret_cast<void *>(0x5a5a5a5a);
    constexpr unsigned kSlotCount[3] = {8, 13, 6};
    for (const Slot &slot : kSlotTable) {
        if (slot.slot >= kSlotCount[slot.face]) ctx.Fail("%s: slot out of range", slot.name);
        const int runs = slot.slot == 0 ? 2 * static_cast<int>(std::size(kIds)) : 24;
        for (int run = 0; run < runs; ++run) {
            Pair p = MakePair(ctx);
            // The same random service fields on both sides: count (0..5 or random), id, priorities, the running byte (0,
            // 0x80 or random, with random padding).
            for (uint32_t offset = 0xc; offset < 0x20; offset += 4) {
                uint32_t value = rng();
                if (offset == 0xc && run % 8 < 6) value = static_cast<uint32_t>(run % 6);
                if (offset == 0x1c && run % 4 == 0) value &= 0xffffff00;
                if (offset == 0x1c && run % 4 == 1) value = (value & 0xffffff00) | 0x80;
                std::memcpy(reinterpret_cast<uint8_t *>(p.a) + offset, &value, 4);
                std::memcpy(reinterpret_cast<uint8_t *>(p.b) + offset, &value, 4);
            }
            uint32_t args[2] = {rng(), rng()};
            if (slot.slot == 0) args[0] = kIds[run % std::size(kIds)];
            void *outA = kUntouched, *outB = kUntouched;
            uint32_t argsA[2] = {args[0], slot.slot == 0 ? svc::P(&outA) : args[1]};
            uint32_t argsB[2] = {args[0], slot.slot == 0 ? svc::P(&outB) : args[1]};
            const void *headA = p.a->mTree.mHead, *headB = p.b->mTree.mHead;
            BeginFrees();
            const uint32_t ra = svc::CallWords(GameAddress<void *>(kGameVt[slot.face])[slot.slot], reinterpret_cast<uint8_t *>(p.a) + kFaceOffset[slot.face], slot.words, argsA);
            const bool freedA = !Live(p.a), headFreedA = !Live(headA);
            BeginFrees();
            const uint32_t rb = svc::CallWords(svc::VTableAt(p.b, kFaceOffset[slot.face])[slot.slot], reinterpret_cast<uint8_t *>(p.b) + kFaceOffset[slot.face], slot.words, argsB);
            const bool freedB = !Live(p.b), headFreedB = !Live(headB);
            ++ctx.cases;
            if (svc::Masked(slot.ret, ra) != svc::Masked(slot.ret, rb)) ctx.Fail("%s run %d: result 0x%x / 0x%x", slot.name, run, ra, rb);
            if (freedA != freedB || headFreedA != headFreedB || freedA != headFreedA) ctx.Fail("%s run %d: freed %d / %d, head %d / %d", slot.name, run, freedA, freedB, headFreedA, headFreedB);
            const int32_t offA = outA == kUntouched ? -1 : static_cast<int32_t>(svc::P(outA) - svc::P(p.a));
            const int32_t offB = outB == kUntouched ? -1 : static_cast<int32_t>(svc::P(outB) - svc::P(p.b));
            if (offA != offB) ctx.Fail("%s run %d id 0x%x: out offset %d / %d", slot.name, run, args[0], offA, offB);
            if (!freedA && !freedB) {
                const std::string diff = maps.Compare(p.a, p.b);
                if (!diff.empty()) ctx.Fail("%s run %d: object differs at %s", slot.name, run, diff.c_str());
            }
            if (slot.slot == 0 && args[0] == cGZRegistry::kInterfaceId && ((ra & 0xff) != 1 || offA != 0)) ctx.Fail("%s: registry id: result %u offset %d", slot.name, ra & 0xff, offA);
            if (slot.slot == 0 && args[0] == 0x287fb697 && ((ra & 0xff) != 1 || offA != 4)) ctx.Fail("%s: system service id: result %u offset %d", slot.name, ra & 0xff, offA);
            if (!freedA) {
                ::operator delete(p.a->mTree.mHead);
                ::operator delete(p.a);
            }
            if (!freedB) {
                ::operator delete(p.b->mTree.mHead);
                ::operator delete(p.b);
            }
        }
    }
}
