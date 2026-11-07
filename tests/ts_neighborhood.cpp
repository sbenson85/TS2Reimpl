// Differential tests for cTSNeighborhood (construction, destruction, Read/Write against recording fake streams and
// the framework, the maps and the small accessors).
//
// A World is one neighbourhood in a guarded raw buffer plus recording fakes (lots, members, a delegate, streams, the
// framework). Every call a fake receives appends to `events`; objects hand out counted references (`refs`), and the final
// counts are compared as well. Two worlds built from one seed start identically; one runs the original, the other ours, and
// everything observable is compared (the object's bytes with heap pointers normalised, the contents of the maps).
#include "difftest.h"
#include "heap_poison.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <Windows.h>
#include <intrin.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <random>
#include <vector>

namespace {
using Nb = cTSNeighborhood;
constexpr unsigned kObjects = 40;
constexpr unsigned kLots = 8;     // objs[0..7]: lots
constexpr unsigned kMembers = 8;  // objs[8..31]: members
constexpr unsigned kExtra = 32;   // objs[32]: delegate, 33: owner face, 34: owner, 35: second owner, 36: kind(hit), 37: kind(miss)

struct Obj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t payload;
    uint32_t ownerValue;
    int32_t bounds[4];
    int32_t valid;
    int32_t kindIndex;
    uint32_t kindHit;
    uint32_t identity[2];
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[0xcc];
    uint8_t after[16];
};

struct World {
    Guarded work;
    Obj objs[kObjects];
    std::vector<uint32_t> events;
    std::mt19937 rng;
    uint32_t nextMember = 0;
    uint32_t failCreate = 0;      // bit n: the n-th CreateInstance fails
    uint32_t createCalls = 0;
    uint32_t persistOwnerValue = 0;
    uint32_t nbRefs = 0;
};
World gW;
Nb *N() { return reinterpret_cast<Nb *>(gW.work.bytes); }

enum : uint32_t {
    evAddRef = 1, evRelease, evMemberWrite, evMemberRead, evSetIdentity, evSetOwnerValue, evCreate, evNbAddRef, evNbDelete,
    evNextLot, evKind, evBounds, evValid, evPersistOwner, evFace, evDelegate,
    evGetU32 = 40, evGetS32, evGetU8, evGetU16, evGetString, evGetError, evSetMeta, evSetU32, evSetS32, evSetU8, evSetU16,
    evSetString
};
void Ev(std::initializer_list<uint32_t> items) { for (uint32_t v : items) gW.events.push_back(v); }

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (unsigned i = 0; i < kObjects; ++i) if (p == &gW.objs[i]) return 100 + i;
    if (p == gW.work.bytes) return 2;
    if (p == gW.work.bytes + 4) return 3;
    if (p == gW.work.bytes + 8) return 4;
    return 1;
}

// ---- fakes ----
void *gNbVT[80], *gMemberVT[48], *gLotVT[24], *gKindVT[32], *gDelegateVT[64], *gFaceVT[8], *gPersistVT[12], *gDirectorVT[16],
    *gFrameworkVT[4], *gPlainVT[4];
struct Framework { void **vt; } gFramework;

uint32_t __fastcall ObjAddRef(Obj *self, void *) { Ev({evAddRef, self->id}); return uint32_t(++self->refs); }
uint32_t __fastcall ObjRelease(Obj *self, void *) { Ev({evRelease, self->id}); return uint32_t(--self->refs); }
uint8_t __fastcall MemberWrite(Obj *self, void *, void *) { Ev({evMemberWrite, self->id}); return 1; }
uint8_t __fastcall MemberRead(Obj *self, void *, void *)
{
    self->payload = gW.rng();
    Ev({evMemberRead, self->id, self->payload});
    return 1;
}
void __fastcall MemberSetIdentity(Obj *self, void *, uint32_t kind, uint32_t id)
{
    self->identity[0] = kind; self->identity[1] = id;
    Ev({evSetIdentity, self->id, kind, id});
}
void __fastcall MemberSetOwnerValue(Obj *self, void *, uint32_t value) { Ev({evSetOwnerValue, self->id, value}); }
void *__fastcall LotGetKind(Obj *self, void *) { Ev({evKind, self->id}); return &gW.objs[36 + (self->kindIndex & 1)]; }
void __fastcall LotGetBounds(Obj *self, void *, int32_t *out)
{
    Ev({evBounds, self->id});
    for (int i = 0; i < 4; ++i) out[i] = self->bounds[i];
}
void *__fastcall LotIsValid(Obj *self, void *) { Ev({evValid, self->id}); return self->valid ? reinterpret_cast<void *>(0x8800 + self->id % 3) : nullptr; }
uint8_t __fastcall KindIsKind(Obj *self, void *, uint32_t kind) { return (kind == 0x10 && self->kindHit) ? 1 : 0; }
void *__fastcall DelegateGetFace(Obj *self, void *) { Ev({evDelegate, self->id, 16}); return &gW.objs[33]; }
uint32_t __fastcall FaceValue(Obj *self, void *) { Ev({evFace, self->id}); return self->ownerValue; }
void *__fastcall DelegateSlot36(Obj *self, void *) { Ev({evDelegate, self->id, 36}); return reinterpret_cast<void *>(0x7700 + 36); }
template <unsigned S> void **__fastcall DelegateIter(Obj *self, void *, void **out)
{
    Ev({evDelegate, self->id, S});
    *out = reinterpret_cast<void *>(0x7700 + S);
    return out;
}
uint32_t __fastcall PersistOwner(void *self, void *)
{
    Ev({evPersistOwner, Id(static_cast<uint8_t *>(self) - 4)});
    return gW.persistOwnerValue;
}
uint32_t __fastcall NbAddRef(Nb *self, void *) { Ev({evNbAddRef}); return ++self->mRefCount; }
void *__fastcall NbDelete(Nb *self, void *, uint32_t flags) { Ev({evNbDelete, flags}); return self; }
void *__fastcall NbNextLot(Nb *, void *) { Ev({evNextLot}); return reinterpret_cast<void *>(0x7755); }
void *__fastcall DirectorFramework(void *, void *) { return &gFramework; }
uint8_t __fastcall FrameworkCreate(Framework *, void *, uint32_t cls, uint32_t iid, void **out)
{
    const uint32_t n = gW.createCalls++;
    Ev({evCreate, cls, iid, n});
    if ((gW.failCreate >> (n % 32)) & 1) return 0;
    Obj *object = &gW.objs[kLots + (gW.nextMember++ % (kMembers * 3))];
    ++object->refs;
    *out = object;
    return 1;
}

