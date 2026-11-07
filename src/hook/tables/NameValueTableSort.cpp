#include "hook/hooks.h"
#include "std/NameValueTableSort.h"

namespace {

const HookSpec kHooks[] = {
    {"NameValueTable_SortUnchecked", 0xaf5fe0, 32, 0x75e68da6, reinterpret_cast<void *>(&NameValueTableSort::SortUnchecked)},
    {"NameValueTable_InsertionSort", 0xaf51e0, 32, 0x59ff89fd, reinterpret_cast<void *>(&NameValueTableSort::InsertionSortUnchecked)},
    {"FUN_a0af55c0", 0xaf55c0, 32, 0xf9e4ab5a, reinterpret_cast<void *>(&NameValueTableSort::PartitionByMedianGuess)},
    {"FUN_a0af54f0", 0xaf54f0, 32, 0xdb103910, reinterpret_cast<void *>(&NameValueTableSort::Med3)},
    {"FUN_a0af5ce0", 0xaf5ce0, 32, 0xe7bd71a7, reinterpret_cast<void *>(&NameValueTableSort::PopHeapHoleByIndex)},
    {"FUN_a0af5e80", 0xaf5e80, 32, 0x0d525628, reinterpret_cast<void *>(&NameValueTableSort::PushHeapByIndex)},
    {"FUN_a0af6800", 0xaf6800, 32, 0x0ac1f9e8, MemberAddress(&NameValueTable::ByLengthThenReverseText::operator())},
};
REGISTER_HOOKS(kHooks);

} // namespace
