// Differential test of the legacy script writer's common attributes 0x86f2b8 (lane WA, foundations):
// src/gz/cGZLegacyScriptWriterCommon.size.cpp.
//
// A winfx window (id, area and flags from the seed; the id sometimes 0) whose slot 117 always writes its three bytes,
// slot 89 gives a caption (empty or not), slot 126 answers the caption resource key property 0x0b31771a (a value of
// 16 bytes with zero words sometimes, another size, or absent) and the tooltip property 0xbaca6879 (a counted source whose
// flags have the texts bit or not, whose slot 62 answers true with a set of eight texts some empty, or false), and slot 128
// (the property walk, whose callback is the original 0x86e7aa on both sides) is logged. Compared: the script text, the
// counts of the window, the source and the set, the key value's counts and the log.
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

#include <new>
#include <random>
#include <string>
#include <vector>

namespace legacy_script_common_test {

using svcfx::Blob;

constexpr uint32_t kCaptionResourceProperty = 0x0b31771a;
constexpr uint32_t kTooltipProperty = 0xbaca6879;

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
drawfx::Fake gSource;
legacy_win_faces::TooltipSetFake gSet;
LegacyText gCaption;
cRZVariant *gKey;    // the caption resource (null: absent)
bool gSourceAnswers; // the tooltip property is there
bool gSetAnswers;    // slot 62 answers true

uint32_t GetProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::gLog.back().words[1] = 1; // the out pointer is a stack address
    if (a[0] == kCaptionResourceProperty && gKey) {
        gKey->AddRef();
        *reinterpret_cast<void **>(uintptr_t(a[1])) = gKey;
        return 1;
    }
    if (a[0] == kTooltipProperty && gSourceAnswers) {
        ++gSource.refs;
        *reinterpret_cast<void **>(uintptr_t(a[1])) = &gSource;
        return 1;
    }
    return 0;
}
uint32_t GetFillColor(drawfx::Fake &, const uint32_t *a) // always writes the three bytes
{
    for (int i = 0; i < 3; ++i) *reinterpret_cast<uint8_t *>(uintptr_t(a[i])) = uint8_t(gRng());
    return 0;
}
uint32_t GetCaption(drawfx::Fake &, const uint32_t *) { return drawfx::P(&gCaption.mText); }
uint32_t GetTexts(drawfx::Fake &, const uint32_t *a)
{
    if (!gSetAnswers) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gSet;
    return 1;
}

cRZVariant *KeyValue()
{
    std::vector<uint32_t> words(Draw(5) ? 4 : Draw(9));
    for (uint32_t &w : words) w = Draw(3) ? uint32_t(gRng()) : 0;
    auto *v = new cRZVariant;
    v->SetVoidArray(words.data(), uint32_t(words.size() * 4));
    return v;
}

ifaces::cIGZWin *Build(uint32_t seed, LegacyText &out)
{
    drawfx::ResetAll();
    gRng.seed(seed);
    gWorld.Build(seed, 2);
    winfx::Win &w = gWorld[1];
    if (!Draw(3)) w.id = 0;
    w.flags = Draw(4) ? uint32_t(gRng()) : (Draw(2) ? 0 : ~0u);
    w.f.Settle<117, 3>("aaa");
    w.f.Settle<128, 2>("ua");
    w.f.On(117, &GetFillColor);
    w.f.On(89, &GetCaption);
    w.f.On(126, &GetProperty);
    gCaption.mText.FromChar(Draw(3) ? "a caption" : "");
    gKey = Draw(5) ? KeyValue() : nullptr;
    if (gKey) gKey->AddRef();
    gSource.Reset<legacy_win_faces::kTooltipSourceFace>(800);
    gSource.Returns(3, Draw(4) ? 0x10 | Draw(0x40) : Draw(0x40) & ~0x10u);
    gSource.On(62, &GetTexts);
    gSourceAnswers = Draw(4) != 0;
    gSetAnswers = Draw(5) != 0;
    gSet.releases = 0;
    static const char *const kTexts[] = {"", "tip", "a longer tooltip text", "x y"};
    for (LegacyText &t : gSet.mTexts) t.mText.FromChar(kTexts[Draw(4)]);
    out.mText.FromChar(Draw(2) ? "" : "pre ");
    return reinterpret_cast<ifaces::cIGZWin *>(&w.f);
}

void Finish(Blob &b, const LegacyText &out)
{
    PushText(b, out.mText.mString);
    b.push_back(uint32_t(gWorld[1].f.refs));
    b.push_back(uint32_t(gSource.refs));
    b.push_back(uint32_t(gSet.releases));
    if (gKey) {
        b.push_back(gKey->AddRef()); // each read released
        gKey->Release();
        gKey->Release();
        gKey = nullptr;
    }
}

using CommonFn = void(__cdecl *)(ifaces::cIGZWin *, cRZString *);

DIFF_TEST(LegacyScriptWriter_Common, 0x86f2b8)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        drawfx::Log logs[2];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            LegacyText out;
            ifaces::cIGZWin *window = Build(seed, out);
            if (side) WriteWindowAttributes(window, &out.mText);
            else ctx.Original<CommonFn>(0x86f2b8)(window, &out.mText);
            Finish(results[side], out);
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyScriptWriter_Common", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyScriptWriter_Common log", seed)) break;
    }
}

} // namespace legacy_script_common_test