void InstallVTables()
{
    gNbVT[1] = reinterpret_cast<void *>(&NbAddRef);
    gNbVT[55] = reinterpret_cast<void *>(&NbNextLot);
    gNbVT[76] = reinterpret_cast<void *>(&NbDelete);
    for (auto *vt : {gMemberVT, gLotVT, gKindVT, gDelegateVT, gFaceVT}) { vt[1] = reinterpret_cast<void *>(&ObjAddRef); vt[2] = reinterpret_cast<void *>(&ObjRelease); }
    gMemberVT[3] = reinterpret_cast<void *>(&MemberWrite);
    gMemberVT[4] = reinterpret_cast<void *>(&MemberRead);
    gMemberVT[6] = reinterpret_cast<void *>(&MemberSetIdentity);
    gMemberVT[45] = reinterpret_cast<void *>(&MemberSetOwnerValue);
    gLotVT[4] = reinterpret_cast<void *>(&LotGetKind);
    gLotVT[12] = reinterpret_cast<void *>(&LotGetBounds);
    gLotVT[18] = reinterpret_cast<void *>(&LotIsValid);
    gKindVT[24] = reinterpret_cast<void *>(&KindIsKind);
    gDelegateVT[16] = reinterpret_cast<void *>(&DelegateGetFace);
    gDelegateVT[36] = reinterpret_cast<void *>(&DelegateSlot36);
    gDelegateVT[46] = reinterpret_cast<void *>(&DelegateIter<46>);
    gDelegateVT[47] = reinterpret_cast<void *>(&DelegateIter<47>);
    gDelegateVT[48] = reinterpret_cast<void *>(&DelegateIter<48>);
    gDelegateVT[49] = reinterpret_cast<void *>(&DelegateIter<49>);
    gFaceVT[6] = reinterpret_cast<void *>(&FaceValue);
    gPersistVT[6] = reinterpret_cast<void *>(&PersistOwner);
    gDirectorVT[12] = reinterpret_cast<void *>(&DirectorFramework);
    gFrameworkVT[3] = reinterpret_cast<void *>(&FrameworkCreate);
    gFramework.vt = gFrameworkVT;
}

// ---- streams ----
struct InStream {
    void **vt;
    std::mt19937 rng;
    int32_t version;
    uint32_t sCalls = 0;       // GetSint32 calls
    int32_t remaining = 0;     // entries left in the map being read
    uint32_t mapsLeft = 3;
    uint32_t errorBits = 0;
    uint32_t errorCalls = 0;
    bool dropFieldReads = false; // some reads into the object's own fields fail and leave them as they were
} gIn;
// A failed read leaves its destination untouched, as cRZIStream's GetVoid does (DL). Only reads into memory off the stack
// fail this way (the object's fields, which Read 0x477350 reads into directly): a stack local would keep the original's
// uninitialised bytes.
bool DropRead(InStream *s, const void *out)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(out);
    const bool onStack = at >= __readfsdword(8) && at < __readfsdword(4);
    if (!s->dropFieldReads || onStack || s->rng() % 4 != 0) return false;
    Ev({0xd209});
    return true;
}
struct OutStream {
    void **vt;
    uint32_t errorBits = 0;
    uint32_t errorCalls = 0;
} gOut;
void *gInVT[24], *gOutVT[24];

