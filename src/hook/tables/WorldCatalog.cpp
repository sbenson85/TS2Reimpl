#include "hook/hooks.h"
#include "world/WorldCatalog.h"

namespace {

const HookSpec kHooks[] = {
    {"WorldUtils_CatalogEntry", 0x7ef640, 26, 0x3729c896, reinterpret_cast<void *>(&worldcatalog::Entry)},
    {"FUN_a07ef660", 0x7ef660, 31, 0xa0d6b8ca, reinterpret_cast<void *>(&worldcatalog::FlagSlot94OrTrue)},
    {"FUN_a07ef680", 0x7ef680, 31, 0x0ded9c4c, reinterpret_cast<void *>(&worldcatalog::FlagSlot98OrTrue)},
    {"FUN_a07ef6a0", 0x7ef6a0, 31, 0x73143f80, reinterpret_cast<void *>(&worldcatalog::FlagSlot8cOrTrue)},
    {"FUN_a07ef6c0", 0x7ef6c0, 31, 0x202a9170, reinterpret_cast<void *>(&worldcatalog::FlagSlot90OrTrue)},
    {"FUN_a07ef6e0", 0x7ef6e0, 18, 0xb5fdca2b, reinterpret_cast<void *>(&worldcatalog::Exists)},
    {"FUN_a07ef700", 0x7ef700, 32, 0x2737dbfa, reinterpret_cast<void *>(&worldcatalog::IsKind1Or3AndSlot94)},
    {"WorldUtils_CatalogKindIs1Or3", 0x7ef760, 32, 0x53092b87, reinterpret_cast<void *>(&worldcatalog::IsKind1Or3)},
    {"WorldUtils_CatalogKindIs2", 0x7ef790, 19, 0x60278c22, reinterpret_cast<void *>(&worldcatalog::IsKind2)},
    {"WorldUtils_CatalogKindIs3", 0x7ef7b0, 19, 0x9023dd4a, reinterpret_cast<void *>(&worldcatalog::IsKind3)},
    {"FUN_a07ef7d0", 0x7ef7d0, 11, 0x51081b1a, reinterpret_cast<void *>(&worldcatalog::IdIs1d)},
    {"FUN_a07ef7e0", 0x7ef7e0, 31, 0x141558fc, reinterpret_cast<void *>(&worldcatalog::FlagSlotA0)},
    {"FUN_a07ef800", 0x7ef800, 14, 0xf8eefd08, reinterpret_cast<void *>(&worldcatalog::IdIs12d)},
    {"FUN_a07ef810", 0x7ef810, 32, 0x30498515, reinterpret_cast<void *>(&worldcatalog::IsKind0Or3)},
    {"FUN_a07ef840", 0x7ef840, 28, 0x57e5a794, reinterpret_cast<void *>(&worldcatalog::FlagSlot70)},
    {"FUN_a07ef860", 0x7ef860, 28, 0xce91e91a, reinterpret_cast<void *>(&worldcatalog::FlagSlot74)},
    {"FUN_a07ef8a0", 0x7ef8a0, 19, 0x319bde88, reinterpret_cast<void *>(&worldcatalog::Slot80Is1)},
    {"FUN_a07ef8c0", 0x7ef8c0, 19, 0x2b43e8b6, reinterpret_cast<void *>(&worldcatalog::Slot80Is4)},
    {"FUN_a07ef8e0", 0x7ef8e0, 19, 0x696765ce, reinterpret_cast<void *>(&worldcatalog::Slot80Is2)},
    {"FUN_a07ef920", 0x7ef920, 28, 0xb4db00aa, reinterpret_cast<void *>(&worldcatalog::FlagSlot68)},
    {"FUN_a07ef940", 0x7ef940, 28, 0x34d901b4, reinterpret_cast<void *>(&worldcatalog::FlagSlot6c)},
    {"FUN_a07ef990", 0x7ef990, 28, 0xdc321ca5, reinterpret_cast<void *>(&worldcatalog::FlagSlot64)},
    {"FUN_a07ef9b0", 0x7ef9b0, 29, 0xd95ba5b3, reinterpret_cast<void *>(&worldcatalog::Slot80)},
    {"WorldUtils_CatalogKind", 0x7ef9d0, 27, 0x76e744bd, reinterpret_cast<void *>(&worldcatalog::Kind)},
};
REGISTER_HOOKS(kHooks);

} // namespace
