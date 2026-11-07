// cIStreamStringBehaviourAdaptor (lane QQ, src/gz/cIStreamStringBehaviourAdaptor.h): the adaptor's own slots 0, 3 and
// 21. Slots 17 and 18 (0xa4d650, 0xa4d640) are folded with cOStreamStringBehaviourAdaptor's and are not hooked; the
// constructor is inlined into the scene-graph factory's Read (0xa4d750).
#include "hook/hooks.h"
#include "gz/cIStreamStringBehaviourAdaptor.h"

namespace {

const HookSpec kHooks[] = {
    {"cIStreamStringBehaviourAdaptor_QueryInterface", 0xa4d6d0, 32, 0x757d6637, MemberAddress(&cIStreamStringBehaviourAdaptor::QueryInterface)},
    {"cIStreamStringBehaviourAdaptor_Skip", 0xa4da80, 10, 0xd5be4c8a, MemberAddress(&cIStreamStringBehaviourAdaptor::Skip)},
    {"cIStreamStringBehaviourAdaptor_vf21", 0xa4d4b0, 32, 0xe7236098, MemberAddress(&cIStreamStringBehaviourAdaptor::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
