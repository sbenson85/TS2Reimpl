#pragma once
// cTSParticleEffectDescriptor (lane WO, rendering row E1): a saved name for a particle effect, the sim side's (the COM
// director's class 0xfba4da59, registered by cTSSimCOMDirector; it answers cIGZSerializable 0xe4fda3d4 and its own id). Despite the name it
// is no effects description: two ids, a flag and a name, read and written as a cIGZSerializable. RTTI:
// cITSParticleEffectDescriptor, cIGZSerializable, cIGZUnknown, all at +0 (vtable 0xdcd7fc, 15 slots). 0x34 bytes. RVAs
// 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md): only Create 0x4db0c0 builds one, so with it hooked every object carries our
// vtable and the slots need no adapters.
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

class cITSParticleEffectDescriptor : public cIGZSerializable {
public:
    virtual void SetId(uint32_t id) = 0;                 // 6: shared 0x14a260
    virtual uint32_t GetId() = 0;                        // 7: shared 0x46390
    virtual void SetInstance(uint32_t instance) = 0;     // 8: shared 0x149c60
    virtual uint32_t GetInstance() = 0;                  // 9: shared 0x145770
    virtual void SetFlag(bool flag) = 0;                 // 10: shared 0x1520c0
    virtual bool GetFlag() = 0;                          // 11: shared 0x150f10 (under 5 bytes)
    virtual void SetName(const cRZString *name) = 0;     // 12: 0x4dc6e0
    virtual cRZString *GetName() = 0;                    // 13: shared 0x23e5c0 (under 5 bytes)
    virtual ~cITSParticleEffectDescriptor() = default;   // 14: the scalar deleting destructor 0x4dade0
};

class cTSParticleEffectDescriptor : public cITSParticleEffectDescriptor {
public:
    static constexpr uint32_t kClass = 0xfba4da59; // also its interface id
    static constexpr uint32_t kSerializableIid = 0xe4fda3d4;
    static constexpr uint32_t kWriteVersion = 1;

    cTSParticleEffectDescriptor();
    ~cTSParticleEffectDescriptor() override;
    cTSParticleEffectDescriptor(const cTSParticleEffectDescriptor &) = delete;
    cTSParticleEffectDescriptor &operator=(const cTSParticleEffectDescriptor &) = delete;
    static cTSParticleEffectDescriptor *__cdecl Create(); // 0x4db0c0: null on failure

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0x4dbe40
    uint32_t AddRef() override;                              // 1: shared 0x148fb0
    uint32_t Release() override;                             // 2: shared 0x1f0210: the last one deletes through slot 14
    bool Write(ifaces::cIGZOStream *stream) override;        // 3: 0x4dca30
    bool Read(ifaces::cIGZIStream *stream) override;         // 4: 0x4dbfb0
    uint32_t GetGZCLSID() override { return 0; }             // 5: shared 0x6c6b0 (under 5 bytes)
    void SetId(uint32_t id) override { mId = id; }
    uint32_t GetId() override { return mId; }
    void SetInstance(uint32_t instance) override { mInstance = instance; }
    uint32_t GetInstance() override { return mInstance; }
    void SetFlag(bool flag) override { mFlag = flag; }
    bool GetFlag() override { return mFlag; }
    void SetName(const cRZString *name) override;
    cRZString *GetName() override { return &mName; }

    uint32_t mRefCount;       // +4
    uint32_t mId;             // +8 -1 when built
    uint32_t mInstance;       // +0xc -1 when built
    bool mFlag;               // +0x10
    union {
        cRZString mName;      // +0x14 built and destroyed by hand (a cRZString has no plain constructor)
    };
};
static_assert(offsetof(cTSParticleEffectDescriptor, mRefCount) == 4 && offsetof(cTSParticleEffectDescriptor, mId) == 8 &&
                  offsetof(cTSParticleEffectDescriptor, mInstance) == 0xc && offsetof(cTSParticleEffectDescriptor, mFlag) == 0x10 &&
                  offsetof(cTSParticleEffectDescriptor, mName) == 0x14 && sizeof(cTSParticleEffectDescriptor) == 0x34,
              "cTSParticleEffectDescriptor layout");
