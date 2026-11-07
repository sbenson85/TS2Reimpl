// Differential tests for the SG resource blockers (src/sg/SGResourceBlockers.cpp).
#include "difftest.h"
#include "sg/SGResourceBlockers.h"

#include <cstring>
#include <iterator>
#include <random>

namespace {

// 8 bytes; `this` is at byte 4, so the helper reads the dword at bytes 4..7. Bytes 0..3 are context that must not
// change.
struct Buffer {
    uint8_t bytes[8];
};

} // namespace

// 0x18c080 (`cmp [ecx],-1; sete al; ret`): returns 1 only for the dword 0xffffffff. Compared as a full byte, with the
// dword and the buffer's other bytes checked for changes. The upper eax bits are not compared: the original leaves
// them as they were, and a C++ function cannot name them.
DIFF_TEST(sg_invalid_dword_tag, 0x18c080)
{
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(0x18c080);
    std::mt19937 gen(0x18c080);

    const uint32_t edges[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu, 0xffffff00u, 0x00ffffffu, 0xff00ffffu};
    for (uint32_t i = 0; i < 4000; ++i) {
        uint32_t word = i < std::size(edges) ? edges[i] : gen();
        if (i % 7 == 0) word = 0xffffffffu;
        Buffer a;
        for (auto &x : a.bytes) x = static_cast<uint8_t>(gen());
        std::memcpy(a.bytes + 4, &word, sizeof(word));
        Buffer b;
        std::memcpy(b.bytes, a.bytes, sizeof(a.bytes));

        uint8_t want = original(a.bytes + 4);
        uint8_t got = reinterpret_cast<SGResourceBlockers::cInvalidDwordTag *>(b.bytes + 4)->IsInvalid();
        if (want != got) ctx.Fail("sg_invalid_dword_tag: word %08x original %02x ours %02x", word, want, got);
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("sg_invalid_dword_tag: buffer changed for %08x", word);
        ++ctx.cases;
    }
}