uint8_t __fastcall InGetU8(InStream *s, void *, void *out)
{
    const uint8_t v = s->rng() % 3 == 0 ? uint8_t(s->rng()) : uint8_t(s->rng() % 2);
    Ev({evGetU8, v});
    *static_cast<uint8_t *>(out) = v;
    return 1;
}
uint8_t __fastcall InGetU16(InStream *s, void *, void *out)
{
    if (DropRead(s, out)) return 0;
    const uint16_t v = uint16_t(s->rng());
    Ev({evGetU16, v});
    *static_cast<uint16_t *>(out) = v;
    return 1;
}
uint8_t __fastcall InGetS32(InStream *s, void *, void *out)
{
    const uint32_t n = s->sCalls++;
    uint32_t v;
    if (n == 0) v = uint32_t(s->version);
    else if (s->remaining <= 0) {   // a count
        v = s->rng() % 5;
        s->remaining = int32_t(v);
    } else {                        // an id: wide, sometimes colliding
        v = s->rng() % 3 == 0 ? s->rng() % 4 : s->rng();
        --s->remaining;
    }
    Ev({evGetS32, n, v});
    *static_cast<uint32_t *>(out) = v;
    return 1;
}
uint8_t __fastcall InGetU32(InStream *s, void *, void *out)
{
    if (DropRead(s, out)) return 0;
    const uint32_t v = s->rng();
    Ev({evGetU32, v});
    *static_cast<uint32_t *>(out) = v;
    return 1;
}
uint8_t __fastcall InGetString(InStream *s, void *, void *out)
{
    const uint32_t v = s->rng() % 50;
    Ev({evGetString, v});
    static_cast<cRZString *>(out)->mString.assign(v, char('a' + v % 26));
    return 1;
}
uint8_t __fastcall InGetError(InStream *s, void *)
{
    const uint32_t n = s->errorCalls++;
    const uint8_t v = (s->errorBits >> (n % 32)) & 1 ? uint8_t(1 + n % 3) : 0;
    Ev({evGetError, n, v});
    return v;
}
void __fastcall InSetMeta(InStream *, void *, void *metadata)
{
    uint32_t words[5] = {};
    if (metadata) std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, metadata ? 1u : 0u, words[0] ? 1u : 0u, words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
uint8_t __fastcall OutSetU8(OutStream *, void *, uint32_t v) { Ev({evSetU8, v & 0xff}); return 1; }
uint8_t __fastcall OutSetU16(OutStream *, void *, uint32_t v) { Ev({evSetU16, v & 0xffff}); return 1; }
uint8_t __fastcall OutSetS32(OutStream *, void *, uint32_t v) { Ev({evSetS32, v}); return 1; }
uint8_t __fastcall OutSetU32(OutStream *, void *, uint32_t v) { Ev({evSetU32, v}); return 1; }
uint8_t __fastcall OutSetString(OutStream *, void *, void *text)
{
    const auto &s = static_cast<cRZString *>(text)->mString;
    Ev({evSetString, uint32_t(s.size())});
    for (char c : s) gW.events.push_back(uint8_t(c));
    return 1;
}
uint8_t __fastcall OutGetError(OutStream *s, void *)
{
    const uint32_t n = s->errorCalls++;
    const uint8_t v = (s->errorBits >> (n % 32)) & 1 ? uint8_t(1 + n % 3) : 0;
    Ev({evGetError, n, v});
    return v;
}
void __fastcall OutSetMeta(OutStream *, void *, void *metadata)
{
    uint32_t words[5] = {};
    if (metadata) std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, metadata ? 1u : 0u, words[0] ? 1u : 0u, words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
void InstallStreams()
{
    gInVT[5] = reinterpret_cast<void *>(&InGetU8);
    gInVT[6] = reinterpret_cast<void *>(&InGetU16);
    gInVT[8] = reinterpret_cast<void *>(&InGetS32);
    gInVT[9] = reinterpret_cast<void *>(&InGetU32);
    gInVT[15] = reinterpret_cast<void *>(&InGetString);
    gInVT[18] = reinterpret_cast<void *>(&InGetError);
    gInVT[19] = reinterpret_cast<void *>(&InSetMeta);
    gOutVT[5] = reinterpret_cast<void *>(&OutSetU8);
    gOutVT[6] = reinterpret_cast<void *>(&OutSetU16);
    gOutVT[8] = reinterpret_cast<void *>(&OutSetS32);
    gOutVT[9] = reinterpret_cast<void *>(&OutSetU32);
    gOutVT[15] = reinterpret_cast<void *>(&OutSetString);
    gOutVT[18] = reinterpret_cast<void *>(&OutGetError);
    gOutVT[19] = reinterpret_cast<void *>(&OutSetMeta);
    gIn.vt = gInVT;
    gOut.vt = gOutVT;
}

// ---- world ----
// `fill` bits: 1 lots, 2 members + state helper, 4 delegate, 8 owners, 16 texts
enum : uint32_t { fLots = 1, fMembers = 2, fDelegate = 4, fOwners = 8, fTexts = 16, fAll = 31 };

void Build(difftest::Context &ctx, bool original, uint32_t seed, uint32_t fill)
{
    World &w = gW;
    w.rng.seed(seed * 2654435761u + 17);
    w.events.clear();
    w.nextMember = 0;
    w.failCreate = 0;
    w.createCalls = 0;
    w.persistOwnerValue = (seed % 3 == 0) ? 0 : 0x500 + seed;
    std::mt19937 &r = w.rng;
    for (auto &b : w.work.before) b = uint8_t(r());
    for (auto &b : w.work.after) b = uint8_t(r());
    for (auto &b : w.work.bytes) b = uint8_t(r());
    InstallVTables();
    for (unsigned i = 0; i < kObjects; ++i) {
        Obj &o = w.objs[i];
        std::memset(&o, 0, sizeof(o));
        o.id = 100 + i;
        o.refs = 1000 + int32_t(i);
        o.vt = i < kLots ? gLotVT : i < kExtra ? gMemberVT : i == 32 ? gDelegateVT : i == 33 ? gFaceVT : i < 36 ? gMemberVT : gKindVT;
        o.ownerValue = (r() % 3 == 0) ? 0 : 0x900 + i;
        o.valid = (r() % 4) != 0;
        o.kindIndex = int32_t(r() % 2);
        o.kindHit = (i == 36) ? 1 : 0;
        // Small bounds so that points and rectangles hit often (signed, some negative).
        const int32_t left = int32_t(r() % 20) - 6, top = int32_t(r() % 20) - 6;
        o.bounds[0] = left; o.bounds[1] = top; o.bounds[2] = left + int32_t(r() % 8); o.bounds[3] = top + int32_t(r() % 8);
    }
    Nb *n = nbshape::Build(ctx, original, w.work.bytes);
    for (auto &word : n->mCode) word = uint16_t(r());
    n->mWidth = r(); n->mHeight = r();
    n->mFlags = r();
    n->mFlagByte = r() & 1;
    n->mInitialized = uint8_t(r());
    for (auto &k : n->mKey) k = r();
    if (fill & fLots) {
        const unsigned count = r() % 7;
        for (unsigned i = 0; i < count; ++i) {
            Obj *lot = &w.objs[r() % kLots];
            n->mLots[int32_t(r() % 12) - 4].Assign(lot);
        }
    }
    if (fill & fMembers) {
        n->mStateHelper = &w.objs[32 + 3];
        ++w.objs[32 + 3].refs;
        NeighborhoodMemberMap *maps[3] = {&n->mMembers1, &n->mMembers2, &n->mMembers3};
        for (unsigned m = 0; m < 3; ++m) {
            const unsigned count = r() % 5;
            for (unsigned i = 0; i < count; ++i) (*maps[m])[int16_t(r() % 9) - 4].Assign(&w.objs[kLots + m * 8 + r() % 8]);
        }
    }
    if (fill & fDelegate) { n->mDelegate = &w.objs[32]; ++w.objs[32].refs; }
    if (fill & fOwners) {
        n->mSecondOwner = &w.objs[35]; ++w.objs[35].refs;
        n->mOwner = &w.objs[34]; ++w.objs[34].refs;
        n->mField_A4 = &w.objs[34]; ++w.objs[34].refs;
    }
    if (fill & fTexts) {
        auto it = n->mTexts.emplace().first;
        auto &entry = const_cast<NeighborhoodTextEntry &>(*it);
        entry.mText.Construct("some text that is long enough to need a heap block, maybe");
        entry.mHeld = &w.objs[35];
        ++w.objs[35].refs;
    }
    n->mPlain[3] = 4;
    n->mPlain[9] = 1;
    // Fake vtables last, so the constructor above ran with the real ones: copies of the side's own, patched.
    nbshape::CopyOwn(gNbVT, 0);
    nbshape::CopyOwn(gPersistVT, 1);
    InstallVTables();
    nbshape::VTableOf(n, 0) = gNbVT;
    nbshape::VTableOf(n, 1) = gPersistVT;
    n->mRefCount = r() % 4;
    // The directory and the framework the helper creation goes through.
    w.events.clear();
}

using Blob = std::vector<uint32_t>;
struct RawNode { RawNode *left, *parent, *right; uint8_t color, isNil; };

void Capture(Blob &b, bool objectAlive = true)
{
    World &w = gW;
    b.push_back(0xb10b);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    if (objectAlive) {
        uint8_t copy[0xcc];
        std::memcpy(copy, w.work.bytes, sizeof(copy));
        auto word = [&](unsigned off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(copy + off); };
        const unsigned heads[] = {0x5c, 0x64, 0x78, 0x80, 0x88, 0xa8, 0xb4};
        for (unsigned off : heads) word(off) = 0;
        // Pointers into the fakes become ids; the lot cursor becomes the key it sits on (or a marker).
        Nb *n = N();
        for (unsigned off : {0x74u, 0xa4u, 0xbcu, 0xc0u, 0xc4u}) word(off) = Id(reinterpret_cast<void *>(word(off)));
        uint32_t cursor = 0xffffffff;
        for (auto it = n->mLots.begin(); it != n->mLots.end(); ++it) if (it == n->mLotCursor) cursor = uint32_t(it->first);
        word(0x70) = cursor;
        // A name longer than the small buffer lives in a heap block: its pointer becomes a marker (the text is compared below).
        if (n->mName.mString.capacity() > 15) word(0x40) = 0xbadbad;
        word(0) = word(0) == reinterpret_cast<uint32_t>(gNbVT) ? 0xfa4e : word(0);
        word(4) = word(4) == reinterpret_cast<uint32_t>(gPersistVT) ? 0xfa4f : word(4);
        nbshape::Norm(copy);
        for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
        b.push_back(0xc0de);
        for (char c : n->mName.mString) b.push_back(uint8_t(c));
        for (auto &e : n->mLots) { b.push_back(uint32_t(e.first)); b.push_back(Id(e.second.Get())); }
        for (auto *m : {&n->mMembers1, &n->mMembers2, &n->mMembers3}) {
            b.push_back(0xaaaa);
            for (auto &e : *m) { b.push_back(uint32_t(e.first)); b.push_back(Id(e.second.Get())); }
        }
        for (auto &e : n->mPlain) { b.push_back(e.first); b.push_back(e.second); }
        for (auto v : n->mServiceIds) b.push_back(v);
        for (auto &e : n->mTexts) { b.push_back(Id(e.mHeld)); b.push_back(uint32_t(e.mText.mString.size())); }
    }
    for (unsigned i = 0; i < kObjects; ++i) {
        b.push_back(uint32_t(w.objs[i].refs));
        b.push_back(w.objs[i].payload);
        b.push_back(w.objs[i].identity[0]);
        b.push_back(w.objs[i].identity[1]);
    }
    b.push_back(0xe7e7);
    for (uint32_t e : w.events) b.push_back(e);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}

// Runs `run(original)` on two identically built worlds; `run` returns the words to compare first. The directory/framework
// fakes are installed in the image's globals around the call.
struct Globals {
    difftest::Context &ctx;
    void *savedVT;
    int32_t savedGuard;
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        savedVT = *ctx.image.At<void *>(0x10c9390);
        savedGuard = *ctx.image.At<int32_t>(0x10c93f4);
        InstallVTables();
        InstallStreams();
        *ctx.image.At<void *>(0x10c9390) = gDirectorVT;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
    }
    ~Globals()
    {
        *ctx.image.At<void *>(0x10c9390) = savedVT;
        *ctx.image.At<int32_t>(0x10c93f4) = savedGuard;
    }
};

template <class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, uint32_t fill, Run run, bool alive = true)
{
    for (int route = 0; route < 2; ++route) {
        nbshape::gRoute = nbshape::Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Build(ctx, side == 0, seed, fill);
            const uint32_t r = uint32_t(run(side == 0));
            blobs[side].push_back(r);
            Capture(blobs[side], alive);
        }
        Same(ctx, blobs[0], blobs[1], what, seed | (route ? 0x80000000u : 0));
    }
    nbshape::gRoute = nbshape::kVTableRoute;
}

