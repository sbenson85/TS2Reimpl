#include "world/TSWorld.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/QueryInterfaces.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstdint>
#include <cstring>

namespace {

// AddRef through the object's own vtable (slot 1), as every original here does.
void AddRef(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
}

// The original's `neg/sbb/and` mask: object + offset when the object is non-null, otherwise zero.
void *OffsetOrNull(void *object, uint32_t offset)
{
    return object != nullptr ? static_cast<uint8_t *>(object) + offset : nullptr;
}

// Stores this + offset through ppv (masked) and AddRefs this. Callers have checked ppv for null.
bool StoreInterface(void *self, uint32_t offset, void **ppv)
{
    *ppv = OffsetOrNull(self, offset);
    AddRef(self);
    return true;
}

// Stores this through ppv (no mask) and AddRefs it.
bool StoreThis(void *self, void **ppv)
{
    *ppv = self;
    AddRef(self);
    return true;
}

// The cRZUnknown::QueryInterface the original tail-jumps to, on the given subobject.
bool UnknownQueryInterface(void *subobject, uint32_t riid, void **ppv)
{
    return reinterpret_cast<RZUnknownQueryInterface *>(subobject)->QueryInterfaceUnknown(riid, ppv);
}

} // namespace

bool __fastcall tsworld::WorldTerrainQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0xe4fda3d4) return StoreInterface(self, 0xc, ppv);
    if (riid == 0xeb589762) return StoreInterface(self, 8, ppv);
    return UnknownQueryInterface(self, riid, ppv);
}

bool __fastcall tsworld::WorldRoofQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0xab9406b1) return StoreInterface(self, 8, ppv);
    if (riid == 0xe4fda3d4) return StoreInterface(self, 0xc, ppv);
    return UnknownQueryInterface(self, riid, ppv);
}

bool __fastcall tsworld::WorldPoolQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x2c900fe6) return StoreInterface(self, 8, ppv);
    if (riid == 0xe4fda3d4) return StoreInterface(self, 0xc, ppv);
    return UnknownQueryInterface(self, riid, ppv);
}

bool __fastcall tsworld::WorldCurvedPoolQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x916da151) return StoreInterface(self, 8, ppv);
    if (riid == 0xe4fda3d4) return StoreInterface(self, 0xc, ppv);
    return UnknownQueryInterface(self, riid, ppv);
}

bool __fastcall tsworld::WallGraphQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 1 || riid == 0x2a284d16) return StoreThis(self, ppv);
    if (riid == 0xe4fda3d4) return StoreInterface(self, 4, ppv);
    return false;
}

uint32_t __fastcall tsworld::WallGraphRelease(void *self, void * /*edx*/)
{
    auto *count = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 8);
    uint32_t remaining = *count - 1;
    *count = remaining;
    if (remaining != 0) return remaining;
    // Last reference: the deleting destructor is slot 24 of the primary vtable, called with flag 1.
    VirtualCall<void(__thiscall *)(void *, uint32_t), 24>(self, 1);
    return 0;
}

bool __fastcall tsworld::WorldOutsideSupportQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0xb21f9b25) return StoreInterface(self, 8, ppv);
    return UnknownQueryInterface(self, riid, ppv);
}

bool __fastcall tsworld::PartitionManagerQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x6ca2a642 || riid == 0x6ca2a64e) return StoreThis(self, ppv);
    return UnknownQueryInterface(static_cast<uint8_t *>(self) + 4, riid, ppv);
}

bool __fastcall tsworld::ProxyManagerQueryInterface(void *self, void * /*edx*/, uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x8ce45d17) return StoreThis(self, ppv);
    return UnknownQueryInterface(static_cast<uint8_t *>(self) + 4, riid, ppv);
}

uint32_t __fastcall tsworld::WorldTerrainGetGZCLSID(void * /*self*/, void * /*edx*/)
{
    return 0x4b58975b;
}

uint32_t __fastcall tsworld::WallGraphGetGZCLSID(void * /*self*/, void * /*edx*/)
{
    return 0x0a284d0b;
}

void __fastcall tsworld::PartitionManagerSetPair(void *self, void * /*edx*/, uint32_t first, uint32_t second)
{
    auto *base = static_cast<uint8_t *>(self);
    std::memcpy(base + 0x18, &first, sizeof(first));
    std::memcpy(base + 0x24, &second, sizeof(second));
}

void __fastcall tsworld::PartitionManagerSetFour(void *self, void * /*edx*/, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    auto *base = static_cast<uint8_t *>(self);
    std::memcpy(base + 0x10, &a, sizeof(a));
    std::memcpy(base + 0x14, &b, sizeof(b));
    std::memcpy(base + 0x1c, &c, sizeof(c));
    std::memcpy(base + 0x20, &d, sizeof(d));
}

uint32_t __fastcall tsworld::WorldRoofGetGZCLSID(void * /*self*/, void * /*edx*/)
{
    return 0xab9406aa;
}

uint32_t __fastcall tsworld::WorldPoolGetGZCLSID(void * /*self*/, void * /*edx*/)
{
    return 0x0c900fdb;
}

uint32_t __fastcall tsworld::WorldCurvedPoolGetGZCLSID(void * /*self*/, void * /*edx*/)
{
    return 0x916da14d;
}
