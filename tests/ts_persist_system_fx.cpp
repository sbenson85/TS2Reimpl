// Lane FX: cTSPersistSystem in the standalone shape (stage A). Tests of the class itself:
// - TSPersistSystemFX_Construct (0x1d8b90 / base 0x9b3f30): the original constructor and ours on the same pre-filled
//   buffer pattern; every dword compared, except the six vtable words (coded: the side's own vtables) and the heap pointers
//   the constructors make, which are compared by shape (a hash table's empty sentinel and its 16 bucket words, a tree's head,
//   the deque proxy, the cache list with its one basic cache strategy (its vtable coded, its plain words compared), the four locks' blocks) and by allocation size.
// - TSPersistSystemFX_Destruct (0x1d9cb0 / 0x1da310 / base 0x9b4120 / 0x9b41d0): both sides built by their constructor, then
//   seeded fake counted objects in the fields the destructor releases (logged in order), destroyed by the original, and on our
//   side by the Destruct adapter, the deleting destructor through our +8 vtable slot 3 (flags 0 or 1), or the adapters on an
//   object the ORIGINAL constructor built whose six vtables are trap copies (arity-matched): an adapter that dispatched
//   virtually would hit a trap. Compared: the release log, which of the object's heap blocks were freed, the bytes left
//   (vtable words aside), and for flags 1 that the object itself was freed. Three seeds in four also put up to three
//   entries in each map whose destructor is the library's (lane IB: change records with inner record maps, the two
//   segment tables, +0x154, the groups' segment vectors, freed through a fake allocator service): their releases are in
//   the log and their heap blocks (nodes, inner sentinels and buckets, long strings' buffers, vectors) in the freed check.
// - TSPersistSystemFX_Slots: every slot of our six vtables. A slot that forwards to a body elsewhere (the base twin's slots to
//   the hook-shaped resource manager's bodies; the secondary faces' QueryInterface and the translation provider's slot 0, the
//   compiler's thunks, to the class's QueryInterface / GetGroupIdForPathChars) must reach that body (its code patched with an
//   arity-matched recorder) with the face pointer the body takes and the arguments unchanged, and the game's vtable must hold,
//   in that slot, the RVA that body replaces. The class's own slot bodies (lane GD, stage B) are covered by their own
//   difftests on both routes (tests/ts_persist_system_shape.h); here they are only checked to sit in a game slot. The other
//   slots (folded getters and setters, the rz service defaults, AddRef / Release thunks, `mov al, 1` hooks) are called on
//   both sides' objects (the game's slot on the original's) and compared.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cGZPersistCacheStrategyBasic.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"

#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace {

using shape::StackWord;

constexpr uint32_t kCtor = 0x1d8b90, kDtor = 0x1d9cb0, kDeleting = 0x1da310;
constexpr uint32_t kFaces[6] = {0, 4, 8, 0xa8, 0xac, 0xb0};
constexpr uint32_t kGameVTables[6] = {cTSPersistSystem::kPrimaryVTable, cTSPersistSystem::kServiceVTable,
                                      cTSPersistSystem::kUnknownVTable, cTSPersistSystem::kITSVTable,
                                      cTSPersistSystem::kHooksVTable, cTSPersistSystem::kTranslationVTable};
constexpr unsigned kSlotCounts[6] = {58, 13, 6, 75, 11, 2};

// Stack words per slot (each face), from the bodies' `ret N`.
constexpr uint8_t kPrimaryWords[58] = {2, 0, 0, 5, 6, 5, 5, 5, 1, 2, 1, 1, 2, 2, 3, 1, 2, 2, 2, 2, 3, 1, 2, 2, 2, 0, 1, 1, 1,
                                       1, 1, 1, 0, 2, 2, 0, 1, 2, 2, 3, 2, 1, 1, 0, 1, 0, 1, 0, 0, 3, 2, 5, 6, 7, 0, 2, 0, 1};
constexpr uint8_t kServiceWords[13] = {2, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0};
constexpr uint8_t kUnknownWords[6] = {2, 0, 0, 1, 0, 0};
constexpr uint8_t kITSWords[75] = {2, 0, 0, 0, 5, 5, 5, 4, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 3, 1, 3, 7, 7, 1, 2, 2, 1, 1, 1,
                                   1, 2, 2, 2, 3, 3, 2, 3, 4, 4, 0, 0, 0, 0, 1, 2, 1, 1, 2, 2, 1, 2, 1, 0, 0, 3, 3, 1, 3,
                                   3, 3, 0, 1, 1, 2, 1, 1, 0, 0, 0, 6, 2, 5, 3, 4, 3};
constexpr uint8_t kHooksWords[11] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kTranslationWords[2] = {1, 0};
const uint8_t *const kWords[6] = {kPrimaryWords, kServiceWords, kUnknownWords, kITSWords, kHooksWords, kTranslationWords};

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t &W(void *object, uint32_t offset) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + offset); }
void **&VT(void *object, uint32_t offset) { return *reinterpret_cast<void ***>(static_cast<uint8_t *>(object) + offset); }

// Overwrites a function's first five bytes with a jump, for the scope.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

// Calls a vtable slot with `words` stack arguments.
template <unsigned... I> uint32_t CallWords(void *fn, void *self, const uint32_t *a, std::integer_sequence<unsigned, I...>)
{
    using F = uint32_t(__thiscall *)(void *, StackWord<I>...);
    return reinterpret_cast<F>(fn)(self, a[I]...);
}

uint32_t CallFn(void *fn, void *self, unsigned words, const uint32_t *a)
{
    switch (words) {
    case 0: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 0>{});
    case 1: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 1>{});
    case 2: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 2>{});
    case 3: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 3>{});
    case 4: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 4>{});
    case 5: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 5>{});
    case 6: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 6>{});
    default: return CallWords(fn, self, a, std::make_integer_sequence<unsigned, 7>{});
    }
}

uint32_t CallSlot(void *object, uint32_t face, unsigned slot, unsigned words, const uint32_t *a)
{
    void *self = static_cast<uint8_t *>(object) + face;
    return CallFn(VT(object, face)[slot], self, words, a);
}

// A recorder of `words` stack arguments, patched over a body: logs `this` and the arguments.
int gHits = 0;
uint32_t gSelf = 0;
uint32_t gArgs[8];
unsigned gWords = 0;
constexpr uint32_t kRecorded = 0x5a5a5a81;

