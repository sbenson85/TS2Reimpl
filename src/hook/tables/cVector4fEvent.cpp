#include "hook/hooks.h"
#include "gz/cVector4fEvent.h"

// nGZEvent::cVectorEvent<cVector<4, float>> (lane FD): the slots only this class has; the game's code still builds the
// event with the game's table. The folded slots (0x154eb0, 0x1559e0, 0x1561d0) are not hooked.
namespace {

using nGZEvent::cVector4fEvent;

const HookSpec kHooks[] = {
    {"nGZEvent_cVector4fEvent_vf1", 0x258ca0, 15, 0xa38e8fd4, MemberAddress(&cVector4fEvent::Assign)},
    {"nGZEvent_cVector4fEvent_vf3", 0x269ba0, 6, 0xb45e3e13, MemberAddress(&cVector4fEvent::GetEventType)},
    {"nGZEvent_cVector4fEvent_vf6", 0x25b130, 21, 0xaac3a1d1, MemberAddress(&cVector4fEvent::CloneInto)},
};
REGISTER_HOOKS(kHooks);

} // namespace
