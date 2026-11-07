#pragma once
// cTSPersistSystem's two routes (lane GD, stage B of the standalone shape; docs/standalone-shape.md, "Tests"). The existing
// tests keep their fake worlds (seeded bytes with the fixture's fake vtables at +0 / +0xa8, or objects the original
// constructor built); only "our side" changes. PS_TEST runs a test's body once per route:
// - route 1 (vtable): the function our class's vtable holds in the slot where the game's vtable of that face holds the
//   original RVA, called with the fixture's face pointer (a slot-order or face-order mistake calls the wrong function);
// - route 2 (adapter): the RVA hook's adapter, with an arity-matched trap in that slot of the vtable the object carries
//   while it runs (whatever the slot held: a fake, the original, nothing), so an adapter that dispatched virtually is caught.
// Non-virtual members (no slot) call their adapter on both routes. Call sites: `psshape::Call(PS(Name), face, args...)`.
#include "difftest.h"
#include "shape.h"
#include "ts/cTSPersistSystem.h"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <set>

#include <windows.h>

namespace psshape {

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
inline const uint8_t *const kWords[6] = {kPrimaryWords, kServiceWords, kUnknownWords, kITSWords, kHooksWords, kTranslationWords};

// Our six vtables, read once from an object our constructor built (then destroyed and freed).
inline void **OurVTable(unsigned face)
{
    static void **tables[6] = {};
    static bool done = false;
    if (!done) {
        void *p = ::operator new(cTSPersistSystem::kSize);
        std::memset(p, 0, cTSPersistSystem::kSize);
        cTSPersistSystem *o = cTSPersistSystemAdapters::Construct(p, nullptr);
        for (unsigned f = 0; f < 6; ++f) tables[f] = shape::VTableAt(p, kFaces[f]);
        cTSPersistSystemAdapters::Destruct(o, nullptr);
        ::operator delete(p);
        done = true;
    }
    return tables[face];
}

// The face and slot whose game vtable holds `rva`; false for a function in no slot (a non-virtual member).
inline bool FindSlot(uint32_t rva, unsigned &face, unsigned &slot)
{
    for (unsigned f = 0; f < 6; ++f) {
        const unsigned s = shape::SlotOf(kGameVTables[f], kSlotCounts[f], rva);
        if (s != ~0u) {
            face = f;
            slot = s;
            return true;
        }
    }
    return false;
}

inline int gRoute = 1;     // 1 our vtable, 2 the adapter
inline int gTrapHits = 0;  // traps hit on route 2
inline int gOnlyRoute = 0; // when set, RunRoutes runs that route only (TSPersistSystemGD_Adapters)
inline std::set<uint32_t> gTrapped; // RVAs whose adapter ran on route 2 guarded: a trap in its slot, or no vtable to dispatch through

inline uint32_t CountTrap(void *, unsigned)
{
    ++gTrapHits;
    return 0;
}

// Whether `table` is memory the test may write a slot of (a fixture's fake table, heap or static data).
inline bool Writable(void **table, unsigned slot)
{
    if (!table) return false;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(table + slot, &mi, sizeof(mi)) || mi.State != MEM_COMMIT) return false;
    const DWORD p = mi.Protect & 0xff;
    return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
}

