// Hooks for cGZScriptTerp and cGZScriptVocab (src/gz/cGZScriptTerp.size.cpp), lane MC: the standalone shape. The script
// director's creators 0x859fe1 (terp, class 0xba2e7953) and 0x859ff7 (vocabulary, class 0xba2e7954) are the only ways
// to the constructors 0x85a202 / 0x85a288 (tail jumps; the terp's constructor calls the vocabulary's, ours too), and the
// vtables 0xe47e50 / 0xe47e28 are stored only by those constructors and the destructors 0x85a2c7 / 0x85a31d: with
// these hooks every terp and vocabulary carries our vtables, and the slots and the parser are in effect through
// callers.
#include "gz/cGZScriptTerp.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZScriptTerp_Create", 0x859fe1, 22, 0x38555b13, reinterpret_cast<void *>(&cGZScriptTerp::Create)},
    {"cGZScriptVocab_Create", 0x859ff7, 22, 0xfb647ca4, reinterpret_cast<void *>(&cGZScriptVocab::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
