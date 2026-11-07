#pragma once
#include "gz/cIGZPersistResource.h"

#include <cstddef>
#include <cstdint>

// cGZPersistResource: the GZ framework's base of persist resources (RTTI: one face, cIGZPersistResource), 0x1c bytes, /O2.
// Lane EL (standalone shape, docs/standalone-shape.md). RVAs 1.18.0.187.
//
// Constructor 0x9b5fd0 (vtable 0xe603ac, +4..+0x18 zeroed), destructor 0x9b6030 (stores the vtable only), vtable 0xe603ac (13
// slots): 0 QI 0x437e30 (folded: 1 and 0x456b8f1d give this), 1 AddRef 0x99ba50 (folded, cGZPersistDBSegment's entry),
// 2 Release 0x9b6260, 3/4 GetKey / SetKey 0x18a670 / 0x4f3d80, 5-7 the key's type / group / instance 0x145770 / 0x149cf0 /
// 0x149d00, 8-10 their setters 0x1eeb70 / 0x1eec40 / 0x47d410, 11 the count 0x147a30, 12 the deleting destructor 0x9b6050.
// Every slot but Release and the destructor is folded with other classes' (shared_vf_*): implemented here, never hooked.
//
// Derived classes (RTTI): the property-set resources (ours, src/gz/cGZPropertySetResource.h) and ~70 game classes still built
// by the game (cRZStringResource, cGZBufferResource, cGZPersistBinaryResource, cRZCursorResource, cRZWinKeyAcceleratorRes,
// cTSEffectsResource, cTSGameTip, nTSSG::cLotImposterDesc, every nGZSceneGraph::cSGResource): their constructors and destructors
// call 0x9b5fd0 / 0x9b6030 (the adapters below: our base part, then they store their own vtables), and their Release
// slots forward to 0x9b6260. The vtable 0xe603ac is referenced by the constructor, the destructor and 0x9b6050 alone.
class cGZPersistResource : public cIGZPersistResource {
public:
    static constexpr uint32_t kOriginalVTable = 0xe603ac;
    static constexpr uint32_t kIid = 0x456b8f1d; // cIGZPersistResource

    cGZPersistResource() = default;  // 0x9b5fd0
    virtual ~cGZPersistResource();   // 0x9b6030; slot 12 the scalar deleting destructor 0x9b6050 (delete 0x1c)
    cGZPersistResource(const cGZPersistResource &) = delete;
    cGZPersistResource &operator=(const cGZPersistResource &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;   // slot 0, folded 0x437e30 (ret 8)
    uint32_t AddRef() override;                                // slot 1, folded 0x99ba50: atomic +1, the new count
    uint32_t Release() override;                               // slot 2, 0x9b6260: atomic -1; at 0 delete this, 0
    cGZPersistResourceKey GetKey() override;                   // slot 3, folded 0x18a670
    bool SetKey(const cGZPersistResourceKey &key) override;    // slot 4, folded 0x4f3d80
    uint32_t GetType() override;                               // slot 5, folded 0x145770
    uint32_t GetGroup() override;                              // slot 6, folded 0x149cf0
    uint32_t GetInstance() override;                           // slot 7, folded 0x149d00
    bool SetType(uint32_t type) override;                      // slot 8, folded 0x1eeb70
    bool SetGroup(uint32_t group) override;                    // slot 9, folded 0x1eec40
    bool SetInstance(uint32_t instance) override;              // slot 10, folded 0x47d410
    uint32_t GetRefCount() override;                           // slot 11, folded 0x147a30

    // +0x00 the vtable pointer
    volatile int32_t mRefCount = 0;   // +0x04 (RZAtomicAdd)
    uint32_t mField8 = 0;             // +0x08 nothing in the class reads it
    cGZPersistResourceKey mKey = {};  // +0x0c
};
static_assert(sizeof(cGZPersistResource) == 0x1c);
static_assert(offsetof(cGZPersistResource, mRefCount) == 4);
static_assert(offsetof(cGZPersistResource, mKey) == 0xc);

// The RVA hooks' entry points (src/hook/tables/cGZPersistResource.cpp): the game's derived classes call the constructor and
// destructor directly, and their Release slots jump to 0x9b6260. Original calling convention (this in ecx), our members called
// without virtual dispatch, bodies inlined.
struct cGZPersistResourceAdapters {
    using Self = cGZPersistResource;
    static Self *__fastcall Construct(void *memory, void *);                 // 0x9b5fd0
    static void __fastcall Destruct(Self *self, void *);                     // 0x9b6030
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags); // 0x9b6050
    static uint32_t __fastcall Release(Self *self, void *);                  // 0x9b6260
};
