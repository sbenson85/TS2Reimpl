// Differential tests for the serial record's owned vtables (src/gz/cGZPersistDBSerialRecordFaces.cpp): every slot of
// each table, the image's against ours, on the same object.
//
//   0xda6098 primary (62 slots)   0xda6194 +4 record face (21)   0xda61ec +0x20 ostream face (24)   0xda6250 +0x30 istream face (24)
//   0xe5e990 base record (21)     0xda29bc cRZOStream (24)       0xda2a20 cRZIStream (24)
//   0xe5e7dc cIGZPersistDBSerialRecord (57 slots, all pure: the image's are one body)
//
// The unhooked compiler-made adjustor thunks in our tables (0x1da02b, 0x1da033, 0x1da03b: the deleting destructors of the
// three sub-object faces, which add the face's offset and jump to 0x1da060) and the folded shared bodies (0x1c04e0,
// 0x142840, 0x140fe0, 0x147480, 0x1474b0, 0x144910, 0x145770, 0x1b6617, 0x1b5f3c, 0x148fb0, 0x1b5fc4, 0x18a7a0, 0x469a20) are
// the image's table entries that each slot test calls; 0x1da060 (the deleting destructor) is called through the +4, +0x20
// and +0x30 slots with flags 0.
//
// The object is a raw buffer. Its four vtable words point at copies of the image's tables in which the slots the members
// call back into are fakes: the raw read (27) and write (51), GetFieldString (24) and the record's deleting destructor on
// the primary object; the raw write, raw read, error and skip slots of the pure stream bases. The child and owner pointers
// are null or a fake cIGZUnknown. A slot runs once from the image's table and once from ours, on identical copies of the
// scene; the return value (masked to the bits the original defines), the calls the fakes saw, the bytes consumed from the
// script and every byte of the object must match. The pure slots are not called (they abort).
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kBytes = 0x200;
constexpr size_t kDst = 0x80;  // the output buffer (0x40 bytes)
constexpr size_t kOutPtr = 0x100; // an out pointer cell
constexpr size_t kSrcLen = 0x100;
constexpr uintptr_t kUnchanged = 0xFACE0000u;
alignas(16) uint8_t gArena[kBytes]; // one fixed object, so stored pointers compare equal between runs

enum Kind : int64_t {
    kRawRead = 1, kRawWrite, kGetString, kDelete, kStrFrom, kStrLen, kStrData, kStrAppend, kObjQuery, kObjAddRef, kObjRelease,
    kObjSer3, kObjSer4, kBaseSkip, kBaseError, kVariantSlot, kStrToChar, kGetSize, kSeekAbs, kSeekRel, kStrResize, kObj24, kObj25, kStrFromN
};

std::vector<int64_t> gLog;
uint8_t *gBase = nullptr;
std::vector<uint8_t> gSource;
size_t gPos = 0;
bool gRefuse = false;
bool gLastReadOk = true;

bool InObject(const void *p)
{
    auto b = static_cast<const uint8_t *>(p);
    return b >= gBase && b < gBase + kBytes;
}
int64_t Off(const void *p) { return InObject(p) ? static_cast<const uint8_t *>(p) - gBase : -1; }
void Push(int64_t kind, int64_t a = 0, int64_t b = 0)
{
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
}

// ---- Fakes ----

