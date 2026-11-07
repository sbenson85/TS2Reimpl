#pragma once
#include <cstdint>

// BCON constant tables (the game's behaviour-constant resources, resource type 0x42434f4e): the tuning numbers the
// autonomy code reads. RVAs are 1.18.0.187.

constexpr uint32_t kResourceTypeBcon = 0x42434f4e;
constexpr uint32_t kResourceGroupDefault = 0x7fd46cd0;
constexpr uint32_t kBconTableInterfaceId = 0x5b267c89;

// The 16-byte resource key the resource service takes (type, group, instance low and high words).
struct ResourceKey {
    uint32_t mType;
    uint32_t mGroup;
    uint32_t mInstance;
    uint32_t mInstanceHigh;
};
static_assert(sizeof(ResourceKey) == 16, "ResourceKey layout");

// 0x54a770 (__cdecl, three arguments): asks the resource service (GetCachedSingletonC) for the BCON resource `instance` in
// the default group, as interface `interfaceId`, stored at *table. Returns the service's answer (al).
uint8_t GetBconTable(uint32_t instance, uint32_t interfaceId, void **table);

// 0x54a6b0 (__cdecl, three arguments): reads constant `index` of BCON resource `instance` into *value. Returns 1 when the
// table exists and the index is in range (a negative or too large index leaves *value alone), else 0.
uint8_t GetBconValue(uint32_t instance, int32_t index, uint16_t *value);