#define ORIG(Fn, rva) ctx.Original<Fn>(rva)
} // namespace

// ---- construction ----
DIFF_TEST(Neighborhood_Construct, 0x471d90)
{
    Globals globals(ctx);
    for (unsigned seed = 0; seed < 96; ++seed) {
        const bool adapterRoute = seed % 2 != 0;
        Guarded a, b;
        std::mt19937 rng(seed);
        for (auto &byte : reinterpret_cast<std::array<uint8_t, sizeof(a)> &>(a)) byte = uint8_t(rng());
        b = a;
        auto *left = reinterpret_cast<Nb *>(a.bytes);
        auto *right = reinterpret_cast<Nb *>(b.bytes);
        using Fn = Nb *(__thiscall *)(Nb *);
        auto *ra = ORIG(Fn, 0x471d90)(left);
        auto *rb = adapterRoute ? cTSNeighborhoodAdapters::Construct(right, nullptr) : ::new (right) Nb;
        if (!nbshape::FacesWhereExpected(right)) ctx.Fail("construct seed %u faces", seed);
        ++ctx.cases;
        if (ra != left || rb != right) ctx.Fail("construct seed %u return", seed);
        // The head nodes are separate heap blocks: compare their links and flags, then normalise the pointers.
        auto headsOk = [&](Nb *t) {
            bool ok = true;
            for (unsigned off : {0x5c, 0x64, 0x78, 0x80, 0x88, 0xa8}) {
                auto *head = *reinterpret_cast<RawNode **>(reinterpret_cast<uint8_t *>(t) + off);
                ok = ok && head && head->left == head && head->parent == head && head->right == head && head->color == 1 && head->isNil == 1;
            }
            return ok;
        };
        if (!headsOk(left) || !headsOk(right)) ctx.Fail("construct seed %u head nodes", seed);
        // The id set holds 0xa4, 0xaf, 0x9e.
        if (left->mServiceIds != right->mServiceIds || left->mServiceIds.size() != 3 || !left->mServiceIds.count(0x9e))
            ctx.Fail("construct seed %u id set", seed);
        if (left->mLotCursor != left->mLots.end() || right->mLotCursor != right->mLots.end()) ctx.Fail("construct seed %u cursor", seed);
        for (unsigned f = 0; f < nbshape::kFaces; ++f) {
            if (nbshape::VTableOf(left, f) != ctx.image.At<void *>(nbshape::kGameVTable[f])) ctx.Fail("construct seed %u game vtable %u", seed, f);
            if (nbshape::VTableOf(right, f) != nbshape::Ours()[f] || nbshape::Ours()[f] == ctx.image.At<void *>(nbshape::kGameVTable[f]))
                ctx.Fail("construct seed %u our vtable %u", seed, f);
        }
        nbshape::Norm(a.bytes);
        nbshape::Norm(b.bytes);
        // Normalise the heap pointers and compare every byte (padding included).
        uint32_t idsA[2], idsB[2];
        for (Nb *t : {left, right}) {
            uint32_t *words = reinterpret_cast<uint32_t *>(t);
            uint32_t *ids = t == left ? idsA : idsB;
            ids[0] = words[0xb4 / 4]; ids[1] = words[0xb4 / 4 + 1];
            for (unsigned off : {0x5c, 0x64, 0x70, 0x78, 0x80, 0x88, 0xa8, 0xb4}) words[off / 4] = 0;
        }
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("construct seed %u bytes differ", seed);
        if (std::memcmp(a.before, b.before, 16) != 0 || std::memcmp(a.after, b.after, 16) != 0) ctx.Fail("construct seed %u guards", seed);
        // Free the nodes (the set's three nodes included) with the replacement's destructor on both objects.
        // (the pointers were zeroed above: rebuild a throwaway object instead of destroying these)
    }
    std::printf("  Construct: 48 randomized allocations\n");
}

