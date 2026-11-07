// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KB: slot 84 (ExportFaceObj, 0xceac20). The text file's
// Open and Close (cRZTextFile::Open 0x1c401a, cRZFile::Close 0x1c0faa: stood in on both sides) and its formatted write
// (0x1c4396), the part entry (0xcf04b0, 0xcfbbb0, 0xce7430), the system's slot 52, the part, the texture service
// (0xae8750), the name tables (GetCachedQueriedObject 0xae8900, stood in on both sides), the texture, its image and the
// image service (0x18d120) are recording fakes; the lines written and the calls are compared. The file's own
// constructor and destructor run for real on both sides.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "gz/GraphicServices.h"
#include "sg/cTextureManager.h"
#include "misc/UtilitiesE.h"
#include "rz/cRZTextFile.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <set>

namespace bpt_faceexport {

using namespace bpt;
using Vt = std::array<const void *, 60>;
using nTSCASCore::PartMeshEntry;
using nTSCASCore::PartMeshFace;
using nTSCASCore::PartMeshFaceGroup;
using nTSCASCore::PartMeshGroup;
using nTSCASCore::PartMeshVertex;

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

uint32_t gScriptSeed;
uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

struct Obj {
    const void *const *vt;
    uint32_t id;
};
Vt gPartVt, gTexturesVt, gNamesVt, gPropsVt, gTextureVt, gLevelVt, gImageVt, gImageServiceVt;
Obj gPart, gTextures, gNames, gProps, gTexture, gLevel, gImage, gImageService;

// The odds (one time in n) each step of the texture chain gives nothing.
struct Script {
    int openFails = 6, partNull = 4, faceNull = 6, nameNull = 6, noTexture = 6, textureNull = 6, levelNull = 6, qiFails = 5,
        unlocked = 5;
} gScript;

// The mesh of the case and the indices its corners use (the lines of other indices print the original's unwritten
// heap bytes: their values are not compared).
PartMeshEntry gEntry;
std::vector<std::vector<PartMeshVertex>> gVertices;
std::vector<std::vector<PartMeshFace>> gFaces;
std::vector<PartMeshGroup> gGroups;
std::vector<PartMeshFaceGroup> gFaceGroups;
std::set<uint32_t> gUsedPositions, gUsedNormals;
uint32_t gPositionLine, gNormalLine; // the v / vn lines written so far on this side
std::vector<std::string> gLines;      // the lines written on this side (for the failure message)

// ---- the file --------------------------------------------------------------------------------------------------------

uint32_t __fastcall StandOpen(cRZTextFile *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    gEvents.push_back({0x1c401a, TextHash(self->GetPathCStr()), access, creation, sharing});
    return Pick() % gScript.openFails == 0 ? 0x4400u : 0x4401u; // the answer is al
}
uint32_t __fastcall StandClose(cRZFile *self, void *)
{
    gEvents.push_back({0x1c0faa, TextHash(self->GetPathCStr())});
    return 1;
}
uint8_t __cdecl StandWrite(cRZTextFile *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (fmt == GameAddress<const char>(0xeba6dc) && !gUsedPositions.count(gPositionLine++)) std::snprintf(text, sizeof(text), "v gap");
    if (fmt == GameAddress<const char>(0xeba72c) && !gUsedNormals.count(gNormalLine++)) std::snprintf(text, sizeof(text), "vn gap");
    gEvents.push_back({0x1c4396, TextHash(self->GetPathCStr()), TextHash(text)});
    gLines.push_back(text);
    return 1;
}

// ---- the entry ---------------------------------------------------------------------------------------------------------

PartMeshEntry *__fastcall F_cf04b0(void *self, void *, uint32_t kind, uint32_t key, uint32_t id, uint32_t partId)
{
    gEvents.push_back({0xcf04b0, Rel(self), kind, key, id, partId});
    return &gEntry;
}
void __fastcall F_cfbbb0(void *self, void *, PartMeshEntry *entry) { gEvents.push_back({0xcfbbb0, Rel(self), entry == &gEntry}); }
void __fastcall F_ce7430(void *self, void *, PartMeshEntry *entry) { gEvents.push_back({0xce7430, Rel(self), entry == &gEntry}); }

// ---- the part and the texture chain ----------------------------------------------------------------------------------

void *__fastcall Slot52(void *self, void *, uint32_t kind, uint32_t key, uint32_t id)
{
    gEvents.push_back({52, Rel(self), kind, key, id});
    return Pick() % gScript.partNull == 0 ? nullptr : &gPart;
}
uint32_t __fastcall AddRef(Obj *self, void *)
{
    gEvents.push_back({0xa001, self->id});
    return 2;
}
uint32_t __fastcall Release(Obj *self, void *)
{
    gEvents.push_back({0xa002, self->id});
    return 1;
}
uint32_t __fastcall PartFaceId(Obj *self, void *, const char *name)
{
    gEvents.push_back({0x6035, self->id, TextHash(name)});
    return Pick() % gScript.faceNull == 0 ? 0u : 0x1000u | (Pick() & 0xff);
}
uint32_t __fastcall NamesIndex(Obj *self, void *, uint32_t faceId)
{
    gEvents.push_back({0x4e13, self->id, faceId});
    return Pick() & 0xffff;
}
struct NameEntry {
    uint32_t id;
    void *properties;
    uint32_t rest[3];
};
NameEntry *__fastcall NamesEntry(Obj *self, void *, NameEntry *out, uint32_t index)
{
    gEvents.push_back({0x4e21, self->id, index});
    *out = {index + 1, &gProps, {Pick(), Pick(), Pick()}}; // every byte slot 33 writes
    return out;
}
const char *__fastcall PropsText(Obj *self, void *, const char *name)
{
    gEvents.push_back({0x5017, self->id, TextHash(name)});
    static const char *const kNames[] = {"tex_a", "", "a_long_texture_name_over_fifteen"};
    return Pick() % gScript.nameNull == 0 ? nullptr : kNames[Pick() % 3];
}
uint32_t __fastcall TexturesHas(Obj *self, void *, const char *name)
{
    gEvents.push_back({0x7019, self->id, TextHash(name)});
    return Pick() % gScript.noTexture == 0 ? 0x3300u : 0x3301u; // the answer is al
}
uint32_t __fastcall TexturesId(Obj *self, void *, const char *name)
{
    gEvents.push_back({0x7011, self->id, TextHash(name)});
    return Pick();
}
void *__fastcall TexturesGet(Obj *self, void *, uint32_t id)
{
    gEvents.push_back({0x7017, self->id, id});
    return Pick() % gScript.textureNull == 0 ? nullptr : &gTexture;
}
void *__fastcall TextureLevel(Obj *self, void *, uint32_t level)
{
    gEvents.push_back({0x7408, self->id, level});
    return Pick() % gScript.levelNull == 0 ? nullptr : &gLevel;
}
uint32_t __fastcall LevelQuery(Obj *self, void *, uint32_t iid, void **out)
{
    gEvents.push_back({0x7500, self->id, iid, *out == nullptr});
    const uint32_t p = Pick();
    if (p % gScript.qiFails == 0) {
        if (p & 0x100) *out = &gImage; // a failed query that still hands out the image: released all the same
        return 0x2200u;
    }
    *out = &gImage;
    return 0x2201u;
}
uint32_t __fastcall ImageLock(Obj *self, void *, uint32_t a)
{
    gEvents.push_back({0x761c, self->id, a});
    return Pick() % gScript.unlocked == 0 ? 0u : 0x100u; // tested as a whole word
}
void *__fastcall ImagePixels(Obj *self, void *, uint32_t a, uint32_t b)
{
    gEvents.push_back({0x761a, self->id, a, b});
    return reinterpret_cast<void *>(static_cast<uintptr_t>(0x5000 + (Pick() & 0xff)));
}
uint32_t __fastcall ImageSave(Obj *self, void *, const char *name, void *pixels)
{
    gEvents.push_back({0x780b, self->id, TextHash(name), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pixels))});
    return 1;
}
void *__cdecl F_ae8750()
{
    gEvents.push_back({0xae8750});
    return &gTextures;
}
void *__cdecl StandNames()
{
    gEvents.push_back({0xae8900});
    return &gNames;
}
void *__cdecl F_18d120()
{
    gEvents.push_back({0x18d120});
    return &gImageService;
}

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

