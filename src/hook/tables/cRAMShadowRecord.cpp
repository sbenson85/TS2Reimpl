#include "hook/hooks.h"
#include "ts/cRAMShadowRecord.h"

namespace {

const HookSpec kHooks[] = {
    {"AVcRAMShadowRecord_A0x02237cae_GetFieldVoid", 0x1dff20, 32, 0xf0be26a1, MemberAddress(&cRAMShadowRecord::GetFieldVoid)},
    {"AVcRAMShadowRecord_A0x02237cae_GetSize", 0x1e0940, 7, 0x72dc9438, MemberAddress(&cRAMShadowRecord::GetSize)},
    {"AVcRAMShadowRecord_A0x02237cae_SeekAbsolute", 0x1e4a90, 25, 0xac96abd2, MemberAddress(&cRAMShadowRecord::SeekAbsolute)},
    {"AVcRAMShadowRecord_A0x02237cae_SeekRelative", 0x1e4ac0, 32, 0x37f09c5b, MemberAddress(&cRAMShadowRecord::SeekRelative)},
    {"AVcRAMShadowRecord_A0x02237cae_DeletingDestructor", 0x1da120, 32, 0x354fef34, MemberAddress(&cRAMShadowRecord::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
