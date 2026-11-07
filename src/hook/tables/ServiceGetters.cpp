#include "hook/hooks.h"
#include "rz/ServiceGetters.h"

namespace {

// Cached service getters (see rz/ServiceGetters.h). Each replacement is the instantiation with the
// cache global and identifiers of that getter.
const HookSpec kHooks[] = {
    {"FUN_a0b390c0", 0xb390c0, 32, 0x5d8cae23, reinterpret_cast<void *>(&CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>)},
    {"FUN_a0b39110", 0xb39110, 32, 0x85cd257f, reinterpret_cast<void *>(&CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>)},
    {"FUN_a0b38ea0", 0xb38ea0, 32, 0xf66c20e9, reinterpret_cast<void *>(&CachedServiceGetter<0x10faf70, 0xeb7d6513, 0xcb7d64f8>)},
    {"FUN_a0b38e50", 0xb38e50, 32, 0x8554fd53, reinterpret_cast<void *>(&CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>)},
    {"FUN_a0b38fb0", 0xb38fb0, 32, 0x2cb7337f, reinterpret_cast<void *>(&CachedServiceGetter<0x10faf80, 0x0dc7057a, 0xcdb467ae>)},
    {"FUN_a0b8bcb0", 0xb8bcb0, 32, 0xc619ce0f, reinterpret_cast<void *>(&CachedServiceGetter<0x10fc684, 0x056b906e, 0x9af5e924>)},
    {"FUN_a0b9a990", 0xb9a990, 32, 0x5da690b8, reinterpret_cast<void *>(&CachedServiceGetter<0x10fc6c0, 0x4bcf1bb0, 0x0bcf1b58>)},
    {"FUN_a0b9a940", 0xb9a940, 32, 0x8d7d1989, reinterpret_cast<void *>(&CachedServiceGetter<0x10fc6bc, 0x056b906e, 0x9af5e924>)},
};
REGISTER_HOOKS(kHooks);

} // namespace
