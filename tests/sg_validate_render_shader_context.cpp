// Differential tests of cValidateRenderShaderContext (lane TM, scenegraph M3a; src/sg/cValidateRenderShaderContext.h),
// in the world of tests/sg_shader_attr_fx.h, which this file extends with its own script (the shared one would call
// Init with random pointers):
//   - the vertex formats manager (0x18dcf0's cache 0x10caa24) is a fake whose slot 8 gives one of four format fakes by
//     id; a format's slot 9 (FindElementAny) has an element for a usage and index by a salt drawn per seed (type 0-5,
//     else 6), noting each question, and slot 16 (Describe) puts "fmt<id>" in the string (its slot 4);
//   - Init's context key is three words, drawn to fit the part's requirements half of the time (then one requirement
//     broken two times in three), else random; light type 0 never gets a count (the image would read 0x7ffffffc bytes
//     past the key: the ORIGINAL-BUG in Init);
//   - cRZStd's report slot (0x1bfb92) is a stand-in noting the file, line, group, level and the formatted text.
// The parts' fields are noted with the element vector as its count and contents (its pointers differ by side).
#include "sg_shader_attr_fx.h"

#include "code_patch.h"
#include "gz/cVertexFormat.h"
#include "rz/cRZStd.h"
#include "sg/cValidateRenderShaderContext.h"

#include <cstdarg>
#include <string>

namespace {

constexpr uint32_t kFormatsCache = 0x10caa24;

void NoteText(uint32_t tag, const std::string &text)
{
    gLog.push_back(tag);
    gLog.push_back(static_cast<uint32_t>(text.size()));
    for (size_t i = 0; i < text.size(); i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text.data() + i, text.size() - i < 4 ? text.size() - i : 4);
        gLog.push_back(w);
    }
    gLog.push_back(0xeeeeeeee);
}

int32_t __cdecl StandReport(cRZStd *self, const char *group, int32_t level, const char *format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Note({0x77, self->mSecond, static_cast<uint32_t>(level), self->mFlags});
    NoteText(0x78, std::string(self->File()) + "|" + group + "|" + buf);
    return 0;
}

// ---- the vertex formats ----
struct Fake {
    void **vt;
};
void __fastcall FakeTrap(Fake *, void *) { Note({0xdeab}); }

const uint32_t kUsages[] = {0x5b830781, 0x3b83078b, 0xdb830795, 0xbb8307ab, 0x89d92ba0, 0x12345678};

void *gFormatVTable[20];
struct Format {
    void **vt;
    uint32_t id;
    uint32_t salt;
};
Format gFormats[4] = {{gFormatVTable, 0}, {gFormatVTable, 1}, {gFormatVTable, 2}, {gFormatVTable, 3}};

nGZGraphic4::cVertexElement *__fastcall FormatFind(Format *self, void *, nGZGraphic4::cVertexElement *out, uint32_t usage,
                                                   uint8_t index)
{
    Note({0x91, self->id, usage, index});
    std::memset(out, 0, sizeof *out);
    out->usage = usage;
    out->index = index;
    const bool has = ((usage >> 3 ^ index ^ self->salt) & 3) != 0;
    out->type = has ? (usage + index + self->salt) % 6 : 6;
    return out;
}

void __fastcall FormatDescribe(Format *self, void *, void *text)
{
    Note({0x92, self->id});
    char s[16];
    std::snprintf(s, sizeof s, "fmt%u", self->id);
    using FromChar = void(__thiscall *)(void *, const char *);
    reinterpret_cast<FromChar>((*static_cast<void ***>(text))[4])(text, s);
}

void *gManagerFakeVTable[12];
Fake gFormatsManager{gManagerFakeVTable};
void *__fastcall ManagerGetFormat(Fake *, void *, uint32_t id)
{
    Note({0x90, id});
    return &gFormats[id & 3];
}

