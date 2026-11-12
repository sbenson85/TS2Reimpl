#include "hook/hooks.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "sg/cLightRigAndCameraParserState.h"

// The light rig file's parser state and its commands (lane 261109d, scenegraph LG7g; src/sg/cLightRigAndCameraParserState.h).
// The state's faces' 14-byte thunks (`sub ecx, [ecx-4]; sub ecx, 0x88; jmp body`) forward to cBaseParser's adapters at
// cBaseParser's own face; the two 8-byte ones (`sub ecx, [ecx-4]; jmp body`, the cIBaseParser face's slots 0 and 10) to the
// state's bodies. The constructor, destructors, bodies and AddRig (0x2c21a0, reached only from the lightrig command's slot 9)
// are hooked to ours. The commands are made with the game's vtables, so their own slots are hooked here too.
namespace {

using nTSSG::cLightRigAndCameraParserState;
using nTSSG::cLightRigAndCameraParserStateAdapters;

#define SHIFTED(adapter) reinterpret_cast<void *>(&nTSSG::LightRigShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define VTORDISP_OWN(adapter) reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&cLightRigAndCameraParserStateAdapters::adapter>::Call)
#define OWN(adapter) reinterpret_cast<void *>(&cLightRigAndCameraParserStateAdapters::adapter)

const HookSpec kHooks[] = {
    {"nTSSG_cLightRigAndCameraParserState_Construct", 0x2c1800, 32, 0xa41b3e01, OWN(Construct)},
    {"nTSSG_cLightRigAndCameraParserState_Destruct", 0x2c1cb0, 32, 0x1cd12a05, OWN(Destruct)},
    {"nTSSG_cLightRigAndCameraParserState_vf0", 0x2c2130, 32, 0x31127aac, OWN(DeletingDestructor)},
    {"nTSSG_cLightRigAndCameraParserState_body_2c2b60", 0x2c2b60, 32, 0xcfac8327, OWN(Initialise)},
    {"nTSSG_cLightRigAndCameraParserState_body_2c3ba0", 0x2c3ba0, 32, 0x6d33ff2c, OWN(ResetBlocks)},
    {"nTSSG_cLightRigAndCameraParserState_AddRig", 0x2c21a0, 32, 0xd252db8b, MemberAddress(&cLightRigAndCameraParserState::AddRig)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_0", 0x2c2b56, 8, 0xfd302584, VTORDISP_OWN(Initialise)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_10", 0x2c3b8e, 8, 0x38840d6b, VTORDISP_OWN(ResetBlocks)},
    {"nTSSG_cLightRigAndCameraParserState_vf568_0", 0x2c400b, 14, 0xdc549b0e, SHIFTED(QueryInterface)},
    {"nTSSG_cLightRigAndCameraParserState_vf568_1", 0x2c2507, 14, 0x2183bd29, SHIFTED(AddRef)},
    {"nTSSG_cLightRigAndCameraParserState_vf568_2", 0x2c4065, 14, 0x360bb9a3, SHIFTED(Release)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_1", 0x2c42b7, 14, 0xa7431cef, SHIFTED(Shutdown)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_2", 0x2c4263, 14, 0x01ddebfe, SHIFTED(SetVersionRange)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_3", 0x2c427f, 14, 0xbae2c6d0, SHIFTED(SetContextState)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_4", 0x2c4049, 14, 0x71e0f538, SHIFTED(RegisterCommand)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_5", 0x2c4378, 14, 0x95cc82cc, SHIFTED(UnregisterCommand)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_6", 0x2c436a, 14, 0xf4c845e5, SHIFTED(UnregisterCommandObject)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_7", 0x2c4057, 14, 0xe28974e9, SHIFTED(RegisterMetaCommand)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_8", 0x2c41dd, 14, 0xcdd670ca, SHIFTED(RestartViaReset)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_9", 0x2c2634, 14, 0xe7b4df8d, SHIFTED(Reset)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_11", 0x2c265e, 14, 0xd3fa9f7b, SHIFTED(NoOp)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_12", 0x2c3f2b, 14, 0xa2576186, SHIFTED(RunFile)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_13", 0x2c3f63, 14, 0xd3b951a9, SHIFTED(RunFiles)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_14", 0x2c3f55, 14, 0x9e770213, SHIFTED(RunText)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_15", 0x2c3f1d, 14, 0xaea7c4d8, SHIFTED(RunLine)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_16", 0x2c4340, 14, 0x5def2ff5, SHIFTED(PushOpenBlock)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_17", 0x2c434e, 14, 0xfee2668e, SHIFTED(PushMetaBlock)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_18", 0x2c26a1, 14, 0xafa4834a, SHIFTED(PopMetaBlock)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_19", 0x2c42a9, 14, 0x0a437cb8, SHIFTED(RunExpandedText)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_20", 0x2c3ffd, 14, 0xe7d726b2, SHIFTED(PushNamespace)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_21", 0x2c3fe1, 14, 0x265a7c8e, SHIFTED(PopNamespace)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_22", 0x2c3fef, 14, 0x634cbc67, SHIFTED(PurgeNamespace)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_23", 0x2c2ac5, 14, 0xce5e6530, SHIFTED(GetFunctions)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_24", 0x2c428d, 14, 0xcf90ce93, SHIFTED(SetOutput)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_25", 0x2c429b, 14, 0x6cfa1d2c, SHIFTED(SetHandler)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_26", 0x2c4271, 14, 0x2a0f936f, SHIFTED(SetSource)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_27", 0x2c4386, 14, 0x70601252, SHIFTED(FindVariable)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_28", 0x2c2685, 14, 0x24779987, SHIFTED(GetNamespacePath)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_29", 0x2c435c, 14, 0x7d6e2c13, SHIFTED(ExpandVariables)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_30", 0x2c3f0f, 14, 0xb0ea6c07, SHIFTED(EvalBool)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_31", 0x2c3f71, 14, 0xcc0a6269, SHIFTED(EvalFloat)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_32", 0x2c3f7f, 14, 0x4057137b, SHIFTED(EvalInt)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_34", 0x2c3fb7, 14, 0x6b3e59c1, SHIFTED(EvalVector2)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_35", 0x2c3fc5, 14, 0xa30cb026, SHIFTED(EvalVector3)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_36", 0x2c3fd3, 14, 0x113820b2, SHIFTED(EvalVector4)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_37", 0x2c3f8d, 14, 0x5e1abba4, SHIFTED(EvalColorRGBA)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_38", 0x2c3f9b, 14, 0xbb3ed473, SHIFTED(EvalColorARGB)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_39", 0x2c3fa9, 14, 0xdd87b5e7, SHIFTED(EvalColorRGB)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_40", 0x2c3324, 14, 0xc97127ab, SHIFTED(CommandCount)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_41", 0x2c2642, 14, 0x98f28c31, SHIFTED(CommandAt)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_42", 0x2c2f52, 14, 0x3dc6e766, SHIFTED(IsInitialised)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_43", 0x2c2ea6, 14, 0x6a621644, SHIFTED(IsCaseSensitive)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_44", 0x2c2ff3, 14, 0xb19c0851, SHIFTED(GetText)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_45", 0x2c4394, 14, 0xadc2d208, SHIFTED(GetVersion)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_46", 0x2c3f47, 14, 0x803aaacc, SHIFTED(GetFileName)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_47", 0x2c3f39, 14, 0x2a336247, SHIFTED(GetLine)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_48", 0x2c2693, 14, 0xe541f190, SHIFTED(HasDefinition)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_49", 0x2c2650, 14, 0xc2d0bc1b, SHIFTED(HasCommand)},
    {"nTSSG_cLightRigAndCameraParserState_vf576_50", 0x2c4332, 14, 0xf7e6e77e, SHIFTED(GetSource)},

    {"nTSSG_cLightRigCommand_vf4", 0x2c2830, 32, 0xf81e20a3, MemberAddress(&nTSSG::cLightRigCommand::Execute)},
    {"nTSSG_cLightRigCommand_vf5", 0x2c3030, 6, 0x35fd6d7a, MemberAddress(&nTSSG::cLightRigCommand::GetName)},
    {"nTSSG_cLightRigCommand_vf9", 0x2c3b20, 32, 0x5b408a9e, MemberAddress(&nTSSG::cLightRigCommand::EndBlock)},
    {"nTSSG_cLightRigResNameCommand_vf4", 0x2c2990, 32, 0xfa48660a, MemberAddress(&nTSSG::cLightRigResNameCommand::Execute)},
    {"nTSSG_cLightRigResNameCommand_vf5", 0x2c3040, 6, 0xa6de39c7, MemberAddress(&nTSSG::cLightRigResNameCommand::GetName)},
    {"nTSSG_cRootTagCommand_vf4", 0x2c2a30, 32, 0xfa48660a, MemberAddress(&nTSSG::cRootTagCommand::Execute)},
    {"nTSSG_cRootTagCommand_vf5", 0x2c3050, 6, 0xeadbab9f, MemberAddress(&nTSSG::cRootTagCommand::GetName)},
    {"nTSSG_cAnimNameCommand_vf4", 0x2c26b0, 32, 0xdab8c85c, MemberAddress(&nTSSG::cAnimNameCommand::Execute)},
    {"nTSSG_cAnimNameCommand_vf5", 0x2c3010, 6, 0x43999713, MemberAddress(&nTSSG::cAnimNameCommand::GetName)},
    {"nTSSG_cAutoRigCommand_vf4", 0x2c27a0, 32, 0x51c61a21, MemberAddress(&nTSSG::cAutoRigCommand::Execute)},
    {"nTSSG_cAutoRigCommand_vf5", 0x2c3020, 6, 0x17388955, MemberAddress(&nTSSG::cAutoRigCommand::GetName)},
};
REGISTER_HOOKS(kHooks);

#undef SHIFTED
#undef VTORDISP_OWN
#undef OWN

} // namespace
