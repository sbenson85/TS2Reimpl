// Differential tests for cTSPersistSpeculativeLoader (src/ts/cTSPersistSpeculativeLoader.*): the persistence system's background
// preloader. The loader is built by hand (the factory is not tested here): lists are our std::list (the game's layout), the
// layers' locks and signal are fake objects with recording vtables, and a request's counted object is one of a table of fake
// objects (QueryInterface, AddRef, Release, Close recorded with the object's id). Nothing starts a thread: the per-item bodies
// (Submit, Process's one step, Update) run directly. A scenario runs the original on world 0 and ours on world 1 from the same
// seed and compares the result, every queue / the pool / mCurrent field by field, the counters, the objects' reference counts
// and the event log of every fake call. Request time fields are compared exactly except where clock() writes them (Preload).
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistSpeculativeLoader.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <new>
#include <random>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
using Loader = cTSPersistSpeculativeLoader;
using Physical = cTSPersistSpeculativeLoaderPhysicalLayer;
using Queue = cTSPersistSpeculativeLoaderQueue;
using Key = cGZPersistResourceKey;
using Request = SpecLoaderRequest;
using List = SpecLoaderList;

enum : uint32_t {
    evLock = 1, evUnlock, evSet, evWait, evReset, evQI, evAddRef, evRelease, evClose, evCancel, evHasKey, evGetResource, evService,
    evStart, evSetName, evThreadWait, evRead, evGetPos, evSetPos, evFind, evSegQI, evSlot21, evOpen, evPackedSize, evPackedFlag,
    evPackedRead, evFindFactory, evCache, evCreate, evSetKey, evPreloadRef, evRefs,
};

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }

// ---- fake critical section and signal (the layers' vtables are called directly) ----
Physical *gUnderTest = nullptr;   // the physical layer whose in-flight request the next unlock cancels (Process)
bool gUnlockClearsCurrent = false;
void *gStreamFace = nullptr;      // what a record's QueryInterface hands out for the stream id
uint32_t __fastcall CsLock(void *, void *)
{
    Ev({evLock});
    return 1;
}
uint32_t __fastcall CsUnlock(void *, void *)
{
    Ev({evUnlock});
    if (gUnlockClearsCurrent && gUnderTest) {
        gUnderTest->mCurrent = nullptr; // Cancel or Shutdown while the worker is unlocked
        gUnlockClearsCurrent = false;
    }
    return 0;
}
uint32_t __fastcall SigSet(void *, void *)
{
    Ev({evSet});
    return 1;
}
uint32_t __fastcall SigWait(void *, void *, int32_t t)
{
    Ev({evWait, uint32_t(t)});
    return 0;
}
uint32_t __fastcall SigReset(void *, void *)
{
    Ev({evReset});
    return 1;
}
void *gCsVT[8], *gSignalVT[8];

// ---- the counted objects requests hold ----
struct FObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t caps; // bit 0: answers the DB record id
};
constexpr unsigned kObjects = 10;
FObj gObjs[kObjects];
void *gObjVT[8];

uint32_t IdOf(const void *p)
{
    if (!p) return 0xffff;
    for (unsigned i = 0; i < kObjects; ++i)
        if (p == &gObjs[i]) return i;
    return 0xfffe;
}
uint32_t __fastcall ObjQI(FObj *o, void *, uint32_t iid, void **out)
{
    Ev({evQI, o->id, iid});
    if (iid == SpecLoader::kDBRecordIid && (o->caps & 1)) {
        ++o->refs;
        *out = o;
        return 0x5a00 | 1;
    }
    if (iid == SpecLoader::kRecordStreamIid && (o->caps & 2)) {
        *out = gStreamFace;
        return 0x5a00 | 1;
    }
    return 0x5a00;
}
uint32_t __fastcall ObjAddRef(FObj *o, void *)
{
    Ev({evAddRef, o->id});
    return uint32_t(++o->refs);
}
uint32_t __fastcall ObjRelease(FObj *o, void *)
{
    Ev({evRelease, o->id});
    return uint32_t(--o->refs);
}
uint32_t __fastcall ObjClose(FObj *o, void *)
{
    Ev({evClose, o->id});
    return 1;
}

// ---- the loader's own vtable: slot 6 (Cancel) is called by GetResource ----
struct Config {
    uint32_t cancelAnswer = 0;
    uint32_t cached = 0; // the resource manager's HasKey answer
    uint32_t getResourceAnswer = 0;
} gCfg;
uint32_t __fastcall LoaderCancel(Loader *, void *, const uint32_t *key)
{
    Ev({evCancel, key[0], key[1], key[2], key[3]});
    return 0x3300 | gCfg.cancelAnswer;
}
uint32_t __fastcall LoaderAddRef(Loader *self, void *)
{
    Ev({evAddRef, 0x1000});
    return ++self->mRefCount;
}
void *gLoaderVT[16];

// ---- the service object (global 0x10c9378) and the resource manager it hands out ----
struct Obj {
    void **vt;
};
void *gServiceVT[8], *gRmVT[48];
Obj gService{gServiceVT}, gRm{gRmVT};