template <unsigned... I> uint32_t __fastcall RecordOf(void *self, void *, StackWord<I>... w)
{
    ++gHits;
    gSelf = P(self);
    const uint32_t a[] = {0u, w...};
    for (unsigned i = 0; i < sizeof...(I); ++i) gArgs[i] = a[i + 1];
    gWords = sizeof...(I);
    return kRecorded;
}
template <unsigned... I> void *RecordAddress(std::integer_sequence<unsigned, I...>) { return reinterpret_cast<void *>(&RecordOf<I...>); }
void *RecorderFor(unsigned words)
{
    switch (words) {
    case 0: return RecordAddress(std::make_integer_sequence<unsigned, 0>{});
    case 1: return RecordAddress(std::make_integer_sequence<unsigned, 1>{});
    case 2: return RecordAddress(std::make_integer_sequence<unsigned, 2>{});
    case 3: return RecordAddress(std::make_integer_sequence<unsigned, 3>{});
    case 4: return RecordAddress(std::make_integer_sequence<unsigned, 4>{});
    case 5: return RecordAddress(std::make_integer_sequence<unsigned, 5>{});
    case 6: return RecordAddress(std::make_integer_sequence<unsigned, 6>{});
    default: return RecordAddress(std::make_integer_sequence<unsigned, 7>{});
    }
}

// Trap vtables: any call is counted (arity-matched, so the stack stays balanced and the test can report).
int gTrapHits = 0;
uint32_t CountTrap(void *, unsigned)
{
    ++gTrapHits;
    return 0;
}

// shape::TrapFor stops at six words; ITS slots 21 / 22 and primary slot 53 take seven.
template <unsigned... I> uint32_t __fastcall CountOf(void *self, void *, StackWord<I>...) { return CountTrap(self, sizeof...(I)); }
template <unsigned... I> void *CountAddress(std::integer_sequence<unsigned, I...>) { return reinterpret_cast<void *>(&CountOf<I...>); }
void *TrapWords(unsigned words)
{
    if (words == 7) return CountAddress(std::make_integer_sequence<unsigned, 7>{});
    return shape::TrapFor<&CountTrap>(words);
}

struct TrapTables {
    std::vector<void *> tables[6];
    void Install(void *object)
    {
        for (unsigned f = 0; f < 6; ++f) {
            tables[f].resize(kSlotCounts[f]);
            for (unsigned s = 0; s < kSlotCounts[f]; ++s) tables[f][s] = TrapWords(kWords[f][s]);
            VT(object, kFaces[f]) = tables[f].data();
        }
    }
};

// Objects. Buffers are heap blocks pre-filled with one pattern, so bytes no constructor writes compare equal.
constexpr uint8_t kFill = 0xcd;

void *NewBuffer()
{
    void *p = ::operator new(cTSPersistSystem::kSize);
    std::memset(p, kFill, cTSPersistSystem::kSize);
    return p;
}

void *BuildOriginal(const difftest::Context &ctx)
{
    void *p = NewBuffer();
    ctx.Original<void *(__thiscall *)(void *)>(kCtor)(p);
    return p;
}

void *BuildOurs()
{
    void *p = NewBuffer();
    cTSPersistSystemAdapters::Construct(p, nullptr);
    return p;
}

// Our six vtable words, read once from an object our constructor built.
const uint32_t *OurVTables()
{
    static uint32_t words[6] = {};
    static bool done = false;
    if (!done) {
        void *p = BuildOurs();
        for (unsigned f = 0; f < 6; ++f) words[f] = W(p, kFaces[f]);
        cTSPersistSystemAdapters::Destruct(static_cast<cTSPersistSystem *>(p), nullptr);
        ::operator delete(p);
        done = true;
    }
    return words;
}

bool IsVTableWord(uint32_t offset)
{
    for (uint32_t f : kFaces)
        if (f == offset) return true;
    return false;
}

size_t BlockSize(const void *p)
{
    if (!p) return 0;
    if (!HeapValidate(GetProcessHeap(), 0, p)) return static_cast<size_t>(-2);
    return HeapSize(GetProcessHeap(), 0, p);
}

// Our basic cache strategy's vtable word, read from a scratch object.
uint32_t OurCacheVTable()
{
    alignas(8) static uint8_t storage[cGZPersistCacheStrategyBasic::kObjectSize];
    static const uint32_t word = [] {
        auto *scratch = cGZPersistCacheStrategyBasic::Construct(storage);
        const uint32_t vt = *reinterpret_cast<const uint32_t *>(scratch);
        scratch->cGZPersistCacheStrategyBasic::~cGZPersistCacheStrategyBasic();
        return vt;
    }();
    return word;
}

// The constructed object as a list of words: raw dwords, with the heap structures replaced by their shape.
constexpr uint32_t kMaps[] = {0x28, 0x48, 0xf4, 0x114, 0x134, 0x154, 0x178, 0x198, 0x1e8, 0x20c, 0x248};
constexpr uint32_t kTrees[] = {0x1b8, 0x23c};
constexpr uint32_t kLocks[] = {0x20, 0x68, 0x88, 0x98};

