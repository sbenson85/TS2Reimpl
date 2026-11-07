// Differential tests for the material manager's three iterator classes (lane TI, src/sg/cMaterialIterators.*):
// cMaterialHandleIterator (vtable 0xe785b4), cRenderShaderContextIterator (0xe785d8) and cShaderIterator (0xe785fc).
//
// One test per class, the same seeded script run twice, on the game's objects (the image's constructors and operator
// new through the image, every slot through the object's own vtable) and on ours (the class's constructors, `new` and
// the same slots through our vtable). Every step is noted: what the call answered, the object's words after the vtable
// (the count and the position), and the class's pool (its free list and used count). The two notes lists must be equal.
// The script: construct at a random place (the constructors; the handle iterator's moves on to the first live index),
// copy-construct, Clone, AddRef / Release (the last release deletes: the deleting destructor 0xaca3e0 / 0xaca450 /
// 0xaca490 and the operator delete behind it), the destructor alone (flags 0) then deleting (flags 1), IsEqual, Get, Next,
// QueryInterface (cIGZUnknown, other ids, no output), and raw operator new / delete.
//
// RVAs this file verifies. Handles: constructor 0xac9610, copy 0xac95e0, vtable store 0xac9de0, operator new 0xaca0e0,
// delete 0xaca170, Clone 0xacaca0, deleting destructor 0xaca3e0. Variants: constructor 0xac9b20, copy 0xac9b00, vtable
// store 0xaca0c0, new 0xaca110, delete 0xaca1f0, Clone 0xacacd0, deleting destructor 0xaca450, Get 0xacb130. Listeners:
// constructor 0xac9b60, copy 0xac9b40, vtable store 0xaca0d0, new 0xaca140, delete 0xaca270, Clone 0xacad00, deleting
// destructor 0xaca490, Get 0xacb150. Both range classes' Next 0xacc4b0. The shared slot bodies (QueryInterface 0x178b20,
// AddRef 0x148fb0, Release 0x1a84c0, IsEqual 0x17b680, the handle iterator's Get 0x46390 and Next 0xacc480) are
// exercised through every object but are not under test here (other classes keep them).
//
// The pools (0x10f6818, 0x10f6824, 0x10f6830) point at a test page of their own, far from empty and from full, so
// neither side reaches the page bookkeeping (game code not under test); both sides start from the same page contents.
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "sg/cMaterialIterators.h"

#include <malloc.h>

#include <cstring>
#include <random>
#include <vector>

using nGZSceneGraph::MaterialListNode;

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

enum class Kind { Handle, Variant, Listener };

constexpr uint32_t kShaderIid = 0xcd2e1781;
constexpr uint32_t kNodes = 7;

// A listener: slot 0 (QueryInterface) records the call and answers a pointer derived from the object and the id.
struct Listener {
    void **vtable;
    uint32_t id;
};
std::vector<std::vector<uint32_t>> *gNotes = nullptr;
void **gListenerVt = nullptr;

uint32_t __fastcall ListenerQuery(Listener *self, void *, uint32_t iid, void **out)
{
    if (gNotes) gNotes->push_back({0x51, self->id, iid});
    *out = reinterpret_cast<void *>(uintptr_t(0x70000000u + self->id * 0x100 + (iid & 0xff)));
    return 1;
}

// The data both sides walk: an entry vector with holes, and a circular list of nodes round a head.
struct Data {
    std::vector<void *> entries;
    MaterialListNode nodes[kNodes + 1]; // [kNodes] is the head
    Listener listeners[kNodes + 1];
    void Build(std::mt19937 &rng)
    {
        const uint32_t size = 1 + rng() % 9;
        entries.assign(size, nullptr);
        for (uint32_t i = 0; i < size; ++i)
            if (rng() % 3 != 0) entries[i] = reinterpret_cast<void *>(uintptr_t(0x1000 + i * 8));
        for (uint32_t i = 0; i <= kNodes; ++i) {
            nodes[i].next = &nodes[i == kNodes ? 0 : i + 1];
            nodes[i].prev = &nodes[i == 0 ? kNodes : i - 1];
            for (uint32_t k = 0; k < 4; ++k) nodes[i].value[k] = rng();
            {
                listeners[i].vtable = gListenerVt;
                listeners[i].id = i + 1;
                nodes[i].value[0] = P(&listeners[i]);
            }
        }
    }
};

