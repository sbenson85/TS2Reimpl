#include "hook/hooks.h"
#include "gz/cFileSource.h"

// Standalone shape (docs/standalone-shape.md, lane IM): every cFileSource is made by our cBaseParser code (the constructor
// 0xafd800 and SetSource 0xb06140, both hooked), so every object carries our vtables; the vtables 0xe7dc30 / 0xe7dc54 are
// referenced only there and by the deleting destructor 0xaff0a0. The slot adapters serve objects carrying the game's
// vtables. Slot 0 (0xb059b0, folded with another class's slot 0) and the folded AddRef / Release are not hooked.
namespace {

using A = nGZCommandParser::cFileSourceAdapters;

const HookSpec kHooks[] = {
    {"cFileSource_DeletingDestructor", 0xaff0a0, 32, 0xdedd924e, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cFileSource_AcceptsPath", 0xb05490, 32, 0x8925daee, reinterpret_cast<void *>(&A::AcceptsPath)},
    {"cFileSource_Exists", 0xb02370, 32, 0x8925daee, reinterpret_cast<void *>(&A::Exists)},
    {"cFileSource_ReadFile", 0xb035e0, 32, 0x47e774b7, reinterpret_cast<void *>(&A::ReadFile)},
    {"cFileSource_ResolvePath", 0xb05ec0, 32, 0xe28f6a88, reinterpret_cast<void *>(&A::ResolvePath)},
    {"cFileSource_CanonicalPath", 0xb02720, 18, 0xe30a6d14, reinterpret_cast<void *>(&A::CanonicalPath)},
    {"cFileSource_SetFileNamePath", 0x1c8789, 32, 0x29219cbc, reinterpret_cast<void *>(&A::SetFileNamePath)},
    {"cFileSource_SplitFileNamePath", 0x1c8beb, 32, 0x7a8805cb, reinterpret_cast<void *>(&A::SplitFileNamePath)},
};
REGISTER_HOOKS(kHooks);

} // namespace
