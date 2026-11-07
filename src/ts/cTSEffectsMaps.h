#pragma once
// cTSEffectsMaps (lane WO, rendering row E1): the effects' maps by index (the COM director's class 0xac28ee02; interface
// 0xac28edfd). RTTI: cITSEffectsMaps +0, cRZUnknown +4 (vtables 0xe8e49c, 9 slots; 0xe8e4c4, 6 slots). 0x1c bytes: +0xc a
// vector of counted pointers, +0x18 a flag cleared by Init and read by nothing. RVAs 1.18.0.187; all /O2.
//
// Heap: the vector's growth (0x2941e0) allocates through operator new (its callees: operator new 0x824632, sized delete
// 0x824624, _Allocate_manually_vector_aligned), so a plain std::vector is right.
//
// Standalone shape (docs/standalone-shape.md): Create 0xbd96a0 (the director's) and the constructor 0xbd95d0 (called by
// Create and by cTSEffectsManager::Init 0xbc8fa0, still original) are hooked, so every object carries our vtables and the
// slots and destructors need no adapters.
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "ts/CountedRef.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cITSEffectsMaps : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xac28edfd;

    virtual bool Init() = 0;                                  // 3
    virtual bool Shutdown() = 0;                              // 4: every map released
    virtual uint32_t GetCount() = 0;                          // 5
    virtual void SetMap(uint32_t index, cIGZUnknown *map) = 0; // 6 (ret 8)
    virtual cIGZUnknown *GetMap(int32_t index) = 0;           // 7 (ret 4)
    virtual void Reserved(uint32_t, uint32_t, uint32_t, uint32_t) = 0; // 8 (ret 0x10): nothing
};

class cTSEffectsMaps : public cITSEffectsMaps, public rz::cRZUnknown {
public:
    static constexpr uint32_t kClass = 0xac28ee02;

    cTSEffectsMaps();           // 0xbd95d0
    ~cTSEffectsMaps() override; // 0xbd9620; deleting 0xbd9650, +4 thunk 0xbd9640
    static cTSEffectsMaps *__cdecl Create(); // 0xbd96a0: null on failure

    bool QueryInterface(uint32_t riid, void **out) override;  // 0: 0xbd99a0; +4 thunk 0xbd99c7
    uint32_t AddRef() override { return rz::cRZUnknown::AddRef(); }   // 1: shared 0x1dab20
    uint32_t Release() override { return rz::cRZUnknown::Release(); } // 2: shared 0x1e3970
    bool Init() override;                                     // 3: 0xbd9850
    bool Shutdown() override;                                 // 4: 0xbd9b00
    uint32_t GetCount() override;                             // 5: shared 0x751740
    void SetMap(uint32_t index, cIGZUnknown *map) override;   // 6: 0xbd9a00
    cIGZUnknown *GetMap(int32_t index) override;              // 7: 0xbd9670
    void Reserved(uint32_t, uint32_t, uint32_t, uint32_t) override {} // 8: shared 0x16a930 (under 5 bytes)

    std::vector<fx::CountedRef<cIGZUnknown>> mMaps; // +0xc
    bool mFlag;                          // +0x18
};
static_assert(offsetof(cTSEffectsMaps, mMaps) == 0xc && offsetof(cTSEffectsMaps, mFlag) == 0x18 &&
                  sizeof(cTSEffectsMaps) == 0x1c,
              "cTSEffectsMaps layout");

// The constructor hook (the manager's Init builds objects with the constructor).
struct cTSEffectsMapsAdapters {
    static cTSEffectsMaps *__fastcall Construct(void *self, void *);
};
