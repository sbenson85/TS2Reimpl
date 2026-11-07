// Differential tests for src/gz/cGZRESIDtoExtensionMap.* (standalone shape, slot-hooked).
//
// Three routes run the same random scripts on two maps each:
//   original: the game's vtable 0xe5ed38, every call through its slot (the original bodies);
//   ours:     our constructor, every call through our vtable's slot;
//   adapters: an object with a copy of the game's vtable whose hooked slots 0, 4, 5, 7 and 8 are traps and 3 and 6 the
//             adapters (what the game's hooked slots reach: CopyFrom's own calls to them are legitimate), each call made
//             on the adapter directly. A hit trap fails the test (an adapter that dispatched virtually).
// The original never constructs the map itself (cGZDBSegmentDirectoryFiles's constructors build it inline: vtable, an
// empty std::multimap), so its object is ours with the game's vtable word; that word is the only one normalised. The
// extensions passed in are fakes (cIGZString slots 4..7 answered and logged, every other slot a trap), so the slot
// calls each body makes, their order and the text handed to FromChar are compared; the maps are compared entry by entry
// (key, the node string's vtable word, refcount, text and capacity), with refcounts, results and out-parameters.
// The game's operator new / sized delete (0x824632 / 0x824624) stand in for ours; the deletes note the size the object
// was freed with.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZRESIDtoExtensionMap.h"
#include "shape.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kGameVTable = 0xe5ed38;
constexpr uint32_t kSlotCount = 10;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kGameSizedDelete = 0x824624;

constexpr uint32_t kQueryInterface = 0x9a2690;
constexpr uint32_t kAddExtension = 0x99f0f0;
constexpr uint32_t kGetTypeForExtension = 0x9a0dd0;
constexpr uint32_t kGetExtensionForType = 0x9a26c0;
constexpr uint32_t kClear = 0x9a0d40;
constexpr uint32_t kCopyFrom = 0x99faa0;
constexpr uint32_t kDeletingDestructor = 0x99ecd0;

constexpr uint32_t kOutSentinel = 0x5eed5eed;

enum Route { kOriginal = 0, kOurs = 1, kAdapters = 2, kRoutes = 3 };

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

// ---- traps ----

int gTrapHits = 0;
uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    return 0;
}

// ---- the fake extension string (cIGZString slots 4..7; the rest traps) ----

std::vector<std::string> *gLog = nullptr;

void Log(const std::string &line)
{
    if (gLog != nullptr) gLog->push_back(line);
}

struct FakeString {
    void **vtable;
    std::string text;
    uint32_t length; // what Strlen answers (the text's size)
};

void __fastcall FakeFromChar(FakeString *s, void *, const char *text)
{
    Log("FromChar(" + std::string(text) + ")");
    s->text = text;
    s->length = static_cast<uint32_t>(s->text.size());
}
const char *__fastcall FakeToChar(FakeString *s, void *)
{
    Log("ToChar");
    return s->text.c_str();
}
const char *__fastcall FakeData(FakeString *s, void *)
{
    Log("Data");
    return s->text.c_str();
}
uint32_t __fastcall FakeStrlen(FakeString *s, void *)
{
    Log("Strlen");
    return s->length;
}

void **FakeStringVTable()
{
    static void *table[40];
    static bool done = false;
    if (!done) {
        for (void *&slot : table) slot = shape::TrapFor<&TrapHit>(shape::kMaxTrapWords);
        table[4] = reinterpret_cast<void *>(&FakeFromChar);
        table[5] = reinterpret_cast<void *>(&FakeToChar);
        table[6] = reinterpret_cast<void *>(&FakeData);
        table[7] = reinterpret_cast<void *>(&FakeStrlen);
        done = true;
    }
    return table;
}

struct Fake {
    FakeString s;
    explicit Fake(const std::string &text) : s{FakeStringVTable(), text, static_cast<uint32_t>(text.size())} {}
    const cIGZString *Face() const { return reinterpret_cast<const cIGZString *>(&s); }
    cIGZString *Face() { return reinterpret_cast<cIGZString *>(&s); }
};

// ---- the routes' objects ----

