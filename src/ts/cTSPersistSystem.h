#pragma once
#include "gz/cGZPersistResourceManager.h"
#include "gz/cGZPersistResourceManagerShape.h"
#include "gz/cIGZFrameWorkHooks.h"
#include "rz/RZSystemServiceThunks.h"
#include "rz/cRZStopwatch.h"

#include <cstddef>
#include <cstdint>

class cIGZString;
class cRZString;

// cTSPersistSystem, the persistence system service (derives from cGZPersistResourceManager). RVAs are
// 1.18.0.187. Destructor: 0x1d9cb0 (rewrites the vtables below; 0x1da310 is the deleting destructor, operator delete of 0x268 bytes).
//
// Subobjects of the primary object (offsets from the primary object, which is at +0):
//   +0x000  primary vtable 0xda5d74 (58 slots, cGZPersistResourceManager's layout)
//   +0x004  cRZSystemService / cIGZSystemService, vtable 0xda5e60 (13 slots).
//   +0x008  cRZUnknownMT, vtable 0xda5e98 (6 slots)
//   +0x0a8  cITSPersistSystem, vtable 0xda5eb4 (75 slots). The original's code for these slots takes `this` as this
//           subobject and reaches the fields at negative offsets (sub + N = primary + 0xa8 + N); ours are members of
//           the class, which the compiler builds the same way.
//   +0x0ac  cIGZFrameWorkHooks, vtable 0xda5fe4
//   +0x0b0  cGroupNameTranslationProvider, vtable 0xda6014
//
// The field layout is the class cTSPersistSystem at the end of this file (lane CN, from the constructor
// 0x1d8b90, Init 0x1e0d60 and the slots). Lane GD moved every slot body into the class (stage B of the standalone shape);
// each function's notes are next to its body in cTSPersistSystem.cpp.

// 0x1e5260 (__cdecl; lane EW): when `segment` answers the packed-file query (0x286d4a6a), that face's slot 26 gets
// group | 0x10000; true when it answered. A null segment gives false.
uint8_t __cdecl PersistSetPackedFileGroup(void *segment, uint16_t group);


// 0x1dacb0 (403 B, __cdecl; lane FK): the debug compare of primary slot 39. Both records' sizes (slot 10); different: 0.
// Else a zeroed buffer of twice the size, a's raw read (slot 8) into the first half and b's into the second; 1 when both
// reads succeed and the halves are equal.
uint8_t __cdecl CompareRecordContents(void *a, void *b);


class cIGZUnknown; // the hand-written global base interface (src/gz/cIGZUnknown.h), not an ifaces:: one

namespace ifaces {
class cIGZPersistDBSegment;
class cIGZPersistCacheStrategy;
class cITSPersistSpeculativeLoader;
class cIGZThread;
class PersistSystemListener;
} // namespace ifaces

