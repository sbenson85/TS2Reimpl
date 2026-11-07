#pragma once
// The renderable level's world (lane PX, scenegraph N3): sg_transform_fx.h's blocks (a cRenderableNodeImpl is 0x70 bytes:
// words 24-27 are the layer mask, the word at +0x64, the cached detail limit and a zero) plus the two services the layer
// code asks, as stand-ins over their getters for the scope of a test:
//   the render-group registry (0xaeb370's object): slot 3 maps a name to a bit (a new name takes the first free bit), 4
//     counts the named bits, 5 maps a bit to its name (null for a bit without one);
//   the detail object (0xaead80's): slot 43 answers the limit.
// Both sides reach the stand-ins (the original's code at its RVA, ours through OriginalFunction at the same address).
#include "sg_transform_fx.h"

#include "code_patch.h"
#include "sg/DetailObject.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/cRenderableNodeImpl.h"

#include <cstring>
#include <string>

namespace sgrx {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

inline constexpr Slot kRegistrySlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"},
    {0, Ret::Int, true, "AddRef", ""},
    {0, Ret::Int, true, "Release", ""},
    {1, Ret::Int, true, "BitOfName", "s"}, // 3
    {0, Ret::Int, true, "Count", ""},      // 4
    {1, Ret::Int, true, "NameOfBit", "u"}, // 5
};
inline constexpr drawfx::Iface kRegistry{"RenderGroupRegistry", 6, kRegistrySlots};

struct Registry {
    Fake fake;
    Fake detail;
    std::string store[32];
    const char *names[32];

    static Registry &Get()
    {
        static Registry r;
        return r;
    }
};

inline uint32_t BitOfName(Fake &, const uint32_t *a)
{
    Registry &r = Registry::Get();
    const char *text = reinterpret_cast<const char *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 32; ++i)
        if (r.names[i] && std::strcmp(r.names[i], text) == 0) return i;
    for (unsigned i = 0; i < 32; ++i)
        if (!r.names[i]) {
            r.store[i] = text;
            r.names[i] = r.store[i].c_str();
            return i;
        }
    return 31;
}
inline uint32_t CountNames(Fake &, const uint32_t *)
{
    Registry &r = Registry::Get();
    uint32_t n = 0;
    for (const char *name : r.names) n += name != nullptr;
    return n;
}
inline uint32_t NameOfBit(Fake &, const uint32_t *a)
{
    Registry &r = Registry::Get();
    return a[0] < 32 ? drawfx::P(r.names[a[0]]) : 0;
}

inline void *StandRegistry() { return &Registry::Get().fake; }
inline void *StandDetail() { return &Registry::Get().detail; }

// The stand-ins over both getters.
struct Services {
    shape::StandIn registry{0xaeb370u, &nGZSceneGraph::RenderGroupRegistry, &StandRegistry};
    shape::StandIn detail{0xaead80u, &nGZSceneGraph::GetDetailObject, &StandDetail};
};

// A fresh registry with a random subset of the names bits named, and a detail object with a random limit.
inline void Setup(std::mt19937 &rng)
{
    static const char *const kNames[] = {"alpha", "beta", "gamma", "delta", "glow", "shadow", "reflect", "ui"};
    Registry &r = Registry::Get();
    r.fake.Reset<kRegistry>(470);
    r.fake.On(3, &BitOfName);
    r.fake.On(4, &CountNames);
    r.fake.On(5, &NameOfBit);
    r.detail.Reset<sgfx::kUnknown>(471);
    r.detail.Settle<43, 0>();
    r.detail.Returns(43, uint32_t(rng() % 1000));
    for (unsigned i = 0; i < 32; ++i) {
        r.names[i] = nullptr;
        r.store[i].clear();
        if (rng() % 3 == 0) continue; // (a bit without a name)
        r.store[i] = std::string(kNames[rng() % 8]) + char('a' + i % 26) + char('a' + i / 26);
        r.names[i] = r.store[i].c_str();
    }
}

// A random layer mask: empty, the default, everything, one bit or anything.
inline uint32_t RandomMask(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 0xffffffffu;
    case 3: return 1u << (rng() % 32);
    default: return uint32_t(rng());
    }
}

// The block's layer words for a test.
inline void RandomLayerWords(uint32_t *block, std::mt19937 &rng)
{
    block[24] = RandomMask(rng);
    block[25] = uint32_t(rng());
    block[26] = uint32_t(rng());
    block[27] = rng() % 2 ? 0 : uint32_t(rng());
}

} // namespace sgrx
