// Differential tests for lane FK: cTSPersistSystem's primary slot 39 OpenRecord override (0x1e2850), its record compare
// helper (0x1dacb0) and the RAM-shadow record it builds (cRAMShadowRecord: deleting destructor 0x1da120; its other own
// slots are byte matches).
//
// OpenRecord's world: a primary block (0x300 seeded bytes) with a fake primary vtable (slot 34 FindSegmentWithKey), a
// fake cITSPersistSystem face at +0xa8 (slots 37 CopyRecord, 58 GetCachedResource, 59 NoteResourceChange), a fake lock at
// +0x98 and one listener in the vector at +0x230 (NotifyListeners is ours / the original's, verified). The store at +0x84
// (or 0), +0x22d and +0x22e are drawn from the seed, and so is the mode (garbage above the tested byte). The base
// cGZPersistResourceManager::vf39 runs for real on both sides (the original's 0x9b50a0 and ours): it reaches the fake
// slot 34 and the segment fakes' slot 22. Records, segments and the other objects are fakes that log every call with
// their arguments; every answer comes from the seed and the call number, with garbage above the low byte.
// The shadow record is real (the game's vtables; its slots run the original code on both sides). Compared: the result,
// the call log, the primary block after, and the handed-out record: a fake's name, or the shadow's whole state (vtables,
// count, flags, key, child, stream words, position, error, size, capacity, bytes), after which it is released (its
// destructor's child release is in the log). With +0x22e set the original does not check the debug record for null, so
// those worlds make every base open succeed.
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/cRZVariant.h"
#include "ts/cRAMShadowRecord.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

uint32_t gSeed = 0;
uint32_t gCalls = 0;
int gSide = 0; // 0 the original, 1 ours: which cRZVariant vtable the empty variant handed to ITS 59 carries

// The vtable our cRZVariant carries (ours builds the empty variant with our class since lane LQ; the original with
// the image's constructor and its vtable 0xe6180c).
const void *OurVariantVTable()
{
    static const void *const vtable = [] {
        cRZVariant probe;
        return *reinterpret_cast<const void *const *>(&probe);
    }();
    return vtable;
}
std::vector<std::string> gLog;
alignas(16) uint8_t gPrimary[0x300];
bool gForceOpen = false;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

// The next seeded answer: garbage above a low byte that is 1 with the given chance (percent).
uint32_t Answer(uint32_t percent)
{
    ++gCalls;
    const uint32_t r = Mix(gSeed, gCalls);
    return (r & 0xffffff00u) | (Mix(gSeed, 1000 + gCalls) % 100 < percent ? 1u : 0u);
}

std::string Hex(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

void Put(uint32_t off, uint32_t v)
{
    std::memcpy(gPrimary + off, &v, 4);
}

// ---- fakes: a vtable pointer, a kind letter and an index ----

struct Obj {
    void *const *vt;
    char kind; // 'g' segment, 'r' record, 'c' a record's child, 'o' cached object, 't' store, 'p' packed face, 'l' listener
    int index;
};

constexpr int kRecords = 6;
Obj gSegments[3], gRecords[kRecords], gChildren[3], gCached[2], gStore, gPacked, gListener;
void *gSegVT[24], *gRecVT[24], *gPlainVT[8], *gStoreVT[28], *gListenerVT[8], *gLockVT[8], *gPrimaryVT[64], *gItsVT[80];
const uint32_t *gKey = nullptr;

bool IsFake(const void *p)
{
    for (const Obj *set : {gSegments, gChildren}) {
        for (int i = 0; i < 3; ++i) if (p == &set[i]) return true;
    }
    for (int i = 0; i < kRecords; ++i) if (p == &gRecords[i]) return true;
    return p == &gCached[0] || p == &gCached[1] || p == &gStore || p == &gPacked || p == &gListener;
}

std::string Name(const void *p)
{
    if (!p) return "null";
    if (!IsFake(p)) return "other";
    const Obj *o = static_cast<const Obj *>(p);
    return std::string(1, o->kind) + std::to_string(o->index);
}

std::string KeyText(const void *key)
{
    if (key == gKey) return "key";
    if (!key) return "null";
    const uint32_t *k = static_cast<const uint32_t *>(key);
    return "{" + Hex(k[0]) + "," + Hex(k[1]) + "," + Hex(k[2]) + "," + Hex(k[3]) + "}";
}

// A record's size: fixed per record and seed (sizes around the 0x4000 limit included).
uint32_t RecordSize(int index)
{
    static const uint32_t kSizes[] = {0, 1, 3, 8, 13, 0x4000, 0x4001, 0x1234};
    return kSizes[Mix(gSeed, 400 + index) % 8];
}

uint8_t RecordByte(int index, uint32_t i)
{
    return static_cast<uint8_t>(Mix(gSeed ^ (index * 0x101u), i));
}

Obj *NextRecord()
{
    return &gRecords[Mix(gSeed, 300 + gCalls) % kRecords];
}

uint32_t __fastcall LogAddRef(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".AddRef");
    return Mix(gSeed, 1);
}
uint32_t __fastcall LogRelease(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".Release");
    return Mix(gSeed, 2);
}