void ValidateWorld(std::mt19937 &rng)
{
    for (void *&v : gFormatVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gFormatVTable[9] = reinterpret_cast<void *>(&FormatFind);
    gFormatVTable[16] = reinterpret_cast<void *>(&FormatDescribe);
    for (Format &f : gFormats) f.salt = rng();
    for (void *&v : gManagerFakeVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gManagerFakeVTable[8] = reinterpret_cast<void *>(&ManagerGetFormat);
}

// The world for a test: the extra parts, the formats manager in its cache, the report stand-in.
struct World {
    void **cache;
    void *saved;
    shape::CodePatch report;
    explicit World(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(kFormatsCache)), saved(*cache),
          report(0x1bfb92, reinterpret_cast<const void *>(&StandReport))
    {
        gImageBase = ctx.image.Base();
        gWorldExtra = &ValidateWorld;
        *cache = &gFormatsManager;
    }
    ~World()
    {
        *cache = saved;
        gWorldExtra = nullptr;
    }
};

int32_t PickCount(std::mt19937 &rng, uint32_t limit)
{
    switch (rng() % 6) {
    case 0: return -1;
    case 1: return static_cast<int32_t>(0x80000000u | rng());
    case 2: return static_cast<int32_t>(rng() % 0x100);
    default: return static_cast<int32_t>(rng() % limit);
    }
}

// One of the setters (slots 14 to 19) with drawn values.
void SetParts(void *p, std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0:
    case 1:
        Slot<void>(p, 14, kUsages[rng() % 6], static_cast<uint32_t>(rng() % 3 | (rng() & 0xffffff00u)),
                   static_cast<uint32_t>((rng() % 4 ? rng() % 3 : rng() % 256) | (rng() & 0xffffff00u)));
        break;
    case 2: {
        static const int32_t modes[] = {0, 1, 2, 3, -1};
        Slot<void>(p, 15, static_cast<uint32_t>(rng() % 4), modes[rng() % 5]);
        break;
    }
    case 3:
    case 4: Slot<void>(p, 16, static_cast<uint32_t>(1 + rng() % 12), PickCount(rng, 4)); break;
    case 5: Slot<void>(p, 17, PickCount(rng, 0x80)); break;
    case 6: Slot<void>(p, 18, PickCount(rng, 0x40)); break;
    default: Slot<void>(p, 19, PickCount(rng, 0x10)); break;
    }
}

const Leaf &ValidateLeaf()
{
    static const Leaf leaf = {
        "cValidateRenderShaderContext", 0xe76a0c, 0xac3370, 0xabdeb0, 0xabe390, 20,
        {{2, 0xac6060}, {8, 0xac4740}, {14, 0xabeb40}, {15, 0xabeac0}, {16, 0xabead0}, {17, 0x230b70},
         {18, 0x3bdbc0}, {19, 0x209e20}, {20, 0xabeaa0}, {21, 0xac3660}, {22, 0xac54e0}},
        reinterpret_cast<Factory>(&cValidateRenderShaderContext::Create), sizeof(cValidateRenderShaderContext),
        &SetParts, 0xffffffffu,
        [](void *p) { new (p) cValidateRenderShaderContext; },
        [](void *p) { static_cast<cValidateRenderShaderContext *>(p)->~cValidateRenderShaderContext(); }};
    return leaf;
}

// The fields: the count, the registered byte, the hash, the elements (count and words), then +0x1c to the end.
void NoteParts(const void *o, uint32_t tag)
{
    const uint8_t *const p = static_cast<const uint8_t *>(o);
    uint32_t w[6];
    std::memcpy(w, p + 4, 24);
    const uint32_t *const first = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(w[3]));
    const uint32_t *const last = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(w[4]));
    Note({tag, w[0], w[1] & 0xff, w[2], static_cast<uint32_t>(last - first)});
    if (first) gLog.insert(gLog.end(), first, last);
    for (size_t off = 0x1c; off < 0x6c; off += 4) {
        uint32_t v;
        std::memcpy(&v, p + off, 4);
        gLog.push_back(v);
    }
    gLog.push_back(0xeeeeeeee);
}

// A key for the part: half of the time fitting its requirements (then two times in three one of them broken).
void DrawKey(const void *o, uint32_t key[3], std::mt19937 &rng)
{
    for (int i = 0; i < 3; ++i) key[i] = rng();
    if (rng() % 2) return;
    int32_t f[25];
    std::memcpy(f, static_cast<const uint8_t *>(o) + 0x1c, sizeof f);
    const int32_t *const flags = f, *const lights = f + 4;
    uint32_t bits = key[2];
    for (int i = 0; i < 4; ++i) {
        if (flags[i] == 0) bits &= ~(1u << i);
        if (flags[i] == 1) bits |= 1u << i;
    }
    if (f[17] >= 0) bits = (bits & ~(0x7fu << 15)) | (uint32_t(f[17]) & 0x7f) << 15;
    if (f[18] >= 0) bits = (bits & ~(0x3fu << 22)) | (uint32_t(f[18]) & 0x3f) << 22;
    if (f[19] >= 0) bits = (bits & ~(0xfu << 11)) | (uint32_t(f[19]) & 0xf) << 11;
    key[2] = bits;
    uint32_t total = key[0] & 0xf;
    for (uint32_t i = 1; i < 13; ++i) {
        total = (total + (lights[i] >= 0 ? uint32_t(lights[i]) : rng())) & 0xf;
        uint32_t &word = key[i >> 3];
        word = (word & ~(0xfu << ((i & 7) * 4))) | total << ((i & 7) * 4);
    }
    if (rng() % 3) key[rng() % 3] ^= 1u << (rng() % 32);
}

// Init with a key, random caps and table words, and a report word whose low byte is drawn.
void NoteInit(void *p, std::mt19937 &rng)
{
    uint32_t key[3];
    DrawKey(p, key, rng);
    const uint32_t report = (rng() & 0xffffff00u) | (rng() % 3 ? 1u + rng() % 255 : 0u);
    const bool r = Slot<bool>(p, 8, rng(), static_cast<void *>(key), rng(), report);
    Note({0x66, uint32_t(r), Slot<uint32_t>(p, 6)});
}

} // namespace

