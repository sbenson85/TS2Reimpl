#include "hook/hooks.h"
#include "ts/cTSEffectsResource.h"

// Standalone shape (docs/standalone-shape.md): the resources are created by the COM director's factory 0xbd2f90 and by
// cTSEffectsResourceFactory's Create slot (0xbd2ed0, which inlines operator new and calls the constructor 0xbd2ba0): with
// the factory and constructor hooks every resource carries our vtables. The slot entries are adapters (a resource the game
// built, with those hooks off); the trigger serializers are free functions only Write / Read call.
namespace {

using A = cTSEffectsResourceAdapters;

const HookSpec kHooks[] = {
    {"cTSEffectsResource_Create", 0xbd2f90, 32, 0x7d20e21a, reinterpret_cast<void *>(&cTSEffectsResource::Create)},
    {"cTSEffectsResource_Construct", 0xbd2ba0, 32, 0x3e9da49e, reinterpret_cast<void *>(&A::Construct)},
    {"cTSEffectsResource_Destruct", 0xbd2da0, 32, 0x027aab33, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSEffectsResource_DeletingDestructor", 0xbd2e50, 32, 0xa3cc8da0, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSEffectsResource_QueryInterface", 0xbd3090, 32, 0xe29d1020, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSEffectsResource_AddRef", 0xbd2ea0, 8, 0x75794378, reinterpret_cast<void *>(&A::AddRef)},
    {"cTSEffectsResource_Release", 0xbd4010, 8, 0x4799062d, reinterpret_cast<void *>(&A::Release)},
    {"cTSEffectsResource_SetName", 0xbd4190, 32, 0x09c9ea41, reinterpret_cast<void *>(&A::SetName)},
    {"cTSEffectsResource_GetName", 0xbd3070, 18, 0x9a2635b1, reinterpret_cast<void *>(&A::GetName)},
    {"cTSEffectsResource_MergeInto", 0xbd2ec0, 8, 0xfe87df8b, reinterpret_cast<void *>(&A::MergeInto)},
    {"cTSEffectsResource_Write", 0xbd41c0, 32, 0xcb37ebe7, reinterpret_cast<void *>(&A::Write)},
    {"cTSEffectsResource_Read", 0xbd30f0, 32, 0xc7e9b50d, reinterpret_cast<void *>(&A::Read)},
    {"cTSEffectsResource_GetClassId", 0xbd3050, 6, 0x5a390f49, reinterpret_cast<void *>(&A::GetClassId)},
    {"cTSEffectsResource_WriteEffectTrigger", 0xbd8dc0, 32, 0xbbc53ac3, reinterpret_cast<void *>(&WriteEffectTrigger)},
    {"cTSEffectsResource_ReadEffectTrigger", 0xbd8a70, 32, 0x5d4e0e05, reinterpret_cast<void *>(&ReadEffectTrigger)},
};
REGISTER_HOOKS(kHooks);

} // namespace
