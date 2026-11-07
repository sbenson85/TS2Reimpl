// Differential tests of the legacy script writer's dispatcher 0x87a9a9 and class header 0x87ae44 (lane FI, foundations):
// src/gz/cGZLegacyScriptWriter.size.cpp.
//
// Both on a winfx window whose GetProperty (slot 126) answers the class id property 0xe85eebde and the interface id
// property 0x08d6e462 with uint32 values (absent, 0, a known class's id, another number), and whose QueryInterface
// answers up to 4 faces: interface ids of the game's class table (several at once, so the first-match rule shows), an
// unknown id; each face is a recording fake. The script text starts empty or prefilled.
//
// LegacyScriptWriter_Header (0x87ae44): compared, the returned interface id, the script text, the window's and the faces'
// counts afterwards (the header releases the window, not the face it was handed) and the log.
// LegacyScriptWriter_Dispatch (0x87a9a9): the header runs for real on both sides; the common attributes 0x86f2b8 (FJ's)
// and the 20 class writers are stood in by recorders (which face they got, and a marker in the text). Compared, the
// result (al), the script text, the counts and the log.
#include "code_patch.h"
#include "difftest.h"
#include "legacy_win_faces.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyScriptWriter.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <random>
#include <string>

namespace legacy_script_dispatch_test {

using svcfx::Blob;

constexpr uint32_t kClassIdProperty = 0xe85eebde;
constexpr uint32_t kInterfaceIdProperty = 0x08d6e462;

// Facts of the game's class table (0x105b880), for drawing ids; the code under test reads its own.
const uint32_t kClsids[] = {0xc2afa76e, 0x00000592, 0x231a1c58, 0x00000598, 0x21335c5a, 0x0000059b, 0x22ecfc47, 0x82fe68c4,
                            0x61325a2e, 0x21325208, 0xa1336cc1, 0x612ce0c4, 0x4303e5b6, 0xe32f0b31, 0x4386d516, 0x1aa52ea4,
                            0x3ae8bae1, 0x078e8055, 0xdaa6b9bf, 0x4b42da5d, 0x478d1e6f, 0xe2ba00ee};
const uint32_t kIids[] = {0xc2afa76f, 0x212cdc1f, 0x231a1c57, 0x4132242b, 0x21335c59, 0x412ce496, 0x00008810, 0xc12cea13,
                          0x61325a2d, 0x21325207, 0xa1336cc0, 0x612ce0c3, 0xe303e5ae, 0x032f0b3c, 0x5386d516, 0x3aa52e64,
                          0x3ae8bad2, 0xa13351b8, 0xdaa6b9be, 0x4b42da53, 0x678d1e84, 0x22ba0121};
constexpr unsigned kRows = 22;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void PushText(Blob &b, const std::string &s)
{
    b.push_back(uint32_t(s.size()));
    for (char c : s) b.push_back(uint8_t(c));
}

winfx::World gWorld;
drawfx::Fake gFaces[4];
cRZVariant *gValues[2]; // the class id and interface id properties (null: absent)

uint32_t FaceIndex(const void *p)
{
    for (unsigned i = 0; i < 4; ++i)
        if (p == &gFaces[i]) return i + 1;
    return p == &gWorld[1].f ? 0x100 : (p ? 0xdead : 0);
}

uint32_t GetIdProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = 1; // the out pointer is a stack address
    cRZVariant *v = a[0] == kClassIdProperty ? gValues[0] : a[0] == kInterfaceIdProperty ? gValues[1] : nullptr;
    if (!v) return 0;
    v->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = v;
    return 1;
}

uint32_t DrawId(const uint32_t *table)
{
    switch (Draw(4)) {
    case 0: return 0;
    case 1: return uint32_t(gRng());
    default: return table[Draw(kRows)];
    }
}

// One side's window: its properties, its faces, a script text. Returns the window.
ifaces::cIGZWin *Build(uint32_t seed, LegacyText &out)
{
    drawfx::ResetAll();
    gRng.seed(seed);
    gWorld.Build(seed, 2);
    winfx::Win &w = gWorld[1];
    w.f.On(126, &GetIdProperty);
    for (unsigned k = 0; k < 2; ++k) gValues[k] = Draw(3) ? new cRZVariant(DrawId(k ? kIids : kClsids)) : nullptr;
    for (cRZVariant *v : gValues)
        if (v) v->AddRef();
    const unsigned faces = Draw(5);
    for (unsigned i = 0; i < faces; ++i) {
        gFaces[i].Reset<legacy_win_faces::kFontEntryFace>(700 + i);
        w.f.AddFace(Draw(6) ? kIids[Draw(kRows)] : uint32_t(gRng()), gFaces[i]);
    }
    for (unsigned i = faces; i < 4; ++i) gFaces[i].Reset<legacy_win_faces::kFontEntryFace>(700 + i);
    out.mText.FromChar(Draw(2) ? "" : "pre ");
    return reinterpret_cast<ifaces::cIGZWin *>(&w.f);
}

