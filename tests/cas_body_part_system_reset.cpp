// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KA: ClearData (slot 3, 0xcf4f50). Every map's erase
// (0xcdc280, 0xcdc300, 0xcdb4a0, 0xcdb5e0, 0xcdb540, 0xcdb400, 0xcdb970, 0xcdb710), the record destructor 0xcdf820, the
// stack vector destructor 0xcdf230, 0xd0a9e0 and 0xcfebc0 are recording fakes, slot 16 is faked through the vtable,
// and the allocator service is wrapped by a recording one (allocation sizes and frees are compared). The image's
// operator new / sized delete (0x824632 / 0x824624) stand in for ours. The maps and stacks are built here per side
// (heads from the service, headers from operator new); afterwards each field is classified (untouched, emptied, or
// replaced by a fresh empty one) and the new objects freed.
#include "cas_body_part_system.h"

namespace bpt_reset {

using namespace bpt;
using nTSCASCore::TreeHeader;

// ---- a recording allocator service in the image's cache ----
struct RecService {
    const void *const *vtable;
} gRecService;
std::array<const void *, 8> gRecServiceVt;
void *gRealService;
void *__fastcall RecAlloc(RecService *, void *, uint32_t bytes)
{
    gEvents.push_back({0x5e3, bytes});
    auto alloc = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>((*reinterpret_cast<void ***>(gRealService))[3]);
    return alloc(gRealService, bytes);
}
void __fastcall RecFree(RecService *, void *, void *p)
{
    gEvents.push_back({0x5e4, static_cast<uint32_t>(p != nullptr)});
    auto free = reinterpret_cast<void(__thiscall *)(void *, void *)>((*reinterpret_cast<void ***>(gRealService))[4]);
    free(gRealService, p);
}
void ServiceFree(void *p)
{
    auto free = reinterpret_cast<void(__thiscall *)(void *, void *)>((*reinterpret_cast<void ***>(gRealService))[4]);
    free(gRealService, p);
}

// ---- the maps ----
// Trees by id: 0-3 the per-age maps, 4 +0x58 (inline), 5 +0x10, 6 +0xc, 7 +0x2c, 8 +0x14, 9 +0x20, 10 +0x28 (heap).
constexpr int kTrees = 11;
const int kHeapOffsets[6] = {0x10, 0x0c, 0x2c, 0x14, 0x20, 0x28};
TreeHeader *gTrees[kTrees];
TreeHeader::Node gInlineHeads[5];

uint32_t TreeId(const void *t)
{
    for (int i = 0; i < kTrees; ++i)
        if (t == gTrees[i]) return static_cast<uint32_t>(i);
    return 0xeeu;
}
uint32_t RootToken(const TreeHeader *t, const TreeHeader::Node *root)
{
    if (root == t->mHead) return 0xffffu; // an empty tree
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root));
}

template <uint32_t kRva> void __fastcall FakeErase(TreeHeader *self, void *, TreeHeader *a, TreeHeader::Node *root)
{
    gEvents.push_back({kRva, TreeId(self), TreeId(a), RootToken(self, root), self->mSize});
}

// ---- the stacks ----
uint8_t gRecords[2][6][0x40];
BodyPartEntryVector *gStacks[2]; // the redo stack, the modifier stack
uint32_t RecordId(const void *p)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 6; ++k)
            if (p == gRecords[s][k]) return static_cast<uint32_t>(s * 16 + k);
    return 0xeeu;
}
uint32_t StackId(const void *p)
{
    for (int s = 0; s < 2; ++s)
        if (p == gStacks[s]) return static_cast<uint32_t>(s);
    return 0xeeu;
}
void __fastcall F_cdf820(void *self, void *) { gEvents.push_back({0xcdf820, RecordId(self)}); }
void __fastcall F_cdf230(BodyPartEntryVector *self, void *)
{
    gEvents.push_back({0xcdf230, StackId(self), static_cast<uint32_t>(self->mLast - self->mFirst)});
}
void __fastcall F_d0a9e0(void *self, void *) { gEvents.push_back({0xd0a9e0, Rel(self)}); }
void __fastcall F_cfebc0(void *self, void *) { gEvents.push_back({0xcfebc0, Rel(self)}); }
void __fastcall Slot16(void *self, void *) { gEvents.push_back({16, Rel(self)}); }

