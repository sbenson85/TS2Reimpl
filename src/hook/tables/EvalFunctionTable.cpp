#include "hook/hooks.h"
#include "gz/EvalFunctionTable.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0b094b0", 0xb094b0, 32, 0x05f25809, reinterpret_cast<void *>(&nGZCommandParser::EvalFunctionTableAdapters::Construct)},
    {"FUN_a0b09010", 0xb09010, 32, 0x2741229d, reinterpret_cast<void *>(&nGZCommandParser::EvalFunctionTableStl::SortUnchecked)},
    {"FUN_a0b08a30", 0xb08a30, 32, 0x8606fc2a, reinterpret_cast<void *>(&nGZCommandParser::EvalFunctionTableStl::PartitionByMedianGuess)},
    {"FUN_a0b08f50", 0xb08f50, 32, 0x46e0fa94, MemberAddress(&nGZCommandParser::EvalFunctionTableVector::ResizeReallocate)},
};
REGISTER_HOOKS(kHooks);

} // namespace