// Whether `p` points into executable memory (a function: ours, the game image's, a trap).
inline bool IsCode(const void *p)
{
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof(mi)) || mi.State != MEM_COMMIT) return false;
    return (mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Whether `slots` words at `table` can be read (a game vtable; not a seeded world's random vtable word).
inline bool Readable(void **table, unsigned slots)
{
    for (const char *p = reinterpret_cast<const char *>(table), *end = p + slots * sizeof(void *); p < end;) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery(p, &mi, sizeof(mi)) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        p = static_cast<const char *>(mi.BaseAddress) + mi.RegionSize;
    }
    return true;
}

// An argument as the parameter type: a pointer passed for a dword parameter (the key slots take the key's address as a
// dword) goes as its address.
template <class P, class A> P ToParam(A &&a)
{
    if constexpr (std::is_integral_v<P> && std::is_pointer_v<std::decay_t<A>>) return static_cast<P>(reinterpret_cast<uintptr_t>(a));
    else return static_cast<P>(a);
}

// Calls the function under test on the current route. `face` is the fixture's face pointer (the `this` the original takes).
template <class R, class F, class... P, class... A> R Call(uint32_t rva, R(__fastcall *adapter)(F *, void *, P...), void *face, A &&...args)
{
    unsigned f = 0, s = 0;
    const bool slot = FindSlot(rva, f, s);
    if ((gRoute == 1 || !adapter) && slot) { // route 1, or a dropped adapter (PSX)
        using Fn = R(__thiscall *)(void *, P...);
        return reinterpret_cast<Fn>(OurVTable(f)[s])(face, ToParam<P>(args)...);
    }
    // The trap goes in the slot of the vtable the object carries: in place when that is a writable table whose slot holds
    // code or nothing (a fixture's fake table: some fakes know their object by its vtable word; the word is put back
    // afterwards), otherwise in a copy the object carries for the call (a game vtable). A seeded world's random word is no
    // table (its slot holds data, not code): left alone, a dispatch through it faults (data pages don't execute).
    void **&vptr = *static_cast<void ***>(face);
    void *copy[75];
    struct Restore {
        void **&vptr;
        void **table = nullptr;
        unsigned s = 0;
        void *word = nullptr;
        bool inPlace = false, swapped = false;
        ~Restore()
        {
            if (inPlace) table[s] = word;
            if (swapped) vptr = table;
        }
    } restore{vptr};
    if (slot && vptr) {
        void *const trap = shape::TrapFor<&CountTrap>(kWords[f][s]);
        restore.table = vptr;
        restore.s = s;
        const bool table = Readable(vptr, kSlotCounts[f]) && (vptr[s] == nullptr || IsCode(vptr[s]));
        if (table && Writable(vptr, s)) {
            restore.word = vptr[s];
            restore.inPlace = true;
            gTrapped.insert(rva);
            vptr[s] = trap;
        } else if (table) {
            std::memcpy(copy, vptr, kSlotCounts[f] * sizeof(void *));
            copy[s] = trap;
            restore.swapped = true;
            gTrapped.insert(rva);
            vptr = copy;
        } else {
            gTrapped.insert(rva); // a seeded world's random word: a dispatch through it faults
        }
    } else if (slot) {
        gTrapped.insert(rva); // no vtable (a zeroed dummy face): a dispatch through it faults
    }
    return adapter(reinterpret_cast<F *>(face), nullptr, ToParam<P>(args)...);
}

// Runs a test body on both routes; a route's failures are labelled, and trap hits fail it.
inline void RunRoutes(difftest::Context &ctx, void (*body)(difftest::Context &))
{
    // Our vtables are read off an object our constructor builds once per process: built here, before the body's world,
    // fakes and logs, rather than inside the first route-1 call of whichever test runs first (whose log, stand-ins and
    // frees it would otherwise change: the tests failed when run alone).
    (void)OurVTable(0);
    for (gRoute = gOnlyRoute ? gOnlyRoute : 1; gRoute <= (gOnlyRoute ? gOnlyRoute : 2); ++gRoute) {
        const size_t before = ctx.failures.size();
        gTrapHits = 0;
        body(ctx);
        if (gTrapHits) ctx.Fail("%d trap hits: an adapter dispatched through the vtable", gTrapHits);
        if (ctx.failures.size() > before) {
            ctx.Fail("(the failures above are on route %d: %s)", gRoute, gRoute == 1 ? "our vtable" : "the adapter");
            break;
        }
    }
    gRoute = 1;
}

} // namespace psshape

