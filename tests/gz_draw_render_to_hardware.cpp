// Differential tests for cGZDrawContextRenderToHardware, the hardware draw context engine's base, as a whole class
// (src/gz/cGZDrawContextRenderToHardware.h; lane OD, rendering R4b).
//
// GZDrawRenderToHardware_Slots: the class is abstract (slots 25 to 34 are its derived class's), so each side drives an
// object whose slots 25 to 34 are one recording class's (RenderToHardwareRecorder below: each logs its arguments, the
// vertices it is handed word for word, and answers from a script both sides share). Ours is that class (our slots 0 to
// 24, placement new); the original is built by its constructor 0x8b8080 and given a copy of the game's vtable 0xe4ec84
// with slots 25 to 34 taken from the recorder's, so it runs the game's slots 0 to 24 and helpers. Both live in a block
// of the same seeded bytes. A case is 70 random slot calls through the object's vtable: SetTarget (targets with and
// without the buffer face, OnTargetSet's answer scripted), Detach, GetBounds, SetClip, SetColors, the state words and
// bytes, gradients (and none), textures (the texture buffer, at origins and scales, scaled or not; a stranger without
// the buffer face; none; SetTextureSource's answer scripted; GetTextureTiles hands out a tiling of the texture's
// bounds), texture clips, the batch slots, ChooseRoute (kind 6 triangles, kind 3 rectangles, other kinds) and
// DrawSpans. Targets are large enough to scale the UI (height 1080 and over, LegacyCalculateUIScale 0x8b8390 on both
// sides) in some cases. Calls that reach the target (GetBounds, SetClip, a texture, ChooseRoute) are made only while
// the engine holds one (the original dereferences it unchecked). After every call the log gets the result, the
// out-parameters and every word of the object but the vtable, with the four vectors as their element counts and
// capacities and their elements word for word. At the end the object is released to zero (the destructor) and the log
// gets the fakes' reference counts and the side's heap balance.
// GZDrawRenderToHardware_Construct: the constructor on seeded bytes, every byte but the vtable word.
#include "difftest.h"
#include "drawfx.h"

#include "game/game.h"
#include "gz/cGZAny.h"
#include "gz/cGZDrawContextRenderToHardware.h"

#include <Windows.h>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Hw = cGZDrawContextRenderToHardware;
using drawfx::Ret;
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U,
    {0, Ret::Int, true, "GetWidth", ""},
    {0, Ret::Int, true, "GetHeight", ""},
    {1, Ret::Int, true, "GetBounds", "i"}, // the hidden result pointer
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 40, kBufferSlots};
inline constexpr drawfx::Slot kUnknownSlots[] = {kQI, kAddRef, kRelease};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

constexpr uint32_t kConstructor = 0x8b8080, kGameVTable = 0xe4ec84;
constexpr uint32_t kFramework = 0x10c9378, kDesignHeight = 0x10ecc88;
constexpr uint32_t kObjectTag = 0x0c000000;

// ---- the case ----

struct Buffer {
    uint32_t width, height;
    cGZRect bounds;
    bool bufferFace;
};
struct Case {
    Buffer buffers[3]; // two targets, the texture
    uint32_t tileWidth, tileHeight, border;
};
Case gCase;
drawfx::Fake gBuffers[3], gStranger;
std::mt19937 gScript; // the recorder's answers (same seed per side, so the same answers in the same order)

uint32_t GetWidth(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].width; }
uint32_t GetHeight(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].height; }
uint32_t GetBounds(drawfx::Fake &self, const uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &gCase.buffers[self.value[0]].bounds, sizeof(cGZRect));
    return args[0];
}

