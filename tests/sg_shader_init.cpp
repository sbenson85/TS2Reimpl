// Differential tests for nGZSceneGraph::cShaderInit (lane RZ, src/sg/cShaderInit.*).
//
// The game keeps its vtable; these are the bodies hooked at their RVAs. Each test builds two objects with the image's
// constructor, brings both to the same random state through the image's own slots (the same seed), runs the original body on one and
// ours on the other with the same arguments, and compares the answer, the whole state afterwards (the object serialised
// with the fake objects as ids and the vectors as counts and contents), the log of every call on the fakes and the
// allocations made and freed.
// The fakes: counted objects (log QueryInterface / AddRef / Release), names (the pool asks them to write themselves) and
// the texture manager the image caches at 0x10f8f94 (make, count, destroy, get).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "sg/cShaderInit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

using namespace nGZSceneGraph;

namespace {

std::string gLog;

void Note(const char *what, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %x %x %x\n", what, a, b, c);
    gLog += buf;
}

uint32_t gQiMask = 0;
struct FakeObj : cIShaderObject {
    uint32_t id = 0;
    bool QueryInterface(uint32_t iid, void **out) override
    {
        Note("qi", id, iid, out ? 1 : 0);
        if (((iid ^ gQiMask) >> id & 1) == 0) return false;
        if (out) *out = this;
        return true;
    }
    uint32_t AddRef() override { Note("addref", id); return 1; }
    uint32_t Release() override { Note("release", id); return 1; }
};
FakeObj gObjs[6];
void InitObjs()
{
    for (uint32_t i = 0; i < 6; ++i) gObjs[i].id = i + 1;
}
uint32_t IdOf(const void *p)
{
    if (!p) return 0;
    for (const FakeObj &o : gObjs)
        if (p == static_cast<const cIShaderObject *>(&o)) return o.id;
    return 0xdead;
}

// A name as the pool takes it: the pool keeps polymorphic objects in a character buffer. The object's size is the word at +6;
// slot 1 builds a copy of the object at the given address; slot 0 (called with 0) destroys one in place.
struct FakeName {
    void **vt;
    uint16_t pad;
    uint16_t len;
    uint32_t id;
};
void __fastcall NameWrite(FakeName *self, void *, char *dest)
{
    Note("name", self->id, self->len);
    std::memset(dest, 0x5a, self->len);
    std::memcpy(dest, self, sizeof(FakeName));
}
void __fastcall NameDestroy(FakeName *self, void *, uint32_t flag)
{
    Note("namedtor", self->id, flag);
}
void *gNameVt[2] = {reinterpret_cast<void *>(&NameDestroy), reinterpret_cast<void *>(&NameWrite)};
FakeName gNames[4];
void InitNames()
{
    const uint16_t lens[4] = {12, 16, 40, 300};
    for (uint32_t i = 0; i < 4; ++i) {
        gNames[i].vt = gNameVt;
        gNames[i].pad = 0;
        gNames[i].len = lens[i];
        gNames[i].id = i;
    }
}

// The texture manager.
struct FakeTexture : cIShaderObject {
    bool QueryInterface(uint32_t, void **) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
};
FakeTexture gTexture;
uint32_t __fastcall MgrMake(void *, void *, uint32_t source) { Note("make", source); return source + 0x100; }
void __fastcall MgrCount(void *, void *, uint32_t id) { Note("count", id); }
void __fastcall MgrDestroy(void *, void *, uint32_t id) { Note("destroy", id); }
void *__fastcall MgrGet(void *, void *, uint32_t id) { Note("texture", id); return &gTexture; }
void *gMgrVt[40];
void *gMgrObject[1] = {gMgrVt};

struct PlantManager {
    void **cache;
    void *saved;
    explicit PlantManager(difftest::Context &ctx)
    {
        InitObjs();
        InitNames();
        std::memset(gMgrVt, 0, sizeof(gMgrVt));
        gMgrVt[17] = reinterpret_cast<void *>(&MgrMake);
        gMgrVt[18] = reinterpret_cast<void *>(&MgrCount);
        gMgrVt[19] = reinterpret_cast<void *>(&MgrDestroy);
        gMgrVt[23] = reinterpret_cast<void *>(&MgrGet);
        cache = ctx.image.At<void *>(0x10f8f94);
        saved = *cache;
        *cache = gMgrObject;
        gLog.reserve(1 << 18);
    }
    ~PlantManager() { *cache = saved; }
};

// ---- state ----------------------------------------------------------------------------------------------------------------
void Add(std::string &s, const char *tag, uint32_t a = 0, uint32_t b = 0)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %x %x;", tag, a, b);
    s += buf;
}
void SerObjs(std::string &s, const ObjectVec &v)
{
    Add(s, "vec", v.mBegin ? uint32_t(v.mEnd - v.mBegin) : 0, v.mBegin ? uint32_t(v.mCap - v.mBegin) : 0);
    for (cIShaderObject **p = v.mBegin; p && p != v.mEnd; ++p) Add(s, "o", IdOf(*p));
}
void SerPool(std::string &s, const NamePool &p)
{
    const char *cb = reinterpret_cast<const char *>(p.w[0]);
    const char *ce = reinterpret_cast<const char *>(p.w[1]);
    const uint32_t *ob = reinterpret_cast<const uint32_t *>(p.w[3]);
    const uint32_t *oe = reinterpret_cast<const uint32_t *>(p.w[4]);
    Add(s, "pool", cb ? uint32_t(ce - cb) : 0, cb ? p.w[2] - p.w[0] : 0);
    Add(s, "offs", ob ? uint32_t(oe - ob) : 0, ob ? (p.w[5] - p.w[3]) / 4 : 0);
    s.append(cb, cb ? size_t(ce - cb) : 0);
    for (const uint32_t *o = ob; o && o != oe; ++o) Add(s, "off", *o);
}
std::string Ser(const ShaderInitData &d)
{
    std::string s;
    Add(s, "attr", d.mAttribute);
    SerObjs(s, d.mObjects);
    SerPool(s, d.mNames);
    Add(s, "descs", d.mDescs.mBegin ? uint32_t(d.mDescs.mEnd - d.mDescs.mBegin) : 0, d.mDescs.mBegin ? uint32_t(d.mDescs.mCap - d.mDescs.mBegin) : 0);
    for (const ShaderDescriptor *p = d.mDescs.mBegin; p && p != d.mDescs.mEnd; ++p) {
        Add(s, "d", IdOf(p->mObject00), IdOf(p->mObject1c));
        Add(s, "e", IdOf(p->mObject20), IdOf(p->mObject24));
        Add(s, "f", IdOf(p->mObject28));
        SerObjs(s, p->mPartsA);
        SerObjs(s, p->mPartsB);
        SerPool(s, p->mNames);
        for (const ShaderTextureSlot &t : p->mTextures) Add(s, "t", t.mHandle, t.mHandle == 0xffffffff ? 0 : t.mValue); // an empty entry's value is never initialised
        Add(s, "b", p->mByteC4 | p->mByteC5 << 8 | p->mByteC6 << 16 | uint32_t(p->mByteC7) << 24);
    }
    for (uint32_t r : d.mRanges) Add(s, "r", r);
    return s;
}
std::string Ser(const cShaderInit &o)
{
    std::string s;
    Add(s, "ref", o.mRefCount);
    return s + Ser(o.mData);
}

