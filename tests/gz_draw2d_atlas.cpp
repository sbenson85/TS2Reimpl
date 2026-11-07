// Differential tests for nGZGraphic4::cTextureAtlas (src/gz/cTextureAtlas.h, lane OL): random Init / Allocate / AllocateIn /
// AddNode / Free / Clear sequences run on the original packer (0x18bb40 and friends, on a raw 0x20-byte object) and on ours
// in lockstep. After every step the return value and the whole object are compared: counters, the node vector (size,
// capacity, every field; the three padding bytes after `used` are left as the allocation had them and are not compared)
// and the free-node list (size, capacity, elements). The original's vector storage is left to the process (the original
// allocates through its own operator new); only small sequences run.
#include "difftest.h"
#include "gz/cTextureAtlas.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using nGZGraphic4::cAtlasNode;
using nGZGraphic4::cTextureAtlas;

using CtorFn = void *(__thiscall *)(void *);
using InitFn = void(__thiscall *)(void *, int32_t, int32_t);
using ClearFn = void(__thiscall *)(void *);
using AllocFn = int32_t(__thiscall *)(void *, int32_t, int32_t);
using AllocInFn = int32_t(__thiscall *)(void *, int32_t, uint32_t, uint32_t);
using AddNodeFn = uint32_t(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t, int32_t);
using FreeFn = void(__thiscall *)(void *, int32_t);

struct Raw { // the original's object: the same 0x20 bytes
    uint32_t regions;
    int32_t freeArea;
    uint8_t *nodesBegin, *nodesEnd, *nodesCap;
    uint8_t *freeBegin, *freeEnd, *freeCap;
};
static_assert(sizeof(Raw) == 0x20, "raw");

struct Fns {
    CtorFn ctor;
    InitFn init;
    ClearFn clear;
    AllocFn alloc;
    AllocInFn allocIn;
    AddNodeFn addNode;
    FreeFn free;
};

// Empty when equal, else a description of the first difference.
std::string Compare(const Raw &o, const cTextureAtlas &w)
{
    char buf[200];
    if (o.regions != w.mRegions || o.freeArea != w.mFreeArea) {
        snprintf(buf, sizeof buf, "counters: original %u/%d ours %u/%d", o.regions, o.freeArea, w.mRegions, w.mFreeArea);
        return buf;
    }
    const size_t on = (o.nodesEnd - o.nodesBegin) / sizeof(cAtlasNode), oc = (o.nodesCap - o.nodesBegin) / sizeof(cAtlasNode);
    if (on != w.mNodes.size() || oc != w.mNodes.capacity()) {
        snprintf(buf, sizeof buf, "nodes size/capacity: original %zu/%zu ours %zu/%zu", on, oc, w.mNodes.size(),
                 w.mNodes.capacity());
        return buf;
    }
    for (size_t i = 0; i < on; ++i) {
        const auto *a = reinterpret_cast<const cAtlasNode *>(o.nodesBegin) + i;
        const cAtlasNode &b = w.mNodes[i];
        if (std::memcmp(&a->rect, &b.rect, sizeof(a->rect)) || a->parent != b.parent || a->first != b.first ||
            a->second != b.second || (a->used != 0) != b.used) {
            snprintf(buf, sizeof buf, "node %zu differs", i);
            return buf;
        }
    }
    const size_t fn = (o.freeEnd - o.freeBegin) / 4, fc = (o.freeCap - o.freeBegin) / 4;
    if (fn != w.mFreeNodes.size() || fc != w.mFreeNodes.capacity()) {
        snprintf(buf, sizeof buf, "free list size/capacity: original %zu/%zu ours %zu/%zu", fn, fc, w.mFreeNodes.size(),
                 w.mFreeNodes.capacity());
        return buf;
    }
    if (fn && std::memcmp(o.freeBegin, w.mFreeNodes.data(), fn * 4)) return "free list elements differ";
    return {};
}

} // namespace