// LogicalLayer::Update's services: factory, created resource, persist system, reference service.
Obj gFactory, gResource, gPersist, gRefs;
void *gFactoryVT[8], *gResourceVT[8], *gPersistVT[8], *gRefsVT[8];
struct UpdCfg {
    uint32_t factoryFound = 1, createOk = 1, refsOk = 1, nKeys = 2, hasRefs = 1;
} gUpd;
uint32_t __fastcall ServiceQuery(Obj *, void *, uint32_t id1, uint32_t id2, void **out)
{
    Ev({evService, id1, id2});
    if (id1 == SpecLoader::kServiceId && id2 == SpecLoader::kPersistSystemIid) *out = &gPersist;
    else if (id1 == SpecLoader::kReferenceServiceId) {
        if (!gUpd.hasRefs) return 0;
        *out = &gRefs;
    } else *out = &gRm;
    return 1;
}
uint32_t __fastcall RmFindFactory(Obj *, void *, const uint32_t *key, void **out)
{
    Ev({evFindFactory, key[0], key[1], key[2], key[3]});
    if (gUpd.factoryFound) *out = &gFactory;
    return 0x4400 | gUpd.factoryFound;
}
uint32_t __fastcall RmCache(Obj *, void *, void *resource)
{
    Ev({evCache, resource == &gResource});
    return 1;
}
uint32_t __fastcall FactoryCreate(Obj *, void *, void *record, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    Ev({evCreate, IdOf(record), iid, a, b});
    if (gUpd.createOk) *out = &gResource;
    return 0x3300 | gUpd.createOk;
}
uint32_t __fastcall FactoryRelease(Obj *, void *)
{
    Ev({evRelease, 0x6000});
    return 0;
}
uint32_t __fastcall ResourceAddRef(Obj *, void *)
{
    Ev({evAddRef, 0x7000});
    return 1;
}
uint32_t __fastcall ResourceRelease(Obj *, void *)
{
    Ev({evRelease, 0x7000});
    return 0;
}
uint32_t __fastcall ResourceSetKey(Obj *, void *, const uint32_t *key)
{
    Ev({evSetKey, key[0], key[1], key[2], key[3]});
    return 0;
}
uint32_t __fastcall PersistRelease(Obj *, void *)
{
    Ev({evRelease, 0x8000});
    return 0;
}
uint32_t __fastcall PersistPreload(Obj *, void *, const uint32_t *key, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Ev({evPreloadRef, key[0], key[1], key[2], key[3], a, b, c, d});
    return 1;
}
uint32_t __fastcall RefsRelease(Obj *, void *)
{
    Ev({evRelease, 0x9000});
    return 0;
}
uint32_t __fastcall RefsList(Obj *, void *, void *resource, ServiceBlockVector *out)
{
    Ev({evRefs, resource == &gResource});
    if (gUpd.refsOk && gUpd.nKeys) {
        auto *keys = static_cast<uint32_t *>(RZMemory::Malloc(16 * gUpd.nKeys));
        for (uint32_t i = 0; i < gUpd.nKeys; ++i) {
            keys[i * 4] = 100 + i;
            keys[i * 4 + 1] = 200 + i;
            keys[i * 4 + 2] = 300 + i;
            keys[i * 4 + 3] = 0;
        }
        out->mFirst = keys;
        out->mLast = keys + 4 * gUpd.nKeys;
        out->mEnd = out->mLast;
    }
    return 0x1100 | gUpd.refsOk;
}
uint32_t __fastcall RmRelease(Obj *, void *)
{
    Ev({evRelease, 0x2000});
    return 0;
}
uint32_t __fastcall RmHasKey(Obj *, void *, const uint32_t *key)
{
    Ev({evHasKey, key[0], key[1], key[2], key[3]});
    return 0x7700 | gCfg.cached;
}
uint32_t __fastcall RmGetResource(Obj *, void *, const uint32_t *key, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    Ev({evGetResource, key[0], key[1], key[2], key[3], riid, a4, a5});
    *out = reinterpret_cast<void *>(0x1234);
    return 0x4400 | gCfg.getResourceAnswer;
}

// ---- the loader's thread object (Init and Shutdown call its slots) ----
struct ThreadConfig {
    uint32_t waitAnswer = 0;
} gThread;
void *gThreadVT[20];
uint32_t __fastcall ThreadStart(void *self, void *, void *fn, void *arg)
{
    Ev({evStart, fn != nullptr, uint32_t(reinterpret_cast<uint8_t *>(arg) - reinterpret_cast<uint8_t *>(self)) /* arg relative to the thread object */});
    return 1;
}
uint32_t __fastcall ThreadSetName(void *, void *, const char *name)
{
    Ev({evSetName, name == GameAddress<const char>(SpecLoader::kThreadNameRva)});
    return 0;
}
uint32_t __fastcall ThreadWait(void *, void *, int32_t t)
{
    Ev({evThreadWait, uint32_t(t)});
    return gThread.waitAnswer;
}

// ---- a record's stream face (QueueReferences) ----
Obj gStreamObj;
void *gStreamVT[60];
std::vector<uint32_t> gScript;
size_t gScriptNext = 0;
uint32_t gReadOrdinal = 0, gFailAt = 0;
uint32_t __fastcall StreamRelease(Obj *, void *)
{
    Ev({evRelease, 0x3000});
    return 0;
}
uint32_t __fastcall StreamGet(Obj *, void *, uint32_t *out)
{
    const uint32_t n = gReadOrdinal++;
    if (gFailAt && n + 1 == gFailAt) {
        Ev({evRead, 0xffffffffu});
        return 0x4400;
    }
    const uint32_t v = gScriptNext < gScript.size() ? gScript[gScriptNext++] : 0u;
    Ev({evRead, n, v});
    *out = v;
    return 0x9900 | 1;
}
uint32_t __fastcall StreamGetPos(Obj *, void *)
{
    Ev({evGetPos});
    return 0x1234;
}
uint32_t __fastcall StreamSetPos(Obj *, void *, uint32_t p)
{
    Ev({evSetPos, p});
    return 0;
}

