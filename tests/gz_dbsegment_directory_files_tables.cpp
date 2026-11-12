// Differential tests for the cGZDBSegmentDirectoryFiles functions that work on its two hash tables (the files
// table at +0x8c and the instance probes at +0x1c), its open-record lists, the shutdown/close paths and the
// small forwarders around them. The object is built in a fixed arena with real MSVC 14.40 containers (the same
// layout as the game's), every object it calls is a recording fake, and after each call the tables are compared
// structurally (list order, contents, bucket first/last per bucket), since node addresses differ between runs.
#include "code_patch.h"
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZDBSegmentDirectoryFiles.h"
#include "rz/DirectoryScan.h"
#include "rz/PathFileOps.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "app/AppServices2.h"
#include "gz/cGZRESIDtoExtensionMap.h"
#include "free_recorder.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRandom.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace {

constexpr size_t kObjBytes = 0x100;
constexpr int kRecords = 6;
constexpr uint32_t kRZStringVTable = 0xd9b470;

alignas(16) uint8_t gObj[kObjBytes];
uint8_t *gHeapObj = nullptr;
const uint8_t *gImage = nullptr;

struct FakeRecord {
    const void *vtable;   // +0: Release (2), Close (15), Abort (16)
    uint32_t id;          // +4
    const void *faceVt;   // +8: the cIGZPersistDBRecord face; slot 3 returns &key
    DirResourceKey key;
    uint8_t pad[0x184 - 0x1c];
    uint32_t access;      // +0x184: the access the record was opened with (2 and 3 write); set by the lane NP tests
};
static_assert(offsetof(FakeRecord, access) == 0x184, "a cGZDBSDFRecord's access at +0x184");
FakeRecord gRecs[kRecords];

struct FakeObj {
    const void *vtable;
    uint32_t pad[3];
};
FakeObj gNamespace, gCache, gAC, gFilter, gStrs[4];
uint32_t gOutSlot[4];
void *gScan = nullptr;

std::vector<int64_t> gLog;

enum Kind : int64_t {
    kLock = 1, kUnlock, kOpen, kSlot17, kLog30, kSlot32, kShutdown5, kClose9, kDelete33, kExt3, kExt5, kExt6,
    kTimerStart, kTimerStop, kTimerRunning, kTimerCallback, kNsTranslate, kNsRelease, kCacheRelease, kAcRelease,
    kAc5, kRecRelease, kRecClose, kRecAbort, kFaceKey, kFilter, kFromChar, kCopy, kAppend, kSprintf, kScanDtor,
    kAddRef, kIsEmpty, kSvcAlloc, kSvcFree,
};

// Set while a dump logs a word the call left as it was (lane RK): that word is the test's own data or a pointer it
// built, never one into the heap object (which sits at a different address on each side), so the heap object's range is
// not applied to it. A random fill word that fell inside one side's heap object read as a self-pointer there and as data
// on the other side (the DeletingDestructor flake).
bool gDataWord = false;

int64_t Id(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    auto in = [&](const void *base, size_t n) {
        auto b = reinterpret_cast<uintptr_t>(base);
        return a >= b && a < b + n;
    };
    if (p == nullptr) return 0;
    if (in(gObj, kObjBytes)) return 0x10000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gObj));
    // The construction and destruction tests' object, on the heap (the deleting destructor frees it): the same ids.
    if (gHeapObj != nullptr && !gDataWord && in(gHeapObj, kObjBytes))
        return 0x10000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gHeapObj));
    if (in(gRecs, sizeof(gRecs))) return 0x20000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gRecs));
    if (in(gStrs, sizeof(gStrs))) return 0x30000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gStrs));
    if (in(gOutSlot, sizeof(gOutSlot))) return 0x40000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gOutSlot));
    if (p == &gNamespace) return 0x50001;
    if (p == &gCache) return 0x50002;
    if (p == &gAC) return 0x50003;
    if (p == &gFilter) return 0x50004;
    if (p == gScan) return 0x50005;
    return -1;
}

int64_t Rva(const void *p)
{
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gImage));
}

void Push(int64_t kind, int64_t a = 0, int64_t b = 0)
{
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
}

void PushKey(const DirResourceKey *k)
{
    gLog.push_back(k->type);
    gLog.push_back(k->group);
    gLog.push_back(k->instance);
    gLog.push_back(k->resource);
}

void PushText(const char *s, size_t n)
{
    gLog.push_back(static_cast<int64_t>(n));
    for (size_t i = 0; i < n; ++i) gLog.push_back(static_cast<uint8_t>(s[i]));
}

// Answers of the fakes, set per trial.
bool gOpenRets[2];
int gOpenCalls = 0;
bool gRet17 = false;
uint8_t gRet32 = 0;
bool gExtOk = false;
uint32_t gExt3Ret = 0;
bool gAcRet = false;
bool gTimerRunning = false;
uint32_t gFilterSalt = 0;
std::string gNameText;
int gZeroRec = -1; // a record whose face key loses its resource part (FindEntry's instance matching)

// ---- fakes (__fastcall: ecx = this, edx unused, the rest on the stack, callee pops) ----

void __fastcall FakeLock(void *self, void *) { Push(kLock, Id(self)); }
void __fastcall FakeUnlock(void *self, void *) { Push(kUnlock, Id(self)); }
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Push(kAddRef, Id(self));
    return 1;
}
bool __fastcall FakeOpen(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kOpen, Id(self), (a & 0xff) | ((b & 0xff) << 8));
    return gOpenRets[gOpenCalls++ & 1];
}
bool __fastcall FakeSlot17(void *self, void *, const DirResourceKey *key, void *name)
{
    Push(kSlot17, Id(self), Id(key));
    PushKey(key);
    // The name is a cRZString the caller made (vtable, std::string, count): give it a text.
    auto *n = static_cast<DirFileName *>(name);
    gLog.push_back(Rva(n->vtable));
    gLog.push_back(static_cast<int64_t>(n->text.size()));
    if (gRet17) n->text.assign(gNameText);
    return gRet17;
}
void __fastcall FakeLog30(void *self, void *, const char *text) { Push(kLog30, Id(self), Rva(text)); }
uint8_t __fastcall FakeSlot32(void *self, void *, const DirResourceKey *key, void **out, uint32_t mode)
{
    Push(kSlot32, Id(self), Id(out));
    PushKey(key);
    gLog.push_back(mode);
    return gRet32;
}
bool __fastcall FakeShutdown5(void *self, void *)
{
    Push(kShutdown5, Id(self));
    return true;
}
bool __fastcall FakeClose9(void *self, void *)
{
    Push(kClose9, Id(self));
    return true;
}
void __fastcall FakeDelete33(void *self, void *, uint32_t flags) { Push(kDelete33, Id(self), flags); }
uint32_t __fastcall FakeExt3(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kExt3, Id(self), a);
    gLog.push_back(b);
    return gExt3Ret;
}
bool __fastcall FakeExt5(void *self, void *, uint32_t *type, uint32_t ext)
{
    Push(kExt5, Id(self), *type);
    gLog.push_back(ext);
    return gExtOk;
}
void __fastcall FakeExt6(void *self, void *) { Push(kExt6, Id(self)); }
void __fastcall FakeTimerStart(void *self, void *, uint32_t ms, void *ctx)
{
    Push(kTimerStart, Id(self), ms);
    gLog.push_back(Id(ctx));
}
void __fastcall FakeTimerStop(void *self, void *) { Push(kTimerStop, Id(self)); }
bool __fastcall FakeTimerRunning(void *self, void *)
{
    Push(kTimerRunning, Id(self));
    return gTimerRunning;
}
void __fastcall FakeTimerCallback(void *self, void *, void *fn, void *ctx)
{
    Push(kTimerCallback, Id(self), Rva(fn));
    gLog.push_back(Id(ctx));
}
void __fastcall FakeNsTranslate(void *self, void *, const DirResourceKey *key, DirResourceKey *out)
{
    Push(kNsTranslate, Id(self), Id(out));
    PushKey(key);
    *out = DirResourceKey{key->type ^ 0x55u, key->group, key->instance + 1, key->resource};
}
void __fastcall FakeNsRelease(void *self, void *) { Push(kNsRelease, Id(self)); }
void __fastcall FakeCacheRelease(void *self, void *) { Push(kCacheRelease, Id(self)); }
void __fastcall FakeAcRelease(void *self, void *) { Push(kAcRelease, Id(self)); }
bool __fastcall FakeAc5(void *self, void *, const DirResourceKey *key, void *out, uint32_t ext, void *segment)
{
    Push(kAc5, Id(self), Id(out));
    PushKey(key);
    gLog.push_back(ext);
    gLog.push_back(Id(segment));
    return gAcRet;
}
void __fastcall FakeRecRelease(void *self, void *) { Push(kRecRelease, Id(self)); }
void __fastcall FakeRecClose(void *self, void *) { Push(kRecClose, Id(self)); }
void __fastcall FakeRecAbort(void *self, void *) { Push(kRecAbort, Id(self)); }
const DirResourceKey *__fastcall FakeFaceKey(void *self, void *)
{
    Push(kFaceKey, Id(self));
    return &reinterpret_cast<FakeRecord *>(static_cast<uint8_t *>(self) - 8)->key;
}
bool __fastcall FakeFilter(void *self, void *, const DirResourceKey *key)
{
    Push(kFilter, Id(self));
    PushKey(key);
    return ((key->instance + gFilterSalt) & 1) != 0;
}
void __fastcall FakeFromChar(void *self, void *, const char *data, uint32_t len)
{
    Push(kFromChar, Id(self));
    PushText(data, len);
}
void __fastcall FakeCopy(void *self, void *, const void *src)
{
    // src is a cRZString (vtable, std::string, count): a file name in a node, or the path at +0x4c.
    auto *s = static_cast<const DirFileName *>(src);
    Push(kCopy, Id(self), Rva(s->vtable));
    PushText(s->text.data(), s->text.size());
}
void __fastcall FakeAppend(void *self, void *, const char *data, uint32_t len)
{
    Push(kAppend, Id(self));
    PushText(data, len);
}
const uint8_t *gFormat2 = nullptr; // the two-number format; its second argument is read only then
void __cdecl FakeSprintf(void *self, const char *fmt, uint32_t a, uint32_t b)
{
    Push(kSprintf, Id(self), Rva(fmt));
    gLog.push_back(a);
    if (reinterpret_cast<const uint8_t *>(fmt) == gFormat2) gLog.push_back(b);
}
void __fastcall FakeScanDtor(void *self, void *) { Push(kScanDtor, Id(self)); }
bool __fastcall FakeIsEmpty(void *self, void *)
{
    Push(kIsEmpty, Id(self));
    return true;
}

void *gLockTable[3], *gExtTable[7], *gTimerTable[10], *gNsTable[5], *gCacheTable[3], *gAcTable[6];
void *gRecTable[17], *gFaceTable[4], *gFilterTable[4], *gStrTable[32];

void InitFakes(const difftest::Context &ctx)
{
    gImage = static_cast<const uint8_t *>(ctx.image.At(0));
    gFormat2 = static_cast<const uint8_t *>(ctx.image.At(0xe5f128));
    gLockTable[1] = reinterpret_cast<void *>(&FakeLock);
    gLockTable[2] = reinterpret_cast<void *>(&FakeUnlock);
    gExtTable[3] = reinterpret_cast<void *>(&FakeExt3);
    gExtTable[5] = reinterpret_cast<void *>(&FakeExt5);
    gExtTable[6] = reinterpret_cast<void *>(&FakeExt6);
    gTimerTable[6] = reinterpret_cast<void *>(&FakeTimerStart);
    gTimerTable[7] = reinterpret_cast<void *>(&FakeTimerStop);
    gTimerTable[8] = reinterpret_cast<void *>(&FakeTimerRunning);
    gTimerTable[9] = reinterpret_cast<void *>(&FakeTimerCallback);
    gNsTable[2] = reinterpret_cast<void *>(&FakeNsRelease);
    gNsTable[3] = reinterpret_cast<void *>(&FakeNsTranslate);
    gCacheTable[2] = reinterpret_cast<void *>(&FakeCacheRelease);
    gAcTable[2] = reinterpret_cast<void *>(&FakeAcRelease);
    gAcTable[5] = reinterpret_cast<void *>(&FakeAc5);
    gRecTable[2] = reinterpret_cast<void *>(&FakeRecRelease);
    gRecTable[15] = reinterpret_cast<void *>(&FakeRecClose);
    gRecTable[16] = reinterpret_cast<void *>(&FakeRecAbort);
    gFaceTable[3] = reinterpret_cast<void *>(&FakeFaceKey);
    gFilterTable[3] = reinterpret_cast<void *>(&FakeFilter);
    gStrTable[3] = reinterpret_cast<void *>(&FakeFromChar);
    gStrTable[8] = reinterpret_cast<void *>(&FakeIsEmpty);
    gStrTable[17] = reinterpret_cast<void *>(&FakeCopy);
    gStrTable[20] = reinterpret_cast<void *>(&FakeAppend);
    gStrTable[31] = reinterpret_cast<void *>(&FakeSprintf);
    gNamespace.vtable = gNsTable;
    gCache.vtable = gCacheTable;
    gAC.vtable = gAcTable;
    gFilter.vtable = gFilterTable;
    for (auto &s : gStrs) s.vtable = gStrTable;
}

struct Tables {
    void *primary[37];
    void *sub[50];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBSegmentDirectoryFiles::kVTable), sizeof(t.primary));
    std::memcpy(t.sub, ctx.image.At(cGZDBSegmentDirectoryFiles::kSubVTable), sizeof(t.sub));
    t.primary[1] = reinterpret_cast<void *>(&FakeAddRef);
    t.primary[5] = reinterpret_cast<void *>(&FakeShutdown5);
    t.primary[6] = reinterpret_cast<void *>(&FakeOpen);
    t.primary[17] = reinterpret_cast<void *>(&FakeSlot17);
    t.primary[30] = reinterpret_cast<void *>(&FakeLog30);
    t.primary[32] = reinterpret_cast<void *>(&FakeSlot32);
    t.sub[1] = reinterpret_cast<void *>(&FakeAddRef);
    t.sub[9] = reinterpret_cast<void *>(&FakeClose9);
    t.sub[33] = reinterpret_cast<void *>(&FakeDelete33);
    return t;
}

// Patches the first bytes of an original function with a jump to a stand-in, for both sides of a test.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(const difftest::Context &ctx, uint32_t rva, void *stand)
        : site(static_cast<uint8_t *>(const_cast<void *>(static_cast<const void *>(ctx.image.At(rva)))))
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

