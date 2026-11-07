#include "hook/hooks.h"
#include "simantics/cTSInteraction.h"

// Standalone shape (docs/standalone-shape.md): the game creates interactions only through the creator 0x450a70 (registered by
// address for class id 0xfc5ecc3d in the cTSSimCOMDirector constructor; the constructor is inline in it), so this hook gives
// every interaction our vtable. Every slot function is referenced only by the game's vtable (tools/callers.py): the slots
// verified by difftest have no entry and are trapped under Hooks.cTSInteraction_Create=1 ("in_effect": "callers"); the
// byte-matched ones keep their adapter. The folded slots (0x148fb0, 0x46390, 0x450b70, 0x450f80, 0x451d00, 0x158f90,
// 0x149d00, 0x229e40, 0x450ff0, 0x15e550, 0x300a0, 0x451050, 0x22fca0, 0x451040, 0x450f70, 0x450fd0, 0x450fe0) are shared
// with other classes and never hooked.
namespace {

using A = cTSInteractionAdapters;

// The creator 0x450a70 (64 bytes) is only referenced by address, so it is not in db/functions.jsonl: CRC computed by hand
// (hookcrc.py checks it).
const HookSpec kHooks[] = {
    {"cTSInteraction_Create", 0x450a70, 32, 0xb45676ba, reinterpret_cast<void *>(&cTSInteraction::Create)},
    {"cTSInteraction_QueryInterface", 0x451560, 32, 0x013de23c, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSInteraction_Release", 0x451990, 24, 0x0ad01218, reinterpret_cast<void *>(&A::Release)},
    {"cTSInteraction_Write", 0x452540, 32, 0x7ef8240b, reinterpret_cast<void *>(&A::Write)},
    {"cTSInteraction_GetGZCLSID", 0x450b50, 6, 0xfe372333, reinterpret_cast<void *>(&A::GetGZCLSID)},
    {"cTSInteraction_IsDefault", 0x4514c0, 32, 0x288b95c2, reinterpret_cast<void *>(&A::IsDefault)},
    {"cTSInteraction_ClearObjectIds", 0x451d90, 9, 0x3ecfab7a, reinterpret_cast<void *>(&A::ClearObjectIds)},
    {"cTSInteraction_GetStackObject", 0x450f10, 29, 0xa264951a, reinterpret_cast<void *>(&A::GetStackObject)},
    {"cTSInteraction_GetTypeSubQueue", 0x450fc0, 5, 0x923a391b, reinterpret_cast<void *>(&A::GetTypeSubQueue)},
    {"cTSInteraction_SetName", 0x451be0, 32, 0x827173ba, reinterpret_cast<void *>(&A::SetName)},
    {"cTSInteraction_SetArguments", 0x451c60, 32, 0x423c9d9a, reinterpret_cast<void *>(&A::SetArguments)},
    {"cTSInteraction_AssignSerial", 0x451d30, 15, 0x1f4c035c, reinterpret_cast<void *>(&A::AssignSerial)},
    {"cTSInteraction_GetAutoFirstSelect", 0x450b80, 6, 0xb1da747f, reinterpret_cast<void *>(&A::GetAutoFirstSelect)},
    {"cTSInteraction_SetAutoFirstSelect", 0x451a90, 25, 0x8522e2cd, reinterpret_cast<void *>(&A::SetAutoFirstSelect)},
    {"cTSInteraction_GetFlag1", 0x450bd0, 8, 0xeec4c656, reinterpret_cast<void *>(&A::GetFlag1)},
    {"cTSInteraction_SetFlag1", 0x451b30, 25, 0x1b63b59a, reinterpret_cast<void *>(&A::SetFlag1)},
    {"cTSInteraction_GetFlag14", 0x450c30, 9, 0x599a3086, reinterpret_cast<void *>(&A::GetFlag14)},
    {"cTSInteraction_SetFlag14", 0x451bc0, 29, 0xb5efcbf0, reinterpret_cast<void *>(&A::SetFlag14)},
    {"cTSInteraction_GetCheckPending", 0x450bc0, 9, 0x134c8f3e, reinterpret_cast<void *>(&A::GetCheckPending)},
    {"cTSInteraction_SetCheckPending", 0x451b10, 25, 0xfc901d75, reinterpret_cast<void *>(&A::SetCheckPending)},
    {"cTSInteraction_HasPassedCheck", 0x450b90, 9, 0xabf0e85b, reinterpret_cast<void *>(&A::HasPassedCheck)},
    {"cTSInteraction_SetCheckPassed", 0x451ab0, 25, 0xe8064aea, reinterpret_cast<void *>(&A::SetCheckPassed)},
    {"cTSInteraction_GetFlag4", 0x450bf0, 9, 0x3627d0e2, reinterpret_cast<void *>(&A::GetFlag4)},
    {"cTSInteraction_SetFlag4", 0x451b70, 25, 0xc12ae5d4, reinterpret_cast<void *>(&A::SetFlag4)},
    {"cTSInteraction_GetBranchFlag", 0x450f00, 9, 0x8e9bb787, reinterpret_cast<void *>(&A::GetBranchFlag)},
    {"cTSInteraction_SetBranchFlag", 0x451c40, 25, 0x9373bba8, reinterpret_cast<void *>(&A::SetBranchFlag)},
    {"cTSInteraction_GetFlag6", 0x450bb0, 9, 0x9c2e1869, reinterpret_cast<void *>(&A::GetFlag6)},
    {"cTSInteraction_SetFlag6", 0x451af0, 25, 0x37c10750, reinterpret_cast<void *>(&A::SetFlag6)},
    {"cTSInteraction_GetFlag10", 0x450f30, 9, 0xd6f8a7d1, reinterpret_cast<void *>(&A::GetFlag10)},
    {"cTSInteraction_SetFlag10", 0x451c90, 29, 0x794bfd11, reinterpret_cast<void *>(&A::SetFlag10)},
    {"cTSInteraction_GetFlag11", 0x451010, 9, 0x6e44c0b4, reinterpret_cast<void *>(&A::GetFlag11)},
    {"cTSInteraction_SetFlag11", 0x451d40, 29, 0x2474ace4, reinterpret_cast<void *>(&A::SetFlag11)},
    {"cTSInteraction_GetNameSet", 0x450e50, 9, 0x24927f0c, reinterpret_cast<void *>(&A::GetNameSet)},
    {"cTSInteraction_SetNameSet", 0x451c00, 29, 0x068e0af0, reinterpret_cast<void *>(&A::SetNameSet)},
    {"cTSInteraction_GetFlag8", 0x450ba0, 9, 0x7cf16f5a, reinterpret_cast<void *>(&A::GetFlag8)},
    {"cTSInteraction_SetFlag8", 0x451ad0, 29, 0xdbff4326, reinterpret_cast<void *>(&A::SetFlag8)},
    {"cTSInteraction_IsVetoed", 0x450f40, 9, 0xc44d083f, reinterpret_cast<void *>(&A::IsVetoed)},
    {"cTSInteraction_SetVetoed", 0x451cb0, 29, 0xba6cd6cb, reinterpret_cast<void *>(&A::SetVetoed)},
    {"cTSInteraction_GetFlag12", 0x450be0, 9, 0xf393f80d, reinterpret_cast<void *>(&A::GetFlag12)},
    {"cTSInteraction_SetFlag12", 0x451b50, 29, 0x9e0a0f0e, reinterpret_cast<void *>(&A::SetFlag12)},
    {"cTSInteraction_GetFlag13", 0x450e60, 9, 0x4b2f9f68, reinterpret_cast<void *>(&A::GetFlag13)},
    {"cTSInteraction_SetFlag13", 0x451c20, 29, 0x31864e9b, reinterpret_cast<void *>(&A::SetFlag13)},
    {"cTSInteraction_GetExtraObject", 0x451020, 29, 0xb6af038c, reinterpret_cast<void *>(&A::GetExtraObject)},
    {"cTSInteraction_GetGroupObject", 0x450f50, 29, 0xe5c9e12e, reinterpret_cast<void *>(&A::GetGroupObject)},
    {"cTSInteraction_SetGroupObject", 0x451cd0, 32, 0x1adfbc5b, reinterpret_cast<void *>(&A::SetGroupObject)},
    {"cTSInteraction_AssignLinkId", 0x450520, 31, 0xf24dba48, reinterpret_cast<void *>(&A::AssignLinkId)},
    {"cTSInteraction_GetLinkId", 0x451000, 5, 0x69447f32, reinterpret_cast<void *>(&A::GetLinkId)},
    {"cTSInteraction_GetLinkNext", 0x450c20, 5, 0x5b721db0, reinterpret_cast<void *>(&A::GetLinkNext)},
    {"cTSInteraction_SetLinkNext", 0x451bb0, 12, 0x576c9d79, reinterpret_cast<void *>(&A::SetLinkNext)},
    {"cTSInteraction_SetParameters", 0x451b90, 17, 0x5648070d, reinterpret_cast<void *>(&A::SetParameters)},
    {"cTSInteraction_GetParameters", 0x450c00, 21, 0x645195c0, reinterpret_cast<void *>(&A::GetParameters)},
    {"cTSInteraction_SetWord32", 0x451d10, 12, 0x3c533de5, reinterpret_cast<void *>(&A::SetWord32)},
    {"cTSInteraction_SetWord34", 0x451d20, 12, 0x19386239, reinterpret_cast<void *>(&A::SetWord34)},
    {"cTSInteraction_HasGroup", 0x451500, 32, 0x4d393bd2, reinterpret_cast<void *>(&A::HasGroup)},
    {"cTSInteraction_SetExtraObject", 0x451d60, 32, 0x8d40ad72, reinterpret_cast<void *>(&A::SetExtraObject)},
};
REGISTER_HOOKS(kHooks);

} // namespace