// ---- Process's segment, packed face and records ----
struct SegCfg {
    uint32_t find[2] = {1, 1};
    uint32_t hasPacked = 1, size = 8, compressedFlag = 0, shortRead = 0, openOk = 1, field50 = 7;
    unsigned findCalls = 0;
} gSeg;
Obj gSegment, gPacked;
void *gSegmentVT[60], *gPackedVT[60];
uint32_t __fastcall RmFindSegment(Obj *, void *, const uint32_t *key, void **out)
{
    Ev({evFind, key[0], key[1], key[2], key[3]});
    const uint32_t ok = gSeg.find[gSeg.findCalls++ ? 1 : 0];
    if (ok) *out = &gSegment;
    return 0x4400 | ok;
}
uint32_t __fastcall SegQI(Obj *, void *, uint32_t iid, void **out)
{
    Ev({evSegQI, iid});
    if (iid == SpecLoader::kPackedSegmentIid && gSeg.hasPacked) {
        *out = &gPacked;
        return 0x2200 | 1;
    }
    return 0x2200;
}
uint32_t __fastcall SegRelease(Obj *, void *)
{
    Ev({evRelease, 0x4000});
    return 0;
}
uint32_t __fastcall SegSlot21(Obj *, void *, const uint32_t *key)
{
    Ev({evSlot21, key[0], key[1], key[2], key[3]});
    return gSeg.field50;
}
uint32_t __fastcall SegOpen(Obj *, void *, const uint32_t *key, void **out, uint32_t flags)
{
    Ev({evOpen, key[0], key[1], key[2], key[3], flags});
    if (gSeg.openOk) {
        gObjs[9].caps |= 2; // this record also answers the stream id
        ++gObjs[9].refs;
        *out = &gObjs[9];
    }
    return 0x6600 | gSeg.openOk;
}
uint32_t __fastcall PackedRelease(Obj *, void *)
{
    Ev({evRelease, 0x5000});
    return 0;
}
uint32_t __fastcall PackedSize(Obj *, void *, const uint32_t *key)
{
    Ev({evPackedSize, key[0], key[1], key[2], key[3]});
    return gSeg.size;
}
uint32_t __fastcall PackedFlag(Obj *, void *, const uint32_t *key)
{
    Ev({evPackedFlag, key[0], key[1], key[2], key[3]});
    return 0x7700 | gSeg.compressedFlag;
}
uint32_t __fastcall PackedRead(Obj *, void *, const uint32_t *key, uint8_t *buf, uint32_t *size)
{
    Ev({evPackedRead, key[0], key[1], key[2], key[3], *size});
    for (uint32_t i = 0; i < *size; ++i) buf[i] = uint8_t(i * 7 + key[2]);
    if (gSeg.shortRead && *size) --*size;
    return 1;
}

void SetFakes()
{
    gStreamVT[2] = reinterpret_cast<void *>(&StreamRelease);
    gStreamVT[13] = reinterpret_cast<void *>(&StreamGet);
    gStreamVT[54] = reinterpret_cast<void *>(&StreamGetPos);
    gStreamVT[55] = reinterpret_cast<void *>(&StreamSetPos);
    gStreamObj.vt = gStreamVT;
    gStreamFace = &gStreamObj;
    gSegmentVT[0] = reinterpret_cast<void *>(&SegQI);
    gSegmentVT[2] = reinterpret_cast<void *>(&SegRelease);
    gSegmentVT[21] = reinterpret_cast<void *>(&SegSlot21);
    gSegmentVT[22] = reinterpret_cast<void *>(&SegOpen);
    gSegment.vt = gSegmentVT;
    gPackedVT[2] = reinterpret_cast<void *>(&PackedRelease);
    gPackedVT[27] = reinterpret_cast<void *>(&PackedSize);
    gPackedVT[28] = reinterpret_cast<void *>(&PackedRead);
    gPackedVT[41] = reinterpret_cast<void *>(&PackedFlag);
    gPacked.vt = gPackedVT;
    gRmVT[34] = reinterpret_cast<void *>(&RmFindSegment);
    gRmVT[23] = reinterpret_cast<void *>(&RmFindFactory);
    gRmVT[8] = reinterpret_cast<void *>(&RmCache);
    gFactoryVT[2] = reinterpret_cast<void *>(&FactoryRelease);
    gFactoryVT[3] = reinterpret_cast<void *>(&FactoryCreate);
    gResourceVT[1] = reinterpret_cast<void *>(&ResourceAddRef);
    gResourceVT[2] = reinterpret_cast<void *>(&ResourceRelease);
    gResourceVT[4] = reinterpret_cast<void *>(&ResourceSetKey);
    gPersistVT[2] = reinterpret_cast<void *>(&PersistRelease);
    gPersistVT[6] = reinterpret_cast<void *>(&PersistPreload);
    gRefsVT[2] = reinterpret_cast<void *>(&RefsRelease);
    gRefsVT[6] = reinterpret_cast<void *>(&RefsList);
    gFactory.vt = gFactoryVT;
    gResource.vt = gResourceVT;
    gPersist.vt = gPersistVT;
    gRefs.vt = gRefsVT;
    gCsVT[1] = reinterpret_cast<void *>(&CsLock);
    gCsVT[2] = reinterpret_cast<void *>(&CsUnlock);
    gSignalVT[1] = reinterpret_cast<void *>(&SigSet);
    gSignalVT[3] = reinterpret_cast<void *>(&SigWait);
    gSignalVT[6] = reinterpret_cast<void *>(&SigReset);
    gObjVT[0] = reinterpret_cast<void *>(&ObjQI);
    gObjVT[1] = reinterpret_cast<void *>(&ObjAddRef);
    gObjVT[2] = reinterpret_cast<void *>(&ObjRelease);
    gObjVT[6] = reinterpret_cast<void *>(&ObjClose);
    gLoaderVT[1] = reinterpret_cast<void *>(&LoaderAddRef);
    gLoaderVT[6] = reinterpret_cast<void *>(&LoaderCancel);
    gServiceVT[5] = reinterpret_cast<void *>(&ServiceQuery);
    gRmVT[2] = reinterpret_cast<void *>(&RmRelease);
    gRmVT[11] = reinterpret_cast<void *>(&RmHasKey);
    gRmVT[5] = reinterpret_cast<void *>(&RmGetResource);
    gService.vt = gServiceVT;
    gRm.vt = gRmVT;
    gThreadVT[2] = reinterpret_cast<void *>(&ThreadStart);
    gThreadVT[8] = reinterpret_cast<void *>(&ThreadWait);
    gThreadVT[12] = reinterpret_cast<void *>(&ThreadSetName);
}