// cITSPersistSystem (75 slots, game vtable 0xda5eb4 at +0xa8; only cTSPersistSystem implements it). Slot order = declaration
// order; the signatures are the hook-shaped bodies' (lane FX; each is described at its body in cTSPersistSystem.cpp).
class cITSPersistSystem : public cIGZUnknown {
public:
    virtual void *GetResourceManager() = 0;                                                              // 3
    virtual uint32_t GetResourceFromFileRef(uint32_t type, void *fileRef, uint32_t instance, uint32_t iid, uint32_t out) = 0; // 4
    virtual uint32_t GetResource(const uint32_t *key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5) = 0; // 5
    virtual uint8_t QueueResourceLoad(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;   // 6
    virtual uint32_t GetResourceSpeculative(uint32_t a1, uint32_t a2, uint32_t a3, void *a4) = 0;            // 7
    virtual uint8_t IsResourceAvailable(uint32_t key) = 0;                                               // 8
    virtual uint8_t IsResourcePending(uint32_t key) = 0;                                                 // 9
    virtual uint32_t GetGroupIdForPathChars(const char *path) = 0;                                       // 10
    virtual uint32_t GetGroupIdForPath(void *path) = 0;                                                  // 11
    virtual uint32_t GetGroupIdForNameChars(const char *name) = 0;                                       // 12
    virtual uint32_t GetGroupIdForFileName(void *segment) = 0;                                           // 13
    virtual uint32_t GetPackedFileGroup(uint32_t group) = 0;                                             // 14
    virtual void Ignore15(uint32_t a1, uint32_t a2) = 0;                                                 // 15 (folded `ret 8`)
    virtual uint8_t SaveResource(void *resource, void *segment) = 0;                                     // 16
    virtual uint8_t DeleteResource(uint32_t key) = 0;                                                    // 17
    virtual uint8_t HasSegmentForFile(void *name, void *path, uint32_t mode) = 0;                        // 18
    virtual void SetFieldF0(uint32_t value) = 0;                                                         // 19 (folded setter)
    virtual uint8_t OpenSegmentByInfo(uint32_t iid, void **out, void *info) = 0;                         // 20
    virtual uint8_t OpenSegmentFromValues(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) = 0; // 21
    virtual uint32_t OpenSegment(uint32_t a1, uint32_t a2, uint32_t a3, void *a4, void *a5, uint32_t a6, uint32_t a7) = 0; // 22
    virtual uint8_t FlushSegmentByInfo(uint32_t info) = 0;                                               // 23
    virtual uint8_t DeleteGroupFiles(uint32_t group, uint32_t onlyUnused) = 0;                           // 24
    virtual uint8_t ReleaseSegmentsForFile(void *path, uint32_t flag) = 0;                               // 25
    virtual void ReopenSegment(void *segment) = 0;                                                       // 26
    virtual void ReopenSegmentsForPath(void *path) = 0;                                                  // 27
    virtual uint8_t IsSegmentWritable(void *segment) = 0;                                                // 28
    virtual void GetWritableSegments(void *out) = 0;                                                     // 29
    virtual uint8_t SegmentHasGroup(void *segment, uint32_t group) = 0;                                  // 30
    virtual uint8_t GetSegmentPath(void *segment, uint32_t path) = 0;                                    // 31
    virtual uint32_t ScoreSegment(void *segment, const uint32_t *info) = 0;                              // 32
    virtual uint8_t FindBestSegment(const uint32_t *key, uint32_t iid, void **out) = 0;                  // 33
    virtual uint8_t FindSegment(const uint32_t *key, uint32_t iid, void **out) = 0;                      // 34
    virtual uint32_t ListKeysOfType(void *out, uint32_t type) = 0;                                       // 35
    virtual uint32_t ListKeys(void *out, uint32_t type, uint32_t value) = 0;                             // 36
    virtual uint8_t CopyResource(const void *key, void *source, const void *newKey, void *target) = 0;   // 37
    virtual uint8_t CopySegmentResources(void *target, void *source, void *mapper, void *filter) = 0;    // 38
    virtual void AgeCaches() = 0;                                                                        // 39
    virtual void PurgeCaches() = 0;                                                                      // 40
    virtual void ClearDeferWrites() = 0;                                                                 // 41
    virtual void CloseAllSegments() = 0;                                                                 // 42
    virtual uint8_t OpenContentPaths(uint32_t categoryMask) = 0;                                         // 43
    virtual void AddCategoryPath(int32_t categoryMask, const char *name) = 0;                            // 44
    virtual uint8_t RegisterSegment(void *segment) = 0;                                                  // 45
    virtual uint8_t AddPackageFile(cIGZString *path) = 0;                                                // 46
    virtual uint8_t CollectPathGroups(cIGZString *path, void *groups) = 0;                               // 47
    virtual void GetSegmentsForGroup(uint32_t group, void *out) = 0;                                     // 48
    virtual uint8_t FlushRollbackSegments(void *const *segments) = 0;                                    // 49
    virtual uint8_t CommitSegmentsWithBackup(void *const *segments, uint32_t guardLowDiskSpace) = 0;     // 50
    virtual uint8_t CommitSegments(void *const *segments) = 0;                                           // 51
    virtual bool LockSystem() = 0;                                                                       // 52
    virtual bool UnlockSystem() = 0;                                                                     // 53
    virtual uint8_t SetResourceName(void *segment, const uint32_t *key, cIGZString *name) = 0;           // 54
    virtual uint8_t GetResourceName(void *segment, const uint32_t *key, cIGZString **out) = 0;           // 55
    virtual uint8_t vf168_56(void *record) = 0;                                                          // 56
    virtual uint32_t RegisterSegmentForKey(uint32_t segment, void *key, uint32_t expected) = 0;          // 57
    virtual uint8_t GetCachedResource(uint32_t group, const uint32_t *key, void **out) = 0;              // 58
    virtual void NoteResourceChange(uint32_t group, uint32_t key, uint32_t a3) = 0;                      // 59
    virtual uint8_t IsVersionSigFound() = 0;                                                             // 60 (folded getter)
    virtual void AddListener(void *listener) = 0;                                                        // 61
    virtual void RemoveListener(void *listener) = 0;                                                     // 62
    virtual void SetKeyedObject(uint32_t key, void *object) = 0;                                         // 63
    virtual uint32_t GetKeyedObject(uint32_t key) = 0;                                                   // 64
    virtual void SetFlushInterval(uint32_t ms) = 0;                                                      // 65 (folded setter)
    virtual uint32_t GetFlushInterval() = 0;                                                             // 66 (folded getter)
    virtual void StartBackgroundOpen() = 0;                                                              // 67
    virtual void StopBackgroundOpen() = 0;                                                               // 68
    virtual uint8_t OpenRecordFromFileRef(void *fileRef, uint32_t type, uint32_t instance, uint32_t iid, uint32_t out, uint32_t *status) = 0; // 69
    virtual uint8_t CreateRecord(uint32_t iid, void **out) = 0;                                          // 70
    virtual uint8_t CommitRecordToFileRef(void *fileRef, uint32_t record, uint32_t type, uint32_t instance, void *name) = 0; // 71
    virtual uint32_t WriteResourceToFileRef(void *resource, void *fileRef, uint32_t instance) = 0;       // 72
    virtual uint32_t ReadResourceFromSegment(void *resource, void *fileRef, uint32_t instance, uint32_t metadata) = 0; // 73
    virtual uint8_t GetAnimTable(const uint32_t *key, uint32_t iid, uint32_t out) = 0;                   // 74
};

namespace nRZSceneGraph {
// The scene graph's group-name translation (game vtable 0xda6014 at +0xb0, 2 slots; not a cIGZUnknown). cTSPersistSystem
// answers slot 0 with cITSPersistSystem slot 10 (the original's thunk 0x1e012a: sub ecx, 8) and slot 1 with its Release
// (thunk 0x1e39ab), so both are declared with those slots' names and signatures and one override covers each pair.
class cGroupNameTranslationProvider {
public:
    virtual uint32_t GetGroupIdForPathChars(const char *path) = 0; // 0
    virtual uint32_t Release() = 0;                                // 1
};
} // namespace nRZSceneGraph

// The whole object in the standalone shape (lane FX, stage A). Bases in RTTI order: the resource manager (its
// cIGZPersistResourceManager face at +0, the system service at +4, cRZUnknownMT at +8), cITSPersistSystem at +0xa8,
// cIGZFrameWorkHooks at +0xac, the translation provider at +0xb0. The compiler builds the vtables (58 / 13 / 6 / 75 / 11 / 2
// slots) and the adjustor thunks. Constructor 0x1d8b90 (the only creation site is cTSPersistCOMDirector::OnStart 0x1d3e20,
// ours, which calls it: its hook is the switch), destructor 0x1d9cb0, deleting destructor 0x1da310 (+8 thunk 0x1da053).
//
// Stage B (lane GD): every slot body is a member of this class (the base's slots the resource manager twin's, whose bodies are
// still the hook-shaped struct's); calls the original makes through a face's vtable are virtual calls on the face, and the
// RVA hooks are one-statement adapters (cTSPersistSystemAdapters). Values in brackets are what the constructor or Init
// (0x1e0d60) stores.
class cTSPersistSystem : public gz::cGZPersistResourceManager,
                         public cITSPersistSystem,
                         public cIGZFrameWorkHooks,
                         public nRZSceneGraph::cGroupNameTranslationProvider {
public:
    static constexpr uint32_t kSize = 0x268;
    // The game's vtables (the tests' original side).
    static constexpr uint32_t kPrimaryVTable = 0xda5d74;      // 58 slots: 0..53 cIGZPersistResourceManager, 54..57 cTS
    static constexpr uint32_t kServiceVTable = 0xda5e60;      // +0x04 cRZSystemService
    static constexpr uint32_t kUnknownVTable = 0xda5e98;      // +0x08 cRZUnknownMT
    static constexpr uint32_t kITSVTable = 0xda5eb4;          // +0xa8 cITSPersistSystem (75 slots)
    static constexpr uint32_t kHooksVTable = 0xda5fe4;        // +0xac cIGZFrameWorkHooks
    static constexpr uint32_t kTranslationVTable = 0xda6014;  // +0xb0 cGroupNameTranslationProvider

    cTSPersistSystem();            // 0x1d8b90
    ~cTSPersistSystem() override;  // 0x1d9cb0 (out of line: the vtable stores, FI's rule)

    // The face addresses without a null test, for the raw slot calls (VirtualCall) and the ifaces:: views of a face.
    void *ServiceFace() { return static_cast<cIGZSystemService *>(this); }
    void *ITSFace() { return static_cast<cITSPersistSystem *>(this); }
    void *HooksFace() { return static_cast<cIGZFrameWorkHooks *>(this); }
    // The hook-shaped resource manager over this object (the base bodies).
    ::cGZPersistResourceManager *Base() { return reinterpret_cast<::cGZPersistResourceManager *>(this); }

    // cIGZUnknown, every face: QueryInterface 0x1e2e50; AddRef 0x1dab30 / Release 0x1e3980 (jumps to the service's).
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;

    // cIGZPersistResourceManager slots this class overrides.
    uint32_t vf3(uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5) override;                 // 0x1e0890
    uint8_t vf4(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override;       // 0x1e08e0
    uint8_t vf5(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override;                    // 0x1e04a0
    uint8_t vf15(uint32_t key) override;                                                                      // 0x1e5670 TestForKey
    uint32_t vf19(uint32_t list, uint32_t type) override;                                                     // 0x1df9b0
    uint8_t vf28(void *segment) override;                                                                     // 0x1e36f0
    uint8_t vf29(void *segment) override;                                                                     // 0x1e36e0 (-> slot 28)
    uint8_t vf30(void *segment) override;                                                                     // 0x1e57a0
    uint8_t vf34(uint32_t key, void **out) override;                                                          // 0x1dea30 FindSegmentWithKey
    uint8_t vf39(uint32_t key, uint32_t record, uint32_t mode) override;                                      // 0x1e2850 OpenRecord
    uint8_t vf49(uint32_t a1, uint32_t a2, uint32_t a3) override;                                             // 0x1dfd00
    uint8_t vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override;      // 0x1dd9d0
    uint8_t vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) override; // 0x1e2f00
    // Slots 54..57, new in this class (appended to the primary vtable in this order).
    virtual void ClearSpeculativeLoader();                                    // 54, 0x1defd0
    virtual uint8_t GetRecordChangeID(uint32_t key, uint32_t changeId);       // 55, 0x1e0780
    virtual uint8_t vf56();                                                   // 56, 0x1e2d50
    virtual void RemoveSegmentsWithPrefix(void *prefix);                      // 57, 0x1dcd80

    // cIGZSystemService (+4).
    int32_t GetServicePriority() override;          // 5, 0x1e0930
    bool Init() override;                           // 8, 0x1e0d60
    bool Shutdown() override;                       // 9, 0x1e53c0
    bool OnTick(uint32_t counter) override;         // 10, 0x1e27c0
    bool OnIdle(uint32_t counter) override;         // 11, 0x1e2740

    // cITSPersistSystem (+0xa8).
    void *GetResourceManager() override;
    uint32_t GetResourceFromFileRef(uint32_t type, void *fileRef, uint32_t instance, uint32_t iid, uint32_t out) override;
    uint32_t GetResource(const uint32_t *key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5) override;
    uint8_t QueueResourceLoad(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override;
    uint32_t GetResourceSpeculative(uint32_t a1, uint32_t a2, uint32_t a3, void *a4) override;
    uint8_t IsResourceAvailable(uint32_t key) override;
    uint8_t IsResourcePending(uint32_t key) override;
    uint32_t GetGroupIdForPathChars(const char *path) override; // also the translation provider's slot 0
    uint32_t GetGroupIdForPath(void *path) override;
    uint32_t GetGroupIdForNameChars(const char *name) override;
    uint32_t GetGroupIdForFileName(void *segment) override;
    uint32_t GetPackedFileGroup(uint32_t group) override;
    void Ignore15(uint32_t, uint32_t) override {}                         // 0x1b9a50 `ret 8`
    uint8_t SaveResource(void *resource, void *segment) override;
    uint8_t DeleteResource(uint32_t key) override;
    uint8_t HasSegmentForFile(void *name, void *path, uint32_t mode) override;
    void SetFieldF0(uint32_t value) override { mFieldF0 = value; }       // 0x1e4d40 (ITS +0x48)
    uint8_t OpenSegmentByInfo(uint32_t iid, void **out, void *info) override;
    uint8_t OpenSegmentFromValues(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) override;
    uint32_t OpenSegment(uint32_t a1, uint32_t a2, uint32_t a3, void *a4, void *a5, uint32_t a6, uint32_t a7) override;
    uint8_t FlushSegmentByInfo(uint32_t info) override;
    uint8_t DeleteGroupFiles(uint32_t group, uint32_t onlyUnused) override;
    uint8_t ReleaseSegmentsForFile(void *path, uint32_t flag) override;
    void ReopenSegment(void *segment) override;
    void ReopenSegmentsForPath(void *path) override;
    uint8_t IsSegmentWritable(void *segment) override;
    void GetWritableSegments(void *out) override;
    uint8_t SegmentHasGroup(void *segment, uint32_t group) override;
    uint8_t GetSegmentPath(void *segment, uint32_t path) override;
    uint32_t ScoreSegment(void *segment, const uint32_t *info) override;
    uint8_t FindBestSegment(const uint32_t *key, uint32_t iid, void **out) override;
    uint8_t FindSegment(const uint32_t *key, uint32_t iid, void **out) override;
    uint32_t ListKeysOfType(void *out, uint32_t type) override;
    uint32_t ListKeys(void *out, uint32_t type, uint32_t value) override;
    uint8_t CopyResource(const void *key, void *source, const void *newKey, void *target) override;
    uint8_t CopySegmentResources(void *target, void *source, void *mapper, void *filter) override;
    void AgeCaches() override;
    void PurgeCaches() override;
    void ClearDeferWrites() override;
    void CloseAllSegments() override;
    uint8_t OpenContentPaths(uint32_t categoryMask) override;
    void AddCategoryPath(int32_t categoryMask, const char *name) override;
    uint8_t RegisterSegment(void *segment) override;
    uint8_t AddPackageFile(cIGZString *path) override;
    uint8_t CollectPathGroups(cIGZString *path, void *groups) override;
    void GetSegmentsForGroup(uint32_t group, void *out) override;
    uint8_t FlushRollbackSegments(void *const *segments) override;
    uint8_t CommitSegmentsWithBackup(void *const *segments, uint32_t guardLowDiskSpace) override;
    uint8_t CommitSegments(void *const *segments) override;
    bool LockSystem() override;
    bool UnlockSystem() override;
    uint8_t SetResourceName(void *segment, const uint32_t *key, cIGZString *name) override;
    uint8_t GetResourceName(void *segment, const uint32_t *key, cIGZString **out) override;
    uint8_t vf168_56(void *record) override;
    uint32_t RegisterSegmentForKey(uint32_t segment, void *key, uint32_t expected) override;
    uint8_t GetCachedResource(uint32_t group, const uint32_t *key, void **out) override;
    void NoteResourceChange(uint32_t group, uint32_t key, uint32_t a3) override;
    uint8_t IsVersionSigFound() override { return mVersionSigFound; }    // 0x1b7090 (ITS +0xc)
    void AddListener(void *listener) override;
    void RemoveListener(void *listener) override;
    void SetKeyedObject(uint32_t key, void *object) override;
    uint32_t GetKeyedObject(uint32_t key) override;
    void SetFlushInterval(uint32_t ms) override { mFlushIntervalMs = ms; } // 0x1e4da0 (ITS +0x138)
    uint32_t GetFlushInterval() override { return mFlushIntervalMs; }    // 0x1dff80 (ITS +0x138)
    void StartBackgroundOpen() override;
    void StopBackgroundOpen() override;
    uint8_t OpenRecordFromFileRef(void *fileRef, uint32_t type, uint32_t instance, uint32_t iid, uint32_t out, uint32_t *status) override;
    uint8_t CreateRecord(uint32_t iid, void **out) override;
    uint8_t CommitRecordToFileRef(void *fileRef, uint32_t record, uint32_t type, uint32_t instance, void *name) override;
    uint32_t WriteResourceToFileRef(void *resource, void *fileRef, uint32_t instance) override;
    uint32_t ReadResourceFromSegment(void *resource, void *fileRef, uint32_t instance, uint32_t metadata) override;
    uint8_t GetAnimTable(const uint32_t *key, uint32_t iid, uint32_t out) override;

    // cIGZFrameWorkHooks (+0xac): PreAppShutdown 0x1e2ce0; the rest the shared `mov al, 1; ret` 0x144910.
    bool PreFrameWorkInit() override { return true; }
    bool PreAppInit() override { return true; }
    bool PostAppInit() override { return true; }
    bool PreAppShutdown() override;
    bool PostAppShutdown() override { return true; }
    bool PostSystemServiceShutdown() override { return true; }
    bool AbortiveQuit() override { return true; }
    bool OnInstall() override { return true; }

    // Non-virtual members (hooked at the member: MemberAddress<&cTSPersistSystem::X>(); notes at the bodies).
    void NotifyListeners(uint32_t event, uint32_t arg); // 0x1de870
    void ReleaseSegments();                             // 0x1e5580
    void SetUpSegment(cRZString *path, cRZString *name, uint32_t info, uint32_t type, void *segment); // 0x1e7230
    uint8_t OpenContentFolder(uint32_t mode, cRZString *path, uint32_t recursive, void *category, void *out, void *onlyFiles); // 0x1e3c70
    // Members tests stand in for through their code (shape::MemberCode): never inlined into their callers here.
    __declspec(noinline) void MakePathKey(const cRZString *path, cRZString *key); // 0x1e6e80
    __declspec(noinline) void NormalisePath(cRZString *path);                     // 0x1e8b80
    __declspec(noinline) void *FillInSegments(uint32_t group); // 0x1e95c0: the group's segment vector (first, last, end)
    __declspec(noinline) void SaveChangeRecords();             // 0x1dedf0
    __declspec(noinline) uint32_t PathStamp(const cRZString *path);                   // 0x1e8ef0
    __declspec(noinline) void *ResetPathEntry(const cRZString *path, uint32_t value); // 0x1e94e0: the entry's vector
    __declspec(noinline) void IndexSegmentGroups(void *segment);                      // 0x1e9b50
    __declspec(noinline) void *SegmentGroups(void *segment); // 0x1e97b0: the segment's group list (std::vector<uint32_t>*) or 0
    __declspec(noinline) void ParkOpenSegments();            // 0x1ea760
    __declspec(noinline) void RelistParkedSegments();        // 0x1e9eb0
    __declspec(noinline) uint8_t LoadResourceNames(void *segment); // 0x1e8fd0
    __declspec(noinline) uint8_t SaveResourceNames(void *segment); // 0x1ea4f0
    __declspec(noinline) void SaveGroupMap();                      // 0x1ea190
    static uint32_t __stdcall BackgroundOpenThread(void *argument); // 0x1defe0: the background thread's procedure

    // Fields. The base's are used under these names: mLockB (+0x68) is the segment lock (cIGZCriticalSection: 1 Lock,
    // 2 Unlock; ITS sub-0x40), mLockD (+0x98) the listener lock, mDeque (+0x70) the base segment list.
    uint8_t mVersionSigFound;            // +0xb4 [0; Init: 1 when Version.sig exists]; ITS slot 60 reads it
    uint8_t mFieldB5;                    // +0xb5 [1] (the constructor stores the word 0x100 at +0xb4)
    uint8_t mPadB6[2];                   // +0xb6
    uint32_t mFieldB8[4];                // +0xb8..+0xc4: not written by the constructor; OpenContentFolder counts in [1..3]
    uint32_t mFieldC8;                   // +0xc8 [5000]
    ::cIGZUnknown *mTypeHandler2026960b; // +0xcc: Init creates class 0xdb5cb1ab (iid 0xa56b8e23); primary vf3/4/5
                                         //        send keys of type 0x2026960b to its slot 7/7/8. Interface not identified.
    ::cIGZUnknown *mTypeHandler4d45f8e6; // +0xd0: class 0xed45f777, keys of type 0x4d45f8e6, same slots.
    ifaces::cIGZPersistDBSegment **mSegmentsBegin; // +0xd4: std::vector<cIGZPersistDBSegment*> (counted) (ITS +0x2c)
    ifaces::cIGZPersistDBSegment **mSegmentsEnd;   // +0xd8 (ITS +0x30)
    ifaces::cIGZPersistDBSegment **mSegmentsCap;   // +0xdc
    ifaces::cIGZPersistCacheStrategy *mSmallObjectCache; // +0xe0: generational (0x78 B), SmallObjectKeyFilter
    ifaces::cIGZPersistCacheStrategy *mLongTermCache;    // +0xe4: generational, LongTermObjectKeyFilter
    ifaces::cIGZPersistCacheStrategy *mGeneralCache;     // +0xe8: generational
    ifaces::cIGZPersistCacheStrategy *mSimCache;         // +0xec: basic (0x34 B, ctor 0x9b35f0), SimKeyFilter
    uint32_t mFieldF0;                   // +0xf0 [1]; ITS slot 19 sets it
    cGZPersistHashMap mNamedMap;         // +0xf4: cRZString-keyed (node 0x2c); vf168_42 empties it
    cGZPersistHashMap mSegmentsByGroup;  // +0x114: group -> std::vector<cIGZPersistDBSegment*> (node 0x18, vector at +0xc)
    cGZPersistHashMap mSegmentIdRanges;  // +0x134: segment id -> DwordRange* (node 0x10)
    cGZPersistHashMap mMap154;           // +0x154: node 0x3c (path key -> two dwords and a vector)
    uint8_t mOpeningContent;             // +0x174 [0]: ITS 43 OpenContentPaths sets it while it runs, clears it at the end
    uint8_t mContentOpened;              // +0x175 [0]: set by the end of ITS 43 (it stores the word 0x100 at +0x174)
    uint8_t mPad176[2];                  // +0x176
    cGZPersistHashMap mMap178;           // +0x178: cRZString -> counted segment (node 0x2c), kept from an earlier open
    cGZPersistHashMap mMap198;           // +0x198: cRZString -> counted segment (node 0x2c), made by OpenContentFolder
    void *mTreeHead;                     // +0x1b8: the resource names (lane FD): std::map<segment id, std::map<type,
                                         //         std::map<std::pair<group, instance>, cRZString *>>> head node (all
                                         //         nodes 0x1c); ITS 54 sets, 55 gets, primary vf56 walks it
    uint32_t mTreeSize;                  // +0x1bc
    ifaces::cITSPersistSpeculativeLoader *mSpeculativeLoader; // +0x1c0 (Init: class 0x8ccf489d, iid 0x6ccf488d)
    uint32_t mField1C4;                  // +0x1c4: not written by the constructor; not mapped
    cRZStopwatch mFlushTimer;            // +0x1c8 [mode 4] (OnTick: auto flush)
    uint32_t mFlushIntervalMs;           // +0x1e0 [0] (ITS slots 65 set, 66 get; 0 = no auto flush)
    ifaces::cIGZThread *mBackgroundThread; // +0x1e4 [0]: cRZThread (8 B) made by ITS 67, joined by 68 (not released)
    cGZPersistHashMap mPathGroups;       // +0x1e8: path cRZString -> group id (node 0x2c, value at node+0x28); ITS 10
    uint32_t mNextPathGroup;             // +0x208 [1]: ITS 10 hands out mNextPathGroup++ | 0x6f000000
    cGZPersistHashMap mChangeRecords;    // +0x20c: group -> change records (node 0x30; 0x1d7970 / 0x1f6de0)
    uint8_t mHasChangeRecords;           // +0x22c: set by ITS 59 (not written by the constructor)
    uint8_t mDeferWrites;                // +0x22d [0]: OnTick and primary vf53 test it; ITS 41 clears it
    uint8_t mField22E;                   // +0x22e [0]
    uint8_t mPad22F;                     // +0x22f
    ifaces::PersistSystemListener **mListenersBegin; // +0x230: std::vector, counted (ITS 61 add / 62 remove)
    ifaces::PersistSystemListener **mListenersEnd;   // +0x234
    ifaces::PersistSystemListener **mListenersCap;   // +0x238
    void *mSet23CHead;                   // +0x23c: std::map<int category mask, value at node+0x14> head (node 0x1c, key +0x10);
                                         //         ITS 43 looks up masks 0xf, 0xf0, ... 0xf00000; Shutdown empties it
    uint32_t mSet23CSize;                // +0x240
    uint32_t mOwnerThreadId;             // +0x244 (StoreCurrentThreadId)
    cGZPersistHashMap mKeyedObjects;     // +0x248: key -> counted object (node 0x10); ITS 63 set / 64 get
};

// The hook entries' entry points (docs/standalone-shape.md, step 6).
struct cTSPersistSystemAdapters {
    static cTSPersistSystem *__fastcall Construct(void *memory, void *edx);                                     // 0x1d8b90
    static void __fastcall Destruct(cTSPersistSystem *self, void *edx);                                         // 0x1d9cb0
    // The slots and non-virtual members (`face` is the face of the slot; one statement each, in cTSPersistSystem.cpp).
    static int32_t __fastcall GetServicePriority(cIGZSystemService *face, void *);
    static void * __fastcall GetResourceManager(cITSPersistSystem *face, void *);
    static void __fastcall ClearDeferWrites(cITSPersistSystem *face, void *);
    static bool __fastcall LockSystem(cITSPersistSystem *face, void *);
    static bool __fastcall UnlockSystem(cITSPersistSystem *face, void *);
    static void __fastcall StopBackgroundOpen(cITSPersistSystem *face, void *);
    static uint8_t __fastcall vf56(cTSPersistSystem *face, void *);
    static uint32_t __fastcall vf3(cIGZPersistResourceManager *face, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5);
    static uint8_t __fastcall vf49(cIGZPersistResourceManager *face, void *, uint32_t a1, uint32_t a2, uint32_t a3);
    static uint8_t __fastcall vf52(cIGZPersistResourceManager *face, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
    static void __fastcall ClearSpeculativeLoader(cTSPersistSystem *face, void *);
    static void __fastcall SetKeyedObject(cITSPersistSystem *face, void *, uint32_t key, void *object);
    static uint8_t __fastcall vf168_56(cITSPersistSystem *face, void *, void *record);
};

static_assert(sizeof(cTSPersistSystem) == cTSPersistSystem::kSize, "cTSPersistSystem size");
static_assert(offsetof(cTSPersistSystem, mVersionSigFound) == 0xb4, "cTSPersistSystem +0xb4: the bases end at +0xb4");
static_assert(offsetof(cTSPersistSystem, mFieldC8) == 0xc8, "cTSPersistSystem +0xc8");
static_assert(offsetof(cTSPersistSystem, mTypeHandler2026960b) == 0xcc, "cTSPersistSystem +0xcc");
static_assert(offsetof(cTSPersistSystem, mSegmentsBegin) == 0xd4, "cTSPersistSystem segment vector");
static_assert(offsetof(cTSPersistSystem, mSmallObjectCache) == 0xe0, "cTSPersistSystem caches");
static_assert(offsetof(cTSPersistSystem, mSimCache) == 0xec, "cTSPersistSystem sim cache");
static_assert(offsetof(cTSPersistSystem, mNamedMap) == 0xf4, "cTSPersistSystem +0xf4");
static_assert(offsetof(cTSPersistSystem, mOpeningContent) == 0x174, "cTSPersistSystem +0x174");
static_assert(offsetof(cTSPersistSystem, mTreeHead) == 0x1b8, "cTSPersistSystem +0x1b8");
static_assert(offsetof(cTSPersistSystem, mFlushTimer) == 0x1c8, "cTSPersistSystem +0x1c8");
static_assert(offsetof(cTSPersistSystem, mPathGroups) == 0x1e8, "cTSPersistSystem +0x1e8");
static_assert(offsetof(cTSPersistSystem, mChangeRecords) == 0x20c, "cTSPersistSystem +0x20c");
static_assert(offsetof(cTSPersistSystem, mDeferWrites) == 0x22d, "cTSPersistSystem +0x22d");
static_assert(offsetof(cTSPersistSystem, mListenersBegin) == 0x230, "cTSPersistSystem +0x230");
static_assert(offsetof(cTSPersistSystem, mSet23CHead) == 0x23c, "cTSPersistSystem +0x23c");
static_assert(offsetof(cTSPersistSystem, mKeyedObjects) == 0x248, "cTSPersistSystem +0x248");
