// Hooks for cRZLanguageUtility (src/rz/cRZLanguageUtility.size.cpp), lane LS: the standalone shape. The constructor
// (0xc6ed94, called by the language manager's constructor and its slot 39) builds our class, so every utility carries our
// vtable; the slot functions are reached only through the game's vtable, which nothing stores any more (in effect through
// callers, trapped). The encoding helper 0xc6f07a is called directly by the original slots (none left once the
// manager is ours) and is hooked as the function it is.
#include "hook/hooks.h"
#include "rz/cRZLanguageUtility.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZLanguageUtility_Construct", 0xc6ed94, 32, 0x82678ca9, reinterpret_cast<void *>(&cRZLanguageUtilityAdapters::Construct)},
    {"cRZLanguageUtility_ToDefaultEncoding", 0xc6f07a, 32, 0x0559eaab, reinterpret_cast<void *>(&cRZLanguageUtility::ToDefaultEncoding)},
};
REGISTER_HOOKS(kHooks);

} // namespace
