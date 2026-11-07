#pragma once
// cTSContentRegistry (lane KT): the content registry service (system service id 0x0dc7057a, priority -1,000,000;
// cITSContentRegistry iid 0xcdb467ae). 0x68 bytes, /O2. It keeps the registry of package files: for each content key
// (a 16-byte CRC of a package's resources, or its "crc" property) the package's path, made portable (the user data
// directory as "%UserDataDir%", the game data directories as "%GameDataDir000%" ...). The registry lives in a package of
// its own (the app path 0xedb70568) with a header record (format, version), a keys record (key -> path) and, in other
// packages, a properties record (name -> value pairs: "version", "crc").
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xe86150, 14 slots: slot 13 is the class's own),
// +4 cRZUnknownMT (0xe8618c, 6), +0x1c cITSContentRegistry (0xe861a8, 27 slots; 3 and 4 are Init and Shutdown).
// The original's slot bodies take `this` as their face and reach the fields at face-relative offsets; ours are members.
//
// Standalone shape (docs/standalone-shape.md): the factory 0xb57de0 (hooked: `new` of ours, the cITSContentRegistry face
// returned) is registered by address with the common COM director and is the only caller of the constructor 0xb56dc0;
// the vtables are referenced only by the constructor and the destructor 0xb57290, so every object carries ours
// (db/status/cTSContentRegistry.jsonl).
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"
#include "ts/cTSCustomContentSupport.h" // ContentString, ContentRef

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

class cIGZString;
struct cGZPersistResourceKey;

// A content key: the 16 bytes slot 7 makes of a package (its "crc" property, or the CRC of its resources).
struct ContentKey {
    uint32_t mWords[4];
    bool operator==(const ContentKey &other) const
    {
        return mWords[0] == other.mWords[0] && mWords[1] == other.mWords[1] && mWords[2] == other.mWords[2] &&
               mWords[3] == other.mWords[3];
    }
};
static_assert(sizeof(ContentKey) == 0x10, "ContentKey");
// The keys' hash: the sum of the four words (inline in operator[] 0xb57140 and ReadKeys 0xb592c0); equality is the 16
// bytes (find 0x38cd90).
struct ContentKeyHash {
    size_t operator()(const ContentKey &key) const { return key.mWords[0] + key.mWords[1] + key.mWords[2] + key.mWords[3]; }
};

// An entry's version (8 bytes at the entry's +4): an id and two half-words (the registry's own entry gets the caller's;
// the game data entries the table 0x107b6e8).
struct ContentVersion {
    uint32_t mId;     // +0
    uint16_t mMinor;  // +4
    uint16_t mMajor;  // +6
};
static_assert(sizeof(ContentVersion) == 8, "ContentVersion");

// key -> portable path: MSVC 14.40 std::unordered_map (0x20 bytes; node 0x38: the key at +8, the path at +0x18;
// operator[] 0xb57140, find 0xb56c10, erase 0xb5b060, clear 0xadfda0, rehash 0xb5afd0, copy 0xb56c70).
using ContentKeyMap = std::unordered_map<ContentKey, ContentString, ContentKeyHash>;
static_assert(sizeof(ContentKeyMap) == 0x20, "MSVC 14.40 unordered_map");

// One registry (0x4c bytes; default constructor 0xb56ec0, copy 0xb56b50, destructor 0xb570e0). Entry 0 is the user's
// registry file; entries 1.. stand for the game data directories (only their versions are set). No move: the vector's
// growth copies, as the original's (0xb56470) does.
struct ContentEntry {
    uint8_t mDirty;           // +0x00 (+1..+3 never written: 0xb56ec0 and 0xb56b50 write the byte)
    ContentVersion mVersion;  // +0x04
    ContentString mPath;      // +0x0c the registry file
    ContentKeyMap mKeys;      // +0x2c

    ContentEntry() : mDirty(0), mVersion{0, 0, 0} {}
    ContentEntry(const ContentEntry &other) : mDirty(other.mDirty), mVersion(other.mVersion), mPath(other.mPath), mKeys(other.mKeys) {}
    ContentEntry &operator=(const ContentEntry &) = delete;
};
static_assert(sizeof(ContentEntry) == 0x4c, "ContentEntry");
static_assert(offsetof(ContentEntry, mVersion) == 4 && offsetof(ContentEntry, mPath) == 0xc && offsetof(ContentEntry, mKeys) == 0x2c,
              "ContentEntry layout");
using ContentEntries = std::vector<ContentEntry>; // operator new; clear by the elements' destructor 0xb570e0

