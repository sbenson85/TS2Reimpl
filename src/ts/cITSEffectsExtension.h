#pragma once
#include "sg/SGNodeInterfaces.h"

#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// The effect extension's own 23-slot face. Signatures are represented at their x86 ABI width; the
// names are kept slot-based until the call sites establish the public API vocabulary.
class cITSEffectsExtension : public virtual cIExtension {
public:
    static constexpr uint32_t kIid = 0x0bb263d5;
    virtual uint8_t vf0() = 0;
    // 1 (0xcc5ef0, ret 0x18): operation 0-4 on the effect named `name` at node `nodeName`; 1 on success, 0 otherwise.
    virtual uint8_t Dispatch(uint32_t operation, const char *name, const char *nodeName, uint32_t kind, uint32_t discriminator, uint32_t flag) = 0;
    // 2 (0xcc5480, ret 0x14): a new effect instance of the effects manager, registered; the instance, or null.
    virtual void *CreateEffect(uint32_t kind, const char *name, const char *nodeName, uint32_t discriminator, uint8_t flag) = 0;
    virtual bool vf3(const uint16_t *, std::vector<uint16_t> *) = 0;
    virtual bool RemoveByInstance(void *) = 0;
    virtual uint32_t vf5(const char *, const char *, uint32_t) = 0;
    virtual void *vf6(const char *, const char *, uint32_t) = 0;
    virtual void vf7(uint8_t, int32_t) = 0;
    virtual void vf8(uint32_t) = 0;
    virtual void vf9(uint8_t) = 0;
    virtual void vf10(uint32_t) = 0;
    virtual void vf11(uint32_t) = 0;
    virtual void vf12(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf13(uint32_t, uint32_t) = 0;
    virtual void vf14() = 0;
    virtual void vf15() = 0;
    virtual uint8_t vf16() = 0;
    virtual uint8_t vf17() = 0;
    virtual void ReadRecords(void *stream) = 0; // 18 (0xcc61c0, ret 4)
    virtual void vf19(void *) = 0;
    virtual void SetByte2b(uint8_t) = 0;
    virtual void SetWord2c(uint32_t) = 0;
    virtual void BuildStatusText() = 0;         // 22 (0xcc5810)
};

} // namespace nGZSceneGraph