const float kCoords[] = {0.0f, 0.001f, 0.0009f, 0.0011f, 1.0f, -0.5f, std::numeric_limits<float>::quiet_NaN(), 0.25f};

// 0-3 vertex groups of 1-6 vertices; as many face groups or fewer, 0-4 faces each; position and normal indices 0-7 (so
// gaps, repeats and a largest index) or 0xffff; texture coordinates that repeat, differ by about 1e-3 (a squared
// distance at the 1e-6 limit) or are NaN.
void BuildMesh(std::mt19937 &rng)
{
    const size_t groups = rng() % 4;
    gVertices.assign(groups, {});
    gFaces.assign(groups, {});
    gUsedPositions.clear();
    gUsedNormals.clear();
    for (size_t g = 0; g < groups; ++g) {
        gVertices[g].resize(1 + rng() % 6);
        for (PartMeshVertex &v : gVertices[g]) {
            for (size_t b = 0; b < sizeof(v); ++b) reinterpret_cast<uint8_t *>(&v)[b] = static_cast<uint8_t>(rng());
            for (float &f : v.mPosition) f = static_cast<float>(static_cast<int>(rng() % 200) - 100) / 8.0f;
            for (float &f : v.mNormal) f = static_cast<float>(static_cast<int>(rng() % 20) - 10) / 4.0f;
            for (float &f : v.mUv) f = kCoords[rng() % 8];
            v.mPositionIndex = static_cast<uint16_t>(rng() % 40 == 0 ? 0x1ff : rng() % 8);
            v.mNormalIndex = static_cast<uint16_t>(rng() % 8);
        }
    }
    const size_t faceGroups = groups && rng() % 4 == 0 ? rng() % groups : groups;
    for (size_t g = 0; g < faceGroups; ++g) {
        gFaces[g].resize(rng() % 5);
        for (PartMeshFace &f : gFaces[g]) {
            for (size_t b = 0; b < sizeof(f); ++b) reinterpret_cast<uint8_t *>(&f)[b] = static_cast<uint8_t>(rng());
            for (uint16_t &c : f.mCorners) {
                c = static_cast<uint16_t>(rng() % gVertices[g].size());
                gUsedPositions.insert(gVertices[g][c].mPositionIndex);
                gUsedNormals.insert(gVertices[g][c].mNormalIndex);
            }
        }
    }
    gGroups.clear();
    for (auto &v : gVertices) gGroups.push_back({v.data(), v.data() + v.size(), v.data() + v.size()});
    gFaceGroups.clear();
    for (size_t g = 0; g < faceGroups; ++g) gFaceGroups.push_back({gFaces[g].data(), gFaces[g].data() + gFaces[g].size(), gFaces[g].data() + gFaces[g].size()});
    std::memset(&gEntry, 0xcd, sizeof(gEntry));
    gEntry.mGroupsFirst = gGroups.data();
    gEntry.mGroupsLast = gGroups.data() + gGroups.size();
    gEntry.mFaceGroupsFirst = gFaceGroups.data();
    gEntry.mFaceGroupsLast = gFaceGroups.data() + gFaceGroups.size();
}

} // namespace bpt_faceexport

