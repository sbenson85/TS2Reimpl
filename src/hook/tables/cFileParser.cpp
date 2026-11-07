#include "hook/hooks.h"
#include "gz/cFileParser.h"

// Standalone shape (docs/standalone-shape.md, lane IM): the game makes cFileParser only through its COM class factory
// 0xb0dc30 (the constructor is inline there; the address is registered in the COM director data), hooked to our Create, so
// every object carries our vtables; the vtables 0xe7f9d0 / 0xe7fa00 / 0xe7fa14 are referenced only by the factory and the
// destructor 0xb0d390. The slot adapters serve objects carrying the game's vtables. The folded slots 8..10 and AddRef /
// Release (shared with other classes) are not hooked.
namespace {

using A = nGZCommandParser::cFileParserAdapters;
using nGZCommandParser::cFileParser;

const HookSpec kHooks[] = {
    {"cFileParser_Create", 0xb0dc30, 32, 0xe2b04d9f, reinterpret_cast<void *>(&cFileParser::Create)},
    {"cFileParser_Destructor", 0xb0d390, 32, 0x23b701e2, reinterpret_cast<void *>(&A::Destructor)},
    {"cFileParser_DeletingDestructor", 0xb0d5a0, 32, 0xac90e34f, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cFileParser_QueryInterface", 0xb0def0, 32, 0x00462757, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cFileParser_Init", 0xb0dd30, 7, 0xf0d100ba, reinterpret_cast<void *>(&A::Init)},
    {"cFileParser_Shutdown", 0xb0df90, 32, 0x1badb45b, reinterpret_cast<void *>(&A::Shutdown)},
    {"cFileParser_SetParser", 0xb0df30, 32, 0xeac5cd36, reinterpret_cast<void *>(&A::SetParser)},
    {"cFileParser_AddFile", 0xb0d5c0, 32, 0xfc4af771, reinterpret_cast<void *>(&A::AddFile)},
    {"cFileParser_RunFiles", 0xb0dd40, 32, 0xfb3a6da9, reinterpret_cast<void *>(&A::RunFiles)},
    {"cFileParser_DoMessage", 0xb0dc00, 32, 0xa6f247f0, reinterpret_cast<void *>(&A::DoMessage)},
    {"cRZFileName_SplitDirectory", 0x1c8554, 32, 0x7224feef, reinterpret_cast<void *>(&A::SplitDirectory)},
    {"cRZFileName_SplitDrive", 0x1c85c2, 32, 0x99403a27, reinterpret_cast<void *>(&A::SplitDrive)},
    {"cRZStopwatch_Reset", 0x1c95dd, 15, 0x0ecf7eca, reinterpret_cast<void *>(&A::ResetStopwatch)},
};
REGISTER_HOOKS(kHooks);

} // namespace