bool __fastcall FakeRawRead(void *, void *, void *dst, uint32_t n)
{
    Push(kRawRead, Off(dst), n);
    gLastReadOk = false;
    if (gRefuse || gSource.size() - gPos < n) return false;
    std::memcpy(dst, gSource.data() + gPos, n);
    gPos += n;
    gLastReadOk = true;
    return true;
}
bool __fastcall FakeRawWrite(void *, void *, const void *src, uint32_t n)
{
    Push(kRawWrite, n);
    auto bytes = static_cast<const uint8_t *>(src);
    for (uint32_t i = 0; i < n; ++i) gLog.push_back(bytes[i]);
    return !gRefuse;
}
bool __fastcall FakeGetString(void *, void *, void *dst);
void __fastcall RecordDelete(void *self, void *, uint32_t flags) { Push(kDelete, Off(self), flags); }
uint32_t gSizeAnswer = 0;
uint32_t __fastcall FakeGetSize(void *, void *)
{
    Push(kGetSize, gSizeAnswer);
    return gSizeAnswer;
}
bool __fastcall FakeSeek55(void *, void *, uint32_t n)
{
    Push(kSeekAbs, n);
    return !gRefuse;
}
bool __fastcall FakeSeek56(void *, void *, int32_t n)
{
    Push(kSeekRel, n);
    return !gRefuse;
}
uint32_t __fastcall StrResize(void *, void *, uint32_t n)
{
    Push(kStrResize, n);
    return 1;
}
bool __fastcall BaseSkip(void *, void *, uint32_t n)
{
    Push(kBaseSkip, n);
    return !gRefuse;
}
uint32_t __fastcall BaseError(void *, void *)
{
    Push(kBaseError);
    return (gRefuse || !gLastReadOk) ? 1 : 0;
}

// A stand-in cIGZString.
struct FakeObject {
    void *vtable;
};
FakeObject gStr;
void *gStrVTable[24];
uint32_t gStrLen = 0;
std::vector<uint8_t> gStrData;

uint32_t __fastcall StrFrom(void *, void *, const char *text)
{
    Push(kStrFrom, text ? static_cast<uint8_t>(text[0]) : -1);
    return 1;
}
const char *__fastcall StrToChar(void *, void *)
{
    Push(kStrToChar);
    return reinterpret_cast<const char *>(gStrData.data());
}
const char *__fastcall StrData(void *, void *)
{
    Push(kStrData);
    return reinterpret_cast<const char *>(gStrData.data());
}
uint32_t __fastcall StrLen(void *, void *)
{
    Push(kStrLen, gStrLen);
    return gStrLen;
}
uint32_t __fastcall StrAppend(void *, void *, const char *text, uint32_t n)
{
    Push(kStrAppend, n);
    if (gLastReadOk) { // else the original appends stack garbage (ORIGINAL-BUG)
        for (uint32_t i = 0; i < n; ++i) gLog.push_back(static_cast<uint8_t>(text[i]));
    }
    return 1;
}
uint32_t __fastcall StrFromN(void *, void *, const char *text, uint32_t n)
{
    Push(kStrFromN, n);
    if (gLastReadOk) {
        for (uint32_t i = 0; i < n && i < 64; ++i) gLog.push_back(static_cast<uint8_t>(text[i]));
    }
    return 1;
}
bool __fastcall FakeGetString(void *, void *, void *dst)
{
    Push(kGetString, dst == static_cast<void *>(&gStr) ? 1 : 0);
    return !gRefuse;
}

// A stand-in cIGZUnknown (child, owner, serializable).
FakeObject gObj;
void *gObjVTable[32];
int64_t gObjRefs = 0;
bool __fastcall ObjQuery(void *, void *, uint32_t riid, void **ppv)
{
    Push(kObjQuery, riid, Off(ppv));
    return false;
}
uint32_t __fastcall ObjAddRef(void *, void *)
{
    Push(kObjAddRef, ++gObjRefs);
    return static_cast<uint32_t>(gObjRefs);
}
uint32_t __fastcall ObjRelease(void *, void *)
{
    Push(kObjRelease, --gObjRefs);
    return static_cast<uint32_t>(gObjRefs);
}
uint32_t __fastcall ObjSer3(void *, void *, void *stream)
{
    Push(kObjSer3, Off(stream));
    return 3;
}
uint32_t __fastcall ObjSer4(void *, void *, void *stream)
{
    Push(kObjSer4, Off(stream));
    return 4;
}

bool __fastcall ObjSer24(void *, void *, void *record)
{
    Push(kObj24, Off(record));
    return gRefuse;
}
bool __fastcall ObjSer25(void *, void *, void *record)
{
    Push(kObj25, Off(record));
    return !gRefuse;
}

