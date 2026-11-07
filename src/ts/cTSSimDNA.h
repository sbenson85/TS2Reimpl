#pragma once
#include <cstddef>
#include <cstdint>
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

// nTSGenetics::cTSSimDNA: a sim's genetic record (genes as pairs of strings in a retained property store). RTTI: one face,
// nTSGenetics::cITSSimDNA (+0, vtable 0xdce3f0, 18 slots, interface id 0x0c7aad39). 12 bytes. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md, lane EE): a real C++ class. The compiler emits its vtable (the interface's slot
// order; slot 17 is the class's scalar deleting destructor) and RTTI. The game creates it only through the factory Create
// 0x4f9020 (operator new + the constructor inline; its address is stored by the cTSSimCOMDirector constructor 0x415ae0), hooked
// to our Create: every DNA object carries our vtable. The game's vtable is referenced only by that factory and the deleting
// destructor 0x4f5e40. The RVA hooks on the old slot functions are adapters (cTSSimDNAAdapters). Nothing in the image reads the
// RTTI (type descriptors 0x10a6a34 / 0x10a69dc: RTTI data only).

namespace nTSGenetics {
// Slot 6 returns two independently retained strings through an eight-byte result.
struct DNAStringPair {
    cIGZString *first;
    cIGZString *second;
};
static_assert(sizeof(DNAStringPair) == 8);

struct DNAResourceKey { uint32_t words[4]; };
static_assert(sizeof(DNAResourceKey) == 16);
struct DNAStringFloat {
    cRZString text; // +0x00
    float value; // +0x20
};
static_assert(sizeof(DNAStringFloat) == 0x24);
static_assert(offsetof(DNAStringFloat, value) == 0x20);
struct DNAWeightedGene {
    cRZString name; // +0x00
    cRZString value; // +0x20
    int32_t weight; // +0x40
};
static_assert(sizeof(DNAWeightedGene) == 0x44);
static_assert(offsetof(DNAWeightedGene, value) == 0x20);
static_assert(offsetof(DNAWeightedGene, weight) == 0x40);
template<class T> struct DNAVector {
    T *begin; // +0x00
    T *end; // +0x04
    T *capacity; // +0x08; allocator-service storage, not std::allocator storage
    uint32_t Size() const { return begin ? uint32_t(end - begin) : 0; }
    T *InsertReallocate(T *position, const T *value); // 0x4f50e0 for weighted genes: insert a copied record in newly grown storage
};
using DNAWeightedVector = DNAVector<DNAWeightedGene>;
using DNAStringVector = DNAVector<cRZString>;
static_assert(sizeof(DNAWeightedVector) == 12);
static_assert(sizeof(DNAStringVector) == 12);
template<> DNAWeightedGene *DNAVector<DNAWeightedGene>::InsertReallocate(DNAWeightedGene *position, const DNAWeightedGene *value);

// The DNA interface (id 0x0c7aad39; the original's name, in its namespace as its RTTI). Byte results are AL only. Slot 17 (the
// deleting destructor) is the class's own virtual destructor, declared there.
class cITSSimDNA : public cIGZUnknown {
public:
    virtual uint8_t CopyFrom(cITSSimDNA *source) = 0;                               // slot 3 (ret 4)
    virtual uint8_t SetGene(uint32_t gene, const DNAStringPair *values) = 0;          // slot 4 (ret 8)
    virtual uint8_t HasGene(uint32_t gene) = 0;                                       // slot 5 (ret 4)
    virtual DNAStringPair *GetGene(DNAStringPair *out, uint32_t gene) = 0;            // slot 6 (ret 8)
    virtual void SetPropertyStore(void *store) = 0;                                   // slot 7 (ret 4)
    virtual void *GetPropertyStore() = 0;                                             // slot 8
    virtual uint8_t GetWeightedGenes(DNAWeightedVector *out, uint8_t second) = 0;     // slot 9 (ret 8)
    virtual uint8_t SetWeightedGenes(const DNAWeightedVector *values, uint8_t both) = 0; // slot 10 (ret 8)
    virtual uint8_t GetStringGenes(DNAStringVector *out) = 0;                         // slot 11 (ret 4)
    virtual uint8_t SetStringGenes(const DNAStringVector *values, uint8_t both) = 0;  // slot 12 (ret 8)
    virtual uint8_t GetAdditionalStringGenes(DNAStringVector *out) = 0;               // slot 13 (ret 4)
    virtual uint8_t SetAdditionalStringGenes(const DNAStringVector *values, uint8_t both) = 0; // slot 14 (ret 8)
    virtual void CaptureAppearance(void *appearance, uint32_t mask) = 0;              // slot 15 (ret 8)
    virtual void ApplyAppearance(void *appearance) = 0;                               // slot 16 (ret 4)
};

class cTSSimDNA : public cITSSimDNA {
public:
    // The game's vtable (the tests relate ours to it; nothing in src/ installs it any more).
    static constexpr uint32_t kOriginalVTable = 0xdce3f0;
    static constexpr uint32_t kInterfaceId = 0x0c7aad39;