// ---- setup through the image's slots ---------------------------------------------------------------------------------------
int32_t PassOf(std::mt19937 &rng)
{
    const uint32_t k = rng() % 12;
    if (k < 8) return int32_t(rng() % 4);
    if (k < 10) return int32_t(rng() % 25);
    if (k == 10) return int32_t(20 + rng() % 4);
    return -1 - int32_t(rng() % 3);
}
cIShaderObject *ObjOf(std::mt19937 &rng)
{
    const uint32_t k = rng() % 7;
    return k == 6 ? nullptr : &gObjs[k];
}
cIShaderObject *ObjNN(std::mt19937 &rng) { return &gObjs[rng() % 6]; }
void Setup(cShaderInit *o, uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    gQiMask = 0;
    const uint32_t n = rng() % 16;
    for (uint32_t i = 0; i < n; ++i) {
        const int32_t pass = PassOf(rng);
        switch (rng() % 18) {
        case 0: o->SetObject00(pass, ObjOf(rng)); break;
        case 1: o->SetObject1c(pass, ObjOf(rng)); break;
        case 2: o->SetObject20(pass, ObjOf(rng)); break;
        case 3: o->SetObject24(pass, ObjOf(rng)); break;
        case 4: o->SetObject28(pass, ObjOf(rng)); break;
        case 5: o->AddPartA(pass, ObjNN(rng)); break;
        case 6: o->AddPartA(pass, ObjNN(rng)); o->AddPartB(pass, ObjNN(rng)); break;
        case 7: o->AddPartB(pass, ObjNN(rng)); break;
        case 8: o->AddObject(ObjNN(rng)); break;
        case 9: o->AddPassName(pass, &gNames[rng() % 4]); break;
        case 10: o->AddName(&gNames[rng() % 4]); break;
        case 11: o->SetTexture(pass, rng() % 16, rng() % 50, rng()); break;
        case 12: o->SetByteC4(pass, uint8_t(rng())); o->SetByteC5(pass, uint8_t(rng())); break;
        case 13: o->SetByteC6(pass, uint8_t(rng())); o->SetByteC7(pass, uint8_t(rng())); break;
        case 14: o->SetRange(rng() % 13, rng(), rng()); break;
        case 15: o->SetAttribute(rng()); break;
        default: o->AddObject(ObjNN(rng)); o->AddPartA(pass, ObjNN(rng)); break;
        }
    }
}

