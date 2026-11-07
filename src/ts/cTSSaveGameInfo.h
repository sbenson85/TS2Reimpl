#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator

class cIGZString;
class cGZPropertySetString;

// cTSSaveGameInfo: the save-game description record 0xebfee342 (one per neighbourhood package, instance 1; in N001 it
// holds key "0" AnyString game name, "1" AnyString file version, "2" AnyUint32 language), stored as a binary string
// property set through cGZPropertySetFactory. Structure pass: lane DM; implementation: lane DV. /O2 code
// (0xcd7a10-0xcd8b30), every slot but QI/Release/DeletingDestructor has an SEH frame. RVAs 1.18.0.187.
//
// Framework class 0xcdcbe0e3, iid 0xcdcbe0dd (cITSSaveGameInfo, db/methods/cTSSaveGameInfo.jsonl). Created by the COM
// director factory Create 0xcd7aa0 (registered in the cTSCommonCOMDirector constructor); users: cTSNeighborManager::
// RetireNeighbor, cTSNeighborhoodManager::Save and cTSLotInfo (Fill, then Save(group, 1)).
//
// Not polymorphic in C++: the object carries the game's vtable (kVTable) and the members are hooked onto its slots.
// Keys are the decimal text of an index ("%u", 0xd9d510) in a cGZPropertySetString.
class cTSSaveGameInfo {
public:
    static constexpr uint32_t kSize = 0xc;
    static constexpr uint32_t kVTable = 0xeb9e08;
    static constexpr uint32_t kClassId = 0xcdcbe0e3;
    static constexpr uint32_t kIid = 0xcdcbe0dd;
    static constexpr uint32_t kResourceType = 0xebfee342;
    static constexpr uint32_t kPersistSystemService = 0x056b906e; // GetSystemService: cITSPersistSystem
    static constexpr uint32_t kPersistSystemIid = 0x9af5e924;
    static constexpr uint32_t kPropertySetIid = 0x4bb0d269;         // cIGZPropertySetString
    static constexpr uint32_t kPersistResourceIid = 0x456b8f1d;     // cIGZPersistResource
    static constexpr uint32_t kIndexFormat = 0xd9d510;              // "%u"
    static constexpr uint32_t kIndexGameName = 0;   // AnyString, from the app service's slot 37 object, slot 5
    static constexpr uint32_t kIndexVersion = 1;    // AnyString, cRZVersion::GetFileVersion
    static constexpr uint32_t kIndexLanguage = 2;   // AnyUint32, language service slot 7

    cTSSaveGameInfo *Construct();                    // 0xcd7a10 (no frame): vtable, +4 / +8 zero
    static cTSSaveGameInfo *Create();                // 0xcd7aa0: new 0xc + Construct
    void Destruct();                                 // 0xcd7a30: vtable, mProperties->Release
    void *DeletingDestructor(uint32_t flags);        // slot 12, 0xcd7a80

    bool QueryInterface(uint32_t riid, void **out);  // slot 0, 0xcd8720: null out -> 0; 1 or kIid
    // slot 1 AddRef is shared 0x148fb0 (verified)
    uint32_t Release();                              // slot 2, 0xcd8750: > 1 -> decrement, else slot 12(1), 0
    void Fill();                                     // slot 3, 0xcd7fa0: name, version, language
    void SetUint32(uint32_t index, uint32_t value);  // slot 4, 0xcd8a50 (ret 8): EnsureProperties first
    void SetString(uint32_t index, cIGZString *value); // slot 5, 0xcd88c0 (ret 8): new cRZString (0x20) in an AnyString
    bool GetUint32(uint32_t index, uint32_t *out);   // slot 6, 0xcd7b10 (ret 8): null set -> 0
    bool GetString(uint32_t index, cIGZString *out); // slot 7, 0xcd7c20 (ret 8): out slot 16 copies the text
    // slot 8, 0xcd7d50 (ret 4): every key's text through atoi, pushed to *out (a vector whose storage
    // comes from the allocator service: growth 0xb5b2e0 allocates with 0x1d4b10 and frees with 0x1d0903).
    using IndexList = std::vector<int, GZServiceAllocator<int>>;
    void GetIndices(IndexList *out);
    void Describe(cIGZString *out);                  // slot 9, 0xcd8140 (ret 4, 1.5 KB): text dump (lane DY)
    bool Save(uint32_t group, uint32_t instance);    // slot 10, 0xcd8770 (ret 8)
    bool Load(uint32_t group, uint32_t instance);    // slot 11, 0xcd7ed0 (ret 8)

    bool EnsureProperties();                         // 0xcd7e30: creates a cGZPropertySetResourceString

    const void *mVTable;                // +0
    uint32_t mRefCount;                 // +4
    cGZPropertySetString *mProperties;  // +8 the +0x1c face of a cGZPropertySetResourceString (counted)
};
static_assert(sizeof(cTSSaveGameInfo) == 0xc);
static_assert(offsetof(cTSSaveGameInfo, mProperties) == 8);
