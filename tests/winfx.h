#pragma once
// winfx: a fake cIGZWin with state, and a fake window manager, for window procedures and window helpers
// (docs/ui-testing.md). Built on drawfx: every call on a fake window is logged like any drawfx call (so the comparison
// sees which properties the code set, in which order), and the slots below answer from the window's state, so code
// that reads back what it set (or walks the children) sees a consistent window.
//
//   winfx::World w;  w.Build(seed, 6);       // windows #300.., random ids, areas, flags, a random tree under w[0]
//   ... run the code under test with &w[0].f (a cIGZWin *) ...
//
// State-backed slots (cIGZWin, tests/fx_slots.h; meanings from cGZWin's bodies, db/methods/GZWinHelpers.jsonl):
//   0 QueryInterface (the faces added with f.AddFace), 1 / 2 AddRef / Release (count), 11 GetParent,
//   15 GetChildCount, 38 GetChildWindowFromID (direct children), 39 GetChildWindowFromIDRecursive (children first, then
//   itself, like cGZWin 0x931546), 45 GetChildInterfaceRecursive (39, then the child's QueryInterface),
//   49 / 50 GetWidth / GetHeight, 51-54 GetLeft / Top / Right / Bottom, 55 GetArea (out rect), 60 / 61 / 62 SetWidth /
//   SetHeight / SetSize, 64 SetArea, 78 / 79 GetID / SetID, 82 IsFlagSet, 83 SetFlag (low byte of the value, returns the
//   flags), 84 SetVisible (flag 1), 85 Hide.
// Every other slot logs and returns its programmed value (f.Returns), or runs a handler (f.On).
// Not covered: the caption (no caption slot is established on cIGZWin; cGZWinText keeps its text in a std::string at
// +0x64 and its setter was not found), messages (slot 3 just logs).
#include "drawfx.h"
#include "stack_out.h"

#include <random>

namespace winfx {

constexpr uint32_t kFlagVisible = 0x1, kFlagEnabled = 0x2;

struct Win {
    drawfx::Fake f; // the cIGZWin the code under test gets: pass &win.f
    uint32_t id = 0;
    int32_t area[4] = {};
    uint32_t flags = 0;
    Win *parent = nullptr;
    std::vector<Win *> children;

    void Reset(uint32_t fakeId, uint32_t windowId = 0);
    void Add(Win &child)
    {
        child.parent = this;
        children.push_back(&child);
    }
    Win *Child(uint32_t wanted)
    {
        for (Win *c : children)
            if (c->id == wanted) return c;
        return nullptr;
    }
    Win *ChildRecursive(uint32_t wanted)
    {
        for (Win *c : children)
            if (Win *w = c->ChildRecursive(wanted)) return w;
        return id == wanted ? this : nullptr;
    }
    // QueryInterface on the fake's faces, without logging (as the window's own lookups are not logged either).
    uint32_t Query(uint32_t iid, uint32_t out)
    {
        for (unsigned i = 0; i < f.faceCount; ++i)
            if (f.faces[i].iid == iid) {
                ++f.faces[i].fake->refs;
                if (out) *reinterpret_cast<void **>(uintptr_t(out)) = f.faces[i].fake;
                return 1;
            }
        return 0;
    }
    uint32_t SetFlag(uint32_t mask, uint32_t value)
    {
        flags = (value & 0xff) ? (flags | mask) : (flags & ~mask);
        return flags;
    }
};

namespace detail {
inline Win &Of(drawfx::Fake &f) { return *static_cast<Win *>(f.owner); }
inline uint32_t W(const Win *w) { return w ? drawfx::P(&w->f) : 0; }
inline uint32_t QueryInterface(drawfx::Fake &f, const uint32_t *a) { return Of(f).Query(a[0], a[1]); }
inline uint32_t GetParent(drawfx::Fake &f, const uint32_t *) { return W(Of(f).parent); }
inline uint32_t GetChildCount(drawfx::Fake &f, const uint32_t *) { return uint32_t(Of(f).children.size()); }
inline uint32_t GetChild(drawfx::Fake &f, const uint32_t *a) { return W(Of(f).Child(a[0])); }
inline uint32_t GetChildRecursive(drawfx::Fake &f, const uint32_t *a) { return W(Of(f).ChildRecursive(a[0])); }
inline uint32_t GetChildInterface(drawfx::Fake &f, const uint32_t *a)
{
    Win *w = Of(f).ChildRecursive(a[0]);
    return w ? w->Query(a[1], a[2]) : 0;
}
inline uint32_t GetWidth(drawfx::Fake &f, const uint32_t *) { return uint32_t(Of(f).area[2] - Of(f).area[0]); }
inline uint32_t GetHeight(drawfx::Fake &f, const uint32_t *) { return uint32_t(Of(f).area[3] - Of(f).area[1]); }
template <int I> uint32_t GetEdge(drawfx::Fake &f, const uint32_t *) { return uint32_t(Of(f).area[I]); }
inline uint32_t GetArea(drawfx::Fake &f, const uint32_t *a)
{
    auto *out = reinterpret_cast<int32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) out[i] = Of(f).area[i];
    difftest::FillOut(out, 16);
    return a[0];
}
inline uint32_t SetArea(drawfx::Fake &f, const uint32_t *a)
{
    for (int i = 0; i < 4; ++i) Of(f).area[i] = int32_t(a[i]);
    return 0;
}
inline uint32_t SetWidth(drawfx::Fake &f, const uint32_t *a)
{
    Of(f).area[2] = Of(f).area[0] + int32_t(a[0]);
    return 0;
}
inline uint32_t SetHeight(drawfx::Fake &f, const uint32_t *a)
{
    Of(f).area[3] = Of(f).area[1] + int32_t(a[0]);
    return 0;
}
inline uint32_t SetSize(drawfx::Fake &f, const uint32_t *a)
{
    SetWidth(f, a);
    return SetHeight(f, a + 1);
}
inline uint32_t GetID(drawfx::Fake &f, const uint32_t *) { return Of(f).id; }
inline uint32_t SetID(drawfx::Fake &f, const uint32_t *a)
{
    Of(f).id = a[0];
    return 0;
}
inline uint32_t IsFlagSet(drawfx::Fake &f, const uint32_t *a) { return (Of(f).flags & a[0]) != 0; }
inline uint32_t SetFlag(drawfx::Fake &f, const uint32_t *a) { return Of(f).SetFlag(a[0], a[1]); }
inline uint32_t SetVisible(drawfx::Fake &f, const uint32_t *a) { return Of(f).SetFlag(kFlagVisible, a[0]); }
inline uint32_t Hide(drawfx::Fake &f, const uint32_t *) { return Of(f).SetFlag(kFlagVisible, 0); }
} // namespace detail