// Segment slot 22, OpenRecord(key, out, mode).
uint32_t __fastcall SegOpen(Obj *self, void *, const uint32_t *key, Obj **out, uint32_t mode)
{
    uint32_t r = Answer(70);
    if (gForceOpen) r |= 1;
    gLog.push_back(Name(self) + ".OpenRecord(" + KeyText(key) + ", " + Hex(mode) + ") -> " + Hex(r & 0xff));
    if ((r & 0xff) || Mix(gSeed, 50 + gCalls) % 4 == 0) *out = NextRecord();
    return r;
}

// Record slots.
uint32_t __fastcall RecClose(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".Close");
    return Mix(gSeed, 3);
}
uint32_t __fastcall RecRead(Obj *self, void *, uint8_t *dst, uint32_t n)
{
    const uint32_t r = Answer(85);
    gLog.push_back(Name(self) + ".Read(" + std::string(dst ? "dst" : "null") + ", " + Hex(n) + ") -> " + Hex(r & 0xff));
    for (uint32_t i = 0; i < n; ++i) dst[i] = RecordByte(self->index, i);
    return r;
}
uint32_t __fastcall RecSize(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".GetSize");
    return RecordSize(self->index);
}
uint32_t __fastcall RecSeek(Obj *self, void *, uint32_t pos)
{
    gLog.push_back(Name(self) + ".SeekAbsolute(" + Hex(pos) + ")");
    return Mix(gSeed, 4);
}
uint32_t __fastcall RecQueryChild(Obj *self, void *, uint32_t iid, Obj **out)
{
    const uint32_t r = Answer(75);
    gLog.push_back(Name(self) + ".QueryChild(" + Hex(iid) + ") -> " + Hex(r & 0xff));
    if ((r & 0xff) || Mix(gSeed, 60 + gCalls) % 3 == 0) *out = &gChildren[self->index % 3];
    return r;
}

// The store (+0x84): 0 QueryInterface, 2 Release, 22 OpenRecord, 26 DeleteRecord.
uint32_t __fastcall StoreQuery(Obj *self, void *, uint32_t iid, Obj **out)
{
    const uint32_t r = Answer(60);
    gLog.push_back(Name(self) + ".QueryInterface(" + Hex(iid) + ") -> " + Hex(r & 0xff));
    if (r & 0xff) *out = &gPacked;
    return r;
}
uint32_t __fastcall StoreOpen(Obj *self, void *, const uint32_t *key, Obj **out, uint32_t mode)
{
    const uint32_t r = Answer(75);
    gLog.push_back(Name(self) + ".OpenRecord(" + KeyText(key) + ", " + Hex(mode) + ") -> " + Hex(r & 0xff));
    if ((r & 0xff) || Mix(gSeed, 70 + gCalls) % 4 == 0) *out = NextRecord();
    return r;
}
void __fastcall StoreDelete(Obj *self, void *, const uint32_t *key)
{
    gLog.push_back(Name(self) + ".DeleteRecord(" + KeyText(key) + ")");
}

