// Differential test for the cIGZAny family's own vtables (lane VX, src/gz/cGZAnyTables.cpp): cIGZAny 0xd9c654,
// AnyBoolean 0xd9c670, AnyUint32 0xd9f534, AnySint32 0xda3d48, AnyFloat32 0xda3d70, AnyString 0xda3d98 and AnyAny
// 0xda3e00, what an image-free program stores in place of the image's tables (standalone/owned_statics.cpp aliases the
// owned statics to them).
//
// Slot for slot, as tests/gz_render_state_own_tables.cpp does: every image entry is hooked (the folded 0x14c5f0,
// 0x1bedcb and 0x1bf068 too, lane VX), and ours is exactly what that hook installs; cIGZAny's slots 1-5 are the image's
// _purecall, ours our CRT's. The owned statics resolve to the image's tables in the hooked build (checked), and the type
// names the owned texts stand for are the image's (the GetTypeName slots return them: checked against the image's).
#include "difftest.h"
#include "game/game.h"

#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "hook/hooks.h"

#include <cstdint>
#include <cstring>

extern "C" const void *ts2_own_vt_d9c654[6];
extern "C" const void *ts2_own_vt_d9c670[6];
extern "C" const void *ts2_own_vt_d9f534[6];
extern "C" const void *ts2_own_vt_da3d48[6];
extern "C" const void *ts2_own_vt_da3d70[6];
extern "C" const void *ts2_own_vt_da3d98[6];
extern "C" const void *ts2_own_vt_da3e00[6];
extern "C" int __cdecl _purecall();

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

struct Table {
    const char *name;
    uint32_t vtable;
    const void *const *ours;
    const void *owned;
    bool pure; // slots 1-5 _purecall (cIGZAny)
};

void CheckSlots(difftest::Context &ctx, const Table &t)
{
    const uint32_t base = P(GameAddress<void>(0));
    const uint32_t *image = GameAddress<uint32_t>(t.vtable);
    ++ctx.cases;
    if (t.owned != image) ctx.Fail("%s: the owned static is %p, not the image's table %p", t.name, t.owned, image);
    for (size_t i = 0; i < 6; ++i) {
        ++ctx.cases;
        if (i > 0 && t.pure) {
            if (t.ours[i] !=reinterpret_cast<const void *>(&_purecall)) ctx.Fail("%s slot %zu: not _purecall", t.name, i);
            continue;
        }
        const HookSpec *h = HookAt(image[i] - base);
        if (!h) ctx.Fail("%s slot %zu: the image's entry %x is not hooked", t.name, i, image[i] - base);
        else if (h->replacement != t.ours[i])
            ctx.Fail("%s slot %zu: ours is %p, the hook of %x installs %p", t.name, i, t.ours[i], image[i] - base,
                     h->replacement);
    }
}

} // namespace

DIFF_TEST(GZAny_OwnTables, 0xd9c654)
{
    const Table tables[] = {
        {"cIGZAny", 0xd9c654, ts2_own_vt_d9c654, gzany::AnyVTable(), true},
        {"AnyBoolean", 0xd9c670, ts2_own_vt_d9c670, gzany::BooleanVTable(), false},
        {"AnyUint32", 0xd9f534, ts2_own_vt_d9f534, gzany::Uint32VTable(), false},
        {"AnySint32", 0xda3d48, ts2_own_vt_da3d48, gzany::Sint32VTable(), false},
        {"AnyFloat32", 0xda3d70, ts2_own_vt_da3d70, gzany::Float32VTable(), false},
        {"AnyString", 0xda3d98, ts2_own_vt_da3d98, gzany::StringVTable(), false},
        {"AnyAny", 0xda3e00, ts2_own_vt_da3e00, gzany::AnyAnyVTable(), false},
    };
    for (const Table &t : tables) CheckSlots(ctx, t);

    // The owned type names: the image's texts (the hooked build resolves them to the image), each what the class's
    // GetTypeName slot returns.
    struct Name {
        const char *owned;
        uint32_t rva;
        const char *text;
    };
    const Name names[] = {
        {ts2_rva_da3d60, 0xda3d60, "AnySint32"},
        {ts2_rva_da3db0, 0xda3db0, "AnyString"},
        {ts2_rva_da3e18, 0xda3e18, "AnyAny"},
    };
    for (const Name &n : names) {
        ++ctx.cases;
        if (n.owned != GameAddress<const char>(n.rva)) ctx.Fail("0x%x: the owned text is not the image's", n.rva);
        ++ctx.cases;
        if (std::strcmp(GameAddress<const char>(n.rva), n.text) != 0) ctx.Fail("0x%x: the image's text is not %s", n.rva, n.text);
    }
    uint32_t obj[2] = {0, 0};
    ++ctx.cases;
    if (reinterpret_cast<AnySint32 *>(obj)->GetTypeName() != GameAddress<const char>(0xda3d60)) ctx.Fail("AnySint32 name");
    ++ctx.cases;
    if (reinterpret_cast<AnyString *>(obj)->GetTypeName() != GameAddress<const char>(0xda3db0)) ctx.Fail("AnyString name");
    ++ctx.cases;
    if (reinterpret_cast<AnyAny *>(obj)->GetTypeName() != GameAddress<const char>(0xda3e18)) ctx.Fail("AnyAny name");
}
