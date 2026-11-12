// Differential test of the debug text billboards' free functions (lane 261111m; src/sg/SGUtilitiesText.cpp).
//
//   SG_Utilities_TextBillboards  a world of two parent nodes (each with a child list: a few pre-existing children
//       with seeded tags, some of them a billboard's "@@TextOutput_<name>") and a texture manager holding a name map
//       (some names pre-registered with a texture that is not procedural); then random operations on either side:
//       MakeBillboardTextOutput 0xaf3860 (translation, name, width, height, colour floats with alpha 1 / not 1 / NaN /
//       infinite, font size), AddNodeTextLabel 0xaf4250 (colours, texts with "%s" / "%d", empty and long),
//       ClearNodeTextLabels 0xaf42a0, RemoveBillboardTextOutput 0xaf4120 and FindNodeTextOutput 0xaf42e0 (its out
//       noted). Names: "", short, "%s", "%d", long. Every call to every fake is logged in order: the director's class
//       objects (material, procedural texture, shape-ref node, shape, from pools, counted), the material's name, type and
//       properties (their texts), the material manager's add and its index's references (the ShapeMaterialHandle), the
//       texture manager's Has / FindOrLoad / Add / GetTexture and its id's references (cTextureHandle), CreateClosest's
//       seven words (the strategy a heap block), the holder's translation and shape, the shape's part and binding, the
//       parent's AddChild / RemoveChild / GetChildren and each child's GetNextSibling. A procedural texture's Redraw
//       notes its strategy's whole state (count, font, every line's colour and text) through the layout, so the pushes
//       and clears of the real strategies (the original's on the original side, ours on ours) are compared. At the end
//       the strategies the textures hold are released (the deleting destructor) and the heap compared.
//
// Stand-ins on both sides: GetDirectorFrameworkObject (0x1ae18d; a framework fake whose GetClassObject hands out the
// pools), the node tag helpers (0xae7900 / 0xae78f0, ours verified; a tag per fake), MakeBillboardQuadMesh (0xaf1420,
// ours verified; its five floats noted, a mesh fake or null) and cRZString::Sprintf (0x1ad6f7: the image's formatting
// reaches CRT state the harness has not set up; the format and the result noted). The texture manager and the material
// manager are fakes in their cache cells (0x10f8f94, 0x10f8fcc); the font system the strategy's constructor asks for
// is a fake behind g_Framework (0x10c9378).
#include "sgfx.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "gz/cIGZFontSys.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "sg/cTextureTextUpdateStrategy.h"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kMaterialClass = 0x49596978, kMaterialIid = 0x49596972;
constexpr uint32_t kProceduralClass = 0x6b81dfe9, kProceduralIid = 0x4b81dfe2;
constexpr uint32_t kHolderClass = 0x65245517, kHolderIid = 0xa516cd10;
constexpr uint32_t kShapeClass = 0xfc6eb1f7, kShapeIid = 0x3c6eb1f0;
constexpr uint32_t kFrameworkCell = 0x10c9378, kTextureManagerCell = 0x10f8f94, kMaterialManagerCell = 0x10f8fcc;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}
#define TABLE(var, name, n, ...)                                                                                       \
    constexpr At var##At[] = {__VA_ARGS__};                                                                            \
    constexpr auto var##Slots = Table<n>(var##At);                                                                     \
    constexpr drawfx::Iface var{name, n, var##Slots.data(), false, drawfx::kTestTable}

TABLE(kDirector, "DirectorFramework", 4, {3, {3, Ret::Int, true, "GetClassObject", "uua"}});
TABLE(kFramework, "FrameWork", 11, {5, {3, Ret::Int, true, "GetSystemService", "uua"}});
TABLE(kFontSys, "FontSys", 24, {11, {5, Ret::Int, true, "FindFont", "Suuub"}});
TABLE(kFont, "Font", 54, {16, {1, Ret::Int, true, "SetQuality", "u"}}, {35, {0, Ret::Int, true, "GetLineSpacing", ""}});
TABLE(kParent, "ParentNode", 47, {42, {1, Ret::Int, true, "GetChildren", "a"}}, {45, {1, Ret::Int, true, "AddChild", "a"}},
      {46, {1, Ret::Int, true, "RemoveChild", "a"}});
TABLE(kChild, "ChildNode", 99, {38, {0, Ret::Int, true, "GetNextSibling", ""}},
      {64, {1, Ret::Int, true, "SetTranslation", "F3"}}, {94, {1, Ret::Int, true, "AddShape", "a"}});
TABLE(kShape, "Shape", 42, {34, {2, Ret::Int, true, "BindMaterial", "ss"}}, {41, {2, Ret::Int, true, "AddPart", "au"}});
TABLE(kMaterial, "Material", 16, {10, {1, Ret::Int, true, "SetMaterialName", "s"}},
      {12, {1, Ret::Int, true, "SetMaterialType", "s"}}, {13, {2, Ret::Int, true, "SetProperty", "ss"}},
      {15, {1, Ret::Int, true, "AddTextureById", "u"}});
TABLE(kMaterialManager, "MaterialManager", 24, {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
      {21, {1, Ret::Int, true, "ReleaseIndex", "u"}}, {23, {2, Ret::Int, true, "AddMaterial", "au"}});
TABLE(kTextureManager, "TextureManager", 26, {17, {1, Ret::Int, true, "FindOrLoad", "s"}},
      {18, {1, Ret::Int, true, "AddRefId", "u"}}, {19, {1, Ret::Int, true, "ReleaseId", "u"}},
      {20, {3, Ret::Int, true, "Add", "asu"}}, {23, {1, Ret::Int, true, "GetTexture", "u"}},
      {25, {1, Ret::Int, true, "Has", "s"}});
TABLE(kProcedural, "ProceduralTexture", 14, {10, {7, Ret::Int, true, "CreateClosest", "auuuuuf"}},
      {12, {0, Ret::Int, true, "Redraw", ""}}, {13, {0, Ret::Int, true, "GetStrategy", ""}});

constexpr unsigned kPool = 14, kOthers = 4, kMaxChildren = 24, kEntries = 16, kNameSize = 200;

struct Parent {
    Fake fake;
    Fake *children[kMaxChildren];
    unsigned count;
};
struct Entry {
    char name[kNameSize];
    Fake *texture;
    uint32_t id;
};
struct World {
    Fake director, framework, fonts, font, textures, materials;
    Parent parents[2];
    Fake others[kOthers];
    const char *otherTags[kOthers];
    Fake holders[kPool], shapes[kPool], materialPool[kPool], procedural[kPool], meshes[kPool], plain[3];
    char holderTags[kPool][kNameSize];
    bool holderTagged[kPool];
    unsigned holdersMade, shapesMade, materialsMade, proceduralMade, meshesMade;
    bool meshFails[kPool];
    Entry entries[kEntries];
    unsigned entryCount;
    uint32_t nextIndex;
} gw;

// The parents' child lists: the children are the "other" fakes and the holders, linked through the list.
Parent *ParentOf(const Fake &f)
{
    for (Parent &p : gw.parents)
        if (&p.fake == &f) return &p;
    return nullptr;
}
uint32_t GetChildren(Fake &self, const uint32_t *a)
{
    Parent *const p = ParentOf(self);
    auto *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    const uint32_t first = p->count ? drawfx::P(p->children[0]) : 0;
    out[0] = first, out[1] = first, out[2] = 0;
    return a[0];
}
uint32_t NextSibling(Fake &self, const uint32_t *)
{
    for (Parent &p : gw.parents)
        for (unsigned i = 0; i < p.count; ++i)
            if (p.children[i] == &self) return i + 1 < p.count ? drawfx::P(p.children[i + 1]) : 0;
    return 0;
}
uint32_t AddChild(Fake &self, const uint32_t *a)
{
    Parent *const p = ParentOf(self);
    Fake *const child = reinterpret_cast<Fake *>(uintptr_t(a[0]));
    if (p->count < kMaxChildren) p->children[p->count++] = child, ++child->refs;
    return 1;
}
uint32_t RemoveChild(Fake &self, const uint32_t *a)
{
    Parent *const p = ParentOf(self);
    Fake *const child = reinterpret_cast<Fake *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < p->count; ++i)
        if (p->children[i] == child) {
            for (unsigned j = i + 1; j < p->count; ++j) p->children[j - 1] = p->children[j];
            --p->count;
            --child->refs;
            return 1;
        }
    return 0;
}

uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *made = nullptr;
    if (a[0] == kMaterialClass && a[1] == kMaterialIid && gw.materialsMade < kPool) made = &gw.materialPool[gw.materialsMade++];
    if (a[0] == kProceduralClass && a[1] == kProceduralIid && gw.proceduralMade < kPool) made = &gw.procedural[gw.proceduralMade++];
    if (a[0] == kHolderClass && a[1] == kHolderIid && gw.holdersMade < kPool) made = &gw.holders[gw.holdersMade++];
    if (a[0] == kShapeClass && a[1] == kShapeIid && gw.shapesMade < kPool) made = &gw.shapes[gw.shapesMade++];
    if (!made) return 0;
    ++made->refs;
    *out = made;
    return 1;
}
void *StandDirector() { return &gw.director; }