void BuildWorld()
{
    for (unsigned i = 0; i < 3; ++i) {
        drawfx::Fake &f = gBuffers[i];
        f.Reset<kBuffer>(600 + i);
        f.value[0] = i;
        f.On(9, &GetWidth);
        f.On(10, &GetHeight);
        f.On(11, &GetBounds);
        if (gCase.buffers[i].bufferFace) f.AddFace(cIGZBuffer::kIid, f);
    }
    gStranger.Reset<kUnknown>(610);
}

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 11);
    for (unsigned i = 0; i < 3; ++i) {
        Buffer &b = gCase.buffers[i];
        const bool big = i < 2 && rng() % 4 == 0; // a UI-scaled target
        b.width = big ? 1400 + rng() % 600 : 1 + rng() % 300;
        b.height = big ? 1080 + rng() % 400 : 1 + rng() % 200;
        const int32_t left = rng() % 3 ? 0 : int32_t(rng() % 5), top = rng() % 3 ? 0 : int32_t(rng() % 5);
        b.bounds = {left, top, left + int32_t(b.width), top + int32_t(b.height)};
        b.bufferFace = i != 1 || rng() % 5 != 0;
    }
    gCase.tileWidth = 4u << (rng() % 4);
    gCase.tileHeight = 4u << (rng() % 4);
    gCase.border = rng() % 2;
}

// ---- the recorder: slots 25 to 34 ----

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, 4);
    return v;
}

void NoteVertices(const char *label, const Hw::Vertex *vertices, uint32_t count)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t w[6];
        std::memcpy(w, &vertices[i], sizeof(w));
        c.words.insert(c.words.end(), w, w + 6);
    }
    drawfx::gLog.push_back(std::move(c));
}

class RenderToHardwareRecorder final : public cGZDrawContextRenderToHardware {
public:
    bool OnTargetSet() override
    {
        const uint32_t answer = gScript() % 5 != 0;
        drawfx::Note("OnTargetSet", answer);
        return answer != 0;
    }
    void OnDetach() override { drawfx::Note("OnDetach", 0); }
    void ApplyState() override { drawfx::Note("ApplyState", 0); }
    void SetViewport(const cGZRect *v) override
    {
        drawfx::Note("SetViewport", {uint32_t(v->left), uint32_t(v->top), uint32_t(v->right), uint32_t(v->bottom)});
    }
    void SetProjection(const float *m) override
    {
        drawfx::Call c;
        c.note = "SetProjection";
        for (int i = 0; i < 16; ++i) c.words.push_back(std::bit_cast<uint32_t>(m[i]));
        drawfx::gLog.push_back(std::move(c));
    }
    bool SetTextureSource(cIGZBuffer *texture) override
    {
        const uint32_t answer = texture ? gScript() % 6 != 0 : 0;
        drawfx::Note("SetTextureSource", {drawfx::Norm(drawfx::P(texture)), answer});
        return answer != 0;
    }
    // A tiling of the texture's bounds: tiles of the case's size less a border each side, one piece per tile.
    void GetTextureTiles(std::vector<Tile> *tiles, std::vector<TileRect> *rects) override
    {
        drawfx::Note("GetTextureTiles", {uint32_t(tiles->size()), uint32_t(rects->size())});
        const cGZRect &b = gCase.buffers[2].bounds;
        const int32_t border = int32_t(gCase.border);
        const int32_t cellW = int32_t(gCase.tileWidth) - 2 * border, cellH = int32_t(gCase.tileHeight) - 2 * border;
        int32_t index = 0;
        for (int32_t y = 0; y < b.bottom - b.top && index < 40; y += cellH)
            for (int32_t x = 0; x < b.right - b.left && index < 40; x += cellW, ++index) {
                tiles->push_back({index, 1, int32_t(gCase.tileWidth), int32_t(gCase.tileHeight)});
                const int32_t w = (std::min)(cellW, b.right - b.left - x), h = (std::min)(cellH, b.bottom - b.top - y);
                rects->push_back({{x, y, x + w, y + h}, {border, border, border + w, border + h}});
            }
    }
    void SelectTile(int32_t index, uint32_t scaled, uint32_t zero) override
    {
        drawfx::Note("SelectTile", {uint32_t(index), scaled, zero});
    }
    void DrawTriangles(const Vertex *vertices, uint32_t count) override { NoteVertices("DrawTriangles", vertices, count); }
    void DrawQuads(const Vertex *vertices, uint32_t count) override { NoteVertices("DrawQuads", vertices, count); }
};

