#include "app/ExceptionReporter.h"
#include "hook/hooks.h"

namespace {

using appbase::ExceptionReporterObject;

const HookSpec kHooks[] = {
    {"ExceptionReporter_Construct", 0x1ba914, 32, 0x643a6c42, MemberAddress(&ExceptionReporterObject::Construct)},
    {"ExceptionReporter_Destruct", 0x1baae3, 32, 0x48936c1f, MemberAddress(&ExceptionReporterObject::Destruct)},
    {"ExceptionReporter_SplitPath", 0x1c3a97, 32, 0x94ade437, reinterpret_cast<void *>(&appbase::SplitPath)},
    {"ExceptionReporter_GetModuleDirectory", 0x1c1c8c, 32, 0x98638722, reinterpret_cast<void *>(&appbase::GetModuleDirectory)},
    {"cRZTextFile_ConstructDefault", 0x1c3f3e, 32, 0xa7015efc, reinterpret_cast<void *>(&appbase::ConstructTextFile)},
    {"cRZTextFile_WriteLine", 0x1c4347, 32, 0x893ecf0b, reinterpret_cast<void *>(&appbase::WriteLine)},
    {"ExceptionReporter_RemoveFilterAndTerminate", 0x1bd001, 24, 0xc8188e61, reinterpret_cast<void *>(&appbase::RemoveFilterAndTerminate)},
    {"ExceptionReporter_SetDirectory", 0x1bd42a, 32, 0x57de9b88, MemberAddress(&ExceptionReporterObject::SetDirectory)},
    {"ExceptionReporter_AddListener", 0x1bac86, 32, 0xc4c90a6d, MemberAddress(&ExceptionReporterObject::AddListener)},
    {"ExceptionReporter_AddExtraData", 0x1bac12, 32, 0xaaa80b3f, MemberAddress(&ExceptionReporterObject::AddExtraData)},
    {"ExceptionReporter_WriteExtraDataEnd", 0x1bb00a, 28, 0x48d0eb78, MemberAddress(&ExceptionReporterObject::WriteExtraDataEnd)},
    {"ExceptionReporter_Broadcast", 0x1bd339, 32, 0x405529e5, MemberAddress(&ExceptionReporterObject::Broadcast)},
    {"ExceptionReporter_NotifyListener", 0x1bd39f, 32, 0xd79e17a9, MemberAddress(&ExceptionReporterObject::NotifyListener)},
};
REGISTER_HOOKS(kHooks);

} // namespace