// The factory 0xac3370 and every slot on two parts: the setters 0xabeb40 0xabeac0 0xabead0 (and the folded 0x230b70
// 0x3bdbc0 0x209e20), Init 0xac4740 (every failure line), HashParams 0xac3660 and EqualParams 0xac54e0 (on twins one
// setter call apart), IsEquivalent, and the last Release (the shared 0xac6060) through the deleting destructor 0xabeaa0
// (the element vector freed by the destructor 0xabe390).
DIFF_TEST(sg_validate_render_shader_context, 0xac3370)
{
    World world(ctx);
    const Leaf &l = ValidateLeaf();
    CheckSides(ctx, l.name, 1500, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const a = make();
        void *const b = make();
        if (original && !CheckVTable(ctx, l, a)) return;
        Slot<uint32_t>(a, 1);
        Slot<uint32_t>(b, 1);
        NoteParts(a, 0x41);
        for (int k = 0; k < 20; ++k) {
            switch (rng() % 8) {
            case 0:
            case 1:
            case 2: SetParts(a, rng); break;
            case 3: SetParts(b, rng); break;
            case 4:
            case 5: NoteInit(a, rng); break;
            case 6:
                NoteInit(b, rng);
                Note({0x67, uint32_t(Slot<bool>(a, 7, b)), Slot<uint32_t>(a, 21), uint32_t(Slot<bool>(a, 22, b))});
                break;
            default: Note({0x60, Slot<uint32_t>(b, 21), uint32_t(Slot<bool>(b, 22, a))}); break;
            }
        }
        NoteParts(a, 0x42);
        NoteParts(b, 0x42);
        // Twins: the same setter calls on two fresh parts, then (two times in three) one more on the second.
        for (int t = 0; t < 6; ++t) {
            void *const x = make();
            void *const y = make();
            Slot<uint32_t>(x, 1);
            Slot<uint32_t>(y, 1);
            const unsigned calls = rng() % 5;
            std::mt19937 rx = rng;
            for (unsigned n = 0; n < calls; ++n) SetParts(x, rx);
            for (unsigned n = 0; n < calls; ++n) SetParts(y, rng);
            if (t % 3) SetParts(y, rng);
            Note({0x44, uint32_t(Slot<bool>(x, 22, y)), uint32_t(Slot<bool>(y, 22, x)), Slot<uint32_t>(x, 21),
                  Slot<uint32_t>(y, 21)});
            for (void *const p : {x, y}) Slot<uint32_t>(p, 2);
        }
        for (void *const p : {a, b}) {
            difftest::FreeRecorder freed;
            const uint32_t r = Slot<uint32_t>(p, 2);
            freed.Stop();
            Note({0x43, r, uint32_t(freed.Count()), uint32_t(freed.Freed(p))});
        }
    });
}

// Init 0xac4740 on fresh parts with up to eight setter calls: each requirement's line, the format's questions.
DIFF_TEST(sg_validate_render_shader_context_init, 0xac4740)
{
    World world(ctx);
    const Leaf &l = ValidateLeaf();
    CheckSides(ctx, "init", 3000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        const unsigned calls = rng() % 9;
        for (unsigned n = 0; n < calls; ++n) SetParts(p, rng);
        NoteInit(p, rng);
        NoteInit(p, rng);
        NoteParts(p, 0x41);
        Slot<uint32_t>(p, 2);
    });
}

// The constructor 0xabdeb0 and destructor 0xabe390 alone on a block of pattern words (the vector's words left null for
// the destructor: it would free them).
DIFF_TEST(sg_validate_render_shader_context_construct_destruct, 0xabdeb0)
{
    World world(ctx);
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    const Leaf &l = ValidateLeaf();
    CheckSides(ctx, l.name, 32, [&](bool original, std::mt19937 &rng) {
        alignas(8) uint32_t block[28];
        for (uint32_t &w : block) w = rng();
        if (original) {
            void *const r = ctx.Original<Ctor>(l.ctor)(block);
            Note({0x50, uint32_t(r == block), uint32_t(block[0] == uint32_t(ctx.image.Base() + l.vtable))});
        } else {
            l.placement(block);
            Note({0x50, 1, 1});
        }
        gLog.insert(gLog.end(), block + 1, block + 28);
        for (size_t i = 1; i < l.size / 4; ++i) block[i] = rng();
        block[4] = block[5] = block[6] = 0;
        if (original)
            ctx.Original<Dtor>(l.dtor)(block);
        else
            l.destroy(block);
        gLog.insert(gLog.end(), block + 1, block + 28);
    });
}

// The light type names 0xaeed90 for every type and a few past them (the text compared).
DIFF_TEST(sg_light_type_name, 0xaeed90)
{
    using Fn = const char *(__cdecl *)(uint32_t);
    for (uint32_t pass = 0; pass < 2; ++pass)
        for (uint32_t type = 0; type < 20; ++type) {
            const uint32_t t = pass ? type * 0x0d0d0d0du + 0x7fffffffu : type;
            const std::string want = ctx.Original<Fn>(0xaeed90)(t);
            const std::string got = LightTypeName(t);
            ++ctx.cases;
            if (want != got) ctx.Fail("light type %u: %s vs %s", t, want.c_str(), got.c_str());
        }
}
