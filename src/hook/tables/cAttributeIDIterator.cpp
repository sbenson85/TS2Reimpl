#include "hook/hooks.h"
#include "gz/cAttributeIDIterator.h"

namespace {

using nGZEvent::cAttributeIDIterator;

// nGZEvent::cAttributeIDIterator (lane VX). The folded slots 0x1a849d (QueryInterface), 0x148fb0 (AddRef), 0x1a84c0
// (Release) and 0x178550 (IsEqual) are shared with unrelated classes: not hooked, ours only in our table.
const HookSpec kHooks[] = {
    {"nGZEvent_cAttributeIDIterator_Construct", 0x1a9942, 32, 0x334ed36d, MemberAddress(&cAttributeIDIterator::Construct)},
    {"nGZEvent_cAttributeIDIterator_CopyConstruct", 0x1a8434, 32, 0x8f3b6ac3,
     MemberAddress(&cAttributeIDIterator::CopyConstruct)},
    {"nGZEvent_cAttributeIDIterator_Destruct", 0x1a8462, 19, 0xc8490f21, MemberAddress(&cAttributeIDIterator::Destruct)},
    {"nGZEvent_cAttributeIDIterator_DeletingDestructor", 0x1468b0, 32, 0x0dd4f9a5,
     MemberAddress(&cAttributeIDIterator::DeletingDestructor)},
    {"nGZEvent_cAttributeIDIterator_Clone", 0x1a8475, 29, 0xf6f9c0e1, MemberAddress(&cAttributeIDIterator::Clone)},
    {"nGZEvent_cAttributeIDIterator_Get", 0x1a8492, 6, 0x05fdec92, MemberAddress(&cAttributeIDIterator::Get)},
    {"nGZEvent_cAttributeIDIterator_Next", 0x1a8498, 5, 0x7cd7ad6d, MemberAddress(&cAttributeIDIterator::Next)},
};
REGISTER_HOOKS(kHooks);

} // namespace