void Finish(Blob &b, const LegacyText &out)
{
    PushText(b, out.mText.mString);
    b.push_back(uint32_t(gWorld[1].f.refs));
    for (const drawfx::Fake &f : gFaces) b.push_back(uint32_t(f.refs));
    for (cRZVariant *&v : gValues) {
        if (!v) continue;
        b.push_back(v->AddRef()); // each property read released
        v->Release();
        v->Release();
        v = nullptr;
    }
}

using HeaderFn = uint32_t(__cdecl *)(ifaces::cIGZWin *, cRZString *);
using DispatchFn = bool(__cdecl *)(ifaces::cIGZWin *, cRZString *);

DIFF_TEST(LegacyScriptWriter_Header, 0x87ae44)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        drawfx::Log logs[2];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            LegacyText out;
            ifaces::cIGZWin *window = Build(seed, out);
            const uint32_t iid = side ? WriteLegacyClassHeader(window, &out.mText)
                                      : ctx.Original<HeaderFn>(0x87ae44)(window, &out.mText);
            results[side].push_back(iid);
            Finish(results[side], out);
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyScriptWriter_Header", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyScriptWriter_Header log", seed)) break;
    }
}

// ---- the dispatcher's stand-ins --------------------------------------------------------------------------------------

void __cdecl CommonStandIn(ifaces::cIGZWin *window, cRZString *out)
{
    drawfx::Note("common", {FaceIndex(window)});
    out->mString.append("common ");
}
template <unsigned N> void __cdecl WriterStandIn(void *face, cRZString *out)
{
    drawfx::Note("writer", {N, FaceIndex(face), uint32_t(static_cast<drawfx::Fake *>(face)->refs)});
    out->mString.append("writer" + std::to_string(N) + " ");
}

DIFF_TEST(LegacyScriptWriter_Dispatch, 0x87a9a9)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn common(0x86f2b8, &WriteWindowAttributes, &CommonStandIn);
    shape::StandIn w0(0x87551d, &WriteLineInputAttributes, &WriterStandIn<0>);
    shape::StandIn w1(0x8777a6, &WriteSliderAttributes, &WriterStandIn<1>);
    shape::StandIn w2(0x87822e, &WriteTextAttributes, &WriterStandIn<2>);
    shape::StandIn w3(0x8791f3, &WriteTextTickerAttributes, &WriterStandIn<3>);
    shape::StandIn w4(0x86ff03, &WriteBtnAttributes, &WriterStandIn<4>);
    shape::StandIn w5(0x878762, &WriteTextEditAttributes, &WriterStandIn<5>);
    shape::StandIn w6(0x875ccb, &WriteListBoxAttributes, &WriterStandIn<6>);
    shape::StandIn w7(0x870b57, &WriteComboAttributes, &WriterStandIn<7>);
    shape::StandIn w8(0x8792f2, &WriteTreeViewAttributes, &WriterStandIn<8>);
    shape::StandIn w9(0x87117a, &WriteFileBrowserAttributes, &WriterStandIn<9>);
    shape::StandIn w10(0x877189, &WriteScrollbar2Attributes, &WriterStandIn<10>);
    shape::StandIn w11(0x872f45, &WriteFoldersAttributes, &WriterStandIn<11>);
    shape::StandIn w12(0x87751e, &WriteScrollbarAttributes, &WriterStandIn<12>);
    shape::StandIn w13(0x877979, &WriteSpinnerAttributes, &WriterStandIn<13>);
    shape::StandIn w14(0x8730b3, &WriteGenAttributes, &WriterStandIn<14>);
    shape::StandIn w15(0x8764bf, &WriteOptGrpAttributes, &WriterStandIn<15>);
    shape::StandIn w16(0x876ea4, &WriteOutlineAttributes, &WriterStandIn<16>);
    shape::StandIn w17(0x873913, &WriteGridAttributes, &WriterStandIn<17>);
    shape::StandIn w18(0x872b33, &WriteFlatRectAttributes, &WriterStandIn<18>);
    shape::StandIn w19(0x86fc7d, &WriteBitmapAttributes, &WriterStandIn<19>);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        drawfx::Log logs[2];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            LegacyText out;
            ifaces::cIGZWin *window = Build(seed, out);
            const bool result = side ? WriteLegacyScript(window, &out.mText)
                                     : ctx.Original<DispatchFn>(0x87a9a9)(window, &out.mText);
            results[side].push_back(result);
            Finish(results[side], out);
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyScriptWriter_Dispatch", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyScriptWriter_Dispatch log", seed)) break;
    }
}

} // namespace legacy_script_dispatch_test