std::vector<uint32_t> Dump(void *object, bool ours)
{
    std::vector<uint32_t> out;
    std::vector<bool> covered(cTSPersistSystem::kSize / 4, false);
    auto cover = [&](uint32_t offset) { covered[offset / 4] = true; };
    const uint32_t *own = ours ? OurVTables() : nullptr;
    for (unsigned f = 0; f < 6; ++f) {
        const uint32_t word = W(object, kFaces[f]);
        const uint32_t expect = ours ? own[f] : P(GameAddress<void>(kGameVTables[f]));
        out.push_back(word == expect ? 0x7700 + f : word);
        if (ours) out.push_back(word != P(GameAddress<void>(kGameVTables[f])));
        else out.push_back(1);
        cover(kFaces[f]);
    }
    for (uint32_t m : kMaps) {
        auto *head = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(W(object, m + 4)));
        auto *begin = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(W(object, m + 0xc)));
        const bool sentinel = head && head[0] == P(head) && head[1] == P(head);
        bool buckets = begin && W(object, m + 0x10) == P(begin + 16) && W(object, m + 0x14) == P(begin + 16);
        for (unsigned i = 0; buckets && i < 16; ++i) buckets = begin[i] == P(head);
        out.push_back(0x1000 + m);
        out.push_back(sentinel);
        out.push_back(static_cast<uint32_t>(BlockSize(head)));
        out.push_back(buckets);
        out.push_back(static_cast<uint32_t>(BlockSize(begin)));
        cover(m + 4);
        cover(m + 0xc);
        cover(m + 0x10);
        cover(m + 0x14);
    }
    for (uint32_t t : kTrees) {
        auto *head = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(W(object, t)));
        out.push_back(0x2000 + t);
        out.push_back(head && head[0] == P(head) && head[1] == P(head) && head[2] == P(head));
        out.push_back(head ? (head[3] & 0xffff) : 0xdead);
        out.push_back(static_cast<uint32_t>(BlockSize(head)));
        cover(t);
    }
    for (uint32_t l : kLocks) {
        out.push_back(0x3000 + l);
        out.push_back(W(object, l)); // the lock's vtable: the game's on both sides (cRZCriticalSection is hook-shaped)
        out.push_back(W(object, l + 4) != 0);
        cover(l);
        cover(l + 4);
    }
    {
        auto *proxy = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(W(object, 0x70)));
        out.push_back(0x4070);
        out.push_back(proxy && proxy[0] == P(static_cast<uint8_t *>(object) + 0x70) && proxy[1] == 0);
        out.push_back(static_cast<uint32_t>(BlockSize(proxy)));
        cover(0x70);
    }
    {
        auto *head = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(W(object, 0x90)));
        auto *node = head ? reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(head[0])) : nullptr;
        out.push_back(0x5090);
        out.push_back(head && node && node != head && node[0] == P(head) && node[1] == P(head) && head[1] == P(node));
        out.push_back(static_cast<uint32_t>(BlockSize(head)));
        out.push_back(static_cast<uint32_t>(BlockSize(node)));
        auto *cache = node ? reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(node[2])) : nullptr;
        // Its vtable, coded: the side's own basic strategy (the game's 0xe60148 from 0x9b35f0; ours from
        // cGZPersistCacheStrategyBasic::Construct since lane QQ). Then the strategy's words that are not heap pointers: the
        // map's load factor, size, mask and max index, the counter, flag, filter and count.
        const uint32_t sideVTable = ours ? OurCacheVTable() : P(GameAddress<void>(0xe60148));
        out.push_back(cache ? (cache[0] == sideVTable ? 0x7800u : cache[0]) : 0);
        // (Word 10 is the flag byte and three bytes of padding the constructors leave as the heap had them.)
        for (unsigned i : {1u, 3u, 7u, 8u, 9u, 10u, 11u, 12u})
            out.push_back(cache && BlockSize(cache) >= 0x34 ? (i == 10 ? cache[i] & 0xff : cache[i]) : 0xdead);
        out.push_back(static_cast<uint32_t>(BlockSize(cache)));
        cover(0x90);
    }
    for (uint32_t o = 0; o < cTSPersistSystem::kSize; o += 4) {
        if (covered[o / 4]) continue;
        out.push_back(o);
        out.push_back(W(object, o));
    }
    return out;
}

void FirstDifference(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, difftest::Context &ctx, const char *what)
{
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            ctx.Fail("%s: word %zu: original %08x, ours %08x (previous %08x)", what, i, a[i], b[i], i ? a[i - 1] : 0u);
            return;
        }
    }
    if (a.size() != b.size()) ctx.Fail("%s: dump sizes %zu / %zu", what, a.size(), b.size());
}

// Fake counted objects for the destructor: Release logs the label.
std::string gLog;
struct Counted {
    void **vtable;
    char label;
};
uint32_t __fastcall FakeQI(Counted *, void *, uint32_t, void **) { return 0; }
uint32_t __fastcall FakeAddRef(Counted *, void *) { return 2; }
uint32_t __fastcall FakeRelease(Counted *self, void *)
{
    gLog += self->label;
    return 1;
}
void *gCountedVTable[3] = {reinterpret_cast<void *>(&FakeQI), reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease)};
Counted gCounted[12];

// The fields the destructor releases, with a label each.
constexpr uint32_t kCountedFields[] = {0xcc, 0xd0, 0xe0, 0xe4, 0xe8, 0xec, 0x1c0, 0x84};

struct World {
    uint32_t present = 0;     // bit i: kCountedFields[i] holds a fake
    unsigned listeners = 0;   // 0..2 fakes in the listener vector
    uint32_t entries = 0;     // nonzero: the seed of the map entries (PopulateEntries)
};

void Populate(void *object, const World &w)
{
    for (unsigned i = 0; i < 8; ++i) {
        gCounted[i].vtable = gCountedVTable;
        gCounted[i].label = static_cast<char>('a' + i);
        if (w.present & (1u << i)) W(object, kCountedFields[i]) = P(&gCounted[i]);
    }
    if (w.listeners) {
        auto **v = static_cast<void **>(::operator new(4 * w.listeners));
        for (unsigned i = 0; i < w.listeners; ++i) {
            gCounted[8 + i].vtable = gCountedVTable;
            gCounted[8 + i].label = static_cast<char>('L' + i);
            v[i] = &gCounted[8 + i];
        }
        W(object, 0x230) = P(v);
        W(object, 0x234) = P(v + w.listeners);
        W(object, 0x238) = P(v + w.listeners);
    }
}

// Entries in the hash maps whose destructors are the library's (lane IB): every payload kind the node destructors free or
// release. The counted objects log '#<n>,' when released; the allocator service the group vectors are freed through
// (0x1e6a40 -> 0x197100, slot 4) logs 'F' and frees. Every heap block made here goes into `blocks`, so the test checks it
// is freed.
uint32_t __fastcall FakeEntryRelease(Counted *self, void *)
{
    gLog += '#';
    gLog += std::to_string(static_cast<unsigned char>(self->label));
    gLog += ',';
    return 1;
}
void *gEntryVTable[3] = {reinterpret_cast<void *>(&FakeQI), reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeEntryRelease)};
Counted gEntryCounted[96];

void __fastcall FakeServiceFree(void *, void *, void *block)
{
    gLog += 'F';
    ::operator delete(block);
}
void *gServiceVTable[5] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FakeServiceFree)};
void *gService[1] = {gServiceVTable};

// While alive, the allocator service cache (0x10c95d4, guard 0x10c95d8: initialised) holds the fake above.
struct ServiceScope {
    explicit ServiceScope(const difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        *cache = gService;
        *guard = INT32_MIN;
    }
    ~ServiceScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
};

struct EntryMaker {
    std::mt19937 rng;
    std::vector<void *> &blocks;
    unsigned counted = 0;