void ResetObjects(uint32_t seed)
{
    for (unsigned i = 0; i < kObjects; ++i) {
        gObjs[i].vt = gObjVT;
        gObjs[i].id = i;
        gObjs[i].refs = 100;
        gObjs[i].caps = ((seed >> i) ^ i) & 1; // some answer the DB record id
    }
}

// The original CRT's clock (0x83a46e) divides by a frequency the harness never initialised: send it to ours.
int __cdecl StubClock()
{
    return static_cast<int>(::clock());
}

// The allocator service FreeBlock (0x195720) frees through (cache 0x10c95d4, guard 0x10c95d8): the general allocator.
struct AllocService {
    void **vt;
} gAlloc;
void *gAllocVT[8];
void *__fastcall AllocAlloc(AllocService *, void *, uint32_t bytes)
{
    return RZMemory::Malloc(bytes);
}
void __fastcall AllocFree(AllocService *, void *, void *block)
{
    if (block) RZMemory::Free(block);
}

struct Globals {
    difftest::Context &ctx;
    uint32_t savedService, savedAlloc, savedGuard;
    uint8_t *clockSite;
    uint8_t savedClock[5];
    bool builtAllocator = false;
    explicit Globals(difftest::Context &c) : ctx(c), clockSite(c.image.At<uint8_t>(0x83a46e))
    {
        // Process allocates the raw record's bytes through the game's allocator (RZMemory::Malloc on both sides).
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
        savedService = *ctx.image.At<uint32_t>(0x10c9378);
        savedAlloc = *ctx.image.At<uint32_t>(0x10c95d4);
        savedGuard = *ctx.image.At<uint32_t>(0x10c95d8);
        gAllocVT[3] = reinterpret_cast<void *>(&AllocAlloc);
        gAllocVT[4] = reinterpret_cast<void *>(&AllocFree);
        gAlloc.vt = gAllocVT;
        *ctx.image.At<void *>(0x10c95d4) = &gAlloc;
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        *ctx.image.At<void *>(0x10c9378) = &gService;
        SetFakes();
        std::memcpy(savedClock, clockSite, 5);
        DWORD old;
        VirtualProtect(clockSite, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubClock) - (reinterpret_cast<uintptr_t>(clockSite) + 5));
        clockSite[0] = 0xE9;
        std::memcpy(clockSite + 1, &rel, 4);
    }
    ~Globals()
    {
        std::memcpy(clockSite, savedClock, 5);
        *ctx.image.At<uint32_t>(0x10c9378) = savedService;
        *ctx.image.At<uint32_t>(0x10c95d4) = savedAlloc;
        *ctx.image.At<uint32_t>(0x10c95d8) = savedGuard;
        if (builtAllocator) {
            auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
            difftest_alloc::ShutdownAndRelease(ga);
        }
    }
};

// ---- the world: a loader built by hand ----
struct World {
    alignas(8) uint8_t raw[sizeof(Loader)];
    Loader *L() { return reinterpret_cast<Loader *>(raw); }
    Physical &P() { return L()->mPhysical; }
    void Build(bool ownerSet = false)
    {
        std::memset(raw, 0xcd, sizeof(raw));
        L()->mVTable = gLoaderVT;
        L()->mRefCount = 1;
        P().mVTable = nullptr;
        P().mOwner = ownerSet ? L() : nullptr;
        P().mSignal.mVTable = gSignalVT;
        P().mSignal.mState = nullptr;
        P().mLock.mVTable = gCsVT;
        P().mLock.mBlock = nullptr;
        P().mQuit = false;
        for (Queue *q : {&P().mPending, &P().mWaiting, &P().mDone}) {
            q->mVTable = nullptr;
            new (&q->mList) List();
        }
        P().mCurrent = nullptr;
        L()->mLogical.mVTable = nullptr;
        L()->mLogical.mOwner = L();
        L()->mLogical.mPhysical = &P();
        L()->mLogical.mPool.mVTable = nullptr;
        new (&L()->mLogical.mPool.mList) List();
        L()->mHits = L()->mExpired = L()->mCancelled = L()->mMisses = 0;
        L()->mThread.mVTable = gThreadVT;
        L()->mThread.mState = nullptr;
    }
    void Destroy()
    {
        for (Queue *q : {&P().mPending, &P().mWaiting, &P().mDone}) q->mList.~List();
        L()->mLogical.mPool.mList.~List();
    }
};

bool gMaskResource = false; // QueueReferences leaves the children's fourth key word uninitialised for version 0 (ORIGINAL-BUG)
bool gCompareTime = true; // false where clock() writes the time (it differs between the two runs)