// A stand-in cRZVariant for SetFieldVariant: scalar tags only.
struct VariantScript {
    uint32_t tag = 0;
    uint64_t bits = 0;
};
VariantScript gVar;
template <int S> uint32_t __fastcall VarU32(void *, void *) { Push(kVariantSlot, S); return static_cast<uint32_t>(gVar.bits); }
template <int S> uint64_t __fastcall VarU64(void *, void *) { Push(kVariantSlot, S); return gVar.bits; }
template <int S> float __fastcall VarF32(void *, void *)
{
    Push(kVariantSlot, S);
    float f;
    uint32_t b = static_cast<uint32_t>(gVar.bits);
    std::memcpy(&f, &b, 4);
    return f;
}
template <int S> double __fastcall VarF64(void *, void *)
{
    Push(kVariantSlot, S);
    double d;
    std::memcpy(&d, &gVar.bits, 8);
    return d;
}
uint32_t __fastcall VarGetType(void *, void *) { Push(kVariantSlot, 3); return gVar.tag; }
uint32_t __fastcall VarGetCount(void *, void *) { Push(kVariantSlot, 4); return 0; }
FakeObject gVariant;
void *gVarVTable[133];

void InitFakes()
{
    gStrVTable[4] = reinterpret_cast<void *>(&StrFrom);
    gStrVTable[3] = reinterpret_cast<void *>(&StrFromN);
    gStrVTable[18] = reinterpret_cast<void *>(&StrResize);
    gStrVTable[5] = reinterpret_cast<void *>(&StrToChar);
    gStrVTable[6] = reinterpret_cast<void *>(&StrData);
    gStrVTable[7] = reinterpret_cast<void *>(&StrLen);
    gStrVTable[20] = reinterpret_cast<void *>(&StrAppend);
    gStr.vtable = gStrVTable;
    gObjVTable[0] = reinterpret_cast<void *>(&ObjQuery);
    gObjVTable[1] = reinterpret_cast<void *>(&ObjAddRef);
    gObjVTable[2] = reinterpret_cast<void *>(&ObjRelease);
    gObjVTable[3] = reinterpret_cast<void *>(&ObjSer3);
    gObjVTable[4] = reinterpret_cast<void *>(&ObjSer4);
    gObjVTable[24] = reinterpret_cast<void *>(&ObjSer24);
    gObjVTable[25] = reinterpret_cast<void *>(&ObjSer25);
    gObj.vtable = gObjVTable;
#define SLOT(Fn, S) gVarVTable[S] = reinterpret_cast<void *>(&Fn<S>)
    gVarVTable[3] = reinterpret_cast<void *>(&VarGetType);
    gVarVTable[4] = reinterpret_cast<void *>(&VarGetCount);
    SLOT(VarU32, 67); SLOT(VarU32, 69); SLOT(VarU32, 71); SLOT(VarU32, 73); SLOT(VarU32, 75); SLOT(VarU32, 77); SLOT(VarU32, 79);
    SLOT(VarU64, 81); SLOT(VarU64, 83); SLOT(VarF32, 85); SLOT(VarF64, 87); SLOT(VarU32, 89); SLOT(VarU32, 91); SLOT(VarU32, 93);
    SLOT(VarU32, 95);
#undef SLOT
    gVariant.vtable = gVarVTable;
}

// ---- Scenes and tables ----

struct Tables {
    void *primary[62];
    void *record[21];
    void *ostream[24];
    void *istream[24];
    void *baseRecord[21];
    void *baseO[24];
    void *baseI[24];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(0xda6098), sizeof(t.primary));
    std::memcpy(t.record, ctx.image.At(0xda6194), sizeof(t.record));
    std::memcpy(t.ostream, ctx.image.At(0xda61ec), sizeof(t.ostream));
    std::memcpy(t.istream, ctx.image.At(0xda6250), sizeof(t.istream));
    std::memcpy(t.baseRecord, ctx.image.At(0xe5e990), sizeof(t.baseRecord));
    std::memcpy(t.baseO, ctx.image.At(0xda29bc), sizeof(t.baseO));
    std::memcpy(t.baseI, ctx.image.At(0xda2a20), sizeof(t.baseI));
    t.primary[24] = reinterpret_cast<void *>(&FakeGetString);
    t.primary[27] = reinterpret_cast<void *>(&FakeRawRead);
    t.primary[51] = reinterpret_cast<void *>(&FakeRawWrite);
    t.primary[52] = reinterpret_cast<void *>(&FakeGetSize);
    t.primary[55] = reinterpret_cast<void *>(&FakeSeek55);
    t.primary[56] = reinterpret_cast<void *>(&FakeSeek56);
    t.record[19] = reinterpret_cast<void *>(&RecordDelete);
    t.baseRecord[19] = reinterpret_cast<void *>(&RecordDelete);
    t.baseO[17] = reinterpret_cast<void *>(&FakeRawWrite);
    t.baseI[17] = reinterpret_cast<void *>(&FakeRawRead);
    t.baseI[18] = reinterpret_cast<void *>(&BaseError);
    t.baseI[3] = reinterpret_cast<void *>(&BaseSkip);
    return t;
}