uint32_t GetSystemService(Fake &, const uint32_t *a)
{
    if (a[0] != cIGZFontSys::kServiceId || a[1] != cIGZFontSys::kIid) return 0;
    ++gw.fonts.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.fonts;
    return 1;
}
uint32_t FindFont(Fake &, const uint32_t *)
{
    ++gw.font.refs;
    return drawfx::P(&gw.font);
}

// The texture manager's name map.
Entry *Lookup(const char *name)
{
    for (unsigned i = 0; i < gw.entryCount; ++i)
        if (std::strcmp(gw.entries[i].name, name) == 0) return &gw.entries[i];
    return nullptr;
}
Entry *Register(const char *name, Fake *texture)
{
    if (gw.entryCount == kEntries) return nullptr;
    Entry &e = gw.entries[gw.entryCount];
    std::snprintf(e.name, kNameSize, "%s", name);
    e.texture = texture;
    e.id = 0x100 + 3 * gw.entryCount++;
    return &e;
}
uint32_t Has(Fake &, const uint32_t *a) { return Lookup(reinterpret_cast<const char *>(uintptr_t(a[0]))) != nullptr; }
uint32_t FindOrLoad(Fake &, const uint32_t *a)
{
    const Entry *const e = Lookup(reinterpret_cast<const char *>(uintptr_t(a[0])));
    return e ? e->id : 0;
}
uint32_t AddTexture(Fake &, const uint32_t *a)
{
    Fake *const texture = reinterpret_cast<Fake *>(uintptr_t(a[0]));
    const Entry *const e = Register(reinterpret_cast<const char *>(uintptr_t(a[1])), texture);
    if (!e) return 0;
    if (texture) ++texture->refs;
    return e->id;
}
uint32_t GetTexture(Fake &, const uint32_t *a)
{
    for (unsigned i = 0; i < gw.entryCount; ++i)
        if (gw.entries[i].id == a[0]) return drawfx::P(gw.entries[i].texture);
    return 0;
}
uint32_t AddMaterial(Fake &, const uint32_t *) { return gw.nextIndex += 7; }

