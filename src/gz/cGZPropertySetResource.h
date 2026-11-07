#pragma once
#include "gz/cGZPersistResource.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZPropertySet.h"
#include "rz/RZUnknownBase.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

// Property sets as persist resources, and the factory that reads and writes them. Structure pass: lane DM
// (notes/feedback/DM.md). RVAs 1.18.0.187.
//
// cGZPropertySetResourceT<cKeyStringTraitsT, cGZPropertySetString>, 0x50 bytes (/O2, module 0x551000-0x557400).
// Faces (RTTI order): +0 cGZPersistResource (vtable 0xdd428c, 13 slots: 0 QI 0x5553b0, 1 AddRef 0x5522b0 and 2 Release 0x556090
// (folded: jumps to cGZPersistResource's), 12 the deleting destructor 0x5520e0, the rest cGZPersistResource's), +0x1c the
// property set (0xdd42c4: the set's slots; 0 / 1 / 2 / 13 thunks 0x5553ed / 0x5522b5 (folded) / 0x556095 (folded) / 0x5520c3),
// +0x40 the dispatcher (0xdd4304: 3 Read 0x555c90, 4 Write 0x5568c0; 0 / 1 / 2 / 5 thunks 0x5553f5 / 0x5522bd / 0x55609d /
// 0x5520cb). Refcount starts at 0. The vtables are referenced by the constructor 0x5519c0 alone.
//
// Standalone shape (docs/standalone-shape.md, lane EL): a real C++ class with the three bases, the original's template
// instantiation by name (an explicit specialization: its RTTI name is the original's). Creation sites: our cGZPropertySetFactory
// (types 0xebfee33f / 0xebfee342), cTSSaveGameInfo::EnsureProperties and cTSBinInfoFactory Read / Write (ours: `new`), and the
// game's callers of the constructor 0x5519c0 (cTSSkinEntryFactory 0xb7e360 / 0xb821d0, cTSProductPatternFactory 0xc14452 /
// 0xc1497a, cTSModelMaterialEntryFactory 0xd07560 / 0xd07f00, cTSWantTreeManager Read / Write 0x555f30 / 0x557240): the
// constructor hook's adapter builds ours. Everyone holds the resource by its set face (+0x1c) or a QI'd face.
template <class Traits, class Set> class cGZPropertySetResourceT;
using cGZPropertySetResourceString = cGZPropertySetResourceT<cKeyStringTraits, cGZPropertySetString>;

template <>
class cGZPropertySetResourceT<cKeyStringTraits, cGZPropertySetString> : public cGZPersistResource,
                                                                      public cGZPropertySetString,      
                                                                      public cGZPropertySetSerializerString {
public:
    static constexpr uint32_t kSize = 0x50;
    static constexpr uint32_t kOriginalVTable = 0xdd428c;
    static constexpr uint32_t kOriginalSetVTable = 0xdd42c4;        // +0x1c
    static constexpr uint32_t kOriginalSerializerVTable = 0xdd4304; // +0x40
    static constexpr uint32_t kSetFace = 0x1c;
    static constexpr uint32_t kSerializerFace = 0x40;

    cGZPropertySetResourceT() = default; // 0x5519c0
    // slot 12, 0x5520e0 (the set face's slot 13 thunk 0x5520c3, the dispatcher's slot 5 thunk 0x5520cb): the dispatcher (its
    // delegates released, binary first), the set, the base.
    ~cGZPropertySetResourceT() override;
    cGZPropertySetResourceT(const cGZPropertySetResourceT &) = delete;
    cGZPropertySetResourceT &operator=(const cGZPropertySetResourceT &) = delete;

    // slot 0, 0x5553b0 (thunks 0x5553ed / 0x5553f5): 0x4bb0d269 -> the set face, 0xebe701f3 -> the dispatcher, counted through
    // slot 1; anything else cGZPersistResource's QueryInterface.
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;  // slot 1 (folded 0x5522b0; thunks 0x5522b5 folded, 0x5522bd): cGZPersistResource's
    uint32_t Release() override; // slot 2 (folded 0x556090; thunks 0x556095 folded, 0x55609d): cGZPersistResource's

    // The set face (+0x1c), as the serializers and the property code take it.
    cGZPropertySetString *SetFace() { return this; }
};
static_assert(sizeof(cGZPropertySetResourceString) == 0x50);

