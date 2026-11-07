#include "hook/hooks.h"
#include "misc/UtilitiesC.h"

namespace {

const HookSpec kHooks[] = {
    {"UtilObjectA_Construct", 0x1b9035, 13, 0x1562031f, MemberAddress(&UtilObjectA::Construct)},
    {"UtilObjectB_Construct", 0x9c7b3d, 32, 0x66a80c8d, MemberAddress(&UtilObjectB::Construct)},
    {"ServiceQueryHolder_QueryWithIdsA", 0x20dbd8, 32, 0xfadcc797, MemberAddress(&ServiceQueryHolder::QueryWithIdsA)},
    {"ServiceQueryHolder_QueryWithIdsB", 0x2365e3, 32, 0xfc184caa, MemberAddress(&ServiceQueryHolder::QueryWithIdsB)},
    {"GetCachedServiceObjectC", 0x2366b8, 32, 0xf7efa354, reinterpret_cast<void *>(&GetCachedServiceObjectC)},
    {"GetCachedServiceObjectD", 0x9c5bd6, 32, 0xc166b03a, reinterpret_cast<void *>(&GetCachedServiceObjectD)},
    {"KeyMapSlot5Walker_CallSlot5ForEachKey", 0x1c0458, 32, 0xd762e70f, MemberAddress(&KeyMapSlot5Walker::CallSlot5ForEachKey)},
    {"AffineTransform3_TransformPoint", 0x19c820, 32, 0xebeec6be, MemberAddress(&AffineTransform3::TransformPoint)},};
REGISTER_HOOKS(kHooks);

} // namespace