void **AdapterVTable()
{
    static void *table[kSlotCount];
    static bool done = false;
    if (!done) {
        void **game = shape::GameVTable(kGameVTable);
        for (uint32_t i = 0; i < kSlotCount; ++i) table[i] = game[i];
        table[0] = shape::TrapFor<&TrapHit>(2);
        table[3] = reinterpret_cast<void *>(&cGZRESIDtoExtensionMapAdapters::AddExtension);
        table[4] = shape::TrapFor<&TrapHit>(2);
        table[5] = shape::TrapFor<&TrapHit>(2);
        table[6] = reinterpret_cast<void *>(&cGZRESIDtoExtensionMapAdapters::Clear);
        table[7] = shape::TrapFor<&TrapHit>(1);
        table[8] = shape::TrapFor<&TrapHit>(1);
        done = true;
    }
    return table;
}

void *OurVTable()
{
    static void *table = nullptr;
    if (table == nullptr) {
        alignas(8) static uint8_t memory[cGZRESIDtoExtensionMap::kSize];
        auto *map = ::new (memory) cGZRESIDtoExtensionMap;
        table = shape::VTableAt(memory, 0);
        map->~cGZRESIDtoExtensionMap();
    }
    return table;
}

cGZRESIDtoExtensionMap *Build(Route route, uint32_t refCount)
{
    auto *map = ::new (::operator new(cGZRESIDtoExtensionMap::kSize)) cGZRESIDtoExtensionMap;
    map->mRefCount = refCount;
    if (route == kOriginal) shape::VTableAt(map, 0) = shape::GameVTable(kGameVTable);
    if (route == kAdapters) shape::VTableAt(map, 0) = AdapterVTable();
    return map;
}

void *OwnVTable(Route route)
{
    if (route == kOurs) return OurVTable();
    if (route == kAdapters) return AdapterVTable();
    return shape::GameVTable(kGameVTable);
}

template <class Fn> Fn Slot(void *object, uint32_t slot)
{
    return reinterpret_cast<Fn>(shape::VTableAt(object, 0)[slot]);
}

// ---- state ----

std::string Dump(const cGZRESIDtoExtensionMap *map, Route route)
{
    char line[160];
    const uint32_t vt = shape::Word(map, 0);
    std::snprintf(line, sizeof line, "vt %s ref %u size %u:", vt == shape::P(OwnVTable(route)) ? "own" : "OTHER",
                  map->mRefCount, static_cast<uint32_t>(map->mMap.size()));
    std::string out = line;
    for (const auto &entry : map->mMap) {
        const cRZString &s = entry.second.s;
        std::snprintf(line, sizeof line, " [%08x vt %08x ref %u cap %u '", entry.first, shape::Word(&s, 0), s.mRefCount,
                      static_cast<uint32_t>(s.mString.capacity()));
        out += line;
        out.append(s.mString.data(), s.mString.size());
        out += "']";
    }
    return out;
}

// ---- the script ----

const char *const kTexts[] = {"bmp",       "BMP",       ".bmp",      "png",      ".PNG",  "",          ".",
                              "jpg",       "jpeg",      "6534284A",  ".6534284a", "1234567", "123456789", "zzzzzzzz",
                              ".zzzzzzzz", "a.b",       " 1a2b3c4",  "0x123456", "fsh",   ".fsh.",     "Fsh",
                              "..fsh",     "ABCDEF01",  ".abcdef01", "simpe.bmp"};
const uint32_t kTypes[] = {0x856ddbac, 0x6534284a, 0, 1, 0xffffffff, 0x7b1acfcd, 0x856ddbac};
const uint32_t kIids[] = {1, cIGZPersistRESIDtoExtensionMap::kIid, 0x12345678, 0, 0xa56c8cfc};

enum Op { kOpAdd, kOpType, kOpExtension, kOpClear, kOpCopy, kOpQuery, kOpAddRef, kOpRelease, kOpCount, kOps };

struct Step {
    Op op;
    int map, other;
    uint32_t type, iid;
    std::string text;
};

template <class T, size_t N> const T &Pick(std::mt19937 &r, const T (&list)[N])
{
    return list[r() % N];
}