// ---- the pair runner -------------------------------------------------------------------------------------------------------
using Ctor = cShaderInit *(__thiscall *)(cShaderInit *);
using Dtor = void(__thiscall *)(cShaderInit *);

struct Pair {
    cShaderInit *a, *b;
};
Pair MakePair(difftest::Context &ctx, uint32_t seed)
{
    auto ctor = ctx.Original<Ctor>(0xaa7b90);
    Pair p{static_cast<cShaderInit *>(::operator new(sizeof(cShaderInit))), static_cast<cShaderInit *>(::operator new(sizeof(cShaderInit)))};
    ctor(p.a);
    ctor(p.b);
    Setup(p.a, seed);
    Setup(p.b, seed);
    return p;
}
void FreePair(difftest::Context &ctx, Pair p)
{
    auto dtor = ctx.Original<Dtor>(0xaa7d70);
    dtor(p.a);
    dtor(p.b);
    ::operator delete(p.a);
    ::operator delete(p.b);
}

// fa / fb: uint64_t(cShaderInit *, std::mt19937 &args, std::string &extra)
template <class FA, class FB>
void Compare(difftest::Context &ctx, uint32_t seed0, int n, int minChanged, FA fa, FB fb)
{
    PlantManager plant(ctx);
    int changed = 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t seed = seed0 + uint32_t(i);
        Pair p = MakePair(ctx, seed);
        const std::string before = Ser(*p.a);
        std::mt19937 argA(seed ^ 0x9999), argB(seed ^ 0x9999);
        std::string extraA, extraB;
        gLog.clear();
        difftest::AllocationCounter countA;
        const uint64_t ra = fa(p.a, argA, extraA);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        const uint64_t rb = fb(p.b, argB, extraB);
        countB.Stop();
        ++ctx.cases;
        const std::string sa = Ser(*p.a), sb = Ser(*p.b);
        if (sa != before) ++changed;
        if (ra != rb || extraA != extraB || logA != gLog || sa != sb || countA.Blocks() != countB.Blocks() ||
            countA.Bytes() != countB.Bytes())
        {
            size_t at = 0;
            while (at < sa.size() && at < sb.size() && sa[at] == sb[at]) ++at;
            const size_t from = at > 60 ? at - 60 : 0;
            ctx.Fail("case %d: result %llx/%llx extra equal %d state equal %d blocks %lld/%lld bytes %lld/%lld\noriginal\n%sours\n%s\nstate differs at %zu: [%s] / [%s]", i,
                     ra, rb, extraA == extraB, sa == sb, countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(),
                     logA.c_str(), gLog.c_str(), at, sa.substr(from, 120).c_str(), sb.substr(from, 120).c_str());
        }
        FreePair(ctx, p);
    }
    if (changed < minChanged) ctx.Fail("coverage: state changed in %d of %d cases", changed, n);
}

#define NOEXTRA std::string &
template <class T> uint64_t U(T v) { return uint64_t(v); }

// ---- the slots that store a counted object in a pass ----------------------------------------------------------------------
using SetObjectFn = void(__thiscall *)(cShaderInit *, int32_t, cIShaderObject *);
void CheckSetObject(difftest::Context &ctx, uint32_t rva, void (cShaderInit::*mine)(int32_t, cIShaderObject *))
{
    auto original = ctx.Original<SetObjectFn>(rva);
    Compare(ctx, rva, 300, 150,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const int32_t pass = PassOf(r); original(o, pass, ObjOf(r)); return U(0); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const int32_t pass = PassOf(r); (o->*mine)(pass, ObjOf(r)); return U(0); });
}

} // namespace

DIFF_TEST(SG_InitSetObject00, 0xaa85b0) { CheckSetObject(ctx, 0xaa85b0, &cShaderInit::SetObject00Body); }
DIFF_TEST(SG_InitSetObject1c, 0xaa8630) { CheckSetObject(ctx, 0xaa8630, &cShaderInit::SetObject1cBody); }
DIFF_TEST(SG_InitSetObject20, 0xaa85f0) { CheckSetObject(ctx, 0xaa85f0, &cShaderInit::SetObject20Body); }
DIFF_TEST(SG_InitSetObject24, 0xaa8720) { CheckSetObject(ctx, 0xaa8720, &cShaderInit::SetObject24Body); }
DIFF_TEST(SG_InitSetObject28, 0xaa8570) { CheckSetObject(ctx, 0xaa8570, &cShaderInit::SetObject28Body); }