// The allocator service (cache 0x10c95d4, guard 0x10c95d8) the key vector's memory comes from: a fake that logs.
void *gSvcTab[5];
uint8_t gSvcObject[8];

void *__fastcall FakeSvcAlloc(void *, void *, uint32_t bytes)
{
    Push(kSvcAlloc, bytes);
    return HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
}

void __fastcall FakeSvcFree(void *, void *, void *mem)
{
    Push(kSvcFree, mem != nullptr);
    if (mem != nullptr) HeapFree(GetProcessHeap(), 0, mem);
}

struct ServiceScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    explicit ServiceScope(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        gSvcTab[3] = reinterpret_cast<void *>(&FakeSvcAlloc);
        gSvcTab[4] = reinterpret_cast<void *>(&FakeSvcFree);
        void *tab = gSvcTab;
        std::memcpy(gSvcObject, &tab, sizeof(tab));
        *cache = gSvcObject;
        *guard = INT32_MIN;
    }
    ~ServiceScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// ---- the world ----

struct FileSpec {
    DirResourceKey key;
    std::string name;
    std::vector<int> recs; // record indices; -1 is a null record
};

struct Spec {
    std::vector<uint8_t> fill;
    std::vector<FileSpec> files;
    std::vector<std::pair<uint32_t, DirInstanceProbe>> probes;
    std::string path;
    bool scan = false;
    bool ns = false;
    bool ac = false;
    uint32_t state = 0, next = 1, segOnce = 0, seconds = 0;
    uint8_t flag3C = 0, b48 = 0, b49 = 0, b0 = 0, b1 = 0, b2 = 0;
};

const uint32_t kTypes[] = {0x1000, 0x2000};
const uint32_t kGroups[] = {0x7f000001, 0x7f000002};
const uint32_t kResources[] = {0, 0, 0x55, 0xa0000000};

DirResourceKey PickKey(std::mt19937 &rng)
{
    return DirResourceKey{kTypes[rng() % 2], kGroups[rng() % 2], 1 + rng() % 6, kResources[rng() % 4]};
}

std::string PickName(std::mt19937 &rng)
{
    size_t n = rng() % 3 == 0 ? 16 + rng() % 20 : rng() % 12; // heap and inline strings
    std::string s;
    for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>('a' + rng() % 26));
    return s;
}

uint8_t PickByte(std::mt19937 &rng)
{
    static const uint8_t v[] = {0, 0, 1, 1, 2, 0x80};
    return v[rng() % 6];
}

Spec MakeSpec(std::mt19937 &rng)
{
    Spec s;
    s.fill.resize(kObjBytes);
    for (auto &b : s.fill) b = static_cast<uint8_t>(rng());
    int nFiles = static_cast<int>(rng() % 9);
    int nextRec = 0;
    for (int i = 0; i < nFiles; ++i) {
        FileSpec f;
        f.key = PickKey(rng);
        bool dup = false;
        for (auto &g : s.files) dup |= std::memcmp(&g.key, &f.key, sizeof(f.key)) == 0;
        if (dup) continue;
        f.name = PickName(rng);
        int nRecs = static_cast<int>(rng() % 3);
        for (int r = 0; r < nRecs; ++r) {
            if (rng() % 6 == 0) {
                f.recs.push_back(-1);
            } else if (nextRec < kRecords) {
                f.recs.push_back(nextRec++);
            }
        }
        s.files.push_back(std::move(f));
    }
    int nProbes = static_cast<int>(rng() % 4);
    for (int i = 0; i < nProbes; ++i) {
        uint32_t inst = 1 + rng() % 6;
        bool dup = false;
        for (auto &p : s.probes) dup |= p.first == inst;
        if (dup) continue;
        s.probes.push_back({inst, DirInstanceProbe{rng() % 2 == 0, kResources[rng() % 4]}});
    }
    s.path = PickName(rng);
    s.scan = rng() % 2 == 0;
    s.ns = rng() % 2 == 0;
    s.ac = rng() % 4 == 0;
    static const uint32_t states[] = {0, 0, 1, 2, 3, 4, 0x100};
    s.state = states[rng() % 7];
    static const uint32_t nexts[] = {1, 2, 3, 5, 0xfffffffd, 0xffffffff};
    s.next = nexts[rng() % 6];
    s.segOnce = kGroups[rng() % 2];
    static const uint32_t secs[] = {0, 14, 15, 16, 300};
    s.seconds = secs[rng() % 5];
    s.flag3C = PickByte(rng);
    s.b48 = PickByte(rng);
    s.b49 = PickByte(rng);
    s.b0 = PickByte(rng);
    s.b1 = PickByte(rng);
    s.b2 = PickByte(rng);
    return s;
}

template <typename T> void Put(size_t at, T v)
{
    std::memcpy(gObj + at, &v, sizeof(v));
}

cGZDBSegmentDirectoryFiles *Self()
{
    return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(gObj);
}

void *Sub()
{
    return gObj + 4;
}

void Build(const Spec &s, const Tables &t)
{
    std::memcpy(gObj, s.fill.data(), kObjBytes);
    Put<const void *>(0, t.primary);
    Put<const void *>(4, t.sub);
    Put<const void *>(0xc, gLockTable);
    Put<const void *>(0x14, &gCache);
    Put<const void *>(0x18, s.ns ? &gNamespace : nullptr);
    Put<uint8_t>(0x3c, s.flag3C);
    Put<uint32_t>(0x40, s.next);
    Put<uint32_t>(0x44, s.state);
    Put<uint8_t>(0x48, s.b48);
    Put<uint8_t>(0x49, s.b49);
    Put<const void *>(0x4c, gImage + kRZStringVTable);
    Put<uint32_t>(0x70, s.segOnce);
    Put<const void *>(0x7c, gExtTable);
    Put<const void *>(0xac, s.ac ? &gAC : nullptr);
    Put<uint8_t>(0xb0, s.b0);
    Put<uint8_t>(0xb1, s.b1);
    Put<uint8_t>(0xb2, s.b2);
    Put<uint32_t>(0xb4, s.seconds);
    Put<const void *>(0xb8, gTimerTable);
    gScan = s.scan ? ::operator new(0x5c) : nullptr;
    if (gScan) std::memset(gScan, 0x5a, 0x5c);
    Put<void *>(0x6c, gScan);

    cGZDBSegmentDirectoryFiles *self = Self();
    new (&self->mPath) std::string(s.path);
    new (&self->mProbes) DirProbeMap();
    new (&self->mFiles) DirFileMap();
    for (auto &p : s.probes) self->mProbes.emplace(p.first, p.second);
    for (auto &f : s.files) {
        auto &e = self->mFiles[f.key];
        e.name.vtable = gImage + kRZStringVTable;
        e.name.text = f.name;
        e.name.refCount = static_cast<uint32_t>(f.name.size()) * 7;
        for (int r : f.recs) {
            if (r < 0) {
                e.open.emplace_back(nullptr);
            } else {
                FakeRecord &rec = gRecs[r];
                rec.vtable = gRecTable;
                rec.id = static_cast<uint32_t>(r);
                rec.faceVt = gFaceTable;
                rec.key = f.key;
                e.open.emplace_back(&rec);
            }
        }
    }
    if (gZeroRec >= 0) gRecs[gZeroRec].key.resource = 0;
    gOpenCalls = 0;
}

// A table's list in order plus, per bucket, the list positions of its first and last node (-1: the head).
void DumpTable(std::vector<int64_t> &d, size_t at, int kind)
{
    const uint8_t *t = gObj + at;
    float lf;
    std::memcpy(&lf, t, 4);
    uint32_t bits;
    std::memcpy(&bits, &lf, 4);
    d.push_back(bits);
    uint8_t *head = *reinterpret_cast<uint8_t *const *>(t + 4);
    uint32_t size = *reinterpret_cast<const uint32_t *>(t + 8);
    d.push_back(size);
    std::map<const void *, int64_t> pos;
    pos[head] = -1;
    int64_t i = 0;
    for (uint8_t *n = *reinterpret_cast<uint8_t **>(head); n != head && i < 1000; n = *reinterpret_cast<uint8_t **>(n), ++i) {
        pos[n] = i;
        if (kind == 0) {
            d.push_back(*reinterpret_cast<uint32_t *>(n + 8));
            d.push_back(n[0xc]);
            d.push_back(*reinterpret_cast<uint32_t *>(n + 0x10));
        } else {
            for (int k = 0; k < 4; ++k) d.push_back(reinterpret_cast<uint32_t *>(n + 8)[k]);
            auto *e = reinterpret_cast<DirFileEntry *>(n + 0x18);
            d.push_back(Rva(e->name.vtable));
            d.push_back(e->name.refCount);
            d.push_back(static_cast<int64_t>(e->name.text.size()));
            for (char c : e->name.text) d.push_back(static_cast<uint8_t>(c));
            d.push_back(static_cast<int64_t>(e->open.size()));
            for (auto &r : e->open) d.push_back(Id(r.record));
        }
    }
    d.push_back(i);
    void *const *vb = *reinterpret_cast<void *const *const *>(t + 0xc);
    void *const *ve = *reinterpret_cast<void *const *const *>(t + 0x10);
    d.push_back(ve - vb);
    for (void *const *p = vb; p != ve; ++p) d.push_back(pos.count(*p) ? pos[*p] : -1000);
    d.push_back(*reinterpret_cast<const uint32_t *>(t + 0x18));
    d.push_back(*reinterpret_cast<const uint32_t *>(t + 0x1c));
}

std::vector<int64_t> Dump()
{
    std::vector<int64_t> d;
    DumpTable(d, 0x1c, 0);
    DumpTable(d, 0x8c, 1);
    cGZDBSegmentDirectoryFiles *self = Self();
    d.push_back(static_cast<int64_t>(self->mPath.size()));
    for (char c : self->mPath) d.push_back(static_cast<uint8_t>(c));
    d.push_back(Id(self->mScan));
    // Every other byte of the object.
    for (size_t at = 8; at < kObjBytes; ++at) {
        bool table = (at >= 0x1c && at < 0x3c) || (at >= 0x8c && at < 0xac);
        bool str = at >= 0x50 && at < 0x68;
        bool scan = at >= 0x6c && at < 0x70;
        if (!table && !str && !scan) d.push_back(gObj[at]);
    }
    return d;
}

void Teardown()
{
    cGZDBSegmentDirectoryFiles *self = Self();
    std::destroy_at(&self->mFiles);
    std::destroy_at(&self->mProbes);
    std::destroy_at(&self->mPath);
    if (self->mScan) ::operator delete(self->mScan);
    gScan = nullptr;
}

struct Outcome {
    uint64_t result = 0;
    std::vector<int64_t> log;
    std::vector<int64_t> dump;
};

template <typename Call> Outcome Run(const Spec &s, const Tables &t, Call call)
{
    Build(s, t);
    gLog.clear();
    Outcome out;
    out.result = call();
    out.log = gLog;
    out.dump = Dump();
    Teardown();
    gLog.clear();
    return out;
}

void Compare(difftest::Context &ctx, const char *name, int trial, const Outcome &a, const Outcome &b, const Outcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s #%d: result original %llu, ours %llu, via slot %llu", name, trial,
                 static_cast<unsigned long long>(a.result), static_cast<unsigned long long>(b.result),
                 static_cast<unsigned long long>(c.result));
    }
    if (a.log != b.log || a.log != c.log) {
        ctx.Fail("%s #%d: calls differ (%zu / %zu / %zu values)", name, trial, a.log.size(), b.log.size(), c.log.size());
        // The first value that differs, with the three before it (the kind and its first arguments).
        const std::vector<int64_t> &other = a.log != b.log ? b.log : c.log;
        size_t i = 0;
        while (i < a.log.size() && i < other.size() && a.log[i] == other[i]) ++i;
        auto at = [](const std::vector<int64_t> &v, size_t k) { return k < v.size() ? static_cast<long long>(v[k]) : -1LL; };
        ctx.Fail("  first at value %zu (%s): original %llx, %s %llx; before it %llx %llx %llx", i,
                 a.log != b.log ? "ours" : "via slot", at(a.log, i), a.log != b.log ? "ours" : "via slot", at(other, i),
                 i >= 3 ? at(a.log, i - 3) : -1LL, i >= 2 ? at(a.log, i - 2) : -1LL, i >= 1 ? at(a.log, i - 1) : -1LL);
    }
    if (a.dump != b.dump || a.dump != c.dump) ctx.Fail("%s #%d: object state differs", name, trial);
}

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// A key to look up: mostly one of the world's keys, or a variant of one (other resource, resource 0), or random.
DirResourceKey PickLookup(std::mt19937 &rng, const Spec &s)
{
    if (!s.files.empty() && rng() % 4 != 0) {
        DirResourceKey k = s.files[rng() % s.files.size()].key;
        switch (rng() % 4) {
        case 0: break;
        case 1: k.resource = 0; break;
        case 2: k.resource = kResources[rng() % 4]; break;
        default: k.instance = 1 + rng() % 6; break;
        }
        return k;
    }
    return PickKey(rng);
}

constexpr int kTrials = 600;

} // namespace

// ---- helpers ----