// The string resource's RVA hooks' entry points (src/hook/tables/cGZPropertySetResource.cpp): the constructor for the game's
// creation sites, the others for direct calls and resources built by the game's constructor (none while its hook is on).
struct cGZPropertySetResourceStringAdapters {
    using Self = cGZPropertySetResourceString;
    static Self *__fastcall Construct(void *memory, void *);                             // 0x5519c0
    // QueryInterface 0x5553b0 and the deleting destructor 0x5520e0 have none (lane GT): only the game's vtables reach them.
};

// cGZPropertySetResourceT<cKeyUint32TraitsT, cGZPropertySetT<cKeyUint32TraitsT>>, 0x44 bytes (/O1, factory module).
// Faces (RTTI order): +0 cGZPersistResource (vtable 0xe992b0: 0 QI 0xc2c126, 1 / 2 the string resource's folded 0x5522b0 /
// 0x556090, 12 the deleting destructor 0xc2bcf0 (destructor 0xc2bcb3), the rest cGZPersistResource's), +0x1c the uint32 set
// (0xe992e8: the set's slots; 0 / 1 / 2 / 13 thunks 0xc2c163 / 0x5522b5 (folded) / 0x556095 (folded) / 0xc2bce0), +0x34 the uint32
// dispatcher (0xe99328: 3 Read 0x2425ad, 4 Write 0x242899; 0 / 1 / 2 / 5 thunks 0xc2c16b / 0xc2bd2e / 0xc2c2b6 / 0xc2bce8).
// Types 0xebfee345 / 0xebfee349 (none in the shipped neighbourhood files).
//
// Standalone shape (docs/standalone-shape.md, lane EQ): a real C++ class with the three bases, the original's template
// instantiation by name. Creation: the constructor 0xc2bb25, called only by cGZPropertySetFactory::CreateInstance2 (ours: `new`);
// its hook is an adapter (placement new). The vtables are referenced by the constructor alone.
template <>
class cGZPropertySetResourceT<cKeyUint32Traits, cGZPropertySetUint32> : public cGZPersistResource,
                                                                       public cGZPropertySetUint32,
                                                                       public cGZPropertySetSerializerUint32 {
public:
    static constexpr uint32_t kSize = 0x44;
    static constexpr uint32_t kOriginalVTable = 0xe992b0;
    static constexpr uint32_t kOriginalSetVTable = 0xe992e8;        // +0x1c
    static constexpr uint32_t kOriginalSerializerVTable = 0xe99328; // +0x34
    static constexpr uint32_t kSetFace = 0x1c;
    static constexpr uint32_t kSerializerFace = 0x34;

    cGZPropertySetResourceT() = default; // 0xc2bb25
    // 0xc2bcb3: the dispatcher (its delegates released, binary first), the set, the base; slot 12 the scalar deleting destructor
    // 0xc2bcf0 (the set face's slot 13 thunk 0xc2bce0, the dispatcher's slot 5 thunk 0xc2bce8).
    ~cGZPropertySetResourceT() override;
    cGZPropertySetResourceT(const cGZPropertySetResourceT &) = delete;
    cGZPropertySetResourceT &operator=(const cGZPropertySetResourceT &) = delete;

    // slot 0, 0xc2c126 (thunks 0xc2c163 / 0xc2c16b): 0xebb10cdd -> the set face, 0xebe701f3 -> the dispatcher, counted through
    // slot 1; anything else cGZPersistResource's QueryInterface.
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;  // slot 1 (folded 0x5522b0; thunks 0x5522b5 folded, 0xc2bd2e): cGZPersistResource's
    uint32_t Release() override; // slot 2 (folded 0x556090; thunks 0x556095 folded, 0xc2c2b6): cGZPersistResource's
};
using cGZPropertySetResourceUint32 = cGZPropertySetResourceT<cKeyUint32Traits, cGZPropertySetUint32>;
static_assert(sizeof(cGZPropertySetResourceUint32) == 0x44);

// The uint32 resource's RVA hooks' entry points: the constructor (the factory's site in the game's code, none while our factory's
// Create hook is on), the rest for direct calls.
struct cGZPropertySetResourceUint32Adapters {
    using Self = cGZPropertySetResourceUint32;
    static Self *__fastcall Construct(void *memory, void *);                             // 0xc2bb25
    static void __fastcall Destruct(Self *self, void *);                                 // 0xc2bcb3
};