struct Pool {
    uint32_t rva, block, tag;
    uint8_t *page = nullptr;
    uint32_t saved = 0;
    static constexpr uint32_t kPage = 0x800;
    void Attach()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(rva + 4);
        *GameAddress<uint32_t>(rva + 4) = P(page);
    }
    void Detach()
    {
        *GameAddress<uint32_t>(rva + 4) = saved;
        _aligned_free(page);
        page = nullptr;
    }
    void Reset()
    {
        std::memset(page, 0, kPage);
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        header[3] = 1000;
        uint32_t next = 0;
        for (uint32_t at = kPage - block; at >= 0x10; at -= block) {
            *reinterpret_cast<uint32_t *>(page + at) = next;
            next = P(page + at);
        }
        header[2] = next;
    }
    void Note(std::vector<std::vector<uint32_t>> &notes) const
    {
        const uint32_t *header = reinterpret_cast<const uint32_t *>(page);
        notes.push_back({0x70, header[2], header[3]});
    }
};

// All three pools, attached and noted together: a block freed into the wrong class's pool shows.
struct PoolSet {
    Pool pools[3] = {{0x10f6818, 0x10, 0}, {0x10f6824, 0xc, 0}, {0x10f6830, 0xc, 0}};
    void Attach() { for (Pool &p : pools) p.Attach(); }
    void Detach() { for (Pool &p : pools) p.Detach(); }
    void Reset() { for (Pool &p : pools) p.Reset(); }
    void Note(std::vector<std::vector<uint32_t>> &notes) const { for (const Pool &p : pools) p.Note(notes); }
};

struct Rvas {
    uint32_t vtable, ctor, copy, newFn, deleteFn;
};

constexpr Rvas kRvas[3] = {
    {0xe785b4, 0xac9610, 0xac95e0, 0xaca0e0, 0xaca170},
    {0xe785d8, 0xac9b20, 0xac9b00, 0xaca110, 0xaca1f0},
    {0xe785fc, 0xac9b60, 0xac9b40, 0xaca140, 0xaca270},
};

using Notes = std::vector<std::vector<uint32_t>>;

struct Live {
    void *object;
    int refs; // our own model: both sides see the same sequence
};

// One side's run of the script.
struct Runner {
    difftest::Context &ctx;
    Kind kind;
    bool game;
    Data &data;
    PoolSet &pool;
    Notes notes;
    std::vector<Live> live;
    std::vector<void *> raw;
    std::mt19937 rng;

    Runner(difftest::Context &c, Kind k, bool g, Data &d, PoolSet &p, uint32_t seed) : ctx(c), kind(k), game(g), data(d), pool(p), rng(seed) {}

    uint32_t Words() const { return kind == Kind::Handle ? 3 : 2; }

    void NoteObject(const char *what, void *object)
    {
        std::vector<uint32_t> n{uint32_t(what[0]), uint32_t(what[1])};
        if (object == nullptr) {
            n.push_back(0xffffffff);
        } else {
            const uint32_t *w = static_cast<const uint32_t *>(object);
            for (uint32_t i = 1; i <= Words(); ++i) n.push_back(w[i]);
        }
        notes.push_back(n);
        pool.Note(notes);
    }

    const MaterialListNode *PickNode()
    {
        return &data.nodes[rng() % (kNodes + 1)];
    }

    void *NewBlock()
    {
        const Rvas &r = kRvas[int(kind)];
        if (game) return ctx.Original<void *(__cdecl *)(uint32_t)>(r.newFn)(kind == Kind::Handle ? 0x10 : 0xc);
        switch (kind) {
        case Kind::Handle: return nGZSceneGraph::cMaterialHandleIterator::operator new(0x10);
        case Kind::Variant: return nGZSceneGraph::cRenderShaderContextIterator::operator new(0xc);
        default: return nGZSceneGraph::cShaderIterator::operator new(0xc);
        }
    }