DIFF_TEST(cGZDBSegmentDirectoryFiles_FindEntry, 0x9a0f00)
{
    InitFakes(ctx);
    using Fn = void **(__thiscall *)(void *, void **, const DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x9a0f00);
    Tables t = MakeTables(ctx);
    std::mt19937 rng(0xd1f01);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        // The answer is a node: its list position (or -1 for the end) is what is compared.
        auto position = [](void *node) -> uint64_t {
            uint8_t *head = *reinterpret_cast<uint8_t **>(gObj + 0x90);
            if (node == head) return 0xffff;
            uint64_t i = 0;
            for (uint8_t *n = *reinterpret_cast<uint8_t **>(head); n != head; n = *reinterpret_cast<uint8_t **>(n), ++i) {
                if (n == node) return i;
            }
            return 0xeeee;
        };
        auto a = Run(s, t, [&] {
            void *out = nullptr;
            void **r = orig(gObj, &out, &key);
            return position(*r) | (r == &out ? 0 : 0x100000);
        });
        auto b = Run(s, t, [&] {
            auto it = Self()->FindEntry(key);
            void *node;
            std::memcpy(&node, &it, sizeof(node));
            return position(node);
        });
        auto viaAbi = reinterpret_cast<Fn>(Addr(&cGZDBSegmentDirectoryFiles::FindEntry));
        auto c = Run(s, t, [&] {
            void *out = nullptr;
            void **r = viaAbi(gObj, &out, &key);
            return position(*r) | (r == &out ? 0 : 0x100000);
        });
        Compare(ctx, "FindEntry", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_EraseProbe, 0x9a1df0)
{
    InitFakes(ctx);
    using Fn = void(__thiscall *)(void *, const DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x9a1df0);
    Tables t = MakeTables(ctx);
    std::mt19937 rng(0xd1f02);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        if (!s.probes.empty() && rng() % 2 == 0) key.instance = s.probes[rng() % s.probes.size()].first;
        auto a = Run(s, t, [&] {
            orig(gObj, &key);
            return 0;
        });
        auto b = Run(s, t, [&] {
            Self()->EraseProbe(&key);
            return 0;
        });
        auto c = Run(s, t, [&] {
            reinterpret_cast<Fn>(Addr(&cGZDBSegmentDirectoryFiles::EraseProbe))(gObj, &key);
            return 0;
        });
        Compare(ctx, "EraseProbe", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_ClearFiles, 0x9a2f30)
{
    InitFakes(ctx);
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a2f30);
    Tables t = MakeTables(ctx);
    std::mt19937 rng(0xd1f03);
    for (int i = 0; i < 300; ++i) {
        Spec s = MakeSpec(rng);
        auto a = Run(s, t, [&] {
            orig(gObj + 0x8c);
            return 0;
        });
        auto b = Run(s, t, [&] {
            cGZDBSegmentDirectoryFiles::ClearFiles(&Self()->mFiles);
            return 0;
        });
        auto c = Run(s, t, [&] {
            reinterpret_cast<Fn>(&cGZDBSegmentDirectoryFiles::ClearFiles)(gObj + 0x8c);
            return 0;
        });
        Compare(ctx, "ClearFiles", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_RemoveOpenRecord, 0x9a0d70)
{
    InitFakes(ctx);
    using Fn = void(__thiscall *)(void *, const void *);
    auto orig = ctx.Original<Fn>(0x9a0d70);
    Tables t = MakeTables(ctx);
    std::mt19937 rng(0xd1f04);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        if (s.files.empty()) continue;
        size_t which = rng() % s.files.size();
        int pick = static_cast<int>(rng() % (kRecords + 1)) - 1; // -1: the null face
        if (rng() % 3 == 0) {
            // Two null records in one list: only the first goes.
            s.files[which].recs.push_back(-1);
            s.files[which].recs.push_back(-1);
            pick = -1;
        }
        const void *face = pick < 0 ? nullptr : reinterpret_cast<const uint8_t *>(&gRecs[pick]) + 8;
        auto entry = [&]() -> DirFileEntry * { return &Self()->mFiles.find(s.files[which].key)->second; };
        auto a = Run(s, t, [&] {
            orig(entry(), face);
            return 0;
        });
        auto b = Run(s, t, [&] {
            entry()->RemoveOpenRecord(face);
            return 0;
        });
        auto c = Run(s, t, [&] {
            reinterpret_cast<Fn>(Addr(&DirFileEntry::RemoveOpenRecord))(entry(), face);
            return 0;
        });
        Compare(ctx, "RemoveOpenRecord", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_StartRefreshTimer, 0x9a2b30)
{
    InitFakes(ctx);
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a2b30);
    Tables t = MakeTables(ctx);
    std::mt19937 rng(0xd1f05);
    for (int i = 0; i < 300; ++i) {
        Spec s = MakeSpec(rng);
        gTimerRunning = rng() % 3 == 0;
        auto a = Run(s, t, [&] {
            orig(gObj);
            return 0;
        });
        auto b = Run(s, t, [&] {
            Self()->StartRefreshTimer();
            return 0;
        });
        auto c = Run(s, t, [&] {
            reinterpret_cast<Fn>(Addr(&cGZDBSegmentDirectoryFiles::StartRefreshTimer))(gObj);
            return 0;
        });
        Compare(ctx, "StartRefreshTimer", i, a, b, c);
    }
}

// ---- primary slots ----

DIFF_TEST(cGZDBSegmentDirectoryFiles_Shutdown, 0x9a2a60)
{
    InitFakes(ctx);
    shape::StandIn scan(0x1c49eb, &DirectoryScanAdapters::Destruct, &FakeScanDtor); // the image's and ours (lane LY)
    using Fn = bool(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a2a60);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[5] = Addr(&cGZDBSegmentDirectoryFiles::Shutdown);
    Tables thunk = MakeTables(ctx);
    thunk.sub[4] = Addr(&DirectorySegmentView::ShutdownThunk);
    std::mt19937 rng(0xd1f06);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj)); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(Self()->Shutdown()); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 5)(gObj)); });
        Compare(ctx, "Shutdown", i, a, b, c);
        auto d = Run(s, thunk, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 4)(Sub())); });
        auto e = Run(s, t, [&] { return static_cast<uint64_t>(ctx.Original<Fn>(0x9a2b22)(Sub())); });
        Compare(ctx, "ShutdownThunk", i, a, d, e);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_vf14, 0x99ed70)
{
    InitFakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x99ed70);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[14] = Addr(&cGZDBSegmentDirectoryFiles::Vf14);
    std::mt19937 rng(0xd1f07);
    for (int i = 0; i < 100; ++i) {
        Spec s = MakeSpec(rng);
        uint32_t x = rng(), y = rng();
        gExt3Ret = rng();
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, x, y)); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(Self()->Vf14(x, y)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 14)(gObj, x, y)); });
        Compare(ctx, "Vf14", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_GetFileName, 0x9a14a0)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *, void *);
    auto orig = ctx.Original<Fn>(0x9a14a0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[17] = Addr(&cGZDBSegmentDirectoryFiles::GetFileName);
    std::mt19937 rng(0xd1f08);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, &key, &gStrs[0]) & 0xff); });
        auto b = Run(s, t, [&] {
            return static_cast<uint64_t>(Self()->GetFileName(&key, reinterpret_cast<cIGZString *>(&gStrs[0])));
        });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 17)(gObj, &key, &gStrs[0]) & 0xff); });
        Compare(ctx, "GetFileName", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_GetFilePath, 0x9a1520)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *, void *);
    auto orig = ctx.Original<Fn>(0x9a1520);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[18] = Addr(&cGZDBSegmentDirectoryFiles::GetFilePath);
    std::mt19937 rng(0xd1f09);
    for (int i = 0; i < 300; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        gRet17 = rng() % 3 != 0;
        gNameText = PickName(rng);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, &key, &gStrs[1]) & 0xff); });
        auto b = Run(s, t, [&] {
            return static_cast<uint64_t>(Self()->GetFilePath(&key, reinterpret_cast<cIGZString *>(&gStrs[1])));
        });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 18)(gObj, &key, &gStrs[1]) & 0xff); });
        Compare(ctx, "GetFilePath", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_NewKey, 0x9a1100)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, uint32_t, DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x9a1100);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[21] = Addr(&cGZDBSegmentDirectoryFiles::NewKey);
    std::mt19937 rng(0xd1f0a);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        // Keys the search will walk into: the segment's group and the first instances.
        for (auto &f : s.files) {
            if (rng() % 2 == 0) f.key.group = s.segOnce;
            if (rng() % 2 == 0) f.key.resource = 0x77;
        }
        uint32_t type = kTypes[rng() % 2];
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        DirResourceKey out[3];
        auto runOne = [&](auto call) {
            out[0] = DirResourceKey{0x11, 0x22, 0x33, 0x77};
            uint64_t r = call() & 0xff;
            return (r << 32) ^ out[0].type ^ (static_cast<uint64_t>(out[0].group) << 8) ^
                   (static_cast<uint64_t>(out[0].instance) << 16) ^ (static_cast<uint64_t>(out[0].resource) << 24);
        };
        auto a = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(orig(gObj, type, out)); }); });
        auto b = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(Self()->NewKey(type, out)); }); });
        auto c = Run(s, routed, [&] { return runOne([&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 21)(gObj, type, out)); }); });
        Compare(ctx, "NewKey", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_GetFileNames, 0x9a0c80)
{
    InitFakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *, void **, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a0c80);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[22] = Addr(&cGZDBSegmentDirectoryFiles::GetFileNames);
    std::mt19937 rng(0xd1f0b);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        void *names[5] = {&gStrs[0], &gStrs[1], &gStrs[2], &gStrs[3], nullptr};
        if (rng() % 4 == 0) names[rng() % 4] = nullptr;
        void **arg = rng() % 4 == 0 ? nullptr : names;
        uint32_t max = rng() % 6;
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, arg, max)); });
        auto b = Run(s, t, [&] {
            return static_cast<uint64_t>(Self()->GetFileNames(reinterpret_cast<cIGZString **>(arg), max));
        });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 22)(gObj, arg, max)); });
        Compare(ctx, "GetFileNames", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SetAutoRefresh, 0x9a2920)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a2920);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[26] = Addr(&cGZDBSegmentDirectoryFiles::SetAutoRefresh);
    std::mt19937 rng(0xd1f0c);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        static const uint32_t enables[] = {0, 1, 2, 0x100, 0x80};
        uint32_t enable = enables[rng() % 5];
        static const uint32_t secs[] = {0, 14, 15, 16, 0x80000000, 1000};
        uint32_t seconds = secs[rng() % 6];
        gTimerRunning = rng() % 3 == 0;
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, enable, seconds) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(Self()->SetAutoRefresh(static_cast<uint8_t>(enable), seconds)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 26)(gObj, enable, seconds) & 0xff); });
        Compare(ctx, "SetAutoRefresh", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_GetFileNameFromKey, 0x9a2fb0)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a2fb0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[35] = Addr(&cGZDBSegmentDirectoryFiles::GetFileNameFromKey);
    std::mt19937 rng(0xd1f0d);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        if (rng() % 2 == 0) key.group = s.segOnce;
        uint32_t ext = rng();
        gExtOk = rng() % 3 != 0;
        gAcRet = rng() % 2 == 0;
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, &key, &gStrs[2], ext) & 0xff); });
        auto b = Run(s, t, [&] {
            return static_cast<uint64_t>(Self()->GetFileNameFromKey(&key, reinterpret_cast<cIGZString *>(&gStrs[2]), ext));
        });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 35)(gObj, &key, &gStrs[2], ext) & 0xff); });
        Compare(ctx, "GetFileNameFromKey", i, a, b, c);
    }
}

// ---- subobject slots ----

DIFF_TEST(cGZDBSegmentDirectoryFiles_Close, 0x99f980)
{
    InitFakes(ctx);
    shape::StandIn scan(0x1c49eb, &DirectoryScanAdapters::Destruct, &FakeScanDtor); // the image's and ours (lane LY)
    using Fn = bool(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x99f980);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[9] = Addr(&DirectorySegmentView::Close);
    std::mt19937 rng(0xd1f0e);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        // Close calls slot 15 of every open record without a null check: no null records here.
        for (auto &f : s.files) {
            std::vector<int> kept;
            for (int r : f.recs) if (r >= 0) kept.push_back(r);
            f.recs = kept;
        }
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub()) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(reinterpret_cast<DirectorySegmentView *>(Sub())->Close()); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 9)(Sub()) & 0xff); });
        Compare(ctx, "Close", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_TestForRecord, 0x9a0bf0)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x9a0bf0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[35] = Addr(&DirectorySegmentView::TestForRecord);
    std::mt19937 rng(0xd1f0f);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), &key)); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->TestForRecord(&key)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 35)(Sub(), &key)); });
        Compare(ctx, "TestForRecord", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_OpenRecord, 0x9a08e0)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *, void **, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a08e0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[37] = Addr(&DirectorySegmentView::OpenRecord);
    std::mt19937 rng(0xd1f10);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        uint32_t mode = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 8);
        gRet32 = static_cast<uint8_t>(rng() % 2);
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        void **out = reinterpret_cast<void **>(&gOutSlot[1]);
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), &key, out, mode) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->OpenRecord(&key, out, mode)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 37)(Sub(), &key, out, mode) & 0xff); });
        Compare(ctx, "OpenRecord", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_CreateNewRecord, 0x99ff20)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *, void **);
    auto orig = ctx.Original<Fn>(0x99ff20);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[38] = Addr(&DirectorySegmentView::CreateNewRecord);
    std::mt19937 rng(0xd1f11);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        DirResourceKey key = PickLookup(rng, s);
        if (!s.probes.empty() && rng() % 2 == 0) key.instance = s.probes[rng() % s.probes.size()].first;
        gRet32 = static_cast<uint8_t>(rng() % 2);
        void **out = reinterpret_cast<void **>(&gOutSlot[2]);
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), &key, out) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->CreateNewRecord(&key, out)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 38)(Sub(), &key, out) & 0xff); });
        Compare(ctx, "CreateNewRecord", i, a, b, c);
    }
}

namespace {

// A face to close or abort: an open record's face (mostly), another record's, or null.
const void *PickFace(std::mt19937 &rng, const Spec &s)
{
    std::vector<int> open;
    for (auto &f : s.files) for (int r : f.recs) if (r >= 0) open.push_back(r);
    if (!open.empty() && rng() % 4 != 0) return reinterpret_cast<const uint8_t *>(&gRecs[open[rng() % open.size()]]) + 8;
    int r = static_cast<int>(rng() % kRecords);
    // A record that is not open still needs its face and key set up.
    gRecs[r].vtable = gRecTable;
    gRecs[r].faceVt = gFaceTable;
    gRecs[r].id = static_cast<uint32_t>(r);
    gRecs[r].key = PickLookup(rng, s);
    return reinterpret_cast<const uint8_t *>(&gRecs[r]) + 8;
}

template <typename Member> void RunFinish(difftest::Context &ctx, uint32_t rva, int slot, Member member, uint32_t seed,
                                          const char *name)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, const void *);
    auto orig = ctx.Original<Fn>(rva);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[slot] = Addr(member);
    std::mt19937 rng(seed);
    for (int i = 0; i < kTrials; ++i) {
        Spec s = MakeSpec(rng);
        // Face keys are the file keys; sometimes a resource-0 lookup (FindEntry's instance matching) is asked for.
        Build(s, t); // sets up the records of the spec, so PickFace can use them
        Teardown();
        const void *face = PickFace(rng, s);
        gZeroRec = -1;
        if (rng() % 5 == 0) gZeroRec = static_cast<int>((static_cast<const uint8_t *>(face) - 8 - reinterpret_cast<const uint8_t *>(gRecs)) / sizeof(FakeRecord));
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), face) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>((view->*member)(const_cast<void *>(face))); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), slot)(Sub(), face) & 0xff); });
        Compare(ctx, name, i, a, b, c);
    }
    gZeroRec = -1;
}

} // namespace

