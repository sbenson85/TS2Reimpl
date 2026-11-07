#pragma once
// Shared pieces of the lane CQ cTSNeighborhood difftests (tests/ts_neighborhood_lots_d*.cpp): a constructed neighbourhood with the
// game's vtables replaced by recording ones for the slots the code under test calls through `this`, the Pair() runner (original on
// one copy, ours on the other, everything compared) and small recording handlers. Same idea as tests/ts_neighborhood_lots_b.cpp.
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "rz/cRZString.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <random>
#include <string>

namespace lotsd {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "ddddd"}, {2, "dddd"}, {3, "ddd"}, {4, "d"}, {5, "dddddd-d"}, {6, "d"}, {7, "d"}, {8, "d"}, {9, "d"}, {10, "dd"}, {11, "d"}, {12, "dd"}, {13, "d"}, {14, "d"}, {15, "d"}, {16, "d"}, {17, "d"}, {18, "d"}, {19, "d"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

struct NbMem {
    alignas(8) uint8_t bytes[sizeof(Nb)];
    Nb *Get() { return reinterpret_cast<Nb *>(bytes); }
};

constexpr unsigned kNbFake = 23;   // the fake the neighbourhood's own recording slots write to
inline void *gNbVT[80];
inline void *gFaceVT[12];
inline uint32_t gGroup = 0;
inline bool gReady = false;

inline uint32_t Hash(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}
inline cRZString MakeString(const std::string &text)
{
    cRZString s{GameAddress<void>(cRZString::kVTable), std::string(text), 0};
    return s;
}

// Slots of the neighbourhood called through `this`: they log like a fake's slot and answer from fake kNbFake.
template <unsigned Slot, unsigned Args> struct NbSlot;
template <unsigned Slot> struct NbSlot<Slot, 0> {
    static uint32_t __fastcall Fn(void *, void *) { uint32_t a[1] = {0}; return detail::Dispatch(&F(kNbFake), Slot, a, 0); }
};
template <unsigned Slot> struct NbSlot<Slot, 1> {
    static uint32_t __fastcall Fn(void *, void *, uint32_t x) { uint32_t a[1] = {x}; return detail::Dispatch(&F(kNbFake), Slot, a, 1); }
};
template <unsigned Slot> struct NbSlot<Slot, 3> {
    static uint32_t __fastcall Fn(void *, void *, uint32_t x, uint32_t y, uint32_t z)
    {
        uint32_t a[3] = {x, y, z};
        return detail::Dispatch(&F(kNbFake), Slot, a, 3);
    }
};
inline uint32_t __fastcall FaceGroup(void *, void *)
{
    Ev(evNote);
    Ev(6);
    return gGroup;
}
inline void Ready()
{
    if (gReady) return;
    gNbVT[21] = reinterpret_cast<void *>(&NbSlot<21, 1>::Fn);
    gNbVT[37] = reinterpret_cast<void *>(&NbSlot<37, 1>::Fn);
    gNbVT[38] = reinterpret_cast<void *>(&NbSlot<38, 1>::Fn);
    gNbVT[57] = reinterpret_cast<void *>(&NbSlot<57, 1>::Fn);
    gNbVT[68] = reinterpret_cast<void *>(&NbSlot<68, 0>::Fn);
    gNbVT[70] = reinterpret_cast<void *>(&NbSlot<70, 3>::Fn);
    gFaceVT[6] = reinterpret_cast<void *>(&FaceGroup);
    gReady = true;
}

inline void CaptureNb(Blob &b, Nb *nb)
{
    b.push_back(nb->mField6C);
    b.push_back(nb->mByteC8);
    b.push_back(Norm(P(nb->mDelegate)));
    b.push_back(Norm(P(nb->mSecondOwner)));
    b.push_back(uint32_t(nb->mLots.size()));
    for (auto &entry : nb->mLots) {
        b.push_back(uint32_t(entry.first));
        b.push_back(Norm(P(entry.second.Get())));
    }
    b.push_back(uint32_t(nb->mTexts.size()));
    for (auto &entry : nb->mTexts) {
        b.push_back(Hash(entry.mText.Data()));
        b.push_back(Norm(P(entry.mHeld)));
    }
    for (auto *map : {&nb->mMembers1, &nb->mMembers2, &nb->mMembers3}) {
        b.push_back(uint32_t(map->size()));
        for (auto &entry : *map) {
            b.push_back(uint32_t(int32_t(entry.first)));
            b.push_back(Norm(P(entry.second.Get())));
        }
    }
    b.push_back(uint32_t(nb->mPlain.size()));
}

// Runs `world(globals, nb, rng, isOriginal)` on a fresh neighbourhood once per side and compares the result, the event log, the
// neighbourhood and the fakes.
template <class Fn> bool Pair(difftest::Context &ctx, const char *what, uint32_t seed, Fn world)
{
    bool same = true;
    for (int route = 0; route < 2; ++route) {
        nbshape::gRoute = nbshape::Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            Ready();
            std::mt19937 rng(seed * 7919u + 17);
            Globals g;
            NbMem mem;
            std::memset(mem.bytes, 0, sizeof(mem.bytes));
            Nb *nb = mem.Get();
            nbshape::Build(ctx, side == 0, mem.bytes);
            nbshape::CopyOwn(gNbVT, 0); // the fakes are copies of the side's own vtables, patched
            nbshape::CopyOwn(gFaceVT, 1);
            gReady = false;
            Ready();
            nbshape::VTableOf(nb, 0) = gNbVT;
            nbshape::VTableOf(nb, 1) = gFaceVT;
            AddRange(nb, sizeof(Nb), 0xe0000000);
            gGroup = 0x1000 + (rng() & 0xfff);
            Blob &b = blobs[side];
            const uint32_t r = world(g, nb, rng, side == 0);
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureNb(b, nb);
            CaptureFakes(b, kValueKinds);
            nbshape::VTableOf(nb, 0) = nbshape::gPristine[0];
            nbshape::VTableOf(nb, 1) = nbshape::gPristine[1];
            nb->mDelegate = nullptr;
            nb->mSecondOwner = nullptr;
            nb->mOwner = nullptr;
            nbshape::Destroy(ctx, side == 0, nb);
        }
        same = Same(ctx, blobs[0], blobs[1], what, seed | (route ? 0x80000000u : 0)) && same;
    }
    nbshape::gRoute = nbshape::kVTableRoute;
    return same;
}