inline void Win::Reset(uint32_t fakeId, uint32_t windowId)
{
    f.Reset<fx_slots::cIGZWin>(fakeId);
    f.owner = this;
    id = windowId;
    for (int32_t &a : area) a = 0;
    flags = 0;
    parent = nullptr;
    children.clear();
    f.On(0, &detail::QueryInterface);
    f.On(11, &detail::GetParent);
    f.On(15, &detail::GetChildCount);
    f.On(38, &detail::GetChild);
    f.On(39, &detail::GetChildRecursive);
    f.On(45, &detail::GetChildInterface);
    f.On(49, &detail::GetWidth);
    f.On(50, &detail::GetHeight);
    f.On(51, &detail::GetEdge<0>);
    f.On(52, &detail::GetEdge<1>);
    f.On(53, &detail::GetEdge<2>);
    f.On(54, &detail::GetEdge<3>);
    f.On(55, &detail::GetArea);
    f.On(60, &detail::SetWidth);
    f.On(61, &detail::SetHeight);
    f.On(62, &detail::SetSize);
    f.On(64, &detail::SetArea);
    f.On(78, &detail::GetID);
    f.On(79, &detail::SetID);
    f.On(82, &detail::IsFlagSet);
    f.On(83, &detail::SetFlag);
    f.On(84, &detail::SetVisible);
    f.On(85, &detail::Hide);
}

// A fake window manager (cIGZWinMgr, 48 slots): logs every call; program answers with f.Returns / f.On.
struct Mgr {
    drawfx::Fake f;
    void Reset(uint32_t fakeId) { f.Reset<fx_slots::cIGZWinMgr>(fakeId); }
};

// Up to 16 windows (fake ids 300 + index) in a random tree under [0], with random ids (drawn from a small set, so
// lookups hit, miss and meet duplicates), areas and flags. Deterministic in the seed: build it again for each side.
struct World {
    static constexpr unsigned kMax = 16;
    Win wins[kMax];
    unsigned count = 0;

    Win &operator[](unsigned i) { return wins[i]; }
    void Build(uint32_t seed, unsigned n, uint32_t idBase = 0x1000, unsigned idChoices = 12)
    {
        std::mt19937 rng(seed);
        count = n < kMax ? n : kMax;
        for (unsigned i = 0; i < count; ++i) {
            wins[i].Reset(300 + i, idBase + rng() % idChoices);
            const int32_t l = int32_t(rng() % 400) - 50, t = int32_t(rng() % 300) - 50;
            wins[i].area[0] = l;
            wins[i].area[1] = t;
            wins[i].area[2] = l + int32_t(rng() % 200);
            wins[i].area[3] = t + int32_t(rng() % 200);
            wins[i].flags = rng() & 0x3f;
            if (i) wins[rng() % i].Add(wins[i]);
        }
    }
    // The windows' state as words (ids, areas, flags, child order), for comparing what the code under test left behind.
    std::vector<uint32_t> State() const
    {
        std::vector<uint32_t> out;
        for (unsigned i = 0; i < count; ++i) {
            const Win &w = wins[i];
            out.push_back(w.id);
            for (int32_t a : w.area) out.push_back(uint32_t(a));
            out.push_back(w.flags);
            out.push_back(uint32_t(w.f.refs));
            for (const Win *c : w.children) out.push_back(c->f.id);
            out.push_back(0xffffffffu);
        }
        return out;
    }
};

} // namespace winfx
