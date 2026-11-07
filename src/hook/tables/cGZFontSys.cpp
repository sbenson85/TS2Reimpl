// cGZFontSys (lane OA, src/gz/cGZFontSys.h): the COM director's creator for class 0x20673960 (the font system). Every
// font system is ours; the constructor 0x90f626 (reached only from the creator), the destructor 0x90f85c (only from the
// deleting destructor, slot 24) and the slots are in effect through it.
#include "gz/cGZFontSys.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZFontSys_Create", 0x909fd2, 22, 0xbfa30ad3, reinterpret_cast<void *>(&cGZFontSys::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