uint32_t __fastcall ListenerNotify(Obj *self, void *, uint32_t event, uint32_t arg)
{
    gLog.push_back(Name(self) + ".Notify(" + Hex(event) + ", " + KeyText(reinterpret_cast<const void *>(static_cast<uintptr_t>(arg))) + ")");
    return Mix(gSeed, 5);
}
uint32_t __fastcall LockEnter(void *self, void *)
{
    gLog.push_back(std::string("lockD") + (self == gPrimary + 0x98 ? "" : "?"));
    return Mix(gSeed, 6);
}
uint32_t __fastcall LockLeave(void *self, void *)
{
    gLog.push_back(std::string("unlockD") + (self == gPrimary + 0x98 ? "" : "?"));
    return Mix(gSeed, 7);
}

// Primary slot 34, FindSegmentWithKey(key, out).
uint32_t __fastcall FindSegment(void *self, void *, const uint32_t *key, Obj **out)
{
    uint32_t r = Answer(75);
    if (gForceOpen) r |= 1;
    gLog.push_back("p.34(" + KeyText(key) + ") -> " + Hex(r & 0xff) + (self == gPrimary ? "" : "?"));
    if ((r & 0xff) || Mix(gSeed, 80 + gCalls) % 4 == 0) *out = &gSegments[Mix(gSeed, 81 + gCalls) % 3];
    return r;
}

// ITS slots (this = primary + 0xa8).
uint32_t __fastcall ItsCopy(void *self, void *, const uint32_t *fromKey, Obj *from, const uint32_t *toKey, Obj *to)
{
    const uint32_t r = Answer(70);
    gLog.push_back("its.37(" + KeyText(fromKey) + ", " + Name(from) + ", " + KeyText(toKey) + ", " + Name(to) + ") -> " + Hex(r & 0xff) +
                   (self == gPrimary + 0xa8 ? "" : "?"));
    return r;
}
uint32_t __fastcall ItsCached(void *self, void *, uint32_t group, const uint32_t *key, Obj **out)
{
    const uint32_t r = Answer(55);
    gLog.push_back("its.58(" + Hex(group) + ", " + KeyText(key) + ") -> " + Hex(r & 0xff) + (self == gPrimary + 0xa8 ? "" : "?"));
    if (Mix(gSeed, 90 + gCalls) % 3 == 0) *out = &gCached[gCalls % 2];
    return r;
}
void __fastcall ItsNote(void *self, void *, uint32_t group, const uint32_t *key, const uint8_t *variant)
{
    uint16_t count, type;
    std::memcpy(&count, variant + 0xc, 2);
    std::memcpy(&type, variant + 0xe, 2);
    const void *const expected = gSide == 0 ? GameAddress(0xe6180c) : OurVariantVTable();
    const bool vt = *reinterpret_cast<const void *const *>(variant) == expected;
    gLog.push_back("its.59(" + Hex(group) + ", " + KeyText(key) + ", variant " + Hex(count) + "/" + Hex(type) + (vt ? "" : " vt?") + ")" +
                   (self == gPrimary + 0xa8 ? "" : "?"));
}