void DumpRequest(Blob &b, const Request &q, bool withTime = true)
{
    const uint32_t mask = gMaskResource ? 0u : ~0u;
    b.insert(b.end(), {q.key.type, q.key.group, q.key.instance, q.key.resource & mask});
    b.insert(b.end(), {q.parentKey.type, q.parentKey.group, q.parentKey.instance, q.parentKey.resource & mask});
    b.insert(b.end(), {withTime && gCompareTime ? q.time : 0u, q.priority, q.lifetime, q.retain, uint32_t(q.poolOnly), uint32_t(q.followReferences),
                       IdOf(q.object.mObject)});
    if (IdOf(q.object.mObject) == 0xfffe) { // a cPreloadedRecord created by Process or Update: its +4 face; compare its fields and bytes
        const uint8_t *base = static_cast<const uint8_t *>(q.object.mObject) - 4;
        const uint32_t size = *reinterpret_cast<const uint32_t *>(base + 0x4c);
        b.insert(b.end(), {size, *reinterpret_cast<const uint32_t *>(base + 0x50), base[0x55], *reinterpret_cast<const uint32_t *>(base + 0x44)});
        const uint8_t *data = *reinterpret_cast<uint8_t *const *>(base + 0x48);
        for (uint32_t i = 0; data && i < size && i < 64; ++i) b.push_back(data[i]);
    }
}
void DumpList(Blob &b, const List &l)
{
    b.push_back(uint32_t(l.size()));
    for (const Request &q : l) DumpRequest(b, q);
}
void CaptureWorld(Blob &b, World &w, bool withTime = true)
{
    DumpList(b, w.P().mPending.mList);
    DumpList(b, w.P().mWaiting.mList);
    DumpList(b, w.P().mDone.mList);
    DumpList(b, w.L()->mLogical.mPool.mList);
    b.push_back(w.P().mCurrent ? 1u : 0u);
    b.push_back(w.P().mQuit);
    b.insert(b.end(), {w.L()->mHits, w.L()->mExpired, w.L()->mCancelled, w.L()->mMisses});
    for (unsigned i = 0; i < kObjects; ++i) b.push_back(uint32_t(gObjs[i].refs));
    (void)withTime;
}

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x", what, seed, unsigned(i), unsigned(a.size()), unsigned(b.size()),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

// A small key domain so that requests collide.
void RandomKey(std::mt19937 &r, Key &k)
{
    k.type = 1 + r() % 2;
    k.group = 1 + r() % 2;
    k.instance = 1 + r() % 3;
    k.resource = 0;
}
void RandomRequest(std::mt19937 &r, Request &q)
{
    RandomKey(r, q.key);
    if (r() % 3 == 0) {
        std::memset(&q.parentKey, 0, sizeof(q.parentKey));
    } else {
        RandomKey(r, q.parentKey);
    }
    const uint32_t times[] = {0, 40, 100, 1000};
    const uint32_t lifetimes[] = {0, 0, 5, 100, 5000};
    q.time = times[r() % 4];
    q.priority = r() % 4;
    q.lifetime = lifetimes[r() % 5];
    q.retain = r() % 3 == 0 ? 0 : 1 + r() % 4;
    q.poolOnly = r() % 2;
    q.followReferences = r() % 2;
    q.object.mObject = r() % 4 == 0 ? nullptr : &gObjs[r() % kObjects]; // a raw pointer: the table owns the objects
}
void Fill(std::mt19937 &r, List &l, unsigned n, bool sortedByPriority)
{
    std::vector<uint32_t> priorities;
    for (unsigned i = 0; i < n; ++i) priorities.push_back(r() % 4);
    if (sortedByPriority) std::sort(priorities.begin(), priorities.end());
    for (unsigned i = 0; i < n; ++i) {
        Request q;
        RandomRequest(r, q);
        q.priority = priorities[i];
        l.push_back(q); // the list's copy counts the object; q's destructor gives its own count back
    }
}
void FillWorld(std::mt19937 &r, World &w)
{
    Fill(r, w.P().mPending.mList, r() % 4, true);
    Fill(r, w.P().mWaiting.mList, r() % 4, false);
    Fill(r, w.P().mDone.mList, r() % 4, false);
    Fill(r, w.L()->mLogical.mPool.mList, r() % 4, false);
}

constexpr uint32_t kSeeds = 400;

} // namespace

// ---- Queue ----

DIFF_TEST(SpecLoader_Queue_PopBack, 0x1f7de0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            w.Build();
            Fill(r, w.P().mPending.mList, seed % 4, seed % 2);
            alignas(8) uint8_t outRaw[sizeof(Request)];
            Request *out = new (outRaw) Request();
            RandomRequest(r, *out);
            if (seed % 5 == 0 && !w.P().mPending.mList.empty()) out->object.mObject = w.P().mPending.mList.back().object.mObject;
            if (seed % 5 == 1) out->object.mObject = nullptr;
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1f7de0)(&w.P().mPending, out));
            else res = w.P().mPending.PopBack(*out);
            blobs[side].push_back(res);
            DumpRequest(blobs[side], *out);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            out->object.mObject = nullptr; // not released here: counts are compared as they are
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "PopBack", seed);
    }
}

DIFF_TEST(SpecLoader_Queue_Insert, 0x1f8800)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            w.Build();
            Fill(r, w.P().mPending.mList, r() % 5, true);
            Request req;
            RandomRequest(r, req);
            if (seed % 3 == 0 && !w.P().mPending.mList.empty()) { // a duplicate of a queued key
                auto it = w.P().mPending.mList.begin();
                std::advance(it, r() % w.P().mPending.mList.size());
                req.key = it->key;
            }
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1f8800)(&w.P().mPending, &req));
            else res = w.P().mPending.Insert(req);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            req.object.mObject = nullptr;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Insert", seed);
    }
}

