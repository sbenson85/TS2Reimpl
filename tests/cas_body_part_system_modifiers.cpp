// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane JZ: ApplyModifierTexts (slot 41, 0xcf81d0). The text
// parser 0xb5ebe0 is a scripted fake (name, kind, weight per text), the named-weight push 0xcf5930 a recording fake, and
// the face's slots 47 / 48 and the +0 face's slot 13 recording fakes. The image's operator new / sized delete (0x824632 /
// 0x824624) stand in for ours: the original frees the parsed names and its group copy with them.
#include "cas_body_part_system.h"
#include "rz/cRZString.h"

namespace bpt_modifiers {

using namespace bpt;
using nTSCASCore::StringVector;

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

struct Script {
    bool ok;
    int32_t kind;
    float weight;
    const char *name;
};
Script gScript[12];
uint8_t gTexts[12][0x20];

bool __cdecl F_b5ebe0(const void *text, cRZString *name, int32_t *kind, float *weight)
{
    const int index = static_cast<int>((static_cast<const uint8_t *>(text) - gTexts[0]) / 0x20);
    gEvents.push_back({0xb5ebe0, static_cast<uint32_t>(index), *kind == 0 ? 0u : 1u, FloatBits(*weight)});
    const Script &s = gScript[index];
    name->mString.assign(s.name);
    *kind = s.kind;
    *weight = s.weight;
    return s.ok;
}

void __fastcall F_cf5930(void *self, void *, const char *name, float weight, uint32_t flag)
{
    gEvents.push_back({0xcf5930, Rel(self), TextHash(name), FloatBits(weight), flag & 0xff});
}
void __fastcall Slot47(void *self, void *, const char *name)
{
    gEvents.push_back({47, Rel(self), TextHash(name)});
}
void __fastcall Slot48(void *self, void *, const char *name, float weight, uint32_t merge)
{
    gEvents.push_back({48, Rel(self), TextHash(name), FloatBits(weight), merge & 0xff});
}
void __fastcall Face0Slot13(void *self, void *) { gEvents.push_back({0x1013, Rel(self)}); }
std::array<const void *, 16> gFace0Vt;

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

const char *const kNames[] = {
    "",
    "nose",
    "nose",
    "body;brow;eyes;cranium;snout;",  // the "every region" group (29 characters)
    "body;brow;eyes;cranium;snouT;",  // same length, one byte off
    "body;brow;eyes;cranium;snout:",  // same length, the last byte off
    "body;brow;eyes;cranium;snout",   // one shorter
    "body;brow;eyes;cranium;snout;x", // one longer
    "a_rather_long_morph_name_on_the_heap",
    "brow",
};

} // namespace bpt_modifiers

DIFF_TEST(cTSCASBodyPartSystem_ApplyModifierTexts, 0xcf81d0)
{
    using namespace bpt_modifiers;
    std::mt19937 rng(0xcf81d0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const StringVector *)>(0xcf81d0);
    VTable vt = CopyVTable(ctx);
    vt.slots[47] = reinterpret_cast<const void *>(&Slot47);
    vt.slots[48] = reinterpret_cast<const void *>(&Slot48);
    gFace0Vt = {};
    gFace0Vt[13] = reinterpret_cast<const void *>(&Face0Slot13);
    Env env(ctx);
    Redirect r1(ctx, 0xb5ebe0, reinterpret_cast<const void *>(&F_b5ebe0));
    Redirect r2(ctx, 0xcf5930, reinterpret_cast<const void *>(&F_cf5930));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    gEvents.reserve(256);
    const float weights[] = {0.0f, 0.5f, 1.0f, 0.99999994f, 1.5f, -1.0f, std::numeric_limits<float>::quiet_NaN()};
    for (int i = 0; i < 4000; ++i) {
        Scenario sc;
        for (auto &b : sc.bytes) b = static_cast<uint8_t>(rng());
        const void *face0 = gFace0Vt.data();
        std::memcpy(sc.bytes, &face0, 4);
        const int n = i < 24 ? i % 12 : static_cast<int>(rng() % 13); // 0..12 texts
        for (int k = 0; k < n; ++k) {
            Script &s = gScript[k];
            s.ok = rng() % 6 != 0;
            s.kind = rng() % 2 ? 3 : static_cast<int32_t>(rng() % 6);
            s.weight = weights[rng() % 7];
            s.name = kNames[rng() % 10];
        }
        StringVector texts = {reinterpret_cast<cRZString *>(gTexts[0]), reinterpret_cast<cRZString *>(gTexts[n]),
                              reinterpret_cast<cRZString *>(gTexts[n])};
        Compare(ctx, "ApplyModifierTexts", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &texts);
            else w.S()->ApplyModifierTexts(&texts);
            return 0;
        });
    }
}