// ---- the object ----

template <class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}

template <class T> void NoteVector(drawfx::Call &c, const void *object, uint32_t offset)
{
    const auto *v = reinterpret_cast<const std::vector<T> *>(static_cast<const uint8_t *>(object) + offset);
    c.words.push_back(uint32_t(v->size()));
    c.words.push_back(uint32_t(v->capacity()));
    for (const T &e : *v) {
        uint32_t w[sizeof(T) / 4];
        std::memcpy(w, &e, sizeof(T));
        c.words.insert(c.words.end(), w, w + sizeof(T) / 4);
    }
}

// Every word of the object but its vtable; the vectors as their counts, capacities and elements. The target (+8), the
// texture (+0x24) and the texture source (+0x28) are pointers; every other word is data, logged as it is (the block
// starts as random bytes, so a data word may happen to point into a registered range on one side only).
void NoteObject(void *object)
{
    drawfx::Call c;
    c.note = "object";
    for (uint32_t off = 4; off < Hw::kSize; off += 4) {
        if (off >= 0xb4 && off < 0xe4) continue;
        const bool pointer = off == 0x8 || off == 0x24 || off == 0x28;
        c.words.push_back(pointer ? drawfx::Ptr(Word(object, off)) : Word(object, off));
    }
    NoteVector<Hw::Vertex>(c, object, 0xb4);
    NoteVector<Hw::Tile>(c, object, 0xc0);
    NoteVector<Hw::TileRect>(c, object, 0xcc);
    NoteVector<Hw::TexelRect>(c, object, 0xd8);
    drawfx::gLog.push_back(std::move(c));
}

void *Block(uint32_t seed)
{
    uint8_t *const block = static_cast<uint8_t *>(::operator new(Hw::kSize));
    std::mt19937 rng(seed * 31u + 7);
    for (uint32_t i = 0; i < Hw::kSize; ++i) block[i] = uint8_t(rng());
    return block;
}

// The original's vtable: the game's slots 0 to 24, the recorder's 25 to 34.
void *gOriginalTable[35];
void *OriginalCreate(uint32_t seed)
{
    alignas(RenderToHardwareRecorder) uint8_t scratch[sizeof(RenderToHardwareRecorder)];
    auto *recorder = ::new (static_cast<void *>(scratch)) RenderToHardwareRecorder;
    void *const *ours = *reinterpret_cast<void *const *const *>(recorder);
    void *const *game = GameAddress<void *const>(kGameVTable);
    for (unsigned i = 0; i < 35; ++i) gOriginalTable[i] = i < 25 ? game[i] : ours[i];
    recorder->~RenderToHardwareRecorder();
    using Ctor = void *(__thiscall *)(void *);
    void *const object = reinterpret_cast<Ctor>(GameAddress<void>(kConstructor))(Block(seed));
    *static_cast<void ***>(object) = gOriginalTable;
    return object;
}
void *OurCreate(uint32_t seed)
{
    return static_cast<cIGZDrawContextEngine *>(::new (Block(seed)) RenderToHardwareRecorder);
}

float Unit(std::mt19937 &rng) { return float(rng() % 1001) / 1000.0f; }

