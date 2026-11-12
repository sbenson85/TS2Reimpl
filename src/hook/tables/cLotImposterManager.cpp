#include "hook/hooks.h"
#include "sg/cImposterImageWriter.h"
#include "sg/cLotImposterManager.h"
#include "sg/cLotImposterRaster.h"

// nTSSG::cLotImposterManager (lane 261108j, scenegraph row LT2b). The object keeps the image's vtables (the generation
// slots 5 / 6 / 7 / 9 are row LT2b2's, still the original's), so each slot of ours is hooked at its RVA, with the factory,
// LoadDefinitions (also called by slot 5) and the definitions' Clear. The constructor and destructor are reached only
// through the factory and the deleting destructor (in effect through callers, trapped: db/status/cLotImposterManager.jsonl).
// Bisect switch: every entry together (the factory builds our layout, which the hooked slots assume only as the original's).
namespace {

using nTSSG::cLotImposterManager;

const HookSpec kHooks[] = {
    {"nTSSG_cLotImposterManager_Create", 0x367790, 32, 0x8631f6d3,
     reinterpret_cast<void *>(&cLotImposterManager::Create)},
    {"nTSSG_cLotImposterManager_QueryInterface", 0x369530, 32, 0x10a74a11,
     MemberAddress(&cLotImposterManager::QueryInterface)},
    {"nTSSG_cLotImposterManager_Release", 0x3697e0, 21, 0x9c31248f, MemberAddress(&cLotImposterManager::Release)},
    {"nTSSG_cLotImposterManager_Init", 0x369140, 32, 0xdc1ea54a, MemberAddress(&cLotImposterManager::Init)},
    {"nTSSG_cLotImposterManager_Shutdown", 0x36c630, 32, 0x77f3ec2b, MemberAddress(&cLotImposterManager::Shutdown)},
    {"nTSSG_cLotImposterManager_DeletingDestructor", 0x3630f0, 32, 0x644ac0ec,
     MemberAddress(&cLotImposterManager::DeletingDestructor)},
    {"nTSSG_cLotImposterManager_DoMessage", 0x3668d0, 32, 0x61695adb,
     MemberAddress(&cLotImposterManager::MessageFace::DoMessage)},
    {"nTSSG_cLotImposterManager_LoadDefinitions", 0x369590, 32, 0x98030128,
     MemberAddress(&cLotImposterManager::LoadDefinitions)},
    {"nTSSG_cImposterDefinitions_Clear", 0x3f7a20, 32, 0x9b82da4a,
     MemberAddress(&nTSSG::cImposterDefinitions::Clear)},
    // Lane 261111q (row LT2b2's first batch): the image writer, the raster helpers and the manager's leaves.
    {"nTSSG_cImposterImageWriter_Construct", 0xb401c0, 32, 0xd43f669e,
     MemberAddress(&nTSSG::cImposterImageWriter::Construct)},
    {"nTSSG_cImposterImageWriter_Write", 0xb402a0, 32, 0xc91d0a79, MemberAddress(&nTSSG::cImposterImageWriter::Write)},
    {"nTSSG_cImposterImageWriter_WriteTop", 0xb40540, 22, 0x64cb6fea,
     MemberAddress(&nTSSG::cImposterImageWriter::WriteTop)},
    {"nTSSG_cImposterImageWriter_SetCodecFormat", 0xb40970, 32, 0x16cab31b,
     reinterpret_cast<void *>(&nTSSG::cImposterImageWriter::SetCodecFormat)},
    {"nTSSG_PaintColumn", 0x3637d0, 32, 0xb9bd25f5, reinterpret_cast<void *>(&nTSSG::PaintColumn)},
    {"nTSSG_PaintRow", 0x363a10, 32, 0x50b1f00e, reinterpret_cast<void *>(&nTSSG::PaintRow)},
    {"nTSSG_PaintRect", 0x363950, 32, 0xc73f2b1b, reinterpret_cast<void *>(&nTSSG::PaintRect)},
    {"nTSSG_PaintFrame", 0x363680, 32, 0x3e8ef779, reinterpret_cast<void *>(&nTSSG::PaintFrame)},
    {"nTSSG_FadeBorder", 0x363490, 32, 0x2797e4be, reinterpret_cast<void *>(&nTSSG::FadeBorder)},
    {"nTSSG_cLotImposterManager_ReleaseGenerationState", 0x369800, 32, 0xb65443c7,
     MemberAddress(&cLotImposterManager::ReleaseGenerationState)},
    {"nTSSG_cLotImposterManager_ReleaseGenerationServices", 0x3698f0, 32, 0x38750d34,
     MemberAddress(&cLotImposterManager::ReleaseGenerationServices)},
    {"nTSSG_cLotImposterManager_SliceCount", 0x368f50, 24, 0x72d8a902, MemberAddress(&cLotImposterManager::SliceCount)},
    {"nTSSG_cLotImposterManager_SliceCountF", 0x368f70, 32, 0x7f615328,
     MemberAddress(&cLotImposterManager::SliceCountF)},
    {"nTSSG_cLotImposterManager_HasSliceBorder", 0x366fc0, 24, 0x4dc2518a,
     MemberAddress(&cLotImposterManager::HasSliceBorder)},
    {"nTSSG_cLotImposterManager_SnapshotName", 0x368fa0, 32, 0x10ace54e,
     reinterpret_cast<void *>(&cLotImposterManager::SnapshotName)},
    // Lane 261111r (row LT2b2a): the generation path's leaves.
    {"nTSSG_cLotImposterManager_ComputeLotBounds", 0x36c7a0, 32, 0xaa9e54de,
     MemberAddress(&cLotImposterManager::ComputeLotBounds)},
    {"nTSSG_cLotImposterManager_CurrentLotName", 0x368de0, 32, 0xd48f6338,
     reinterpret_cast<void *>(&cLotImposterManager::CurrentLotName)},
    {"nTSSG_cLotImposterManager_IsFlat", 0x369460, 32, 0x7d16a327, MemberAddress(&cLotImposterManager::IsFlat)},
    {"nTSSG_cLotImposterManager_GroundHeightAt", 0x369070, 32, 0xa1c07d46,
     MemberAddress(&cLotImposterManager::GroundHeightAt)},
    {"nTSSG_cLotImposterManager_KeepHighest", 0x363120, 32, 0xc5966dcd,
     MemberAddress(&cLotImposterManager::KeepHighest)},
    {"nTSSG_cLotImposterManager_AddHeight", 0x363250, 32, 0x87f74419, MemberAddress(&cLotImposterManager::AddHeight)},
    {"nTSSG_cLotImposterManager_ClearAtlases", 0x369430, 32, 0x2425aad0,
     MemberAddress(&cLotImposterManager::ClearAtlases)},
    {"nTSSG_cLotImposterManager_ClassId", 0x3677f0, 6, 0x26cc862e,
     reinterpret_cast<void *>(&cLotImposterManager::ClassId)},
    {"nTSSG_LotImposterClassId", 0x36d100, 6, 0xeee6abd8, reinterpret_cast<void *>(&nTSSG::LotImposterClassId)},
    {"nTSSG_cObjectImposterCommand_GetName", 0x369500, 6, 0xb8aac813,
     MemberAddress(&nTSSG::cObjectImposterCommandName::GetName)},
    {"nTSSG_cLotImposterManager_ScaleImage", 0x36c690, 32, 0x0014a5cd,
     reinterpret_cast<void *>(&cLotImposterManager::ScaleImage)},
    {"nTSSG_cLotImposterManager_CopySliceBorders", 0x366fe0, 32, 0xc665bfd0,
     MemberAddress(&cLotImposterManager::CopySliceBorders)},
    {"nTSSG_cLotImposterManager_PlaceSlice", 0x3632b0, 32, 0x014bd0ee,
     MemberAddress(&cLotImposterManager::PlaceSlice)},
    {"nTSSG_cLotImposterManager_OpenLotResource", 0x368e20, 32, 0x36a01262,
     reinterpret_cast<void *>(&cLotImposterManager::OpenLotResource)},
    {"nTSSG_cLotImposterManager_SetImposterName", 0x36c510, 32, 0x0bbd65ab,
     MemberAddress(&cLotImposterManager::SetImposterName)},
    {"nTSSG_cLotImposterManager_LoadLotResources", 0x366460, 32, 0xb5c9b2bb,
     MemberAddress(&cLotImposterManager::LoadLotResources)},
    // Lane 261111s (LT2b2a's left rows): the design modes' constructor, ApplyDefinition, the generation services, the
    // description adder, the definitions command's Execute and the manager's slots 6 / 9.
    {"nTSSG_cDesignModes_Construct", 0x362950, 32, 0x89f1edd3, reinterpret_cast<void *>(&nTSSG::ConstructDesignModes)},
    {"nTSSG_cLotImposterManager_ApplyDefinition", 0x365410, 32, 0x0477f4d8,
     MemberAddress(&cLotImposterManager::ApplyDefinition)},
    {"nTSSG_cLotImposterManager_CreateGenerationServices", 0x364940, 32, 0xb0ee3b79,
     MemberAddress(&cLotImposterManager::CreateGenerationServices)},
    {"nTSSG_AddImposterDesc", 0x36b2b0, 32, 0xf525b87d, reinterpret_cast<void *>(&nTSSG::AddImposterDesc)},
    {"nTSSG_cObjectImposterCommand_Execute", 0x366f10, 32, 0x59246b3a,
     MemberAddress(&nTSSG::cObjectImposterCommand::Execute)},
    {"nTSSG_cLotImposterManager_LoadNamedLot", 0x369960, 32, 0x8925daee,
     MemberAddress(&cLotImposterManager::LoadNamedLot)},
    {"nTSSG_cLotImposterManager_LoadCurrentLot", 0x369a30, 32, 0x086cefc1,
     MemberAddress(&cLotImposterManager::LoadCurrentLot)},
    // Lane 261111u (row LT2a2a): the imposter definitions (the definitions command's and ApplyDefinition's callees).
    // Bisect switch: these eight together.
    {"nTSSG_cImposterDefinition_Construct", 0x3f72b0, 32, 0x76ca0aba,
     reinterpret_cast<void *>(&nTSSG::cImposterDefinition::Construct)},
    {"nTSSG_cImposterDefinition_Destruct", 0x3f73b0, 32, 0x564afccb,
     reinterpret_cast<void *>(&nTSSG::cImposterDefinition::Destruct)},
    {"nTSSG_cImposterDefinition_Assign", 0x3f7430, 32, 0xecc3e120,
     MemberAddress(&nTSSG::cImposterDefinition::operator=)},
    {"nTSSG_cImposterDefinition_AddEntry", 0x3f7500, 32, 0x8a6104da,
     MemberAddress(&nTSSG::cImposterDefinition::AddEntry)},
    {"nTSSG_cImposterDefinition_Find", 0x3f8300, 32, 0xe632d867, MemberAddress(&nTSSG::cImposterDefinition::Find)},
    {"nTSSG_cImposterDefinitions_Define2", 0x3f7890, 32, 0x31f2ffa0,
     MemberAddress(static_cast<void (nTSSG::cImposterDefinitions::*)(const char *, const char *)>(
         &nTSSG::cImposterDefinitions::Define))},
    {"nTSSG_cImposterDefinitions_Define6", 0x3f76e0, 32, 0x31f2ffa0,
     MemberAddress(static_cast<void (nTSSG::cImposterDefinitions::*)(const char *, const char *, const char *,
                                                                    const char *, const char *, const char *)>(
         &nTSSG::cImposterDefinitions::Define))},
    {"nTSSG_cImposterDefinitions_Map", 0x3f83d0, 32, 0x09a95b5c, MemberAddress(&nTSSG::cImposterDefinitions::Map)},
};

REGISTER_HOOKS(kHooks);

} // namespace