void *__cdecl ImageNew(size_t size)
{
    return ::operator new(size);
}
void __cdecl ImageSizedDelete(void *p, size_t size)
{
    ::operator delete(p, size);
}

TreeHeader *MakeTree(int id, uint32_t token, uint32_t size)
{
    TreeHeader *t;
    TreeHeader::Node *head;
    if (id < 5) {
        head = &gInlineHeads[id];
        t = nullptr; // set by the caller (inline in the object)
    } else {
        head = static_cast<TreeHeader::Node *>(ServiceAlloc(0x80));
        t = new TreeHeader;
    }
    head->mLeft = reinterpret_cast<TreeHeader::Node *>(0x11110000u + token);
    head->mParent = reinterpret_cast<TreeHeader::Node *>(0x22220000u + token);
    head->mRight = reinterpret_cast<TreeHeader::Node *>(0x33330000u + token);
    reinterpret_cast<uint8_t *>(head)[0xc] = 0; // red and not nil: a fresh head is black and nil
    reinterpret_cast<uint8_t *>(head)[0xd] = 0;
    if (t != nullptr) {
        t->mHead = head;
        t->mSize = size;
    }
    return t;
}

// What became of a tree: 1 untouched (its token), 2 emptied in place, 3 a fresh empty tree (head linked to itself,
// black, nil).
uint32_t TreeState(const TreeHeader *t, uint32_t token)
{
    const TreeHeader::Node *h = t->mHead;
    const uint8_t *flags = reinterpret_cast<const uint8_t *>(h) + 0xc;
    const bool selfLinked = h->mLeft == h && h->mParent == h && h->mRight == h && t->mSize == 0;
    if (h->mParent == reinterpret_cast<TreeHeader::Node *>(0x22220000u + token)) return 1;
    if (selfLinked) return flags[0] == 1 && flags[1] == 1 ? 3 : 2;
    return 0xbad;
}

} // namespace bpt_reset