namespace {
void CheckAddPart(difftest::Context &ctx, uint32_t rva, void (cShaderInit::*mine)(int32_t, cIShaderObject *))
{
    CheckSetObject(ctx, rva, mine); // the same shape: a pass and an object
}
} // namespace
DIFF_TEST(SG_InitAddPartA, 0xaa80b0) { CheckAddPart(ctx, 0xaa80b0, &cShaderInit::AddPartABody); }
DIFF_TEST(SG_InitAddPartB, 0xaa7f90) { CheckAddPart(ctx, 0xaa7f90, &cShaderInit::AddPartBBody); }

DIFF_TEST(SG_InitAddObject, 0xaa8030)
{
    auto original = ctx.Original<void(__thiscall *)(cShaderInit *, cIShaderObject *)>(0xaa8030);
    Compare(ctx, 0xaa8030, 300, 250,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { original(o, ObjOf(r)); return U(0); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { o->AddObjectBody(ObjOf(r)); return U(0); });
}

DIFF_TEST(SG_InitAddPassName, 0xaa8160)
{
    auto original = ctx.Original<void(__thiscall *)(cShaderInit *, int32_t, const void *)>(0xaa8160);
    Compare(ctx, 0xaa8160, 300, 150,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const int32_t pass = PassOf(r); original(o, pass, &gNames[r() % 4]); return U(0); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const int32_t pass = PassOf(r); o->AddPassNameBody(pass, &gNames[r() % 4]); return U(0); });
}

DIFF_TEST(SG_InitAddName, 0xaa8150)
{
    auto original = ctx.Original<void(__thiscall *)(cShaderInit *, const void *)>(0xaa8150);
    Compare(ctx, 0xaa8150, 300, 250,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { original(o, &gNames[r() % 4]); return U(0); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { o->AddNameBody(&gNames[r() % 4]); return U(0); });
}

DIFF_TEST(SG_InitSetTexture, 0xaa8690)
{
    auto original = ctx.Original<void(__thiscall *)(cShaderInit *, int32_t, uint32_t, uint32_t, uint32_t)>(0xaa8690);
    Compare(ctx, 0xaa8690, 300, 150,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                const int32_t pass = PassOf(r);
                const uint32_t i = r() % 16, s = r() % 60, v = r();
                original(o, pass, i, s, v);
                return U(0);
            },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                const int32_t pass = PassOf(r);
                const uint32_t i = r() % 16, s = r() % 60, v = r();
                o->SetTextureBody(pass, i, s, v);
                return U(0);
            });
}

namespace {
void CheckByte(difftest::Context &ctx, uint32_t rva, void (cShaderInit::*mine)(int32_t, uint8_t))
{
    auto original = ctx.Original<void(__thiscall *)(cShaderInit *, int32_t, uint8_t)>(rva);
    Compare(ctx, rva, 300, 120,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const int32_t pass = PassOf(r); original(o, pass, uint8_t(r())); return U(0); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const int32_t pass = PassOf(r); (o->*mine)(pass, uint8_t(r())); return U(0); });
}
} // namespace
DIFF_TEST(SG_InitSetByteC4, 0xaa8700) { CheckByte(ctx, 0xaa8700, &cShaderInit::SetByteC4Body); }
DIFF_TEST(SG_InitSetByteC5, 0xaa8550) { CheckByte(ctx, 0xaa8550, &cShaderInit::SetByteC5Body); }
DIFF_TEST(SG_InitSetByteC6, 0xaa8670) { CheckByte(ctx, 0xaa8670, &cShaderInit::SetByteC6Body); }
DIFF_TEST(SG_InitSetByteC7, 0xaa8530) { CheckByte(ctx, 0xaa8530, &cShaderInit::SetByteC7Body); }

DIFF_TEST(SG_InitSetRange, 0xaa8510)
{
    auto original = ctx.Original<void(__thiscall *)(cShaderInit *, uint32_t, uint32_t, uint32_t)>(0xaa8510);
    Compare(ctx, 0xaa8510, 200, 190,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const uint32_t k = r() % 13; const uint32_t a = r(), b = r(); original(o, k, a, b); return U(0); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { const uint32_t k = r() % 13; const uint32_t a = r(), b = r(); o->SetRangeBody(k, a, b); return U(0); });
}