struct Scene {
    std::vector<uint8_t> object;
    std::vector<uint8_t> source;
    std::vector<uint8_t> strData;
    uint32_t strLen;
    bool refuse;
    uint32_t args[8];
    uint32_t tag;
    uint64_t bits;
};

Scene MakeScene(std::mt19937 &rng)
{
    Scene s;
    s.object.resize(kBytes);
    for (auto &b : s.object) b = static_cast<uint8_t>(rng());
    // Reference counts that sometimes reach zero.
    uint16_t count = static_cast<uint16_t>(rng() % 4);
    std::memcpy(&s.object[8], &count, 2);
    auto putPtr = [&](size_t at) {
        uintptr_t p = rng() % 3 == 0 ? 0 : reinterpret_cast<uintptr_t>(&gObj);
        std::memcpy(&s.object[at], &p, sizeof(p));
    };
    putPtr(0x1c);
    putPtr(0x2c);
    putPtr(0x3c);
    static const uint8_t flags[] = {0, 0, 0, 1, 2, 0x80, 0xff};
    s.object[0x40] = flags[rng() % 7];
    s.source.resize(kSrcLen);
    for (auto &b : s.source) b = static_cast<uint8_t>(rng());
    s.source[0x7f] = 0; // text arguments end here
    s.strLen = rng() % 40;
    s.strData.resize(s.strLen + 8);
    for (auto &b : s.strData) b = static_cast<uint8_t>(rng());
    s.refuse = rng() % 8 == 0;
    for (auto &a : s.args) a = static_cast<uint32_t>(rng());
    s.tag = rng() % 0x14;
    s.bits = (uint64_t(rng()) << 32) | rng();
    s.bits &= ~(uint64_t(0x7ff) << 52); // finite doubles and floats
    s.bits &= ~(uint64_t(0xff) << 23);
    return s;
}

struct Outcome {
    uint64_t ret = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes;
    size_t pos = 0;
    int64_t refs = 0;
};

// How a slot is called: `args` has one letter per dword (V random, D dst buffer, S source bytes, C count 0..6, T the fake
// string, P an out-pointer cell, O the fake object or null, F the fake variant, K unused, Z zero); `ret` is b (low byte),
// u (all 32 bits), p (a pointer: compared as an offset into the object), v (void, not compared).
struct Spec {
    int slot;
    const char *args;
    char ret;
};

using Fn0 = uint32_t(__thiscall *)(void *);
using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Fn3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

uint32_t CallSlot(void *fn, void *self, const uint32_t *a, size_t n)
{
    switch (n) {
    case 0: return reinterpret_cast<Fn0>(fn)(self);
    case 1: return reinterpret_cast<Fn1>(fn)(self, a[0]);
    case 2: return reinterpret_cast<Fn2>(fn)(self, a[0], a[1]);
    default: return reinterpret_cast<Fn3>(fn)(self, a[0], a[1], a[2]);
    }
}

// Which object offset `this` is, and which vtable words point where.
struct Layout {
    size_t thisOffset;
    const void *v0, *v4, *v20, *v30;
};

