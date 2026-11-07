#include "hook/hooks.h"
#include "ts/cTSCompDescription.h"

// Standalone shape (docs/standalone-shape.md): the factories give the effects code's descriptions our vtables; the parser
// commands still build some with the game's (cGroupBrushCommand / cGroupSoundCommand inline the constructor), so the
// class-specific slots keep their adapters. The brush and water instances share their copy (0xbf6820) and deleting
// destructor (0xbf6770, thunk 0xbf6760) bodies: only those two classes' vtables reference them, and the brush adapters do
// the same for a water object (the copy compares the types through the vtables and moves 0x28 bytes, both 0x34-byte
// objects). Slots 0 / 1 / 3 / 6 / 7 / 8 elsewhere are bodies folded with unrelated classes: never hooked.
namespace {

using Brush = cTSCompDescriptionAdapters<cTSBrushComponentDescription>;
using Scrubber = cTSCompDescriptionAdapters<cTSScrubberComponentDescription>;
using Sound = cTSCompDescriptionAdapters<cTSSoundComponentDescription>;
using Game = cTSCompDescriptionAdapters<cTSGameComponentDescription>;
using Water = cTSCompDescriptionAdapters<cTSWaterComponentDescription>;
using Sequence = cTSCompDescriptionAdapters<cTSSequenceComponentDescription>;
using Model = cTSCompDescriptionAdapters<cTSModelComponentDescription>;
using Camera = cTSCompDescriptionAdapters<cTSCameraComponentDescription>;
using Screen = cTSCompDescriptionAdapters<cTSScreenComponentDescription>;
using Light = cTSCompDescriptionAdapters<cTSLightComponentDescription>;
using Decal = cTSCompDescriptionAdapters<cTSDecalComponentDescription>;

const HookSpec kHooks[] = {
    // brush (type 4)
    {"cTSBrushDescription_Create", 0xbf8af0, 32, 0x41ad39c4, reinterpret_cast<void *>(&cTSBrushComponentDescription::Create)},
    {"cTSBrushDescription_ConstructDescription", 0xbf8600, 32, 0xabf6ba70, reinterpret_cast<void *>(&Brush::ConstructDescription)},
    {"cTSBrushDescription_CopyTo", 0xbf6820, 32, 0xde74102f, reinterpret_cast<void *>(&Brush::CopyTo)},
    {"cTSBrushDescription_Read", 0xbf89a0, 32, 0xf51a4359, reinterpret_cast<void *>(&Brush::Read)},
    {"cTSBrushDescription_Write", 0xbf8b70, 32, 0x54214a15, reinterpret_cast<void *>(&Brush::Write)},
    {"cTSBrushDescription_DeletingDestructor", 0xbf6770, 32, 0x8d051fbe, reinterpret_cast<void *>(&Brush::DeletingDestructor)},
    {"cTSBrushDescription_ReadDescription", 0xbf89e0, 32, 0xd8081e2c, reinterpret_cast<void *>(&cTSBrushDescription::Read)},
    {"cTSBrushDescription_WriteDescription", 0xbf8ba0, 32, 0x79d58874, reinterpret_cast<void *>(&cTSBrushDescription::Write)},
    // scrubber (type 5)
    {"cTSScrubberDescription_Create", 0xbfa140, 32, 0xef3fb843, reinterpret_cast<void *>(&cTSScrubberComponentDescription::Create)},
    {"cTSScrubberDescription_ConstructDescription", 0xbf9f80, 32, 0x025389c3, reinterpret_cast<void *>(&Scrubber::ConstructDescription)},
    {"cTSScrubberDescription_CopyTo", 0xbfa020, 32, 0x45e55239, reinterpret_cast<void *>(&Scrubber::CopyTo)},
    {"cTSScrubberDescription_Read", 0xbfa060, 32, 0xf51a4359, reinterpret_cast<void *>(&Scrubber::Read)},
    {"cTSScrubberDescription_Write", 0xbfa1c0, 32, 0x54214a15, reinterpret_cast<void *>(&Scrubber::Write)},
    {"cTSScrubberDescription_DeletingDestructor", 0xbf9ff0, 32, 0xc683e106, reinterpret_cast<void *>(&Scrubber::DeletingDestructor)},
    {"cTSScrubberDescription_ReadDescription", 0xbfa0a0, 32, 0x90d2d876, reinterpret_cast<void *>(&cTSScrubberDescription::Read)},
    {"cTSScrubberDescription_WriteDescription", 0xbfa1f0, 32, 0x7b651971, reinterpret_cast<void *>(&cTSScrubberDescription::Write)},
    // sound (type 7)
    {"cTSSoundDescription_Create", 0xbf25d0, 32, 0xf8e0bc99, reinterpret_cast<void *>(&cTSSoundComponentDescription::Create)},
    {"cTSSoundDescription_ConstructDescription", 0xbf2110, 32, 0xb9c13619, reinterpret_cast<void *>(&Sound::ConstructDescription)},
    {"cTSSoundDescription_CreateEffect", 0xbf2530, 13, 0x341f9968, reinterpret_cast<void *>(&Sound::CreateEffect)},
    {"cTSSoundDescription_CopyTo", 0xbf2170, 32, 0x463350a2, reinterpret_cast<void *>(&Sound::CopyTo)},
    {"cTSSoundDescription_Read", 0xbf2540, 32, 0xf51a4359, reinterpret_cast<void *>(&Sound::Read)},
    {"cTSSoundDescription_Write", 0xbf26f0, 32, 0x54214a15, reinterpret_cast<void *>(&Sound::Write)},
    {"cTSSoundDescription_DeletingDestructor", 0xbf2140, 32, 0xf105dd77, reinterpret_cast<void *>(&Sound::DeletingDestructor)},
    {"cTSSoundDescription_ReadDescription", 0xbf2580, 32, 0x3c67977a, reinterpret_cast<void *>(&cTSSoundDescription::Read)},
    {"cTSSoundDescription_WriteDescription", 0xbf2720, 32, 0x25bbe07f, reinterpret_cast<void *>(&cTSSoundDescription::Write)},
    // game (type 9)
    {"cTSGameDescription_Create", 0xbfa3a0, 32, 0xba86af4d, reinterpret_cast<void *>(&cTSGameComponentDescription::Create)},
    {"cTSGameDescription_GetType", 0xbfa420, 6, 0x4826c1c0, reinterpret_cast<void *>(&Game::GetType)},
    {"cTSGameDescription_CopyTo", 0xbfa2e0, 32, 0x906ab3bf, reinterpret_cast<void *>(&Game::CopyTo)},
    {"cTSGameDescription_Read", 0xbfa310, 32, 0xf51a4359, reinterpret_cast<void *>(&Game::Read)},
    {"cTSGameDescription_Write", 0xbfa430, 32, 0x54214a15, reinterpret_cast<void *>(&Game::Write)},
    {"cTSGameDescription_DeletingDestructor", 0xbfa2b0, 32, 0x7ff60e61, reinterpret_cast<void *>(&Game::DeletingDestructor)},
    {"cTSGameDescription_ReadDescription", 0xbfa350, 32, 0x580b527e, reinterpret_cast<void *>(&cTSGameDescription::Read)},
    {"cTSGameDescription_WriteDescription", 0xbfa460, 32, 0x7b651971, reinterpret_cast<void *>(&cTSGameDescription::Write)},
    // water (type 12)
    {"cTSWaterDescription_Create", 0xbf6f90, 32, 0xb01dfa15, reinterpret_cast<void *>(&cTSWaterComponentDescription::Create)},
    {"cTSWaterDescription_Construct", 0xbf66f0, 32, 0x4d98aedc, reinterpret_cast<void *>(&Water::Construct)},
    {"cTSWaterDescription_CreateEffect", 0xbf6c30, 13, 0x245e04d4, reinterpret_cast<void *>(&Water::CreateEffect)},
    {"cTSWaterDescription_Read", 0xbf6eb0, 32, 0xf51a4359, reinterpret_cast<void *>(&Water::Read)},
    {"cTSWaterDescription_Write", 0xbf7170, 32, 0x54214a15, reinterpret_cast<void *>(&Water::Write)},
    {"cTSWaterDescription_ReadDescription", 0xbf6ef0, 32, 0x50f6bb2d, reinterpret_cast<void *>(&cTSWaterDescription::Read)},
    {"cTSWaterDescription_WriteDescription", 0xbf71a0, 32, 0xd8a64432, reinterpret_cast<void *>(&cTSWaterDescription::Write)},
    // sequence (type 6)
    {"cTSSequenceDescription_Create", 0xbf4370, 32, 0xf6b02a19, reinterpret_cast<void *>(&cTSSequenceComponentDescription::Create)},
    {"cTSSequenceDescription_ConstructDescription", 0xbf38a0, 32, 0xda4aafd1, reinterpret_cast<void *>(&Sequence::ConstructDescription)},
    {"cTSSequenceDescription_CreateEffect", 0xbf3fe0, 13, 0x49a7023e, reinterpret_cast<void *>(&Sequence::CreateEffect)},
    {"cTSSequenceDescription_CopyTo", 0xbf3b00, 32, 0x5bca6bb7, reinterpret_cast<void *>(&Sequence::CopyTo)},
    {"cTSSequenceDescription_Read", 0xbf4270, 32, 0xf51a4359, reinterpret_cast<void *>(&Sequence::Read)},
    {"cTSSequenceDescription_Write", 0xbf4520, 32, 0x54214a15, reinterpret_cast<void *>(&Sequence::Write)},
    {"cTSSequenceDescription_DeletingDestructor", 0xbf3a50, 32, 0x796afbbc, reinterpret_cast<void *>(&Sequence::DeletingDestructor)},
    {"cTSSequenceDescription_ReadDescription", 0xbf42b0, 32, 0x3f4641dd, reinterpret_cast<void *>(&cTSSequenceDescription::Read)},
    {"cTSSequenceDescription_WriteDescription", 0xbf4550, 32, 0xccff8c20, reinterpret_cast<void *>(&cTSSequenceDescription::Write)},
    {"cTSSequenceEntry_Read", 0xbf3930, 32, 0x5e642610, reinterpret_cast<void *>(&cTSSequenceEntry::Read)},
    {"cTSSequenceEntry_Write", 0xbf3a10, 32, 0x235a9587, reinterpret_cast<void *>(&cTSSequenceEntry::Write)},
    // model (type 10)
    {"cTSModelDescription_Create", 0xbf3050, 32, 0xeb164e61, reinterpret_cast<void *>(&cTSModelComponentDescription::Create)},
    {"cTSModelDescription_Construct", 0xbf2780, 32, 0xfd145f11, reinterpret_cast<void *>(&Model::Construct)},
    {"cTSModelDescription_CreateEffect", 0xbf2d40, 13, 0x916c6c5b, reinterpret_cast<void *>(&Model::CreateEffect)},
    {"cTSModelDescription_CopyTo", 0xbf2990, 32, 0x1ef6c5e6, reinterpret_cast<void *>(&Model::CopyTo)},
    {"cTSModelDescription_Read", 0xbf2f10, 32, 0xf51a4359, reinterpret_cast<void *>(&Model::Read)},
    {"cTSModelDescription_Write", 0xbf3230, 32, 0x54214a15, reinterpret_cast<void *>(&Model::Write)},
    {"cTSModelDescription_DeletingDestructor", 0xbf2860, 32, 0x72dc3415, reinterpret_cast<void *>(&Model::DeletingDestructor)},
    {"cTSModelDescription_ReadDescription", 0xbf2f50, 32, 0x5d7a6c08, reinterpret_cast<void *>(&cTSModelDescription::Read)},
    {"cTSModelDescription_WriteDescription", 0xbf3260, 32, 0xdd2f7742, reinterpret_cast<void *>(&cTSModelDescription::Write)},
    // camera (type 8, lane IW); GetType is folded (shared_vf)
    {"cTSCameraDescription_Create", 0xbf6530, 32, 0x96455d96, reinterpret_cast<void *>(&cTSCameraComponentDescription::Create)},
    {"cTSCameraDescription_ConstructDescription", 0xbf4610, 32, 0x0cf57ed7, reinterpret_cast<void *>(&Camera::ConstructDescription)},
    {"cTSCameraDescription_DestroyDescription", 0xbf4720, 32, 0xe1a7b95b, reinterpret_cast<void *>(&Camera::DestroyDescription)},
    {"cTSCameraDescription_MoveDescription", 0xbf4a50, 32, 0xcd287855, reinterpret_cast<void *>(&Camera::MoveDescription)},
    {"cTSCameraDescription_CopyDescription", 0xbf4d20, 32, 0xe0ba3fcb, reinterpret_cast<void *>(&Camera::CopyDescription)},
    {"cTSCameraDescription_CreateEffect", 0xbf5700, 13, 0xb0d16068, reinterpret_cast<void *>(&Camera::CreateEffect)},
    {"cTSCameraDescription_CopyTo", 0xbf4fb0, 32, 0xff70b735, reinterpret_cast<void *>(&Camera::CopyTo)},
    {"cTSCameraDescription_Read", 0xbf6110, 32, 0xf51a4359, reinterpret_cast<void *>(&Camera::Read)},
    {"cTSCameraDescription_Write", 0xbf65c0, 32, 0x54214a15, reinterpret_cast<void *>(&Camera::Write)},
    {"cTSCameraDescription_GetSize", 0xbf5710, 6, 0x39437a28, reinterpret_cast<void *>(&Camera::GetSize)},
    {"cTSCameraDescription_DeletingDestructor", 0xbf4ef0, 32, 0x0ca58108, reinterpret_cast<void *>(&Camera::DeletingDestructor)},
    {"cTSCameraDescription_ReadDescription", 0xbf6150, 32, 0x47d66d3f, reinterpret_cast<void *>(&cTSCameraDescription::Read)},
    {"cTSCameraDescription_WriteDescription", 0xbf65f0, 32, 0x107ef93d, reinterpret_cast<void *>(&cTSCameraDescription::Write)},
    // screen (type 11, lane IW); GetType is folded (shared_vf)
    {"cTSScreenDescription_Create", 0xbf8360, 32, 0x02b880ed, reinterpret_cast<void *>(&cTSScreenComponentDescription::Create)},
    {"cTSScreenDescription_ConstructDescription", 0xbf7280, 32, 0x4b09bbb7, reinterpret_cast<void *>(&Screen::ConstructDescription)},
    {"cTSScreenDescription_DestroyDescription", 0xbf7320, 32, 0x00b90f93, reinterpret_cast<void *>(&Screen::DestroyDescription)},
    {"cTSScreenDescription_CopyDescription", 0xbf73c0, 32, 0x3672f786, reinterpret_cast<void *>(&Screen::CopyDescription)},
    {"cTSScreenDescription_CreateEffect", 0xbf7bf0, 13, 0x64346927, reinterpret_cast<void *>(&Screen::CreateEffect)},
    {"cTSScreenDescription_CopyTo", 0xbf7520, 32, 0xff70b735, reinterpret_cast<void *>(&Screen::CopyTo)},
    {"cTSScreenDescription_Read", 0xbf8150, 32, 0xf51a4359, reinterpret_cast<void *>(&Screen::Read)},
    {"cTSScreenDescription_Write", 0xbf8520, 32, 0x54214a15, reinterpret_cast<void *>(&Screen::Write)},
    {"cTSScreenDescription_GetSize", 0xbf7c00, 6, 0x88a5caba, reinterpret_cast<void *>(&Screen::GetSize)},
    {"cTSScreenDescription_DeletingDestructor", 0xbf7460, 32, 0x2d073a9d, reinterpret_cast<void *>(&Screen::DeletingDestructor)},
    {"cTSScreenDescription_ReadDescription", 0xbf8190, 32, 0xe82180e9, reinterpret_cast<void *>(&cTSScreenDescription::Read)},
    {"cTSScreenDescription_WriteDescription", 0xbf8550, 32, 0x57d995ed, reinterpret_cast<void *>(&cTSScreenDescription::Write)},
    // light (type 13, lane IW)
    {"cTSLightDescription_Create", 0xbf9370, 32, 0x0b5ca53c, reinterpret_cast<void *>(&cTSLightComponentDescription::Create)},
    {"cTSLightDescription_Construct", 0xbf8c20, 32, 0xc1adb134, reinterpret_cast<void *>(&Light::Construct)},
    {"cTSLightDescription_DestroyDescription", 0xbf8cd0, 32, 0x52118f67, reinterpret_cast<void *>(&Light::DestroyDescription)},
    {"cTSLightDescription_CopyDescription", 0xbf8d80, 32, 0x3672f786, reinterpret_cast<void *>(&Light::CopyDescription)},
    {"cTSLightDescription_GetType", 0xbf9560, 6, 0xbda66700, reinterpret_cast<void *>(&Light::GetType)},
    {"cTSLightDescription_CreateEffect", 0xbf9140, 13, 0xb6e50050, reinterpret_cast<void *>(&Light::CreateEffect)},
    {"cTSLightDescription_CopyTo", 0xbf8ee0, 32, 0xff70b735, reinterpret_cast<void *>(&Light::CopyTo)},
    {"cTSLightDescription_Read", 0xbf9240, 32, 0xf51a4359, reinterpret_cast<void *>(&Light::Read)},
    {"cTSLightDescription_Write", 0xbf9570, 32, 0x54214a15, reinterpret_cast<void *>(&Light::Write)},
    {"cTSLightDescription_GetSize", 0xbf9150, 6, 0x2db5b039, reinterpret_cast<void *>(&Light::GetSize)},
    {"cTSLightDescription_DeletingDestructor", 0xbf8e20, 32, 0x1f4b41d9, reinterpret_cast<void *>(&Light::DeletingDestructor)},
    {"cTSLightDescription_ReadDescription", 0xbf9280, 32, 0xae60c321, reinterpret_cast<void *>(&cTSLightDescription::Read)},
    {"cTSLightDescription_WriteDescription", 0xbf95a0, 32, 0xc1cb4756, reinterpret_cast<void *>(&cTSLightDescription::Write)},
    // decal (type 3, lane IW; slot 0 is the folded 0x18c4d0, the +4 face's thunk 0xbeb88e exists only in our vtables)
    {"cTSDecalDescription_Create", 0xbecb10, 32, 0xf66ab67a, reinterpret_cast<void *>(&cTSDecalComponentDescription::Create)},
    {"cTSDecalDescription_ConstructDescription", 0xbeb2c0, 32, 0x8db82e07, reinterpret_cast<void *>(&Decal::ConstructDescription)},
    {"cTSDecalDescription_DestroyDescription", 0xbeb410, 32, 0x1ef581f8, reinterpret_cast<void *>(&Decal::DestroyDescription)},
    {"cTSDecalDescription_MoveDescription", 0xbeb5a0, 32, 0x3d9964aa, reinterpret_cast<void *>(&Decal::MoveDescription)},
    {"cTSDecalDescription_CopyDescription", 0xbeb760, 32, 0x2b307c2e, reinterpret_cast<void *>(&Decal::CopyDescription)},
    {"cTSDecalDescription_CreateEffect", 0xbec240, 13, 0x8104e7c3, reinterpret_cast<void *>(&Decal::CreateEffect)},
    {"cTSDecalDescription_CopyTo", 0xbeb960, 32, 0xff70b735, reinterpret_cast<void *>(&Decal::CopyTo)},
    {"cTSDecalDescription_Read", 0xbec8a0, 32, 0xf51a4359, reinterpret_cast<void *>(&Decal::Read)},
    {"cTSDecalDescription_Write", 0xbecce0, 32, 0x54214a15, reinterpret_cast<void *>(&Decal::Write)},
    {"cTSDecalDescription_GetSize", 0xbec250, 6, 0xa440cf60, reinterpret_cast<void *>(&Decal::GetSize)},
    {"cTSDecalDescription_DeletingDestructor", 0xbeb8a0, 32, 0xd1c94096, reinterpret_cast<void *>(&Decal::DeletingDestructor)},
    {"cTSDecalDescription_ReadDescription", 0xbec8e0, 32, 0x445f03a3, reinterpret_cast<void *>(&cTSDecalDescription::Read)},
    {"cTSDecalDescription_WriteDescription", 0xbecd10, 32, 0x83cf3681, reinterpret_cast<void *>(&cTSDecalDescription::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