inline void LogKey(const void *key)
{
    const auto *w = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(w[i]);
}
inline uint32_t KeyArg0(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return 0;
}
inline uint32_t TextArg0(Obj *, uint32_t *a)
{
    Ev(Hash(reinterpret_cast<const cRZString *>(a[0])->Data()));
    return 0;
}

// ---- messages: the message server fake logs a message by content (the extra string by its text) ----
inline void LogMessageText(uint32_t pointer)
{
    Ev(evMessage);
    if (!pointer) {
        Ev(0);
        return;
    }
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(pointer));
    Ev(words[1]);
    Ev(words[2] & 0xff);
    Ev(words[3]);
    svcfx::LogMessageValues(words);
    Ev(words[8] != 0);
    Ev(words[9] != 0);
    Ev(words[10] ? Hash(reinterpret_cast<const cRZString *>(uintptr_t(words[10]))->Data()) : 0);
}
inline uint32_t __fastcall SendShim(Obj *self, void *, uint32_t message)
{
    Ev(evCall); Ev(self->id); Ev(3); Ev(1);
    LogMessageText(message);
    return self->ret[3];
}
inline uint32_t __fastcall PostShim(Obj *self, void *, uint32_t message, uint32_t flag)
{
    Ev(evCall); Ev(self->id); Ev(4); Ev(2);
    LogMessageText(message);
    Ev(flag);
    return self->ret[4];
}
// Makes `server` take sent (slot 3) and posted (slot 4) messages and log them by content.
inline void UseAsMessageServer2(Obj &server, uint32_t result = 0)
{
    server.Set(3, 1, result);
    server.table[3] = reinterpret_cast<void *>(&SendShim);
    server.Set(4, 2, result);
    server.table[4] = reinterpret_cast<void *>(&PostShim);
}

} // namespace lotsd