namespace {
using PartIsFn = bool(__thiscall *)(cShaderInit *, int32_t, uint32_t, void **);
void CheckPartIs(difftest::Context &ctx, uint32_t rva, bool (cShaderInit::*mine)(int32_t, uint32_t, void **))
{
    auto original = ctx.Original<PartIsFn>(rva);
    int yes = 0;
    Compare(ctx, rva, 600, 0,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                gQiMask = r();
                const int32_t pass = PassOf(r);
                const uint32_t iid = r();
                void *out = nullptr;
                const bool ok = original(o, pass, iid, &out);
                yes += ok;
                return U(ok) | U(IdOf(out)) << 8;
            },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                gQiMask = r();
                const int32_t pass = PassOf(r);
                const uint32_t iid = r();
                void *out = nullptr;
                const bool ok = (o->*mine)(pass, iid, &out);
                return U(ok) | U(IdOf(out)) << 8;
            });
    if (yes < 20) ctx.Fail("coverage: %d yes answers", yes);
}
} // namespace
DIFF_TEST(SG_InitPartAIs, 0xaa83c0) { CheckPartIs(ctx, 0xaa83c0, &cShaderInit::PartAIsBody); }
DIFF_TEST(SG_InitPartBIs, 0xaa8310) { CheckPartIs(ctx, 0xaa8310, &cShaderInit::PartBIsBody); }

DIFF_TEST(SG_InitObjectIs, 0xaa8380)
{
    auto original = ctx.Original<bool(__thiscall *)(cShaderInit *, uint32_t, void **)>(0xaa8380);
    int yes = 0;
    Compare(ctx, 0xaa8380, 400, 0,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                gQiMask = r();
                const uint32_t iid = r();
                void *out = nullptr;
                const bool ok = original(o, iid, &out);
                yes += ok;
                return U(ok) | U(IdOf(out)) << 8;
            },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                gQiMask = r();
                const uint32_t iid = r();
                void *out = nullptr;
                const bool ok = o->ObjectIsBody(iid, &out);
                return U(ok) | U(IdOf(out)) << 8;
            });
    if (yes < 30) ctx.Fail("coverage: %d yes answers", yes);
}

DIFF_TEST(SG_InitTextureOf, 0xaa8760)
{
    auto original = ctx.Original<void *(__thiscall *)(cShaderInit *, int32_t, uint32_t)>(0xaa8760);
    Compare(ctx, 0xaa8760, 500, 0,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                const int32_t pass = PassOf(r);
                void *t = original(o, pass, r() % 16);
                return t == &gTexture ? U(1) : U(uintptr_t(t));
            },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                const int32_t pass = PassOf(r);
                void *t = o->TextureOfBody(pass, r() % 16);
                return t == &gTexture ? U(1) : U(uintptr_t(t));
            });
}

DIFF_TEST(SG_InitElement, 0xaa8460)
{
    auto original = ctx.Original<ShaderDescriptor *(__thiscall *)(cShaderInit *, uint32_t)>(0xaa8460);
    Compare(ctx, 0xaa8460, 500, 80,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                const uint32_t pass = uint32_t(PassOf(r) & 0x7fffffff) % 30;
                ShaderDescriptor *e = original(o, pass);
                return U(e - o->mData.mDescs.mBegin);
            },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                const uint32_t pass = uint32_t(PassOf(r) & 0x7fffffff) % 30;
                ShaderDescriptor *e = o->Element(pass);
                return U(e - o->mData.mDescs.mBegin);
            });
}

// ---- the slots whose bodies other classes share ---------------------------------------------------------------------------
DIFF_TEST(SG_InitAddRef, 0x148fb0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(cShaderInit *)>(0x148fb0);
    Compare(ctx, 0x148fb0, 40, 25,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { o->mRefCount = r() % 5 ? r() : 0xffffffff; return U(original(o)); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { o->mRefCount = r() % 5 ? r() : 0xffffffff; return U(o->AddRefBody()); });
}
DIFF_TEST(SG_InitSetAttribute, 0x14a260)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(cShaderInit *, uint32_t)>(0x14a260);
    Compare(ctx, 0x14a260, 40, 30,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { return U(original(o, r())); },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) { return U(o->SetAttributeBody(r())); });
}
DIFF_TEST(SG_InitGetData, 0x142840)
{
    auto original = ctx.Original<ShaderInitData *(__thiscall *)(cShaderInit *)>(0x142840);
    Compare(ctx, 0x142840, 20, 0,
            [&](cShaderInit *o, std::mt19937 &, NOEXTRA) { return U(reinterpret_cast<uint8_t *>(original(o)) - reinterpret_cast<uint8_t *>(o)); },
            [&](cShaderInit *o, std::mt19937 &, NOEXTRA) { return U(reinterpret_cast<uint8_t *>(o->GetDataBody()) - reinterpret_cast<uint8_t *>(o)); });
}