DIFF_TEST(cTSCASBodyPartSystem_ClearData, 0xcf4f50)
{
    using namespace bpt_reset;
    std::mt19937 rng(0xcf4f50);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xcf4f50);
    VTable vt = CopyVTable(ctx);
    vt.slots[16] = reinterpret_cast<const void *>(&Slot16);
    Env env(ctx);
    gRealService = env.object;
    gRecServiceVt = {};
    gRecServiceVt[3] = reinterpret_cast<const void *>(&RecAlloc);
    gRecServiceVt[4] = reinterpret_cast<const void *>(&RecFree);
    gRecService.vtable = gRecServiceVt.data();
    Redirect e1(ctx, 0xcdc280, reinterpret_cast<const void *>(&FakeErase<0xcdc280>));
    Redirect e2(ctx, 0xcdc300, reinterpret_cast<const void *>(&FakeErase<0xcdc300>));
    Redirect e3(ctx, 0xcdb4a0, reinterpret_cast<const void *>(&FakeErase<0xcdb4a0>));
    Redirect e4(ctx, 0xcdb5e0, reinterpret_cast<const void *>(&FakeErase<0xcdb5e0>));
    Redirect e5(ctx, 0xcdb540, reinterpret_cast<const void *>(&FakeErase<0xcdb540>));
    Redirect e6(ctx, 0xcdb400, reinterpret_cast<const void *>(&FakeErase<0xcdb400>));
    Redirect e7(ctx, 0xcdb970, reinterpret_cast<const void *>(&FakeErase<0xcdb970>));
    Redirect e8(ctx, 0xcdb710, reinterpret_cast<const void *>(&FakeErase<0xcdb710>));
    Redirect f1(ctx, 0xcdf820, reinterpret_cast<const void *>(&F_cdf820));
    Redirect f2(ctx, 0xcdf230, reinterpret_cast<const void *>(&F_cdf230));
    Redirect f3(ctx, 0xd0a9e0, reinterpret_cast<const void *>(&F_d0a9e0));
    Redirect f4(ctx, 0xcfebc0, reinterpret_cast<const void *>(&F_cfebc0));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    gEvents.reserve(512);
    for (int i = 0; i < 1500; ++i) {
        // every flag combination first (bits 0..4), then random ones with bits beyond them
        const uint32_t flags = i < 64 ? static_cast<uint32_t>(i) : static_cast<uint32_t>(rng());
        uint32_t tokens[kTrees], sizes[kTrees];
        for (int k = 0; k < kTrees; ++k) {
            tokens[k] = static_cast<uint32_t>(rng() % 1000);
            sizes[k] = static_cast<uint32_t>(rng() % 5);
        }
        int counts[2];
        for (int s = 0; s < 2; ++s) counts[s] = i < 12 ? i % 3 : static_cast<int>(rng() % 7); // 0..6 records
        Scenario sc = RandomScenario(rng);
        Compare(ctx, "ClearData", sc, vt, [&](World &w, bool original) -> uint64_t {
            uint8_t *const iface = w.Base() + kIfaceOffset;
            for (int k = 0; k < 5; ++k) {
                gTrees[k] = reinterpret_cast<TreeHeader *>(iface + (k < 4 ? 0x38 + 8 * k : 0x58));
                MakeTree(k, tokens[k], 0);
                gTrees[k]->mHead = &gInlineHeads[k];
                gTrees[k]->mSize = sizes[k];
            }
            for (int k = 0; k < 6; ++k) {
                gTrees[5 + k] = MakeTree(5 + k, tokens[5 + k], sizes[5 + k]);
                std::memcpy(iface + kHeapOffsets[k], &gTrees[5 + k], 4);
            }
            for (int s = 0; s < 2; ++s) {
                uint8_t *first = gRecords[s][0];
                gStacks[s] = new BodyPartEntryVector{reinterpret_cast<BodyPartEntry *>(first), reinterpret_cast<BodyPartEntry *>(first + 0x40 * counts[s]),
                                                     reinterpret_cast<BodyPartEntry *>(first + 0x40 * 6)};
                std::memcpy(iface + 0x18 + 4 * s, &gStacks[s], 4);
            }
            void *const savedCache = *env.cache;
            *env.cache = &gRecService;
            if (original) orig(w.S(), flags);
            else w.S()->ClearData(flags);
            *env.cache = savedCache;
            uint64_t h = 0;
            for (int k = 0; k < 5; ++k) h = h * 7 + TreeState(gTrees[k], tokens[k]);
            for (int k = 0; k < 6; ++k) {
                TreeHeader *t;
                std::memcpy(&t, iface + kHeapOffsets[k], 4);
                h = h * 31 + TreeState(t, tokens[5 + k]); // (a replaced header may reuse the old one's address)
                ServiceFree(t->mHead);
                delete t;
            }
            for (int s = 0; s < 2; ++s) {
                BodyPartEntryVector *v;
                std::memcpy(&v, iface + 0x18 + 4 * s, 4);
                h = h * 31 + 2 * RecordId(v->mFirst) + 512 * (v->mLast == v->mFirst) + 1024 * (v->mEnd == nullptr);
                delete v;
            }
            // the heap pointers differ between sides: the fields go back to the scenario's bytes before the compare
            for (int off : kHeapOffsets) std::memcpy(iface + off, sc.bytes + kIfaceOffset + off, 4);
            std::memcpy(iface + 0x18, &w.redo, 4);
            std::memcpy(iface + 0x1c, &w.stack, 4);
            for (int k = 0; k < 5; ++k) std::memcpy(iface + (k < 4 ? 0x38 + 8 * k : 0x58), sc.bytes + kIfaceOffset + (k < 4 ? 0x38 + 8 * k : 0x58), 8);
            return h;
        });
    }
}