Outcome RunSlot(const Scene &s, const Layout &lay, void *fn, const Spec &spec, uint32_t riid)
{
    std::memcpy(gArena, s.object.data(), kBytes);
    struct {
        uint8_t *p;
        uint8_t &operator[](size_t i) { return p[i]; }
        uint8_t *data() { return p; }
    } buf{gArena};
    auto put = [&](size_t at, const void *v) { std::memcpy(&buf[at], &v, sizeof(v)); };
    put(0, lay.v0);
    put(4, lay.v4);
    put(0x20, lay.v20);
    put(0x30, lay.v30);
    uintptr_t sentinel = kUnchanged;
    std::memcpy(&buf[kOutPtr], &sentinel, sizeof(sentinel));
    gBase = buf.data();
    gLog.clear();
    gSource = s.source;
    gPos = 0;
    gRefuse = s.refuse;
    gLastReadOk = true;
    gStrLen = s.strLen;
    gSizeAnswer = s.args[6] % 0x30;
    gStrData = s.strData;
    gVar.tag = s.tag;
    gVar.bits = s.bits;
    gObjRefs = 100;

    uint32_t a[8];
    size_t n = std::strlen(spec.args);
    bool first = true;
    for (size_t i = 0; i < n; ++i) {
        uint32_t v = s.args[i];
        switch (spec.args[i]) {
        case 'D': v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf.data() + kDst)); break;
        case 'S': v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gSource.data())); break;
        case 'C': v = v % 7; break;
        case 'T': v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gStr)); break;
        case 'P': v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf.data() + kOutPtr)); break;
        case 'O': v = (s.args[7] % 3 == 0) ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gObj)); break;
        case 'F': v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gVariant)); break;
        case 'N': v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gObj)); break;
        case 'Z': v = 0; break;
        case 'V': if (first && spec.args[0] == 'V' && n == 2 && spec.args[1] == 'P') v = riid; break;
        default: break;
        }
        first = false;
        a[i] = v;
    }
    Outcome out;
    uint32_t r = CallSlot(fn, buf.data() + lay.thisOffset, a, n);
    switch (spec.ret) {
    case 'b': out.ret = r & 0xff; break;
    case 'u': out.ret = r; break;
    case 'p': out.ret = InObject(reinterpret_cast<void *>(static_cast<uintptr_t>(r))) ? Off(reinterpret_cast<void *>(static_cast<uintptr_t>(r))) : r; break;
    default: out.ret = 0; break;
    }
    out.log = gLog;
    out.bytes.assign(gArena, gArena + kBytes);
    out.pos = gPos;
    out.refs = gObjRefs;
    return out;
}

const uint32_t kIds[] = {0, 1, 2, 0x051b11bd, 0x051b11bc, 0x051b11bb, 0xc51b11cb, 0xc56b8f08, 0xe56b8f03,
                         0xc51b11cc, 0xc56b8f09, 0xe56b8f04, 0x80000000u, 0xffffffffu};

void CheckTable(difftest::Context &ctx, const char *name, const void *image, const void *ours, const Layout &lay,
                const std::vector<Spec> &specs, int trials, uint32_t seed)
{
    std::mt19937 rng(seed);
    void *const *imageT = static_cast<void *const *>(image);
    void *const *ourT = static_cast<void *const *>(ours);
    for (const Spec &spec : specs) {
        if (const char *only = std::getenv("QJ_SLOT")) if (std::atoi(only) != spec.slot) continue;
        for (int i = 0; i < trials; ++i) {
            Scene s = MakeScene(rng);
            if (spec.slot == 26) s.refuse = true; // GetFieldVariant: the first read fails
            uint32_t riid = i < 14 ? kIds[i] : (i % 3 == 0 ? kIds[rng() % 14] : static_cast<uint32_t>(rng()));
            if (i >= 14 && i % 3 == 1) riid ^= 1u << (rng() % 32);
            Outcome a = RunSlot(s, lay, imageT[spec.slot], spec, riid);
            Outcome b = RunSlot(s, lay, ourT[spec.slot], spec, riid);
            ++ctx.cases;
            std::string who = std::string(name) + " slot " + std::to_string(spec.slot);
            if (a.ret != b.ret) ctx.Fail("%s: return image %llx, ours %llx", who.c_str(), (unsigned long long)a.ret, (unsigned long long)b.ret);
            if (a.log != b.log) ctx.Fail("%s: calls made differ (%zu / %zu values)", who.c_str(), a.log.size(), b.log.size());
            if (a.pos != b.pos) ctx.Fail("%s: bytes consumed differ (%zu, %zu)", who.c_str(), a.pos, b.pos);
            if (a.refs != b.refs) ctx.Fail("%s: fake object references differ (%lld, %lld)", who.c_str(), (long long)a.refs, (long long)b.refs);
            if (a.bytes != b.bytes) ctx.Fail("%s: object state differs", who.c_str());
        }
    }
}