std::string PickText(std::mt19937 &r)
{
    std::string text = Pick(r, kTexts);
    if (r() % 16 == 0) text.insert(text.size() / 2, 1, '\0'); // a NUL inside the counted text
    return text;
}

struct Outcome {
    uint32_t result = 0, out = 0;
    std::string state;
    std::vector<std::string> log;
};

// One step on route `route`; maps[] are that route's two maps.
Outcome Run(cGZRESIDtoExtensionMap *const maps[2], Route route, const Step &step)
{
    Outcome o;
    gLog = &o.log;
    cGZRESIDtoExtensionMap *map = maps[step.map];
    const bool direct = route == kAdapters;
    using A = cGZRESIDtoExtensionMapAdapters;
    switch (step.op) {
    case kOpAdd: {
        Fake text(step.text);
        o.result = direct ? A::AddExtension(map, nullptr, step.type, text.Face())
                          : Slot<uint8_t(__thiscall *)(void *, uint32_t, const cIGZString *)>(map, 3)(map, step.type,
                                                                                                     text.Face());
        o.result &= 0xff;
        break;
    }
    case kOpType: {
        Fake text(step.text);
        uint32_t type = kOutSentinel;
        o.result = direct ? A::GetTypeForExtension(map, nullptr, text.Face(), &type)
                          : Slot<uint8_t(__thiscall *)(void *, const cIGZString *, uint32_t *)>(map, 4)(map, text.Face(),
                                                                                                       &type);
        o.result &= 0xff;
        o.out = type;
        break;
    }
    case kOpExtension: {
        Fake out("unchanged");
        const uint32_t type = step.type;
        o.result = direct ? A::GetExtensionForType(map, nullptr, &type, out.Face())
                          : Slot<uint8_t(__thiscall *)(void *, const uint32_t *, cIGZString *)>(map, 5)(map, &type,
                                                                                                       out.Face());
        o.result &= 0xff;
        Log("out " + out.s.text);
        break;
    }
    case kOpClear:
        if (direct) A::Clear(map, nullptr);
        else Slot<void(__thiscall *)(void *)>(map, 6)(map);
        break;
    case kOpCopy: {
        const cGZRESIDtoExtensionMap *other = maps[step.other];
        if (direct) A::CopyFrom(map, nullptr, other);
        else Slot<void(__thiscall *)(void *, const void *)>(map, 7)(map, other);
        break;
    }
    case kOpQuery: {
        void *out = reinterpret_cast<void *>(static_cast<uintptr_t>(kOutSentinel));
        o.result = direct ? A::QueryInterface(map, nullptr, step.iid, &out)
                          : Slot<uint8_t(__thiscall *)(void *, uint32_t, void **)>(map, 0)(map, step.iid, &out);
        o.result &= 0xff;
        o.out = out == map ? 0x5e1f : shape::P(out);
        break;
    }
    case kOpAddRef:
        o.result = Slot<uint32_t(__thiscall *)(void *)>(map, 1)(map);
        break;
    case kOpRelease:
        o.result = Slot<uint32_t(__thiscall *)(void *)>(map, 2)(map);
        break;
    case kOpCount:
        o.result = Slot<uint32_t(__thiscall *)(void *)>(map, 9)(map);
        break;
    default:
        break;
    }
    gLog = nullptr;
    o.state = Dump(maps[0], route) + " | " + Dump(maps[1], route);
    return o;
}

// The end of a map: 0 the deleting destructor with flags 1, 1 with flags 0 (then freed by hand), 2 Release at count 1
// (the adapters route: the deleting destructor's adapter with flags 1, since its Release reaches the trapped slot 8).
struct End {
    uint32_t returned = 0;
    size_t freed = 0;
};

End Finish(cGZRESIDtoExtensionMap *map, Route route, int kind)
{
    End e;
    gWatched = map;
    gFreedSize = 0;
    const uint32_t flags = kind == 1 ? 0 : 1;
    if (kind == 2 && route != kAdapters) {
        map->mRefCount = 1;
        e.returned = Slot<uint32_t(__thiscall *)(void *)>(map, 2)(map);
    } else if (route == kAdapters) {
        e.returned = shape::P(cGZRESIDtoExtensionMapAdapters::DeletingDestructor(map, nullptr, flags)) == shape::P(map)
                         ? 0x5e1f
                         : 1;
    } else {
        e.returned = Slot<void *(__thiscall *)(void *, uint32_t)>(map, 8)(map, flags) == map ? 0x5e1f : 1;
    }
    e.freed = gFreedSize;
    gWatched = nullptr;
    if (kind == 1) ::operator delete(map);
    return e;
}