    void FreeBlock(void *block)
    {
        const Rvas &r = kRvas[int(kind)];
        if (game) {
            ctx.Original<void(__cdecl *)(void *)>(r.deleteFn)(block);
            return;
        }
        switch (kind) {
        case Kind::Handle: nGZSceneGraph::cMaterialHandleIterator::operator delete(block); break;
        case Kind::Variant: nGZSceneGraph::cRenderShaderContextIterator::operator delete(block); break;
        default: nGZSceneGraph::cShaderIterator::operator delete(block); break;
        }
    }

    // A new object at a random place through the constructor.
    void *Construct()
    {
        const Rvas &r = kRvas[int(kind)];
        if (kind == Kind::Handle) {
            const uint32_t index = rng() % (uint32_t(data.entries.size()) + 2);
            if (game) {
                void *block = NewBlock();
                return ctx.Original<void *(__thiscall *)(void *, const void *, uint32_t)>(r.ctor)(block, &data.entries, index);
            }
            return new nGZSceneGraph::cMaterialHandleIterator(&data.entries, index);
        }
        const MaterialListNode *node = PickNode();
        if (game) {
            void *block = NewBlock();
            return ctx.Original<void *(__thiscall *)(void *, const void *)>(r.ctor)(block, &node);
        }
        if (kind == Kind::Variant) return new nGZSceneGraph::cRenderShaderContextIterator(node);
        return new nGZSceneGraph::cShaderIterator(node);
    }

    void *CopyConstruct(void *other)
    {
        const Rvas &r = kRvas[int(kind)];
        if (game) {
            void *block = NewBlock();
            return ctx.Original<void *(__thiscall *)(void *, const void *)>(r.copy)(block, other);
        }
        switch (kind) {
        case Kind::Handle: return new nGZSceneGraph::cMaterialHandleIterator(*static_cast<nGZSceneGraph::cMaterialHandleIterator *>(other));
        case Kind::Variant: return new nGZSceneGraph::cRenderShaderContextIterator(*static_cast<nGZSceneGraph::cRenderShaderContextIterator *>(other));
        default: return new nGZSceneGraph::cShaderIterator(*static_cast<nGZSceneGraph::cShaderIterator *>(other));
        }
    }

    uint32_t AddRef(void *o) { return VirtualSlot<uint32_t(__thiscall *)(void *)>(o, 1)(o); }
    uint32_t Release(void *o) { return VirtualSlot<uint32_t(__thiscall *)(void *)>(o, 2)(o); }

    void Track(void *o)
    {
        if (o == nullptr) return;
        live.push_back({o, 0});
        Add(live.back());
    }
    void Add(Live &l)
    {
        const uint32_t n = AddRef(l.object);
        ++l.refs;
        notes.push_back({0x41, n});
        NoteObject("ar", l.object);
    }

    void Get(void *o)
    {
        if (kind == Kind::Handle) {
            notes.push_back({0x47, VirtualSlot<uint32_t(__thiscall *)(void *)>(o, 5)(o)});
        } else if (kind == Kind::Variant) {
            uint32_t out[5] = {0xdead0000u, 0xdead0001u, 0xdead0002u, 0xdead0003u, 0xdead0004u};
            void *back = VirtualSlot<void *(__thiscall *)(void *, uint32_t *)>(o, 5)(o, out);
            notes.push_back({0x47, out[0], out[1], out[2], out[3], out[4], uint32_t(back == out)});
        } else {
            notes.push_back({0x47, P(VirtualSlot<void *(__thiscall *)(void *)>(o, 5)(o))});
        }
    }

