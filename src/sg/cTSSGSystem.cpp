#include "sg/cTSSGSystem.h"

namespace nTSSG {

uint32_t cTSSGSystemPrimary::GetFieldDword2C4()
{
    return *reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(this) + 0x2c4);
}

void cTSSGSystemPrimary::SetFieldDword2C4(uint32_t value)
{
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(this) + 0x2c4) = value;
}

void cTSSGSystemPrimary::SetFlag3A8()
{
    *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(this) + 0x3a8) = 1;
}

uint32_t cTSSGSystemPrimary::GetFieldDword3A4()
{
    return *reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(this) + 0x3a4);
}

uint8_t cTSSGSystemPrimary::GetFieldByte162()
{
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(this) + 0x162);
}

uint8_t cTSSGSystemPrimary::GetFieldByte18E()
{
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(this) + 0x18e);
}

uint8_t cTSSGSystemPrimary::GetFieldByte18A()
{
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(this) + 0x18a);
}

uint8_t cTSSGSystemPrimary::GetFieldByte18B()
{
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(this) + 0x18b);
}

uint8_t cTSSGSystemPrimary::GetFieldByte18C()
{
    return *reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(this) + 0x18c);
}

void *cTSSGSystemPrimary::AddressOf374()
{
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(this) + 0x374);
}

void *cTSSGSystemPrimary::AddressOf380()
{
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(this) + 0x380);
}

void *cTSSGSystemPrimary::AddressOf38C()
{
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(this) + 0x38c);
}

uint32_t cTSSGSystemPrimary::GetDwordArrayC4(int32_t index)
{
    // The original's cmp/jge is signed, so every negative index takes the read path. The address wraps modulo 2^32
    // (index * 4 is a 32-bit lea/imul result), so index 0x80000000 reads the dword at +0xc4 itself.
    if (index >= 9) return 0;
    uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)) + 0xc4u + static_cast<uint32_t>(index) * 4u;
    return *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(addr));
}

uint32_t cTSSGSystemServiceSub::GetServiceID()
{
    return 0x9a353ce8u;
}

uint32_t cTSSGSystemServiceSub::GetServicePriority()
{
    return 0xfff0bdc0u;
}

} // namespace nTSSG