DIFF_TEST(cGZDBSegmentDirectoryFiles_CloseRecord, 0x99fe80)
{
    RunFinish(ctx, 0x99fe80, 39, &DirectorySegmentView::CloseRecord, 0xd1f12, "CloseRecord");
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_AbortRecord, 0x99fde0)
{
    RunFinish(ctx, 0x99fde0, 40, &DirectorySegmentView::AbortRecord, 0xd1f13, "AbortRecord");
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_GetResourceKeyList, 0x9a0810)
{
    InitFakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *, void *, void *);
    auto orig = ctx.Original<Fn>(0x9a0810);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[45] = Addr(&DirectorySegmentView::GetResourceKeyList);
    ServiceScope service(ctx);
    std::mt19937 rng(0xd1f14);
    for (int i = 0; i < 400; ++i) {
        Spec s = MakeSpec(rng);
        bool withOut = rng() % 4 != 0;
        bool withFilter = rng() % 2 == 0;
        size_t reserve = rng() % 3 == 0 ? rng() % 6 : 0;
        size_t prefill = rng() % 3;
        gFilterSalt = rng();
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        auto call = [&](auto invoke) {
            DirKeyVector v;
            v.reserve(reserve);
            for (size_t k = 0; k < prefill; ++k) v.push_back(DirResourceKey{9, 9, static_cast<uint32_t>(k), 9});
            uint64_t n = invoke(withOut ? &v : nullptr, withFilter ? static_cast<void *>(&gFilter) : nullptr);
            // The vector's contents and capacity go into the log.
            gLog.push_back(static_cast<int64_t>(v.size()));
            gLog.push_back(static_cast<int64_t>(v.capacity()));
            for (auto &k : v) PushKey(&k);
            return n;
        };
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return call([&](void *o, void *f) { return static_cast<uint64_t>(orig(Sub(), o, f)); }); });
        auto b = Run(s, t, [&] {
            return call([&](void *o, void *f) {
                return static_cast<uint64_t>(view->GetResourceKeyList(static_cast<DirKeyVector *>(o), f));
            });
        });
        auto c = Run(s, routed, [&] {
            return call([&](void *o, void *f) { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 45)(Sub(), o, f)); });
        });
        Compare(ctx, "GetResourceKeyList", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_DeleteSelf, 0x9a0ee0)
{
    InitFakes(ctx);
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a0ee0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[34] = Addr(&DirectorySegmentView::DeleteSelf);
    std::mt19937 rng(0xd1f15);
    for (int i = 0; i < 50; ++i) {
        Spec s = MakeSpec(rng);
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] {
            orig(Sub());
            return 0;
        });
        auto b = Run(s, t, [&] {
            view->DeleteSelf();
            return 0;
        });
        auto c = Run(s, routed, [&] {
            VirtualSlot<Fn>(Sub(), 34)(Sub());
            return 0;
        });
        Compare(ctx, "DeleteSelf", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SetPathThunk, 0x1d4505)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, void *);
    auto orig = ctx.Original<Fn>(0x1d4505);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[12] = Addr(&DirectorySegmentView::SetPathThunk);
    // The argument is a string; Vf9 asks its slot 8 (empty?) and gives it to the embedded path's slot 16. Here the
    // path's vtable is the game's cRZString one, so the path copies through it; the fake string answers slot 8.
    std::mt19937 rng(0xd1f16);
    for (int i = 0; i < 100; ++i) {
        Spec s = MakeSpec(rng);
        s.state &= ~3u; // otherwise nothing happens
        if (rng() % 3 == 0) s.state |= 1;
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), &gStrs[3]) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->SetPathThunk(&gStrs[3])); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 12)(Sub(), &gStrs[3]) & 0xff); });
        Compare(ctx, "SetPathThunk", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SubQueryInterface, 0x9a2687)
{
    InitFakes(ctx);
    using Fn = bool(__thiscall *)(void *, uint32_t, void **);
    auto orig = ctx.Original<Fn>(0x9a2687);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[0] = Addr(&DirectorySegmentView::QueryInterfaceThunk);
    static const uint32_t ids[] = {cGZDBSegmentDirectoryFiles::kIdSelf, 1, 0x656b8f0c, 0x856c8cf6, 0};
    std::mt19937 rng(0xd1f17);
    for (int i = 0; i < 100; ++i) {
        Spec s = MakeSpec(rng);
        uint32_t id = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : ids[rng() % 5];
        void **out = reinterpret_cast<void **>(&gOutSlot[0]);
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto pack = [&](bool r) { return (static_cast<uint64_t>(r) << 40) ^ static_cast<uint64_t>(Id(*out)); };
        auto a = Run(s, t, [&] { *out = nullptr; return pack(orig(Sub(), id, out)); });
        auto b = Run(s, t, [&] { *out = nullptr; return pack(view->QueryInterfaceThunk(id, out)); });
        auto c = Run(s, routed, [&] { *out = nullptr; return pack(VirtualSlot<Fn>(Sub(), 0)(Sub(), id, out)); });
        Compare(ctx, "SubQueryInterface", i, a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentDirectoryFiles_SubRelease, 0x9a27d5)
{
    InitFakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a27d5);
    Tables t = MakeTables(ctx);
    t.sub[34] = reinterpret_cast<void *>(&FakeClose9); // the deleting slot: recorded, nothing deleted
    Tables routed = t;
    routed.sub[2] = Addr(&DirectorySegmentView::ReleaseThunk);
    std::mt19937 rng(0xd1f18);
    for (int i = 0; i < 100; ++i) {
        Spec s = MakeSpec(rng);
        static const uint32_t counts[] = {1, 2, 3, 0, 0x80000000};
        uint32_t count = counts[rng() % 5];
        std::memcpy(s.fill.data() + 8, &count, 4);
        auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub())); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->ReleaseThunk()); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 2)(Sub())); });
        Compare(ctx, "SubRelease", i, a, b, c);
    }
}

// ---- opening and file names (lane NP) ----

namespace {

enum KindNP : int64_t {
    kText5 = 100, kText6, kText7, kExt4, kNs4, kAc3, kAc4, kSlot19, kSlot33, kFwService, kRegister, kListenerRelease,
    kDirExists,
};

// A cIGZString fake for the names: slots 5 (ToChar), 6 (Data), 7 (Strlen) record and answer from `text`.
struct FakeText {
    const void *vtable;
    std::string text;
};
FakeText gTexts[3];
void *gTextTable[8];
FakeObj gFramework, gListener;
void *gFwTable[6], *gListenerTable[8];

int64_t TextId(const void *p)
{
    for (int i = 0; i < 3; ++i)
        if (p == &gTexts[i]) return 0x60000 + i;
    return Id(p);
}

const char *__fastcall FakeToChar(FakeText *self, void *)
{
    Push(kText5, TextId(self));
    return self->text.c_str();
}
const char *__fastcall FakeData(FakeText *self, void *)
{
    Push(kText6, TextId(self));
    return self->text.c_str();
}
uint32_t __fastcall FakeStrlen(FakeText *self, void *)
{
    Push(kText7, TextId(self));
    return static_cast<uint32_t>(self->text.size());
}

uint32_t gExt4Type = 0;
bool __fastcall FakeExt4(void *self, void *, const void *extension, uint32_t *type)
{
    Push(kExt4, Id(self), TextId(extension));
    *type = gExt4Type;
    return gExtOk;
}
void __fastcall FakeNs4(void *self, void *, DirResourceKey *in, DirResourceKey *out)
{
    Push(kNs4, Id(self), in == out);
    PushKey(in);
    out->type ^= 0x5500;
    out->instance += 3;
}
bool __fastcall FakeAc3(void *self, void *, const void *name, void *segment)
{
    Push(kAc3, Id(self), TextId(name));
    gLog.push_back(Id(segment));
    return gAcRet;
}
bool __fastcall FakeAc4(void *self, void *, const DirFileName *path, DirResourceKey *out, void *segment)
{
    Push(kAc4, Id(self), Id(segment));
    gLog.push_back(Rva(path->vtable));
    gLog.push_back(path->refCount);
    PushText(path->text.data(), path->text.size());
    if (gAcRet) *out = DirResourceKey{0xa1, 0xa2, 0xa3, out->resource};
    return gAcRet;
}
bool gRet19 = false;
bool __fastcall FakeSlot19(void *self, void *, const DirFileName *name, const DirFileName *ext, DirResourceKey *key)
{
    Push(kSlot19, Id(self));
    gLog.push_back(Rva(name->vtable));
    gLog.push_back(name->refCount);
    PushText(name->text.data(), name->text.size());
    gLog.push_back(Rva(ext->vtable));
    gLog.push_back(ext->refCount);
    PushText(ext->text.data(), ext->text.size());
    PushKey(key);
    return gRet19;
}
bool gRet33 = false;
bool __fastcall FakeSlot33(void *self, void *)
{
    // The state and whether the scan object exists when the key list is built.
    Push(kSlot33, Id(self), *reinterpret_cast<uint32_t *>(gObj + 0x44));
    gLog.push_back(Self()->mScan != nullptr);
    return gRet33;
}
bool gListenerOn = false, gRegisterRet = false;
void __fastcall FakeFwService(void *, void *, uint32_t a, uint32_t b, void **out)
{
    Push(kFwService, a, b);
    *out = gListenerOn ? &gListener : nullptr;
}
bool __fastcall FakeRegister(void *self, void *, const char *name, void *fn, void *context)
{
    Push(kRegister, self == &gListener, Rva(name));
    gLog.push_back(Rva(fn));
    gLog.push_back(Id(context));
    return gRegisterRet;
}
void __fastcall FakeListenerRelease(void *self, void *) { Push(kListenerRelease, self == &gListener); }
bool gDirExists = false;
bool __cdecl FakeDirectoryExists(cRZString *path)
{
    Push(kDirExists, Id(path));
    return gDirExists;
}

void InitFakesNP()
{
    gTextTable[5] = reinterpret_cast<void *>(&FakeToChar);
    gTextTable[6] = reinterpret_cast<void *>(&FakeData);
    gTextTable[7] = reinterpret_cast<void *>(&FakeStrlen);
    for (auto &t : gTexts) t.vtable = gTextTable;
    gExtTable[4] = reinterpret_cast<void *>(&FakeExt4);
    gNsTable[4] = reinterpret_cast<void *>(&FakeNs4);
    gAcTable[3] = reinterpret_cast<void *>(&FakeAc3);
    gAcTable[4] = reinterpret_cast<void *>(&FakeAc4);
    gFwTable[5] = reinterpret_cast<void *>(&FakeFwService);
    gListenerTable[2] = reinterpret_cast<void *>(&FakeListenerRelease);
    gListenerTable[7] = reinterpret_cast<void *>(&FakeRegister);
    gFramework.vtable = gFwTable;
    gListener.vtable = gListenerTable;
}

// File names around the hex rules: 8 or 16 hex digits at the start or the end, fewer, mixed, high characters.
std::string PickFileName(std::mt19937 &rng)
{
    static const char kHex[] = "0123456789abcdefABCDEF";
    static const char kOther[] = "ghxyz_-. \x80\xff";
    std::string s;
    auto hex = [&](uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) s.push_back(kHex[rng() % 22]);
    };
    auto other = [&](uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) s.push_back(kOther[rng() % 11]);
    };
    switch (rng() % 9) {
    case 0: hex(8); break;
    case 1: hex(16); break;
    case 2: hex(8 + rng() % 10); other(rng() % 5); break;
    case 3: other(rng() % 5); hex(8 + rng() % 10); break;
    case 4: other(rng() % 5); hex(rng() % 20); other(rng() % 3); break;
    case 5: hex(rng() % 8); break;
    case 6: other(1 + rng() % 20); break;
    case 7: hex(7 + rng() % 3); other(1); hex(7 + rng() % 10); break;
    default: hex(rng() % 24); break;
    }
    return s;
}

// The game's operator new and sized delete, ours (the original's scan and map blocks are freed by our code and back).
void *__cdecl GameNewNP(size_t n)
{
    return ::operator new(n);
}
void __cdecl GameDeleteNP(void *p, size_t n)
{
    ::operator delete(p, n);
}

} // namespace

// Slot 19 (name, extension, out key): the hex rules (+0x78 leading, +0x79 trailing, +0x7a the name's Crc24), the
// extension map's type (slot 4), the namespace's slot 4, and with +0xac its slot 4 on the joined path (0x1c2732, run
// on both sides). The name and extension are recording fakes; the out key starts filled, so the untouched resource
// word is compared too.
DIFF_TEST(cGZDBSegmentDirectoryFiles_KeyFromFileName, 0x9a15f0)
{
    InitFakes(ctx);
    InitFakesNP();
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    using Fn = bool(__thiscall *)(void *, const void *, const void *, DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x9a15f0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[19] = Addr(&cGZDBSegmentDirectoryFiles::KeyFromFileName);
    std::mt19937 rng(0xd1f30);
    for (int i = 0; i < 2000; ++i) {
        Spec s = MakeSpec(rng);
        gTexts[0].text = PickFileName(rng);
        gTexts[1].text = PickName(rng);
        gExtOk = rng() % 5 != 0;
        gExt4Type = rng();
        gAcRet = rng() % 2 == 0;
        const uint8_t b78 = rng() % 2 ? PickByte(rng) : 0, b79 = rng() % 2 ? PickByte(rng) : 0;
        const uint8_t b7a = rng() % 2 ? PickByte(rng) : 0;
        auto runOne = [&](auto call) {
            Put<uint8_t>(0x78, b78);
            Put<uint8_t>(0x79, b79);
            Put<uint8_t>(0x7a, b7a);
            DirResourceKey out{0x11, 0x22, 0x33, 0x44};
            uint64_t r = call(&out) & 0xff;
            gLog.push_back(r);
            PushKey(&out);
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&](DirResourceKey *out) { return static_cast<uint64_t>(orig(gObj, &gTexts[0], &gTexts[1], out)); }); });
        auto b = Run(s, t, [&] {
            return runOne([&](DirResourceKey *out) {
                return static_cast<uint64_t>(Self()->KeyFromFileName(reinterpret_cast<const cIGZString *>(&gTexts[0]),
                                                                     reinterpret_cast<const cIGZString *>(&gTexts[1]), out));
            });
        });
        auto c = Run(s, routed, [&] {
            return runOne([&](DirResourceKey *out) { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 19)(gObj, &gTexts[0], &gTexts[1], out)); });
        });
        Compare(ctx, "KeyFromFileName", i, a, b, c);
    }
}

