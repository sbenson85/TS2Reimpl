// Differential tests for cTSCatalog (src/ts/cTSCatalog.*), the build/buy catalogue, and cPatternParsingFilter.
//
// Standalone shape (docs/standalone-shape.md): the original side's catalogue is built by the original factory 0xc105a5 (game
// vtable) and every slot is called through the game's vtable; ours is built by cTSCatalog::Create (the factory hook's entry) and
// called through our vtable, the same slot index on both. Calls between slots go through each object's own vtable, so a run on
// the original side is the original code end to end and ours is ours end to end.
//
// A world (built from a seed) is a pool of fake products with the catalogue's face layout (a virtual base table pointer at +4,
// every face at +8 * index with its own table, so a face is itself product-like) whose slots log their calls and answer from
// per-product tables; fake resource parsers (built in the operator-new block by stand-ins over the 13 parser constructors) with
// per-file contents; stand-ins over the object and half-wall constructors (pool products), the pattern factory, the file
// reader 0xc13038, the pattern-key handler 0xc130db, the message constructor and server; and fake services in the game globals
// (service object, the two cached services, the definitions singleton, the allocator service). cRZFile::PathExists and
// cTSAppCommonImpl slot 5 (the file lists) are stood in on both sides.
//
// A run applies one random sequence of slot calls (Load first, Unload/Load again, queries, adds, removes, the loaders and
// apply passes called directly, AddRef/Release down to the deleting destructor) and compares after every call: the result,
// the event log (every fake call with its arguments; pointers normalised to pool product + face, the catalogue, the static
// fakes, parser ordinal, or "a stack address"), the catalogue's containers (sizes, capacities, elements, keys, defaults,
// values) and the reference count of every pool product and fake.
//
// Left out of the comparison: AddRef/Release calls on products are not logged one by one, only the counts after each call are
// compared. The original takes temporary references whose AddRef/Release pairs net to zero inside a call (e.g. AddFence
// 0xc0efae / 0xc0efd5 around the push_back at 0xc0efbc, AddPattern 0xc0f547 / 0xc0f573 around the insert at 0xc0f55a), and the
// caller holds its own reference for the whole call, so no product reaches zero inside one: the pairs' order is not
// observable. Allocations through the allocator service and operator new are not logged (the containers are compared).
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "game_allocator.h"
#include "stack_out.h"
#include "ts_persist_world.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "gz/cGZAny.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZFile.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSCatalog.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <random>
#include <string>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }
difftest::Context *gCtx = nullptr;

uint32_t TextHash(const char *s)
{
    if (!s) return 0xdead0000u;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h & 0x0fffffffu;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stubs: thiscall slots as __fastcall with a dummy edx, arity 0..4, all dispatching to a handler with (self, tag, args).
using Handler = uint32_t (*)(void *self, int tag, const uint32_t *args, int count);
template <Handler H, int Tag> uint32_t __fastcall T0(void *s, void *) { return H(s, Tag, nullptr, 0); }
template <Handler H, int Tag> uint32_t __fastcall T1(void *s, void *, uint32_t a0)
{
    const uint32_t a[] = {a0};
    return H(s, Tag, a, 1);
}
template <Handler H, int Tag> uint32_t __fastcall T2(void *s, void *, uint32_t a0, uint32_t a1)
{
    const uint32_t a[] = {a0, a1};
    return H(s, Tag, a, 2);
}
template <Handler H, int Tag> uint32_t __fastcall T3(void *s, void *, uint32_t a0, uint32_t a1, uint32_t a2)
{
    const uint32_t a[] = {a0, a1, a2};
    return H(s, Tag, a, 3);
}
template <Handler H, int Tag> uint32_t __fastcall T4(void *s, void *, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t a[] = {a0, a1, a2, a3};
    return H(s, Tag, a, 4);
}
uint32_t __fastcall Unexpected(void *self, void *)
{
    if (gCtx) gCtx->Fail("unexpected slot call on %p", self);
    return 0;
}
constexpr int kSlots = 112;
void FillTraps(void **vt)
{
    for (int i = 0; i < kSlots; ++i) vt[i] = reinterpret_cast<void *>(&Unexpected);
}

// ---------------------------------------------------------------------------------------------------------------------
// The product pool.
constexpr int kFaces = 8;
struct Sub {
    void **vt;
    int32_t *vbptr;
};
enum Kind { kGeneric, kDeck, kObject, kKinds };
enum Role : uint8_t {
    rPattern, rBrush, rPaint, rWall, rWallPart, rFence, rArch, rDeck, rDeckPart, rStairs, rFireplace, rAwning, rRoof,
    rRoofPattern, rUserTool, rExtra, rObject, rHalfWall, rRoles
};
constexpr int kPerRole[rRoles] = {24, 12, 12, 10, 40, 16, 12, 10, 20, 12, 12, 10, 12, 16, 10, 16, 24, 12};
struct Product {
    Sub sub[kFaces];
    int32_t vbt[kFaces][kFaces];
    uint32_t index;
    int32_t refs;
    int kind;
    Role role;
    uint32_t ret[kFaces][48];
    char text[3][40];
    char materialText[4];
    uint32_t defaultKey;
    uint32_t material;
    bool nameAfterMaterial;
    int materialMode; // 0 null name, 1 "", 2 "m"
    int parts[4];
    int nextPart;
    int qiAnswer[2]; // for the fence (0) and fence arch (1) resource types
    bool qiOk[2];
};
constexpr int kPool = [] {
    int n = 0;
    for (int c : kPerRole) n += c;
    return n;
}();
Product gPool[kPool];
int gRoleStart[rRoles];
void *gProductVt[kKinds][kFaces][kSlots];

int RoleIndex(Role r, int i) { return gRoleStart[r] + int(uint32_t(i) % uint32_t(kPerRole[r])); }
Product *P(int index) { return index < 0 ? nullptr : &gPool[index]; }
void *FaceAddr(Product *p, int f) { return reinterpret_cast<uint8_t *>(p) + 8 * f; }

// Pointer normalisation for the logs and dumps.
struct StaticFake;
extern StaticFake gFakes[];
extern const int kFakeCount;
void *gSideCatalog = nullptr;
std::vector<void *> gParsers; // live and dead parsers of the current side, by creation order
uint32_t gStackLow = 0, gStackHigh = 0;

uint32_t FakeCode(const void *p);
uint32_t Norm(uint32_t v)
{
    const auto lo = reinterpret_cast<uintptr_t>(&gPool[0]), hi = reinterpret_cast<uintptr_t>(&gPool[kPool]);
    if (v >= lo && v < hi) {
        const uint32_t off = v - uint32_t(lo);
        const uint32_t idx = off / sizeof(Product), within = off % sizeof(Product);
        if (within < 8 * kFaces && within % 8 == 0) return 0x40000000u | (idx << 4) | (within / 8);
        return 0x4f000000u | (idx << 12) | within;
    }
    if (v && v == reinterpret_cast<uintptr_t>(gSideCatalog)) return 0x3c000001u;
    if (const uint32_t code = FakeCode(reinterpret_cast<const void *>(uintptr_t(v)))) return code;
    for (size_t i = 0; i < gParsers.size(); ++i)
        if (v >= reinterpret_cast<uintptr_t>(gParsers[i]) && v < reinterpret_cast<uintptr_t>(gParsers[i]) + 0x40)
            return 0x3b000000u | uint32_t(i << 8) | (v - uint32_t(reinterpret_cast<uintptr_t>(gParsers[i])));
    if (v >= gStackLow && v < gStackHigh) return 0x3e000000u;
    return v;
}
uint32_t NormPtr(const void *p) { return Norm(uint32_t(reinterpret_cast<uintptr_t>(p))); }

Product *OwnerOf(void *self, int face)
{
    auto *p = reinterpret_cast<Product *>(static_cast<uint8_t *>(self) - 8 * face);
    if (p < &gPool[0] || p >= &gPool[kPool]) {
        gCtx->Fail("product slot called on a non-product %p (face %d)", self, face);
        return nullptr;
    }
    return p;
}

// Hands out `product` through an out pointer with a reference (the getter's AddRef).
void GiveProduct(void **out, Product *product)
{
    *out = product;
    if (product) ++product->refs;
}

enum : uint32_t {
    evSlot = 0x51000000, evFake = 0x52000000, evParser = 0x53000000, evExists = 0x54000000, evParse = 0x55000000,
    evKeys = 0x56000000, evList = 0x57000000, evCtor = 0x58000000, evOp = 0x59000000, evMalloc = 0x5a000000, evFilter = 0x5b000000,
    evNegative = 0x5c000000,
};

uint32_t ProductHandler(void *self, int tag, const uint32_t *a, int n)
{
    const int face = tag / 1000, slot = tag % 1000;
    Product *p = OwnerOf(self, face);
    if (!p) return 0;
    if (face == 1 && slot == 1) return uint32_t(++p->refs);
    if (face == 1 && slot == 2) {
        if (--p->refs < 0) Ev({evNegative, p->index});
        return uint32_t(p->refs);
    }
    gEv.push_back(evSlot | (p->index << 8) | (face << 5) | 0);
    gEv.push_back(uint32_t(slot));
    for (int i = 0; i < n; ++i) gEv.push_back(Norm(a[i]));
    switch (face * 1000 + slot) {
    case 1000: { // QueryInterface(type, &out): the default fence (0xaad51b06) or fence arch (0x0b4ce79b) answer
        const int which = a[0] == 0xaad51b06u ? 0 : 1;
        void **out = reinterpret_cast<void **>(uintptr_t(a[1]));
        GiveProduct(out, P(p->qiAnswer[which]));
        difftest::FillOut(out, 4);
        return p->qiOk[which];
    }
    case 3000: // SetId (an object face: its definition, which its slot 1 then gives, as RemoveObject looks it up by)
        if (p->kind == kObject) p->ret[3][1] = a[0];
        return 0;
    case 3001:
        if (p->kind == kObject) return p->ret[3][1]; // the object face's key
        p->defaultKey = a[0];
        return 0;
    case 3003:
        return p->defaultKey;
    case 2009:
        p->material = a[0];
        if (p->nameAfterMaterial) p->materialMode = 2;
        return 0;
    case 2031:
        return p->materialMode == 0 ? 0u : uint32_t(reinterpret_cast<uintptr_t>(p->materialMode == 1 ? "" : p->materialText));
    case 2026: { // (side, &a, &b, &c)
        for (int i = 1; i <= 3; ++i) {
            auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[i]));
            *out = p->index * 16 + a[0] * 4 + uint32_t(i);
            difftest::FillOut(out, 4);
        }
        return (p->ret[2][26] >> a[0]) & 1;
    }
    case 2034: { // (&resource)
        auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
        *out = p->ret[2][34] >> 1;
        difftest::FillOut(out, 4);
        return p->ret[2][34] & 1;
    }
    case 4022: // a wall's part
    case 11:   // a deck foundation's part
        if (face == 0 && p->kind != kDeck) break;
        {
            void **out = reinterpret_cast<void **>(uintptr_t(a[1]));
            GiveProduct(out, P(p->parts[p->nextPart++ % 4]));
            difftest::FillOut(out, 4);
            return 1;
        }
    default:
        break;
    }
    return p->ret[face][slot];
}