// The procedural textures: value[0] the strategy CreateClosest was given (held, counted through its own AddRef).
uint32_t CreateClosest(Fake &self, const uint32_t *a)
{
    auto *const strategy = reinterpret_cast<cIGZUnknown *>(uintptr_t(a[0]));
    if (strategy) strategy->AddRef();
    self.value[0] = a[0];
    return 0;
}
uint32_t GetStrategy(Fake &self, const uint32_t *) { return self.value[0]; }

// The strategy's whole state through the layout (the same on both sides).
void NoteStrategy(uint32_t object)
{
    difftest::NotCounted bookkeeping;
    if (!object) {
        drawfx::Note("no strategy", 0u);
        return;
    }
    const auto *const w = reinterpret_cast<const uint32_t *>(uintptr_t(object));
    drawfx::Note("strategy", {w[1], drawfx::Ptr(w[2]), w[3]});
    const uint32_t first = w[4], last = w[5], end = w[6];
    const uint32_t size = (last - first) / 0x24;
    drawfx::Note("lines", {size, (end - first) / 0x24});
    for (uint32_t i = 0; i < size; ++i) {
        const auto *const line = reinterpret_cast<const cTextureTextUpdateStrategy::Line *>(uintptr_t(first) + i * 0x24);
        drawfx::Note("line", {line->mColour, uint32_t(line->mText.mString.size()), line->mText.mRefCount});
        sgfx::NoteText("text", line->mText.mString.c_str());
    }
}
uint32_t Redraw(Fake &self, const uint32_t *)
{
    NoteStrategy(self.value[0]);
    return 0;
}
// The texture's Release: the strategy's count as it stands (so the order of the two Releases shows), and the output
// record a find is filling.
const TextOutput *gOutCell;
uint32_t ProceduralRelease(Fake &self, const uint32_t *)
{
    {
        difftest::NotCounted bookkeeping;
        const auto *const w = reinterpret_cast<const uint32_t *>(uintptr_t(self.value[0]));
        drawfx::Note("texture release, strategy count", w ? w[1] : 0xffffffffu);
        if (gOutCell)
            drawfx::Note("out at release", {drawfx::Norm(drawfx::P(gOutCell->mTexture)), drawfx::Ptr(gOutCell->mStrategy)});
    }
    return uint32_t(--self.refs);
}