// ---- The tables' specs ----

std::vector<Spec> PrimarySpecs()
{
    std::vector<Spec> v = {{0, "VP", 'b'}, {1, "", 'u'}, {2, "", 'u'}, {3, "", 'p'}};
    for (int s = 4; s <= 23; ++s) v.push_back({s, s % 2 == 0 ? "DC" : "D", 'b'});
    v.push_back({24, "T", 'b'});
    v.push_back({25, "D", 'b'});
    v.push_back({26, "P", 'b'});
    for (int s = 28; s <= 47; ++s) {
        const char *args = s % 2 == 0 ? "SC" : "V";
        if (s == 41 || s == 43 || s == 47) args = "VV";
        v.push_back({s, args, 'b'});
    }
    v.push_back({48, "T", 'b'});
    v.push_back({49, "S", 'b'});
    v.push_back({50, "F", 'b'});
    v.push_back({55, "V", 'b'});
    v.push_back({56, "V", 'b'});
    v.push_back({57, "DCC", 'b'});
    v.push_back({58, "SCC", 'b'});
    v.push_back({59, "T", 'p'});
    v.push_back({60, "V", 'p'});
    v.push_back({61, "T", 'p'});
    return v;
}

std::vector<Spec> RecordSpecs()
{
    return {{0, "VP", 'b'}, {1, "", 'u'}, {2, "", 'u'}, {3, "", 'p'}, {4, "S", 'v'}, {5, "", 'u'}, {6, "", 'b'}, {7, "", 'b'},
            {13, "V", 'b'}, {14, "V", 'b'}, {15, "VP", 'b'}, {16, "O", 'v'}, {17, "V", 'b'}, {18, "VV", 'u'}, {19, "Z", 'p'},
            {20, "V", 'u'}};
}

// The base record's table has 13 and 14 pure (the serial record's +4 face fills them in with the false-returning body).
std::vector<Spec> BaseRecordSpecs()
{
    std::vector<Spec> v = RecordSpecs();
    v.erase(std::remove_if(v.begin(), v.end(), [](const Spec &s) { return s.slot == 13 || s.slot == 14; }), v.end());
    return v;
}

std::vector<Spec> OStreamSpecs()
{
    std::vector<Spec> v = {{0, "VP", 'b'}, {1, "", 'u'}, {2, "", 'u'}, {3, "", 'v'}};
    for (int s = 4; s <= 9; ++s) v.push_back({s, "V", 'b'});
    v.push_back({10, "VV", 'b'});
    v.push_back({11, "VV", 'b'});
    v.push_back({12, "V", 'b'});
    v.push_back({13, "VV", 'b'});
    v.push_back({14, "S", 'b'});
    v.push_back({15, "T", 'b'});
    v.push_back({16, "N", 'u'});
    v.push_back({17, "SC", 'b'});
    v.push_back({18, "", 'b'});
    v.push_back({19, "O", 'p'});
    v.push_back({20, "", 'p'});
    v.push_back({21, "Z", 'p'});
    v.push_back({22, "V", 'b'});
    v.push_back({23, "", 'b'});
    return v;
}

std::vector<Spec> IStreamSpecs()
{
    std::vector<Spec> v = {{0, "VP", 'b'}, {1, "", 'u'}, {2, "", 'u'}, {3, "V", 'b'}};
    for (int s = 4; s <= 13; ++s) v.push_back({s, "D", 'b'});
    v.push_back({14, "DC", 'b'});
    v.push_back({15, "T", 'b'});
    v.push_back({16, "N", 'u'});
    v.push_back({17, "DC", 'b'});
    v.push_back({18, "", 'b'});
    v.push_back({19, "O", 'p'});
    v.push_back({20, "", 'p'});
    v.push_back({21, "Z", 'p'});
    v.push_back({22, "V", 'b'});
    v.push_back({23, "", 'b'});
    return v;
}

