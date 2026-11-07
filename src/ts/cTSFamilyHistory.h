#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

// cTSFamilyHistory: a family's history, a list of up to 256 snapshots saved with the family (record class id
// 0x46414d68). Primary vtable 0xdcb8ec (cITSFamilyHistory + cIGZSerializable, 11 slots), cIGZPersistResource face at
// +4 (vtable 0xdcb91c, 12 slots). The creator (0x49e1e0) allocates 0x28 bytes and zeroes +8..+0x27. RVAs are 1.18.0.187.

// One string of a snapshot: a cRZString held by value (32 bytes) in an allocator-service vector. The game's element
// type has a user-written copy constructor (vtable, a copied std::string, reference count 0), assignment that assigns
// only the text, no move, and a virtual destructor the vector calls through vtable slot 33 with 0.
struct FamilyHistoryText {
    union {
        cRZString value;
    };
    explicit FamilyHistoryText(const cRZString &from);
    FamilyHistoryText(const FamilyHistoryText &other) : FamilyHistoryText(other.value) {}
    FamilyHistoryText &operator=(const FamilyHistoryText &other);
    ~FamilyHistoryText();
};
static_assert(sizeof(FamilyHistoryText) == 0x20, "FamilyHistoryText layout");

using FamilyHistoryTextVector = std::vector<FamilyHistoryText, GZServiceAllocator<FamilyHistoryText>>;
static_assert(sizeof(FamilyHistoryTextVector) == 12, "FamilyHistoryTextVector layout");

// One snapshot (0x54 bytes). Copying is the game's: zero everything, then assign (fields, then the texts when they
// are another vector); there is no move, so std::vector copies on reallocation and erase, as the game's does.
struct FamilyHistoryEntry {
    int16_t words[8];          // +0x00
    int32_t value10;           // +0x10 (16-bit before stream version 0x29)
    int32_t value14;           // +0x14 (16-bit before stream version 0x29)
    int16_t word18;            // +0x18
    int16_t word1A;            // +0x1a
    int32_t values1C[6];       // +0x1c..+0x30 (16-bit at stream version 0)
    int32_t value34;           // +0x34
    int32_t value38;           // +0x38
    int16_t word3C;            // +0x3c (from version 0x4e)
    int16_t word3E;            // +0x3e (from version 0x4e)
    uint8_t flag40;            // +0x40 (from version 0x4e; 0/1 when read, written as != 0)
    uint8_t flag41;            // +0x41
    uint8_t flag42;            // +0x42
    uint8_t pad43;
    FamilyHistoryTextVector texts; // +0x44 (from version 0x4e)
    int16_t word50;            // +0x50
    uint16_t pad52;

    FamilyHistoryEntry();
    FamilyHistoryEntry(const FamilyHistoryEntry &other);
    FamilyHistoryEntry &operator=(const FamilyHistoryEntry &other);
    ~FamilyHistoryEntry() = default;

    uint8_t Read(void *stream);        // 0x49f650 (__thiscall, ret 4)
    uint8_t Write(void *stream) const; // 0x4a0f50 (__thiscall, ret 4)
};
static_assert(sizeof(FamilyHistoryEntry) == 0x54, "FamilyHistoryEntry layout");
static_assert(offsetof(FamilyHistoryEntry, texts) == 0x44, "FamilyHistoryEntry texts");
static_assert(offsetof(FamilyHistoryEntry, word50) == 0x50, "FamilyHistoryEntry word50");

using FamilyHistoryEntryVector = std::vector<FamilyHistoryEntry>;
// What slot 7 appends to: the same entries in an allocator-service vector.
using FamilyHistoryEntryServiceVector = std::vector<FamilyHistoryEntry, GZServiceAllocator<FamilyHistoryEntry>>;

// The history's own interface (RTTI cITSFamilyHistory, a cIGZSerializable at +0): slots 6..9, then the virtual destructor as
// slot 10 (the scalar deleting destructor). Declared here until a second implementer appears.
class cITSFamilyHistory : public cIGZSerializable {
public:
    virtual void AddEntry(const FamilyHistoryEntry &entry) = 0;          // slot 6
    virtual void GetEntries(FamilyHistoryEntryServiceVector *out) = 0;   // slot 7
    virtual void WriteText(cIGZString *path) = 0;                        // slot 8
    virtual cIGZPersistResource *GetPersistFace() = 0;                   // slot 9
    virtual ~cITSFamilyHistory() = default;                              // slot 10
};

// The standalone shape (docs/standalone-shape.md): cITSFamilyHistory (+ cIGZSerializable) at +0, cIGZPersistResource at +4;
// the compiler emits both vtables, the persist face's QI / Release adjustor thunks (the original's 0x49f642 / 0x49fec7; its
// AddRef thunk 0x4805dd is folded) and the RTTI. Creation: no constructor function; the creator 0x49e1e0 (operator new 0x28,
// constructor inline; its address registered by the cTSSimCOMDirector constructor) is the only site, and the game's vtables are
// referenced only there and by the deleting destructor 0x49ba80. The RVA hooks on the old slot functions are adapters.
class cTSFamilyHistory : public cITSFamilyHistory, public cIGZPersistResource {
public:
    static constexpr uint32_t kOriginalVTable = 0xdcb8ec;
    static constexpr uint32_t kOriginalPersistVTable = 0xdcb91c;
    static constexpr uint32_t kClassId = 0x46414d68;
    static constexpr uint32_t kVersion = 0x55;
    static constexpr uint32_t kMaxEntries = 256;

