#pragma once
#include <cstdint>

// nTSCASCore::cTSBinInfoFactory: the persist factory of three CAS record types that are stored as binary string
// property sets (format in src/gz/cGZPropertySetSerializer.h). Structure pass: lane DM (notes/feedback/DM.md).
// /O2 code; RVAs 1.18.0.187.
//
// Created and registered inline in nTSCASCore::cTSBinCatalog::Init (0xb8c0db: new, vtables 0xe8af20 / 0xe8af40 with
// the cRZUnknown part built by 0x1b9052, AddRef, then resource-manager slot 20 RegisterFactory(0x8c4f2caa, type,
// factory) for 0xac598eac, 0x6c4f359d, 0x0c560f39). Layout as cGZPropertySetFactory: vtable, cRZUnknown {vtable,
// refcount}.
// Slots: 0 QI 0xb7bd10 (shared), 1/2 shared AddRef/Release, 3 CreateInstance 0x5522d0 (shared by many factories:
// slot 4 then slot 5 Read, then QI riid on the new object), 4 CreateInstance2 0xb89520, 5 Read 0xb8ddb0,
// 6 Write 0xb90e10. +4 face: all shared.
//
// The resource classes are CAS, not persistence (each its own group):
//   0x0c560f39 BINX  cTSSimFragment   (new 0x74, ctor 0xb86ea0, vtable 0xe8ad90, iid 0x0c560f33):
//                    LoadFromPropertySet slot 29 0xb8ed20 (1.6 KB), SaveToPropertySet slot 28 0xb8a400
//   0x6c4f359d COLL  cTSBinInfo       (new 0x5c, ctor 0xb86e30, vtable 0xe8ad14, iid 0x6c4f3599): slots 25 / 24
//   0xac598eac GZPS  cTSSimOutfitData (new 0x18c, ctor 0xb922d0, vtable 0xe8b25c, iid 0xac598ea8): slots 87 / 86
// Load is (cGZPropertySetString *set, key list (3IDR list slot 7) or 0, segment); Save is (set, key list slot 7).
// In N001 the 1851 BINX records hold binidx, creatorid (string), iconidx, objectidx, sortindex, stringindex,
// stringsetidx (all others AnyUint32).
namespace nTSCASCore {

class cTSBinInfoFactory {
public:
    static constexpr uint32_t kVTable = 0xe8af20;
    static constexpr uint32_t kUnknownVTable = 0xe8af40;
    static constexpr uint32_t kTypeSimFragment = 0x0c560f39;
    static constexpr uint32_t kTypeBinInfo = 0x6c4f359d;
    static constexpr uint32_t kTypeOutfitData = 0xac598eac;
    static constexpr uint32_t kIidSimFragment = 0x0c560f33;
    static constexpr uint32_t kIidBinInfo = 0x6c4f3599;
    static constexpr uint32_t kIidOutfitData = 0xac598ea8;
    static constexpr uint32_t kSimFragmentSize = 0x74;  // what CreateInstance2 allocates for each
    static constexpr uint32_t kBinInfoSize = 0x5c;
    static constexpr uint32_t kOutfitDataSize = 0x18c;
    static constexpr uint32_t kKeyListType = 0xac506764;     // 3IDR: the record's resource key list
    static constexpr uint32_t kKeyListIid = 0x6cd04176;
    static constexpr uint32_t kSegmentIid = 0x656b8f0c;      // record slot 15 (segment of the record)
    static constexpr uint32_t kRegisterClass = 0x8c4f2caa;   // first argument of RegisterFactory
    static constexpr uint32_t kIStreamIidForRead = 0x051b11bd;  // record QI: cIGZIStream
    static constexpr uint32_t kOStreamIidForWrite = 0xc51b11cb; // record QI: cIGZOStream

    // slot 4, 0xb89520 (ret 0x14, SEH): new resource by type (above), AddRef, slot 8 SetType(type), QI(riid, out);
    // unknown type -> 0.
    bool CreateInstance2(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5);
    // slot 5, 0xb8ddb0 (ret 8, SEH, 792 B): temporary cGZPropertySetResourceString, serializer Read, the 3IDR list
    // (resource manager slot 5 GetResource, key {0xac506764, group, instance, high}), the segment (manager primary
    // slot 34, else record slot 15), then the resource's Load; returns 1 once the load is called (its result ignored).
    bool Read(void *resource, void *record);
    // slot 6, 0xb90e10 (ret 8, SEH, 861 B): temporary set; key list from the manager, or a new one (framework create
    // 0xac506764 / 0x6cd04176, persist face SetKey, record segment); resource Save; list slot 4(segment); serializer
    // Write(stream, set, 1).
    bool Write(void *resource, void *record);

    const void *mVTable;        // +0
    const void *mUnknownVTable; // +4
    uint32_t mRefCount;         // +8
};
static_assert(sizeof(cTSBinInfoFactory) == 0xc);

} // namespace nTSCASCore
