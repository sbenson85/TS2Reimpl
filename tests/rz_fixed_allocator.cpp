// Differential tests for the shared base of cRZFastFixedAllocator2 (src/rz/RZFixedAllocator.*).
//
// Each side has a pool object and 32 pages (2 KB-aligned); a fake vtable's slot 2 hands out the side's
// next unused page and slot 3 records freed pages. Random sequences of the four operations run on
// both sides; after each one the return value and the state are compared with pages as indices: both
// rings (walked forwards, and checked backwards), every page's links, free-list head and use count,
// and the slot calls made.
#include "difftest.h"
#include "rz/RZFixedAllocator.h"
#include "rz/RZFrameworkHelpers.h"

#include <malloc.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kPages = 32;

struct Side {
    uint8_t *pages[kPages];
    int nextFresh = 0;
    std::vector<int> calls; // slot 2 -> 1000 + page index handed out; slot 3 -> freed page index
} gSide[2];
int gActive = 0;

int IndexOf(int side, const void *p)
{
    for (int i = 0; i < kPages; ++i)
        if (gSide[side].pages[i] == p) return i;
    return p ? -2 : -1;
}

RZFixedPage *__fastcall FakeNewPage(void *, void *)
{
    Side &s = gSide[gActive];
    int i = s.nextFresh++ % kPages;
    auto *page = reinterpret_cast<RZFixedPage *>(s.pages[i]);
    page->next = page->prev = nullptr;
    page->freeList = s.pages[i] + 0x10;
    page->used = 0;
    s.calls.push_back(1000 + i);
    return page;
}

void __fastcall FakeFreePage(void *, void *, RZFixedPage *page)
{
    gSide[gActive].calls.push_back(IndexOf(gActive, page));
}

void *gVTable[4] = {nullptr, nullptr, reinterpret_cast<void *>(&FakeNewPage), reinterpret_cast<void *>(&FakeFreePage)};

std::string Ring(int side, RZFixedPage *start)
{
    std::string out;
    if (!start) return "null";
    RZFixedPage *p = start;
    for (int n = 0; n <= kPages; ++n) {
        out += std::to_string(IndexOf(side, p));
        if (p->next->prev != p) out += "!";
        p = p->next;
        if (p == start) return out;
        out += ",";
    }
    return out + "...";
}

std::string Describe(int side, const RZFixedAllocatorBase &pool)
{
    std::string out = "cur " + Ring(side, pool.mCurrent) + " full " + Ring(side, pool.mFull) + " pages";
    for (int i = 0; i < kPages; ++i) {
        auto *p = reinterpret_cast<RZFixedPage *>(gSide[side].pages[i]);
        out += " " + std::to_string(IndexOf(side, p->next)) + "/" + std::to_string(IndexOf(side, p->prev)) + "/" +
               std::to_string(reinterpret_cast<uint8_t *>(p->freeList) - gSide[side].pages[i]) + "/" + std::to_string(p->used);
    }
    return out;
}

// Pages on a ring, as indices (for picking operands).
std::vector<int> Members(int side, RZFixedPage *start)
{
    std::vector<int> out;
    if (!start) return out;
    RZFixedPage *p = start;
    do {
        out.push_back(IndexOf(side, p));
        p = p->next;
    } while (p != start && out.size() <= kPages);
    return out;
}

} // namespace