// Slot 20 (file name): +0xac's slot 3, or the name without extension (0x1c1ea9) and the extension without its '.'
// (0x1c1e0e), both run on both sides, to slot 19 (a fake that records both strings and the scratch key).
DIFF_TEST(cGZDBSegmentDirectoryFiles_IsResourceFileName, 0x9a1e80)
{
    InitFakes(ctx);
    InitFakesNP();
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    using Fn = bool(__thiscall *)(void *, const void *);
    auto orig = ctx.Original<Fn>(0x9a1e80);
    Tables t = MakeTables(ctx);
    t.primary[19] = reinterpret_cast<void *>(&FakeSlot19);
    Tables routed = t;
    routed.primary[20] = Addr(&cGZDBSegmentDirectoryFiles::IsResourceFileName);
    std::mt19937 rng(0xd1f31);
    static const char *const kDirs[] = {"", "", "C:\\x\\", "a/b/", "..\\"};
    static const char *const kExts[] = {"", ".", ".package", ".x", ".a.b", ".0123456789abcdefXYZ"};
    for (int i = 0; i < 1000; ++i) {
        Spec s = MakeSpec(rng);
        gTexts[2].text = std::string(kDirs[rng() % 5]) + (rng() % 5 ? PickFileName(rng) : std::string()) + kExts[rng() % 6];
        gRet19 = rng() % 2 == 0;
        gAcRet = rng() % 2 == 0;
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(gObj, &gTexts[2]) & 0xff); });
        auto b = Run(s, t, [&] {
            return static_cast<uint64_t>(Self()->IsResourceFileName(reinterpret_cast<const cIGZString *>(&gTexts[2])));
        });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 20)(gObj, &gTexts[2]) & 0xff); });
        Compare(ctx, "IsResourceFileName", i, a, b, c);
    }
}

// Slot 6 (Open: read, write) and the subobject's thunk 0x1d3f3b. DirectoryExists (0x1c4f94 and ours) is a stand-in
// on both sides; the framework (0x10c9378) is a fake or null, its slot 5 hands out a listener fake or null; slot 33
// records the state and whether the scan exists. A scan built here is compared by its directory text and destroyed.
DIFF_TEST(cGZDBSegmentDirectoryFiles_Open, 0x9a1fc0)
{
    InitFakes(ctx);
    InitFakesNP();
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    shape::StandIn exists(0x1c4f94, &as2::DirectoryExists, &FakeDirectoryExists);
    void **framework = ctx.image.At<void *>(0x10c9378);
    void *savedFramework = *framework;
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a1fc0);
    Tables t = MakeTables(ctx);
    t.primary[33] = reinterpret_cast<void *>(&FakeSlot33);
    Tables routed = t;
    routed.primary[6] = Addr(&cGZDBSegmentDirectoryFiles::Open);
    Tables thunk = t;
    thunk.sub[5] = Addr(&DirectorySegmentView::OpenThunk);
    std::mt19937 rng(0xd1f32);
    for (int i = 0; i < 1500; ++i) {
        Spec s = MakeSpec(rng);
        static const uint32_t states[] = {0, 0, 0, 0, 1, 2, 4};
        s.state = states[rng() % 7];
        if (rng() % 3 == 0) s.flag3C = 1;
        if (rng() % 3 == 0) s.path.clear();
        static const uint32_t args[] = {0, 0, 1, 1, 0x80, 0x100, 0x1ff};
        const uint32_t read = args[rng() % 7], write = args[rng() % 7];
        const uint8_t b3d = rng() % 2 ? PickByte(rng) : 0;
        gDirExists = rng() % 2 == 0;
        gRet33 = rng() % 3 != 0;
        gTimerRunning = rng() % 3 == 0;
        gListenerOn = rng() % 2 == 0;
        gRegisterRet = rng() % 3 != 0;
        void *fw = rng() % 4 == 0 ? nullptr : &gFramework;
        auto runOne = [&](auto call) {
            Put<uint8_t>(0x3d, b3d);
            *framework = fw;
            uint64_t r = call() & 0xff;
            *framework = savedFramework;
            gLog.push_back(r);
            void *scan = Self()->mScan;
            if (scan != gScan) {
                // Built here: the directory text (the cRZString at +0xc of the scan), then destroyed.
                auto *dir = reinterpret_cast<DirFileName *>(static_cast<uint8_t *>(scan) + 0xc);
                gLog.push_back(Rva(dir->vtable));
                PushText(dir->text.data(), dir->text.size());
                DirectoryScanAdapters::Destruct(static_cast<DirectoryScan *>(scan), nullptr);
                ::operator delete(scan, 0x5c);
                Self()->mScan = gScan;
            }
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(orig(gObj, read, write)); }); });
        auto b = Run(s, t, [&] {
            return runOne([&] { return static_cast<uint64_t>(Self()->Open(static_cast<uint8_t>(read), static_cast<uint8_t>(write))); });
        });
        auto c = Run(s, routed, [&] { return runOne([&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 6)(gObj, read, write)); }); });
        Compare(ctx, "Open", i, a, b, c);
        auto d = Run(s, thunk, [&] { return runOne([&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 5)(Sub(), read, write)); }); });
        auto e = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(ctx.Original<Fn>(0x1d3f3b)(Sub(), read, write)); }); });
        Compare(ctx, "OpenThunk", i, a, d, e);
    }
}

// Slot 13 (out): a new extension map with one reference, filled from the one at +0x7c (a real multimap built there for
// the call). The new map is compared by its count and entries (each side's own vtable: the original's is the game's,
// ours our class's), then deleted through its slot 8.
DIFF_TEST(cGZDBSegmentDirectoryFiles_CopyExtensionMap, 0x9a1bb0)
{
    InitFakes(ctx);
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    using Fn = bool(__thiscall *)(void *, void **);
    auto orig = ctx.Original<Fn>(0x9a1bb0);
    Tables t = MakeTables(ctx);
    Tables routed = t;
    routed.primary[13] = Addr(&cGZDBSegmentDirectoryFiles::CopyExtensionMap);
    std::mt19937 rng(0xd1f33);
    for (int i = 0; i < 300; ++i) {
        Spec s = MakeSpec(rng);
        std::vector<std::pair<uint32_t, std::string>> entries;
        const uint32_t n = rng() % 6;
        for (uint32_t k = 0; k < n; ++k) entries.emplace_back(kTypes[rng() % 2] + rng() % 3, PickName(rng));
        auto runOne = [&](auto call) {
            uint8_t saved[0xc];
            std::memcpy(saved, gObj + 0x80, sizeof(saved));
            auto *source = new (gObj + 0x84) residext::Map();
            for (auto &e : entries)
                source->emplace(std::piecewise_construct, std::forward_as_tuple(e.first),
                                std::forward_as_tuple(e.second.data(), static_cast<uint32_t>(e.second.size())));
            void *out = &gOutSlot[0];
            uint64_t r = call(&out) & 0xff;
            gLog.push_back(r);
            if (out != &gOutSlot[0]) {
                auto *map = static_cast<uint8_t *>(out);
                gLog.push_back(*reinterpret_cast<uint32_t *>(map + 4));
                auto *copy = reinterpret_cast<residext::Map *>(map + 8);
                gLog.push_back(static_cast<int64_t>(copy->size()));
                for (auto &e : *copy) {
                    gLog.push_back(e.first);
                    gLog.push_back(Rva(e.second.s.mVTable));
                    PushText(e.second.s.mString.data(), e.second.s.mString.size());
                }
                VirtualSlot<void(__thiscall *)(void *, uint32_t)>(out, 8)(out, 1);
            }
            std::destroy_at(source);
            std::memcpy(gObj + 0x80, saved, sizeof(saved));
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&](void **out) { return static_cast<uint64_t>(orig(gObj, out)); }); });
        auto b = Run(s, t, [&] { return runOne([&](void **out) { return static_cast<uint64_t>(Self()->CopyExtensionMap(out)); }); });
        auto c = Run(s, routed, [&] { return runOne([&](void **out) { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 13)(gObj, out)); }); });
        Compare(ctx, "CopyExtensionMap", i, a, b, c);
    }
}

// ---- the directory listing (lane NP) ----

namespace {

enum KindNP2 : int64_t {
    kFindFirst = 200, kFindNext, kFindClose, kSlot20, kSlot22, kSlot34, kSlot19Key, kStamp, kCache22, kRead, kWrite,
    kSeek, kRecClose6, kRecAbort7, kRecRelease2, kLogText, kSprintfText,
};

// The directory's listing: FindFirstFileA / FindNextFileA / FindClose (the image's import slots) answer from gFs
// whatever the pattern (logged); the game's wide pointers are cleared so both sides take the ANSI calls.
struct FsEntry {
    std::string name;
    DWORD attributes;
};
std::vector<FsEntry> gFs;
size_t gFsNext = 0;

void FillFind(WIN32_FIND_DATAA *data, const FsEntry &e)
{
    std::memset(data, 0xa5, sizeof(*data));
    data->dwFileAttributes = e.attributes;
    std::memset(data->cFileName, 0, sizeof(data->cFileName));
    std::memcpy(data->cFileName, e.name.c_str(), e.name.size());
}
HANDLE WINAPI FakeFindFirstA(const char *pattern, WIN32_FIND_DATAA *data)
{
    Push(kFindFirst);
    PushText(pattern, std::strlen(pattern));
    if (gFs.empty()) return INVALID_HANDLE_VALUE;
    FillFind(data, gFs[0]);
    gFsNext = 1;
    return reinterpret_cast<HANDLE>(0x700);
}
BOOL WINAPI FakeFindNextA(HANDLE handle, WIN32_FIND_DATAA *data)
{
    Push(kFindNext, reinterpret_cast<intptr_t>(handle));
    if (gFsNext >= gFs.size()) return FALSE;
    FillFind(data, gFs[gFsNext++]);
    return TRUE;
}
BOOL WINAPI FakeFindClose(HANDLE handle)
{
    Push(kFindClose, reinterpret_cast<intptr_t>(handle));
    return TRUE;
}

struct FsScope {
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }
    explicit FsScope(difftest::Context &c) : ctx(c)
    {
        Set(0xd9a1f8, reinterpret_cast<void *>(&FakeFindFirstA));
        Set(dscan::kIatFindNextFileA, reinterpret_cast<void *>(&FakeFindNextA));
        Set(0xd9a1f4, reinterpret_cast<void *>(&FakeFindClose));
        Set(0x10cb618, nullptr);
        Set(dscan::kFindNextFileWPointer, nullptr);
    }
    ~FsScope()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    FsScope(const FsScope &) = delete;
    FsScope &operator=(const FsScope &) = delete;
};

// A listing: files (mixed case, several extensions, ASCII only: the ANSI path converts from the code page) and now and
// then folders, "." and "..".
void PickFs(std::mt19937 &rng)
{
    static const char *const kExts[] = {".package", ".PNG", "", ".x", ".a.b", "."};
    gFs.clear();
    if (rng() % 3 == 0) gFs.push_back({".", FILE_ATTRIBUTE_DIRECTORY});
    if (rng() % 3 == 0) gFs.push_back({"..", FILE_ATTRIBUTE_DIRECTORY});
    const uint32_t n = rng() % 9;
    for (uint32_t i = 0; i < n; ++i) {
        std::string name = PickFileName(rng);
        for (char &c : name)
            if (static_cast<uint8_t>(c) >= 0x80) c = 'Q';
        if (name.empty()) name = "N";
        name += kExts[rng() % 6];
        gFs.push_back({name, rng() % 6 == 0 ? static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY) : static_cast<DWORD>(FILE_ATTRIBUTE_ARCHIVE)});
    }
}

std::string Lowered(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

void PushName(const DirFileName *name)
{
    gLog.push_back(Rva(name->vtable));
    gLog.push_back(name->refCount);
    PushText(name->text.data(), name->text.size());
}

uint32_t gKeepSalt = 0;
bool __fastcall FakeSlot20(void *self, void *, const DirFileName *name)
{
    Push(kSlot20, Id(self));
    PushName(name);
    return (name->text.size() + gKeepSalt) % 4 != 0;
}
uint32_t gCount22 = 0;
std::vector<std::string> gNames22;
uint32_t __fastcall FakeSlot22(void *self, void *, DirFileName **names, uint32_t max)
{
    Push(kSlot22, Id(self), max);
    gLog.push_back(names == nullptr);
    if (names == nullptr) return gCount22;
    uint32_t n = 0;
    for (; n < max && n < gNames22.size(); ++n) {
        PushName(names[n]);
        names[n]->text = gNames22[n]; // what the real slot's FromChar leaves
    }
    return n;
}
bool __fastcall FakeSlot34(void *self, void *, const DirFileName *name, uint32_t added)
{
    Push(kSlot34, Id(self), added & 0xff); // the real slot reads the byte
    PushName(name);
    return (name->text.size() & 1) != 0;
}

// Slot 19 writing a key: from gKey19 (FileChanged), or derived from the name and extension (BuildKeyList).
bool gKeyFromText = false, gRet19Key = false;
DirResourceKey gKey19{};
bool __fastcall FakeSlot19Key(void *self, void *, const DirFileName *name, const DirFileName *ext, DirResourceKey *key)
{
    Push(kSlot19Key, Id(self));
    PushName(name);
    PushName(ext);
    PushKey(key);
    if (gKeyFromText) {
        uint32_t h = 7;
        for (char c : name->text) h = h * 31 + static_cast<uint8_t>(c);
        for (char c : ext->text) h = h * 37 + static_cast<uint8_t>(c);
        if (h % 7 == 0) return false;
        key->type = kTypes[h % 2];
        key->group = kGroups[(h >> 3) % 2];
        key->instance = 1 + (h >> 5) % 6;
        return true;
    }
    if (gRet19Key) {
        key->type = gKey19.type;
        key->group = gKey19.group;
        key->instance = gKey19.instance;
    }
    return gRet19Key;
}

void __fastcall FakeLogText(void *self, void *, const char *text)
{
    Push(kLogText, Id(self));
    PushText(text, std::strlen(text));
}

// cRZString::Sprintf (0x1ad6f7 and ours, the image's cRZString slot 31): records the format and its string arguments
// (one for the missing-directory text, three for the duplicate-key one) and leaves a text made of them.
const uint8_t *gDupFormat = nullptr;
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, const char *a, const char *b, const char *c)
{
    Push(kSprintfText, Id(self), Rva(fmt));
    PushText(a, std::strlen(a));
    std::string text = std::string("[") + a;
    if (reinterpret_cast<const uint8_t *>(fmt) == gDupFormat) {
        PushText(b, std::strlen(b));
        PushText(c, std::strlen(c));
        text += "|";
        text += b;
        text += "|";
        text += c;
    }
    self->mString = text;
    return self;
}

uint32_t gStamp = 0;
uint32_t __cdecl FakeStamp(const void *path)
{
    Push(kStamp, Id(path));
    return gStamp;
}