void RunSide(bool ours, uint32_t seed)
{
    drawfx::Cells cells;
    cells.Set(kFramework, 0u);
    cells.Set(kDesignHeight, std::bit_cast<uint32_t>(768.0f));
    drawfx::gLog.reserve(1 << 14);
    gScript.seed(seed ^ 0x5eed);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = ours ? OurCreate(seed) : OriginalCreate(seed);
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(object, Hw::kSize, kObjectTag);
    }
    NoteObject(object);
    drawfx::Note("addref", Call<uint32_t>(object, 1));

    int target = -1;       // the buffer the engine holds, or none
    bool textured = false; // a texture is set (the last SetTexture got through)
    for (int step = 0; step < 70; ++step) {
        uint32_t pick = rng() % 26;
        if (target < 0 && rng() % 2) pick = 0;
        drawfx::Note("pick", pick);
        const Buffer *const t = target >= 0 ? &gCase.buffers[target] : nullptr;
        switch (pick) {
        case 0: {
            const unsigned which = rng() % 2;
            const uint32_t ok = Call<uint32_t>(object, 3, static_cast<void *>(&gBuffers[which])) & 0xff;
            drawfx::Note("settarget", ok);
            target = ok ? int(which) : -1;
            break;
        }
        case 1: Call<void>(object, 4); break;
        case 2: {
            if (!t) break;
            cGZRect r{-7, -7, -7, -7};
            const uint32_t back = Call<uint32_t>(object, 5, &r);
            drawfx::Note("bounds", {uint32_t(back == drawfx::P(&r)), uint32_t(r.left), uint32_t(r.top), uint32_t(r.right),
                                    uint32_t(r.bottom)});
            break;
        }
        case 3: {
            if (!t) break;
            const int32_t w = int32_t(t->width), h = int32_t(t->height);
            cGZRect r{int32_t(rng() % 20) - 5, int32_t(rng() % 20) - 5, w - int32_t(rng() % 20) + 5, h - int32_t(rng() % 20) + 5};
            if (rng() % 8 == 0) r.right = r.left;
            if (rng() % 10 == 0) r = {w + 3, h + 3, w + 9, h + 9};
            Call<void>(object, 6, &r);
            break;
        }
        case 4: {
            uint32_t colours[4];
            for (uint32_t &c : colours) c = rng();
            const uint32_t first = rng() % 4, count = rng() % (5 - first);
            Call<void>(object, 7, first, count, static_cast<const uint32_t *>(colours));
            break;
        }
        case 5: {
            const uint32_t values[] = {0, 1, 2, 3, 4, 0x80000000u, 0xffffffffu};
            Call<void>(object, 8, rng() % 4 ? values[rng() % 7] : rng());
            break;
        }
        case 6: case 7: case 8: {
            const uint32_t value = (rng() & 0xffffff00u) | (rng() % 3 ? rng() % 2 : rng() & 0xff);
            Call<void>(object, pick + 3, value);
            break;
        }
        case 9: Call<void>(object, 12, Unit(rng) * 4.0f); break;
        case 10: {
            if (rng() % 5 == 0) {
                Call<void>(object, 13, static_cast<const float *>(nullptr), 0.0f, 0.0f);
                break;
            }
            float c[12];
            for (int i = 0; i < 4; ++i) c[i] = Unit(rng);
            for (int i = 4; i < 12; ++i) c[i] = (Unit(rng) - 0.5f) * (rng() % 4 ? 0.05f : 1.0f);
            if (rng() % 10 == 0)
                for (float &v : c) v = (Unit(rng) - 0.5f) * 4.0f;
            const float x = float(int(rng() % 60) - 10) + (rng() % 2 ? 0.5f : 0.0f), y = float(int(rng() % 40) - 10);
            Call<void>(object, 13, static_cast<const float *>(c), x, y);
            break;
        }
        case 11: case 12: {
            const uint32_t kind = rng() % 6;
            void *const texture = kind < 4 ? static_cast<void *>(&gBuffers[2]) : kind == 4 ? static_cast<void *>(&gStranger)
                                                                                           : nullptr;
            if (texture && !t) break; // the common part asks the target's bounds
            const float scales[] = {1.0f, 1.0f, 1.0f, 2.0f, 0.5f, 1.5f, 0.75f, 1.00001f};
            const float sx = scales[rng() % 8], sy = rng() % 3 ? sx : scales[rng() % 8];
            const float x = float(int(rng() % 60) - 10) + (rng() % 3 ? 0.0f : Unit(rng));
            const float y = float(int(rng() % 40) - 8) + (rng() % 3 ? 0.0f : Unit(rng));
            cGZRect r{-9, -9, -9, -9};
            const uint32_t ok = Call<uint32_t>(object, 14, texture, x, y, sx, sy, &r) & 0xff;
            drawfx::Note("texture", {ok, uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
            textured = Word(object, 0x24) != 0;
            break;
        }
        case 13: Call<void>(object, 15); break;
        case 14: {
            const Buffer &x = gCase.buffers[2];
            const int32_t l = int32_t(rng() % x.width), tp = int32_t(rng() % x.height);
            cGZRect r{l, tp, l + 1 + int32_t(rng() % (x.width - l)), tp + 1 + int32_t(rng() % (x.height - tp))};
            if (rng() % 4 == 0) r.right = l + 1;
            if (rng() % 4 == 0) r.bottom = tp + 1;
            Call<void>(object, 16, &r);
            break;
        }
        case 15: Call<void>(object, 17); break;
        case 16: Call<void>(object, 18); break;
        case 17: Call<void>(object, 19); break;
        case 18: {
            cGZDrawSurface s;
            std::memset(&s, 0xcd, sizeof(s));
            drawfx::Note("lock", Call<uint32_t>(object, 20, &s, rng() % 2) & 0xff);
            Call<void>(object, 21);
            break;
        }
        case 19: case 20: case 21: case 22: case 23: {
            if (!t) break;
            const uint32_t kinds[] = {6, 6, 3, 3, 1, 7};
            const uint32_t kind = kinds[rng() % 6];
            uint32_t count = kind == 3 ? 2 * (1 + rng() % 4) : 1 + rng() % 9;
            if (rng() % 10 == 0) count = kind == 3 || !textured ? rng() % 3 : 1 + rng() % 2;
            std::vector<float> points(2 * size_t(count) + 2);
            const float w = float((std::min)(t->width, 400u)), h = float((std::min)(t->height, 300u));
            for (uint32_t i = 0; i < count; ++i) {
                points[2 * i] = float(int(rng() % uint32_t(w + 20)) - 10) + Unit(rng);
                points[2 * i + 1] = float(int(rng() % uint32_t(h + 20)) - 10) + Unit(rng);
                if (rng() % 6 == 0) points[2 * i] = float(int(rng() % uint32_t(w + 1)));
                if (rng() % 6 == 0) points[2 * i + 1] = float(int(rng() % uint32_t(h + 1)));
            }
            drawfx::Note("route", Call<uint32_t>(object, 22, kind, static_cast<const float *>(points.data()), count));
            break;
        }
        default: Call<void>(object, 23, static_cast<const int32_t *>(nullptr), 0u); break;
        }
        NoteObject(object);
    }

    const uint32_t count = Word(object, 4);
    drawfx::Note("count", count);
    if (rng() % 2) {
        uint32_t out = 0xdeadbeef;
        const uint32_t ids[] = {1, Hw::kIid, 0x12345678};
        const uint32_t answer = Call<uint32_t>(object, 0, ids[rng() % 3], static_cast<void *>(&out)) & 0xff;
        drawfx::Note("query", {answer, drawfx::Norm(out)});
        if (answer) drawfx::Note("release", Call<uint32_t>(object, 2));
    }
    for (uint32_t i = 0; i < count; ++i) drawfx::Note("release", Call<uint32_t>(object, 2));
    heap.Stop();
    drawfx::Note("refs", {uint32_t(gBuffers[0].refs), uint32_t(gBuffers[1].refs), uint32_t(gBuffers[2].refs),
                          uint32_t(gStranger.refs)});
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

} // namespace

DIFF_TEST(GZDrawRenderToHardware_Slots, 0x8b8b70)
{
    // Every slot (QueryInterface 0x8b82c0, AddRef 0x8b8290, Release 0x8b82a0, SetTarget 0x8b82f0, Detach 0x8b8370,
    // GetBounds 0x8b8380, SetClip 0x8b8520, SetColors 0x8b85d0, vf8 0x8b8ab0, vf9 0x8b8af0, vf10 0x8b8b00, vf11
    // 0x8b8b10, SetLineWidth 0x8b8b20, SetGradient 0x8b89c0, SetTexture 0x8b8600, ClearTextureClip 0x8b8990,
    // SetTextureClip 0x8b89a0, vf17 0x8b8b30, vf18 0x8b8b40, vf19 0x8b8b60, ChooseRoute 0x8b8b70, DrawSpans 0x8b8c10),
    // the helpers FlushBatch 0x8baa50, ApplyClip 0x8ba660, GradientColor 0x8b8c40, DrawTriangleRoute 0x8b9e10,
    // DrawRectRoute 0x8b8d30, DrawTiles 0x8ba3a0, the constructor 0x8b8080 and the deleting destructor 0x8baf80 /
    // destructor 0x8b8140 (the last Release).
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 3000); ++seed) {
        NewCase(seed);
        ctx.Mark("orig");
        auto a = drawfx::Side(&BuildWorld, [&] { RunSide(false, seed); });
        NewCase(seed);
        ctx.Mark("ours");
        auto b = drawfx::Side(&BuildWorld, [&] { RunSide(true, seed); });
        if (!drawfx::Same(ctx, a.first, b.first, "render to hardware", seed, a.second)) break;
    }
}

