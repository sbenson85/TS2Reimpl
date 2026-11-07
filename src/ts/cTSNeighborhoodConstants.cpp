#include "ts/cTSNeighborhoodConstants.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "ts/cTSFloatConstants.h"

#include <cstdint>
#include <new>

namespace {

template <typename T> T &At(const void *base, uintptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

constexpr uintptr_t kLoadedField = 0x0c;
constexpr uintptr_t kValueField = 0x10; // ids 0..2
constexpr uintptr_t kFloatField = 0x1c; // ids 3..5

void EnsureLoaded(cTSNeighborhoodConstants *self)
{
    if (At<uint8_t>(self, kLoadedField) == 0) VirtualCall<void(__thiscall *)(void *), 3>(self);
}

} // namespace

void cTSConstantsClient::Destruct()
{
    At<const void *>(this, 0) = GameAddress(kVTable);
}

void *cTSConstantsClient::GetSegment()
{
    return RawSlot<6, void *>(RawSlot<24, void *>(GetCachedSingleton()));
}

void cTSNeighborhoodConstants::Load()
{
    void *const segment = RawSlot<0, void *>(this);
    const uint16_t id = static_cast<uint16_t>(RawSlot<1>(this));
    cTSFloatConstants constants;
    if (segment != nullptr) constants.Load(segment, id);

    // The values at +0x10..+0x18 are whole numbers (a float truncated), the three factors floats.
    At<int32_t>(this, 0x10) = static_cast<int32_t>(constants.GetFloat("money for new family", 20000.0f, true));
    At<int32_t>(this, 0x14) = static_cast<int32_t>(constants.GetFloat("money per sim for college households", 2000.0f, true));
    At<int32_t>(this, 0x18) = static_cast<int32_t>(constants.GetFloat("minimum grade for college admission", 2.0f, true));
    At<float>(this, 0x1c) = constants.GetFloat("ResidentialLotInUniversity_AsRentalPropertyDepositFactor", 0.25f, true);
    At<float>(this, 0x20) = constants.GetFloat("BusinessRankValue_RankMultiplier", 1.0f, true);
    At<float>(this, 0x24) = constants.GetFloat("BusinessRankValue_EmployeeWageMultiplier", 1.0f, true);

    // The game globals the neighbourhood code reads (sims, lots and upkeep).
    *GameAddress<int32_t>(0x10daa30) = static_cast<int32_t>(constants.GetFloat("new family start hour", 7.0f, true));
    *GameAddress<float>(0x10daa34) = constants.GetFloat("perfect family friend count", 15.0f, true);
    *GameAddress<float>(0x10daa38) = constants.GetFloat("weight of lot size in overall score", 3.0f, true);
    *GameAddress<int32_t>(0x10daa3c) = static_cast<int32_t>(constants.GetFloat("fill value for layout history", 200.0f, true));
    *GameAddress<float>(0x10daa40) = constants.GetFloat("yard score multiplier", 0.33f, true);
    *GameAddress<int32_t>(0x10daa44) = static_cast<int32_t>(constants.GetFloat("upkeep penalty per dirty object", 6.0f, true));
    *GameAddress<int32_t>(0x10daa48) = static_cast<int32_t>(constants.GetFloat("upkeep penalty per broken object", 15.0f, true));

    void *const singleton = GetCachedSingletonC();
    RawSlot<48, uint32_t, float>(singleton, *GameAddress<float>(0x10daa34));
    RawSlot<50, uint32_t, int32_t>(singleton, *GameAddress<int32_t>(0x10daa44));
    RawSlot<52, uint32_t, int32_t>(singleton, *GameAddress<int32_t>(0x10daa48));

    if (constants.IsDirty() && segment != nullptr && RawSlot<9, bool>(segment)) constants.Save(segment, id);
    At<uint8_t>(this, kLoadedField) = 1;
}

cTSNeighborhoodConstants *cTSNeighborhoodConstants::DeletingDestructor(uint32_t flags)
{
    At<const void *>(this, 0) = GameAddress(kVTable);
    reinterpret_cast<cTSConstantsClient *>(this)->Destruct();
    if (flags & 1) ::operator delete(this, kObjectSize);
    return this;
}

bool cTSNeighborhoodConstants::GetFloat(uint32_t id, float *out)
{
    EnsureLoaded(this);
    if (id < 3 || id > 5) return false;
    // Copied as bits (the original moves them through movss, which changes no value).
    *reinterpret_cast<uint32_t *>(out) = At<uint32_t>(this, kFloatField + 4 * (id - 3));
    return true;
}

bool cTSNeighborhoodConstants::GetValue(uint32_t id, uint32_t *out)
{
    EnsureLoaded(this);
    if (id > 2) return false;
    *out = At<uint32_t>(this, kValueField + 4 * id);
    return true;
}