DIFF_TEST(SpecLoader_Queue_DeletingDestructor, 0x1f7710)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            // A heap block so the block's own free is exercised (flag 1); the list is destroyed by the call.
            auto *q = static_cast<Queue *>(::operator new(sizeof(Queue)));
            q->mVTable = nullptr;
            new (&q->mList) List();
            Fill(r, q->mList, r() % 5, false);
            const uint32_t flags = seed % 2;
            gEv.clear();
            Queue *res;
            if (side == 0) res = ctx.Original<Queue *(__thiscall *)(void *, uint32_t)>(0x1f7710)(q, flags);
            else res = q->DeletingDestructor(flags);
            blobs[side].push_back(res == q);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            for (unsigned i = 0; i < kObjects; ++i) blobs[side].push_back(uint32_t(gObjs[i].refs));
            if (!flags) ::operator delete(q, sizeof(Queue));
        }
        Same(ctx, blobs[0], blobs[1], "Queue::DeletingDestructor", seed);
    }
}

// ---- Physical layer ----

namespace {
// A world with random queues and sometimes a request in flight (mCurrent points at `current`).
void BuildPhysical(std::mt19937 &r, World &w, Request &current, bool withCurrent)
{
    w.Build(r() % 2);
    FillWorld(r, w);
    if (withCurrent) {
        RandomRequest(r, current);
        w.P().mCurrent = &current;
    }
}
void CaptureCurrent(Blob &b, World &w)
{
    if (w.P().mCurrent) DumpRequest(b, *w.P().mCurrent);
}
} // namespace

DIFF_TEST(SpecLoader_Physical_Submit, 0x1f8650)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            Request current;
            BuildPhysical(r, w, current, seed % 2);
            Request req;
            RandomRequest(r, req);
            // Aim at the in-flight request, a queued done request or a pending one.
            if (seed % 3 == 0 && w.P().mCurrent) req.key = w.P().mCurrent->key;
            else if (seed % 3 == 1 && !w.P().mDone.mList.empty()) req.key = w.P().mDone.mList.front().key;
            else if (seed % 7 == 2 && !w.P().mPending.mList.empty()) req.key = w.P().mPending.mList.back().key;
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1f8650)(&w.P(), &req));
            else res = w.P().Submit(req);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            CaptureCurrent(blobs[side], w);
            req.object.mObject = nullptr;
            current.object.mObject = nullptr;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Submit", seed);
    }
}

DIFF_TEST(SpecLoader_Physical_IsPending, 0x1f81b0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            Request current;
            BuildPhysical(r, w, current, seed % 2);
            Key key;
            RandomKey(r, key);
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1f81b0)(&w.P(), &key));
            else res = w.P().IsPending(key);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            current.object.mObject = nullptr;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "IsPending", seed);
    }
}

DIFF_TEST(SpecLoader_Physical_CompleteParents, 0x1f8330)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            Request current;
            BuildPhysical(r, w, current, 0);
            // Chain the waiting parents: each one's parent key names the next.
            if (!w.P().mWaiting.mList.empty()) {
                auto prev = w.P().mWaiting.mList.begin();
                for (auto it = std::next(prev); it != w.P().mWaiting.mList.end(); ++it, ++prev) prev->parentKey = it->key;
            }
            Request req;
            RandomRequest(r, req);
            if (seed % 2 == 0 && !w.P().mWaiting.mList.empty()) req.parentKey = w.P().mWaiting.mList.front().key;
            if (seed % 5 == 0) std::memset(&req.parentKey, 0, sizeof(req.parentKey));
            gEv.clear();
            if (side == 0) ctx.Original<void(__thiscall *)(void *, const void *)>(0x1f8330)(&w.P(), &req);
            else w.P().CompleteParents(req);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            req.object.mObject = nullptr;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "CompleteParents", seed);
    }
}

// ---- Logical layer ----

DIFF_TEST(SpecLoader_Logical_Cancel, 0x1f7790)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            Request current;
            BuildPhysical(r, w, current, seed % 2);
            if (seed % 9 == 0) w.L()->mLogical.mPhysical = nullptr;
            Key key;
            RandomKey(r, key);
            const auto pick = seed % 6;
            if (pick == 0 && w.P().mCurrent) key = w.P().mCurrent->key;
            if (pick == 1 && !w.P().mPending.mList.empty()) key = w.P().mPending.mList.front().key;
            if (pick == 2 && !w.P().mWaiting.mList.empty()) key = w.P().mWaiting.mList.front().key;
            if (pick == 3 && !w.P().mDone.mList.empty()) key = w.P().mDone.mList.back().key;
            if (pick == 4 && !w.L()->mLogical.mPool.mList.empty()) key = w.L()->mLogical.mPool.mList.back().key;
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1f7790)(&w.L()->mLogical, &key));
            else res = w.L()->mLogical.Cancel(key);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.L()->mLogical.mPhysical = &w.P();
            current.object.mObject = nullptr;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Cancel", seed);
    }
}

// ---- The loader ----