// The tag helpers (ours, verified) stood in: the other children answer their seeded tags, the holders the tag set.
const char *StandGetTag(cICompositionTreeNode *node)
{
    const Fake *const f = reinterpret_cast<const Fake *>(node);
    for (unsigned i = 0; i < kOthers; ++i)
        if (f == &gw.others[i]) return gw.otherTags[i];
    for (unsigned i = 0; i < kPool; ++i)
        if (f == &gw.holders[i]) return gw.holderTagged[i] ? gw.holderTags[i] : nullptr;
    drawfx::Note("get tag of an unknown node", drawfx::Norm(drawfx::P(node)));
    return nullptr;
}
void StandSetTag(cICompositionTreeNode *node, const char *tag)
{
    const Fake *const f = reinterpret_cast<const Fake *>(node);
    drawfx::Note("set tag", drawfx::Norm(drawfx::P(node)));
    sgfx::NoteText("tag", tag);
    for (unsigned i = 0; i < kPool; ++i)
        if (f == &gw.holders[i]) {
            std::snprintf(gw.holderTags[i], kNameSize, "%s", tag ? tag : "");
            gw.holderTagged[i] = tag != nullptr;
        }
}

cIGeometryNode **StandQuad(cIGeometryNode **out, float halfSize, float y, float scale, float uScale, float vOffset)
{
    drawfx::Note("quad", {std::bit_cast<uint32_t>(halfSize), std::bit_cast<uint32_t>(y), std::bit_cast<uint32_t>(scale),
                          std::bit_cast<uint32_t>(uScale), std::bit_cast<uint32_t>(vOffset)});
    Fake *mesh = nullptr;
    if (gw.meshesMade < kPool) {
        const unsigned k = gw.meshesMade++;
        if (!gw.meshFails[k]) mesh = &gw.meshes[k], ++mesh->refs;
    }
    *out = reinterpret_cast<cIGeometryNode *>(mesh);
    return out;
}

// cRZString::Sprintf on both sides (the image's formatting reaches CRT state the harness has not set up).
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    {
        difftest::NotCounted bookkeeping;
        sgfx::NoteText("format", fmt);
        sgfx::NoteText("formatted", text);
    }
    self->mString = text;
    return self;
}

constexpr const char *kNames[] = {"", "dbg", "debug", "%s", "%d", "anim",
                                  "a rather long billboard name that goes past the short string buffer by far"};
constexpr const char *kTexts[] = {"", "x", "%s %d", "fifteen chars!!", "sixteen chars!!!",
                                  "a line long enough to need its own heap block, 0123456789"};