// Paths empty, short and long; ages 0 and non-zero; meshes as BuildMesh (no groups, no faces, fewer face groups,
// indices with gaps and one far index, coordinates at the 1e-6 limit and NaN); every step of the texture chain giving
// nothing in turn (the part, the face id, the texture name, the texture, its level, the image query, also failing with
// an image handed out, the image's lock) and the whole chain through to the save.
DIFF_TEST(cTSCASBodyPartSystem_ExportFaceObj, 0xceac20)
{
    using namespace bpt_faceexport;
    std::mt19937 rng(0xceac20);
    auto orig = ctx.Original<bool(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(0xceac20);
    VTable vt = CopyVTable(ctx);
    vt.slots[52] = reinterpret_cast<const void *>(&Slot52);
    Env env(ctx);
    Redirect r1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect r2(ctx, 0xcfbbb0, reinterpret_cast<const void *>(&F_cfbbb0));
    Redirect r3(ctx, 0xce7430, reinterpret_cast<const void *>(&F_ce7430));
    Redirect r4(ctx, 0x1c4396, reinterpret_cast<const void *>(&StandWrite));
    shape::StandIn r5(0xae8750, &nGZSceneGraph::TextureManager, &F_ae8750); // the image's callee and ours
    Redirect r6(ctx, 0x18d120, reinterpret_cast<const void *>(&F_18d120));
    shape::CodePatch r6Ours(shape::CodeOf(&nGZGraphic4::GetImageManipCached), shape::CodeOf(&F_18d120)); // ours is the one called
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    shape::StandIn open(0x1c401a, &cRZTextFile::Open, &StandOpen);
    shape::StandIn close(0x1c0faa, &cRZFile::Close, &StandClose);
    shape::StandIn names(0xae8900, &GetCachedQueriedObject, &StandNames);
    auto set = [](Vt &v, Obj &o, uint32_t id) {
        v = {};
        v[1] = reinterpret_cast<const void *>(&AddRef);
        v[2] = reinterpret_cast<const void *>(&Release);
        o = {v.data(), id};
    };
    set(gPartVt, gPart, 1);
    gPartVt[53] = reinterpret_cast<const void *>(&PartFaceId);
    set(gTexturesVt, gTextures, 2);
    gTexturesVt[25] = reinterpret_cast<const void *>(&TexturesHas);
    gTexturesVt[17] = reinterpret_cast<const void *>(&TexturesId);
    gTexturesVt[23] = reinterpret_cast<const void *>(&TexturesGet);
    set(gNamesVt, gNames, 3);
    gNamesVt[19] = reinterpret_cast<const void *>(&NamesIndex);
    gNamesVt[33] = reinterpret_cast<const void *>(&NamesEntry);
    set(gPropsVt, gProps, 4);
    gPropsVt[23] = reinterpret_cast<const void *>(&PropsText);
    set(gTextureVt, gTexture, 5);
    gTextureVt[8] = reinterpret_cast<const void *>(&TextureLevel);
    set(gLevelVt, gLevel, 6);
    gLevelVt[0] = reinterpret_cast<const void *>(&LevelQuery);
    set(gImageVt, gImage, 7);
    gImageVt[28] = reinterpret_cast<const void *>(&ImageLock);
    gImageVt[26] = reinterpret_cast<const void *>(&ImagePixels);
    set(gImageServiceVt, gImageService, 8);
    gImageServiceVt[11] = reinterpret_cast<const void *>(&ImageSave);
    static const char *const kPaths[] = {"C:\\out\\face", "", "a_path_long_enough_for_the_heap"};
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const bool chain = rng() % 3 == 0; // every step of the texture chain succeeds
        gScript = chain ? Script{1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000} : Script{};
        BuildMesh(rng);
        const uint32_t age = rng() % 2 == 0 ? 0u : 1u + static_cast<uint32_t>(rng() % 3);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        const char *const path = kPaths[rng() % 3];
        const uint32_t kind = static_cast<uint32_t>(rng() % 9), key = static_cast<uint32_t>(rng() % 3);
        std::vector<std::string> lines[2];
        Compare(ctx, "ExportFaceObj", sc, vt, [&](World &w, bool original) -> uint64_t {
            gPositionLine = gNormalLine = 0;
            gLines.clear();
            const bool written = original ? orig(w.S(), path, kind, key) : w.S()->ExportFaceObj(path, kind, key);
            lines[original ? 0 : 1] = gLines;
            return written ? 1u : 0u;
        });
        for (size_t k = 0; k < lines[0].size() && k < lines[1].size(); ++k) {
            if (lines[0][k] != lines[1][k]) {
                ctx.Fail("ExportFaceObj: line %zu: original '%s', ours '%s'", k, lines[0][k].c_str(), lines[1][k].c_str());                break;
            }
        }
    }
}
