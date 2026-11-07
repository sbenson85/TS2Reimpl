// Differential test of the shade light level (lane TL, scenegraph N2r; src/sg/cLightLevels.h): cLightT<cILight>'s
// constructor 0x3c7cb0, called as a base part (most-derived flag 0) by nTSSG::cShadeLight's constructor 0x3c80b0 (and by
// cTerrainShadeLight's 0x3c8120, which is the same shape). The standard light level 0xa45a40 is tested with the game's
// own ambient and directional lights in tests/sg_lights_standard.cpp (SG_StandardLights_Level).
//
// Both sides build the game's cShadeLight (0x64 bytes, the cSGResource part at +0, the node at +0x2c, the virtual base at
// +0x60) with the game's constructor in a buffer, run the same operations through its faces (the game's vtables and
// bodies), and destroy it with the game's destructor 0x3c81c0. On the second side 0x3c7cb0 is patched to our compiler's
// constructor of the template (cLightT_ShadeLight_Constructor), which with flag 0 builds the cSGResource part, the node
// and the referent exactly where the game's does. Every word the object holds, the vbtable's entries, the referrers by id,
// the blocks freed and the fakes' counts are compared.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "sgfx.h"

#include "sg/SGLightInterfaces.h"
#include "sg/cLightLevels.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kShadeCtor = 0x3c80b0, kShadeDtor = 0x3c81c0, kLevel = 0x3c7cb0;
constexpr uint32_t kSize = 0x64, kNode = 0x2c, kVbptr = 0x1c;
constexpr uint32_t kObjectTag = 0x7e000000;
// Vtables (0, 11, 13, 24: the game's derived constructor stores them the same on both sides, but the vbptr and the
// referrer vector (16-18: heap pointers) are compared by content).
constexpr uint32_t kSkipWords = (1u << 7) | (1u << 16) | (1u << 17) | (1u << 18);

using CtorFn = void *(__thiscall *)(void *, const float *, uint32_t);
using DtorFn = void(__thiscall *)(void *);

alignas(16) uint8_t gObject[0x80];
Fake gReferrer[3]; // ids 600-602

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gResources.Reset(cells);
    for (unsigned i = 0; i < 3; ++i) {
        gReferrer[i].Reset<sgfx::kBehavior>(600 + i);
        gReferrer[i].Returns(4, uint32_t(rng() % 2));
    }
}

cILight *Node() { return reinterpret_cast<cILight *>(gObject + kNode); }
cIGZUnknown *Part() { return reinterpret_cast<cIGZUnknown *>(gObject); }
cIGZUnknown *Persist() { return Node()->GetReferent(); }

void NoteObject(const char *label)
{
    const uint32_t *words = reinterpret_cast<const uint32_t *>(gObject);
    drawfx::Call c;
    c.note = label;
    for (unsigned k = 0; k < kSize / 4; ++k)
        if (!(kSkipWords >> k & 1)) c.words.push_back(drawfx::Norm(words[k]));
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(gObject + kVbptr);
    c.words.push_back(uint32_t(vbtable[0]));
    c.words.push_back(uint32_t(vbtable[1]));
    const auto *first = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
    const auto *last = reinterpret_cast<Fake *const *>(uintptr_t(words[17]));
    const auto *end = reinterpret_cast<Fake *const *>(uintptr_t(words[18]));
    c.words.push_back(first != nullptr);
    c.words.push_back(uint32_t(last - first));
    c.words.push_back(uint32_t(end - first));
    for (auto *p = first; p && p != last; ++p) c.words.push_back((*p)->id);
    drawfx::gLog.push_back(std::move(c));
}

