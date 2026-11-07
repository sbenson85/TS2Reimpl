#pragma once
// cTSCustomContentSupport (lane JB): the custom content service (system service id 0xac59cfaf, priority -1,000,000; class id
// 0xec59ab08, cITSCustomContentSupport iid 0xec59ab01). 0xcc bytes, /O2. It keeps the downloads directory (the app's path
// 0xece41771, made when missing), the name tables of the content categories (13) and types (30) as lower-case maps, the
// files an import wrote (snapshots, package files) and the records of the import in progress; DoMessage 0xc5 runs the
// import of the downloads directory (slot 12).
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xdbc854, 14 slots: slot 13 is the class's own),
// +4 cRZUnknownMT (0xdbc890, 6), +0x1c cIGZMessageTarget2 (0xdbc8ac), +0x20 cITSCustomContentSupport (0xdbc8c0, 30 slots).
// The original's slot bodies take `this` as their face and reach the fields at face-relative offsets; ours are members.
//
// Standalone shape (docs/standalone-shape.md): the factory 0x3989c0 (hooked: `new` of ours, the ITS face returned) is
// registered by address with nTSSG::cDllCOMDirector (class 0xec59ab08) and is the only caller of the constructor 0x38e0a0
// (hooked too: placement new); the vtables are referenced only by the constructor and the destructor 0x38f420, so every
// object carries ours. Slots still game code forward to their originals (handoff D2: the original called on the content
// face, every argument and result a whole dword); their RVAs are the constructor entry's original_calls.
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class cIGZString;

using ContentRef = cRZAutoRefCount<cIGZUnknown>;

// A cRZString as a value (members, container elements, locals): built and destroyed by the hook-shaped string's own bodies;
// copies are the (string, 0, npos) constructor 0x141eb3 as the original's containers use, assignment the std::string's.
// No move: the original's containers copy (the game's cRZString has none).
struct ContentString {
    union {
        cRZString s;
    };
    ContentString() { s.Construct(); }
    explicit ContentString(const char *text) { s.Construct(text); }
    ContentString(const ContentString &other) { s.Construct(other.s, 0, 0xffffffffu); }
    explicit ContentString(const cRZString &other) { s.Construct(other, 0, 0xffffffffu); }
    ContentString &operator=(const ContentString &other)
    {
        s.mString.assign(other.s.mString);
        return *this;
    }
    ~ContentString() { s.Destruct(); }
    cIGZString *Interface() { return reinterpret_cast<cIGZString *>(&s); }
};
static_assert(sizeof(ContentString) == 0x20, "cRZString");

// The maps' order: the strings' characters (std::operator< on the std::string at +4, 0x144a19).
struct ContentStringLess {
    bool operator()(const ContentString &a, const ContentString &b) const { return a.s.mString < b.s.mString; }
};
// lower-case name -> index: MSVC 14.40 std::map as in the game (node 0x34: links, colour / nil bytes, the key cRZString at
// +0x10, the index at +0x30; insert 0x38dc80, lower_bound 0x37eeb0, _Erase_tree 0x38cbc0).
using ContentNameMap = std::map<ContentString, uint32_t, ContentStringLess>;
static_assert(sizeof(ContentNameMap) == 8, "MSVC 14.40 map");

// The files written by snapshots (+0x4c): elements 0x20 bytes from the allocator service (push_back 0x3a05e0, growth
// 0x38c670, destroyed through the elements' slot 33 with 0 and freed by 0x38f1d0).
using ContentServiceStrings = std::vector<ContentString, GZServiceAllocator<ContentString>>;
// The package files of an import (+0x78), relative to the downloads directory (operator new; tidy 0x1441eb).
using ContentStrings = std::vector<ContentString>;
static_assert(sizeof(ContentServiceStrings) == 0xc && sizeof(ContentStrings) == 0xc, "MSVC 14.40 vector");