namespace {
// A loader whose pool and queues are random; `bigTimes` keeps the pooled requests' times far from clock()'s value.
void BuildLoader(std::mt19937 &r, World &w, bool poolObjectsNonNull)
{
    w.Build(r() % 2);
    FillWorld(r, w);
    for (Request &q : w.L()->mLogical.mPool.mList) {
        q.time = r() % 2 ? 0u : 0x7f000000u;
        if (poolObjectsNonNull && !q.object.mObject) q.object.mObject = &gObjs[r() % kObjects]; // raw, as the table owns them
    }
    gCfg.cached = r() % 3 == 0;
    gCfg.cancelAnswer = r() % 2;
    gCfg.getResourceAnswer = r() % 2;
}
Key PickKey(std::mt19937 &r, World &w, uint32_t seed)
{
    Key key;
    RandomKey(r, key);
    auto &pool = w.L()->mLogical.mPool.mList;
    if (seed % 3 == 0 && !pool.empty()) {
        auto it = pool.begin();
        std::advance(it, r() % pool.size());
        key = it->key;
    }
    return key;
}
} // namespace

DIFF_TEST(SpecLoader_Loader_Preload, 0x1f8450)
{
    Globals globals(ctx);
    gCompareTime = false; // clock() makes the new request's time differ between the two runs
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            BuildLoader(r, w, false);
            if (seed % 8 == 0) w.L()->mLogical.mPhysical = nullptr;
            const Key key = PickKey(r, w, seed);
            const uint32_t poolOnly = r() % 2, follow = r() % 2, priority = r() % 4, lifetime = r() % 3 ? r() % 200 : 0, retain = r() % 3 ? r() % 5 : 0;
            gEv.clear();
            uint8_t res;
            if (side == 0)
                res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x1f8450)(
                    w.L(), &key, poolOnly, follow, priority, lifetime, retain));
            else res = w.L()->Preload(key, poolOnly != 0, follow != 0, priority, lifetime, retain);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Preload", seed);
    }
    gCompareTime = true;
}

DIFF_TEST(SpecLoader_Loader_IsAvailable, 0x1f80b0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            BuildLoader(r, w, false);
            const Key key = PickKey(r, w, seed);
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1f80b0)(w.L(), &key));
            else res = w.L()->IsAvailable(key);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "IsAvailable", seed);
    }
}

DIFF_TEST(SpecLoader_Loader_GetResource, 0x1f7ee0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            BuildLoader(r, w, true);
            const Key key = PickKey(r, w, seed);
            const uint32_t riid = r() % 2 ? SpecLoader::kDBRecordIid : 0x12345678u;
            void *out = reinterpret_cast<void *>(0xdeadbeef);
            bool fromPool = false;
            std::memset(&fromPool, 0xaa, 1);
            bool *fromPoolPtr = seed % 4 == 0 ? nullptr : &fromPool;
            const uint32_t a4 = r(), a5 = r();
            gEv.clear();
            uint8_t res;
            if (side == 0)
                res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t, void **, uint32_t, uint32_t, bool *)>(0x1f7ee0)(
                    w.L(), &key, riid, &out, a4, a5, fromPoolPtr));
            else res = w.L()->GetResource(key, riid, &out, a4, a5, fromPoolPtr);
            blobs[side].push_back(res);
            blobs[side].push_back(uint32_t(reinterpret_cast<uintptr_t>(out) - reinterpret_cast<uintptr_t>(&gObjs[0])) < sizeof(gObjs) ? 1u : uint32_t(reinterpret_cast<uintptr_t>(out)));
            blobs[side].push_back(*reinterpret_cast<uint8_t *>(&fromPool));
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "GetResource", seed);
    }
}

DIFF_TEST(SpecLoader_Loader_Clear, 0x1f7a40)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            Request current;
            BuildPhysical(r, w, current, seed % 2);
            gEv.clear();
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x1f7a40)(w.L());
            else w.L()->Clear();
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            current.object.mObject = nullptr;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Clear", seed);
    }
}

DIFF_TEST(SpecLoader_Loader_Shutdown, 0x1f8c70)
{
    Globals globals(ctx);
    const uint32_t answers[] = {0, 1, 0x100, 0xffffffffu, 0x80000000u};
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetObjects(seed);
            World w;
            w.Build();
            gThread.waitAnswer = answers[seed % 5];
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f8c70)(w.L()));
            else res = w.L()->Shutdown();
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Shutdown", seed);
    }
}

DIFF_TEST(SpecLoader_Loader_Init, 0x1f8050)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetObjects(seed);
            World w;
            w.Build();
            w.L()->mLogical.mPhysical = nullptr;
            w.L()->mLogical.mOwner = nullptr;
            w.L()->mHits = 5 + seed;
            w.L()->mExpired = 6;
            w.L()->mCancelled = 7;
            w.L()->mMisses = 8;
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f8050)(w.L()));
            else res = w.L()->Init();
            blobs[side].push_back(res);
            blobs[side].push_back(w.L()->mLogical.mPhysical == &w.P());
            blobs[side].push_back(w.L()->mLogical.mOwner == w.L());
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Init", seed);
    }
}

// ---- QueueReferences and Process ----

namespace {
// The key list a record holds: [marker with version] count, then per key: skipped, instance, [instanceHigh], type.
void BuildScript(std::mt19937 &r, uint32_t &version)
{
    gScript.clear();
    gScriptNext = 0;
    gReadOrdinal = 0;
    version = r() % 3;
    const bool marker = version != 0 || r() % 3 == 0;
    if (marker) gScript.push_back(0xffff0000u | version);
    const uint32_t count = r() % 5;
    gScript.push_back(count);
    for (uint32_t i = 0; i < count; ++i) {
        gScript.push_back(r());
        gScript.push_back(1 + r() % 3);
        if (version >= 1) gScript.push_back(r() % 3);
        gScript.push_back(1 + r() % 2);
    }
    gFailAt = r() % 4 == 0 ? 1 + r() % (gScript.size() + 1) : 0;
}
} // namespace

