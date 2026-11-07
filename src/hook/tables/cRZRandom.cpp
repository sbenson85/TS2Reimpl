#include "hook/hooks.h"
#include "rz/cRZRandom.h"

namespace {

// Overloaded members need an explicit type to take their address.
using RandNext = uint32_t (cRZRandom::*)();
using RandScaled = uint32_t (cRZRandom::*)(uint32_t);
using SuperNext = uint32_t (cRZRandomSuper::*)();
using SuperScaled = uint32_t (cRZRandomSuper::*)(uint32_t);

const HookSpec kHooks[] = {
    {"cRZRandom_RandomUint32Uniform", 0x1b8d75, 24, 0x7fec356e, MemberAddress(static_cast<RandNext>(&cRZRandom::RandomUint32Uniform))},
    {"cRZRandom_Construct", 0x1b8bc8, 22, 0x05c9f20a, MemberAddress(&cRZRandom::Construct)},
    {"cRZRandom_Seed", 0x1b8ecb, 32, 0x1306b436, MemberAddress(&cRZRandom::Seed)},
    {"cRZRandom_RandomUint32Uniform_Scale", 0x1b8d5c, 25, 0xf062996e, MemberAddress(static_cast<RandScaled>(&cRZRandom::RandomUint32Uniform))},
    {"cRZRandom_RandomSint32RangeUniform", 0x1b8d47, 21, 0xde58d6c5, MemberAddress(&cRZRandom::RandomSint32RangeUniform)},
    {"cRZRandom_RandomDoubleUniform", 0x1b8c78, 32, 0x97158bd4, MemberAddress(&cRZRandom::RandomDoubleUniform)},
    {"cRZRandom_RandomDoubleRangeUniform", 0x1b8c48, 24, 0x4c685407, MemberAddress(&cRZRandom::RandomDoubleRangeUniform)},
    {"cRZRandomSuper_Construct", 0x1b8bde, 32, 0xdc377021, MemberAddress(&cRZRandomSuper::Construct)},
    {"cRZRandomSuper_Seed", 0x1b8efa, 32, 0xd6eebaf8, MemberAddress(&cRZRandomSuper::Seed)},
    {"cRZRandomSuper_Reload", 0x1b8deb, 32, 0xa5d89fc7, MemberAddress(&cRZRandomSuper::Reload)},
    {"cRZRandomSuper_RandomUint32Uniform", 0x1b8d9b, 32, 0x7537409a, MemberAddress(static_cast<SuperNext>(&cRZRandomSuper::RandomUint32Uniform))},
    {"cRZRandomSuper_RandomUint32Uniform_Scale", 0x1b8d8d, 14, 0x80b9727d, MemberAddress(static_cast<SuperScaled>(&cRZRandomSuper::RandomUint32Uniform))},
    {"cRZRandomSuper_RandomDoubleUniform", 0x1b8cba, 32, 0x553ffab8, MemberAddress(&cRZRandomSuper::RandomDoubleUniform)},
    {"cRZRandomSuper_RandomDoubleRangeUniform", 0x1b8c60, 24, 0xa746f6bf, MemberAddress(&cRZRandomSuper::RandomDoubleRangeUniform)},
    {"cRZRandomSuper_Hash", 0x1b8c01, 32, 0x2741c733, reinterpret_cast<void *>(&cRZRandomSuper::Hash)},
};
REGISTER_HOOKS(kHooks);

} // namespace
