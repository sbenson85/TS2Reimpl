#include "hook/hooks.h"
#include "ts/cTSResourceFactoryForIGZSerializable.h"
namespace {
const HookSpec kHooks[] = {
// The creator 0x1f6740 is not in db/functions.jsonl: CRC over its first 32 bytes, computed by hand.
{"cTSResourceFactoryForIGZSerializable_Create", 0x1f6740, 32, 0x902ddaeb, reinterpret_cast<void *>(&cTSResourceFactoryForIGZSerializable::Create)},
{"cTSResourceFactoryForIGZSerializable_vf7", 0x1f6180, 32, 0xbdf4743d, MemberAddress(&cTSResourceFactoryForIGZSerializable::DeletingDestructor)},
{"cTSResourceFactoryForIGZSerializable_vf9", 0x1f63d0, 26, 0x3a1c4d46, MemberAddress(&cTSResourceFactoryForIGZSerializable::CreateInstanceByType3)},
};
REGISTER_HOOKS(kHooks);
}