DIFF_TEST(SG_InitQueryInterface, 0xaa8430)
{
    auto original = ctx.Original<bool(__thiscall *)(cShaderInit *, uint32_t, void **)>(0xaa8430);
    const uint32_t ids[] = {1, cShaderInit::kIidCounted, 0, 2, 0x5c17ff19, 0xffffffffu, 0x9c544810};
    int yes = 0;
    Compare(ctx, 0xaa8430, 70, 0,
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                void *out = nullptr;
                const uint32_t iid = ids[r() % 7];
                const bool nullOut = r() % 8 == 0;
                const bool ok = original(o, iid, nullOut ? nullptr : &out);
                yes += ok;
                return U(ok) | U(out == o) << 8 | U(o->mRefCount) << 16;
            },
            [&](cShaderInit *o, std::mt19937 &r, NOEXTRA) {
                void *out = nullptr;
                const uint32_t iid = ids[r() % 7];
                const bool nullOut = r() % 8 == 0;
                const bool ok = o->QueryInterfaceBody(iid, nullOut ? nullptr : &out);
                return U(ok) | U(out == o) << 8 | U(o->mRefCount) << 16;
            });
    if (yes < 20) ctx.Fail("coverage: %d yes answers", yes);
}

DIFF_TEST(SG_InitClone, 0xaa8180)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(cShaderInit *, uint32_t, void **)>(0xaa8180);
    const uint32_t ids[] = {1, cShaderInit::kIidCounted, 0, 2, 1, 0x5c17ff19};
    auto run = [&](bool ours) {
        return [&, ours](cShaderInit *o, std::mt19937 &r, std::string &extra) {
            void *out = nullptr;
            const uint32_t iid = ids[r() % 6];
            const uint32_t ret = ours ? o->CloneBody(iid, &out) : original(o, iid, &out);
            if (out) {
                auto *copy = static_cast<cShaderInit *>(out);
                extra = Ser(*copy);
                extra += *reinterpret_cast<const void *const *>(copy) == (ours ? static_cast<const void *>(cShaderInit::kOwnVTable) : GameAddress<void>(0xe760a4)) ? "vt-ok" : "vt-WRONG";
                copy->Release();
            }
            return U(ret);
        };
    };
    Compare(ctx, 0xaa8180, 200, 0, run(false), run(true));
}

// ---- construction -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_InitDataConstruct, 0xaa7bc0)
{
    auto original = ctx.Original<ShaderInitData *(__thiscall *)(ShaderInitData *)>(0xaa7bc0);
    for (uint32_t i = 0; i < 12; ++i) {
        alignas(16) uint8_t bufA[sizeof(ShaderInitData)], bufB[sizeof(ShaderInitData)];
        std::memset(bufA, 0x11 * (i + 1), sizeof(bufA));
        std::memset(bufB, 0x11 * (i + 1), sizeof(bufB));
        ShaderInitData *x = original(reinterpret_cast<ShaderInitData *>(bufA));
        ShaderInitData *y = reinterpret_cast<ShaderInitData *>(bufB)->Construct();
        ++ctx.cases;
        if (x != reinterpret_cast<ShaderInitData *>(bufA) || y != reinterpret_cast<ShaderInitData *>(bufB) || std::memcmp(bufA, bufB, sizeof(bufA)) != 0)
            ctx.Fail("fill %u: result or bytes differ", i);
    }
}

DIFF_TEST(SG_InitConstruct, 0xaa7b90)
{
    auto original = ctx.Original<Ctor>(0xaa7b90);
    for (uint32_t i = 0; i < 12; ++i) {
        alignas(16) uint8_t bufA[sizeof(cShaderInit)], bufB[sizeof(cShaderInit)];
        std::memset(bufA, 0x11 * (i + 1), sizeof(bufA));
        std::memset(bufB, 0x11 * (i + 1), sizeof(bufB));
        cShaderInit *x = original(reinterpret_cast<cShaderInit *>(bufA));
        cShaderInit *y = reinterpret_cast<cShaderInit *>(bufB)->Construct();
        ++ctx.cases;
        if (*reinterpret_cast<void *const *>(bufA) != GameAddress<void>(0xe760a4) ||
            *reinterpret_cast<void *const *>(bufB) != static_cast<const void *>(cShaderInit::kOwnVTable))
            ctx.Fail("fill %u: table word is not the expected one", i);
        *reinterpret_cast<void **>(bufA) = *reinterpret_cast<void **>(bufB) = nullptr;
        if (x != reinterpret_cast<cShaderInit *>(bufA) || y != reinterpret_cast<cShaderInit *>(bufB) || std::memcmp(bufA, bufB, sizeof(bufA)) != 0)
            ctx.Fail("fill %u: result or bytes differ", i);
    }
}

