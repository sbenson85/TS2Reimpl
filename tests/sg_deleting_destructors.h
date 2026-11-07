// Lane IR's deleting-destructor check (tests/sg_*.cpp): the destructor (still the original's) is stood in over the image
// with a logging stand-in, the object is a heap block of the class's size, and FreeRecorder says whether it was freed.
// Compared per call: the stand-in's log (the object normalised), the returned object and whether the block was freed.
#pragma once

#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "game/game.h"

#include <cstdint>
#include <new>
#include <random>

namespace sgdel {

inline void __fastcall StandDestructor(void *self, void *)
{
    svcfx::Ev(0xde), svcfx::Ev(svcfx::Norm(svcfx::P(self)));
}

// `original` and `ours` take (object, flags) and return the object.
template <class Original, class Ours>
void Run(difftest::Context &ctx, uint32_t destructorRva, uint32_t size, Original original, Ours ours, uint32_t seedBase)
{
    using namespace svcfx;
    shape::CodePatch destructor{GameAddress<void>(destructorRva), shape::CodeOf(&StandDestructor)};
    {
        // gEv's first growth frees its old buffer inside the recorded window of whichever side logs first, so the
        // free counts differed when the test ran without an earlier test having grown it (lane QM saw it alone).
        difftest::NotCounted bookkeeping;
        gEv.reserve(gEv.size() + 64);
    }
    for (uint32_t seed = 0; seed < 64; ++seed) {
        std::mt19937 gen(seed * 7919u + seedBase);
        const uint32_t flags = seed % 4 == 3 ? gen() : gen() % 4;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            auto *object = static_cast<uint8_t *>(::operator new(size));
            std::mt19937 fill(seed);
            for (uint32_t i = 0; i < size; ++i) object[i] = uint8_t(fill());
            AddRange(object, size, 0x5e000000);
            ctx.Mark(side ? "ours" : "original");
            difftest::FreeRecorder frees;
            void *result = side == 0 ? original(object, flags) : ours(object, flags);
            frees.Stop();
            Blob &o = outcome[side];
            o = gEv;
            o.push_back(Norm(P(result)));
            const bool freed = frees.Freed(object);
            o.push_back(freed);
            o.push_back(uint32_t(frees.Count()));
            if (!freed) ::operator delete(object);
        }
        Same(ctx, outcome[0], outcome[1], "deleting_destructor", seed);
    }
}

} // namespace sgdel