DIFF_TEST(SpecLoader_Physical_QueueReferences, 0x1f89d0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            Request current;
            BuildPhysical(r, w, current, 0);
            uint32_t version;
            BuildScript(r, version);
            gMaskResource = version == 0; // ORIGINAL-BUG: the children's fourth key word is stack garbage then
            Request parent;
            RandomRequest(r, parent);
            parent.object.mObject = nullptr;
            void *record = nullptr;
            if (seed % 5 != 0) {
                record = &gObjs[r() % kObjects];
                if (seed % 7 != 0) static_cast<FObj *>(record)->caps |= 2;
            }
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x1f89d0)(&w.P(), &parent, record));
            else res = w.P().QueueReferences(parent, record);
            blobs[side].push_back(res);
            DumpRequest(blobs[side], parent);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            blobs[side].push_back(gReadOrdinal);
            current.object.mObject = nullptr;
            w.Destroy();
            gMaskResource = false;
        }
        Same(ctx, blobs[0], blobs[1], "QueueReferences", seed);
    }
}

DIFF_TEST(SpecLoader_Logical_Update, 0x1f8d90)
{
    Globals globals(ctx);
    const uint32_t nows[] = {0, 30, 500, 0x7fffffffu};
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            w.Build(r() % 2);
            Fill(r, w.P().mDone.mList, r() % 5, false);
            Fill(r, w.L()->mLogical.mPool.mList, r() % 4, false);
            for (Request &q : w.L()->mLogical.mPool.mList) q.time = r() % 2 ? 0u : 0x7f000000u;
            for (Request &q : w.P().mDone.mList) q.time = r() % 2 ? 0u : 0x7f000000u;
            gUpd = UpdCfg();
            gUpd.factoryFound = r() % 4 != 0;
            gUpd.createOk = r() % 4 != 0;
            gUpd.refsOk = r() % 3 != 0;
            gUpd.nKeys = r() % 4;
            gUpd.hasRefs = r() % 4 != 0;
            w.P().mCurrent = nullptr;
            const uint32_t now = nows[r() % 4];
            if (seed % 11 == 0) w.L()->mLogical.mPhysical = nullptr;
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x1f8d90)(&w.L()->mLogical, now, 0xffffffffu));
            else res = w.L()->mLogical.Update(now, 0xffffffffu);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.L()->mLogical.mPhysical = &w.P();
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "LogicalUpdate", seed);
    }
}

// The frame timer is never started here (the real-time path is not testable: IsStarted / Elapsed read the performance counter in
// the original on both sides), so the budget stays at its default; the logical layer's Update and the timer restart still run.
DIFF_TEST(SpecLoader_Loader_Update, 0x1f8cf0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            w.Build(r() % 2);
            Fill(r, w.L()->mLogical.mPool.mList, r() % 4, false);
            for (Request &q : w.L()->mLogical.mPool.mList) q.time = r() % 2 ? 0u : 0x7f000000u;
            std::memset(&w.L()->mFrameTimer, 0, sizeof(w.L()->mFrameTimer));
            w.L()->mFrameTimer.mMode = 4;
            w.L()->mAverageFrameTime = 33.333332f;
            if (seed % 5 == 0) w.L()->mLogical.mPhysical = nullptr;
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f8cf0)(w.L()));
            else res = w.L()->Update();
            blobs[side].push_back(res);
            uint32_t avg;
            std::memcpy(&avg, &w.L()->mAverageFrameTime, 4);
            blobs[side].push_back(avg);
            blobs[side].push_back(w.L()->mFrameTimer.mStart != 0); // Restart started it
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            w.L()->mLogical.mPhysical = &w.P();
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "LoaderUpdate", seed);
    }
}

DIFF_TEST(SpecLoader_Physical_Process, 0x1f9360)
{
    Globals globals(ctx);
    const uint32_t nows[] = {0, 50, 200, 5000};
    for (uint32_t seed = 0; seed < kSeeds * 5; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            ResetObjects(seed);
            World w;
            w.Build(r() % 2);
            Fill(r, w.P().mPending.mList, r() % 4, true);
            Fill(r, w.P().mWaiting.mList, r() % 3, false);
            Fill(r, w.P().mDone.mList, r() % 2, false);
            w.P().mQuit = r() % 4 == 0;
            gSeg = SegCfg();
            gSeg.find[0] = r() % 4 != 0;
            gSeg.find[1] = r() % 3 != 0;
            gSeg.hasPacked = r() % 4 != 0;
            gSeg.size = r() % 3 == 0 ? 0 : 1 + r() % 20;
            gSeg.compressedFlag = r() % 2;
            gSeg.shortRead = r() % 4 == 0;
            gSeg.openOk = r() % 4 != 0;
            gSeg.field50 = r();
            uint32_t version;
            BuildScript(r, version);
            gMaskResource = true; // children's fourth key word may be garbage (ORIGINAL-BUG in QueueReferences, version 0)
            gUnderTest = &w.P();
            gUnlockClearsCurrent = r() % 5 == 0;
            // Make some pending requests expire, some wait forever.
            const uint32_t now = nows[r() % 4];
            gEv.clear();
            uint8_t res;
            if (side == 0) res = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1f9360)(&w.P(), now));
            else res = w.P().Process(now);
            blobs[side].push_back(res);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            CaptureWorld(blobs[side], w);
            gUnderTest = nullptr;
            gUnlockClearsCurrent = false;
            gMaskResource = false;
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Process", seed);
    }
}
