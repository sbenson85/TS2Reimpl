#include "game/game.h"

#include <cstdio>
#include <cstdlib>

uintptr_t GameBase()
{
    return reinterpret_cast<uintptr_t>(ts2_rva_0);
}

void SetGameBase(uintptr_t base)
{
    if (base != kGameBase || GameBase() != kGameBase) {
        std::fprintf(stderr, "the game image is mapped at %p, but this build references it at %p\n",
                     reinterpret_cast<void *>(base), reinterpret_cast<void *>(GameBase()));
        std::abort();
    }
}