// One entry of an import in progress (+0xac; 0xac bytes, destroyed by 0x38f6f0, the vector by 0x38f0b0).
struct ContentRecord {
    uint32_t mType;           // +0x00 index into the type names (slot 19)
    uint32_t mKey[4];         // +0x04
    ContentString mName;      // +0x14
    ContentRef mResource;     // +0x34 counted; its slot 11 gives the file it was read from (slot 10 deletes that file)
    ContentString mText38;    // +0x38
    uint32_t mWord58;         // +0x58
    ContentString mText5C;    // +0x5c
    ContentString mText7C;    // +0x7c
    uint32_t mWords9C[4];     // +0x9c
};
static_assert(sizeof(ContentRecord) == 0xac, "ContentRecord");
static_assert(offsetof(ContentRecord, mName) == 0x14 && offsetof(ContentRecord, mResource) == 0x34 &&
                  offsetof(ContentRecord, mText38) == 0x38 && offsetof(ContentRecord, mWord58) == 0x58 &&
                  offsetof(ContentRecord, mText5C) == 0x5c && offsetof(ContentRecord, mText7C) == 0x7c &&
                  offsetof(ContentRecord, mWords9C) == 0x9c,
              "ContentRecord layout");
using ContentRecords = std::vector<ContentRecord>;

// cITSCustomContentSupport (iid 0xec59ab01): slots 3-29. Slots still game code take and return whole dwords.
class cITSCustomContentSupport : public cIGZUnknown {
public:
    // 3 (0x39eaa0): slot 13 (file, files, out word), then slot 5 (target) and slot 6; the files released (slot 15 first).
    virtual bool InstallFile(void *file, void *target) = 0;
    virtual uint32_t Slot4(uint32_t a, uint32_t b, uint32_t c) = 0;        // 4 (0x39eb80)
    // 5 (0x39e820): the installer (framework class 0xad50ac22) prepared for `target` (its slot 3), named (slot 5, category
    // name 3), described (slot 11, the text service 0x8af76dc8's slot 19), given every record with a resource (slot 7),
    // then run (slot 12 with 1): its answer.
    virtual bool Install(void *target) = 0;
    virtual void Reset() = 0;                                              // 6 (folded 0x1ae2f0): slot 10 tail-called
    // 7 (0x39cd10): the records' keys as hex names handed to the service 0x8bfc2f67 (slot 11, with the state code); the
    // state dropped (slot 10) when it refuses.
    virtual bool Commit() = 0;
    virtual uint32_t Slot8(uint32_t a) = 0;                                // 8 (0x39f680)
    // 9 (0x39faf0): the written files that exist handed to the service 0x8bfc2f67 (slot 9) with +0x58; its result.
    virtual uint32_t SubmitWrittenFiles(uint32_t unused, uint32_t value) = 0;
    // 10 (0x39fd30): the import's files deleted, its state dropped. No result (eax is left over; no caller reads it).
    virtual void Cleanup() = 0;
    virtual uint32_t Slot11(uint32_t a, uint32_t b) = 0;                   // 11 (0x39cf70)
    virtual uint32_t ImportDownloads() = 0;                                // 12 (0x39d400): DoMessage 0xc5
    virtual uint32_t Slot13(uint32_t a, uint32_t b, uint32_t c) = 0;       // 13 (0x391c20)
    virtual uint32_t Slot14(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) = 0; // 14 (0x3923f0)
    // 15 (0x391040): `files` (a ServiceRefVector of counted cIGZStrings): each outside the downloads directory deleted, each
    // inside it closed where the resource manager has it open (slot 9, then slot 5); the files released, the vector emptied.
    virtual void ClosePackages(void *files) = 0;
    virtual uint32_t Slot16() = 0;                                         // 16 (0x39a2f0)
    virtual uint32_t WrittenFilesSize() = 0;                               // 17 (0x39cb10)
    virtual void GetCategoryName(uint32_t index, cIGZString *out) = 0;     // 18 (0x39bed0): index 0 past 12
    virtual void GetTypeName(uint32_t index, cIGZString *out) = 0;         // 19 (0x39bf00): index 0 past 29
    virtual void FindCategory(const cIGZString *name, uint32_t *out) = 0;  // 20 (0x39b0f0): 0 when unknown
    virtual void FindType(const cIGZString *name, uint32_t *out) = 0;      // 21 (0x39c470): 0 when unknown
    virtual uint32_t Slot22(uint32_t a) = 0;                               // 22 (0x390400)
    virtual bool LotObjectsAllowed(void *lotSource) = 0;                   // 23 (0x390be0)
    virtual uint32_t Slot24(uint32_t a, uint32_t b) = 0;                   // 24 (0x397730)
    virtual bool IsRegistered(void *resource) = 0;                         // 25 (0x39e6e0)
    // 26 (0x390d80): whether a registered resource is in use: by an object the app lists (*byObject), or among the
    // entries the persist system has for the owner (*inOwner).
    virtual void ResourceInUse(void *owner, void *resource, bool *byObject, bool *inOwner) = 0;
    virtual bool Register(void *resource) = 0;                             // 27 (0x3975d0)
    virtual uint32_t Slot28(uint32_t image, uint32_t path) = 0;            // 28 (0x396070): vf13 writes the snapshot with it
    virtual uint32_t Slot29(uint32_t a, uint32_t b) = 0;                   // 29 (0x398240)
};