// The names' order: _stricmp of the characters (0xb558c0's comparisons, 0xb5b120).
struct ContentNameLess {
    bool operator()(const ContentString &a, const ContentString &b) const;
};
// portable path -> key: MSVC 14.40 std::map (node 0x40: the path at +0x10, the key at +0x30; insert 0xb558c0, erase by
// name 0xb5b120, _Erase_tree 0xb55c20).
using RegistryNameMap = std::map<ContentString, ContentKey, ContentNameLess>;
static_assert(sizeof(RegistryNameMap) == 8, "MSVC 14.40 map");

// Slot 25's input: pairs of words each turned into text by 0xaeb800 (name, value).
using ContentPropertyIds = std::vector<std::pair<uint32_t, uint32_t>>;
// Slot 26's output: (name, value) pairs, elements from the allocator service (resize 0xb561e0 / 0xb56ae0).
using ContentProperties = std::vector<std::pair<ContentString, ContentString>, GZServiceAllocator<std::pair<ContentString, ContentString>>>;
static_assert(sizeof(std::pair<ContentString, ContentString>) == 0x40, "property pair");

// cITSContentRegistry (iid 0xcdb467ae): slots 3-26. Indices are the low half-word of their argument.
class cITSContentRegistry : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                        // 3 (thunk 0xb58cd2 to 0xb58b40)
    virtual bool Shutdown() = 0;                                                    // 4 (thunk 0xb5a32d to 0xb5a300)
    virtual bool SaveAll() = 0;                                                     // 5 (0xb59d60)
    virtual bool SaveEntry(uint16_t index) = 0;                                     // 6 (0xb59e70)
    virtual bool ComputeKey(void *segment, ContentKey *out) = 0;                    // 7 (0xb57770)
    virtual bool GetVersion(void *segment, cIGZString *out) = 0;                    // 8 (0xb5aad0)
    virtual bool GetProperty(void *segment, const char *name, cIGZString *out) = 0; // 9 (0xb5a0d0)
    virtual uint32_t EntryCount() = 0;                                              // 10 (0xb59800)
    virtual bool GetEntryVersion(uint16_t index, ContentVersion *out) = 0;          // 11 (0xb58410)
    virtual bool HasKey(const ContentKey *key) = 0;                                 // 12 (0xb58e70)
    virtual bool FindEntryForResource(const cGZPersistResourceKey *key, int16_t *out) = 0; // 13 (0xb58460)
    virtual bool FindEntryForPath(const cIGZString *path, int16_t *out) = 0;        // 14 (0xb58540)
    virtual bool FindKey(const ContentKey *key, uint16_t *out) = 0;                 // 15 (0xb588d0)
    virtual bool IsPathRegistered(const cIGZString *path) = 0;                      // 16 (0xb58e90)
    virtual bool Rebuild(const ContentVersion *version) = 0;                        // 17 (0xb57480)
    virtual bool RebuildEntry(uint16_t index, const ContentVersion *version) = 0;   // 18 (0xb5a800)
    virtual bool RegisterPath(const cIGZString *path, const ContentKey *key) = 0;   // 19 (0xb5a3b0)
    virtual bool RegisterSegment(void *segment) = 0;                                // 20 (0xb5a6f0)
    virtual bool UnregisterSegment(void *segment) = 0;                              // 21 (0xb5a750)
    virtual bool RemoveSegment(void *segment) = 0;                                  // 22 (0xb57cc0)
    virtual void UnregisterKey(const ContentKey *key) = 0;                          // 23 (0xb57d80): no result (al left over)
    virtual bool GetPathForKey(const ContentKey *key, cIGZString *out, bool *literal) = 0; // 24 (0xb58950)
    virtual bool WriteProperties(void *segment, const ContentPropertyIds *ids) = 0; // 25 (0xb5ab10)
    virtual bool ReadProperties(void *segment, ContentProperties *out) = 0;         // 26 (0xb59870)
};

class cTSContentRegistry : public rz::cRZSystemService, public cITSContentRegistry {
public:
    static constexpr uint32_t kVTable = 0xe86150;
    static constexpr uint32_t kVTable4 = 0xe8618c;
    static constexpr uint32_t kRegistryVTable = 0xe861a8;
    static constexpr uint32_t kSize = 0x68;
    static constexpr uint32_t kServiceId = 0x0dc7057a;
    static constexpr int32_t kPriority = int32_t(0xfff0bdc0);
    static constexpr uint32_t kIid = 0xcdb467ae; // cITSContentRegistry

    cTSContentRegistry();                   // 0xb56dc0
    ~cTSContentRegistry() noexcept override; // 0xb57290 (deleting; +4 thunk 0xb57282)
    cTSContentRegistry(const cTSContentRegistry &) = delete;
    cTSContentRegistry &operator=(const cTSContentRegistry &) = delete;

