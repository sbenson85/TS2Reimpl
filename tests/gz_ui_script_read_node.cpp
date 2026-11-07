// Differential test for cGZUIScriptService::ReadScriptNode (lane MC, src/gz/cGZUIScriptService.size.cpp), 0x85bc39:
// the service's script interpreter made on first use and given the UI script's vocabulary.
//
// UIScript_ReadNode: a service built by the original constructor (0x85b39a) on pre-filled memory against one built by
// ours, its script node (+0x34) null or already a fake, then the original 0x85bc39 or our member called once and the
// service destroyed (the destructor 0x85b434). The framework's GetClassObject (class 0xba2e7953, iid 0x5a2c1570) hands
// out a recording node fake or refuses; the node's AddId (slot 9) logs the name by content (and whether it is a cRZString
// with the game's vtable), the id and the service's shared-interpreter flag (+0x3c) at the time; its SetTarget (slot 3)
// logs the target (a face of the service) and the flag. Compared: the result, the node field, the flag, the event log,
// every fake's count. Stood in on both sides: the game's operator new / delete (ours).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZUIScriptService.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace ui_read_node_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cGZUIScriptService;

constexpr uint32_t kTag = 0x7d000000;
enum : unsigned { kFramework = 0, kNode, kOldNode };

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint8_t *gService;

uint32_t ClassObject(Obj *, uint32_t *a)
{
    Ev(0xc1a55);
    Ev(a[0]);
    Ev(a[1]);
    if (a[0] != 0xba2e7953 || a[1] != 0x5a2c1570 || Draw(6) == 0) return 0x100;
    ++F(kNode).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kNode);
    return 0x101;
}
uint32_t NodeAddId(Obj *, uint32_t *a)
{
    const auto *name = reinterpret_cast<const cIGZString *>(uintptr_t(a[0]));
    Ev(0xadd1d);
    Ev(*reinterpret_cast<const uint32_t *>(name) == P(GameAddress<void>(cRZString::kVTable)));
    const uint32_t n = name->Strlen();
    Ev(n);
    const char *text = name->Data();
    for (uint32_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
    Ev(a[1]);
    Ev(gService[0x3c]);
    return 0x100 | Draw(2);
}
uint32_t NodeSetTarget(Obj *, uint32_t *a)
{
    Ev(0x5e7);
    Ev(svcfx::Norm(a[0]));
    Ev(gService[0x3c]);
    return 0x101;
}

} // namespace ui_read_node_test

using namespace ui_read_node_test;

DIFF_TEST(UIScript_ReadNode, 0x85bc39)
{
    svcfx::Globals globals;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 600; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            gRng.seed(seed);
            F(kFramework).Set(3, "ddp", 0, &ClassObject);
            F(kNode).Set(9, "pd", 0, &NodeAddId);
            F(kNode).Set(3, "p", 0, &NodeSetTarget);
            const bool present = Draw(5) == 0;
            void *memory = ::operator new(Sys::kSize);
            std::memset(memory, 0xc5, Sys::kSize);
            gService = static_cast<uint8_t *>(memory);
            if (side)
                ::new (memory) Sys;
            else
                ctx.Original<void *(__thiscall *)(void *)>(0x85b39a)(memory);
            svcfx::AddRange(memory, Sys::kSize, kTag);
            if (present) {
                ++F(kOldNode).refs;
                *reinterpret_cast<void **>(gService + 0x34) = &F(kOldNode);
            }
            svcfx::gEv.clear();
            Blob &b = results[side];
            const bool r = side ? static_cast<Sys *>(memory)->ReadScriptNode()
                                : ctx.Original<bool(__thiscall *)(void *)>(0x85bc39)(memory);
            b.push_back(r);
            b.push_back(svcfx::Norm(*reinterpret_cast<uint32_t *>(gService + 0x34)));
            b.push_back(gService[0x3c]);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            if (side)
                static_cast<Sys *>(memory)->~Sys();
            else
                ctx.Original<void(__thiscall *)(void *)>(0x85b434)(memory);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
            ::operator delete(memory);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "UIScript_ReadNode", seed)) break;
    }
}