class cTSCustomContentSupport : public rz::cRZSystemService, public cIGZMessageTarget2, public cITSCustomContentSupport {
public:
    static constexpr uint32_t kVTable = 0xdbc854;
    static constexpr uint32_t kVTable4 = 0xdbc890;
    static constexpr uint32_t kMessageVTable = 0xdbc8ac;
    static constexpr uint32_t kContentVTable = 0xdbc8c0;
    static constexpr uint32_t kSize = 0xcc;
    static constexpr uint32_t kServiceId = 0xac59cfaf;
    static constexpr int32_t kPriority = int32_t(0xfff0bdc0);
    static constexpr uint32_t kIid = 0xec59ab01;     // cITSCustomContentSupport
    static constexpr uint32_t kImportMessage = 0xc5; // DoMessage: run the import
    static constexpr uint32_t kCategoryNames = 0x100a6e8; // 13 cRZStrings (static, built by the game's initialiser)
    static constexpr uint32_t kTypeNames = 0x100a888;     // 30 cRZStrings
    static constexpr uint32_t kCategoryCount = 13;
    static constexpr uint32_t kTypeCount = 30;

    cTSCustomContentSupport();                  // 0x38e0a0
    ~cTSCustomContentSupport() noexcept override; // 0x38f420; deleting 0x38f970 (+4 thunk 0x38f95b)
    cTSCustomContentSupport(const cTSCustomContentSupport &) = delete;
    cTSCustomContentSupport &operator=(const cTSCustomContentSupport &) = delete;

    // 0x3989c0 (__cdecl): the COM director's factory: a new service, its cITSCustomContentSupport face (null when new failed).
    static cITSCustomContentSupport *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x39f030; thunks 0x39f061 (+4), 0x39f069 (+0x1c), 0x39f071 (+0x20)
    uint32_t AddRef() override;                              // folded 0x1cc320; thunks folded
    uint32_t Release() override;                             // folded 0x1cd7a0; thunks folded

    // ---- cIGZSystemService ----
    bool Init() override;     // 0x39cbb0
    bool Shutdown() override; // 0x39fac0

    // ---- the primary face's own slot 13 ----
    // 0x39a1d0: with a snapshot path set, the image service's picture of `source` (300 x 300) written there (slot 28); the
    // path is then remembered as written.
    virtual void SaveSnapshot(void *source);

    // ---- cIGZMessageTarget2 (+0x1c) ----
    bool DoMessage(void *message) override; // 0x398210

    // ---- cITSCustomContentSupport (+0x20) ----
    bool InstallFile(void *file, void *target) override;
    uint32_t Slot4(uint32_t a, uint32_t b, uint32_t c) override;
    bool Install(void *target) override;
    void Reset() override;
    bool Commit() override;
    uint32_t Slot8(uint32_t a) override;
    uint32_t SubmitWrittenFiles(uint32_t unused, uint32_t value) override;
    void Cleanup() override;
    uint32_t Slot11(uint32_t a, uint32_t b) override;
    uint32_t ImportDownloads() override;
    uint32_t Slot13(uint32_t a, uint32_t b, uint32_t c) override;
    uint32_t Slot14(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) override;
    void ClosePackages(void *files) override;
    // 0x39cb80: the code Commit hands over: with a current object 0 when its slot 24 is 1, else 4; without, 3 when the
    // state is 0, else 1.
    uint32_t StateCode() const;
    uint32_t Slot16() override;
    uint32_t WrittenFilesSize() override;
    void GetCategoryName(uint32_t index, cIGZString *out) override;
    void GetTypeName(uint32_t index, cIGZString *out) override;
    void FindCategory(const cIGZString *name, uint32_t *out) override;
    void FindType(const cIGZString *name, uint32_t *out) override;
    uint32_t Slot22(uint32_t a) override;
    bool LotObjectsAllowed(void *lotSource) override;
    uint32_t Slot24(uint32_t a, uint32_t b) override;
    bool IsRegistered(void *resource) override;
    void ResourceInUse(void *owner, void *resource, bool *byObject, bool *inOwner) override;
    bool Register(void *resource) override;
    uint32_t Slot28(uint32_t image, uint32_t path) override;
    uint32_t Slot29(uint32_t a, uint32_t b) override;