const char *TextureName(const Parent &p, const char *name, char (&buf)[kNameSize])
{
    std::snprintf(buf, kNameSize, "@@TextOutput_%d_%s", int32_t(uintptr_t(&p.fake)), name);
    return buf;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 2654435761u + 23u);
    gw.director.Reset<kDirector>(600);
    gw.director.On(3, &Create);
    gw.framework.Reset<kFramework>(601);
    gw.framework.On(5, &GetSystemService);
    gw.fonts.Reset<kFontSys>(602);
    gw.fonts.On(11, &FindFont);
    gw.font.Reset<kFont>(603);
    gw.font.Returns(35, rng() % 3 ? 13u : uint32_t(1 + rng() % 40));
    gw.textures.Reset<kTextureManager>(604);
    gw.textures.On(17, &FindOrLoad);
    gw.textures.On(20, &AddTexture);
    gw.textures.On(23, &GetTexture);
    gw.textures.On(25, &Has);
    gw.materials.Reset<kMaterialManager>(605);
    gw.materials.On(23, &AddMaterial);
    gw.nextIndex = rng() % 50;
    static constexpr const char *kTags[] = {nullptr, "", "other", "@@TextOutput_", "@@TextOutput_dbg",
                                            "@@TextOutput_debug", "@@TextOutput_Debug", "@@TextOutput_dbg "};
    for (unsigned i = 0; i < kOthers; ++i) {
        gw.others[i].Reset<kChild>(610 + i);
        gw.others[i].On(38, &NextSibling);
        gw.otherTags[i] = kTags[rng() % std::size(kTags)];
    }
    for (unsigned k = 0; k < 2; ++k) {
        Parent &p = gw.parents[k];
        p.fake.Reset<kParent>(620 + k);
        p.fake.On(42, &GetChildren);
        p.fake.On(45, &AddChild);
        p.fake.On(46, &RemoveChild);
        p.count = 0;
    }
    for (unsigned i = 0; i < kOthers; ++i) {
        if (rng() % 3 == 0) continue;
        Parent &p = gw.parents[rng() % 2];
        p.children[p.count++] = &gw.others[i];
    }
    for (unsigned k = 0; k < kPool; ++k) {
        gw.holders[k].Reset<kChild>(630 + k);
        gw.holders[k].On(38, &NextSibling);
        gw.holderTagged[k] = false;
        gw.shapes[k].Reset<kShape>(650 + k);
        gw.materialPool[k].Reset<kMaterial>(670 + k);
        gw.procedural[k].Reset<kProcedural>(690 + k);
        gw.procedural[k].On(10, &CreateClosest);
        gw.procedural[k].On(12, &Redraw);
        gw.procedural[k].On(13, &GetStrategy);
        gw.procedural[k].On(2, &ProceduralRelease);
        gw.procedural[k].AddFace(kProceduralIid, gw.procedural[k]);
        gw.meshes[k].Reset<sgfx::kUnknown>(710 + k);
        gw.meshFails[k] = rng() % 8 == 0;
    }
    for (unsigned k = 0; k < 3; ++k) gw.plain[k].Reset<sgfx::kUnknown>(730 + k);
    gw.holdersMade = gw.shapesMade = gw.materialsMade = gw.proceduralMade = gw.meshesMade = 0;
    gw.entryCount = 0;
    // Some names already taken by a texture that is not procedural.
    for (unsigned k = 0; k < 3; ++k) {
        if (rng() % 3) continue;
        char buf[kNameSize];
        Register(TextureName(gw.parents[rng() % 2], kNames[rng() % std::size(kNames)], buf), &gw.plain[k]);
    }
    cells.Set(kFrameworkCell, &gw.framework);
    cells.Set(kTextureManagerCell, &gw.textures);
    cells.Set(kMaterialManagerCell, &gw.materials);
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }
float PickFloat(std::mt19937 &rng)
{
    static constexpr uint32_t kEdges[] = {0x3f800000, 0x3f800000, 0x00000000, 0x80000000, 0x3f000000, 0x7f800000,
                                          0xff800000, 0x7fc00000, 0xffc00001, 0x00000001, 0x3f7fffff, 0x3f800001};
    return rng() % 3 ? std::bit_cast<float>(kEdges[rng() % std::size(kEdges)]) : float(rng() % 1000) / 999.0f;
}

using MakeFn = void(__cdecl *)(cICompositionTreeNode *, const float *, const char *, uint32_t, uint32_t, const float *,
                               uint32_t);
using AddFn = void(__cdecl *)(cICompositionTreeNode *, const char *, const uint32_t *, const char *);
using ClearFn = void(__cdecl *)(cICompositionTreeNode *, const char *);
using FindFn = TextOutput *(__cdecl *)(TextOutput *, cICompositionTreeNode *, const char *);