// The base streams: slots 17 and 18 (and 3) are the fakes in the copied tables, the others are the shared bodies.
std::vector<Spec> BaseOStreamSpecs()
{
    std::vector<Spec> v = OStreamSpecs();
    v.erase(std::remove_if(v.begin(), v.end(), [](const Spec &s) { return s.slot == 3 || s.slot == 17 || s.slot == 18; }), v.end());
    return v;
}
std::vector<Spec> BaseIStreamSpecs()
{
    std::vector<Spec> v = IStreamSpecs();
    v.erase(std::remove_if(v.begin(), v.end(), [](const Spec &s) { return s.slot == 3 || s.slot == 17 || s.slot == 18; }), v.end());
    return v;
}

Layout Lay(const Tables &t, size_t thisOffset, const void *v0, const void *v4, const void *v20, const void *v30)
{
    (void)t;
    return Layout{thisOffset, v0, v4, v20, v30};
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecord_PrimaryFace, 0x99cfa0)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 0, t.primary, t.record, t.ostream, t.istream);
    CheckTable(ctx, "serial record primary face", ctx.image.At(0xda6098), cGZPersistDBSerialRecord::OwnVTable(), lay,
               PrimarySpecs(), 60, 0x6098);
}

DIFF_TEST(cGZPersistDBSerialRecord_RecordFace, 0x1da060)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 4, t.primary, t.record, t.ostream, t.istream);
    CheckTable(ctx, "serial record +4 face", ctx.image.At(0xda6194), cGZPersistDBSerialRecord::OwnRecordVTable(), lay,
               RecordSpecs(), 80, 0x6194);
}

DIFF_TEST(cGZPersistDBSerialRecord_OStreamFace, 0x1da060)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 0x20, t.primary, t.record, t.ostream, t.istream);
    CheckTable(ctx, "serial record +0x20 face", ctx.image.At(0xda61ec), cGZPersistDBSerialRecord::OwnOStreamVTable(), lay,
               OStreamSpecs(), 80, 0x61ec);
}

DIFF_TEST(cGZPersistDBSerialRecord_IStreamFace, 0x1da060)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 0x30, t.primary, t.record, t.ostream, t.istream);
    CheckTable(ctx, "serial record +0x30 face", ctx.image.At(0xda6250), cGZPersistDBSerialRecord::OwnIStreamVTable(), lay,
               IStreamSpecs(), 80, 0x6250);
}

DIFF_TEST(cGZPersistDBRecord_BaseFace, 0x99b960)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 4, t.primary, t.baseRecord, t.ostream, t.istream);
    CheckTable(ctx, "cGZPersistDBRecord face", ctx.image.At(0xe5e990), cGZPersistDBRecord::OwnVTable(), lay, BaseRecordSpecs(), 80, 0xe5e990);
}

DIFF_TEST(cRZOStream_BaseFace, 0x1b6617)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 0x20, t.primary, t.record, t.baseO, t.istream);
    CheckTable(ctx, "cRZOStream face", ctx.image.At(0xda29bc), cRZOStream::OwnVTable(), lay, BaseOStreamSpecs(), 80, 0xda29bc);
}

DIFF_TEST(cRZIStream_BaseFace, 0x1b6617)
{
    InitFakes();
    Tables t = MakeTables(ctx);
    Layout lay = Lay(t, 0x30, t.primary, t.record, t.ostream, t.baseI);
    CheckTable(ctx, "cRZIStream face", ctx.image.At(0xda2a20), cRZIStream::OwnVTable(), lay, BaseIStreamSpecs(), 80, 0xda2a20);
}

// The abstract interface table: all 57 slots are the one pure body in the image.
DIFF_TEST(cIGZPersistDBSerialRecord_InterfaceFace, 0x99cfa0)
{
    void *const *image = static_cast<void *const *>(ctx.image.At(0xe5e7dc));
    for (int i = 1; i < 57; ++i) {
        ++ctx.cases;
        if (image[i] != image[0]) ctx.Fail("interface table: slot %d is not the pure body", i);
    }
    if (cGZPersistDBSerialRecord::OwnInterfaceVTable() == nullptr) ctx.Fail("no interface table");
}