DIFF_TEST(SG_InitCreate, 0xaa82a0)
{
    auto original = ctx.Original<cShaderInit *(__cdecl *)()>(0xaa82a0);
    for (uint32_t i = 0; i < 6; ++i) {
        difftest::AllocationCounter countA;
        cShaderInit *a = original();
        countA.Stop();
        difftest::AllocationCounter countB;
        cShaderInit *b = cShaderInit::Create();
        countB.Stop();
        ++ctx.cases;
        if (!a || !b) {
            ctx.Fail("round %u: no object", i);
            continue;
        }
        if (countA.Blocks() != 1 || countB.Blocks() != 1 || countA.Bytes() != countB.Bytes())
            ctx.Fail("round %u: blocks %lld/%lld bytes %lld/%lld", i, countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes());
        if (*reinterpret_cast<void **>(a) != GameAddress<void>(0xe760a4) || *reinterpret_cast<const void **>(b) != static_cast<const void *>(cShaderInit::kOwnVTable))
            ctx.Fail("round %u: table words", i);
        *reinterpret_cast<void **>(a) = *reinterpret_cast<void **>(b) = nullptr;
        if (std::memcmp(a, b, sizeof(cShaderInit)) != 0) ctx.Fail("round %u: bytes differ", i);
        ::operator delete(a, sizeof(cShaderInit));
        ::operator delete(b, sizeof(cShaderInit));
    }
}

// ---- destruction ------------------------------------------------------------------------------------------------------------
namespace {
enum class Gone { Destruct, Delete, Release };

void CheckGone(difftest::Context &ctx, Gone mode, uint32_t rva)
{
    PlantManager plant(ctx);
    auto origDestruct = ctx.Original<Dtor>(rva);
    auto origDelete = ctx.Original<cShaderInit *(__thiscall *)(cShaderInit *, uint32_t)>(rva);
    auto origRelease = ctx.Original<uint32_t(__thiscall *)(cShaderInit *)>(rva);
    const uint32_t flagSets[] = {0, 1, 4, 5, 2, 3, 0xfffffff1, 0xfffffff4};
    int freed = 0, large = 0;
    for (uint32_t i = 0; i < 160; ++i) {
        Pair p = MakePair(ctx, 0x4000 + i);
        const uint32_t flags = flagSets[i % 8];
        const uint32_t count = 1 + i % 3;
        if (p.a->mData.mDescs.mBegin && p.a->mData.mDescs.mCap - p.a->mData.mDescs.mBegin >= 21) ++large;
        if (mode == Gone::Release) {
            p.a->mRefCount = count;
            p.b->mRefCount = count;
            *reinterpret_cast<const void **>(p.b) = cShaderInit::kOwnVTable; // the delete goes through our slot 26
        }
        gLog.clear();
        difftest::AllocationCounter countA;
        uint64_t ra = 0, rb = 0;
        switch (mode) {
        case Gone::Destruct: origDestruct(p.a); break;
        case Gone::Delete: ra = origDelete(p.a, flags) == p.a; break;
        case Gone::Release: ra = origRelease(p.a); break;
        }
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        switch (mode) {
        case Gone::Destruct: p.b->DestructBody(); break;
        case Gone::Delete: rb = p.b->DeleteBody(flags) == p.b; break;
        case Gone::Release: rb = p.b->ReleaseBody(); break;
        }
        countB.Stop();
        ++ctx.cases;
        const bool deleted = (mode == Gone::Delete && (flags & 1)) || (mode == Gone::Release && count == 1);
        freed += deleted;
        bool same = ra == rb && logA == gLog && countA.Blocks() == countB.Blocks() && countA.Bytes() == countB.Bytes();
        if (!deleted) {
            const ShaderInitData &da = p.a->mData, &db = p.b->mData;
            if (mode == Gone::Destruct || mode == Gone::Delete) {
                same = same && !da.mDescs.mBegin && !db.mDescs.mBegin && !da.mObjects.mBegin && !db.mObjects.mBegin;
                const bool okA = *reinterpret_cast<void *const *>(p.a) == GameAddress<void>(0xe760a4);
                const bool okB = *reinterpret_cast<const void *const *>(p.b) == static_cast<const void *>(cShaderInit::kOwnVTable);
                same = same && okA && okB;
            } else {
                same = same && Ser(*p.a) == Ser(*p.b);
            }
        }
        if (!same)
            ctx.Fail("case %u flags %x: result %llx/%llx blocks %lld/%lld bytes %lld/%lld\noriginal\n%sours\n%s", i, flags, ra, rb,
                     countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(), logA.c_str(), gLog.c_str());
        if (deleted) continue;
        if (mode == Gone::Destruct || mode == Gone::Delete) {
            ::operator delete(p.a);
            ::operator delete(p.b);
        } else {
            *reinterpret_cast<const void **>(p.b) = GameAddress<void>(0xe760a4);
            FreePair(ctx, p);
        }
    }
    if (mode == Gone::Release && freed < 20) ctx.Fail("coverage: freed %d", freed);
    if (mode != Gone::Release && large < 5) ctx.Fail("coverage: large descriptor vectors %d", large);
}
} // namespace