struct ProductSlot {
    int face, slot, arity;
};
// Every product slot the catalogue calls, with its arity (a kind may override one).
constexpr ProductSlot kProductSlots[] = {
    {0, 1, 0},  {0, 3, 1},  {0, 4, 0},  {0, 6, 0},  {0, 7, 0},  {0, 10, 1}, {0, 11, 0}, {0, 13, 0},
    {1, 0, 2},  {1, 1, 0},  {1, 2, 0},
    {2, 0, 3},  {2, 7, 1},  {2, 9, 1},  {2, 13, 0}, {2, 15, 1}, {2, 16, 1}, {2, 20, 0}, {2, 26, 4}, {2, 29, 0}, {2, 31, 0}, {2, 34, 1},
    {3, 0, 1},  {3, 1, 1},  {3, 2, 0},  {3, 3, 0},  {3, 4, 0},
    {4, 18, 1}, {4, 22, 2}, {4, 23, 0},
    {7, 0, 1},
};
template <int Tag> void *StubFor(int arity)
{
    switch (arity) {
    case 0: return reinterpret_cast<void *>(&T0<&ProductHandler, Tag>);
    case 1: return reinterpret_cast<void *>(&T1<&ProductHandler, Tag>);
    case 2: return reinterpret_cast<void *>(&T2<&ProductHandler, Tag>);
    case 3: return reinterpret_cast<void *>(&T3<&ProductHandler, Tag>);
    default: return reinterpret_cast<void *>(&T4<&ProductHandler, Tag>);
    }
}
template <size_t... I> void FillProductVtables(std::index_sequence<I...>)
{
    for (auto &kind : gProductVt)
        for (auto &face : kind) FillTraps(face);
    for (int k = 0; k < kKinds; ++k) {
        ((gProductVt[k][kProductSlots[I].face][kProductSlots[I].slot] =
              StubFor<kProductSlots[I].face * 1000 + kProductSlots[I].slot>(kProductSlots[I].arity)),
         ...);
    }
    gProductVt[kDeck][0][11] = StubFor<11>(2);
    gProductVt[kObject][3][1] = StubFor<3001>(0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Static fakes: services, segments, definitions, the message and its server, the pattern factory, the key source.
enum FakeRole {
    fService, fCatalogService, fRegistry, fSegmentService, fSegment, fAnswer, fSource, fHolder, fFactory, fApp, fDefinitions,
    fDefinition, fY, fZ, fMsg, fServer, fRoles
};
struct StaticFake {
    void **vt;
    int role;
    int index;
    int32_t refs;
    uint32_t ret[8];
};
constexpr int kSegments = 6, kAnswers = 6, kDefinitions = 8, kYs = 4, kZs = 4;
StaticFake gFakes[] = {
    {nullptr, fService}, {nullptr, fCatalogService}, {nullptr, fRegistry}, {nullptr, fSegmentService},
    {nullptr, fSegment, 0}, {nullptr, fSegment, 1}, {nullptr, fSegment, 2}, {nullptr, fSegment, 3}, {nullptr, fSegment, 4}, {nullptr, fSegment, 5},
    {nullptr, fAnswer, 0}, {nullptr, fAnswer, 1}, {nullptr, fAnswer, 2}, {nullptr, fAnswer, 3}, {nullptr, fAnswer, 4}, {nullptr, fAnswer, 5},
    {nullptr, fSource}, {nullptr, fHolder}, {nullptr, fFactory}, {nullptr, fApp}, {nullptr, fDefinitions},
    {nullptr, fDefinition, 0}, {nullptr, fDefinition, 1}, {nullptr, fDefinition, 2}, {nullptr, fDefinition, 3},
    {nullptr, fDefinition, 4}, {nullptr, fDefinition, 5}, {nullptr, fDefinition, 6}, {nullptr, fDefinition, 7},
    {nullptr, fY, 0}, {nullptr, fY, 1}, {nullptr, fY, 2}, {nullptr, fY, 3},
    {nullptr, fZ, 0}, {nullptr, fZ, 1}, {nullptr, fZ, 2}, {nullptr, fZ, 3},
    {nullptr, fMsg}, {nullptr, fServer},
};
const int kFakeCount = int(sizeof(gFakes) / sizeof(gFakes[0]));
void *gFakeVt[fRoles][kSlots];
uint32_t FakeCode(const void *p)
{
    const auto *f = static_cast<const StaticFake *>(p);
    if (f >= &gFakes[0] && f < &gFakes[kFakeCount]) return 0x3d000000u | uint32_t(f - &gFakes[0]);
    return 0;
}
StaticFake *Find(int role, int index = 0)
{
    for (auto &f : gFakes)
        if (f.role == role && f.index == index) return &f;
    return nullptr;
}

// What the world says (set per seed).
struct WorldConfig {
    bool holderAvailable;
    int segmentsOf[8][4]; // segments (fake index, -1 end) for resource id % 8
    uint32_t segmentKnown;  // bit per segment index: the segment service knows its name
    int definitionCount;
    int ySlotFor[16];       // definition id (fireplace slot 7) % 16 -> y index or -1
    int zFor[kYs];          // y -> z index or -1
    std::vector<std::array<uint32_t, 4>> sourceKeys, holderKeys;
} gCfg;

using KeyVector = std::vector<std::array<uint32_t, 4>, GZServiceAllocator<std::array<uint32_t, 4>>>;

uint32_t FakeHandler(void *self, int slot, const uint32_t *a, int n)
{
    auto *f = static_cast<StaticFake *>(self);
    if ((f->role == fSegment || f->role == fFactory || f->role == fMsg) && slot == 1) return uint32_t(++f->refs);
    if ((f->role == fSegment || f->role == fAnswer || f->role == fHolder || f->role == fFactory || f->role == fMsg) && slot == 2) {
        if (--f->refs < 0) Ev({evNegative, 0x1000u + uint32_t(f - gFakes)});
        return uint32_t(f->refs);
    }
    gEv.push_back(evFake | (uint32_t(f - gFakes) << 8) | uint32_t(slot));
    // The key source's second argument is the filter (a heap object): compared through its answers below, not its address.
    for (int i = 0; i < n; ++i) gEv.push_back((f->role == fSource || f->role == fHolder) && i == 1 ? 0x3a000000u : Norm(a[i]));
    switch (f->role) {
    case fService: { // slot 5 (id, iid, &out): the pattern key holder
        void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
        StaticFake *holder = Find(fHolder);
        *out = gCfg.holderAvailable ? holder : nullptr;
        if (*out) ++holder->refs;
        return *out != nullptr;
    }
    case fCatalogService:
        if (slot == 3) return uint32_t(reinterpret_cast<uintptr_t>(Find(fRegistry)));
        { // slot 48 (resource, &vector): the resource's segments, each referenced
            auto *v = reinterpret_cast<ServiceRefVector *>(uintptr_t(a[1]));
            const int *list = gCfg.segmentsOf[a[0] % 8];
            int count = 0;
            while (count < 4 && list[count] >= 0) ++count;
            if (count) {
                void **block = static_cast<void **>(RZMemory::Malloc(uint32_t(count * 4)));
                for (int i = 0; i < count; ++i) {
                    StaticFake *s = Find(fSegment, list[i]);
                    ++s->refs;
                    block[i] = s;
                }
                v->mFirst = block;
                v->mLast = v->mEnd = block + count;
            }
            return 0;
        }
    case fSegmentService: { // slot 16 (name): known?
        const auto *s = reinterpret_cast<const cRZString *>(uintptr_t(a[0]));
        gEv.push_back(TextHash(s->mString.c_str()));
        const int index = s->mString.size() > 3 ? s->mString[3] - '0' : 0;
        return (gCfg.segmentKnown >> index) & 1;
    }
    case fSegment:
        if (slot == 0) { // (iid, &answer)
            void **out = reinterpret_cast<void **>(uintptr_t(a[1]));
            const bool ok = f->ret[0] & 1;
            if (ok || (f->ret[0] & 2)) {
                StaticFake *answer = Find(fAnswer, f->index);
                *out = answer;
                ++answer->refs;
            }
            return ok;
        }
        { // slot 11 (cIGZString *name)
            auto *s = reinterpret_cast<cRZString *>(uintptr_t(a[0]));
            char name[8] = "seg0";
            name[3] = char('0' + f->index);
            s->mString = name;
            return 1;
        }
    case fAnswer:
        return f->ret[1];
    case fSource:
    case fHolder: { // (&keys, filter): the keys the filter takes
        auto *keys = reinterpret_cast<KeyVector *>(uintptr_t(a[0]));
        void *filter = reinterpret_cast<void *>(uintptr_t(a[1]));
        for (const auto &key : f->role == fSource ? gCfg.sourceKeys : gCfg.holderKeys) {
            using Included = uint32_t(__thiscall *)(void *, const uint32_t *);
            const bool in = uint8_t((*static_cast<Included **>(filter))[3](filter, key.data())) != 0;
            Ev({evFilter, key[0], in});
            if (in) keys->push_back(key);
        }
        return 0;
    }
    case fApp:
        return uint32_t(reinterpret_cast<uintptr_t>(Find(fDefinitions)));
    case fDefinitions:
        if (slot == 10) return uint32_t(gCfg.definitionCount);
        if (slot == 13) return uint32_t(reinterpret_cast<uintptr_t>(Find(fDefinition, int(a[0] % kDefinitions))));
        { // slot 16 (definition id)
            const int y = gCfg.ySlotFor[a[0] % 16];
            return y < 0 ? 0u : uint32_t(reinterpret_cast<uintptr_t>(Find(fY, y)));
        }
    case fDefinition:
        return slot == 38 ? (f->ret[0] & 1) : slot == 37 ? (f->ret[1] & 1) : f->ret[2];
    case fY: {
        const int z = gCfg.zFor[f->index];
        return z < 0 ? 0u : uint32_t(reinterpret_cast<uintptr_t>(Find(fZ, z)));
    }
    case fZ:
        return f->ret[0] + a[0];
    default:
        return f->ret[slot % 8];
    }
}

struct FakeSlot {
    int role, slot, arity;
};
constexpr FakeSlot kFakeSlots[] = {
    {fService, 5, 3},       {fCatalogService, 3, 0}, {fCatalogService, 48, 2}, {fRegistry, 20, 3}, {fSegmentService, 16, 1},
    {fSegment, 0, 2},       {fSegment, 1, 0},        {fSegment, 2, 0},         {fSegment, 11, 1},  {fAnswer, 2, 0},
    {fAnswer, 6, 0},        {fSource, 19, 2},        {fHolder, 18, 2},         {fHolder, 2, 0},    {fFactory, 1, 0},
    {fFactory, 2, 0},       {fApp, 24, 0},           {fDefinitions, 10, 0},    {fDefinitions, 13, 1}, {fDefinitions, 16, 1},
    {fDefinition, 37, 0},   {fDefinition, 38, 0},    {fDefinition, 76, 0},     {fY, 4, 0},         {fZ, 3, 1},
    {fMsg, 1, 0},           {fMsg, 2, 0},            {fMsg, 5, 1},             {fMsg, 33, 1},      {fServer, 3, 1},
};
template <int Slot> void *FakeStub(int arity)
{
    switch (arity) {
    case 0: return reinterpret_cast<void *>(&T0<&FakeHandler, Slot>);
    case 1: return reinterpret_cast<void *>(&T1<&FakeHandler, Slot>);
    case 2: return reinterpret_cast<void *>(&T2<&FakeHandler, Slot>);
    default: return reinterpret_cast<void *>(&T3<&FakeHandler, Slot>);
    }
}
template <size_t... I> void FillFakeVtables(std::index_sequence<I...>)
{
    for (auto &vt : gFakeVt) FillTraps(vt);
    ((gFakeVt[kFakeSlots[I].role][kFakeSlots[I].slot] = FakeStub<kFakeSlots[I].slot>(kFakeSlots[I].arity)), ...);
    for (auto &f : gFakes) f.vt = gFakeVt[f.role];
}

// ---------------------------------------------------------------------------------------------------------------------
// Resource parsers, built in the block operator new gave the parser's constructor.
struct FileContent {
    std::vector<int> resources; // pool indexes (-1: a null resource)
    int fallback = -1;
    bool fallbackOk = false;
};
struct LoaderConfig {
    std::map<std::string, FileContent> files;
    uint32_t materialValue = 0xffffffff;
};
std::map<uint32_t, LoaderConfig> gLoaders; // by parser constructor RVA

struct Parser {
    void **vt;       // +0
    uint32_t pad;    // +4
    int32_t *vbptr;  // +8
    Sub f1;          // +12
    Sub f2;          // +20
    int32_t vbt[3];
    uint32_t ctor;
    int32_t refs;
    uint32_t ordinal;
    std::string file;
};
void *gParserVt[3][kSlots];

Parser *ParserOf(void *self, int face)
{
    return reinterpret_cast<Parser *>(static_cast<uint8_t *>(self) - (face == 0 ? 0 : face == 1 ? 12 : 20));
}
const FileContent *Content(Parser *p)
{
    auto &files = gLoaders[p->ctor].files;
    auto it = files.find(p->file);
    return it == files.end() ? nullptr : &it->second;
}

uint32_t ParserHandler(void *self, int tag, const uint32_t *a, int n)
{
    const int face = tag / 1000, slot = tag % 1000;
    Parser *p = ParserOf(self, face);
    if (face == 1 && slot == 1) return uint32_t(++p->refs);
    if (face == 1 && slot == 2) {
        if (--p->refs == 0) {
            Ev({evParser | 0xff, p->ordinal});
            const size_t ordinal = p->ordinal;
            p->~Parser();
            ::operator delete(static_cast<void *>(p));
            (void)ordinal;
            return 0;
        }
        return uint32_t(p->refs);
    }
    gEv.push_back(evParser | (p->ordinal << 12) | uint32_t(face << 8) | uint32_t(slot));
    for (int i = 0; i < n; ++i) gEv.push_back(Norm(a[i]));
    const FileContent *c = Content(p);
    switch (face * 1000 + slot) {
    case 5: { // GetResource(index, type, &ref)
        void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
        const int index = int(a[0]);
        GiveProduct(out, c && index < int(c->resources.size()) ? P(c->resources[size_t(index)]) : nullptr);
        difftest::FillOut(out, 4);
        return 1;
    }
    case 6:
        return c ? uint32_t(c->resources.size()) : 0u;
    case 9: { // GetDefault(type, &ref)
        void **out = reinterpret_cast<void **>(uintptr_t(a[1]));
        if (c && c->fallback >= 0) GiveProduct(out, P(c->fallback));
        difftest::FillOut(out, 4);
        return c ? c->fallbackOk : false;
    }
    case 10:
        return gLoaders[p->ctor].materialValue;
    default:
        return 0;
    }
}

template <uint32_t Rva> void *__fastcall ParserCtor(void *memory, void *, uint32_t arg)
{
    auto *p = new (memory) Parser{};
    p->vt = gParserVt[0];
    p->vbptr = p->vbt;
    p->f1.vt = gParserVt[1];
    p->f2.vt = gParserVt[2];
    p->vbt[0] = -8;
    p->vbt[1] = 4;
    p->vbt[2] = 12;
    p->ctor = Rva;
    p->ordinal = uint32_t(gParsers.size());
    gParsers.push_back(p);
    Ev({evCtor, Rva, arg});
    return p;
}
constexpr uint32_t kParserCtors[] = {0xc18a8b, 0xc0e98b, 0xc0e8f2, 0xc19b92, 0xc1a432, 0xc1ab57, 0xc1b0ff,
                                     0xc1bd0c, 0xc1c34f, 0xc1c9a7, 0xc1d540, 0xc1da3d, 0xc1e16d};
template <size_t... I> std::vector<std::pair<uint32_t, void *>> ParserCtorStands(std::index_sequence<I...>)
{
    return {{kParserCtors[I], reinterpret_cast<void *>(&ParserCtor<kParserCtors[I]>)}...};
}
void FillParserVtables()
{
    for (auto &vt : gParserVt) FillTraps(vt);
    gParserVt[0][5] = reinterpret_cast<void *>(&T3<&ParserHandler, 5>);
    gParserVt[0][6] = reinterpret_cast<void *>(&T0<&ParserHandler, 6>);
    gParserVt[0][9] = reinterpret_cast<void *>(&T2<&ParserHandler, 9>);
    gParserVt[0][10] = reinterpret_cast<void *>(&T1<&ParserHandler, 10>);
    gParserVt[1][1] = reinterpret_cast<void *>(&T0<&ParserHandler, 1001>);
    gParserVt[1][2] = reinterpret_cast<void *>(&T0<&ParserHandler, 1002>);
    gParserVt[2][0] = reinterpret_cast<void *>(&T1<&ParserHandler, 2000>);
    gParserVt[2][1] = reinterpret_cast<void *>(&T0<&ParserHandler, 2001>);
}

// ---------------------------------------------------------------------------------------------------------------------
// Stand-ins over code.
uint32_t __cdecl StandParseFile(void *parser, const char *path)
{
    auto *p = static_cast<Parser *>(parser);
    Ev({evParse, p->ordinal, TextHash(path)});
    p->file = path;
    return 1;
}
void __cdecl StandPatternKey(const uint32_t *entry, void *catalog)
{
    Ev({evKeys, entry[0], entry[1], entry[2], entry[3], NormPtr(catalog)});
}
bool __cdecl StandPathExists(const cIGZString &path)
{
    const auto &s = reinterpret_cast<const cRZString &>(path).mString;
    Ev({evExists, TextHash(s.c_str())});
    return !s.empty() && s[0] != 'x';
}
std::map<uint32_t, std::vector<std::string>> gLists; // the file lists cTSAppCommonImpl slot 5 gives, by option
uint32_t __cdecl StandCommonImpl5(uint32_t option, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    Ev({evList, option, c, d, e});
    auto *files = reinterpret_cast<GZStringList *>(uintptr_t(list));
    for (const auto &name : gLists[option]) files->push_back(GZString(name.c_str()));
    return 1;
}
int gNextObject = 0, gNextHalfWall = 0;
void *__fastcall StandObjectCtor(void *memory, void *, uint32_t arg)
{
    ::operator delete(memory);
    Ev({evCtor, 0xc15270, arg});
    return P(RoleIndex(rObject, gNextObject++));
}
void *__fastcall StandHalfWallCtor(void *memory, void *, uint32_t arg)
{
    ::operator delete(memory);
    Ev({evCtor, 0xc170be, arg});
    return P(RoleIndex(rHalfWall, gNextHalfWall++));
}
void *__fastcall StandFactoryCtor(void *memory, void *)
{
    ::operator delete(memory);
    Ev({evCtor, 0xc0e8c8});
    return Find(fFactory);
}
alignas(8) uint8_t gMessageBlock[0x2c];
void *__cdecl StandMalloc(uint32_t bytes)
{
    Ev({evMalloc, bytes});
    return gMessageBlock;
}
void *__fastcall StandMessageCtor(void *memory, void *)
{
    Ev({evCtor, 0x1c033d, memory == gMessageBlock});
    return Find(fMsg);
}
void *__cdecl StandMessageServer() { return Find(fServer); }

// The original's cRZRandom constructor with the run's seed; ours gets the same through the clock-seed hook.
uint32_t gRandomSeed = 1;
cRZRandom *__fastcall RandomStub(cRZRandom *self, void *, uint32_t seed) { return self->Construct(seed == UINT32_MAX ? gRandomSeed : seed); }
uint32_t RandomClockSeed(int, uint32_t) { return gRandomSeed; }

// ---------------------------------------------------------------------------------------------------------------------
// The world.
const char *const kNames[] = {"Alpha", "alpha", "Beta", "beta", "Gamma", "delta", "Delta", "", "zeta", "Zeta"};

void BuildPool(uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 17);
    int at = 0;
    for (int role = 0; role < rRoles; ++role) {
        gRoleStart[role] = at;
        at += kPerRole[role];
    }
    const char *special = GameAddress<const char>(0x10806a8);
    for (int i = 0; i < kPool; ++i) {
        Product &p = gPool[i];
        std::memset(static_cast<void *>(&p), 0, sizeof(p));
        int role = 0;
        while (role + 1 < rRoles && i >= gRoleStart[role + 1]) ++role;
        p.role = Role(role);
        p.index = uint32_t(i);
        p.kind = role == rDeck ? kDeck : role == rObject ? kObject : kGeneric;
        p.refs = (role == rObject || role == rHalfWall) ? 0 : 1;
        for (int f = 0; f < kFaces; ++f) {
            p.sub[f].vt = gProductVt[p.kind][f];
            p.sub[f].vbptr = p.vbt[f];
            for (int g = 0; g < kFaces; ++g) p.vbt[f][g] = 8 * g - (8 * f + 4);
            for (int s = 0; s < 48; ++s) p.ret[f][s] = r() % 6;
        }
        p.ret[3][2] = r() % 14 + 1;      // guids: collisions on purpose
        p.ret[4][23] = r() % 10;         // wall part keys
        p.ret[0][4] = r() % 9;           // roof keys
        p.ret[0][7] = r() % 3 ? r() % 7 : 0; // roof pattern keys, extras' root flag, fireplace definitions
        p.ret[0][13] = r() % 8;
        p.ret[2][20] = uint32_t(int32_t(r() % 11) - 5); // sort keys, signed
        p.ret[3][4] = r() % 3 == 0;      // has its own default
        p.ret[2][29] = r() % 2;
        p.ret[0][11] = r() % 2;          // fences: material choice
        p.ret[0][1] = r() % 9;
        p.ret[0][6] = r() % 2 ? 1 : r() % 5;
        p.ret[2][26] = r();
        p.defaultKey = r() % 16;
        p.materialMode = int(r() % 3);
        p.nameAfterMaterial = r() % 2;
        std::memcpy(p.materialText, "m", 2);
        p.nextPart = 0;
        for (int k = 0; k < 2; ++k) {
            // A successful query always gives an object (the original uses it without a null test, 0xc0f8bc).
            p.qiOk[k] = r() % 3 != 0;
            p.qiAnswer[k] = p.qiOk[k] || r() % 2 ? RoleIndex(k ? rArch : rFence, int(r())) : -1;
        }
        const char *name = r() % 9 == 0 ? special : kNames[r() % 10];
        std::snprintf(p.text[0], sizeof(p.text[0]), "%s", name);
        std::snprintf(p.text[1], sizeof(p.text[1]), "%s%s", kNames[r() % 10], r() % 4 == 0 ? " with a long tail name" : "");
        if (role == rPattern) {
            p.ret[0][11] = uint32_t(reinterpret_cast<uintptr_t>(p.text[0]));
            p.ret[3][2] = uint32_t(reinterpret_cast<uintptr_t>(r() % 2 ? p.text[0] : p.text[1]));
        }
        if (role == rBrush) p.ret[0][6] = uint32_t(reinterpret_cast<uintptr_t>(p.text[1]));
        if (role == rPaint) {
            p.ret[0][4] = uint32_t(reinterpret_cast<uintptr_t>(p.text[1]));
            p.ret[3][2] = uint32_t(reinterpret_cast<uintptr_t>(p.text[r() % 2]));
        }
        if (role == rDeck && r() % 3 == 0) p.ret[3][2] = 5;
    }
    for (int i = 0; i < kPool; ++i) {
        Product &p = gPool[i];
        if (p.role == rWall)
            for (int k = 0; k < 4; ++k) p.parts[k] = RoleIndex(rWallPart, int(r()));
        if (p.role == rDeck)
            for (int k = 0; k < 4; ++k) p.parts[k] = RoleIndex(rDeckPart, int(r()));
        if (p.role == rDeckPart) p.defaultKey = r() % 3 ? gPool[RoleIndex(rDeck, int(r()))].ret[3][2] : r() % 20;
        if (p.role == rWallPart) p.defaultKey = r() % 3 ? gPool[RoleIndex(rWall, int(r()))].ret[3][2] : r() % 20;
    }
    gNextObject = gNextHalfWall = 0;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 r(seed * 40503u + 3);
    for (auto &f : gFakes) {
        f.refs = f.role == fFactory || f.role == fMsg || f.role == fSegment || f.role == fAnswer || f.role == fHolder ? 0 : 1;
        for (auto &w : f.ret) w = r() % 4;
    }
    // Always there: ParseHeldPatternKeys calls the holder without a null test (0xc11d7f).
    gCfg.holderAvailable = true;
    for (auto &list : gCfg.segmentsOf) {
        const int n = int(r() % 4);
        for (int i = 0; i < 4; ++i) list[i] = i < n ? int(r() % kSegments) : -1;
    }
    gCfg.segmentKnown = r() % 3 ? 0x3f : r();
    gCfg.definitionCount = int(r() % 9);
    for (auto &y : gCfg.ySlotFor) y = r() % 3 ? int(r() % kYs) : -1;
    for (auto &z : gCfg.zFor) z = r() % 3 ? int(r() % kZs) : -1;
    const uint32_t types[] = {0x2ac90e94, 0x2cb230b8, 0x4dcadb7e, 0x6d619378, 0xac8ce8fa, 0xaca8ea06, 0xcca8e925, 0x12345678, 0xaca8ea07};
    gCfg.sourceKeys.clear();
    gCfg.holderKeys.clear();
    for (int i = int(r() % 6); i > 0; --i) gCfg.sourceKeys.push_back({types[r() % 9], r() % 5, r() % 100, r() % 3});
    for (int i = int(r() % 6); i > 0; --i) gCfg.holderKeys.push_back({types[r() % 9], r() % 5, r() % 100, r() % 3});
}

struct LoaderSpec {
    uint32_t ctor;
    Role role;
    uint32_t option; // the Load list's option (0: LoadWallsAndFences' lists)
};
constexpr LoaderSpec kLoaderSpecs[] = {
    {0xc18a8b, rPattern, 0x6bd04212}, {0xc0e98b, rBrush, 0x4bd0420e},     {0xc0e8f2, rPaint, 0xebd0423e},
    {0xc19b92, rWall, 0x2bd0423c},    {0xc1a432, rFence, 0xebd04239},     {0xc1ab57, rArch, 0xcbd04239},
    {0xc1b0ff, rDeck, 0xabd04238},    {0xc1bd0c, rStairs, 0x2bd04237},    {0xc1c34f, rFireplace, 0xabd04234},
    {0xc1c9a7, rAwning, 0xf03a7ee3},  {0xc1d540, rRoof, 0xebd04231},      {0xc1da3d, rRoofPattern, 0xcbd77a21},
    {0xc1e16d, rUserTool, 0x8c7194f2},
};
constexpr uint32_t kFloorPatternOption = 0x6bd0423b;

void BuildLoaders(uint32_t seed)
{
    std::mt19937 r(seed * 69069u + 1);
    gLoaders.clear();
    gLists.clear();
    for (const auto &spec : kLoaderSpecs) {
        LoaderConfig &config = gLoaders[spec.ctor];
        config.materialValue = r() % 3 ? r() % 40 : 0xffffffff;
        const int files = int(r() % 4);
        auto makeList = [&](uint32_t option, char tag) {
            std::vector<std::string> &names = gLists[option];
            for (int f = 0; f < files + int(r() % 2); ++f) {
                char name[64];
                std::snprintf(name, sizeof(name), "%s%c%d%s", r() % 5 == 0 ? "x" : "f", tag, int(r() % 4),
                              r() % 3 == 0 ? "-with-a-name-longer-than-sixteen" : "");
                names.push_back(name);
                FileContent &content = config.files[name];
                if (!content.resources.empty() || content.fallback >= 0) continue;
                const int count = int(r() % 6);
                for (int i = 0; i < count; ++i)
                    content.resources.push_back(spec.role == rUserTool && r() % 4 == 0 ? -1 : RoleIndex(spec.role, int(r())));
                if (r() % 2) content.fallback = count && r() % 2 ? content.resources[r() % size_t(count)] : RoleIndex(spec.role, int(r()));
                if (spec.role == rUserTool && content.fallback < 0) content.fallback = -1;
                content.fallbackOk = r() % 3 != 0;
            }
        };
        makeList(spec.option, char('a' + (&spec - kLoaderSpecs)));
        if (spec.ctor == 0xc18a8b) makeList(kFloorPatternOption, 'A');
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The catalogue's state, read through our class (the original's object has the same layout and the same library types).
void PushRefs(Blob &b, const ProductVector &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    for (const auto &ref : v) b.push_back(NormPtr(ref.Get()));
}
void PushMap(Blob &b, const ProductIdMap &m)
{
    b.push_back(uint32_t(m.size()));
    for (const auto &e : m) b.insert(b.end(), {Norm(e.first), NormPtr(e.second.Get())});
}
void PushMap(Blob &b, const ProductNameMap &m)
{
    b.push_back(uint32_t(m.size()));
    for (const auto &e : m) b.insert(b.end(), {TextHash(e.first.c_str()), NormPtr(e.second.Get())});
}
void Dump(Blob &b, const void *object)
{
    const auto *c = static_cast<const cTSCatalog *>(object);
    b.insert(b.end(), {c->mRefCount, c->mNextId, c->mInitialized, c->mLoaded});
    for (const ProductVector *v : {&c->mObjects, &c->mWallParts, &c->mWalls, &c->mFences, &c->mHalfWalls, &c->mFenceArches,
                                   &c->mDeckFoundations, &c->mPatterns[0], &c->mPatterns[1], &c->mTerrainBrushes,
                                   &c->mTerrainPaints, &c->mStairs, &c->mFireplaces, &c->mAwnings, &c->mRoofs, &c->mRoofPatterns,
                                   &c->mExtras, &c->mRootExtras, &c->mUserTools})
        PushRefs(b, *v);
    for (const ProductIdMap *m : {&c->mObjectsByDefinition, &c->mWallPartsById, &c->mWallsById, &c->mFencesById,
                                  &c->mHalfWallsById, &c->mFenceArchesById, &c->mDeckFoundationsById, &c->mStairsByGuid,
                                  &c->mFireplacesByKey, &c->mAwningsByKey, &c->mRoofsByKey, &c->mRoofPatternsByKey,
                                  &c->mExtrasByGuid, &c->mUserToolsByKey})
        PushMap(b, *m);
    for (const ProductNameMap *m : {&c->mPatternsByName[0], &c->mPatternsByName[1], &c->mTerrainBrushesByName, &c->mTerrainPaintsByName})
        PushMap(b, *m);
    for (const ProductRef *r : {&c->mDefaultPattern[0], &c->mDefaultPattern[1], &c->mDefaultRoofPattern, &c->mDefaultFence,
                                &c->mDefaultFenceArch})
        b.push_back(NormPtr(r->Get()));
    b.push_back(uint32_t(c->mIdByGuid.size()));
    for (const auto &e : c->mIdByGuid) b.insert(b.end(), {e.first, e.second});
    b.push_back(uint32_t(c->mFenceKeys.size()));
    for (uint32_t k : c->mFenceKeys) b.push_back(k);
    b.insert(b.end(), {c->mFenceMaterialValue, c->mFenceArchMaterialValue, c->mPatternMaterialValue[0], c->mPatternMaterialValue[1],
                       c->mRoofPatternMaterialValue});
}
void DumpCounts(Blob &b)
{
    for (const auto &p : gPool) b.insert(b.end(), {uint32_t(p.refs), p.defaultKey, p.material, uint32_t(p.materialMode)});
    for (const auto &f : gFakes) b.push_back(uint32_t(f.refs));
}

// ---------------------------------------------------------------------------------------------------------------------
// Globals and patches for a test.
struct Fixture {
    difftest::Context &ctx;
    uint32_t saved[6];
    bool builtAllocator = false;
    pworld::Patches patches;
    uint8_t *randomSite;
    uint8_t randomSaved[5];
    uint32_t (*savedClock)(int, uint32_t);

    struct AllocService {
        void **vt;
    } alloc;
    void *allocVt[8] = {};
    static void *__fastcall Alloc(AllocService *, void *, uint32_t bytes) { return RZMemory::Malloc(bytes); }
    static void __fastcall Free(AllocService *, void *, void *block)
    {
        if (block) RZMemory::Free(block);
    }

    static constexpr uint32_t kGlobals[6] = {0x10c95d4, 0x10c95d8, 0x10c9378, 0x10faf6c, 0x10faf80, 0x10cc288};

    explicit Fixture(difftest::Context &c) : ctx(c)
    {
        gCtx = &ctx;
        for (int i = 0; i < 6; ++i) saved[i] = *ctx.image.At<uint32_t>(kGlobals[i]);
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
        allocVt[3] = reinterpret_cast<void *>(&Alloc);
        allocVt[4] = reinterpret_cast<void *>(&Free);
        alloc.vt = allocVt;
        *ctx.image.At<void *>(0x10c95d4) = &alloc;
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        FillProductVtables(std::make_index_sequence<sizeof(kProductSlots) / sizeof(kProductSlots[0])>{});
        FillFakeVtables(std::make_index_sequence<sizeof(kFakeSlots) / sizeof(kFakeSlots[0])>{});
        FillParserVtables();
        *ctx.image.At<void *>(0x10c9378) = Find(fService);
        *ctx.image.At<void *>(0x10faf6c) = Find(fCatalogService);
        *ctx.image.At<void *>(0x10faf80) = Find(fSegmentService);
        *ctx.image.At<void *>(0x10cc288) = Find(fApp);

        patches.Heap();
        for (const auto &stand : ParserCtorStands(std::make_index_sequence<sizeof(kParserCtors) / sizeof(kParserCtors[0])>{}))
            patches.Image(stand.first, stand.second);
        patches.Image(0xc15270, &StandObjectCtor);
        patches.Image(0xc170be, &StandHalfWallCtor);
        patches.Image(0xc0e8c8, &StandFactoryCtor);
        patches.Image(0xc13038, &StandParseFile);
        patches.Image(0xc130db, &StandPatternKey);
        patches.Both(0x1c033d, &cRZMessage2Standard::Construct, &StandMessageCtor);
        patches.Both(0x9c5a89, &GetCachedMessageServer, &StandMessageServer);
        patches.Both(0x1c1242, &cRZFile::PathExists, &StandPathExists);
        patches.Both(0x1d0c60, &appinit::CommonImplSlot5, &StandCommonImpl5);

        randomSite = ctx.image.At<uint8_t>(0x1b8bc8);
        std::memcpy(randomSaved, randomSite, 5);
        DWORD old;
        VirtualProtect(randomSite, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(&RandomStub) - (reinterpret_cast<uintptr_t>(randomSite) + 5));
        randomSite[0] = 0xE9;
        std::memcpy(randomSite + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), randomSite, 5);
        savedClock = gRZRandomClockSeedOverride;
        gRZRandomClockSeedOverride = &RandomClockSeed;

        NT_TIB *tib = reinterpret_cast<NT_TIB *>(NtCurrentTeb());
        gStackLow = uint32_t(reinterpret_cast<uintptr_t>(tib->StackLimit));
        gStackHigh = uint32_t(reinterpret_cast<uintptr_t>(tib->StackBase));
    }
    ~Fixture()
    {
        std::memcpy(randomSite, randomSaved, 5);
        FlushInstructionCache(GetCurrentProcess(), randomSite, 5);
        gRZRandomClockSeedOverride = savedClock;
        for (int i = 0; i < 6; ++i) *ctx.image.At<uint32_t>(kGlobals[i]) = saved[i];
        if (builtAllocator) difftest_alloc::ShutdownAndRelease(GameAddress<GeneralAllocator>(0x10cb098));
        gCtx = nullptr;
    }
};

// A slot of the side's object, through its own vtable.
template <class R = uint32_t, class... A> R CallSlot(void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return (*static_cast<Fn *const *>(object))[slot](object, args...);
}

void *MakeSide(difftest::Context &ctx, int side)
{
    return side == 0 ? ctx.Original<void *(__cdecl *)()>(0xc105a5)() : static_cast<void *>(cTSCatalog::Create());
}

// A file list for a loader called directly, from the world's lists.
struct TestList {
    GZStringList list;
    explicit TestList(uint32_t option)
    {
        for (const auto &name : gLists[option]) list.push_back(GZString(name.c_str()));
    }
};

// One operation of a run: a slot and its arguments, drawn from the run's generator.
struct Op {
    int slot;
    uint32_t a = 0, b = 0, c = 0;
};

std::vector<Op> MakeOps(uint32_t seed, int count)
{
    std::mt19937 r(seed * 1103515245u + 12345);
    std::vector<Op> ops;
    auto roleProduct = [&](Role role) { return uint32_t(RoleIndex(role, int(r()))); };
    // Before Load: queries on the empty catalogue, and Init on its own sometimes.
    for (int s : {11, 24, 27, 40, 43, 73, 75, 78, 79, 80}) ops.push_back({s, 0});
    if (r() % 2) ops.push_back({3});
    ops.push_back({5});
    // Sometimes a run of extras and random root picks first: the random ops alone rarely hold more than one root extra.
    if (r() % 2) {
        for (int i = 0; i < 8; ++i) ops.push_back({85, roleProduct(rExtra)});
        for (int i = 0; i < 3; ++i) ops.push_back({73, r()});
    }
    // Reached through these ops (catalogue slot, ours, RVA): 3 Init 0xc10768, 8 AllSegmentsOfKindOne 0xc1087c,
    //  10 AddPattern 0xc0f414, 11 GetObjectCount 0x806c60, 12 AddObject 0xc0f2b2, 13 GetObjectAt 0x808d30,
    //  14 FindObject 0x6e7c75, 15 RemoveObject 0xc101e8, 16 GetWallPartCount 0x5773e0, 17 GetWallPartAt 0x808190,
    //  18 FindWallPart 0xc142c6, 19 FindWallPartByKey 0xc142e8, 20 GetWallCount 0x806c50, 21 GetWallAt 0x807330,
    //  22 FindWall 0xc14909, 23 FindWallByKey 0xc1492b, 24 GetFenceCount 0xb52a00, 25 GetFenceAt 0xc10508,
    //  26 FindFenceByKey 0xc10515, 27 GetDefaultFence 0x6f27ab, 28 RemoveFence 0xc10110, 29 GetHalfWallCount 0xc12f9a,
    //  30 GetHalfWallAt 0xc1070c, 31 FindHalfWallByKey 0xc10719, 32 GetFenceArchCount 0x73e3d1,
    //  33 GetFenceArchAt 0xc1049a, 34 FindFenceArchByKey 0xc104a7, 35 GetDefaultFenceArch 0x530770,
    //  36 GetDeckFoundationCount 0x6bd993, 37 GetDeckFoundationAt 0xc100a6, 38 FindDeckFoundation 0xc10084,
    //  39 FindDeckFoundationByKey 0xc100b3, 40 GetPatternCount 0xc12fc4, 41 GetPatternAt 0xc14398,
    //  42 FindPattern 0xc1432c, 43 GetDefaultPattern 0xc100f7, 44 RemovePattern 0xc1024a,
    //  45 GetTerrainBrushCount 0xa58000, 46 GetTerrainBrushAt 0xc1488b, 47 FindTerrainBrush 0xc14842,
    //  48 GetTerrainPaintCount 0xc12ff7, 49 GetTerrainPaintAt 0xc14832, 50 FindTerrainPaint 0xc147e9,
    //  51 RemoveTerrainPaint 0xc1036c, 52 GetStairsCount 0xc12fa4, 53 GetStairsAt 0xc12f2d, 54 FindStairs 0x707810,
    //  55 GetFireplaceCount 0xc12f8a, 56 GetFireplaceAt 0xc10595, 57 FindFireplace 0xc10573,
    //  58 GetAwningCount 0xc12f6f, 59 GetAwningAt 0xc0fb21, 60 FindAwning 0xc0faff, 61 FindAwningByKey 0xc0fb31,
    //  62 GetRoofCount 0xc12fe7, 63 GetRoofAt 0xc146cc, 64 FindRoof 0xc146aa, 65 GetRoofPatternCount 0xc12fd7,
    //  66 GetRoofPatternAt 0xc1469a, 67 FindRoofPattern 0xc14661, 68 GetDefaultRoofPattern 0x52fd40,
    //  69 RemoveRoofPattern 0xc102fc, 70 GetExtraCount 0xc12fb4, 71 GetExtraAt 0xc12f3d, 72 FindExtra 0xc12f4d,
    //  74 RemoveExtra 0xc10178, 75 GetUserToolCount 0xc13007, 76 GetUserToolAt 0xc148d0, 77 FindUserTool 0xc148ae,
    //  79 GetSlot79Value 0xc14616, 80 SortFences 0xc14796, 82 AddRoofPattern 0xc0f6c5, 83 AddFence 0xc0ef18,
    //  87 ParseHeldPatternKeys 0xc11d1c, 88 AddObjectsFromDefinitions 0xc11cbe, 89 LoadPatterns 0xc11ec4,
    //  90 LoadTerrainBrushes 0xc1276e, 91 LoadTerrainPaints 0xc1256c, 92 LoadWallsAndFences 0xc11df2,
    //  93 LoadWalls 0xc12b83, 94 LoadFences 0xc111c0, 95 LoadFenceArches 0xc10e7d, 96 LoadDeckFoundations 0xc11650,
    //  97 LoadModularStairs 0xc11a7e, 98 LoadFireplaces 0xc113e7, 99 LoadAwnings 0xc10c9e, 100 LoadRoofs 0xc1232a,
    //  101 LoadRoofPatterns 0xc120fc, 102 LoadUserToolProxies 0xc129a2, 103 ResolveWallPartKeys 0xc0f83e,
    //  104 ApplyDefaultPattern 0xc0fa09, 105 ApplyDefaultRoofPattern 0xc0fa8a, 106 ApplyMaterialValues 0xc0ff0c,
    //  107 ApplyPatternMaterialValues 0xc0ffcc, 108 ApplyFenceMaterialValues 0xc0ff7a,
    //  109 ApplyFenceArchMaterialValues 0xc0ff36, 110 ApplyRoofPatternMaterialValues 0xc1002f.
    const int idSlots[] = {14, 18, 22, 38, 54, 57, 60, 64, 72, 77, 19, 23, 31, 39, 61};
    const int countSlots[] = {11, 16, 20, 24, 29, 32, 36, 45, 48, 52, 55, 58, 62, 65, 70, 75};
    for (int i = 0; i < count; ++i) {
        Op op{};
        switch (r() % 24) {
        case 0: op = {countSlots[r() % 16]}; break;
        case 1: op = {1000 + countSlots[r() % 16], uint32_t(r())}; break; // the matching At slot, index from the side's count
        case 2: case 3: op = {idSlots[r() % 15], r() % 22}; break;
        case 4: op = {int(r() % 2 ? 26 : 34), r() % 12, r() % 2}; break;
        case 5: op = {67, r() % 9, r() % 2}; break;
        case 6: op = {42, r() % 2, r() % 11, r() % 2}; break; // name index 10: a null name
        case 7: op = {int(r() % 2 ? 47 : 50), r() % 11}; break;
        case 8: {
            const int which = int(r() % 6);
            const int slots[] = {15, 28, 44, 51, 69, 74};
            const Role roles[] = {rObject, rFence, rPattern, rPaint, rRoofPattern, rExtra};
            op = {slots[which], roleProduct(roles[which])};
            break;
        }
        case 9: {
            const int which = int(r() % 6);
            const int slots[] = {82, 83, 84, 85, 86, 10};
            const Role roles[] = {rRoofPattern, rFence, rFence, rExtra, rPaint, rPattern};
            op = {slots[which], roleProduct(roles[which]), r() % 2};
            break;
        }
        case 10: op = {int(r() % 2 ? 8 : 9), roleProduct(Role(r() % rRoles)), r() % 5 == 0}; break;
        case 11: op = {7, r() % 4 == 0}; break;
        case 12: op = {int(103 + r() % 8), r() % 2}; break;
        case 13: op = {int(89 + r() % 14), r() % 2}; break;
        case 14: op = {73, r()}; break;
        case 15: op = {int(r() % 3 ? 87 : 88)}; break;
        case 16: op = {int(r() % 2 ? 78 : 79)}; break;
        case 17: {
            const int slots[] = {80, 27, 35, 68};
            op = {slots[r() % 4]};
            break;
        }
        case 18: op = {0, r() % 3 == 0 ? 1u : r() % 3 == 0 ? 0xdadba5f9u : r(), r() % 4 == 0}; break;
        case 19: op = {int(r() % 2 ? 1 : 2)}; break;
        case 20: op = {int(r() % 3 ? 5 : r() % 2 ? 6 : 4)}; break;
        case 21: op = r() % 3 ? Op{12, r(), r() % 6 == 0} : Op{3}; break;
        case 22: op = {40 + int(r() % 2) * 3, r() % 2}; break;
        default: op = {1000 + 40, r() % 2, uint32_t(r())}; break;
        }
        ops.push_back(op);
    }
    return ops;
}

const char *NameArg(uint32_t i) { return i >= 10 ? nullptr : kNames[i]; }

// Runs one op on the side's object; returns the result word (normalised).
uint32_t RunOp(void *cat, const Op &op, int &refs)
{
    switch (op.slot) {
    case 0: {
        void *out = reinterpret_cast<void *>(0x1234);
        const uint32_t ok = CallSlot<uint8_t>(cat, 0, op.a, op.b ? nullptr : &out);
        if (ok && !op.b) ++refs;
        return ok | (NormPtr(out) << 8);
    }
    case 1: ++refs; return CallSlot(cat, 1);
    case 2:
        if (refs <= 1) return 0;
        --refs;
        return CallSlot(cat, 2);
    case 7: case 87: { // the blocks freed (the filter, 8 bytes, is deleted by its last Release)
        difftest::FreeRecorder freed;
        if (op.slot == 7) CallSlot(cat, 7, op.a ? nullptr : static_cast<void *>(Find(fSource)));
        else CallSlot(cat, 87);
        freed.Stop();
        size_t sizes[16];
        const size_t n = freed.SortedSizes(sizes, 16);
        uint32_t eight = 0;
        for (size_t i = 0; i < n && i < 16; ++i) eight += sizes[i] == 8;
        return eight;
    }
    case 8: return CallSlot<uint8_t>(cat, 8, FaceAddr(P(int(op.a)), 2));
    case 9: {
        void *out = reinterpret_cast<void *>(0x5678);
        const uint8_t ok = CallSlot<uint8_t>(cat, 9, op.b ? nullptr : FaceAddr(P(int(op.a)), 2), &out);
        const uint32_t result = ok | (NormPtr(out) << 8);
        if (ok) CallSlot(out, 2);
        return result;
    }
    case 10: return CallSlot(cat, 10, op.b, static_cast<void *>(P(int(op.a)))), 0;
    case 26: case 34: return NormPtr(CallSlot<void *>(cat, op.slot, op.a, uint32_t(op.b)));
    case 67: return NormPtr(CallSlot<void *>(cat, 67, op.a, uint32_t(op.b)));
    case 42: return NormPtr(CallSlot<void *>(cat, 42, op.a, NameArg(op.b), uint32_t(op.c)));
    case 47: case 50: return NormPtr(CallSlot<void *>(cat, op.slot, NameArg(op.a)));
    case 15: case 28: case 44: case 51: case 69: case 74: {
        std::unique_ptr<shape::StandIn> malloc;
        if (op.slot == 51) malloc.reset(new shape::StandIn(0x1be3b0, &RZMemory::Malloc, &StandMalloc));
        void *item = P(int(op.a));
        if (op.slot == 15) item = FaceAddr(P(int(op.a)), 3); // the catalogue keeps the object products' face 3
        return CallSlot<uint8_t>(cat, op.slot, item);
    }
    case 82: case 85: case 86: return CallSlot(cat, op.slot, static_cast<void *>(P(int(op.a)))), 0;
    case 83: return CallSlot(cat, 83, static_cast<void *>(P(int(op.a))), uint32_t(op.b)), 0;
    case 84: return CallSlot(cat, 84, static_cast<void *>(P(int(op.a)))), 0;
    case 73: gRandomSeed = op.a; return NormPtr(CallSlot<void *>(cat, 73));
    case 78: return CallSlot(cat, 78);
    case 79: {
        const float f = CallSlot<float>(cat, 79);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
    }
    case 80: return CallSlot<uint8_t>(cat, 80);
    case 40: case 43: return op.slot == 40 ? CallSlot(cat, 40, op.a) : NormPtr(CallSlot<void *>(cat, 43, op.a));
    case 89: case 90: case 91: case 93: case 94: case 95: case 96: case 97: case 98: case 99: case 100: case 101: case 102: {
        // The loader's own list (89: wall patterns or floor patterns by the type argument).
        uint32_t option = 0;
        for (const auto &spec : kLoaderSpecs) {
            static const int kSlot[] = {89, 90, 91, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102};
            if (kSlot[&spec - kLoaderSpecs] == op.slot) option = spec.option;
        }
        if (op.slot == 89 && op.a == 0) option = kFloorPatternOption;
        TestList list(option);
        if (op.slot == 89) CallSlot(cat, 89, op.a, &list.list);
        else CallSlot(cat, op.slot, &list.list);
        return 0;
    }
    case 104: return CallSlot(cat, 104, op.a), 0;
    case 3: case 4: case 5: case 6: case 88: case 92: case 103: case 105: case 106: case 107: case 108: case 109: case 110:
        return CallSlot(cat, op.slot), 0;
    default:
        if (op.slot >= 1000) { // At slots: an index below the count (slot 76 also past it)
            const int countSlot = op.slot - 1000;
            if (countSlot == 40) {
                const uint32_t n = CallSlot(cat, 40, op.a);
                if (!n) return 0xffffffff;
                return NormPtr(CallSlot<void *>(cat, 41, op.a, op.b % n));
            }
            const uint32_t n = CallSlot(cat, countSlot);
            if (countSlot == 75) return NormPtr(CallSlot<void *>(cat, 76, op.a % (n + 2)));
            if (!n) return 0xffffffff;
            return NormPtr(CallSlot<void *>(cat, countSlot == 11 ? 13 : countSlot + 1, op.a % n));
        }
        if (op.slot == 12) // AddObject(definition or null)
            return CallSlot<uint8_t>(cat, 12, op.b ? nullptr : static_cast<void *>(Find(fDefinition, int(op.a % kDefinitions))));
        static const std::set<int> kNoArgument = {11, 16, 20, 24, 29, 32, 36, 45, 48, 52, 55, 58, 62, 65, 70, 75, 27, 35, 68};
        if (kNoArgument.count(op.slot)) return NormPtr(CallSlot<void *>(cat, op.slot));
        if (op.slot == 14) return NormPtr(CallSlot<void *>(cat, 14, uint32_t(reinterpret_cast<uintptr_t>(Find(fDefinition, int(op.a % kDefinitions))))));
        // counts and id / key finders
        return NormPtr(CallSlot<void *>(cat, op.slot, op.a));
    }
}

struct RunResult {
    std::vector<Blob> steps;
};

RunResult Run(difftest::Context &ctx, int side, uint32_t seed, const std::vector<Op> &ops)
{
    BuildPool(seed);
    BuildFakes(seed);
    BuildLoaders(seed);
    gParsers.clear();
    gEv.clear();
    RunResult out;
    void *cat = MakeSide(ctx, side);
    gSideCatalog = cat;
    int refs = 1;
    CallSlot(cat, 1);
    for (const Op &op : ops) {
        gEv.clear();
        Blob step;
        step.insert(step.end(), {evOp, uint32_t(op.slot), op.a, op.b, op.c});
        step.push_back(RunOp(cat, op, refs));
        step.insert(step.end(), gEv.begin(), gEv.end());
        Dump(step, cat);
        DumpCounts(step);
        out.steps.push_back(std::move(step));
    }
    // Release down to zero: the deleting destructor (slot 81) frees the catalogue and releases every reference.
    gEv.clear();
    Blob last;
    while (refs > 0) {
        last.push_back(CallSlot(cat, 2));
        --refs;
    }
    last.insert(last.end(), gEv.begin(), gEv.end());
    DumpCounts(last);
    out.steps.push_back(std::move(last));
    gSideCatalog = nullptr;
    return out;
}

void Compare(difftest::Context &ctx, const RunResult &a, const RunResult &b, uint32_t seed, const std::vector<Op> &ops)
{
    ++ctx.cases;
    for (size_t i = 0; i < a.steps.size() && i < b.steps.size(); ++i) {
        if (a.steps[i] == b.steps[i]) continue;
        size_t at = 0;
        while (at < a.steps[i].size() && at < b.steps[i].size() && a.steps[i][at] == b.steps[i][at]) ++at;
        if (GetEnvironmentVariableA("CATALOG_DIFF", nullptr, 0)) { // the words around the difference, on stderr
            for (int s = 0; s < 2; ++s) {
                const Blob &x = s ? b.steps[i] : a.steps[i];
                std::fprintf(stderr, "seed %u step %u %s:", seed, unsigned(i), s ? "ours" : "orig");
                for (size_t k = at > 24 ? at - 24 : 0; k < at + 24 && k < x.size(); ++k)
                    std::fprintf(stderr, k == at ? " [%08x]" : " %08x", x[k]);
                std::fprintf(stderr, "\n");
            }
        }
        ctx.Fail("seed %u step %u (slot %d): differ at word %u (sizes %u/%u): %08x vs %08x", seed, unsigned(i),
                 i < ops.size() ? ops[i].slot : 81, unsigned(at), unsigned(a.steps[i].size()), unsigned(b.steps[i].size()),
                 at < a.steps[i].size() ? a.steps[i][at] : 0, at < b.steps[i].size() ? b.steps[i][at] : 0);
        return;
    }
    if (a.steps.size() != b.steps.size()) ctx.Fail("seed %u: step counts differ", seed);
}

void RunSeeds(difftest::Context &ctx, uint32_t first, uint32_t count, int ops)
{
    Fixture fixture(ctx);
    for (uint32_t seed = first; seed < first + count; ++seed) {
        const std::vector<Op> list = MakeOps(seed, ops);
        char mark[64];
        std::snprintf(mark, sizeof(mark), "original: seed %u", seed);
        ctx.Mark(mark);
        const RunResult original = Run(ctx, 0, seed, list);
        std::snprintf(mark, sizeof(mark), "ours: seed %u", seed);
        ctx.Mark(mark);
        const RunResult ours = Run(ctx, 1, seed, list);
        Compare(ctx, original, ours, seed, list);
        if (ctx.failures.size() > 8) return;
    }
}

} // namespace