    cTSFamilyHistory() = default;
    ~cTSFamilyHistory() override; // slot 10, 0x49ba80 (ret 4; bit 0 frees the 0x28 bytes; returns this)

    // 0x49e1e0 (not a known function start, hooked with a hand-checked CRC): the factory the COM director registers.
    static cITSFamilyHistory *__cdecl Create();

    // cIGZUnknown (both faces; the persist face's QI / Release are the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **out) override;           // slot 0, 0x49f5f0
    uint32_t AddRef() override { return ++mRefCount; }                 // slot 1, folded 0x192140
    uint32_t Release() override;                                       // slot 2, 0x49feb0
    // cIGZSerializable
    bool Write(ifaces::cIGZOStream *stream) override;                  // slot 3, 0x4a1240
    bool Read(ifaces::cIGZIStream *stream) override;                   // slot 4, 0x49fce0
    uint32_t GetGZCLSID() override;                                    // slot 5, 0x49e260
    // cITSFamilyHistory
    void AddEntry(const FamilyHistoryEntry &entry) override;           // slot 6, 0x49d350: drops the oldest beyond 255, appends a copy
    void GetEntries(FamilyHistoryEntryServiceVector *out) override;    // slot 7, 0x49eca0: appends a copy of every entry
    // slot 8, 0x49d9c0: writes the entries as tab-separated text to the file named by `path` (slot 5, ToChar): a header row,
    // then one row per entry (26 numbers, then the entry's texts each followed by ". ").
    void WriteText(cIGZString *path) override;
    cIGZPersistResource *GetPersistFace() override { return static_cast<cIGZPersistResource *>(this); } // slot 9, folded 0x1c04e0

    // cIGZPersistResource (+4; these run with this = the face): the record key at +0x18 (the face's +0x14). Folded bodies.
    cGZPersistResourceKey GetKey() override;                                                 // slot 3, 0x49e6e0
    bool SetKey(const cGZPersistResourceKey &key) override;                                  // slot 4, 0x4a0dc0
    uint32_t GetType() override { return mRecordKey[0]; }                                    // slot 5, 0x149d00
    uint32_t GetGroup() override { return mRecordKey[1]; }                                   // slot 6, 0x148ba0
    uint32_t GetInstance() override { return mRecordKey[2]; }                                // slot 7, 0x147d00
    bool SetType(uint32_t type) override { mRecordKey[0] = type; return true; }              // slot 8, 0x47d410
    bool SetGroup(uint32_t group) override { mRecordKey[1] = group; return true; }           // slot 9, 0x1f5270
    bool SetInstance(uint32_t instance) override { mRecordKey[2] = instance; return true; }  // slot 10, 0x1f5240
    uint32_t GetRefCount() override { return mRefCount; }                                    // slot 11, 0x149cf0

    // The CRT calls of WriteText (fopen, fprintf, fclose), through pointers a test can swap for recording fakes.
    static FILE *(__cdecl *sOpen)(const char *path, const char *mode);
    static int(__cdecl *sPrint)(FILE *file, const char *format, ...);
    static int(__cdecl *sClose)(FILE *file);

    // +0x00 / +0x04: the two vtable pointers.
    FamilyHistoryEntryVector mEntries{}; // +0x08
    uint32_t mRefCount = 0;              // +0x14
    uint32_t mRecordKey[4]{};            // +0x18..+0x27: the persist face's key, zeroed by the creator
};
static_assert(sizeof(cTSFamilyHistory) == 0x28, "cTSFamilyHistory layout");
static_assert(offsetof(cTSFamilyHistory, mEntries) == 8, "cTSFamilyHistory entries");
static_assert(offsetof(cTSFamilyHistory, mRefCount) == 0x14, "cTSFamilyHistory refcount");
static_assert(offsetof(cTSFamilyHistory, mRecordKey) == 0x18, "cTSFamilyHistory key");

namespace famhist {
constexpr uint32_t kPersistFace = 4; // offset of the cIGZPersistResource face
}

// The RVA hooks' entry points (src/hook/tables/cTSFamilyHistory.cpp): every history the game creates carries our vtables
// (the Create hook), so these run only where the game calls an old entry point or holds a history built with its vtables.
struct cTSFamilyHistoryAdapters {
    using Self = cTSFamilyHistory;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static void __fastcall AddEntry(Self *self, void *, const FamilyHistoryEntry &entry);
    static void __fastcall GetEntries(Self *self, void *, FamilyHistoryEntryServiceVector *out);
    static void __fastcall WriteText(Self *self, void *, cIGZString *path);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
};