// cGZPropertySetFactory: cIGZPersistResourceFactory (+0, vtable 0xe99274, 7 slots) and cRZUnknown (+4, 0xe99294, 6 slots),
// 0xc bytes; /O1 (factory module 0xc2bb00-0xc2c4d0). Created only by Create 0xc2be41 (new 0xc + the constructor 0xc2bb7a),
// whose address the cTSAppFWCOMDirector constructor 0xc2bba4 registers; the vtables are referenced by the constructor alone.
// Slots: 0 QI 0xc14428 (folded: 0xa56b8f17 gives this, anything else cRZUnknown's QI on the +4 face), 1/2 AddRef/Release
// (folded 0x1dab20 / 0x1e3970: `add ecx, 4; jmp` cRZUnknown's), 3 CreateInstance 0xc2bd36, 4 CreateInstance2 0xc2bd7c,
// 5 Read 0xc2c173, 6 Write 0xc2c2be. +4 face: 0-2 thunks to the primary's, 3 the deleting destructor (thunk 0xc0ed0e, body
// 0xc0edaa: cRZUnknown's destructor, delete 0xc), 4 RemoveRef 0x1b9538, 5 the count 0x147a30 (cRZUnknown's).
// Type -> class, set iid, Write's binary flag:
//   0xebfee33f string, 0x4bb0d269, 0 (XML)   the genetic data records (cTSNeighborGeneticDataTable)
//   0xebfee342 string, 0x4bb0d269, 1         cTSSaveGameInfo
//   0xebfee345 uint32, 0xebb10cdd, 0 (XML)
//   0xebfee349 uint32, 0xebb10cdd, 1
//
// Standalone shape (docs/standalone-shape.md, lane EF): a real C++ class; the compiler emits both vtables, the +4 face's
// adjustor thunks and the deleting destructor. With the Create hook on, every factory carries our vtables; the slot hooks are
// adapters (cGZPropertySetFactoryAdapters) for an object built by the game's constructor with the hook off.
class cGZPropertySetFactory : public cIGZPersistResourceFactory, public rz::cRZUnknown {
public:
    static constexpr uint32_t kOriginalVTable = 0xe99274;
    static constexpr uint32_t kOriginalUnknownVTable = 0xe99294;
    static constexpr uint32_t kFactoryIid = 0xa56b8f17; // cIGZPersistResourceFactory
    static constexpr uint32_t kTypeStringXml = 0xebfee33f;
    static constexpr uint32_t kTypeStringBinary = 0xebfee342;
    static constexpr uint32_t kTypeUint32Xml = 0xebfee345;
    static constexpr uint32_t kTypeUint32Binary = 0xebfee349;
    static constexpr uint32_t kIStreamIid = 0x051b11bd;
    static constexpr uint32_t kOStreamIid = 0xc51b11cb;

    cGZPropertySetFactory() = default;                  // 0xc2bb7a (the cRZUnknown part 0x1b9052: count 0)
    ~cGZPropertySetFactory() override;                  // +4 slot 3: deleting destructor 0xc0edaa (folded thunk 0xc0ed0e)
    cGZPropertySetFactory(const cGZPropertySetFactory &) = delete;
    cGZPropertySetFactory &operator=(const cGZPropertySetFactory &) = delete;
    static cGZPropertySetFactory *Create();             // 0xc2be41 (_EH_prolog)

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0xc14428 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x1dab20
    uint32_t Release() override;                             // slot 2, folded 0x1e3970
    // slot 3, 0xc2bd36 (ret 0x14): slot 4(record->GetKey()->type, riid, out, a4, a5), then slot 5 Read(*out, record).
    bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5) override;
    // slot 4, 0xc2bd7c (ret 0x14, _EH_prolog): new resource by type (table above), its slot 8 SetType(type), QI.
    // ORIGINAL-BUG: any other type calls slot 8 on a null pointer.
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5) override;
    bool Read(cIGZUnknown *resource, cIGZUnknown *record) override;  // slot 5, 0xc2c173 (ret 8, _EH_prolog)
    bool Write(cIGZUnknown *resource, cIGZUnknown *record) override; // slot 6, 0xc2c2be (ret 8, _EH_prolog)

    // +0x00 the cIGZPersistResourceFactory vtable, +0x04 cRZUnknown {vtable, count}
};
static_assert(sizeof(cGZPropertySetFactory) == 0xc);

// The RVA hooks' entry points (src/hook/tables/cGZPropertySetResource.cpp): reached only by an object built by the game's
// constructor (none while the Create hook is on) or a direct call. Original calling convention (this in ecx), our members
// called without virtual dispatch, bodies inlined.
struct cGZPropertySetFactoryAdapters {
    using Self = cGZPropertySetFactory;
    static Self *__fastcall Construct(void *memory, void *);
};