DIFF_TEST(Neighborhood_Create, 0x473b80)
{
    Globals globals(ctx);
    for (unsigned i = 0; i < 16; ++i) {
        // Every heap block is poisoned on both sides (heap_poison.h, DT), so bytes the constructor leaves hold the pattern
        // on both sides: the whole object is compared, heap pointers (as in Construct above) set aside.
        heappoison::Scope poison(ctx, i % 2 ? 0x5a : 0xcd);
        using Fn = Nb *(__cdecl *)();
        Nb *a = ORIG(Fn, 0x473b80)();
        Nb *b = Nb::Create();
        ++ctx.cases;
        if (!a || !b) { ctx.Fail("create returned null"); continue; }
        if (HeapSize(GetProcessHeap(), 0, a) < 0xcc || HeapSize(GetProcessHeap(), 0, b) < 0xcc) ctx.Fail("create block too small");
        if (a->mServiceIds.size() != 3 || b->mServiceIds.size() != 3 || a->mFlags != 1 || b->mFlags != 1 || a->mWidth != 0x80 || b->mWidth != 0x80)
            ctx.Fail("create fields");
        if (std::memcmp(&a->mName, &b->mName, sizeof(cRZString)) != 0) ctx.Fail("create name bytes");
        uint32_t wordsA[0xcc / 4], wordsB[0xcc / 4];
        std::memcpy(wordsA, a, sizeof(wordsA));
        std::memcpy(wordsB, b, sizeof(wordsB));
        nbshape::Norm(reinterpret_cast<uint8_t *>(wordsA));
        nbshape::Norm(reinterpret_cast<uint8_t *>(wordsB));
        for (unsigned off : {0x5c, 0x64, 0x70, 0x78, 0x80, 0x88, 0xa8, 0xb4}) wordsA[off / 4] = wordsB[off / 4] = 0;
        for (unsigned k = 0; k < 0xcc / 4; ++k)
            if (wordsA[k] != wordsB[k]) {
                ctx.Fail("create %u: word +0x%x differs (original %08x, ours %08x)", i, k * 4, wordsA[k], wordsB[k]);
                break;
            }
        using DeleteFn = void *(__thiscall *)(Nb *, uint32_t);
        ORIG(DeleteFn, 0x4728c0)(a, 1);
        delete b; // our scalar deleting destructor through slot 76
    }
}

// ---- destruction ----
DIFF_TEST(Neighborhood_Destruct, 0x472490)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 120; ++seed) {
        const uint32_t fill = seed % 32;
        Pair(ctx, "Destruct", seed, fill, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *);
            Nb *n = N();
            if (original) ORIG(Fn, 0x472490)(n);
            else if (nbshape::gRoute == nbshape::kAdapterRoute) cTSNeighborhoodAdapters::Destruct(n, nullptr);
            else n->Nb::~Nb();
            // The destructor leaves the side's own vtables of the four faces.
            uint32_t codes = 0;
            for (unsigned f = 0; f < nbshape::kFaces; ++f) codes = codes * 16 + (nbshape::VtCode(nbshape::P(nbshape::VTableOf(n, f))) & 0xff00000f);
            return codes;
        }, false);
    }
}