// Load (0xc109b4) and everything it reaches: Init, Unload, the loaders 89-102, LoadWallsAndFences, ParseHeldPatternKeys,
// AddObjectsFromDefinitions (AddObject, AllSegmentsOfKindOne), AddPattern, AddFence, AddRoofPattern, the sorts (SortFences),
// ResolveWallPartKeys, the default and material passes 104-110 (ApplyMaterialValue 0xc146e7), the factory 0xc105a5, the
// constructor 0xc0e538, QueryInterface 0xc143f6, AddRef 0x148fb0, Release 0xc1463e and the destructor 0xc0eafa / 0xc0ed88.
DIFF_TEST(Catalog_Load, 0xc109b4) { RunSeeds(ctx, 1, 120, 0); }

// Random sequences of every slot after a Load: the getters and finders 11-80, the removers 15/28/44/51/69/74 (the terrain
// paint message), AddHalfWall 0xc0f0e6, AddExtra 0xc0ee10, AddTerrainPaint 0xc0f75b, ParsePatternKeysFrom 0xc0f5d8 (and the
// filter cPatternParsingFilter: IsKeyIncluded 0xc10965, Release 0x72c480, the destructor 0xc0ed68), FindFirstKnownSegment
// 0xc1061d, GetRandomRootExtra 0xc105e0, Shutdown 0xc14786, the loaders and passes called directly, reloads.
DIFF_TEST(Catalog_Slots, 0xc0fb91) { RunSeeds(ctx, 1000, 160, 60); }