void BuildFakes()
{
    gSegVT[1] = reinterpret_cast<void *>(&LogAddRef);
    gSegVT[2] = reinterpret_cast<void *>(&LogRelease);
    gSegVT[22] = reinterpret_cast<void *>(&SegOpen);
    gRecVT[1] = reinterpret_cast<void *>(&LogAddRef);
    gRecVT[2] = reinterpret_cast<void *>(&LogRelease);
    gRecVT[6] = reinterpret_cast<void *>(&RecClose);
    gRecVT[8] = reinterpret_cast<void *>(&RecRead);
    gRecVT[10] = reinterpret_cast<void *>(&RecSize);
    gRecVT[13] = reinterpret_cast<void *>(&RecSeek);
    gRecVT[15] = reinterpret_cast<void *>(&RecQueryChild);
    gPlainVT[1] = reinterpret_cast<void *>(&LogAddRef);
    gPlainVT[2] = reinterpret_cast<void *>(&LogRelease);
    gStoreVT[0] = reinterpret_cast<void *>(&StoreQuery);
    gStoreVT[2] = reinterpret_cast<void *>(&LogRelease);
    gStoreVT[22] = reinterpret_cast<void *>(&StoreOpen);
    gStoreVT[26] = reinterpret_cast<void *>(&StoreDelete);
    gListenerVT[3] = reinterpret_cast<void *>(&ListenerNotify);
    gLockVT[1] = reinterpret_cast<void *>(&LockEnter);
    gLockVT[2] = reinterpret_cast<void *>(&LockLeave);
    gPrimaryVT[34] = reinterpret_cast<void *>(&FindSegment);
    gItsVT[37] = reinterpret_cast<void *>(&ItsCopy);
    gItsVT[58] = reinterpret_cast<void *>(&ItsCached);
    gItsVT[59] = reinterpret_cast<void *>(&ItsNote);
    for (int i = 0; i < 3; ++i) {
        gSegments[i] = Obj{gSegVT, 'g', i};
        gChildren[i] = Obj{gPlainVT, 'c', i};
    }
    for (int i = 0; i < kRecords; ++i) gRecords[i] = Obj{gRecVT, 'r', i};
    gCached[0] = Obj{gPlainVT, 'o', 0};
    gCached[1] = Obj{gPlainVT, 'o', 1};
    gStore = Obj{gStoreVT, 't', 0};
    gPacked = Obj{gPlainVT, 'p', 0};
    gListener = Obj{gListenerVT, 'l', 0};
}

void *gListeners[1];

void BuildWorld(uint32_t seed)
{
    std::mt19937 rng(seed * 7 + 1);
    for (uint8_t &b : gPrimary) b = static_cast<uint8_t>(rng());
    Put(0, Word(gPrimaryVT));
    Put(0x98, Word(gLockVT));
    Put(0xa8, Word(gItsVT));
    gListeners[0] = &gListener;
    Put(0x230, Word(&gListeners[0]));
    Put(0x234, Word(&gListeners[1]));
    const uint32_t shape = rng() % 16;
    Put(0x84, shape == 0 ? 0 : Word(&gStore));
    gPrimary[0x22d] = shape == 1 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
    gPrimary[0x22e] = rng() % 4 == 0 ? static_cast<uint8_t>(1 + rng() % 255) : 0;
    gForceOpen = gPrimary[0x22e] != 0;
}

std::string DumpRecord(void *face)
{
    if (!face || IsFake(face)) return Name(face);
    auto *shadow = reinterpret_cast<cRAMShadowRecord *>(static_cast<uint8_t *>(face) - 4);
    const auto *raw = reinterpret_cast<const uint8_t *>(shadow);
    auto word = [&](uint32_t off) {
        uint32_t v;
        std::memcpy(&v, raw + off, 4);
        return v;
    };
    std::string s = "shadow{";
    s += word(0) == Word(GameAddress(cRAMShadowRecord::kVTable)) ? "vt" : "vt?";
    s += word(4) == Word(GameAddress(cRAMShadowRecord::kRecordVTable)) ? " vt4" : " vt4?";
    s += word(0x20) == Word(GameAddress(cRAMShadowRecord::kOStreamVTable)) ? " vt20" : " vt20?";
    s += word(0x30) == Word(GameAddress(cRAMShadowRecord::kIStreamVTable)) ? " vt30" : " vt30?";
    s += " +8 " + Hex(word(8)) + " key " + KeyText(raw + 0xc) + " child " + Name(reinterpret_cast<void *>(static_cast<uintptr_t>(word(0x1c))));
    // The streams' byte-order flags (+0x28, +0x38) and mSkipByteSwap (+0x40) are bytes: cRZOStream::Construct (0x1b4f75)
    // and cRZIStream::Construct (0x1b4d82) write `mov byte ptr [esi + 8], al`, the base constructor a byte at +0x40, and
    // nothing writes the three bytes after each (heap garbage from operator new), so only the low byte is compared.
    for (uint32_t off = 0x24; off < 0x30; off += 4) s += " " + Hex(off == 0x28 ? word(off) & 0xff : word(off));
    for (uint32_t off = 0x34; off < 0x44; off += 4) s += " " + Hex(off == 0x38 || off == 0x40 ? word(off) & 0xff : word(off));
    s += " pos " + Hex(shadow->mPosition) + " err " + Hex(shadow->mError) + " size " + Hex(static_cast<uint32_t>(shadow->mData.size())) + " cap " +
         Hex(static_cast<uint32_t>(shadow->mData.capacity())); // +0x55..+0x57: padding, never written (heap garbage)
    uint32_t h = 0;
    for (uint8_t b : shadow->mData) h = Mix(h, b);
    s += " bytes " + Hex(h) + "}";
    return s;
}