DIFF_TEST(Neighborhood_DeletingDestructor, 0x4728c0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 60; ++seed) {
        // flags without bit 0 keep the memory (the world's buffer); the bit-0 path is covered by Create.
        Pair(ctx, "DeletingDestructor", seed, seed % 32, [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *, uint32_t);
            Nb *n = N();
            const uint32_t flags = (seed % 4) * 2;
            void *r = nbshape::Call<Fn>(ctx, original, n, 0, 0x4728c0, NB_ADAPTER(DeletingDestructor), flags);
            return (r == n) * 0x100 + flags;
        }, false);
    }
}

DIFF_TEST(Neighborhood_Release, 0x4786e0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 80; ++seed)
        Pair(ctx, "Release", seed, seed % 32, [&](bool original) {
            using Fn = uint32_t(__thiscall *)(Nb *);
            N()->mRefCount = seed % 5;
            return nbshape::Call<Fn>(ctx, original, N(), 0, 0x4786e0, NB_ADAPTER(Release));
        });
}

DIFF_TEST(Neighborhood_QueryInterface, 0x4771c0)
{
    Globals globals(ctx);
    constexpr uint32_t ids[] = {1, 0x7aa51c46, 0xe4fda3d4, 0x456b8f1d, 0x8db5bf48, 0, 2, 0x7aa51c47, 0xffffffff, 0x456b8f1c, 0x8db5bf49};
    for (unsigned i = 0; i < 66; ++i) {
        const uint32_t id = ids[i % 11];
        Pair(ctx, "QueryInterface", i, 0, [&](bool original) {
            struct Output { uint32_t before; void *value; uint32_t after; } out{0x98ab76cd, reinterpret_cast<void *>(0x1234abcd), 0xcdef4321};
            using Fn = uint8_t(__thiscall *)(Nb *, uint32_t, void **);
            const uint8_t r = nbshape::Call<Fn>(ctx, original, N(), 0, 0x4771c0, NB_ADAPTER(QueryInterface), id, &out.value);
            gW.events.push_back(out.before); gW.events.push_back(out.after);
            gW.events.push_back(out.value == reinterpret_cast<void *>(0x1234abcd) ? 0xffffffff : Id(out.value));
            return r; // AL only: the original returns through AL
        });
    }
}

DIFF_TEST(Neighborhood_GetGZCLSID, 0x473f10)
{
    ++ctx.cases;
    using Fn = uint32_t(__thiscall *)(Nb *);
    if (ORIG(Fn, 0x473f10)(nullptr) != 0x4e474248 || reinterpret_cast<Fn>(nbshape::Ours()[0][5])(nullptr) != 0x4e474248 ||
        cTSNeighborhoodAdapters::GetGZCLSID(nullptr, nullptr) != 0x4e474248)
        ctx.Fail("GetGZCLSID");
}

// ---- Write / Read ----
DIFF_TEST(Neighborhood_Write, 0x47af80)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 200; ++seed)
        Pair(ctx, "Write", seed, fMembers | (seed % 2 ? fLots : 0), [&](bool original) {
            gOut.errorBits = seed % 3 == 0 ? seed * 2654435761u : 0;
            gOut.errorCalls = 0;
            using Fn = uint8_t(__thiscall *)(Nb *, void *);
            return nbshape::Call<Fn>(ctx, original, N(), 0, 0x47af80, NB_ADAPTER(Write), &gOut);
        });
}

DIFF_TEST(Neighborhood_Read, 0x477350)
{
    Globals globals(ctx);
    const int32_t versions[] = {0, 1, 0x13, 0x14, 0x64, 0x65, 0x66, 0x6a, 0x6b, 0x6d, 0x6e, 0x6f, 0x70, 0xcb, 0x100, -1};
    for (uint32_t seed = 0; seed < 24; ++seed) {
        for (int32_t version : versions) {
            const uint32_t fill = (seed % 2 ? fMembers : 0) | (seed % 3 == 0 ? fDelegate : 0);
            Pair(ctx, "Read", seed * 1000 + uint32_t(version), fill, [&](bool original) {
                gIn.rng.seed(seed * 7919 + 3);
                gIn.version = version;
                gIn.sCalls = 0;
                gIn.remaining = 0;
                gIn.errorCalls = 0;
                gIn.errorBits = seed % 4 == 0 ? seed * 2654435761u : 0;
                gIn.dropFieldReads = seed % 5 == 2;
                gW.failCreate = seed % 5 == 0 ? 0x6 : 0; // never the first create: the original crashes on a missing state helper (ORIGINAL-BUG, see Read)
                using Fn = uint8_t(__thiscall *)(Nb *, void *);
                return nbshape::Call<Fn>(ctx, original, N(), 0, 0x477350, NB_ADAPTER(Read), &gIn);
            });
        }
    }
}

DIFF_TEST(Neighborhood_EnsureStateHelper, 0x473a70)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 120; ++seed)
        Pair(ctx, "EnsureStateHelper", seed, (seed % 2 ? fMembers : 0) | (seed % 3 == 0 ? fDelegate : 0), [&](bool original) {
            gW.failCreate = seed % 7 == 0 ? 1 : 0;
            using Fn = void(__thiscall *)(Nb *);
            if (original) ORIG(Fn, 0x473a70)(N());
            else Nb::EnsureStateHelper(N()); // not a slot: the hook's own function on both routes
            return 0;
        });
}

// ---- small accessors and the maps ----
DIFF_TEST(Neighborhood_Flags, 0x476330)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Pair(ctx, "GetFlag0", seed, 0, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(Nb *);
            return nbshape::Call<Fn>(ctx, original, N(), 0, 0x476330, NB_ADAPTER(GetFlag0)); // AL only
        });
        Pair(ctx, "SetFlag0", seed, 0, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *);
            nbshape::Call<Fn>(ctx, original, N(), 0, 0x476e70, NB_ADAPTER(SetFlag0));
            return 0;
        });
        Pair(ctx, "GetFlagByte", seed, 0, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(Nb *);
            return nbshape::Call<Fn>(ctx, original, N(), 0, 0x4749c0, NB_ADAPTER(GetFlagByte));
        });
    }
}

