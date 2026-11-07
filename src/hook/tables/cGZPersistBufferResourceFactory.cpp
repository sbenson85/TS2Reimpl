#include "hook/hooks.h"
#include "gz/cGZPersistBufferResourceFactory.h"

namespace {

using Self = cGZPersistBufferResourceFactory;

const HookSpec kHooks[] = {
    {"cGZPersistBufferResourceFactory_QueryInterface", 0x9b7d00, 32, 0x7b88e666, MemberAddress(&Self::QueryInterface)},
    {"cGZPersistBufferResourceFactory_CreateInstance_2", 0x9b6740, 32, 0x333d5910, MemberAddress(&Self::CreateInstanceByType)},
    {"cGZPersistBufferResourceFactory_vf7", 0x9b6650, 32, 0x736a6b79, MemberAddress(&Self::DeletingDestructor)},
    {"cGZPersistBufferResourceFactory_vf9", 0x9b6710, 32, 0xa05431a8, MemberAddress(&Self::Create)},
    {"cGZPersistBufferResourceFactory_vf10", 0x9b7fa0, 32, 0xcaaae4a2, MemberAddress(&Self::ReadRecord)},
    {"cGZPersistBufferResourceFactory_vf16_3", 0x9b6f60, 32, 0xa370d472, MemberAddress(&Self::Face::NewBufferFromFile)},
    {"cGZPersistBufferResourceFactory_vf16_5", 0x9b7060, 32, 0xa0373ed2, MemberAddress(&Self::Face::NewBufferFromBitmap)},
    {"cGZPersistBufferResourceFactory_vf16_6", 0x9b7280, 32, 0x7ed5e48a, MemberAddress(&Self::Face::ReadBuffer)},
    {"cGZPersistBufferResourceFactory_vf16_7", 0x9b7d40, 32, 0xab65c8b5, MemberAddress(&Self::Face::WriteBuffer)},
    {"cGZPersistBufferResourceFactory_vf16_8", 0x9b8120, 17, 0x02fb1464, MemberAddress(&Self::Face::SetColorType)},
};
REGISTER_HOOKS(kHooks);

} // namespace