    void Step()
    {
        const uint32_t op = rng() % 100;
        if (live.empty() || op < 14) {
            Track(Construct());
            return;
        }
        Live &l = live[rng() % live.size()];
        void *o = l.object;
        if (op < 22) {
            Track(CopyConstruct(o));
        } else if (op < 32) {
            void *c = VirtualSlot<void *(__thiscall *)(void *)>(o, 3)(o);
            notes.push_back({0x43, c != nullptr});
            Track(c);
        } else if (op < 40) {
            Add(l);
        } else if (op < 52) {
            if (l.refs > 0) {
                const uint32_t n = Release(o);
                --l.refs;
                notes.push_back({0x52, n});
                if (n == 0) {
                    pool.Note(notes);
                    live.erase(live.begin() + (&l - live.data()));
                } else {
                    NoteObject("re", o);
                }
            }
        } else if (op < 62) {
            Live &b = live[rng() % live.size()];
            const uint32_t eq = VirtualSlot<uint32_t(__thiscall *)(void *, void *)>(o, 4)(o, b.object) & 0xff;
            notes.push_back({0x45, eq});
        } else if (op < 72) {
            Get(o);
        } else if (op < 86) {
            VirtualSlot<void(__thiscall *)(void *)>(o, 6)(o);
            NoteObject("nx", o);
        } else if (op < 92) {
            void *out = nullptr;
            const uint32_t iid = rng() % 3 == 0 ? 1 : rng() % 2 ? 0x1234 : 0;
            const bool noOut = rng() % 4 == 0;
            const uint32_t ok = VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t, void **)>(o, 0)(o, iid, noOut ? nullptr : &out) & 0xff;
            notes.push_back({0x71, iid, noOut, ok, out == o});
            if (ok && out) {
                const uint32_t n = Release(out);
                notes.push_back({0x72, n});
            }
            NoteObject("qi", o);
        } else if (op < 96) {
            // The destructor alone on an object nobody holds (flags 0), then deleting (flags 1).
            void *fresh = Construct();
            NoteObject("d0", fresh);
            VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(fresh, 7)(fresh, 0);
            NoteObject("d1", fresh);
            VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(fresh, 7)(fresh, 1);
            pool.Note(notes);
        } else {
            // Raw operator new / delete.
            if (raw.empty() || rng() % 2) {
                void *b = NewBlock();
                raw.push_back(b);
                notes.push_back({0x6e, P(b)});
            } else {
                const size_t i = rng() % raw.size();
                FreeBlock(raw[i]);
                raw.erase(raw.begin() + i);
                notes.push_back({0x6f});
            }
            pool.Note(notes);
        }
    }

    void Finish()
    {
        while (!live.empty()) {
            Live &l = live.back();
            while (l.refs > 0) {
                const uint32_t n = Release(l.object);
                --l.refs;
                notes.push_back({0x52, n});
            }
            live.pop_back();
            pool.Note(notes);
        }
        for (void *b : raw) FreeBlock(b);
        raw.clear();
        pool.Note(notes);
    }
};

void RunKind(difftest::Context &ctx, Kind kind)
{
    void *vt[1] = {reinterpret_cast<void *>(&ListenerQuery)};
    gListenerVt = vt;
    PoolSet pool;
    pool.Attach();
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        std::mt19937 build(seed * 7919u);
        Data data;
        data.Build(build);
        Notes both[2];
        for (int side = 0; side < 2; ++side) {
            pool.Reset();
            Runner runner(ctx, kind, side == 0, data, pool, seed);
            gNotes = &runner.notes;
            const int steps = 60 + int(seed % 40);
            for (int i = 0; i < steps; ++i) runner.Step();
            runner.Finish();
            gNotes = nullptr;
            both[side] = std::move(runner.notes);
        }
        ++ctx.cases;
        size_t i = 0;
        for (; i < both[0].size() && i < both[1].size(); ++i)
            if (both[0][i] != both[1][i]) break;
        if (i != both[0].size() || i != both[1].size()) {
            char text[160];
            int n = std::snprintf(text, sizeof(text), "seed %u step %zu of %zu/%zu: game", seed, i, both[0].size(), both[1].size());
            if (i < both[0].size())
                for (uint32_t w : both[0][i]) n += std::snprintf(text + n, sizeof(text) - n, " %x", w);
            n += std::snprintf(text + n, sizeof(text) - n, " ours");
            if (i < both[1].size())
                for (uint32_t w : both[1][i]) n += std::snprintf(text + n, sizeof(text) - n, " %x", w);
            ctx.Fail("%s", text);
            break;
        }
    }
    pool.Detach();
}

} // namespace

DIFF_TEST(SG_MaterialIterators_Handle, 0xac9610)
{
    RunKind(ctx, Kind::Handle);
}

DIFF_TEST(SG_MaterialIterators_Variant, 0xac9b20)
{
    RunKind(ctx, Kind::Variant);
}

DIFF_TEST(SG_MaterialIterators_Listener, 0xac9b60)
{
    RunKind(ctx, Kind::Listener);
}