    void *Block(size_t size)
    {
        void *p = ::operator new(size);
        std::memset(p, 0xcd, size);
        blocks.push_back(p);
        return p;
    }
    // A fake counted object, or (one in four) none.
    uint32_t NewCounted()
    {
        if (rng() % 4 == 0) return 0;
        auto &c = gEntryCounted[counted];
        c.vtable = gEntryVTable;
        c.label = static_cast<char>(counted++);
        return P(&c);
    }
    // A node appended to the std::list whose sentinel is `head`; the list size word is `*size`.
    static void Link(void *head, uint32_t &size, void *node)
    {
        void *last = reinterpret_cast<void *>(static_cast<uintptr_t>(W(head, 4)));
        W(node, 0) = P(head);
        W(node, 4) = P(last);
        W(last, 0) = P(node);
        W(head, 4) = P(node);
        ++size;
    }
    void LinkInto(void *object, uint32_t map, void *node)
    {
        Link(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, map + 4))), W(object, map + 8), node);
    }
    // A cRZString at node+offset: short (in place) or long (its buffer a block).
    void String(void *node, uint32_t offset)
    {
        std::string text = "seg" + std::to_string(rng() % 100);
        if (rng() & 1) text += "/a path long enough for the heap " + std::to_string(rng() % 1000);
        auto *s = reinterpret_cast<cRZString *>(static_cast<uint8_t *>(node) + offset);
        s->Construct(text.c_str());
        if (W(node, offset + 0x18) > 15) blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(node, offset + 4))));
    }
    // A vector (first, last, end) at node+offset of `count` words made by `word`, with spare capacity; null when empty
    // and without capacity.
    template <typename F> void Vector(void *node, uint32_t offset, F word)
    {
        const uint32_t count = rng() % 4, cap = count + rng() % 3;
        uint32_t *data = cap ? static_cast<uint32_t *>(Block(cap * 4)) : nullptr;
        for (uint32_t i = 0; i < count; ++i) data[i] = word();
        W(node, offset) = P(data);
        W(node, offset + 4) = P(data + count);
        W(node, offset + 8) = P(data + cap);
    }
};

// The base class's cache list (+0x90: nodes of next, prev, a counted strategy or none) and segment deque (+0x70..+0x80:
// a block table of `mapSize` pointers, the first blocks made, 0x10 bytes each), which the destructor clears and tidies
// (lane QN: ours now, the game's 0x99df40 and 0x3cfe90 before). Every node and block goes into `blocks`.
Counted gValueCounted[4];

void PopulateBase(void *object, uint32_t seed, std::vector<void *> &blocks)
{
    std::mt19937 rng(seed * 7919u + 1);
    void *head = reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, 0x90)));
    uint32_t listSize = 0;
    for (uint32_t i = 0, n = rng() % 4; i < n; ++i) {
        void *node = ::operator new(0xc);
        blocks.push_back(node);
        if (rng() % 4 == 0) {
            W(node, 8) = 0;
        } else {
            gValueCounted[i].vtable = gEntryVTable;
            gValueCounted[i].label = static_cast<char>(0x60 + i);
            W(node, 8) = P(&gValueCounted[i]);
        }
        EntryMaker::Link(head, listSize, node);
    }
    const uint32_t used = rng() % 4;
    if (used) {
        const uint32_t mapSize = used + rng() % 3;
        auto *table = static_cast<uint32_t *>(::operator new(mapSize * 4));
        blocks.push_back(table);
        for (uint32_t i = 0; i < mapSize; ++i) {
            if (i < used) {
                void *block = ::operator new(0x10);
                std::memset(block, 0, 0x10);
                blocks.push_back(block);
                table[i] = P(block);
            } else {
                table[i] = 0;
            }
        }
        W(object, 0x74) = P(table);
        W(object, 0x78) = mapSize;
        W(object, 0x7c) = rng() % 4;
        W(object, 0x80) = rng() % 9;
    }
}

// Up to three entries in each of the five maps: the change records (+0x20c, node 0x30: key, an inner map of node 0x20 with
// a counted pointer at node+0x18, a trailing word), the segment tables (+0x178 / +0x198, node 0x2c: cRZString, counted
// segment), +0x154 (node 0x3c: cRZString, two words, a word vector), the groups (+0x114, node 0x18: key, a vector of
// counted segments in the allocator service's memory).
void PopulateEntries(void *object, uint32_t seed, std::vector<void *> &blocks)
{
    EntryMaker m{std::mt19937(seed), blocks};
    for (uint32_t i = 0, n = m.rng() % 4; i < n; ++i) {
        void *node = m.Block(0x30);
        W(node, 8) = m.rng();
        void *inner = m.Block(0x20);
        W(inner, 0) = W(inner, 4) = P(inner);
        reinterpret_cast<float &>(W(node, 0xc)) = 1.0f;
        W(node, 0x10) = P(inner);
        W(node, 0x14) = 0;
        if (m.rng() & 1) {
            uint32_t *buckets = static_cast<uint32_t *>(m.Block(0x40));
            for (int b = 0; b < 16; ++b) buckets[b] = P(inner);
            W(node, 0x18) = P(buckets);
            W(node, 0x1c) = W(node, 0x20) = P(buckets + 16);
        } else {
            W(node, 0x18) = W(node, 0x1c) = W(node, 0x20) = 0;
        }
        W(node, 0x24) = 7;
        W(node, 0x28) = 8;
        W(node, 0x2c) = m.rng();
        for (uint32_t j = 0, k = m.rng() % 4; j < k; ++j) {
            void *record = m.Block(0x20);
            for (uint32_t o = 8; o < 0x18; o += 4) W(record, o) = m.rng();
            W(record, 0x18) = m.NewCounted();
            W(record, 0x1c) = m.rng();
            EntryMaker::Link(inner, W(node, 0x14), record);
        }
        m.LinkInto(object, 0x20c, node);
    }
    for (uint32_t map : {0x178u, 0x198u}) {
        for (uint32_t i = 0, n = m.rng() % 4; i < n; ++i) {
            void *node = m.Block(0x2c);
            m.String(node, 8);
            W(node, 0x28) = m.NewCounted();
            m.LinkInto(object, map, node);
        }
    }
    for (uint32_t i = 0, n = m.rng() % 4; i < n; ++i) {
        void *node = m.Block(0x3c);
        m.String(node, 8);
        W(node, 0x28) = m.rng();
        W(node, 0x2c) = m.rng();
        m.Vector(node, 0x30, [&] { return static_cast<uint32_t>(m.rng()); });
        m.LinkInto(object, 0x154, node);
    }
    for (uint32_t i = 0, n = m.rng() % 4; i < n; ++i) {
        void *node = m.Block(0x18);
        W(node, 8) = m.rng();
        m.Vector(node, 0xc, [&] { return m.NewCounted(); });
        m.LinkInto(object, 0x114, node);
    }
}