void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    const unsigned op = rng() % 6;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph, 0x5b0aaa74, 0xbb11a53e, 1,
                                 0x3b253dc7, 0xfb859324, cISGResource::kIid, 0x9b11bf70, 0x12345678, sgfx::kIidTree};
        const uint32_t iid = iids[rng() % 12];
        const unsigned via = rng() % 3;
        cIGZUnknown *on = via == 0 ? static_cast<cIGZUnknown *>(Node()) : via == 1 ? Persist() : Part();
        void *out = nullptr;
        const uint32_t before = reinterpret_cast<const uint32_t *>(gObject)[1];
        const bool r = rng() % 8 ? on->QueryInterface(iid, &out) : on->QueryInterface(iid, nullptr);
        const uint32_t after = reinterpret_cast<const uint32_t *>(gObject)[1];
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), after - before});
        if (out && after != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part's
        const unsigned via = rng() % 3;
        cIGZUnknown *on = via == 0 ? static_cast<cIGZUnknown *>(Node()) : via == 1 ? Persist() : Part();
        const uint32_t a = on->AddRef();
        drawfx::Note("counts", {via, a, reinterpret_cast<const uint32_t *>(gObject)[1]});
        const uint32_t r = on->Release();
        drawfx::Note("released", r);
        break;
    }
    case 2:
    case 3: { // referrers: added, removed (only one in the list), counted
        Fake &r = gReferrer[rng() % 3];
        auto *b = reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&r);
        bool listed = false;
        const uint32_t *words = reinterpret_cast<const uint32_t *>(gObject);
        for (auto *p = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
             p && p != reinterpret_cast<Fake *const *>(uintptr_t(words[17])); ++p)
            listed |= *p == &r;
        if (listed && rng() % 2)
            Node()->RemoveReferrer(b);
        else
            Node()->AddReferrer(b);
        drawfx::Note("referrers", Node()->GetReferrerCount());
        break;
    }
    case 4: // the resource faces
        drawfx::Note("faces", {drawfx::Norm(P(Node()->GetResource())), drawfx::Norm(P(Node()->GetResourceConst())),
                               drawfx::Norm(P(Node()->GetReferent())), drawfx::Norm(P(Node()->GetReferentConst()))});
        break;
    default: { // the virtual base's flag and segment
        cISGResource *face = Node()->GetResource();
        face->SetFlag(uint8_t(rng()));
        drawfx::Note("resource", {face->GetFlag(), face->HasKey(), drawfx::Norm(P(face->GetSegment()))});
        break;
    }
    }
}

} // namespace

DIFF_TEST(SG_ShadeLightLevel, kLevel)
{
    sgfx::CheckSides(ctx, "shade level", 48, Build, [&](bool original, std::mt19937 &rng) {
        std::optional<shape::CodePatch> level;
        if (!original) level.emplace(kLevel, &cLightT_ShadeLight_Constructor);

        float vec[4];
        for (float &f : vec) f = float(int(rng() % 200) - 100) / 8.0f;
        std::memset(gObject, 0xcd, sizeof(gObject));
        drawfx::AddRange(gObject, kSize, kObjectTag);
        void *const r = ctx.Original<CtorFn>(kShadeCtor)(gObject, vec, 1);
        drawfx::Note("constructor returns", drawfx::Norm(P(r)));
        NoteObject("built");
        Node()->AddRef(); // the test's reference (the light is a buffer)
        for (int k = 0; k < 16; ++k) Operate(ctx, k, rng);
        NoteObject("after");
        // Drop the referrers (the destructor does not), then the game's destructor: the blocks it frees.
        for (Fake &f : gReferrer) {
            auto *b = reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&f);
            const uint32_t *words = reinterpret_cast<const uint32_t *>(gObject);
            for (auto *p = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
                 p && p != reinterpret_cast<Fake *const *>(uintptr_t(words[17])); ++p)
                if (*p == &f) {
                    Node()->RemoveReferrer(b);
                    break;
                }
        }
        difftest::FreeRecorder freed;
        ctx.Original<DtorFn>(kShadeDtor)(gObject);
        freed.Stop();
        size_t sizes[64];
        const size_t n = freed.SortedSizes(sizes, 64);
        drawfx::Call c;
        c.note = "freed";
        for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
        drawfx::gLog.push_back(std::move(c));
        for (const Fake &f : gReferrer) drawfx::Note("referrer", {f.id, uint32_t(f.refs)});
        sgfx::gPool.NoteCounts();
        sgfx::gResources.NoteRefs();
    });
}