struct Result {
    uint32_t ret = 0;
    std::string record;
    std::vector<std::string> log;
    std::vector<uint8_t> primary;
};

void Compare(difftest::Context &ctx, uint32_t seed, const Result &a, const Result &b)
{
    if (a.ret != b.ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, a.ret, b.ret);
    if (a.record != b.record) ctx.Fail("seed %u: record: original %s, ours %s", seed, a.record.c_str(), b.record.c_str());
    if (a.log != b.log) {
        size_t i = 0;
        while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
        ctx.Fail("seed %u: call %zu differs: original '%s', ours '%s'", seed, i, i < a.log.size() ? a.log[i].c_str() : "(end)",
                 i < b.log.size() ? b.log[i].c_str() : "(end)");
    }
    if (a.primary != b.primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
}

} // namespace

PS_TEST(TSPersistSystemFK_OpenRecord, 0x1e2850)
{
    using F = uint32_t(__thiscall *)(void *, const uint32_t *, void **, uint32_t);
    auto orig = ctx.Original<F>(0x1e2850);
    BuildFakes();
    uint32_t direct = 0, shadows = 0, checked = 0, copied = 0, noted = 0, big = 0, fallbackAfterOpen = 0;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Result sides[2];
        uint32_t key[4];
        {
            std::mt19937 k(seed + 11);
            for (uint32_t &w : key) w = k();
        }
        uint32_t mode;
        {
            std::mt19937 m(seed + 5);
            const uint32_t pick = m() % 6;
            mode = pick == 0 ? 1 : pick == 1 ? 0x100 : pick == 2 ? (m() & 0xffffff00u) | 0x80 : (m() & 0xffffff00u);
        }
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side == 0 ? "original" : "ours");
            gSeed = seed;
            gCalls = 0;
            gSide = side;
            gLog.clear();
            gKey = key;
            BuildWorld(seed);
            void *out = nullptr;
            const uint32_t r = side == 0 ? orig(gPrimary, key, &out, mode)
                                         : psshape::Call(PSX(vf39), gPrimary, key, &out, mode);
            Result &s = sides[side];
            s.ret = r & 0xff;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            s.record = DumpRecord(out);
            if (out && !IsFake(out)) {
                gLog.push_back("-- release the shadow");
                VirtualCall<uint32_t(__thiscall *)(void *), 2>(out);
            }
            s.log = gLog;
        }
        ++ctx.cases;
        Compare(ctx, seed, sides[0], sides[1]);
        const std::vector<std::string> &log = sides[0].log;
        bool sawCached = false, sawStoreOpen = false, sawDelete = false;
        for (const std::string &line : log) {
            sawCached = sawCached || line.rfind("its.58(", 0) == 0;
            sawStoreOpen = sawStoreOpen || (line.rfind("t0.OpenRecord(", 0) == 0 && line.find("-> 0x1") != std::string::npos);
            sawDelete = sawDelete || line.rfind("t0.DeleteRecord(", 0) == 0;
            copied += line.rfind("its.37(", 0) == 0;
            noted += line.rfind("its.59(", 0) == 0;
        }
        // A record opened through the base but over 0x4000 bytes: no store update.
        big += sawCached && !sawStoreOpen && !sawDelete && sides[0].ret != 0;
        direct += !sawCached;
        const bool shadow = sides[0].record.rfind("shadow", 0) == 0;
        shadows += shadow;
        checked += shadow && gPrimary[0x22e] != 0;
        fallbackAfterOpen += sawCached && !sawStoreOpen;
    }
    if (direct < 300 || shadows < 300 || checked < 50 || copied < 200 || noted < 100 || fallbackAfterOpen < 300 || big < 50)
        ctx.Fail("coverage: %u direct, %u shadows, %u checked, %u copied, %u noted, %u fallback, %u big", direct, shadows, checked, copied, noted,
                 fallbackAfterOpen, big);
}