    cITSCustomContentSupport *Content() { return this; }
    cIGZMessageTarget2 *MessageTarget() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c, +0x20: the faces' vtables
    ContentString mDownloadsPath;          // 0x24 the app's path 0xece41771 (Init, when the app has the option object)
    ContentRef mRegistry;                  // 0x44 framework system service 0x0dc7057a (Init)
    ContentRef mPersist;                   // 0x48 the persist system (Init)
    ContentServiceStrings mWrittenFiles;   // 0x4c snapshots written (vf13); slot 17 adds their sizes, slot 10 deletes them
    ContentString mText58;                 // 0x58 a file slot 10 deletes
    ContentStrings mPackageFiles;          // 0x78 names under the downloads directory slot 10 deletes
    ContentString mSnapshotPath;           // 0x84 where vf13 writes
    uint32_t mState;                       // 0xa4
    void *mCurrent;                        // 0xa8 not counted (slot 4)
    ContentRecords mRecords;               // 0xac
    ContentNameMap mCategories;            // 0xb8 lower-case category name -> index (13 names, "assets" -> 2)
    ContentNameMap mTypes;                 // 0xc0 lower-case type name -> index (30 names, "pet" -> 0x1c, "sim", "part" -> 4)
    uint8_t mFlagC8;                       // 0xc8 (+0xc9..+0xcb never written)
};

static_assert(sizeof(cTSCustomContentSupport) == cTSCustomContentSupport::kSize, "cTSCustomContentSupport size");
static_assert(offsetof(cTSCustomContentSupport, mDownloadsPath) == 0x24, "mDownloadsPath");
static_assert(offsetof(cTSCustomContentSupport, mRegistry) == 0x44, "mRegistry");
static_assert(offsetof(cTSCustomContentSupport, mPersist) == 0x48, "mPersist");
static_assert(offsetof(cTSCustomContentSupport, mWrittenFiles) == 0x4c, "mWrittenFiles");
static_assert(offsetof(cTSCustomContentSupport, mText58) == 0x58, "mText58");
static_assert(offsetof(cTSCustomContentSupport, mPackageFiles) == 0x78, "mPackageFiles");
static_assert(offsetof(cTSCustomContentSupport, mSnapshotPath) == 0x84, "mSnapshotPath");
static_assert(offsetof(cTSCustomContentSupport, mState) == 0xa4, "mState");
static_assert(offsetof(cTSCustomContentSupport, mCurrent) == 0xa8, "mCurrent");
static_assert(offsetof(cTSCustomContentSupport, mRecords) == 0xac, "mRecords");
static_assert(offsetof(cTSCustomContentSupport, mCategories) == 0xb8, "mCategories");
static_assert(offsetof(cTSCustomContentSupport, mTypes) == 0xc0, "mTypes");
static_assert(offsetof(cTSCustomContentSupport, mFlagC8) == 0xc8, "mFlagC8");

// The hooks' entry points: the factory (Create), the constructor (placement new of ours) and the state-code helper, which
// the original slot 14 (still game code) calls on our objects (`this` the primary face).
struct cTSCustomContentSupportAdapters {
    static cTSCustomContentSupport *__fastcall Construct(void *memory, void *edx);       // 0x38e0a0
    static uint32_t __fastcall StateCode(const cTSCustomContentSupport *self, void *edx); // 0x39cb80
};