    // 0xb57de0 (__cdecl): the COM director's factory: a new registry, its cITSContentRegistry face (null when new failed).
    static cITSContentRegistry *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xb59820; thunks 0xb5985f (+4) and 0xb59867 (+0x1c)
    uint32_t AddRef() override;                              // folded (the base's)
    uint32_t Release() override;                             // folded (the base's)

    // ---- cIGZSystemService (and cITSContentRegistry 3 / 4) ----
    bool Init() override;     // 0xb58b40
    bool Shutdown() override; // 0xb5a300

    // ---- the primary face's own slot 13 ----
    // 0xb57c10 (ret 0x10): the 16-byte CRC `state` updated with `size` bytes (restarted at 16 x 0xff unless `continuing`).
    virtual bool UpdateCrc(uint8_t *state, const uint8_t *data, uint32_t size, bool continuing);

    // ---- cITSContentRegistry (+0x1c) ----
    bool SaveAll() override;
    bool SaveEntry(uint16_t index) override;
    bool ComputeKey(void *segment, ContentKey *out) override;
    bool GetVersion(void *segment, cIGZString *out) override;
    bool GetProperty(void *segment, const char *name, cIGZString *out) override;
    uint32_t EntryCount() override;
    bool GetEntryVersion(uint16_t index, ContentVersion *out) override;
    bool HasKey(const ContentKey *key) override;
    bool FindEntryForResource(const cGZPersistResourceKey *key, int16_t *out) override;
    bool FindEntryForPath(const cIGZString *path, int16_t *out) override;
    bool FindKey(const ContentKey *key, uint16_t *out) override;
    bool IsPathRegistered(const cIGZString *path) override;
    bool Rebuild(const ContentVersion *version) override;
    bool RebuildEntry(uint16_t index, const ContentVersion *version) override;
    bool RegisterPath(const cIGZString *path, const ContentKey *key) override;
    bool RegisterSegment(void *segment) override;
    bool UnregisterSegment(void *segment) override;
    bool RemoveSegment(void *segment) override;
    void UnregisterKey(const ContentKey *key) override;
    bool GetPathForKey(const ContentKey *key, cIGZString *out, bool *literal) override;
    bool WriteProperties(void *segment, const ContentPropertyIds *ids) override;
    bool ReadProperties(void *segment, ContentProperties *out) override;

    cITSContentRegistry *Registry() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSContentRegistry face's vtable
    bool mInitialized;              // 0x20 (+0x21..+0x23 never written: 0xb56dc0 writes the byte)
    ContentEntries mEntries;        // 0x24
    RegistryNameMap mNames;          // 0x30 portable path -> key, every registered package
    ContentString mRegistryDir;     // 0x38 the app path 0x2bcfce83 (the user data directory), normalised
    ContentStrings mDataDirs;       // 0x58 the app paths 0x4bcfce5e of every option bit up to the highest, normalised
    ContentRef mManager;            // 0x64 the persist system's resource manager (Init)

private:
    void Load();                                                      // 0xb58f80
    void Clear();                                                     // 0xb59c40
    uint16_t AddEntry(const ContentVersion *version, cRZString *path); // 0xb573b0
    void AddSegmentKey(const ContentKey *key, void *segment, uint16_t index); // 0xb59680
    void EraseKey(ContentEntry &entry, const ContentKey &key);        // inline in 0xb5a750 / 0xb57d80
};

static_assert(sizeof(cTSContentRegistry) == cTSContentRegistry::kSize, "cTSContentRegistry size");
static_assert(offsetof(cTSContentRegistry, mInitialized) == 0x20, "mInitialized");
static_assert(offsetof(cTSContentRegistry, mEntries) == 0x24, "mEntries");
static_assert(offsetof(cTSContentRegistry, mNames) == 0x30, "mNames");
static_assert(offsetof(cTSContentRegistry, mRegistryDir) == 0x38, "mRegistryDir");
static_assert(offsetof(cTSContentRegistry, mDataDirs) == 0x58, "mDataDirs");
static_assert(offsetof(cTSContentRegistry, mManager) == 0x64, "mManager");

// The registry's free helpers (exposed for the difftest's citations; all __cdecl in the original).
namespace content_registry {
void BuildCrcTable();                                      // 0xb58ce0
void Normalize(cRZString *path);                           // 0xb57ac0
void LowerCase(cRZString *path);                           // 0xb57b90
void MakePortable(const cRZString *path, cRZString *out);  // 0xb58170
void ExpandPortable(const cRZString *path, cRZString *out); // 0xb57e50
} // namespace content_registry