// The heap blocks the destructor must free: the hash tables' sentinels and bucket vectors, the trees' heads, the deque
// proxy, the cache list's sentinel, the listener vector.
std::vector<void *> Blocks(void *object)
{
    std::vector<void *> blocks;
    for (uint32_t m : kMaps) {
        blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, m + 4))));
        blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, m + 0xc))));
    }
    for (uint32_t t : kTrees) blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, t))));
    blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, 0x70))));
    blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, 0x90))));
    if (W(object, 0x230)) blocks.push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(object, 0x230))));
    return blocks;
}

// Which blocks the recorded destructor call freed (lane IB: a FreeRecorder rather than HeapValidate, whose answer on a freed
// block depends on reuse; the log copies after the call allocate).
void Freed(const difftest::FreeRecorder &rec, const std::vector<void *> &blocks, std::vector<uint32_t> &out)
{
    out.resize(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) out[i] = rec.Freed(blocks[i]);
}

// Words that hold heap pointers the constructors made (lock blocks, hash table sentinels and bucket vectors, tree heads, the
// deque proxy, the cache list's sentinel) or the listener vector: their values differ between two objects, so after
// destruction only whether each still holds what it held before, or zero, or something else, is compared.
bool IsPointerWord(uint32_t o)
{
    for (uint32_t l : kLocks)
        if (o == l + 4) return true;
    for (uint32_t m : kMaps)
        if (o == m + 4 || o == m + 0xc || o == m + 0x10 || o == m + 0x14) return true;
    for (uint32_t t : kTrees)
        if (o == t) return true;
    return o == 0x70 || o == 0x74 || o == 0x90 || o == 0x230 || o == 0x234 || o == 0x238;
}

std::vector<uint32_t> Snapshot(void *object, uint32_t size)
{
    std::vector<uint32_t> out;
    for (uint32_t o = 0; o < size; o += 4) out.push_back(W(object, o));
    return out;
}

// What is left of the object after destruction, vtable words aside.
std::vector<uint32_t> Remains(void *object, const std::vector<uint32_t> &before, uint32_t size = cTSPersistSystem::kSize)
{
    std::vector<uint32_t> out;
    for (uint32_t o = 0; o < size; o += 4) {
        if (IsVTableWord(o)) continue;
        const uint32_t v = W(object, o);
        if (IsPointerWord(o)) out.push_back(v == 0 ? 0xe0 : v == before[o / 4] ? 0xe1 : 0xe2);
        else out.push_back(v);
    }
    return out;
}

} // namespace

