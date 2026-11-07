#include "hook/hooks.h"
#include "ui/cHTMLElement.h"

namespace {

const HookSpec kHooks[] = {
    {"cHTMLElement_vf7", 0x9892c0, 32, 0xae76610a, MemberAddress(&cHTMLElement::HitTest)},
    {"FUN_a05bbc56", 0x5bbc56, 32, 0x39b6aff6, MemberAddress(&HTMLRect::Contains)},
    {"cHTMLElement_vf0", 0x97e660, 32, 0x9caefcc3, MemberAddress(&cHTMLElement::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