// ---- CompareRecordContents (0x1dacb0) ----

namespace {

std::vector<uint8_t> gContent[2];
uint32_t gReported[2];

uint32_t __fastcall CmpSize(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".GetSize");
    return gReported[self->index];
}
uint32_t __fastcall CmpRead(Obj *self, void *, uint8_t *dst, uint32_t n)
{
    const uint32_t r = Answer(85);
    char where[32];
    std::snprintf(where, sizeof(where), "%s", dst ? "dst" : "null");
    gLog.push_back(Name(self) + ".Read(" + where + ", " + Hex(n) + ") -> " + Hex(r & 0xff));
    const std::vector<uint8_t> &c = gContent[self->index];
    for (uint32_t i = 0; i < n; ++i) dst[i] = i < c.size() ? c[i] : 0xee;
    return r;
}

} // namespace

DIFF_TEST(TSPersistSystemFK_CompareRecordContents, 0x1dacb0)
{
    using F = uint32_t(__cdecl *)(void *, void *);
    auto orig = ctx.Original<F>(0x1dacb0);
    void *vt[16] = {};
    vt[8] = reinterpret_cast<void *>(&CmpRead);
    vt[10] = reinterpret_cast<void *>(&CmpSize);
    Obj a{vt, 'a', 0}, b{vt, 'b', 1};
    uint32_t same = 0, differ = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 rng(seed * 3 + 2);
        const uint32_t sizes[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 31, 64, 0x1000, 0x1003};
        const uint32_t size = sizes[rng() % 13];
        gReported[0] = size;
        gReported[1] = rng() % 6 == 0 ? sizes[rng() % 13] : size;
        gContent[0].resize(size);
        for (uint8_t &c : gContent[0]) c = static_cast<uint8_t>(rng() % 3);
        gContent[1] = gContent[0];
        const uint32_t edit = rng() % 4;
        if (edit != 0 && size) {
            const uint32_t at = edit == 1 ? 0 : edit == 2 ? size - 1 : rng() % size;
            gContent[1][at] ^= static_cast<uint8_t>(1 + rng() % 255);
        }
        uint32_t ret[2];
        std::vector<std::string> logs[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side == 0 ? "compare: original" : "compare: ours");
            gSeed = seed;
            gCalls = 0;
            gLog.clear();
            ret[side] = (side == 0 ? orig(&a, &b) : CompareRecordContents(&a, &b)) & 0xff;
            logs[side] = gLog;
        }
        ++ctx.cases;
        same += ret[0] != 0;
        differ += ret[0] == 0 && logs[0].size() == 4;
        if (ret[0] != ret[1]) ctx.Fail("seed %u (size %u): original returned %u, ours %u", seed, size, ret[0], ret[1]);
        if (logs[0] != logs[1]) {
            size_t i = 0;
            while (i < logs[0].size() && i < logs[1].size() && logs[0][i] == logs[1][i]) ++i;
            ctx.Fail("seed %u: call %zu differs: original '%s', ours '%s'", seed, i, i < logs[0].size() ? logs[0][i].c_str() : "(end)",
                     i < logs[1].size() ? logs[1][i].c_str() : "(end)");
        }
    }
    if (same < 500 || differ < 500) ctx.Fail("coverage: %u same, %u differ after both reads", same, differ);
}

// ---- cRAMShadowRecord's deleting destructor (0x1da120) ----

namespace {
int gChildReleases = 0;
uint32_t __fastcall CountRelease(void *, void *)
{
    ++gChildReleases;
    return 0;
}
} // namespace