DIFF_TEST(TSPersistSystemFX_Construct, 0x1d8b90)
{
    (void)OurVTables();
    for (int i = 0; i < 20; ++i) {
        void *a = BuildOriginal(ctx);
        void *b = BuildOurs();
        ++ctx.cases;
        FirstDifference(Dump(a, false), Dump(b, true), ctx, "constructed object");
        ctx.Original<void(__thiscall *)(void *)>(kDtor)(a);
        cTSPersistSystemAdapters::Destruct(static_cast<cTSPersistSystem *>(b), nullptr);
        ::operator delete(a);
        ::operator delete(b);
        if (!ctx.failures.empty()) return;
    }
    // The base's own constructor on its own (0x9b3f30; the game calls it only from 0x1d8b90): the first 0xa8 bytes.
    for (int i = 0; i < 5; ++i) {
        void *a = NewBuffer();
        void *b = NewBuffer();
        ctx.Original<void *(__thiscall *)(void *)>(0x9b3f30)(a);
        gz::cGZPersistResourceManagerAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (W(a, 0) != P(GameAddress<void>(0xe60274)) || W(b, 0) == W(a, 0) || W(a, 0xa0) != W(b, 0xa0) || W(a, 0xa4) != W(b, 0xa4)
            || W(a, 0x94) != W(b, 0x94) || W(a, 0x84) != W(b, 0x84) || std::memcmp(static_cast<uint8_t *>(a) + 0xc, static_cast<uint8_t *>(b) + 0xc, 0x14) != 0)
            ctx.Fail("base constructor state differs");
        const auto beforeA = Snapshot(a, 0xa8), beforeB = Snapshot(b, 0xa8);
        ctx.Original<void(__thiscall *)(void *)>(0x9b4120)(a);
        gz::cGZPersistResourceManagerAdapters::Destruct(static_cast<gz::cGZPersistResourceManager *>(b), nullptr);
        FirstDifference(Remains(a, beforeA, 0xa8), Remains(b, beforeB, 0xa8), ctx, "base destroyed");
        ::operator delete(a);
        ::operator delete(b);
    }
    // The base's deleting destructor (0x9b41d0; never reached in the game): flags 0 keeps the object, flags 1 frees it.
    for (uint32_t flags = 0; flags < 4; ++flags) {
        void *a = NewBuffer();
        void *b = NewBuffer();
        ctx.Original<void *(__thiscall *)(void *)>(0x9b3f30)(a);
        gz::cGZPersistResourceManagerAdapters::Construct(b, nullptr);
        const auto beforeA = Snapshot(a, 0xa8), beforeB = Snapshot(b, 0xa8);
        std::vector<void *> blocks[2];
        for (int side = 0; side < 2; ++side) {
            void *o = side ? b : a;
            for (uint32_t m : {0x28u, 0x48u}) {
                blocks[side].push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(o, m + 4))));
                blocks[side].push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(o, m + 0xc))));
            }
            blocks[side].push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(o, 0x70))));
            blocks[side].push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(W(o, 0x90))));
            blocks[side].push_back(o);
        }
        std::vector<uint32_t> freedA(blocks[0].size()), freedB(blocks[1].size());
        difftest::FreeRecorder recA;
        void *ra = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x9b41d0)(a, flags);
        recA.Stop();
        Freed(recA, blocks[0], freedA);
        // Ours through our +8 face's slot 3 (the compiler thunk, as the game's 0x9b41c4): the adapter is gone (lane GT).
        difftest::FreeRecorder recB;
        void *rb = reinterpret_cast<void *>(static_cast<uintptr_t>(CallSlot(b, 8, 3, 1, &flags)));
        recB.Stop();
        Freed(recB, blocks[1], freedB);
        ++ctx.cases;
        if ((ra == a) != (rb == b)) ctx.Fail("base deleting destructor flags %u: result", flags);
        if (freedA != freedB) ctx.Fail("base deleting destructor flags %u: freed blocks differ", flags);
        if (!(flags & 1)) {
            FirstDifference(Remains(a, beforeA, 0xa8), Remains(b, beforeB, 0xa8), ctx, "base deleting destructor, flags 0");
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

DIFF_TEST(TSPersistSystemFX_Destruct, 0x1d9cb0)
{
    std::mt19937 rng(0xf8);
    ServiceScope service(ctx);
    for (int seed = 0; seed < 400; ++seed) {
        World w;
        w.present = rng() & 0xff;
        w.listeners = rng() % 3;
        w.entries = (rng() % 4) ? (rng() | 1) : 0;
        const unsigned route = seed % 4; // 0 Destruct adapter, 1 our +8 slot 3 (the compiler thunk, game RVA 0x1da053) flags 0, 2 the same with flags 1, 3 adapters on a game-built object
        const bool freeIt = route == 2 || (route == 3 && (rng() & 1));

        void *a = BuildOriginal(ctx);
        Populate(a, w);
        auto blocksA = Blocks(a);
        PopulateBase(a, seed, blocksA);
        if (w.entries) PopulateEntries(a, w.entries, blocksA);
        const auto beforeA = Snapshot(a, cTSPersistSystem::kSize);
        std::vector<uint32_t> freedA, freedB;
        gLog.clear();
        gLog.reserve(1024); // the log does not allocate (or free) inside the recorded call
        difftest::FreeRecorder recA;
        if (freeIt) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(a, 1);
        else ctx.Original<void(__thiscall *)(void *)>(kDtor)(a);
        recA.Stop();
        const std::string logA = gLog;
        Freed(recA, blocksA, freedA);
        const bool selfFreedA = freeIt && recA.Freed(a);
        const auto remainsA = freeIt ? std::vector<uint32_t>() : Remains(a, beforeA);

        void *b = route == 3 ? BuildOriginal(ctx) : BuildOurs();
        Populate(b, w);
        auto blocksB = Blocks(b);
        PopulateBase(b, seed, blocksB);
        if (w.entries) PopulateEntries(b, w.entries, blocksB);
        const auto beforeB = Snapshot(b, cTSPersistSystem::kSize);
        TrapTables traps;
        if (route == 3) traps.Install(b);
        gLog.clear();
        gLog.reserve(1024); // the log does not allocate (or free) inside the recorded call
        gTrapHits = 0;
        auto *self = static_cast<cTSPersistSystem *>(b);
        difftest::FreeRecorder recB;
        if (route == 0) {
            cTSPersistSystemAdapters::Destruct(self, nullptr);
        } else if (route == 3) {
            // The deleting destructor's adapter is gone (lane GT: only the game's vtables reach 0x1da310): a game-built
            // object is destroyed by the Destruct adapter, then freed as the original's deleting destructor frees it.
            cTSPersistSystemAdapters::Destruct(self, nullptr);
            if (freeIt) ::operator delete(self, cTSPersistSystem::kSize);
        } else {
            const uint32_t flags = route == 2 ? 1 : 0;
            CallSlot(b, 8, 3, 1, &flags);
        }
        recB.Stop();
        const std::string logB = gLog;
        Freed(recB, blocksB, freedB);
        const bool selfFreedB = freeIt && recB.Freed(b);
        const auto remainsB = freeIt ? std::vector<uint32_t>() : Remains(b, beforeB);
        ++ctx.cases;
        if (recA.Overflowed() || recB.Overflowed()) ctx.Fail("seed %d: the free recorder overflowed", seed);
        if (logA != logB) ctx.Fail("seed %d route %u: releases '%s' vs '%s'", seed, route, logA.c_str(), logB.c_str());
        for (size_t i = 0; i < freedA.size() && i < freedB.size(); ++i)
            if (freedA[i] != freedB[i]) {
                ctx.Fail("seed %d route %u free %d: block %zu freed %u vs %u", seed, route, int(freeIt), i, freedA[i], freedB[i]);
                break;
            }
        if (freedA.size() != freedB.size()) ctx.Fail("seed %d route %u: %zu vs %zu blocks", seed, route, freedA.size(), freedB.size());
        if (selfFreedA != selfFreedB) ctx.Fail("seed %d route %u: object freed %d vs %d", seed, route, selfFreedA, selfFreedB);
        if (gTrapHits) ctx.Fail("seed %d route %u: %d trap hits (an adapter dispatched through the vtable)", seed, route, gTrapHits);
        if (!freeIt) FirstDifference(remainsA, remainsB, ctx, "destroyed object");
        if (!freeIt) {
            ::operator delete(a);
            ::operator delete(b);
        }
        if (!ctx.failures.empty()) return;
    }
}

namespace {

enum Kind : uint8_t { kWord, kByte, kBool, kVoid };

struct Forward {
    uint8_t face;     // index into kFaces
    uint8_t slot;
    uint32_t rva;     // what the game's vtable holds there
    void *body;       // the hook-shaped body the slot must reach
    uint32_t self;    // the `this` the body takes, as an offset in the object
    Kind kind;
};

using Base = ::cGZPersistResourceManager;

std::vector<Forward> Forwards()
{
    std::vector<Forward> f;
    auto add = [&](uint8_t face, uint8_t slot, uint32_t rva, void *body, uint32_t self, Kind kind) { f.push_back({face, slot, rva, body, self, kind}); };
    // QueryInterface on every face but the translation provider's: the primary body (the thunks adjust).
    add(0, 0, 0x1e2e50, psshape::OurVTable(0)[0], 0, kBool);
    add(1, 0, 0x1e2ed7, psshape::OurVTable(0)[0], 0, kBool);
    add(2, 0, 0x1e2edf, psshape::OurVTable(0)[0], 0, kBool);
    add(3, 0, 0x1e2ee7, psshape::OurVTable(0)[0], 0, kBool);
    add(4, 0, 0x1e2ef2, psshape::OurVTable(0)[0], 0, kBool);
    // Primary slots of cTSPersistSystem.
    add(0, 29, 0x1e36e0, MemberCode(&Base::vf27), 0, kByte);
    // Primary slots the base's bodies serve.
    add(0, 6, 0x9b4c20, MemberCode(&Base::vf6), 0, kByte);
    add(0, 7, 0x9b4b70, MemberCode(&Base::vf7), 0, kBool);
    add(0, 8, 0x9b5460, MemberCode(&Base::vf8), 0, kByte);
    add(0, 9, 0x9b5430, MemberCode(&Base::vf9), 0, kWord);
    add(0, 10, 0x9b5d30, MemberCode(&Base::vf10), 0, kByte);
    add(0, 11, 0x9b4f80, MemberCode(&Base::vf11), 0, kByte);
    add(0, 12, 0x9b5560, MemberCode(&Base::vf12), 0, kByte);
    add(0, 13, 0x9b55b0, MemberCode(&Base::vf13), 0, kBool);
    add(0, 14, 0x9b5610, MemberCode(&Base::vf14), 0, kBool);
    add(0, 16, 0x9b4d90, MemberCode(&Base::vf16), 0, kWord);
    add(0, 17, 0x9b4df0, MemberCode(&Base::vf17), 0, kWord);
    add(0, 18, 0x9b4970, MemberCode(&Base::vf18), 0, kWord);
    add(0, 20, 0x9b5380, MemberCode(&Base::vf20), 0, kBool);
    add(0, 21, 0x9b5b20, MemberCode(&Base::vf21), 0, kBool);
    add(0, 22, 0x9b47d0, MemberCode(&Base::vf22), 0, kBool);
    add(0, 23, 0x9b4790, MemberCode(&Base::vf23), 0, kWord);
    add(0, 24, 0x9b47b0, MemberCode(&Base::vf24), 0, kWord);
    add(0, 25, 0x9b4b50, MemberCode(&Base::vf25), 0, kWord);
    add(0, 26, 0x9b4b10, MemberCode(&Base::vf26), 0, kWord);
    add(0, 27, 0x1e36e0, MemberCode(&Base::vf27), 0, kWord);
    add(0, 31, 0x9b5940, MemberCode(&Base::vf31), 0, kByte);
    add(0, 32, 0x9b4890, MemberCode(&Base::vf32), 0, kByte);
    add(0, 33, 0x9b46f0, MemberCode(&Base::vf33), 0, kByte);
    add(0, 35, 0x9b4f50, MemberCode(&Base::vf35), 0, kWord);
    add(0, 36, 0x9b4ed0, MemberCode(&Base::vf36), 0, kWord);
    add(0, 37, 0x9b45f0, MemberCode(&Base::vf37), 0, kByte);
    add(0, 38, 0x9b4520, MemberCode(&Base::vf38), 0, kWord);
    add(0, 40, 0x9b43e0, MemberCode(&Base::vf40), 0, kByte);
    add(0, 41, 0x9b4290, MemberCode(&Base::vf41), 0, kByte);
    add(0, 42, 0x9b54e0, MemberCode(&Base::vf42), 0, kByte);
    add(0, 43, 0x9b4ff0, MemberCode(&Base::vf43), 0, kByte);
    add(0, 44, 0x9b5740, MemberCode(&Base::vf44), 0, kVoid);
    add(0, 45, 0x9b4920, MemberCode(&Base::vf45), 0, kByte);
    add(0, 46, 0x9b5790, MemberCode(&Base::vf46), 0, kVoid);
    add(0, 47, 0x9b4f20, MemberCode(&Base::vf47), 0, kWord);
    add(0, 48, 0x9b4400, MemberCode(&Base::vf48), 0, kWord);
    add(0, 50, 0x9b4360, MemberCode(&Base::vf50), 0, kByte);
    add(0, 51, 0x9b44c0, MemberCode(&Base::vf51), 0, kByte);
    // The system service face (+4).
    // cITSPersistSystem (+0xa8).
    // cIGZFrameWorkHooks (+0xac) and the translation provider (+0xb0, slot 0 = ITS slot 10 through the thunk 0x1e012a).
    add(5, 0, 0x1e012a, psshape::OurVTable(3)[10], 0xa8, kWord);
    return f;
}

// Slots not forwarded to a body: called on both sides' objects, the result and the object's words compared.
struct Direct {
    uint8_t face;
    uint8_t slot;
    uint32_t rva;      // the game's slot (checked against its vtable); 0 for the folded shared bodies listed by classinfo
    Kind kind;
    uint32_t setOffset; // a byte or dword prepared before the call (0 = none)
    uint32_t setValue;
};

const Direct kDirect[] = {
    {0, 1, 0x1dab30, kWord, 0xc, 5}, {0, 2, 0x1e3980, kWord, 0xc, 5},                                  // AddRef / Release
    {1, 1, 0x1dab35, kWord, 0xc, 5}, {1, 2, 0x1e3985, kWord, 0xc, 5}, {2, 1, 0x1dab3d, kWord, 0xc, 5},
    {2, 2, 0x1e398d, kWord, 0xc, 5}, {3, 1, 0x1dab45, kWord, 0xc, 5}, {3, 2, 0x1e3995, kWord, 0xc, 5},
    {4, 1, 0x1dab50, kWord, 0xc, 5}, {4, 2, 0x1e39a0, kWord, 0xc, 5}, {5, 1, 0x1e39ab, kWord, 0xc, 5},
    {1, 3, 0x145770, kWord, 0x10, 0x12345678}, {1, 4, 0x149c60, kVoid, 0, 0}, {1, 6, 0x156110, kByte, 0x1c, 0x80},
    {1, 7, 0x1b9690, kVoid, 0, 0}, {1, 12, 0x149d00, kWord, 0x18, 0x7654321},                          // rz service defaults
    {2, 4, 0x1b9544, kWord, 0xc, 5}, {2, 5, 0x147a30, kWord, 0xc, 7},                                  // RemoveRef / RefCount
    {3, 15, 0x1b9a50, kVoid, 0, 0}, {3, 19, 0x1e4d40, kVoid, 0, 0}, {3, 60, 0x1b7090, kByte, 0xb4, 0x80},
    {3, 65, 0x1e4da0, kVoid, 0, 0}, {3, 66, 0x1dff80, kWord, 0x1e0, 0x13579bdf},                       // ITS folded slots
    {4, 3, 0x144910, kByte, 0, 0}, {4, 4, 0x144910, kByte, 0, 0}, {4, 5, 0x144910, kByte, 0, 0},       // hooks `mov al, 1`
    {4, 7, 0x144910, kByte, 0, 0}, {4, 8, 0x144910, kByte, 0, 0}, {4, 9, 0x144910, kByte, 0, 0},
    {4, 10, 0x144910, kByte, 0, 0},
};

uint32_t Expected(Kind kind, uint32_t r)
{
    switch (kind) {
    case kWord: return r;
    case kByte: return r & 0xff;
    case kBool: return (r & 0xff) != 0;
    default: return 0;
    }
}

} // namespace

DIFF_TEST(TSPersistSystemFX_Slots, 0x1e012a)
{
    void *ours = BuildOurs();
    // Forwarding slots.
    const auto forwards = Forwards();
    std::vector<std::vector<bool>> seen(6);
    for (unsigned f = 0; f < 6; ++f) seen[f].assign(kSlotCounts[f], false);
    for (const Forward &fw : forwards) {
        const unsigned words = kWords[fw.face][fw.slot];
        ++ctx.cases;
        seen[fw.face][fw.slot] = true;
        void **game = GameAddress<void *>(kGameVTables[fw.face]);
        if (fw.rva && game[fw.slot] != GameAddress<void>(fw.rva))
            ctx.Fail("face +%x slot %u: the game's vtable holds %08x, not the table's RVA %x", kFaces[fw.face], fw.slot, P(game[fw.slot]), fw.rva);
        uint32_t args[8];
        for (unsigned i = 0; i < 8; ++i) args[i] = 0x10203040u * (i + 1) + fw.slot * 0x101u + fw.face;
        gHits = 0;
        gSelf = 0;
        uint32_t r;
        {
            CodePatch patch(fw.body, RecorderFor(words));
            r = CallSlot(ours, kFaces[fw.face], fw.slot, words, args);
        }
        if (gHits != 1) {
            ctx.Fail("face +%x slot %u: the body was reached %d times", kFaces[fw.face], fw.slot, gHits);
            continue;
        }
        if (gSelf != P(static_cast<uint8_t *>(ours) + fw.self))
            ctx.Fail("face +%x slot %u: the body got this = object + %x, not + %x", kFaces[fw.face], fw.slot,
                     gSelf - P(ours), fw.self);
        if (gWords != words || std::memcmp(gArgs, args, 4 * words) != 0)
            ctx.Fail("face +%x slot %u: the arguments changed on the way", kFaces[fw.face], fw.slot);
        if (fw.kind != kVoid && Expected(fw.kind, r) != Expected(fw.kind, kRecorded))
            ctx.Fail("face +%x slot %u: result %08x", kFaces[fw.face], fw.slot, r);
    }
    // The class's own slot bodies (lane GD): each has its difftest on both routes (PS_TEST: our vtable's slot where the game's
    // vtable holds the RVA, and the adapter), so here they are only marked.
    using namespace psrva;
    const uint32_t kClassSlots[] = {
        GetServicePriority, GetResourceManager, QueueResourceLoad, IsResourceAvailable, IsResourcePending, ClearDeferWrites,
        LockSystem, UnlockSystem, StopBackgroundOpen, GetResourceSpeculative, GetGroupIdForPath, DeleteResource, OpenSegment,
        FlushSegmentByInfo, GetSegmentPath, RegisterSegmentForKey, GetRecordChangeID, QueryInterface, vf56,
        GetResourceFromFileRef, GetKeyedObject, vf3, vf4, vf5, GetResource, vf49, vf52, ClearSpeculativeLoader, OnIdle,
        AgeCaches, PurgeCaches, OpenRecordFromFileRef, CreateRecord, OpenSegmentFromValues, ScoreSegment, CommitSegments,
        AddListener, RemoveListener, CommitRecordToFileRef, WriteResourceToFileRef, ReadResourceFromSegment, GetAnimTable,
        PreAppShutdown, OnTick, SaveResource, ReopenSegment, GetWritableSegments, RegisterSegment, vf19, vf34, vf15,
        GetSegmentsForGroup, SegmentHasGroup, FindSegment, ListKeysOfType, ListKeys, GetPackedFileGroup, ReopenSegmentsForPath,
        HasSegmentForFile, vf53, RemoveSegmentsWithPrefix, SetKeyedObject, vf168_56, NoteResourceChange, StartBackgroundOpen,
        GetGroupIdForNameChars, GetGroupIdForFileName, GetGroupIdForPathChars, FlushRollbackSegments, CloseAllSegments,
        FindBestSegment, Shutdown, ReleaseSegmentsForFile, OpenContentPaths, Init, AddCategoryPath, CollectPathGroups,
        OpenSegmentByInfo, CopyResource, CopySegmentResources, IsSegmentWritable, vf28, vf30, DeleteGroupFiles,
        CommitSegmentsWithBackup, AddPackageFile, SetResourceName, GetResourceName, GetCachedResource, vf39};
    for (uint32_t rva : kClassSlots) {
        unsigned f = 0, s = 0;
        ++ctx.cases;
        if (!psshape::FindSlot(rva, f, s)) ctx.Fail("%x is in no slot of the game's vtables", rva);
        else seen[f][s] = true;
    }
    // The secondary faces' QueryInterface thunks (their own test is TSPersistSystem2_Thunks) are in the forwards above.
    // Direct slots: both sides' objects.
    void *orig = BuildOriginal(ctx);
    for (const Direct &d : kDirect) {
        const unsigned words = kWords[d.face][d.slot];
        ++ctx.cases;
        seen[d.face][d.slot] = true;
        void **game = GameAddress<void *>(kGameVTables[d.face]);
        if (game[d.slot] != GameAddress<void>(d.rva))
            ctx.Fail("face +%x slot %u: the game's vtable holds %08x, not %x", kFaces[d.face], d.slot, P(game[d.slot]), d.rva);
        uint32_t results[2];
        std::vector<uint32_t> words2[2];
        void *objects[2] = {orig, ours};
        for (int side = 0; side < 2; ++side) {
            void *o = objects[side];
            if (d.setOffset) {
                if (d.kind == kByte) *(static_cast<uint8_t *>(o) + d.setOffset) = static_cast<uint8_t>(d.setValue);
                else W(o, d.setOffset) = d.setValue;
            }
            const uint32_t args[2] = {0x9abcdef1u + d.slot, 0x2468ace0u};
            results[side] = CallSlot(o, kFaces[d.face], d.slot, words, args);
            for (uint32_t off : {0xcu, 0x10u, 0x18u, 0x1cu, 0xb4u, 0xf0u, 0x1e0u}) words2[side].push_back(W(o, off));
        }
        if (d.kind != kVoid && Expected(d.kind, results[0]) != Expected(d.kind, results[1]))
            ctx.Fail("face +%x slot %u: original %08x, ours %08x", kFaces[d.face], d.slot, results[0], results[1]);
        if (words2[0] != words2[1]) ctx.Fail("face +%x slot %u: the object's fields differ after the call", kFaces[d.face], d.slot);
    }
    // The deleting destructor (+8 slot 3) is the Destruct test's; every other slot must be in one of the tables.
    seen[2][3] = true;
    for (unsigned f = 0; f < 6; ++f)
        for (unsigned s = 0; s < kSlotCounts[f]; ++s)
            if (!seen[f][s]) ctx.Fail("face +%x slot %u is not covered", kFaces[f], s);
    W(orig, 0xc) = 0;
    W(ours, 0xc) = 0;
    ctx.Original<void(__thiscall *)(void *)>(kDtor)(orig);
    cTSPersistSystemAdapters::Destruct(static_cast<cTSPersistSystem *>(ours), nullptr);
    ::operator delete(orig);
    ::operator delete(ours);
}
