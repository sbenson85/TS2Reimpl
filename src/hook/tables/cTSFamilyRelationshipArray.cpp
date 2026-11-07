#include "hook/hooks.h"
#include "ts/cTSFamilyRelationshipArray.h"

// Standalone shape (docs/standalone-shape.md): the game creates every array through the constructor (0x4f2070, from the
// persist resource factory 0x4f25d0 and Create) and Create (0x4f26e0, the class factory function the COM director
// registers), so those two hooks give every object our vtables. The slot entries are adapters for the old entry points
// (cTSFamilyRelationshipArrayAdapters): reached only through direct calls (GetGZCLSID, from the COM director's
// constructor) or an object carrying the game's vtables (none while the constructor hook is on). Slots reached only
// through those vtables have no entry (lane GJ): their status entries are "in_effect": "callers", trapped under
// Hooks.cTSFamilyRelationshipArray_Construct=1; the slots left keep the adapter their byte match ties through.
namespace {

using A = cTSFamilyRelationshipArrayAdapters;

const HookSpec kHooks[] = {
    {"cTSFamilyRelationshipArray_Construct", 0x4f2070, 32, 0x54c06109, reinterpret_cast<void *>(&A::Construct)},
    {"cTSFamilyRelationshipArray_Create", 0x4f26e0, 32, 0xb1677347, reinterpret_cast<void *>(&cTSFamilyRelationshipArray::Create)},
    {"cTSFamilyRelationshipArray_QueryInterface", 0x4f2f00, 32, 0x7be7d5eb, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSFamilyRelationshipArray_Release", 0x4f3770, 32, 0x352b0856, reinterpret_cast<void *>(&A::Release)},
    {"cTSFamilyRelationshipArray_vf4", 0x4f38a0, 32, 0x3ce5ab97, reinterpret_cast<void *>(&A::RemoveRelationship)},
    {"cTSFamilyRelationshipArray_vf6", 0x4f2e80, 5, 0x83867cd9, reinterpret_cast<void *>(&A::SetDirty)},
    {"cTSFamilyRelationshipArray_vf17", 0x4f3d90, 32, 0x12b0f7c6, reinterpret_cast<void *>(&A::Clear)},
    {"cTSFamilyRelationshipArray_GetGZCLSID", 0x4f2770, 6, 0xb4fe63d3, reinterpret_cast<void *>(&A::GetGZCLSID)},
    {"SimIdVector_Destroy", 0x4f2110, 32, 0x4ff9eaf1, MemberAddress(&SimIdVectorTidy::Destroy)},
};
REGISTER_HOOKS(kHooks);

} // namespace
