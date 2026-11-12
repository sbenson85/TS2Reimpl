#include "hook/hooks.h"
#include "sg/cImposterSliceRecord.h"
#include "sg/cLotImposter.h"
#include "sg/cLotImposterDesignModes.h"

// nTSSG::cLotImposterDesc, nTSSG::cLotImposter and the design modes (lane 261108j, scenegraph row LT2a). The two factories
// build our classes (our vtables), so every other member of theirs is reached through our vtables (in effect through
// callers, trapped: db/status/cLotImposter.jsonl). The design-mode members and the free helpers are called directly by
// the season manager and the imposter manager, so each is hooked at its RVA. Bisect switch: the two Create entries
// together; the design-mode entries together.
namespace {

using nTSSG::cDesignModeNode;
using nTSSG::cDesignModes;
using nTSSG::cImposterSliceRecord;
using nTSSG::cImposterWallEnds;
#define FN(f) reinterpret_cast<void *>(&nTSSG::f)

const HookSpec kHooks[] = {
    {"nTSSG_cLotImposterDesc_Create", 0x36d260, 32, 0xafd80b62,
     reinterpret_cast<void *>(&nTSSG::cLotImposterDesc::Create)},
    {"nTSSG_cLotImposter_Create", 0x36d0a0, 32, 0xfa4de5da, reinterpret_cast<void *>(&nTSSG::cLotImposter::Create)},
    {"nTSSG_cDesignModes_Destruct", 0x335a60, 32, 0x005e3ff8, reinterpret_cast<void *>(&cDesignModes::Destruct)},
    {"nTSSG_cDesignModes_Clear", 0x3f5420, 32, 0xd240bb44, MemberAddress(&cDesignModes::Clear)},
    {"nTSSG_cDesignModes_Load", 0x3f62f0, 32, 0xf891c56f, MemberAddress(&cDesignModes::Load)},
    {"nTSSG_cDesignModes_AddDesignMode", 0x3f5360, 32, 0x3ba33828, MemberAddress(&cDesignModes::AddDesignMode)},
    {"nTSSG_cDesignModes_SetName", 0x3f6440, 32, 0xc6b8686c, MemberAddress(&cDesignModes::SetName)},
    {"nTSSG_cDesignModes_RewriteForLod", 0x3f5550, 32, 0x37d72252, MemberAddress(&cDesignModes::RewriteForLod)},
    {"nTSSG_cDesignModeNode_Reset", 0x3f7a00, 29, 0x6afa3c5f, MemberAddress(&cDesignModeNode::Reset)},
    {"nTSSG_cDesignModeNode_Set", 0x3f7e90, 32, 0x48f24801, MemberAddress(&cDesignModeNode::Set)},
    {"nTSSG_cDesignModeNode_BaseName", 0x3f8800, 32, 0x494e57c8, MemberAddress(&cDesignModeNode::BaseName)},
    {"nTSSG_cDesignModeNode_ApplySeason", 0x3f8a10, 32, 0x149aa167, MemberAddress(&cDesignModeNode::ApplySeason)},
    {"nTSSG_cDesignModeNode_ApplyDesignModesToTree", 0x3f8990, 32, 0xf7afd635,
     MemberAddress(&cDesignModeNode::ApplyDesignModesToTree)},
    {"nTSSG_MakeShapesUnique", 0x3f8750, 32, 0x7bc3250c, FN(MakeShapesUnique)},
    {"nTSSG_BindDesignModeInTree", 0x3f8850, 32, 0x66fa2cab, FN(BindDesignModeInTree)},
    {"nTSSG_FindLod", 0x3f5470, 32, 0x1b297e1f, FN(FindLod)},
    {"nTSSG_StripLod", 0x3ac4f0, 32, 0x8cf0059a, FN(StripLod)},
    {"nTSSG_MakeMaterialsUnique", 0xae7af0, 32, 0x48f59bd7, FN(MakeMaterialsUnique)},
    {"nTSSG_LoadImposterResources", 0xb40560, 32, 0x53d46781, FN(LoadImposterResources)},
    {"nTSSG_GetLotGroup", 0x401790, 32, 0xace57618, FN(GetLotGroup)},
    // The wall-slice record (lane 261111t, row LT2a2a): no vtable, built inline by the imposter manager, so each member
    // at its RVA. Bisect switch: these thirteen together.
    {"nTSSG_cImposterSliceRecord_Construct", 0x3f6610, 32, 0x025389c3, FN(ConstructImposterSliceRecord)},
    {"nTSSG_cImposterWallEnds_Init", 0x3f66a0, 32, 0x5e91879c, MemberAddress(&cImposterWallEnds::Init)},
    {"nTSSG_cImposterSliceRecord_MinBottom", 0x3f69c0, 21, 0xa038ef44, MemberAddress(&cImposterSliceRecord::MinBottom)},
    {"nTSSG_cImposterSliceRecord_MaxTop", 0x3f6b10, 21, 0x7e30ee31, MemberAddress(&cImposterSliceRecord::MaxTop)},
    {"nTSSG_cImposterSliceRecord_Height", 0x3f6a80, 27, 0x40526b05, MemberAddress(&cImposterSliceRecord::Height)},
    {"nTSSG_cImposterSliceRecord_RowCount", 0x3f6a30, 32, 0x006e24b7, MemberAddress(&cImposterSliceRecord::RowCount)},
    {"nTSSG_cImposterSliceRecord_SameAngle", 0x3f6e30, 32, 0x2fedfad8, MemberAddress(&cImposterSliceRecord::SameAngle)},
    {"nTSSG_cImposterSliceRecord_Normal", 0x3f69e0, 32, 0x07af55a1, MemberAddress(&cImposterSliceRecord::Normal)},
    {"nTSSG_cImposterSliceRecord_Length", 0x3f6aa0, 32, 0xbadf3428, MemberAddress(&cImposterSliceRecord::Length)},
    {"nTSSG_cImposterSliceRecord_Begin", 0x3f6b30, 32, 0x9f48661d, MemberAddress(&cImposterSliceRecord::Begin)},
    {"nTSSG_cImposterSliceRecord_Extend", 0x3f6800, 32, 0x7da4a00b, MemberAddress(&cImposterSliceRecord::Extend)},
    {"nTSSG_cImposterSliceRecord_Build", 0x3f6730, 32, 0x060649f9, MemberAddress(&cImposterSliceRecord::Build)},
    {"nTSSG_cImposterSliceRecord_Split", 0x3f6d00, 32, 0x17a1ebc2, MemberAddress(&cImposterSliceRecord::Split)},
    // Lane 261111u (row LT2a2b): ApplyDefinition's design-mode callees. Bisect switch: these five together.
    {"nTSSG_cDesignModes_LoadFromObject", 0x3f5b80, 32, 0xf89f0999, MemberAddress(&cDesignModes::LoadFromObject)},
    {"nTSSG_cDesignModeNode_LoadByName", 0x3f7b10, 32, 0xb648808a, MemberAddress(&cDesignModeNode::LoadByName)},
    {"nTSSG_cDesignModeNode_LoadForObject", 0x3f7a50, 32, 0xc1a1bc8c, MemberAddress(&cDesignModeNode::LoadForObject)},
    {"nTSSG_cDesignModeNode_Take", 0x3f7c80, 32, 0x53d4f095, MemberAddress(&cDesignModeNode::Take)},
    {"nTSSG_cDesignModeNode_SetFromObject", 0x3f7f40, 32, 0xe09732bd, MemberAddress(&cDesignModeNode::SetFromObject)},
};

REGISTER_HOOKS(kHooks);

#undef FN

} // namespace