// The segment cache (+0x14, slot 22 opens a record) and the record it hands out (slots 2, 6, 7, 8 Read, 9 Write, 11).
FakeObj gListCache, gRecordNP;
void *gListCacheTable[23], *gRecordTable[12];
bool gCacheOk[3];
std::vector<uint8_t> gStream;
size_t gStreamPos = 0;
int gReadCalls = 0, gReadFailAt = -1, gWriteCalls = 0, gWriteFailAt = -1;

int64_t CacheId(const void *p)
{
    if (p == &gListCache) return 0x70001;
    if (p == &gRecordNP) return 0x70002;
    return Id(p);
}
bool __fastcall FakeCache22(void *self, void *, const DirResourceKey *key, void **out, uint32_t mode)
{
    Push(kCache22, CacheId(self), mode);
    PushKey(key);
    gLog.push_back(CacheId(*out));
    if (mode < 3 && gCacheOk[mode]) {
        *out = &gRecordNP;
        return true;
    }
    return false;
}
void __fastcall FakeRecRelease2(void *self, void *) { Push(kRecRelease2, CacheId(self)); }
void __fastcall FakeRecClose6(void *self, void *) { Push(kRecClose6, CacheId(self)); }
void __fastcall FakeRecAbort7(void *self, void *) { Push(kRecAbort7, CacheId(self)); }
bool __fastcall FakeRead(void *self, void *, void *data, uint32_t n)
{
    Push(kRead, CacheId(self), n);
    if (gReadCalls++ == gReadFailAt || gStreamPos + n > gStream.size()) return false;
    std::memcpy(data, gStream.data() + gStreamPos, n);
    gStreamPos += n;
    return true;
}
bool __fastcall FakeWrite(void *self, void *, const void *data, uint32_t n)
{
    Push(kWrite, CacheId(self), n);
    PushText(static_cast<const char *>(data), n);
    return gWriteCalls++ != gWriteFailAt;
}
void __fastcall FakeSeek(void *self, void *, uint32_t at) { Push(kSeek, CacheId(self), at); }

void InitFakesNP2(difftest::Context &ctx)
{
    gDupFormat = static_cast<const uint8_t *>(ctx.image.At(0xe5f378));
    gListCacheTable[22] = reinterpret_cast<void *>(&FakeCache22);
    gRecordTable[2] = reinterpret_cast<void *>(&FakeRecRelease2);
    gRecordTable[6] = reinterpret_cast<void *>(&FakeRecClose6);
    gRecordTable[7] = reinterpret_cast<void *>(&FakeRecAbort7);
    gRecordTable[8] = reinterpret_cast<void *>(&FakeRead);
    gRecordTable[9] = reinterpret_cast<void *>(&FakeWrite);
    gRecordTable[11] = reinterpret_cast<void *>(&FakeSeek);
    gListCache.vtable = gListCacheTable;
    gRecordNP.vtable = gRecordTable;
}

void PutWord(uint32_t v)
{
    for (int i = 0; i < 4; ++i) gStream.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A cached key list: the stamp (now and then another), the count (now and then one more than stored), the entries
// (keys colliding with the world's and each other), sometimes cut short.
void PickStream(std::mt19937 &rng)
{
    gStream.clear();
    PutWord(rng() % 4 == 0 ? gStamp + 1 : gStamp);
    const uint32_t count = rng() % 5;
    PutWord(rng() % 6 == 0 ? count + 1 : count);
    for (uint32_t i = 0; i < count; ++i) {
        PutWord(kTypes[rng() % 2]);
        PutWord(kGroups[rng() % 2]);
        PutWord(1 + rng() % 6);
        const uint32_t length = rng() % 3 == 0 ? 16 + rng() % 10 : rng() % 10;
        PutWord(length);
        for (uint32_t k = 0; k < length; ++k) gStream.push_back(static_cast<uint8_t>('a' + rng() % 26));
    }
    if (rng() % 6 == 0) gStream.resize(rng() % (gStream.size() + 1));
}

// A name as the game passes it: a cRZString (the game's vtable).
DirFileName gArgName;

} // namespace

// Slot 34 (file name, added). Removed: the first file without open records whose lowercased name (0x1ad167) equals the
// argument is erased; the world's names get upper-case letters now and then, and the argument is a lowercased name, a
// name as stored, or another. Added: the name without extension (0x1c1ea9) and the extension (0x1c1e0e) minus its first
// character go to slot 19 (a fake that writes one of the world's keys, another key, or fails); a new key gets an entry.
// EnsureOpen's slot 6 is the recording fake.
DIFF_TEST(cGZDBSegmentDirectoryFiles_FileChanged, 0x99ed80)
{
    InitFakes(ctx);
    InitFakesNP();
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    using Fn = bool(__thiscall *)(void *, const void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x99ed80);
    Tables t = MakeTables(ctx);
    t.primary[19] = reinterpret_cast<void *>(&FakeSlot19Key);
    Tables routed = t;
    routed.primary[34] = Addr(&cGZDBSegmentDirectoryFiles::FileChanged);
    gKeyFromText = false;
    gArgName.vtable = gImage + kRZStringVTable;
    std::mt19937 rng(0xd1f34);
    static const char *const kExts[] = {"", ".", ".package", ".x", "x.y", ".0123456789abcdefXYZ"};
    for (int i = 0; i < 2000; ++i) {
        Spec s = MakeSpec(rng);
        for (auto &f : s.files)
            if (rng() % 2 == 0)
                for (char &ch : f.name)
                    if (rng() % 3 == 0) ch = static_cast<char>(ch - 'a' + 'A');
        if (rng() % 3 == 0) s.state = 1;
        const uint32_t added = rng() % 2 ? 0u : (rng() % 4 ? 1u : 0x100u | (rng() % 2));
        std::string arg;
        if (!s.files.empty() && rng() % 3 != 0) {
            const std::string &name = s.files[rng() % s.files.size()].name;
            arg = rng() % 3 ? Lowered(name) : name;
        } else {
            arg = PickName(rng);
        }
        if (rng() % 2 == 0) arg += kExts[rng() % 6];
        gArgName.text = arg;
        gArgName.refCount = rng() % 3;
        gRet19Key = rng() % 4 != 0;
        gKey19 = !s.files.empty() && rng() % 2 ? s.files[rng() % s.files.size()].key : PickKey(rng);
        gOpenRets[0] = rng() % 2 == 0;
        gOpenRets[1] = rng() % 2 == 0;
        auto runOne = [&](auto call) {
            uint64_t r = call() & 0xff;
            gLog.push_back(r);
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(orig(gObj, &gArgName, added)); }); });
        auto b = Run(s, t, [&] {
            return runOne([&] {
                return static_cast<uint64_t>(Self()->FileChanged(reinterpret_cast<const cRZString *>(&gArgName),
                                                                 static_cast<uint8_t>(added)));
            });
        });
        auto c = Run(s, routed, [&] { return runOne([&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 34)(gObj, &gArgName, added)); }); });
        Compare(ctx, "FileChanged", i, a, b, c);
    }
}

// Slot 27 (Refresh) over a scripted listing of the path (a DirectoryScan on both sides: the image's 0x1c48da / 0x1c5bbd
// / 0x1c49eb, ours on ours; both through the faked import slots). Slots 20 (keeps a file or not), 22 (the table's
// names: the count, then names that match listed files in another case, repeat, or don't) and 34 (records each report)
// are fakes, so the table is untouched; the random state at +0xe0 and the timer's start are compared. The names the
// original frees without their destructor (ORIGINAL-BUG) leak on both sides alike.
DIFF_TEST(cGZDBSegmentDirectoryFiles_Refresh, 0x9a00b0)
{
    InitFakes(ctx);
    InitFakesNP();
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    FsScope fs(ctx);
    using Fn = bool(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a00b0);
    Tables t = MakeTables(ctx);
    t.primary[20] = reinterpret_cast<void *>(&FakeSlot20);
    t.primary[22] = reinterpret_cast<void *>(&FakeSlot22);
    t.primary[34] = reinterpret_cast<void *>(&FakeSlot34);
    Tables routed = t;
    routed.primary[27] = Addr(&cGZDBSegmentDirectoryFiles::Refresh);
    std::mt19937 rng(0xd1f35);
    for (int i = 0; i < 1500; ++i) {
        Spec s = MakeSpec(rng);
        if (rng() % 4 != 0) s.flag3C = 1;
        if (rng() % 2 == 0) s.path = "C:\\Dir\\";
        PickFs(rng);
        gKeepSalt = rng() % 4;
        gNames22.clear();
        const uint32_t nNames = rng() % 7;
        for (uint32_t k = 0; k < nNames; ++k) {
            if (!gFs.empty() && rng() % 3 != 0) {
                std::string name = gFs[rng() % gFs.size()].name;
                if (rng() % 2 == 0) name = Lowered(name);
                gNames22.push_back(name);
            } else {
                gNames22.push_back(PickName(rng));
            }
        }
        gCount22 = rng() % 5 == 0 ? static_cast<uint32_t>(rng() % 9) : nNames;
        auto runOne = [&](auto call) {
            uint64_t r = call() & 0xff;
            gLog.push_back(r);
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(orig(gObj)); }); });
        auto b = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(Self()->Refresh()); }); });
        auto c = Run(s, routed, [&] { return runOne([&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 27)(gObj)); }); });
        Compare(ctx, "Refresh", i, a, b, c);
    }
}

// Slot 33 (BuildKeyList). DirectoryExists (0x1c4f94 and ours), the volume stamp 0x9a11c0 and cRZString::Sprintf
// (0x1ad6f7 and ours) are stand-ins; the segment cache (+0x14) is a fake or null whose record reads a scripted key
// list (failing at a chosen read now and then) and records every write (failing at a chosen one); the +0x6c scan is a
// real DirectoryScan of the path (built by ours for both sides) over the scripted listing; slot 19 derives a key from
// the name and extension (or refuses), so duplicates occur; slot 30 records the text. The table is compared after.
DIFF_TEST(cGZDBSegmentDirectoryFiles_BuildKeyList, 0x99f360)
{
    InitFakes(ctx);
    InitFakesNP();
    InitFakesNP2(ctx);
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    shape::StandIn exists(0x1c4f94, &as2::DirectoryExists, &FakeDirectoryExists);
    shape::StandIn sprintf(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    shape::CodePatch stamp{0x9a11c0, reinterpret_cast<const void *>(&FakeStamp)};
    FsScope fs(ctx);
    using Fn = bool(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x99f360);
    Tables t = MakeTables(ctx);
    t.primary[19] = reinterpret_cast<void *>(&FakeSlot19Key);
    t.primary[30] = reinterpret_cast<void *>(&FakeLogText);
    Tables routed = t;
    routed.primary[33] = Addr(&cGZDBSegmentDirectoryFiles::BuildKeyList);
    gKeyFromText = true;
    std::mt19937 rng(0xd1f36);
    for (int i = 0; i < 2000; ++i) {
        Spec s = MakeSpec(rng);
        if (rng() % 3 != 0) s.files.clear(); // the table is usually empty when the list is built
        if (rng() % 2 == 0) s.path = "C:\\Dir\\";
        PickFs(rng);
        gDirExists = rng() % 6 != 0;
        gStamp = rng() % 5 == 0 ? 0u : static_cast<uint32_t>(rng() | 1);
        const bool cacheOn = rng() % 4 != 0;
        gCacheOk[1] = rng() % 3 != 0;
        gCacheOk[2] = rng() % 3 != 0;
        PickStream(rng);
        const int readFailAt = rng() % 6 == 0 ? static_cast<int>(rng() % 8) : -1;
        const int writeFailAt = rng() % 5 == 0 ? static_cast<int>(rng() % 10) : -1;
        auto runOne = [&](auto call) {
            Put<const void *>(0x14, cacheOn ? &gListCache : nullptr);
            void *scan = ::operator new(DirectoryScan::kSize);
            DirectoryScanAdapters::Construct(scan, nullptr, reinterpret_cast<const cRZString *>(gObj + 0x4c), false,
                                             false, false);
            Self()->mScan = scan;
            gStreamPos = 0;
            gReadCalls = 0;
            gWriteCalls = 0;
            gReadFailAt = readFailAt;
            gWriteFailAt = writeFailAt;
            uint64_t r = call() & 0xff;
            gLog.push_back(r);
            DirectoryScanAdapters::Destruct(static_cast<DirectoryScan *>(scan), nullptr);
            ::operator delete(scan, DirectoryScan::kSize);
            Self()->mScan = gScan;
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(orig(gObj)); }); });
        auto b = Run(s, t, [&] { return runOne([&] { return static_cast<uint64_t>(Self()->BuildKeyList()); }); });
        auto c = Run(s, routed, [&] { return runOne([&] { return static_cast<uint64_t>(VirtualSlot<Fn>(gObj, 33)(gObj)); }); });
        Compare(ctx, "BuildKeyList", i, a, b, c);
    }
    gKeyFromText = false;
}

// ---- the record functions on the file system (lane NP) ----

