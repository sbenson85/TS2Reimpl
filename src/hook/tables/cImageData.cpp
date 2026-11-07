#include "hook/hooks.h"
#include "sg/cImageData.h"

namespace {

using nGZSceneGraph::cImageData;
using nGZSceneGraph::cImageDataAdapters;
using nGZSceneGraph::cImageDataSubobject;

// The whole class (src/sg/cImageData.cpp). The factory and the constructor make every new image ours (the derived
// cTSTextureRecipeImageData's constructor builds its base part through 0xaafd60), so the class's own vtables are no
// longer installed; the bodies cTSTextureRecipeImageData's vtables name and those the game calls directly keep their
// hooks, through adapters. The vf4 body is shared by slots 4 and 5, so one entry covers both. vf26, vf27, vf28, vf8 and
// vf22 share their first 32 bytes, so their CRCs match.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cImageData_Create", 0xab1590, 32, 0x6e324402, reinterpret_cast<void *>(&cImageData::Create)},
    {"nGZSceneGraph_cImageData_Construct", 0xaafd60, 32, 0x2006b016, const_cast<uint8_t *>(&cImageData_Constructor)},
    {"nGZSceneGraph_cImageData_Destruct", 0xab0070, 32, 0x5727ab56, reinterpret_cast<void *>(&cImageDataAdapters::Destruct)},
    {"nGZSceneGraph_cImageData_QueryInterface", 0xab2330, 32, 0x8f996c8a,
     reinterpret_cast<void *>(&cImageDataAdapters::QueryInterface)},
    {"nGZSceneGraph_cImageData_vf3", 0xab1390, 32, 0x7a72156b, reinterpret_cast<void *>(&cImageDataAdapters::Clone)},
    {"nGZSceneGraph_cImageData_vf6", 0xab1890, 32, 0xe4ded0a8, reinterpret_cast<void *>(&cImageDataAdapters::Init)},
    {"nGZSceneGraph_cImageData_BuildLevels", 0xab1960, 32, 0xfe1c1121, reinterpret_cast<void *>(&cImageData::BuildLevels)},
    {"nGZSceneGraph_cImageData_MipChainBytes", 0xab0250, 32, 0x806b670c,
     reinterpret_cast<void *>(&cImageData::MipChainBytes)},
    {"nGZSceneGraph_cImageData_GetTypeInfo", 0xab3980, 32, 0xe2fbad47,
     reinterpret_cast<void *>(&cImageDataAdapters::GetTypeInfo)},
    {"nGZSceneGraph_cImageData_Read", 0xab23d0, 32, 0xb1febaa0, reinterpret_cast<void *>(&cImageDataAdapters::Read)},
    {"nGZSceneGraph_cImageData_ReadLevels", 0xab2da0, 32, 0x50705921,
     reinterpret_cast<void *>(&cImageDataAdapters::ReadLevels)},
    {"nGZSceneGraph_cImageData_Write", 0xab3b20, 32, 0xb312df6e, reinterpret_cast<void *>(&cImageDataAdapters::Write)},
    {"nGZSceneGraph_cImageData_vf33", 0xab2070, 32, 0xf8b21b20, reinterpret_cast<void *>(&cImageDataAdapters::PixelFormat)},
    {"nGZSceneGraph_cImageData_vf4", 0xab0330, 19, 0xe74c13ff, reinterpret_cast<void *>(&cImageDataAdapters::GetResource)},
    {"nGZSceneGraph_cImageData_vf36", 0xab03b0, 14, 0x5070c773, reinterpret_cast<void *>(&cImageDataAdapters::GetEntryScale)},
    {"nGZSceneGraph_cImageData_vf12", 0xab3340, 24, 0x39ac10b6, reinterpret_cast<void *>(&cImageDataAdapters::SetEntryScale)},
    {"nGZSceneGraph_cImageData_vf37", 0xab1870, 21, 0x64f7eb05, reinterpret_cast<void *>(&cImageDataAdapters::GetNameText)},
    {"nGZSceneGraph_cImageData_vf13", 0xab3360, 32, 0x38e771ac, reinterpret_cast<void *>(&cImageDataAdapters::SetName)},
    {"nGZSceneGraph_cImageData_vf31", 0xab1c20, 22, 0xf8ac2ad0, reinterpret_cast<void *>(&cImageDataAdapters::LevelCount)},
    {"nGZSceneGraph_cImageData_vf25", 0xab1c40, 24, 0x8a8cf424, reinterpret_cast<void *>(&cImageDataAdapters::EntryCount)},
    {"nGZSceneGraph_cImageData_vf35", 0xab32d0, 24, 0x8ad66631, reinterpret_cast<void *>(&cImageDataAdapters::GetEntryColour)},
    {"nGZSceneGraph_cImageData_vf11", 0xab3950, 26, 0x0128c733, reinterpret_cast<void *>(&cImageDataAdapters::SetEntryColour)},
    {"nGZSceneGraph_cImageData_vf26", 0xab3ae0, 32, 0x3fa4ba34, reinterpret_cast<void *>(&cImageDataAdapters::LevelWidth)},
    {"nGZSceneGraph_cImageData_vf27", 0xab1830, 32, 0x3fa4ba34, reinterpret_cast<void *>(&cImageDataAdapters::LevelHeight)},
    {"nGZSceneGraph_cImageData_vf28", 0xab1c60, 32, 0x3fa4ba34, reinterpret_cast<void *>(&cImageDataAdapters::LevelPitch)},
    {"nGZSceneGraph_cImageData_vf29", 0xab1460, 32, 0x352de832, reinterpret_cast<void *>(&cImageDataAdapters::TotalBytes)},
    {"nGZSceneGraph_cImageData_vf30", 0xab1b90, 32, 0xc794fd9b, reinterpret_cast<void *>(&cImageDataAdapters::LevelBytes)},
    {"nGZSceneGraph_cImageData_vf24", 0xab1b40, 32, 0x0124e813, reinterpret_cast<void *>(&cImageDataAdapters::HasPixels)},
    {"nGZSceneGraph_cImageData_vf8", 0xab2030, 32, 0x3fa4ba34, reinterpret_cast<void *>(&cImageDataAdapters::LevelData)},
    {"nGZSceneGraph_cImageData_vf22", 0xab0370, 32, 0x3fa4ba34, reinterpret_cast<void *>(&cImageDataAdapters::LevelResource)},
    {"nGZSceneGraph_cImageData_vf23", 0xab3300, 32, 0x43a0cc31, reinterpret_cast<void *>(&cImageDataAdapters::SetLevelFlag)},
    {"nGZSceneGraph_cImageData_vf15", 0xab14d0, 32, 0xe5783442, reinterpret_cast<void *>(&cImageDataAdapters::DropTopLevels)},
    {"nGZSceneGraph_cImageData_vf14", 0xab1680, 32, 0xeee40b02, reinterpret_cast<void *>(&cImageDataAdapters::RegenerateMips)},
    {"nGZSceneGraph_cImageData_vf16", 0xab03c0, 32, 0x3b9c6cea, reinterpret_cast<void *>(&cImageDataAdapters::SetFormat)},
    {"nGZSceneGraph_cImageData_vf38", 0xab2150, 32, 0x309f5db9, reinterpret_cast<void *>(&cImageDataAdapters::GetPixelRGB)},
    {"nGZSceneGraph_cImageData_vf40", 0xab1ec0, 32, 0x309f5db9, reinterpret_cast<void *>(&cImageDataAdapters::GetPixelChannel)},
    {"nGZSceneGraph_cImageData_vf108_6", 0xab32f3, 11, 0x304fb735, MemberAddress(&cImageDataSubobject::VirtualBaseIfObject)},
    {"nGZSceneGraph_cImageData_vf108_18", 0xab1bf6, 14, 0x291181c7, MemberAddress(&cImageDataSubobject::ConstantSlot18)},
    {"nGZSceneGraph_cImageData_vf108_19", 0xab1bd4, 14, 0xd47dafb7, MemberAddress(&cImageDataSubobject::ConstantSlot19)},
};
REGISTER_HOOKS(kHooks);

} // namespace
