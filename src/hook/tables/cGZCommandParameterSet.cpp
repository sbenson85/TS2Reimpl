#include "hook/hooks.h"
#include "gz/cGZCommandParameterSet.h"

namespace {
using A = cGZCommandParameterSetAdapters;
const HookSpec kHooks[] = {
    {"cGZCommandParameterSet_QueryInterface", 0xd18380, 32, 0xaaafc1a9, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cGZCommandParameterSet_vf4", 0xd1840a, 32, 0x1b6f552e, reinterpret_cast<void *>(&A::SetParameterCount)},
    {"cGZCommandParameterSet_vf5", 0xd181de, 32, 0xa28c0e41, reinterpret_cast<void *>(&A::GetParameter)},
    {"cGZCommandParameterSet_vf6", 0xd18360, 32, 0xc12b4827, reinterpret_cast<void *>(&A::FindParameter)},
    {"cGZCommandParameterSet_vf7", 0xd183e1, 32, 0xf1d5f7da, reinterpret_cast<void *>(&A::SetParameter)},
    {"cGZCommandParameterSet_vf8", 0xd18221, 32, 0x4196d7fb, reinterpret_cast<void *>(&A::AppendParameter)},
    {"cGZCommandParameterSet_vf9", 0xd183c1, 32, 0xf84fbbc7, reinterpret_cast<void *>(&A::RemoveParameter)},
    {"cGZCommandParameterSet_vf10", 0xd18460, 32, 0xc3ea7f4e, reinterpret_cast<void *>(&A::SetVersion)},
    {"cGZCommandParameterSet_vf11", 0xd18254, 32, 0x44aafc6e, reinterpret_cast<void *>(&A::FindText)},
    {"cGZCommandParameterSet_vf12", 0xd18490, 31, 0x1d2feb5a, reinterpret_cast<void *>(&A::IsSpecialParameter)},
    {"cGZCommandParameterSet_vf13", 0xd18318, 19, 0x3b298d45, reinterpret_cast<void *>(&A::GetFirstParameterIndex)},
    {"cGZCommandParameterSet_vf14", 0xd1832b, 32, 0x5160fae6, reinterpret_cast<void *>(&A::GetNextParameterIndex)},
    {"cGZCommandParameterSet_vf15", 0xd18202, 31, 0x6b2d0369, reinterpret_cast<void *>(&A::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);
} // namespace