namespace {

enum KindNP3 : int64_t {
    kLengthOfPath = 300, kDeletePath, kFileTime, kFileConstruct, kFileOpen, kFileLength, kFileRead, kFileClose,
    kFileDestruct, kRecSize,
};

uint32_t gPathLength = 0, gFileTime = 0, gFileLength = 0, gReadAvail = 0;
uint8_t gDeleteRet = 0;
bool gFileOpenOk = false;
uint32_t gRecSize[kRecords];
uint8_t gBuf[0x50];

// A path as both sides build it: a cRZString (its vtable) holding the text.
void PushPath(int64_t kind, const void *path)
{
    const std::string &text = reinterpret_cast<const cRZString *>(path)->mString;
    gLog.push_back(kind);
    gLog.push_back(Rva(*static_cast<const void *const *>(path)));
    PushText(text.data(), text.size());
}

uint32_t __cdecl StandLengthOfPath(const cIGZString &path)
{
    PushPath(kLengthOfPath, &path);
    return gPathLength;
}
uint8_t __cdecl FakeDeletePath(const void *path)
{
    PushPath(kDeletePath, path);
    return gDeleteRet;
}
uint32_t __cdecl FakeFileTime(const void *path, uint32_t which)
{
    PushPath(kFileTime, path);
    gLog.push_back(which);
    return gFileTime;
}
cRZFile *__fastcall StandFileConstruct(cRZFile *self, void *, const cIGZString &path)
{
    PushPath(kFileConstruct, &path);
    return self;
}
bool __fastcall StandFileOpen(cRZFile *, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    Push(kFileOpen, access, creation);
    gLog.push_back(sharing);
    return gFileOpenOk;
}
uint32_t __fastcall StandFileLength(cRZFile *, void *)
{
    Push(kFileLength);
    return gFileLength;
}
// Reads min(length, what the file has left) bytes, writing each of them, and stores that count in length.
bool __fastcall StandFileRead(cRZFile *, void *, void *dst, uint32_t &length)
{
    Push(kFileRead, length);
    const uint32_t n = length < gReadAvail ? length : gReadAvail;
    for (uint32_t i = 0; i < n; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(0x30 + i);
    length = n;
    return n != 0;
}
bool __fastcall StandFileClose(cRZFile *, void *)
{
    Push(kFileClose);
    return true;
}
void __fastcall StandFileDestruct(cRZFile *, void *) { Push(kFileDestruct); }
uint32_t __fastcall FakeRecSize(FakeRecord *self, void *)
{
    Push(kRecSize, self->id);
    return gRecSize[self->id];
}

// The file system under the record functions: LengthOfPath and the cRZFile members the functions use are stand-ins on
// both sides (the image's and ours); so are the delete (0x1c2e25) and the modification time (0x1c2068), which are ours now
// (PathDelete, PathFileTime); game new/delete go to ours for the path strings.
struct FileStands {
    using ConstructPath = cRZFile *(cRZFile::*)(const cIGZString &);
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    shape::StandIn length{0x1c1b8b, &cRZFile::LengthOfPath, &StandLengthOfPath};
    shape::StandIn construct{0x1c0c07, static_cast<ConstructPath>(&cRZFile::Construct), &StandFileConstruct};
    shape::StandIn open{0x1c285d, &cRZFile::Open, &StandFileOpen};
    shape::StandIn fileLength{0x1c230b, &cRZFile::Length, &StandFileLength};
    shape::StandIn read{0x1c2c97, &cRZFile::ReadWithCount, &StandFileRead};
    shape::StandIn close{0x1c0faa, &cRZFile::Close, &StandFileClose};
    shape::StandIn destruct{0x1c0e6d, &cRZFile::Destruct, &StandFileDestruct};
    shape::StandIn deletePath{0x1c2e25, &PathDelete, &FakeDeletePath};
    shape::StandIn fileTime{0x1c2068, &PathFileTime, &FakeFileTime};
};

// A world for the record functions: no null records (the original reads every open record's access unchecked); each
// record's access (2 and 3 write) and size answer, the file system's answers and EnsureOpen's are picked.
void PickRecords(std::mt19937 &rng, Spec &s)
{
    gRecTable[3] = reinterpret_cast<void *>(&FakeRecSize);
    for (auto &f : s.files) {
        std::vector<int> recs;
        for (int r : f.recs)
            if (r >= 0) recs.push_back(r);
        f.recs = recs;
    }
    static const uint32_t accesses[] = {0, 1, 1, 2, 3, 4};
    static const uint32_t sizes[] = {0, 5, 0x40, 0xffffffff, 0xffffffff};
    for (int r = 0; r < kRecords; ++r) {
        gRecs[r].access = accesses[rng() % 6];
        gRecSize[r] = sizes[rng() % 5];
    }
    static const uint32_t lengths[] = {0, 3, 0x20, 0x40, 0x100, 0xffffffff};
    gPathLength = lengths[rng() % 6];
    gFileLength = lengths[rng() % 5];
    gReadAvail = static_cast<uint32_t>(rng() % 0x48);
    gFileTime = rng() % 4 == 0 ? 0u : static_cast<uint32_t>(rng());
    gDeleteRet = static_cast<uint8_t>(rng() % 2);
    gFileOpenOk = rng() % 4 != 0;
    gOpenRets[0] = rng() % 2 == 0;
    gOpenRets[1] = rng() % 2 == 0;
    if (rng() % 3 != 0) s.flag3C = 1;
}

constexpr int kFsTrials = 1500;

} // namespace

// Sub slot 36 (key): open records' slot 3 (records open for writing only), else the file's length.
DIFF_TEST(cGZDBSegmentDirectoryFiles_GetRecordSize, 0x9a0650)
{
    InitFakes(ctx);
    FileStands stands;
    using Fn = uint32_t(__thiscall *)(void *, const DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x9a0650);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[36] = Addr(&DirectorySegmentView::GetRecordSize);
    auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
    std::mt19937 rng(0xd1f40);
    for (int i = 0; i < kFsTrials; ++i) {
        Spec s = MakeSpec(rng);
        PickRecords(rng, s);
        DirResourceKey key = PickLookup(rng, s);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), &key)); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->GetRecordSize(&key)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 36)(Sub(), &key)); });
        Compare(ctx, "GetRecordSize", i, a, b, c);
    }
}

// Sub slot 41 (key): a file without open records is erased from the table and deleted; its probe goes.
DIFF_TEST(cGZDBSegmentDirectoryFiles_DeleteRecord, 0x99ff60)
{
    InitFakes(ctx);
    FileStands stands;
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *);
    auto orig = ctx.Original<Fn>(0x99ff60);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[41] = Addr(&DirectorySegmentView::DeleteRecord);
    auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
    std::mt19937 rng(0xd1f41);
    for (int i = 0; i < kFsTrials; ++i) {
        Spec s = MakeSpec(rng);
        PickRecords(rng, s);
        if (rng() % 2 == 0)
            for (auto &f : s.files) f.recs.clear();
        DirResourceKey key = PickLookup(rng, s);
        auto a = Run(s, t, [&] { return static_cast<uint64_t>(orig(Sub(), &key) & 0xff); });
        auto b = Run(s, t, [&] { return static_cast<uint64_t>(view->DeleteRecord(&key)); });
        auto c = Run(s, routed, [&] { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 41)(Sub(), &key) & 0xff); });
        Compare(ctx, "DeleteRecord", i, a, b, c);
    }
}

// Sub slot 42 (key, buffer, in/out size): the file's length without a buffer, else its bytes read through a cRZFile.
// The buffer (filled with 0xcc first) and the size are compared after the call.
DIFF_TEST(cGZDBSegmentDirectoryFiles_ReadRecord, 0x9a09b0)
{
    InitFakes(ctx);
    FileStands stands;
    using Fn = int32_t(__thiscall *)(void *, const DirResourceKey *, void *, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a09b0);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[42] = Addr(&DirectorySegmentView::ReadRecord);
    auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
    std::mt19937 rng(0xd1f42);
    for (int i = 0; i < kFsTrials; ++i) {
        Spec s = MakeSpec(rng);
        PickRecords(rng, s);
        DirResourceKey key = PickLookup(rng, s);
        const bool withBuffer = rng() % 4 != 0;
        const uint32_t size0 = rng() % 3 == 0 ? 0x40u : static_cast<uint32_t>(rng() % 0x41);
        auto runOne = [&](auto call) {
            std::memset(gBuf, 0xcc, sizeof(gBuf));
            uint32_t size = size0;
            const uint64_t r = static_cast<uint32_t>(call(withBuffer ? gBuf : nullptr, &size));
            gLog.push_back(size);
            gLog.insert(gLog.end(), gBuf, gBuf + sizeof(gBuf));
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&](void *buf, uint32_t *size) { return orig(Sub(), &key, buf, size); }); });
        auto b = Run(s, t, [&] { return runOne([&](void *buf, uint32_t *size) { return view->ReadRecord(&key, buf, size); }); });
        auto c = Run(s, routed, [&] {
            return runOne([&](void *buf, uint32_t *size) { return VirtualSlot<Fn>(Sub(), 42)(Sub(), &key, buf, size); });
        });
        Compare(ctx, "ReadRecord", i, a, b, c);
    }
}

// Sub slot 47 (key, out stamp): modification time ^ length unless the only open record writes. The out value starts
// as a marker and is compared after the call.
DIFF_TEST(cGZDBSegmentDirectoryFiles_GetRecordStamp, 0x9a0520)
{
    InitFakes(ctx);
    FileStands stands;
    using Fn = bool(__thiscall *)(void *, const DirResourceKey *, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a0520);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[47] = Addr(&DirectorySegmentView::GetRecordStamp);
    auto view = reinterpret_cast<DirectorySegmentView *>(gObj + 4);
    std::mt19937 rng(0xd1f47);
    for (int i = 0; i < kFsTrials; ++i) {
        Spec s = MakeSpec(rng);
        PickRecords(rng, s);
        DirResourceKey key = PickLookup(rng, s);
        auto runOne = [&](auto call) {
            uint32_t out = 0x5a5a5a5a;
            const uint64_t r = call(&out) & 0xff;
            gLog.push_back(out);
            return r;
        };
        auto a = Run(s, t, [&] { return runOne([&](uint32_t *out) { return static_cast<uint64_t>(orig(Sub(), &key, out)); }); });
        auto b = Run(s, t, [&] { return runOne([&](uint32_t *out) { return static_cast<uint64_t>(view->GetRecordStamp(&key, out)); }); });
        auto c = Run(s, routed, [&] {
            return runOne([&](uint32_t *out) { return static_cast<uint64_t>(VirtualSlot<Fn>(Sub(), 47)(Sub(), &key, out)); });
        });
        Compare(ctx, "GetRecordStamp", i, a, b, c);
    }
}

// ---- opening a record (lane NP) ----

namespace {

enum KindNP4 : int64_t { kSlot36 = 400, kNewRecordOpen, kNewRecordQuery, kNewRecordRefs };

uint8_t *gNewRec = nullptr;   // the record the function under test built (the one AddRef'd that isn't a gRecs entry)
int64_t gNewRefs = 0;         // its AddRefs less its Releases
bool gRet36 = false, gNewOpenOk = false, gNewQueryOk = false;
size_t gRecordDumpAt = 0; // where the new record's dwords start in the log (for a failure's detail)
std::string gName36, gExt36;

bool IsTestRecord(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p), b = reinterpret_cast<uintptr_t>(gRecs);
    return a >= b && a < b + sizeof(gRecs);
}

// A dword of the new record as both sides would see it: into the record (its offset), into the image (its RVA), into
// the test object (Id), or the value.
int64_t RecordWord(uint32_t v)
{
    const uintptr_t rec = reinterpret_cast<uintptr_t>(gNewRec), image = reinterpret_cast<uintptr_t>(gImage);
    if (v >= rec && v < rec + 0x188) return 0x1000000 + static_cast<int64_t>(v - rec);
    if (v >= image && v < image + 0x1400000) return 0x2000000 + static_cast<int64_t>(v - image);
    const int64_t id = Id(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)));
    if (id > 0) return 0x3000000 + id;
    return v;
}

// Slot 36 (key, name, extension): the name and the extension the file gets when it is created.
bool __fastcall FakeSlot36(void *self, void *, const DirResourceKey *key, void *name, void *extension)
{
    Push(kSlot36, Id(self));
    PushKey(key);
    auto *n = static_cast<DirFileName *>(name);
    auto *e = static_cast<DirFileName *>(extension);
    gLog.push_back(Rva(n->vtable));
    gLog.push_back(Rva(e->vtable));
    n->text = gName36;
    e->text = gExt36;
    return gRet36;
}

// The new record's AddRef (0x99f2e0) and Release (0x9a2780), counted.
uint32_t __fastcall FakeNewAddRef(uint8_t *self, void *)
{
    if (!IsTestRecord(self)) {
        gNewRec = self;
        ++gNewRefs;
    }
    return 1;
}
uint32_t __fastcall FakeNewRelease(uint8_t *self, void *)
{
    if (self == gNewRec) --gNewRefs;
    return 1;
}
// Its slot 14 (0x9a1c80: key, path, access): records the arguments and the record as built, every dword of it.
bool __fastcall FakeNewOpen(uint8_t *self, void *, const DirResourceKey *key, const void *path, uint32_t access)
{
    gLog.push_back(kNewRecordOpen);
    gLog.push_back(self == gNewRec);
    PushKey(key);
    PushPath(kNewRecordOpen, path);
    gLog.push_back(access);
    gRecordDumpAt = gLog.size();
    // The padding after byte fields is left as the heap gave it on both sides (no constructor writes it), so those
    // dwords are compared in their written bytes only: the streams' byte at +8 (+0x2c, +0x3c: `mov byte [ecx+8]` in
    // cRZOStream / cRZIStream's constructors), the serial record's flag (+0x44: 0x99b840's `mov [ebp+0x40], bl`), the
    // record's +0x48 byte (`mov byte [ebp+0x48], 0` at 0x9a2491), and each cRZFile's two bools at +0x24 (+0x70, +0x10c).
    for (size_t at = 0; at < 0x188; at += 4) {
        uint32_t v = *reinterpret_cast<const uint32_t *>(self + at);
        if (at == 0x2c || at == 0x3c || at == 0x44 || at == 0x48) v &= 0xff;
        if (at == 0x70 || at == 0x10c) v &= 0xffff;
        // Each cRZFile's error word (+0x3c: +0x88, +0x124) holds a code, never a pointer: kErrorNone, 0x20000000, read as
        // into the record when the heap put the record across that address (261111a: #952, ours at 0x1fffff60 logged
        // it as +0xa0, the original's at 0x1ffffdc8 as the code).
        const bool errorWord = at == 0x88 || at == 0x124;
        gLog.push_back(errorWord ? static_cast<int64_t>(v) : RecordWord(v));
    }
    return gNewOpenOk;
}
// Its QueryInterface (0x9a2610): the face (record + 8) into out when it accepts.
bool __fastcall FakeNewQuery(uint8_t *self, void *, uint32_t riid, void **out)
{
    Push(kNewRecordQuery, self == gNewRec, riid);
    gLog.push_back(Id(out));
    if (!gNewQueryOk) return false;
    *out = self + 8;
    ++gNewRefs;
    return true;
}

} // namespace

