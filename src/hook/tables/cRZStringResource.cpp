// cRZStringResource (lane LS, src/rz/cRZStringResource.h): hook-shaped. The game's four creators build it inline with
// the game's vtables, so the class's own slot bodies and the resource face's adjustor thunks are hooked at their RVAs.
#include "hook/hooks.h"
#include "rz/cRZStringResource.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZStringResource_QueryInterface", 0x9b63d0, 32, 0xd8230828, MemberAddress(&cRZStringResource::QueryInterface)},
    {"cRZStringResource_AddRef", 0x1eb500, 8, 0x64347bf3, MemberAddress(&cRZStringResource::AddRef)},
    {"cRZStringResource_Release", 0x1ee2e0, 8, 0x8238f813, MemberAddress(&cRZStringResource::Release)},
    {"cRZStringResource_vf33", 0x1eb120, 32, 0x9a8ecb0e, MemberAddress(&cRZStringResource::DeletingDestructor)},
    {"cRZStringResource_Resource_QueryInterface", 0x1ee218, 8, 0x72c917db,
     reinterpret_cast<void *>(&cRZStringResourceThunks::QueryInterface)},
    {"cRZStringResource_Resource_AddRef", 0x1eb508, 8, 0x8a1e2eb9, reinterpret_cast<void *>(&cRZStringResourceThunks::AddRef)},
    {"cRZStringResource_Resource_Release", 0x1ee2e8, 8, 0x8a1e2eb9,
     reinterpret_cast<void *>(&cRZStringResourceThunks::Release)},
    {"cRZStringResource_vf32_12", 0x1eb109, 8, 0x54a50e5a,
     reinterpret_cast<void *>(&cRZStringResourceThunks::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