// The original RVAs of the adapters (the hook entries in src/hook/tables/cTSPersistSystem.cpp).
namespace psrva {
constexpr uint32_t GetServicePriority = 0x1e0930;
constexpr uint32_t GetResourceManager = 0x1dae50;
constexpr uint32_t QueueResourceLoad = 0x1e3ae0;
constexpr uint32_t IsResourceAvailable = 0x1e2170;
constexpr uint32_t IsResourcePending = 0x1e2190;
constexpr uint32_t ClearDeferWrites = 0x1dea20;
constexpr uint32_t LockSystem = 0x1dae80;
constexpr uint32_t UnlockSystem = 0x1dea10;
constexpr uint32_t StopBackgroundOpen = 0x1e5e40;
constexpr uint32_t GetResourceSpeculative = 0x1e04e0;
constexpr uint32_t GetGroupIdForPath = 0x1dff90;
constexpr uint32_t DeleteResource = 0x1e2d60;
constexpr uint32_t OpenSegment = 0x1dda20;
constexpr uint32_t FlushSegmentByInfo = 0x1e5dc0;
constexpr uint32_t GetSegmentPath = 0x1dfd40;
constexpr uint32_t RegisterSegmentForKey = 0x1e2700;
constexpr uint32_t GetRecordChangeID = 0x1e0780;
constexpr uint32_t QueryInterface = 0x1e2e50;
constexpr uint32_t vf56 = 0x1e2d50;
constexpr uint32_t GetResourceFromFileRef = 0x1e0800;
constexpr uint32_t GetKeyedObject = 0x1e0410;
constexpr uint32_t vf3 = 0x1e0890;
constexpr uint32_t vf4 = 0x1e08e0;
constexpr uint32_t vf5 = 0x1e04a0;
constexpr uint32_t GetResource = 0x1e08cf;
constexpr uint32_t vf49 = 0x1dfd00;
constexpr uint32_t vf52 = 0x1dd9d0;
constexpr uint32_t ClearSpeculativeLoader = 0x1defd0;
constexpr uint32_t OnIdle = 0x1e2740;
constexpr uint32_t AgeCaches = 0x1def30;
constexpr uint32_t PurgeCaches = 0x1df160;
constexpr uint32_t OpenRecordFromFileRef = 0x1e0680;
constexpr uint32_t CreateRecord = 0x1de1e0;
constexpr uint32_t OpenSegmentFromValues = 0x1dda60;
constexpr uint32_t ScoreSegment = 0x1dfe10;
constexpr uint32_t CommitSegments = 0x1de7b0;
constexpr uint32_t AddListener = 0x1daa20;
constexpr uint32_t RemoveListener = 0x1e39c0;
constexpr uint32_t CommitRecordToFileRef = 0x1dd410;
constexpr uint32_t WriteResourceToFileRef = 0x1e5e70;
constexpr uint32_t ReadResourceFromSegment = 0x1e3030;
constexpr uint32_t GetAnimTable = 0x1df640;
constexpr uint32_t PreAppShutdown = 0x1e2ce0;
constexpr uint32_t OnTick = 0x1e27c0;
constexpr uint32_t SaveResource = 0x1e3b20;
constexpr uint32_t ReopenSegment = 0x1e2430;
constexpr uint32_t GetWritableSegments = 0x1e0980;
constexpr uint32_t RegisterSegment = 0x1e3630;
constexpr uint32_t vf19 = 0x1df9b0;
constexpr uint32_t vf34 = 0x1dea30;
constexpr uint32_t vf15 = 0x1e5670;
constexpr uint32_t GetSegmentsForGroup = 0x1e0140;
constexpr uint32_t SegmentHasGroup = 0x1de910;
constexpr uint32_t FindSegment = 0x1dec80;
constexpr uint32_t ListKeysOfType = 0x1df880;
constexpr uint32_t ListKeys = 0x1df740;
constexpr uint32_t GetPackedFileGroup = 0x1e0550;
constexpr uint32_t ReopenSegmentsForPath = 0x1e2510;
constexpr uint32_t HasSegmentForFile = 0x1df300;
constexpr uint32_t vf53 = 0x1e2f00;
constexpr uint32_t RemoveSegmentsWithPrefix = 0x1dcd80;
constexpr uint32_t SetKeyedObject = 0x1e5200;
constexpr uint32_t vf168_56 = 0x1e3b10;
constexpr uint32_t NoteResourceChange = 0x1e4b40;
constexpr uint32_t StartBackgroundOpen = 0x1e2c40;
constexpr uint32_t GetGroupIdForNameChars = 0x1e02f0;
constexpr uint32_t GetGroupIdForFileName = 0x1e0200;
constexpr uint32_t GetGroupIdForPathChars = 0x1dffb0;
constexpr uint32_t FlushRollbackSegments = 0x1e4c00;
constexpr uint32_t CloseAllSegments = 0x1dcc60;
constexpr uint32_t FindBestSegment = 0x1deb30;
constexpr uint32_t Shutdown = 0x1e53c0;
constexpr uint32_t ReleaseSegmentsForFile = 0x1de2d0;
constexpr uint32_t OpenContentPaths = 0x1dae90;
constexpr uint32_t Init = 0x1e0d60;
constexpr uint32_t AddCategoryPath = 0x1da360;
constexpr uint32_t CollectPathGroups = 0x1da600;
constexpr uint32_t OpenSegmentByInfo = 0x1ddb10;
constexpr uint32_t CopyResource = 0x1dd4f0;
constexpr uint32_t CopySegmentResources = 0x1dd7a0;
constexpr uint32_t IsSegmentWritable = 0x1e1d70;
constexpr uint32_t vf28 = 0x1e36f0;
constexpr uint32_t vf30 = 0x1e57a0;
constexpr uint32_t DeleteGroupFiles = 0x1de470;
constexpr uint32_t CommitSegmentsWithBackup = 0x1dcef0;
constexpr uint32_t AddPackageFile = 0x1e3180;
constexpr uint32_t SetResourceName = 0x1e4de0;
constexpr uint32_t GetResourceName = 0x1e21b0;
constexpr uint32_t GetCachedResource = 0x1dfaa0;
constexpr uint32_t vf39 = 0x1e2850;
} // namespace psrva

