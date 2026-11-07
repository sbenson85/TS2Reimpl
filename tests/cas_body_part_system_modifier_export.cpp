// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KA: slot 42 (ExportModifierTexts, 0xcf2180), the
// modifier stack written out as "%s:%d:%f" texts. The image's formatter 0xb5f0b0 and push_back 0x3a05e0 run on both
// sides; cRZString::Sprintf (0x1ad6f7) and the image's new / delete are stood in for (CRT state the harness has not
// initialised). The out vector starts with 0-3 strings and is compared text by text with its size and capacity.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/cRZString.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>

namespace bpt_modexport {

using namespace bpt;
using StrVec = nTSCASCore::StringVector;

cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    self->mString = text;
    return self;
}
void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

// The strings `out` starts with get a copy of the image's cRZString vtable whose slot 33 (the destructor the clear
// calls) records the string before running the image's.
std::array<const void *, 64> gStringVt;
using DestroyFn = void *(__thiscall *)(void *, uint32_t);
DestroyFn gImageDestroy;
void *__fastcall RecordingDestroy(cRZString *self, void *, uint32_t flags)
{
    gEvents.push_back({0x5021, static_cast<uint32_t>(self->mString.size()), flags});
    return gImageDestroy(self, flags);
}

StrVec *BuildOut(difftest::Context &ctx, const std::vector<std::string> &texts)
{
    auto push = ctx.Original<void(__thiscall *)(StrVec *, const cRZString *)>(0x3a05e0);
    auto *v = new StrVec{nullptr, nullptr, nullptr};
    for (const auto &t : texts) {
        cRZString s;
        s.Construct(t.c_str());
        push(v, &s);
        s.Destruct();
    }
    for (cRZString *s = v->mFirst; s != v->mLast; ++s) *reinterpret_cast<const void **>(s) = gStringVt.data();
    return v;
}

uint64_t HashOut(const StrVec *v)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    mix(static_cast<uint64_t>(v->mLast - v->mFirst));
    mix(static_cast<uint64_t>(v->mEnd - v->mFirst));
    for (const cRZString *s = v->mFirst; s != v->mLast; ++s) {
        mix(s->mString.size());
        for (char c : s->mString) mix(static_cast<uint8_t>(c));
        mix(s->mRefCount);
        mix(reinterpret_cast<uintptr_t>(*reinterpret_cast<void *const *>(s)));
    }
    return h;
}

// Group names that repeat (and differ only in case or length), the empty name, heap-sized names.
const char *const kNames[] = {"brow", "brow", "Brow", "bro", "", "a_rather_long_group_name_on_the_heap", "lips", "lips"};
const int kKinds[] = {0, 0, 0, 1, 1, 1, 2, 2, 7, 7, 3, 4, 5, 6, 8, -1};

} // namespace bpt_modexport

// Records of kinds 0/1/2/7 and others, weights 0 / -0 / 1 / just below 1 / NaN / others, repeated and empty group names
// (a full-weight morph with no group yet drops the list; a group equal to the last one is skipped).
DIFF_TEST(cTSCASBodyPartSystem_ExportModifierTexts, 0xcf2180)
{
    using namespace bpt_modexport;
    std::mt19937 rng(0xcf2180);
    auto orig = ctx.Original<void(__thiscall *)(void *, StrVec *)>(0xcf2180);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    std::memcpy(gStringVt.data(), ctx.image.At<const void *>(cRZString::kVTable), sizeof(gStringVt));
    gImageDestroy = reinterpret_cast<DestroyFn>(const_cast<void *>(gStringVt[33]));
    gStringVt[33] = reinterpret_cast<const void *>(&RecordingDestroy);
    const float kWeights[] = {0.0f, -0.0f, 1.0f, std::nextafter(1.0f, 0.0f), 0.5f, 2.0f, -1.0f,
                              std::numeric_limits<float>::quiet_NaN(), 0.125f};
    for (int i = 0; i < 4000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack.resize(rng() % 11);
        for (auto &s : sc.stack) {
            s.kind = kKinds[rng() % (sizeof(kKinds) / sizeof(kKinds[0]))];
            s.weight = kWeights[rng() % (sizeof(kWeights) / sizeof(kWeights[0]))];
            s.name = kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))];
        }
        std::vector<std::string> before(rng() % 4);
        for (auto &t : before) t = kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))];
        Compare(ctx, "ExportModifierTexts", sc, vt, [&](World &w, bool original) -> uint64_t {
            StrVec *out = BuildOut(ctx, before);
            if (original) orig(w.S(), out);
            else w.S()->ExportModifierTexts(out);
            return HashOut(out);
        });
    }
}