DIFF_TEST(Neighborhood_Owners, 0x479be0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 90; ++seed) {
        Obj *replacement = seed % 5 == 0 ? nullptr : &gW.objs[34 + seed % 3];
        const uint32_t fill = fDelegate | fOwners;
        Pair(ctx, "SetDelegate", seed, fill, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, void *);
            Obj *target = seed % 4 == 0 ? static_cast<Obj *>(N()->mDelegate) : replacement; // the same object: nothing happens
            nbshape::Call<Fn>(ctx, original, N(), 0, 0x479be0, NB_ADAPTER(SetDelegate), target);
            return 0;
        });
        Pair(ctx, "SetSecondOwner", seed, fill, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, void *);
            Obj *target = seed % 4 == 0 ? static_cast<Obj *>(N()->mSecondOwner) : replacement;
            nbshape::Call<Fn>(ctx, original, N(), 0, 0x479b80, NB_ADAPTER(SetSecondOwner), target);
            return 0;
        });
    }
}

DIFF_TEST(Neighborhood_CodeWords, 0x475450)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 64; ++seed) {
        const int32_t index = int32_t(seed % 16);
        Pair(ctx, "GetCodeWord", seed, 0, [&](bool original) {
            using Fn = uint16_t(__thiscall *)(Nb *, int32_t);
            return nbshape::Call<Fn>(ctx, original, N(), 0, 0x475450, NB_ADAPTER(GetCodeWord), index); // AX only
        });
        Pair(ctx, "SetCodeWord", seed, 0, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, int32_t, uint16_t);
            nbshape::Call<Fn>(ctx, original, N(), 0, 0x479bc0, NB_ADAPTER(SetCodeWord), index, uint16_t(seed * 977));
            return 0;
        });
        Pair(ctx, "SetSize", seed, 0, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, uint32_t, uint32_t);
            nbshape::Call<Fn>(ctx, original, N(), 0, 0x479c30, NB_ADAPTER(SetSize), seed * 31, seed * 7);
            return 0;
        });
    }
}

DIFF_TEST(Neighborhood_StateHelperOrDelegated, 0x4742e0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 40; ++seed)
        Pair(ctx, "GetStateHelperOrDelegated", seed, fMembers | (seed % 2 ? fDelegate : 0), [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *);
            void *result = nbshape::Call<Fn>(ctx, original, N(), 0, 0x4742e0, NB_ADAPTER(GetStateHelperOrDelegated));
            // A fake object (by id) or the delegate fake's magic value.
            return Id(result) != 1 ? Id(result) : uint32_t(reinterpret_cast<uintptr_t>(result));
        });
}

DIFF_TEST(Neighborhood_ClearMemberMaps, 0x473740)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 90; ++seed) {
        using Fn = void(__thiscall *)(Nb *);
        Pair(ctx, "ClearMemberMap1", seed, fMembers, [&](bool original) { nbshape::Call<Fn>(ctx, original, N(), 0, 0x473740, NB_ADAPTER(ClearMemberMap1)); return 0; });
        Pair(ctx, "ClearMemberMap2", seed, fMembers, [&](bool original) { nbshape::Call<Fn>(ctx, original, N(), 0, 0x473710, NB_ADAPTER(ClearMemberMap2)); return 0; });
        Pair(ctx, "ClearMemberMap3", seed, fMembers, [&](bool original) { nbshape::Call<Fn>(ctx, original, N(), 0, 0x473770, NB_ADAPTER(ClearMemberMap3)); return 0; });
    }
}

DIFF_TEST(Neighborhood_MemberMapIterators, 0x4750e0)
{
    Globals globals(ctx);
    struct Entry { uint32_t rva; void *adapter; };
    const Entry entries[] = {{0x4750e0, NB_ADAPTER(MemberMap1Begin)}, {0x4750f0, NB_ADAPTER(MemberMap1End)}, {0x474260, NB_ADAPTER(MemberMap2Begin)},
        {0x474290, NB_ADAPTER(MemberMap2End)}, {0x4752b0, NB_ADAPTER(MemberMap3Begin)}, {0x4752e0, NB_ADAPTER(MemberMap3End)}};
    for (const Entry &entry : entries) {
        for (uint32_t seed = 0; seed < 24; ++seed) {
            Pair(ctx, "MemberMapIterator", seed * 16 + (entry.rva & 0xff) % 16, fMembers | (seed % 2 ? fDelegate : 0), [&](bool original) {
                using Fn = void **(__thiscall *)(Nb *, void **);
                void *slot = reinterpret_cast<void *>(0xdeadbeef);
                void **r = nbshape::Call<Fn>(ctx, original, N(), 0, entry.rva, entry.adapter, &slot);
                // The iterator is a node pointer into the map: report which entry it is (by position), or its raw value for the fakes.
                uint32_t position = 0xffffffff;
                auto *map = (entry.rva <= 0x4750f0) ? &N()->mMembers1 : (entry.rva <= 0x474290) ? &N()->mMembers2 : &N()->mMembers3;
                uint32_t i = 0;
                for (auto it = map->begin(); it != map->end(); ++it, ++i) {
                    void *raw;
                    std::memcpy(&raw, &it, sizeof(raw));
                    if (slot == raw) position = i;
                }
                auto endIt = map->end();
                void *endRaw;
                std::memcpy(&endRaw, &endIt, sizeof(endRaw));
                if (slot == endRaw) position = 0xfffffff0;
                gW.events.push_back(r == &slot);
                gW.events.push_back(position);
                gW.events.push_back((reinterpret_cast<uintptr_t>(slot) >> 8) == 0x77 ? uint32_t(reinterpret_cast<uintptr_t>(slot)) : 0);
                return 0;
            });
        }
    }
}

DIFF_TEST(Neighborhood_LotCursor, 0x476f50)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 80; ++seed) {
        Pair(ctx, "NextLot", seed, fLots, [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *);
            Nb *n = N();
            // Start the cursor somewhere in the map, or on the head.
            auto it = n->mLots.begin();
            for (uint32_t i = 0; i < seed % 6 && it != n->mLots.end(); ++i) ++it;
            n->mLotCursor = it;
            uint32_t sum = 0;
            for (int i = 0; i < 3; ++i) sum = sum * 131 + Id(nbshape::Call<Fn>(ctx, original, n, 0, 0x476f50, NB_ADAPTER(NextLot)));
            return sum;
        });
        Pair(ctx, "RewindLots", seed, fLots, [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *);
            return uint32_t(reinterpret_cast<uintptr_t>(nbshape::Call<Fn>(ctx, original, N(), 0, 0x473b60, NB_ADAPTER(RewindLots))));
        });
    }
}