// `PS(Name)`: the RVA and the adapter of a function, the first two arguments of psshape::Call.
#define PS(Name) psrva::Name, &cTSPersistSystemAdapters::Name

// The adapters lane GT dropped (only the game's vtables reached their originals, and no object carries those while the
// constructor and destructor hooks are in; the originals are trapped): their signatures only, never defined. `PSX(Name)`
// passes a null adapter of that type, and route 2 then calls our vtable's function as route 1 does.
struct cTSPersistSystemDroppedAdapters {
    static uint8_t __fastcall QueueResourceLoad(cITSPersistSystem *face, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    static uint8_t __fastcall IsResourceAvailable(cITSPersistSystem *face, void *, uint32_t key);
    static uint8_t __fastcall IsResourcePending(cITSPersistSystem *face, void *, uint32_t key);
    static uint32_t __fastcall GetResourceSpeculative(cITSPersistSystem *face, void *, uint32_t a1, uint32_t a2, uint32_t a3, void *a4);
    static uint32_t __fastcall GetGroupIdForPath(cITSPersistSystem *face, void *, void *path);
    static uint8_t __fastcall DeleteResource(cITSPersistSystem *face, void *, uint32_t key);
    static uint32_t __fastcall OpenSegment(cITSPersistSystem *face, void *, uint32_t a1, uint32_t a2, uint32_t a3, void *a4, void *a5, uint32_t a6, uint32_t a7);
    static uint8_t __fastcall FlushSegmentByInfo(cITSPersistSystem *face, void *, uint32_t info);
    static uint8_t __fastcall GetSegmentPath(cITSPersistSystem *face, void *, void *segment, uint32_t path);
    static uint32_t __fastcall RegisterSegmentForKey(cITSPersistSystem *face, void *, uint32_t segment, void *key, uint32_t expected);
    static uint8_t __fastcall GetRecordChangeID(cTSPersistSystem *face, void *, uint32_t key, uint32_t changeId);
    static bool __fastcall QueryInterface(cIGZPersistResourceManager *face, void *, uint32_t riid, void **out);
    static uint32_t __fastcall GetResourceFromFileRef(cITSPersistSystem *face, void *, uint32_t type, void *fileRef, uint32_t instance, uint32_t iid, uint32_t out);
    static uint32_t __fastcall GetKeyedObject(cITSPersistSystem *face, void *, uint32_t key);
    static uint8_t __fastcall vf4(cIGZPersistResourceManager *face, void *, uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
    static uint8_t __fastcall vf5(cIGZPersistResourceManager *face, void *, uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    static uint32_t __fastcall GetResource(cITSPersistSystem *face, void *, const uint32_t *key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5);
    static bool __fastcall OnIdle(cIGZSystemService *face, void *, uint32_t counter);
    static void __fastcall AgeCaches(cITSPersistSystem *face, void *);
    static void __fastcall PurgeCaches(cITSPersistSystem *face, void *);
    static uint8_t __fastcall OpenRecordFromFileRef(cITSPersistSystem *face, void *, void *fileRef, uint32_t type, uint32_t instance, uint32_t iid, uint32_t out, uint32_t *status);
    static uint8_t __fastcall CreateRecord(cITSPersistSystem *face, void *, uint32_t iid, void **out);
    static uint8_t __fastcall OpenSegmentFromValues(cITSPersistSystem *face, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);
    static uint32_t __fastcall ScoreSegment(cITSPersistSystem *face, void *, void *segment, const uint32_t *info);
    static uint8_t __fastcall CommitSegments(cITSPersistSystem *face, void *, void *const *segments);
    static void __fastcall AddListener(cITSPersistSystem *face, void *, void *listener);
    static void __fastcall RemoveListener(cITSPersistSystem *face, void *, void *listener);
    static uint8_t __fastcall CommitRecordToFileRef(cITSPersistSystem *face, void *, void *fileRef, uint32_t record, uint32_t type, uint32_t instance, void *name);
    static uint32_t __fastcall WriteResourceToFileRef(cITSPersistSystem *face, void *, void *resource, void *fileRef, uint32_t instance);
    static uint32_t __fastcall ReadResourceFromSegment(cITSPersistSystem *face, void *, void *resource, void *fileRef, uint32_t instance, uint32_t metadata);
    static uint8_t __fastcall GetAnimTable(cITSPersistSystem *face, void *, const uint32_t *key, uint32_t iid, uint32_t out);
    static bool __fastcall PreAppShutdown(cIGZFrameWorkHooks *face, void *);
    static bool __fastcall OnTick(cIGZSystemService *face, void *, uint32_t counter);
    static uint8_t __fastcall SaveResource(cITSPersistSystem *face, void *, void *resource, void *segment);
    static void __fastcall ReopenSegment(cITSPersistSystem *face, void *, void *segment);
    static void __fastcall GetWritableSegments(cITSPersistSystem *face, void *, void *out);
    static uint8_t __fastcall RegisterSegment(cITSPersistSystem *face, void *, void *segment);
    static uint32_t __fastcall vf19(cIGZPersistResourceManager *face, void *, uint32_t list, uint32_t type);
    static uint8_t __fastcall vf34(cIGZPersistResourceManager *face, void *, uint32_t key, void **out);
    static uint8_t __fastcall vf15(cIGZPersistResourceManager *face, void *, uint32_t key);
    static void __fastcall GetSegmentsForGroup(cITSPersistSystem *face, void *, uint32_t group, void *out);
    static uint8_t __fastcall SegmentHasGroup(cITSPersistSystem *face, void *, void *segment, uint32_t group);
    static uint8_t __fastcall FindSegment(cITSPersistSystem *face, void *, const uint32_t *key, uint32_t iid, void **out);
    static uint32_t __fastcall ListKeysOfType(cITSPersistSystem *face, void *, void *out, uint32_t type);
    static uint32_t __fastcall ListKeys(cITSPersistSystem *face, void *, void *out, uint32_t type, uint32_t value);
    static uint32_t __fastcall GetPackedFileGroup(cITSPersistSystem *face, void *, uint32_t group);
    static void __fastcall ReopenSegmentsForPath(cITSPersistSystem *face, void *, void *path);
    static uint8_t __fastcall HasSegmentForFile(cITSPersistSystem *face, void *, void *name, void *path, uint32_t mode);
    static uint8_t __fastcall vf53(cIGZPersistResourceManager *face, void *, uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);
    static void __fastcall RemoveSegmentsWithPrefix(cTSPersistSystem *face, void *, void *prefix);
    static void __fastcall NoteResourceChange(cITSPersistSystem *face, void *, uint32_t group, uint32_t key, uint32_t a3);
    static void __fastcall StartBackgroundOpen(cITSPersistSystem *face, void *);
    static uint32_t __fastcall GetGroupIdForNameChars(cITSPersistSystem *face, void *, const char *name);
    static uint32_t __fastcall GetGroupIdForFileName(cITSPersistSystem *face, void *, void *segment);
    static uint32_t __fastcall GetGroupIdForPathChars(cITSPersistSystem *face, void *, const char *path);
    static uint8_t __fastcall FlushRollbackSegments(cITSPersistSystem *face, void *, void *const *segments);
    static void __fastcall CloseAllSegments(cITSPersistSystem *face, void *);
    static uint8_t __fastcall FindBestSegment(cITSPersistSystem *face, void *, const uint32_t *key, uint32_t iid, void **out);
    static bool __fastcall Shutdown(cIGZSystemService *face, void *);
    static uint8_t __fastcall ReleaseSegmentsForFile(cITSPersistSystem *face, void *, void *path, uint32_t flag);
    static uint8_t __fastcall OpenContentPaths(cITSPersistSystem *face, void *, uint32_t categoryMask);
    static bool __fastcall Init(cIGZSystemService *face, void *);
    static void __fastcall AddCategoryPath(cITSPersistSystem *face, void *, int32_t categoryMask, const char *name);
    static uint8_t __fastcall CollectPathGroups(cITSPersistSystem *face, void *, cIGZString *path, void *groups);
    static uint8_t __fastcall OpenSegmentByInfo(cITSPersistSystem *face, void *, uint32_t iid, void **out, void *info);
    static uint8_t __fastcall CopyResource(cITSPersistSystem *face, void *, const void *key, void *source, const void *newKey, void *target);
    static uint8_t __fastcall CopySegmentResources(cITSPersistSystem *face, void *, void *target, void *source, void *mapper, void *filter);
    static uint8_t __fastcall IsSegmentWritable(cITSPersistSystem *face, void *, void *segment);
    static uint8_t __fastcall vf28(cIGZPersistResourceManager *face, void *, void *segment);
    static uint8_t __fastcall vf30(cIGZPersistResourceManager *face, void *, void *segment);
    static uint8_t __fastcall DeleteGroupFiles(cITSPersistSystem *face, void *, uint32_t group, uint32_t onlyUnused);
    static uint8_t __fastcall CommitSegmentsWithBackup(cITSPersistSystem *face, void *, void *const *segments, uint32_t guardLowDiskSpace);
    static uint8_t __fastcall AddPackageFile(cITSPersistSystem *face, void *, cIGZString *path);
    static uint8_t __fastcall SetResourceName(cITSPersistSystem *face, void *, void *segment, const uint32_t *key, cIGZString *name);
    static uint8_t __fastcall GetResourceName(cITSPersistSystem *face, void *, void *segment, const uint32_t *key, cIGZString **out);
    static uint8_t __fastcall GetCachedResource(cITSPersistSystem *face, void *, uint32_t group, const uint32_t *key, void **out);
    static uint8_t __fastcall vf39(cIGZPersistResourceManager *face, void *, uint32_t key, uint32_t record, uint32_t mode);
};
#define PSX(Name) psrva::Name, static_cast<decltype(&cTSPersistSystemDroppedAdapters::Name)>(nullptr)

// A test whose body runs once per route (psshape::RunRoutes).
#define PS_TEST(name, rva)                                                      \
    static void DIFF_TEST_CAT(PsRoutes_, name)(difftest::Context & ctx);      \
    DIFF_TEST(name, rva) { psshape::RunRoutes(ctx, DIFF_TEST_CAT(PsRoutes_, name)); } \
    static void DIFF_TEST_CAT(PsRoutes_, name)(difftest::Context & ctx)
