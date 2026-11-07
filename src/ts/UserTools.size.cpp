#include "ts/UserTools.h"

#include "game/vtable.h"

#include <cstring>

namespace {

// The dword at byte offset `off` of `self`, read or written in place.
template <typename T> T &Field(const void *self, uint32_t off)
{
    return *reinterpret_cast<T *>(const_cast<uint8_t *>(static_cast<const uint8_t *>(self)) + off);
}

void *Delegate(const void *self)
{
    return Field<void *>(self, 4);
}

} // namespace

void cTSUserToolBase::ForwardSlot37()
{
    void *target = Delegate(this);
    if (target)
        VirtualCall<void(__thiscall *)(void *), 37>(target);
}

uint32_t cTSUserToolBase::ForwardSlot17(uint32_t a1)
{
    void *target = Delegate(this);
    // Without a delegate the original returns with eax untouched, so the value is undefined; 0 is returned here.
    if (!target)
        return 0;
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 17>(target, a1);
}

uint32_t cTSUserToolBase::ForwardSlot23(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    void *target = Delegate(this);
    if (!target)
        return 0;
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 23>(target, a1, a2, a3, a4, a5);
}

uint32_t cTSUserToolBase::ForwardSlot24(uint32_t a1, uint32_t a2, uint32_t a3)
{
    void *target = Delegate(this);
    if (!target)
        return 0;
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 24>(target, a1, a2, a3);
}

bool cTSUserToolBase::ForwardSlot25Flag(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    void *target = Delegate(this);
    if (!target)
        return false;
    // The original pushes a4 before the constant 1 and the 1 before a3, so the callee sees (a1, a2, a3, 1, a4).
    const uint32_t r = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 25>(target, a1, a2, a3, 1, a4);
    return static_cast<uint8_t>(r) != 0;
}

bool cTSUserToolBase::ForwardSlot26Flag(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    void *target = Delegate(this);
    if (!target)
        return false;
    // The constant 1 is the last argument: the callee sees (a1, a2, a3, a4, 1).
    const uint32_t r = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 26>(target, a1, a2, a3, a4, 1);
    return static_cast<uint8_t>(r) != 0;
}

uint32_t cTSUserToolBase::SelfSlot18(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 18>(this, a1, a2, a3);
}

uint32_t cTSUserToolBase::StoreAndSelfSlot17(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t)
{
    const auto *pair = reinterpret_cast<const uint32_t *>(a3);
    Field<uint32_t>(this, 0x14) = pair[0];
    Field<uint32_t>(this, 0x18) = pair[1];
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 17>(this, a1, a2, a3);
}

void cTSUserToolBuyBuild::ClearByte4C(uint32_t)
{
    Field<uint8_t>(this, 0x4c) = 0;
}

void cTSUserToolGroundBased::EnsureFlag50()
{
    if (!Field<uint8_t>(this, 0x50))
        VirtualCall<void(__thiscall *)(void *), 59>(this);
}

void cTSUserToolGroundBased::ClearByte4CAndNotify(uint32_t a1)
{
    // The original calls BuyBuild's slot 44 directly, not through the vtable.
    ClearByte4C(a1);
    if (Field<uint8_t>(this, 0x60)) {
        void *sub = &Field<uint8_t>(this, 0x54);
        VirtualCall<void(__thiscall *)(void *, void *), 57>(this, sub);
    }
}

void cTSUserToolGroundBased::ClearFlagsAfterSlot60()
{
    if (Field<uint8_t>(this, 0x50))
        VirtualCall<void(__thiscall *)(void *), 60>(this);
    Field<uint8_t>(this, 0x60) = 0;
}

void cTSUserToolGroundBased::SetFlag50()
{
    Field<uint8_t>(this, 0x50) = 1;
}

void cTSUserToolGroundBased::ClearFlag50()
{
    Field<uint8_t>(this, 0x50) = 0;
}

void cTSUserToolGroundBased::SetByte61(uint32_t a1)
{
    Field<uint8_t>(this, 0x61) = static_cast<uint8_t>(a1);
}