DIFF_TEST(GZDrawRenderToHardware_Construct, 0x8b8080)
{
    // The constructor on seeded bytes: every byte it writes and every byte it leaves, but the vtable word.
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    alignas(8) uint8_t a[Hw::kSize], b[Hw::kSize];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        void *const ra = original(a);
        auto *const rb = ::new (static_cast<void *>(b)) RenderToHardwareRecorder;
        ++ctx.cases;
        if (ra != a || static_cast<void *>(rb) != static_cast<void *>(b)) ctx.Fail("seed %u: another address", seed);
        if (Word(a, 0) != drawfx::P(GameAddress<void>(kGameVTable))) ctx.Fail("seed %u: the original's vtable", seed);
        std::memset(a, 0, 4);
        std::memset(b, 0, 4);
        for (uint32_t off = 0; off < sizeof(a); ++off)
            if (a[off] != b[off]) {
                ctx.Fail("seed %u: byte +%#x orig %#x ours %#x", seed, off, a[off], b[off]);
                return;
            }
    }
}

// ---- LegacyCalculateUIScale 0x8b8390 ----
//
// GZDrawRenderToHardware_UIScale: both sides called directly on random bounds, with a fake framework (the settings
// service asked for through slot 5, or none, or no framework at all), a fake settings object (slot 9 hands the key and the
// any, slot 2 the release) and the four height globals random as in tests/gz_win_mgr_w32.cpp. Compared: the bits of the
// result and every call with its arguments (the key's text, the any's vtable and default value).
namespace {

struct ScaleScript {
    bool settings, writes;
    uint32_t value;
    std::vector<uint32_t> log;
} gScale;

struct ScaleFake {
    void *const *vtable;
};
ScaleFake gScaleSettings;

uint32_t __fastcall ScaleQuery(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    gScale.log.insert(gScale.log.end(), {5, id, iid, uint32_t(*out != nullptr)});
    if (gScale.settings) *out = &gScaleSettings;
    return 0;
}

uint32_t __fastcall ScaleRead(void *self, void *, const std::string *key, uint32_t *any)
{
    // The any is the original's AnyUint32 (vtable 0xd9f534) or ours: the same class on each side.
    const gz::AnyUint32 ours(0);
    const bool isAny = any[0] == reinterpret_cast<uintptr_t>(GameAddress<void>(0xd9f534)) || any[0] == *reinterpret_cast<const uint32_t *>(&ours);
    gScale.log.insert(gScale.log.end(), {9, uint32_t(self == &gScaleSettings), uint32_t(key->size()), uint32_t(key->capacity()), uint32_t(isAny), any[1]});
    for (const char c : *key) gScale.log.push_back(uint8_t(c));
    if (gScale.writes) any[1] = gScale.value;
    return gScale.writes;
}

uint32_t __fastcall ScaleRelease(void *self, void *)
{
    gScale.log.insert(gScale.log.end(), {2, uint32_t(self == &gScaleSettings)});
    return 0;
}

} // namespace

