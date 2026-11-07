#include "hook/hooks.h"
#include "gz/GimexFshCluster.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a08995da", 0x8995da, 32, 0x0af9a99a, gimex::fsh::PrincipalAxis},
    {"FUN_a0899a2f", 0x899a2f, 32, 0xc69f725f, gimex::fsh::FarthestPairAxis},
    {"FUN_a089d691", 0x89d691, 32, 0xf17b095a, gimex::fsh::OrderAlongAxis},
    {"FUN_a089c42d", 0x89c42d, 32, 0x01b9dc23, gimex::fsh::SameOrdering},
    {"FUN_a089c480", 0x89c480, 32, 0xb4606da6, gimex::fsh::ClusterFit},
    {"FUN_a089cacf", 0x89cacf, 32, 0x66016a4a, gimex::fsh::BoundedClusterFit},
    {"FUN_a0899d55", 0x899d55, 32, 0x5888bd15, gimex::fsh::SolveAlpha},
    {"FUN_a0899b1f", 0x899b1f, 32, 0x01ca5ee3, gimex::fsh::PlaceEndpoints},
    {"FUN_a089d7b1", 0x89d7b1, 32, 0xcab8b2e7, gimex::fsh::EncodeRecord},
    {"fsh_quantise_endpoints", 0x89aa95, 32, 0x622194da, gimex::fsh::QuantiseEndpoints},
};
REGISTER_HOOKS(kHooks);

} // namespace
