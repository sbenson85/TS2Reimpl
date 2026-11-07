#include "hook/hooks.h"
#include "gz/cGZPersistResourceFactory.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZPersistResourceFactory_Construct", 0x9b6010, 28, 0x4af66ea1, MemberAddress(&cGZPersistResourceFactory::Construct)},
    {"cGZPersistResourceFactory_Destruct", 0x9b6040, 14, 0x1f8c8bf7, MemberAddress(&cGZPersistResourceFactory::Destruct)},
    {"cGZPersistResourceFactory_vf7", 0x9b6080, 32, 0xba634b60, MemberAddress(&cGZPersistResourceFactory::DeletingDestructor)},
    {"cGZPersistResourceFactory_QueryInterface", 0x9b61d0, 32, 0x2225a303, MemberAddress(&cGZPersistResourceFactory::QueryInterface)},
    {"cGZPersistResourceFactory_AddRef", 0x9b60b0, 32, 0x9a01e4bc, MemberAddress(&cGZPersistResourceFactory::AddRef)},
    {"cGZPersistResourceFactory_Release", 0x9b6290, 32, 0xdfa80cd9, MemberAddress(&cGZPersistResourceFactory::Release)},
    {"cGZPersistResourceFactory_CreateInstance_1", 0x9b60e0, 32, 0xd5512d0b, MemberAddress(&cGZPersistResourceFactory::CreateInstance)},
    {"cGZPersistResourceFactory_CreateInstance_2", 0x9b61b0, 22, 0x1b1f10fc, MemberAddress(&cGZPersistResourceFactory::CreateInstance2)},
    {"cGZPersistResourceFactory_Read", 0x9b6200, 32, 0x7c2b076f, MemberAddress(&cGZPersistResourceFactory::Read)},
    {"cGZPersistResourceFactory_Write", 0x9b62d0, 32, 0x7c2b076f, MemberAddress(&cGZPersistResourceFactory::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