DIFF_TEST(SG_InitDestructBody, 0xaa7d70) { CheckGone(ctx, Gone::Destruct, 0xaa7d70); }
DIFF_TEST(SG_InitDelete, 0xaa7f60) { CheckGone(ctx, Gone::Delete, 0xaa7f60); }
DIFF_TEST(SG_InitRelease, 0x292890) { CheckGone(ctx, Gone::Release, 0x292890); }

// The descriptor vector's destroy is a function of the vector only (this = its address).
DIFF_TEST(SG_InitDescriptorVecDestroy, 0xaa7cf0)
{
    PlantManager plant(ctx);
    auto original = ctx.Original<void(__thiscall *)(DescriptorVec *)>(0xaa7cf0);
    for (uint32_t i = 0; i < 160; ++i) {
        Pair p = MakePair(ctx, 0x8000 + i);
        gLog.clear();
        difftest::AllocationCounter countA;
        original(&p.a->mData.mDescs);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        p.b->mData.mDescs.Destroy();
        countB.Stop();
        ++ctx.cases;
        const DescriptorVec &va = p.a->mData.mDescs, &vb = p.b->mData.mDescs;
        if (logA != gLog || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes() || va.mBegin || va.mEnd || va.mCap ||
            vb.mBegin || vb.mEnd || vb.mCap || Ser(*p.a) != Ser(*p.b))
            ctx.Fail("case %u: blocks %lld/%lld bytes %lld/%lld\noriginal\n%sours\n%s", i, countA.Blocks(), countB.Blocks(),
                     countA.Bytes(), countB.Bytes(), logA.c_str(), gLog.c_str());
        FreePair(ctx, p);
    }
}

// ---- the tables: slot order and count as the image's -------------------------------------------------------------------------
DIFF_TEST(SG_InitTables, 0xe760a4)
{
    const uint32_t rvas[27] = {0xaa8430, 0x148fb0, 0x292890, 0xaa8180, 0x14a260, 0xaa85b0, 0xaa8630, 0xaa85f0, 0xaa80b0,
                               0xaa7f90, 0xaa8030, 0xaa8160, 0xaa8150, 0xaa8690, 0xaa8720, 0xaa8570, 0xaa8700, 0xaa8550,
                               0xaa8670, 0xaa8530, 0xaa8510, 0x142840, 0xaa83c0, 0xaa8310, 0xaa8380, 0xaa8760, 0xaa7f60};
    const uint32_t *image = ctx.image.At<uint32_t>(0xe760a4);
    for (int i = 0; i < 27; ++i) {
        ++ctx.cases;
        if (image[i] != uint32_t(uintptr_t(GameAddress<void>(rvas[i])))) ctx.Fail("image table slot %d is not %x", i, rvas[i]);
    }
    const void *const ours[27] = {
        MemberAddress(&cShaderInit::QueryInterfaceBody), MemberAddress(&cShaderInit::AddRefBody), MemberAddress(&cShaderInit::ReleaseBody),
        MemberAddress(&cShaderInit::CloneBody), MemberAddress(&cShaderInit::SetAttributeBody), MemberAddress(&cShaderInit::SetObject00Body),
        MemberAddress(&cShaderInit::SetObject1cBody), MemberAddress(&cShaderInit::SetObject20Body), MemberAddress(&cShaderInit::AddPartABody),
        MemberAddress(&cShaderInit::AddPartBBody), MemberAddress(&cShaderInit::AddObjectBody), MemberAddress(&cShaderInit::AddPassNameBody),
        MemberAddress(&cShaderInit::AddNameBody), MemberAddress(&cShaderInit::SetTextureBody), MemberAddress(&cShaderInit::SetObject24Body),
        MemberAddress(&cShaderInit::SetObject28Body), MemberAddress(&cShaderInit::SetByteC4Body), MemberAddress(&cShaderInit::SetByteC5Body),
        MemberAddress(&cShaderInit::SetByteC6Body), MemberAddress(&cShaderInit::SetByteC7Body), MemberAddress(&cShaderInit::SetRangeBody),
        MemberAddress(&cShaderInit::GetDataBody), MemberAddress(&cShaderInit::PartAIsBody), MemberAddress(&cShaderInit::PartBIsBody),
        MemberAddress(&cShaderInit::ObjectIsBody), MemberAddress(&cShaderInit::TextureOfBody), MemberAddress(&cShaderInit::DeleteBody)};
    for (int i = 0; i < 27; ++i)
        if (cShaderInit::kOwnVTable[i] != ours[i]) ctx.Fail("our table slot %d", i);
}