DIFF_TEST(GZDrawRenderToHardware_UIScale, 0x8b8390)
{
    using Scale = float(__stdcall *)(const cGZRect *);
    const Scale original = ctx.Original<Scale>(0x8b8390);
    void *frameworkTable[8] = {};
    frameworkTable[5] = reinterpret_cast<void *>(&ScaleQuery);
    ScaleFake framework{frameworkTable};
    void *settingsTable[16] = {};
    settingsTable[2] = reinterpret_cast<void *>(&ScaleRelease);
    settingsTable[9] = reinterpret_cast<void *>(&ScaleRead);
    gScaleSettings.vtable = settingsTable;

    float *const minimum = GameAddress<float>(0x105f808), *const design = GameAddress<float>(0x10ecc88),
                *const height2 = GameAddress<float>(0x105f80c), *const height3 = GameAddress<float>(0x105f810);
    void **const frameworkCell = GameAddress<void *>(0x10c9378);
    void *const savedFramework = *frameworkCell;
    const float saved[] = {*minimum, *design, *height2, *height3};

    static const float kMin[] = {0.f, 480.f, 600.f, 1000.f, 1080.f};
    static const float kDesign[] = {768.f, 1024.f, 600.f, 2160.f, 1080.f};
    static const float kF2[] = {1536.f, 1440.f, 768.f};
    static const float kF3[] = {2160.f, 2304.f, 1080.f};
    static const int32_t kHeights[] = {0, 1, -40, 479, 480, 600, 768, 769, 1000, 1024, 1079, 1080, 1081, 1440, 2160, 2304, 4000};
    static const uint32_t kValues[] = {0, 1, 2, 3, 4, 5, 0xffffffffu, 0x80000000u};

    std::mt19937 rng(0x8b8390);
    for (int i = 0; i < 20000; ++i) {
        ++ctx.cases;
        *minimum = kMin[rng() % 5];
        *design = kDesign[rng() % 5];
        *height2 = kF2[rng() % 3];
        *height3 = rng() % 4 ? kF3[rng() % 3] : *design;
        *frameworkCell = rng() % 8 ? &framework : nullptr;
        gScale.settings = rng() % 4 != 0;
        gScale.writes = rng() % 4 != 0;
        gScale.value = kValues[rng() % 8];
        cGZRect bounds;
        bounds.left = int32_t(rng() % 100);
        bounds.right = int32_t(rng() % 3000);
        bounds.top = int32_t(rng() % 200) - 100;
        bounds.bottom = bounds.top + (rng() % 8 ? kHeights[rng() % 17] : int32_t(rng() % 5000));

        gScale.log.clear();
        const float a = original(&bounds);
        const std::vector<uint32_t> logA = gScale.log;
        gScale.log.clear();
        const float b = LegacyCalculateUIScale(&bounds);
        if (std::bit_cast<uint32_t>(a) != std::bit_cast<uint32_t>(b) || logA != gScale.log) {
            ctx.Fail("case %d (height %d, min %g design %g f2 %g f3 %g, value %u): original %g (%zu log words), ours %g (%zu)", i,
                     bounds.bottom - bounds.top, *minimum, *design, *height2, *height3, gScale.value, a, logA.size(), b,
                     gScale.log.size());
            break;
        }
    }
    *frameworkCell = savedFramework;
    *minimum = saved[0];
    *design = saved[1];
    *height2 = saved[2];
    *height3 = saved[3];
}