DIFF_TEST(GZTextureAtlas_Sequences, 0x18bdf0)
{
    const Fns f = {ctx.Original<CtorFn>(0x18bb40),    ctx.Original<InitFn>(0x18bf00),   ctx.Original<ClearFn>(0x18bf30),
                   ctx.Original<AllocFn>(0x18bdf0),   ctx.Original<AllocInFn>(0x18bb80), ctx.Original<AddNodeFn>(0x18bd40),
                   ctx.Original<FreeFn>(0x18be20)};
    std::mt19937 gen(0x18bdf0);
    for (int run = 0; run < 300; ++run) {
        Raw o;
        std::memset(&o, 0xcd, sizeof o);
        f.ctor(&o);
        cTextureAtlas w;
        std::vector<int32_t> held; // regions handed out and not yet freed
        const int32_t size = 16 << (gen() % 5);
        const int32_t height = size / (1 + static_cast<int32_t>(gen() % 2));
        f.init(&o, size, height);
        w.Init(size, height);
        for (int step = 0; step < 120; ++step) {
            const int op = gen() % 12;
            int32_t ro = 0, rw = 0;
            const char *name = "";
            if (op < 6) {
                name = "Allocate";
                const int32_t aw = 1 + gen() % (size / 2 + 4), ah = 1 + gen() % (size / 2 + 4);
                ro = f.alloc(&o, aw, ah);
                rw = w.Allocate(aw, ah);
                if (ro != -1) held.push_back(ro);
            } else if (op < 9) {
                name = "Free";
                if (held.empty()) continue;
                const size_t k = gen() % held.size();
                f.free(&o, held[k]);
                w.Free(held[k]);
                held.erase(held.begin() + k);
            } else if (op == 9) {
                name = "AllocateIn";
                const size_t n = (o.nodesEnd - o.nodesBegin) / sizeof(cAtlasNode);
                if (!n) continue;
                const int32_t node = gen() % n;
                const uint32_t aw = 1 + gen() % (size / 2 + 4), ah = 1 + gen() % (size / 2 + 4);
                // A node on the free list is not part of the tree; splitting it would hand itself out as its own child.
                bool recycled = false;
                for (const uint32_t *p = reinterpret_cast<const uint32_t *>(o.freeBegin);
                     p != reinterpret_cast<const uint32_t *>(o.freeEnd); ++p)
                    recycled |= *p == static_cast<uint32_t>(node);
                if (recycled) continue;
                ro = f.allocIn(&o, node, aw, ah);
                rw = w.AllocateIn(node, aw, ah);
            } else if (op == 10) {
                name = "AddNode";
                const int32_t a = gen() % 100, b = gen() % 100, c = gen() % 100, d = gen() % 100,
                              p = static_cast<int32_t>(gen() % 4) - 1;
                ro = static_cast<int32_t>(f.addNode(&o, a, b, c, d, p));
                rw = static_cast<int32_t>(w.AddNode(a, b, c, d, p));
            } else {
                name = "Clear";
                if (gen() % 4) continue;
                f.clear(&o);
                w.Clear();
                held.clear();
                f.init(&o, size, height);
                w.Init(size, height);
            }
            ++ctx.cases;
            if (ro != rw) ctx.Fail("run %d step %d %s: result original %d ours %d", run, step, name, ro, rw);
            const std::string diff = Compare(o, w);
            if (!diff.empty()) {
                ctx.Fail("run %d step %d %s: %s", run, step, name, diff.c_str());
                break;
            }
        }
    }
}

DIFF_TEST(GZTextureAtlas_Constructor, 0x18bb40)
{
    const auto construct = ctx.Original<CtorFn>(0x18bb40);
    for (int round = 0; round < 8; ++round) {
        Raw o;
        std::memset(&o, 0x5a + round, sizeof o);
        void *r = construct(&o);
        ++ctx.cases;
        if (r != &o) ctx.Fail("constructor returned %p, not this", r);
        const cTextureAtlas w;
        const std::string diff = Compare(o, w);
        if (!diff.empty()) ctx.Fail("constructor: %s", diff.c_str());
        if (o.nodesBegin || o.freeBegin) ctx.Fail("constructor left pointers");
    }
}