DIFF_TEST(TSPersistSystemFK_ShadowDestructor, 0x1da120)
{
    using F = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<F>(0x1da120);
    void *childVT[8] = {};
    childVT[2] = reinterpret_cast<void *>(&CountRelease);
    struct {
        void *vt;
    } child{childVT};
    std::mt19937 rng(0x1da1);
    for (int trial = 0; trial < 480; ++trial) {
        const uint32_t flags = trial % 4 == 0 ? 0 : trial % 4 == 1 ? 1 : trial % 4 == 2 ? 0x100 : 0xff;
        const uint32_t sizes[] = {0, 1, 100, 0x1000, 0x2345, 0};
        const uint32_t size = sizes[(trial / 4) % 6];
        const bool reserveOnly = (trial / 24) % 2 == 1; // capacity without size
        const bool withChild = (trial / 48) % 2 == 0;
        uint32_t key[4] = {uint32_t(rng()), uint32_t(rng()), uint32_t(rng()), uint32_t(rng())};
        const uint32_t position = rng() % 7;
        struct Out {
            bool returnedThis = false, objectFreed = false, dataFreed = false;
            size_t frees = 0;
            int childReleases = 0;
            std::vector<uint8_t> bytes;
        } out[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side == 0 ? "destructor: original" : "destructor: ours");
            auto *rec = new cRAMShadowRecord(reinterpret_cast<const cGZPersistResourceKey *>(key));
            if (reserveOnly) rec->mData.reserve(size);
            else rec->mData.resize(size);
            rec->mPosition = position;
            if (withChild) rec->mChild = &child;
            // Padding nothing writes (after the stream flags +0x28 / +0x38, mSkipByteSwap +0x40 and mError +0x54; see
            // DumpRecord): cleared so the bytes after can be compared whole.
            for (uint32_t off : {0x29u, 0x39u, 0x41u, 0x55u}) std::memset(reinterpret_cast<uint8_t *>(rec) + off, 0, 3);
            // The heap block behind the data: big blocks (0x1000 bytes and more) are over-aligned, the real block's address
            // stored just before the data.
            void *data = rec->mData.data();
            void *block = data && rec->mData.capacity() >= 0x1000 ? static_cast<void **>(data)[-1] : data;
            gChildReleases = 0;
            void *returned;
            {
                difftest::FreeRecorder freed;
                returned = side == 0 ? orig(rec, flags) : rec->DeletingDestructor(flags);
                freed.Stop();
                out[side].objectFreed = freed.Freed(rec);
                out[side].frees = freed.Count();
                out[side].dataFreed = block && freed.Freed(block);
            }
            out[side].returnedThis = returned == rec;
            out[side].childReleases = gChildReleases;
            if (!out[side].objectFreed) {
                const auto *raw = reinterpret_cast<const uint8_t *>(rec);
                out[side].bytes.assign(raw, raw + cRAMShadowRecord::kSize);
                ::operator delete(static_cast<void *>(rec), cRAMShadowRecord::kSize);
            }
        }
        ++ctx.cases;
        if (out[0].returnedThis != out[1].returnedThis) ctx.Fail("trial %d: returns this %d / %d", trial, out[0].returnedThis, out[1].returnedThis);
        if (out[0].objectFreed != out[1].objectFreed) ctx.Fail("trial %d: object freed %d / %d", trial, out[0].objectFreed, out[1].objectFreed);
        if (out[0].frees != out[1].frees) ctx.Fail("trial %d: %zu / %zu frees", trial, out[0].frees, out[1].frees);
        if (out[0].dataFreed != out[1].dataFreed) ctx.Fail("trial %d: data freed %d / %d", trial, out[0].dataFreed, out[1].dataFreed);
        if (out[0].childReleases != out[1].childReleases) ctx.Fail("trial %d: child released %d / %d", trial, out[0].childReleases, out[1].childReleases);
        if (out[0].bytes != out[1].bytes) ctx.Fail("trial %d: object bytes after differ", trial);
    }
}