DIFF_TEST(RZFixedAllocator_Operations, 0x1be2bc)
{
    auto takeFreePage = ctx.Original<void *(__thiscall *)(void *)>(0x1be2bc);
    auto returnFullPage = ctx.Original<void *(__thiscall *)(void *, void *)>(0x1be31b);
    auto releaseEmptyPage = ctx.Original<void(__thiscall *)(void *, void *)>(0x1be02b);
    auto freeRing = ctx.Original<void(__thiscall *)(void *, void *)>(0x1be23b);
    for (auto &s : gSide)
        for (auto &p : s.pages) p = static_cast<uint8_t *>(_aligned_malloc(0x800, 0x800));
    std::mt19937 rng(0xF1D);
    for (int run = 0; run < 300; ++run) {
        RZFixedAllocatorBase pool[2];
        // Start: a ring of 1-3 pages as current, and maybe a ring of full pages.
        uint32_t nCur = 1 + rng() % 3, nFull = rng() % 4;
        for (int side = 0; side < 2; ++side) {
            Side &s = gSide[side];
            s.calls.clear();
            for (int i = 0; i < kPages; ++i) {
                std::memset(s.pages[i], 0, 0x800);
                auto *p = reinterpret_cast<RZFixedPage *>(s.pages[i]);
                p->freeList = s.pages[i] + 0x10 + 0x14 * (i % 7);
                p->used = static_cast<uint32_t>(i * 3);
            }
            auto link = [&](int first, uint32_t count, uint32_t usedFlag) -> RZFixedPage * {
                if (!count) return nullptr;
                for (uint32_t k = 0; k < count; ++k) {
                    auto *p = reinterpret_cast<RZFixedPage *>(s.pages[first + k]);
                    p->next = reinterpret_cast<RZFixedPage *>(s.pages[first + (k + 1) % count]);
                    p->prev = reinterpret_cast<RZFixedPage *>(s.pages[first + (k + count - 1) % count]);
                    p->used += usedFlag;
                }
                return reinterpret_cast<RZFixedPage *>(s.pages[first]);
            };
            pool[side].mVTable = gVTable;
            pool[side].mCurrent = link(0, nCur, 0);
            pool[side].mFull = link(4, nFull, 0x40000000);
            s.nextFresh = 8;
        }
        std::string diff;
        for (int step = 0; step < 40 && diff.empty(); ++step) {
            int op = static_cast<int>(rng() % 10);
            std::vector<int> full = Members(0, pool[0].mFull), cur = Members(0, pool[0].mCurrent);
            if (op < 4 || (op < 7 && full.empty())) op = 0;
            else if (op < 7) op = 1;
            else if (op < 9) op = 2;
            else op = 3;
            int pick = static_cast<int>(rng());
            uint32_t ra = 0, rb = 0;
            for (int side = 0; side < 2; ++side) {
                gActive = side;
                RZFixedAllocatorBase &p = pool[side];
                auto page = [&](const std::vector<int> &from) { return reinterpret_cast<RZFixedPage *>(gSide[side].pages[from[static_cast<size_t>(pick) % from.size()]]); };
                uint32_t r = 0;
                switch (op) {
                case 0:
                    r = static_cast<uint32_t>(IndexOf(side, side ? p.TakeFreePage() : takeFreePage(&p)));
                    break;
                case 1:
                    r = static_cast<uint32_t>(IndexOf(side, side ? p.ReturnFullPage(page(full)) : returnFullPage(&p, page(full))));
                    break;
                case 2:
                    if (side) p.ReleaseEmptyPage(page(cur));
                    else releaseEmptyPage(&p, page(cur));
                    break;
                default: {
                    bool which = pick & 1;
                    RZFixedPage *ring = which ? p.mFull : p.mCurrent;
                    if (side) reinterpret_cast<RZListNodeVisitor *>(&p)->VisitNodesThenHead(ring);
                    else freeRing(&p, ring);
                    break;
                }
                }
                (side ? rb : ra) = r;
            }
            ++ctx.cases;
            if (op == 3) break; // the freed pages are gone; the pool isn't used after a ring is freed
            std::string da = Describe(0, pool[0]), db = Describe(1, pool[1]);
            if (ra != rb) diff = "op " + std::to_string(op) + " result " + std::to_string(ra) + " vs " + std::to_string(rb);
            else if (da != db) diff = "op " + std::to_string(op) + ":\n      " + da + "\n      " + db;
            else if (gSide[0].calls != gSide[1].calls) diff = "op " + std::to_string(op) + ": slot calls differ";
        }
        if (diff.empty() && gSide[0].calls != gSide[1].calls) diff = "slot calls differ";
        if (!diff.empty()) ctx.Fail("run %d: %s", run, diff.c_str());
    }
    for (auto &s : gSide)
        for (auto &p : s.pages) _aligned_free(p);
}