void Script(difftest::Context &ctx, int cases)
{
    Heap heap;
    std::mt19937 r(0x5e1d);
    for (int c = 0; c < cases && ctx.failures.empty(); ++c) {
        cGZRESIDtoExtensionMap *maps[kRoutes][2];
        const uint32_t refs[2] = {r() % 4, r() % 4};
        for (int route = 0; route < kRoutes; ++route)
            for (int m = 0; m < 2; ++m) maps[route][m] = Build(static_cast<Route>(route), refs[m]);
        ++ctx.cases;
        const int steps = 1 + static_cast<int>(r() % 40);
        for (int k = 0; k < steps && ctx.failures.empty(); ++k) {
            Step step{static_cast<Op>(r() % kOps), static_cast<int>(r() % 2), static_cast<int>(r() % 2),
                      Pick(r, kTypes), Pick(r, kIids), PickText(r)};
            if (step.op == kOpAdd || step.op == kOpType) step.text = PickText(r);
            if (step.op == kOpAdd && r() % 3 == 0) step.op = kOpType; // more lookups
            if (step.op == kOpRelease && maps[0][step.map]->mRefCount <= 1) step.op = kOpAddRef;
            gTrapHits = 0;
            Outcome o[kRoutes];
            for (int route = 0; route < kRoutes; ++route) o[route] = Run(maps[route], static_cast<Route>(route), step);
            if (gTrapHits != 0) ctx.Fail("case %d step %d op %d: %d trap hits", c, k, step.op, gTrapHits);
            for (int route = 1; route < kRoutes; ++route) {
                if (o[route].result != o[0].result || o[route].out != o[0].out)
                    ctx.Fail("case %d step %d op %d '%s' route %d: original %08x %08x, this route %08x %08x", c, k, step.op,
                             step.text.c_str(), route, o[0].result, o[0].out, o[route].result, o[route].out);
                if (o[route].state != o[0].state)
                    ctx.Fail("case %d step %d op %d route %d: state\n  %s\n  %s", c, k, step.op, route, o[0].state.c_str(),
                             o[route].state.c_str());
                if (o[route].log != o[0].log) {
                    std::string a, b;
                    for (const auto &s : o[0].log) a += s + "; ";
                    for (const auto &s : o[route].log) b += s + "; ";
                    ctx.Fail("case %d step %d op %d route %d: calls\n  %s\n  %s", c, k, step.op, route, a.c_str(), b.c_str());
                }
            }
        }
        for (int m = 0; m < 2; ++m) {
            const int kind = static_cast<int>(r() % 3);
            End e[kRoutes];
            gTrapHits = 0;
            for (int route = 0; route < kRoutes; ++route) e[route] = Finish(maps[route][m], static_cast<Route>(route), kind);
            if (kind == 2) e[kAdapters].returned = e[kOriginal].returned; // Release's result is the shared body's
            if (gTrapHits != 0) ctx.Fail("case %d map %d end %d: %d trap hits", c, m, kind, gTrapHits);
            for (int route = 1; route < kRoutes; ++route)
                if (e[route].returned != e[0].returned || e[route].freed != e[0].freed)
                    ctx.Fail("case %d map %d end %d route %d: original %08x freed 0x%zx, this route %08x freed 0x%zx", c, m,
                             kind, route, e[0].returned, e[0].freed, e[route].returned, e[route].freed);
        }
    }
}

} // namespace

// The script: QueryInterface 0x9a2690, AddExtension 0x99f0f0, GetTypeForExtension 0x9a0dd0, GetExtensionForType
// 0x9a26c0, Clear 0x9a0d40, CopyFrom 0x99faa0 and the deleting destructor 0x99ecd0, on three routes.
DIFF_TEST(RESIDtoExtensionMap_Script, 0x9a0dd0)
{
    Script(ctx, 1500);
}