    cTSSimDNA() : mRefCount(0), mPropertyStore(nullptr) {} // inline in Create 0x4f9020
    // Slot 17: the scalar deleting destructor 0x4f5e40 (EH noexcept) is the compiler's: this destructor (vtable stored back, the
    // retained property store released, not cleared), then operator delete(this, 12) on flags bit 0; returns this.
    virtual ~cTSSimDNA();
    cTSSimDNA(const cTSSimDNA &) = delete;
    cTSSimDNA &operator=(const cTSSimDNA &) = delete;
    static cTSSimDNA *__cdecl Create(); // 0x4f9020: new, null on failure

    // cIGZUnknown
    bool QueryInterface(uint32_t id, void **out) override; // slot 0, 0x4fb6a0: ids 1 and kInterfaceId; AL only
    uint32_t AddRef() override;                            // slot 1, folded 0x148fb0: ++count
    uint32_t Release() override;                           // slot 2, 0x4fb6f0: above one decrement, otherwise delete this
    // cITSSimDNA
    uint8_t CopyFrom(cITSSimDNA *source) override; // slot 3, 0x4f6aa0: copy present genes and indexed families, excluding gene 4
    uint8_t SetGene(uint32_t gene, const DNAStringPair *values) override; // slot 4, 0x4fc690: both channels under decimal keys; AL=0
    uint8_t HasGene(uint32_t gene) override; // slot 5, 0x4fa590: either channel in the property store
    DNAStringPair *GetGene(DNAStringPair *out, uint32_t gene) override; // slot 6, 0x4f9280: two retained copies, "" when missing
    void SetPropertyStore(void *store) override; // slot 7, folded 0x4043a0: retain the new store, release the old one
    void *GetPropertyStore() override;           // slot 8, folded 0x46390
    uint8_t GetWeightedGenes(DNAWeightedVector *out, uint8_t second) override; // slot 9, 0x4f9830
    uint8_t SetWeightedGenes(const DNAWeightedVector *values, uint8_t both) override; // slot 10, 0x4fd090
    uint8_t GetStringGenes(DNAStringVector *out) override; // slot 11, 0x4f9d30: count gene 11, base 0x1000
    uint8_t SetStringGenes(const DNAStringVector *values, uint8_t both) override; // slot 12, 0x4fd390
    uint8_t GetAdditionalStringGenes(DNAStringVector *out) override; // slot 13, 0x4f9600: count gene 15, base 0x1400
    uint8_t SetAdditionalStringGenes(const DNAStringVector *values, uint8_t both) override; // slot 14, 0x4fcea0
    void CaptureAppearance(void *appearance, uint32_t mask) override; // slot 15, 0x4fb740
    void ApplyAppearance(void *appearance) override; // slot 16, 0x4fc8a0

    // +0x00: the vtable pointer
    uint32_t mRefCount; // +0x04
    void *mPropertyStore; // +0x08: retained key/value interface; slots 3/9/10 write/read/test string keys
};
static_assert(sizeof(cTSSimDNA) == 0x0c);
static_assert(offsetof(cTSSimDNA, mRefCount) == 4);
static_assert(offsetof(cTSSimDNA, mPropertyStore) == 8);

// The RVA hooks' entry points (src/hook/tables/cTSSimDNA.cpp). Every DNA object the game creates carries our vtable, so these
// run only where the game calls an old entry point directly or holds an object built with the game's vtable (none while the
// Create hook is on). Original calling convention (this in ecx), our functions called without virtual dispatch, bodies inlined.
struct cTSSimDNAAdapters {
    using Self = cTSSimDNA;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
};
}