// Slot 32 (key, out, access). The new record's construction runs for real (the serial record base 0x99b840 is image
// code on both sides; the two cRZFiles are built by the image's constructor and by ours); its AddRef, Release,
// QueryInterface and slot 14 are patched fakes on both sides. Slots 17 and 36 give the name; the out pointer, the open
// lists (the new record by identity) and the record's net reference count are compared after the call.
DIFF_TEST(cGZDBSegmentDirectoryFiles_OpenRecordFile, 0x9a2110)
{
    InitFakes(ctx);
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    shape::CodePatch addRef{0x99f2e0, reinterpret_cast<const void *>(&FakeNewAddRef)};
    shape::CodePatch release{0x9a2780, reinterpret_cast<const void *>(&FakeNewRelease)};
    shape::CodePatch open{0x9a1c80, reinterpret_cast<const void *>(&FakeNewOpen)};
    shape::CodePatch query{0x9a2610, reinterpret_cast<const void *>(&FakeNewQuery)};
    using Fn = uint8_t(__thiscall *)(void *, const DirResourceKey *, void **, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a2110);
    Tables t = MakeTables(ctx);
    t.primary[36] = reinterpret_cast<void *>(&FakeSlot36);
    Tables routed = t;
    routed.primary[32] = Addr(&cGZDBSegmentDirectoryFiles::OpenRecordFile);
    std::mt19937 rng(0xd1f32);
    for (int i = 0; i < 2000; ++i) {
        Spec s = MakeSpec(rng);
        PickRecords(rng, s);
        if (rng() % 3 == 0) {
            s.files.clear(); // an empty table: every key is new
        }
        DirResourceKey key = PickLookup(rng, s);
        static const uint32_t accesses[] = {0, 1, 1, 2, 2, 3, 3, 4};
        const uint32_t access = accesses[rng() % 8];
        gRet17 = rng() % 4 != 0;
        gNameText = PickName(rng);
        gRet36 = rng() % 2 == 0;
        gName36 = PickName(rng);
        gExt36 = rng() % 4 == 0 ? std::string() : PickName(rng);
        gNewOpenOk = rng() % 4 != 0;
        gNewQueryOk = rng() % 4 != 0;
        auto runOne = [&](auto call) {
            gNewRec = nullptr;
            gNewRefs = 0;
            gOutSlot[0] = 0x5a5a5a5a;
            const uint64_t r = call(reinterpret_cast<void **>(&gOutSlot[0])) & 0xff;
            gLog.push_back(kNewRecordRefs);
            gLog.push_back(gNewRefs);
            const auto outValue = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(gOutSlot[0]));
            gLog.push_back(gNewRec != nullptr && outValue == gNewRec + 8 ? 0x7778 : static_cast<int64_t>(gOutSlot[0]));
            for (auto &file : Self()->mFiles) {
                PushKey(&file.first);
                for (auto &rec : file.second.open) gLog.push_back(rec.record == gNewRec ? 0x7777 : Id(rec.record));
            }
            return r;
        };
        size_t dumpAt[3] = {};
        const void *recordAt[3] = {};
        gRecordDumpAt = 0;
        auto a = Run(s, t, [&] { return runOne([&](void **out) { return orig(gObj, &key, out, access); }); });
        dumpAt[0] = std::exchange(gRecordDumpAt, 0);
        recordAt[0] = gNewRec;
        auto b = Run(s, t, [&] {
            return runOne([&](void **out) { return static_cast<uint8_t>(Self()->OpenRecordFile(&key, out, access)); });
        });
        dumpAt[1] = std::exchange(gRecordDumpAt, 0);
        recordAt[1] = gNewRec;
        auto c = Run(s, routed, [&] { return runOne([&](void **out) { return VirtualSlot<Fn>(gObj, 32)(gObj, &key, out, access); }); });
        dumpAt[2] = std::exchange(gRecordDumpAt, 0);
        recordAt[2] = gNewRec;
        Compare(ctx, "OpenRecordFile", i, a, b, c);
        if (a.log != b.log || a.log != c.log)
            ctx.Fail("  the new record's dwords start at value %zu / %zu / %zu; the record at %p / %p / %p", dumpAt[0], dumpAt[1],
                     dumpAt[2], recordAt[0], recordAt[1], recordAt[2]);
    }
}

// ---- construction and destruction (lane NP) ----

namespace {

enum KindNP5 : int64_t { kTimerCtor = 500, kTimerDtor, kRandomCtor, kFrees, kHeapWord, kTableClose };

// Sub slot 9 of the world's own table in the destructor test: logs apart from the image's Close (a stand-in logging
// kClose9), so a destructor that left the world's sub vtable in place would show.
bool __fastcall FakeTableClose9(void *self, void *)
{
    Push(kTableClose, Id(self));
    return true;
}

cRZCallbackTimer *__fastcall StandTimerConstruct(cRZCallbackTimer *self, void *)
{
    Push(kTimerCtor, Id(self));
    return self;
}
void __fastcall StandTimerDestruct(cRZCallbackTimer *self, void *) { Push(kTimerDtor, Id(self)); }
cRZRandom *__fastcall StandRandomConstruct(cRZRandom *self, void *, uint32_t seed)
{
    Push(kRandomCtor, Id(self), seed);
    return self;
}

// A word of the object as both sides would see it: the image's addresses as RVAs, the test's fakes and the object
// itself by Id, anything else as it is.
int64_t ObjectWord(uint32_t v)
{
    const uintptr_t image = reinterpret_cast<uintptr_t>(gImage);
    if (v >= image && v < image + 0x1400000) return 0x2000000 + static_cast<int64_t>(v - image);
    const int64_t id = Id(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)));
    if (id > 0) return 0x3000000 + id;
    return v;
}

uint32_t Word(const uint8_t *o, size_t at)
{
    uint32_t v;
    std::memcpy(&v, o + at, 4);
    return v;
}

// A dumped word: one the call changed from before (the object's state as the call got it) by ObjectWord; one it left
// as it was by ObjectWord without the heap object's range (see gDataWord).
int64_t DumpWord(const uint8_t *o, const uint8_t *before, size_t at)
{
    const uint32_t v = Word(o, at);
    if (v != Word(before, at)) return ObjectWord(v);
    gDataWord = true;
    const int64_t word = ObjectWord(v);
    gDataWord = false;
    return word;
}

// An empty MSVC hash table at o + at (load factor, list head, size, bucket vector, mask, max index): its heap blocks
// by shape (the head linked to itself, 16 buckets that all point at it).
void DumpEmptyHash(std::vector<int64_t> &d, const uint8_t *o, size_t at)
{
    d.push_back(Word(o, at));
    auto *head = reinterpret_cast<void *const *>(static_cast<uintptr_t>(Word(o, at + 4)));
    d.push_back(head[0] == head && head[1] == head);
    d.push_back(Word(o, at + 8));
    auto *begin = reinterpret_cast<void *const *>(static_cast<uintptr_t>(Word(o, at + 0xc)));
    auto *end = reinterpret_cast<void *const *>(static_cast<uintptr_t>(Word(o, at + 0x10)));
    auto *cap = reinterpret_cast<void *const *>(static_cast<uintptr_t>(Word(o, at + 0x14)));
    d.push_back(end - begin);
    d.push_back(cap - begin);
    for (auto *p = begin; p != end && p - begin < 64; ++p) d.push_back(*p == head);
    d.push_back(Word(o, at + 0x18));
    d.push_back(Word(o, at + 0x1c));
}

// The words of a constructed object; its heap blocks (the lock's, the two hash tables', the extension map's tree head)
// by shape.
std::vector<int64_t> ConstructedDump(const uint8_t *o, const uint8_t *before)
{
    std::vector<int64_t> d;
    for (size_t at = 0; at < kObjBytes; at += 4) {
        const bool heap = at == 0x10 || (at >= 0x1c && at < 0x3c) || at == 0x84 || (at >= 0x8c && at < 0xac);
        d.push_back(heap ? kHeapWord : DumpWord(o, before, at));
    }
    d.push_back(Word(o, 0x10) != 0); // the lock's block
    DumpEmptyHash(d, o, 0x1c);
    DumpEmptyHash(d, o, 0x8c);
    auto *tree = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Word(o, 0x84)));
    for (size_t k = 0; k < 3; ++k) d.push_back(Word(tree, k * 4) == Word(o, 0x84)); // left, parent, right: the head
    d.push_back(Word(tree, 0xc) & 0xffff);                                            // colour and the nil flag
    return d;
}

// The words of a destroyed object that are not freed heap pointers (the lock's block, the tables, the path's buffer,
// the extension map's head).
std::vector<int64_t> DestroyedDump(const uint8_t *o, const uint8_t *before)
{
    std::vector<int64_t> d;
    for (size_t at = 0; at < kObjBytes; at += 4) {
        const bool heap = at == 0x10 || (at >= 0x1c && at < 0x3c) || (at >= 0x50 && at < 0x60) || at == 0x84 ||
                          (at >= 0x8c && at < 0xac);
        d.push_back(heap ? kHeapWord : DumpWord(o, before, at));
    }
    return d;
}

// The frees a call made: whether the object was freed, then the sizes of every block freed.
void PushFrees(std::vector<int64_t> &log, const difftest::FreeRecorder &frees, const void *object)
{
    log.push_back(kFrees);
    log.push_back(frees.Freed(object));
    size_t sizes[256];
    const size_t n = frees.SortedSizes(sizes, 256);
    log.push_back(static_cast<int64_t>(n));
    for (size_t k = 0; k < n; ++k) log.push_back(static_cast<int64_t>(sizes[k]));
    log.push_back(frees.Overflowed());
}

} // namespace

// 0x99e4c0 (constructor) on a heap block filled with the same random bytes for both sides. The base's constructor
// (cGZPersistDBSegment, 0x99b790 / ours) and the lock run for real; the timer's constructor (0x9ccf32, it registers the
// timer in a global set) and the cRZRandom's (0x1b8bc8, seeded from the clock) are stand-ins on both sides. Every word
// is compared, heap blocks by shape. Each object is then destroyed by our destructor (tested below).
DIFF_TEST(cGZDBSegmentDirectoryFiles_Construct, 0x99e4c0)
{
    InitFakes(ctx);
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    shape::StandIn timerCtor(0x9ccf32, &cRZCallbackTimer::Construct, &StandTimerConstruct);
    shape::StandIn timerDtor(0x9ccfa9, &cRZCallbackTimer::Destruct, &StandTimerDestruct);
    shape::StandIn random(0x1b8bc8, &cRZRandom::Construct, &StandRandomConstruct);
    using Fn = void *(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x99e4c0);
    std::mt19937 rng(0xd1f4c);
    for (int i = 0; i < 300; ++i) {
        const uint32_t seed = rng();
        auto runOne = [&](auto call) {
            gHeapObj = static_cast<uint8_t *>(::operator new(kObjBytes));
            std::mt19937 fill(seed);
            for (size_t k = 0; k < kObjBytes; ++k) gHeapObj[k] = static_cast<uint8_t>(fill());
            uint8_t before[kObjBytes];
            std::memcpy(before, gHeapObj, kObjBytes);
            gLog.clear();
            Outcome out;
            out.result = static_cast<void *>(call(gHeapObj)) == gHeapObj;
            out.log = gLog;
            out.dump = ConstructedDump(gHeapObj, before);
            reinterpret_cast<cGZDBSegmentDirectoryFiles *>(gHeapObj)->DeletingDestructor(1);
            gHeapObj = nullptr;
            gLog.clear();
            return out;
        };
        auto a = runOne([&](uint8_t *o) { return orig(o); });
        auto b = runOne([&](uint8_t *o) { return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(o)->Construct(); });
        Compare(ctx, "Construct", i, a, b, b);
    }
}

// 0x99ebf0 (deleting destructor, flags) and its subobject thunk 0x99eaeb (sub slot 33), on a world built as for the
// other tests (fake records, timer, cache, namespace and +0xac) plus a real lock at +0xc and a real extension map tree
// (0 to 3 entries); the world is moved to a heap block (MSVC's containers hold no pointers into themselves) so the
// destructor can free it. Shutdown runs for real; the timer's destructor (0x9ccfa9) and the scan's (0x1c49eb) are
// stand-ins on both sides. Compared: the calls, which heap blocks were freed (a FreeRecorder across the call: the object
// itself and the sizes of all), and, when the object is not freed, its words other than freed heap pointers.
DIFF_TEST(cGZDBSegmentDirectoryFiles_DeletingDestructor, 0x99ebf0)
{
    InitFakes(ctx);
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNewNP)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDeleteNP)};
    shape::StandIn scan(0x1c49eb, &DirectoryScanAdapters::Destruct, &FakeScanDtor);
    shape::StandIn timerDtor(0x9ccfa9, &cRZCallbackTimer::Destruct, &StandTimerDestruct);
    // The destructor puts the image's vtables back before Shutdown, which closes an open segment through sub slot 9:
    // Close (0x99f980, ours) is tested on its own, so it is the logging fake here.
    shape::StandIn close(0x99f980, &DirectorySegmentView::Close, &FakeClose9);
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x99ebf0);
    auto origThunk = ctx.Original<Fn>(0x99eaeb);
    Tables t = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[33] = Addr(&DirectorySegmentView::DeletingDestructorThunk);
    t.sub[9] = reinterpret_cast<void *>(&FakeTableClose9);
    routed.sub[9] = reinterpret_cast<void *>(&FakeTableClose9);
    gLog.reserve(1 << 16); // the log's own growth would show among the frees of whichever side ran first
    std::mt19937 rng(0xd1f3d);
    for (int i = 0; i < 1000; ++i) {
        Spec s = MakeSpec(rng);
        const uint32_t flags = rng() % 4;
        const uint32_t extensionSeed = rng();
        auto runOne = [&](const Tables &tables, auto call) {
            Build(s, tables);
            // The path's vtable starts as another table, so the destructor's store of the game's shows.
            Put<const void *>(0x4c, gStrTable);
            reinterpret_cast<cRZCriticalSection *>(gObj + 0xc)->Construct();
            auto *map = new (gObj + 0x84) residext::Map();
            std::mt19937 e(extensionSeed);
            for (uint32_t k = e() % 4; k > 0; --k) {
                const std::string text = PickName(e);
                map->emplace(std::piecewise_construct, std::forward_as_tuple(e() % 3),
                             std::forward_as_tuple(text.data(), static_cast<uint32_t>(text.size())));
            }
            gHeapObj = static_cast<uint8_t *>(::operator new(kObjBytes));
            std::memcpy(gHeapObj, gObj, kObjBytes);
            gLog.clear();
            Outcome out;
            difftest::FreeRecorder frees;
            out.result = static_cast<void *>(call(gHeapObj)) == gHeapObj;
            frees.Stop();
            out.log = gLog;
            PushFrees(out.log, frees, gHeapObj);
            if ((flags & 1) == 0) {
                out.dump = DestroyedDump(gHeapObj, gObj); // (gObj: the state the heap object was copied from)
                ::operator delete(gHeapObj);
            }
            gHeapObj = nullptr;
            gScan = nullptr; // freed by Shutdown, or left (a leak in the test only)
            gLog.clear();
            return out;
        };
        auto a = runOne(t, [&](uint8_t *o) { return orig(o, flags); });
        auto b = runOne(t, [&](uint8_t *o) { return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(o)->DeletingDestructor(flags); });
        auto c = runOne(routed, [&](uint8_t *o) { return VirtualSlot<Fn>(o + 4, 33)(o + 4, flags); });
        Compare(ctx, "DeletingDestructor", i, a, b, c);
        auto d =runOne(t, [&](uint8_t *o) { return origThunk(o + 4, flags); });
        auto e = runOne(t, [&](uint8_t *o) {
            return reinterpret_cast<DirectorySegmentView *>(o + 4)->DeletingDestructorThunk(flags);
        });
        Compare(ctx, "DeletingDestructorThunk", i, a, d, e);
    }
}