void Run(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    const unsigned steps = 4 + rng() % 20;
    for (unsigned s = 0; s < steps; ++s) {
        char mark[32];
        std::snprintf(mark, sizeof(mark), "step %u", s);
        sgfx::MarkStep(ctx, mark);
        Parent &p = gw.parents[rng() % 2];
        auto *const node = reinterpret_cast<cICompositionTreeNode *>(&p.fake);
        const char *const name = kNames[rng() % std::size(kNames)];
        const unsigned op = rng() % 9;
        drawfx::Note("op", {op, p.fake.id});
        switch (op) {
        case 0:
        case 1: {
            if (gw.holdersMade == kPool || gw.proceduralMade == kPool || gw.entryCount == kEntries) break;
            const float translation[3] = {PickFloat(rng), -PickFloat(rng), 2.5f};
            float colour[4];
            for (float &c : colour) c = PickFloat(rng);
            static constexpr uint32_t kWidths[] = {0, 1, 64, 128, 256, 300};
            static constexpr uint32_t kHeights[] = {0, 1, 16, 64, 100, 255};
            const uint32_t width = kWidths[rng() % std::size(kWidths)], height = kHeights[rng() % std::size(kHeights)];
            const uint32_t fontSize = 8 + rng() % 5;
            drawfx::Note("make", {width, height, fontSize, Bits(colour[0]), Bits(colour[1]), Bits(colour[2]), Bits(colour[3])});
            if (original) ctx.Original<MakeFn>(0xaf3860)(node, translation, name, width, height, colour, fontSize);
            else MakeBillboardTextOutput(node, translation, name, width, height, colour, fontSize);
            break;
        }
        case 2:
        case 3:
        case 4: {
            const unsigned n = 1 + (rng() % 4 ? 0 : rng() % 30);
            for (unsigned i = 0; i < n; ++i) {
                static constexpr uint32_t kColours[] = {0, 0xffffffff, 0x00123456, 0x80ff0000, 0xffffff00};
                const uint32_t colour = rng() % 3 ? kColours[rng() % std::size(kColours)] : uint32_t(rng());
                const char *const text = kTexts[rng() % std::size(kTexts)];
                if (original) ctx.Original<AddFn>(0xaf4250)(node, name, &colour, text);
                else AddNodeTextLabel(node, name, &colour, text);
            }
            break;
        }
        case 5:
            if (original) ctx.Original<ClearFn>(0xaf42a0)(node, name);
            else ClearNodeTextLabels(node, name);
            break;
        case 6:
            if (original) ctx.Original<ClearFn>(0xaf4120)(node, name);
            else RemoveBillboardTextOutput(node, name);
            break;
        default: {
            TextOutput out{reinterpret_cast<cIProceduralTexture *>(uintptr_t(0x11111111)),
                           reinterpret_cast<cTextureTextUpdateStrategy *>(uintptr_t(0x22222222))};
            gOutCell = &out;
            TextOutput *const got = original ? ctx.Original<FindFn>(0xaf42e0)(&out, node, name)
                                             : FindNodeTextOutput(&out, node, name);
            gOutCell = nullptr;
            drawfx::Note("found", {uint32_t(got == &out), drawfx::Norm(drawfx::P(out.mTexture)), drawfx::Ptr(out.mStrategy)});
            break;
        }
        }
        drawfx::Note("children", p.count);
        for (unsigned i = 0; i < p.count; ++i) drawfx::Note("child", {p.children[i]->id, uint32_t(p.children[i]->refs)});
    }
    sgfx::MarkStep(ctx, "end");
    // The strategies the textures hold, released (the last reference: the deleting destructor).
    for (unsigned k = 0; k < gw.proceduralMade; ++k) {
        auto *const strategy = reinterpret_cast<cIGZUnknown *>(uintptr_t(gw.procedural[k].value[0]));
        if (strategy) drawfx::Note("strategy release", strategy->Release());
        gw.procedural[k].value[0] = 0;
    }
    for (unsigned k = 0; k < kPool; ++k)
        drawfx::Note("pool refs", {uint32_t(gw.holders[k].refs), uint32_t(gw.shapes[k].refs),
                                   uint32_t(gw.materialPool[k].refs), uint32_t(gw.procedural[k].refs),
                                   uint32_t(gw.meshes[k].refs)});
    drawfx::Note("made", {gw.holdersMade, gw.shapesMade, gw.materialsMade, gw.proceduralMade, gw.meshesMade});
    drawfx::Note("font refs", {uint32_t(gw.fonts.refs), uint32_t(gw.font.refs), uint32_t(gw.plain[0].refs),
                               uint32_t(gw.plain[1].refs), uint32_t(gw.plain[2].refs)});
    for (unsigned i = 0; i < gw.entryCount; ++i) sgfx::NoteText("texture name", gw.entries[i].name);
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: 0xaf3860 (MakeBillboardTextOutput), 0xaf4250 (AddNodeTextLabel), 0xaf42a0 (ClearNodeTextLabels), 0xaf4120
// (RemoveBillboardTextOutput), 0xaf42e0 (FindNodeTextOutput).
DIFF_TEST(SG_Utilities_TextBillboards, 0xaf3860)
{
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn getTag{0xae7900, &GetNodeTag, &StandGetTag};
    shape::StandIn setTag{0xae78f0, &SetNodeTag, &StandSetTag};
    shape::StandIn quad{0xaf1420, &MakeBillboardQuadMesh, &StandQuad};
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    sgfx::CheckSides(
        ctx, "text billboards", 1200, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            Run(ctx, original, rng);
            ++ctx.cases;
        });
}