DIFF_TEST(Neighborhood_GetLot, 0x475000)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 120; ++seed)
        Pair(ctx, "GetLot", seed, fLots, [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *, int32_t);
            uint32_t sum = 0;
            for (int32_t number = -6; number < 10; ++number)
                sum = sum * 131 + Id(nbshape::Call<Fn>(ctx, original, N(), 0, 0x475000, NB_ADAPTER(GetLot), number));
            return sum;
        });
}

DIFF_TEST(Neighborhood_LotSearches, 0x474a20)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Pair(ctx, "FindLotAt", seed, fLots, [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *, int32_t, int32_t);
            uint32_t sum = 0;
            for (int32_t x = -8; x < 22; x += 3) for (int32_t y = -8; y < 22; y += 3)
                sum = sum * 131 + Id(nbshape::Call<Fn>(ctx, original, N(), 0, 0x474a20, NB_ADAPTER(FindLotAt), x, y));
            return sum;
        });
    }
}
DIFF_TEST(Neighborhood_FindLotOverlapping, 0x473fd0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Pair(ctx, "FindLotOverlapping", seed, fLots, [&](bool original) {
            using Fn = void *(__thiscall *)(Nb *, const int32_t *);
            uint32_t sum = 0;
            std::mt19937 q(seed);
            for (int i = 0; i < 12; ++i) {
                int32_t rect[4] = {int32_t(q() % 24) - 8, int32_t(q() % 24) - 8, 0, 0};
                rect[2] = rect[0] + int32_t(q() % 8) - (i % 4 == 0 ? 2 : 0);
                rect[3] = rect[1] + int32_t(q() % 8) - (i % 5 == 0 ? 2 : 0);
                sum = sum * 131 + Id(nbshape::Call<Fn>(ctx, original, N(), 0, 0x473fd0, NB_ADAPTER(FindLotOverlapping), rect));
            }
            return sum;
        });
    }
}
DIFF_TEST(Neighborhood_CollectLotsOverlapping, 0x475100)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Pair(ctx, "CollectLotsOverlapping", seed, fLots, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, const int32_t *, std::vector<void *> *);
            uint32_t sum = 0;
            std::mt19937 q(seed + 1);
            for (int i = 0; i < 6; ++i) {
                int32_t rect[4] = {int32_t(q() % 24) - 8, int32_t(q() % 24) - 8, 0, 0};
                rect[2] = rect[0] + int32_t(q() % 10);
                rect[3] = rect[1] + int32_t(q() % 10);
                std::vector<void *> out;
                out.reserve(64); out.push_back(&gW.objs[0]); // cleared first; room for every record, so neither side reallocates (the test heap vs the game CRT)
                nbshape::Call<Fn>(ctx, original, N(), 0, 0x475100, NB_ADAPTER(CollectLotsOverlapping), rect, &out);
                sum = sum * 131 + uint32_t(out.size());
                for (void *p : out) sum = sum * 131 + uint32_t(reinterpret_cast<uintptr_t>(p));
            }
            return sum;
        });
    }
}

DIFF_TEST(Neighborhood_Constants, 0x475c60)
{
    Globals globals(ctx);
    ++ctx.cases;
    using WordFn = uint32_t(__thiscall *)(Nb *);
    using FloatFn = float(__thiscall *)(Nb *);
    // Ours through our vtable's slots 32 / 34 / 33 / 50 and through the adapters (`this` unused).
    void **ours = nbshape::Ours()[0];
    using A = cTSNeighborhoodAdapters;
    const uint32_t word = ORIG(WordFn, 0x475c60)(nullptr);
    if (word != reinterpret_cast<WordFn>(ours[32])(nullptr) || word != A::GetGlobalWord(nullptr, nullptr)) ctx.Fail("GetGlobalWord");
    const float a = ORIG(FloatFn, 0x475c70)(nullptr), b = ORIG(FloatFn, 0x475c80)(nullptr), ten = ORIG(FloatFn, 0x47a4a0)(nullptr);
    if (a != reinterpret_cast<FloatFn>(ours[34])(nullptr) || a != A::GetGlobalFloatA(nullptr, nullptr)) ctx.Fail("GetGlobalFloatA");
    if (b != reinterpret_cast<FloatFn>(ours[33])(nullptr) || b != A::GetGlobalFloatB(nullptr, nullptr)) ctx.Fail("GetGlobalFloatB");
    if (ten != reinterpret_cast<FloatFn>(ours[50])(nullptr) || ten != A::GetTen(nullptr, nullptr)) ctx.Fail("GetTen");
}

DIFF_TEST(Neighborhood_PersistFace, 0x4749d0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Pair(ctx, "GetKey", seed, 0, [&](bool original) {
            using Fn = uint32_t *(__thiscall *)(void *, uint32_t *); // the hidden result pointer, returned
            uint32_t out[4] = {1, 2, 3, 4};
            uint32_t *r = nbshape::Call<Fn>(ctx, original, N(), 1, 0x4749d0, NB_ADAPTER(GetKey), out);
            for (uint32_t v : out) gW.events.push_back(v);
            return r == out;
        });
        Pair(ctx, "SetKey", seed, 0, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(void *, const uint32_t *);
            const uint32_t key[4] = {seed, seed * 3, seed * 5, seed * 7};
            return nbshape::Call<Fn>(ctx, original, N(), 1, 0x479a20, NB_ADAPTER(SetKey), key);
        });
        Pair(ctx, "SetInstance", seed, 0, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(void *, uint32_t);
            return nbshape::Call<Fn>(ctx, original, N(), 1, 0x4799f0, NB_ADAPTER(SetInstance), seed * 11);
        });
    }
}
