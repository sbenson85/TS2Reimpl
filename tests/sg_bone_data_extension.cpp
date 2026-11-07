// Differential tests of nGZSceneGraph::cBoneDataExtension (lane QP, scenegraph D3 part 1; src/sg/cBoneDataExtension.h).
//
// Each side's extension comes from its own factory: the game's 0xa98470 (new 0x4c, the constructor 0xa980a0) and ours
// Create; each is driven through its own vtables.
//
//   BoneDataExtension_Slots      random calls through the extension's three faces: the cIBoneDataExtension face (every
//                                slot: the axis codes over -1..6, the scale over 1, NaN, 0 and others, the quaternion;
//                                each getter), cIExtension (QueryInterface over the iids, IsKindOf, the type name,
//                                GetDescription into a text fake's FromChar, Clone with the copy dumped then released,
//                                cExtension's part through the class's own thunks: owner, next, IsPersistent, the
//                                referent) and the referent face (its type-info record, GetOwnerInfo, Write through a
//                                tape writer). The values dumped through the face's getters every 8 calls and at the end.
//   BoneDataExtension_ReadWrite  a random extension written to a tape (its record, cExtension's, the values), the record
//                                version on the tape set to 3, 4 or 5, a fresh extension of the same side with other
//                                values reading that tape (Read 0xa98560: version 3 keeps its own quaternion), then dumped
//                                and written again: the tapes and dumps compared between the sides.
//
// RVAs these tests reach (status entries cite them): the factory 0xa98470, the constructors 0xa980a0 / 0xa97ff0, the
// destructor 0xa98160 and its deleting destructor 0xa981b0, Clone 0xa981f0, QueryInterface 0xa98510, Read 0xa98560,
// GetTypeInfo 0xa98670, Write 0xa98740, IsKindOf 0xa98650, the type name 0xa98460, GetDescription 0xa98270, the face's
// SetQuaternion 0xa98630 (its other slots are folded accessors), and the vtordisp thunks of the vtables 0xe74f88 /
// 0xe74ff8 the game side calls through: QueryInterface 0xa98501, AddRef 0xa981d0, Release 0xa98614, Clone 0xa981db,
// IsKindOf 0xa9863e, IsPersistent 0xa984f6, the type name 0xa98454, GetDescription 0xa9825e, SetQuaternion 0xa9861f.
#include "difftest.h"
#include "sgfx.h"
#include "test_helpers.h"

#include "rz/cRZString.h"
#include "sg/cBoneDataExtension.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFactory = 0xa98470;
constexpr uint32_t kObjectTag = 0x7b000000;
using FactoryFn = cIGZUnknown *(__cdecl *)();

difftest::Context *gCtx;

// ---- a tape writer and reader (the persist writer's and reader's slots the extension calls) ----
struct Token {
    char kind; // 'w' a word, 'a' a float array, 'i' a type-info record
    std::vector<uint32_t> words;
    std::string text;
};
std::vector<Token> gTape;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float FloatOf(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
std::string TextOf(const void *str) { return str ? reinterpret_cast<const cRZString *>(str)->ToChar() : std::string("<null>"); }

void Put(const char *kind, Token t)
{
    drawfx::Call c;
    c.note = kind;
    c.words = t.words;
    c.text = t.text;
    drawfx::gLog.push_back(std::move(c));
    difftest::NotCounted quiet;
    gTape.push_back(std::move(t));
}

#define SGBDE_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGBDE_BAD(0) SGBDE_BAD(1) SGBDE_BAD(2) SGBDE_BAD(3) SGBDE_BAD(4) SGBDE_BAD(5) SGBDE_BAD(6) SGBDE_BAD(7) SGBDE_BAD(8)
    virtual uint32_t SetSint32(int32_t v) { Put("w SetSint32", {'w', {uint32_t(v)}, {}}); return 1; } // 9
    SGBDE_BAD(10)
    virtual uint32_t SetFloat32(float v) { Put("w SetFloat32", {'w', {Bits(v)}, {}}); return 1; } // 11
    SGBDE_BAD(12) SGBDE_BAD(13) SGBDE_BAD(14) SGBDE_BAD(15) SGBDE_BAD(16) SGBDE_BAD(17) SGBDE_BAD(18) SGBDE_BAD(19)
    virtual uint32_t SetFloat32Array(const float *data, uint32_t count) // 20
    {
        Token t{'a', {}, {}};
        for (uint32_t i = 0; i < count; ++i) t.words.push_back(Bits(data[i]));
        Put("w SetFloat32Array", std::move(t));
        return 1;
    }
    SGBDE_BAD(21) SGBDE_BAD(22) SGBDE_BAD(23) SGBDE_BAD(24) SGBDE_BAD(25) SGBDE_BAD(26) SGBDE_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

// The reader answers from gTape in order, one token a call whatever its kind. ReadTypeInfo answers a name fake, counted
// (each noted at the end).
Fake gNames[4];
unsigned gNameAt;
struct TapeReader {
    size_t next = 0;
    const Token *Take(const char *kind)
    {
        drawfx::Note(kind, uint32_t(next));
        return next < gTape.size() ? &gTape[next++] : nullptr;
    }
    static uint32_t WordOf(const Token *t) { return t && !t->words.empty() ? t->words[0] : 0; }
    SGBDE_BAD(0) SGBDE_BAD(1) SGBDE_BAD(2) SGBDE_BAD(3) SGBDE_BAD(4) SGBDE_BAD(5) SGBDE_BAD(6) SGBDE_BAD(7) SGBDE_BAD(8)
    virtual int32_t GetSint32() { return int32_t(WordOf(Take("r GetSint32"))); } // 9
    SGBDE_BAD(10)
    virtual float GetFloat32() { return FloatOf(WordOf(Take("r GetFloat32"))); } // 11
    SGBDE_BAD(12) SGBDE_BAD(13) SGBDE_BAD(14) SGBDE_BAD(15) SGBDE_BAD(16) SGBDE_BAD(17) SGBDE_BAD(18) SGBDE_BAD(19)
    virtual void GetFloat32Array(float *data, uint32_t count) // 20
    {
        const Token *t = Take("r GetFloat32Array");
        drawfx::Note("array count", count);
        for (uint32_t i = 0; i < count; ++i) data[i] = FloatOf(t && i < t->words.size() ? t->words[i] : 0x3f800000u + i);
    }
    SGBDE_BAD(21) SGBDE_BAD(22) SGBDE_BAD(23) SGBDE_BAD(24) SGBDE_BAD(25)
    virtual bool ReadTypeInfo(cPersistTypeInfo *out) // 26
    {
        const Token *t = Take("r ReadTypeInfo");
        Fake &name = gNames[gNameAt++ % 4];
        ++name.refs;
        out->mName = reinterpret_cast<cIGZUnknown *>(&name);
        out->mWord4 = t && t->words.size() > 0 ? t->words[0] : 0;
        out->mVersion = t && t->words.size() > 1 ? t->words[1] : 0;
        return t != nullptr;
    }
};
TapeReader gReader;

// ---- a cIGZString as GetDescription uses it (FromChar, slot 4) ----
inline constexpr drawfx::Slot kTextSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {2, drawfx::Ret::Int, false, "", ""},
    {1, drawfx::Ret::Int, true, "FromChar", "s"},        {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {3, drawfx::Ret::Int, false, "", ""},
    {2, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, false, "", ""},
    {2, drawfx::Ret::Int, false, "", ""},                {3, drawfx::Ret::Int, false, "", ""},
    {2, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, false, "", ""},
    {1, drawfx::Ret::Int, false, "", ""},
};
inline constexpr drawfx::Iface kText{"Text", 17, kTextSlots};
uint32_t TextFromChar(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("from char", reinterpret_cast<const char *>(uintptr_t(a[0])));
    return P(&self);
}

// ---- the world ----
struct World {
    Fake text;     // id 330
    Fake owner;    // id 300: a node whose referent face is `ownerRef`
    Fake ownerRef; // id 310: its GetOwnerInfo (6) answers 0x600 + seed
    Fake next[3];  // ids 320-322: extensions for the node's list
};
World w;

void BuildWorld(uint32_t seed, drawfx::Cells &)
{
    w.text.Reset<kText>(330);
    w.text.On(4, &TextFromChar);
    for (unsigned i = 0; i < 4; ++i) gNames[i].Reset<sgfx::kUnknown>(440 + i);
    gNameAt = 0;
    gReader.next = 0;
    {
        difftest::NotCounted quiet;
        gTape.clear();
    }
    sgfx::MakeNode(w.owner, 300);
    w.ownerRef.Reset<sgfx::kReferent>(310);
    w.ownerRef.Returns(6, 0x600u + seed);
    sgfx::SetWord(w.owner, sgfx::kReferentWord, &w.ownerRef);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.next[i], 320 + i);
}

void NoteWorld()
{
    for (const Fake &e : w.next) drawfx::Note("next refs", {e.id, uint32_t(e.refs)});
    drawfx::Note("owner refs", uint32_t(w.owner.refs));
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

// One extension of the side being run, as its three faces (the test holds two references: one for the extension and
// one the face's query gave).
struct Extension {
    cIExtension *base = nullptr;
    cIBoneDataExtension *bone = nullptr;
    cIGZPersistSerializableReferent *referent = nullptr;
};

Extension Make(bool original)
{
    Extension e;
    cIGZUnknown *const made = original ? gCtx->Original<FactoryFn>(kFactory)() : cBoneDataExtension::Create();
    e.base = static_cast<cIExtension *>(made);
    // The object: cIExtension at +0x3c in the game's layout (ours the same, or the normalised pointers below differ).
    drawfx::AddRange(reinterpret_cast<uint8_t *>(e.base) - 0x3c, 0x4c, kObjectTag);
    drawfx::Note("first count", e.base->AddRef());
    void *face = nullptr;
    drawfx::Note("query bone face", {uint8_t(e.base->QueryInterface(cIBoneDataExtension::kIid, &face)), drawfx::Norm(P(face))});
    e.bone = static_cast<cIBoneDataExtension *>(face);
    e.referent = e.base->GetPersistReferent();
    drawfx::Note("referent", drawfx::Norm(P(e.referent)));
    return e;
}

// Every reference the test holds dropped through the base (the face's own slots 0-2 are setters), each answer noted.
void Drop(const Extension &e)
{
    drawfx::Note("face release", e.base->Release());
    drawfx::Note("last release", e.base->Release());
}

void Dump(cIBoneDataExtension *bone)
{
    const float *const q = bone->GetQuaternion();
    drawfx::Note("values", {uint32_t(bone->GetAxisH()), uint32_t(bone->GetAxisM()), uint32_t(bone->GetAxisS()),
                            Bits(bone->GetScale()), Bits(q[0]), Bits(q[1]), Bits(q[2]), Bits(q[3])});
}

// ---- the operations ----
const uint32_t kIids[] = {cIBoneDataExtension::kIid, cIExtension::kIid, 0x5b0aaa74, 0xbb11a53e, 0x3b253dc7, 0xfb859324, 1,
                          0x12345678};

float Scale(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0:
    case 1: return 1.0f;
    case 2: return std::numeric_limits<float>::quiet_NaN();
    case 3: return 0.0f;
    case 4: return 1.0000001f;
    case 5: return -2.125f;
    case 6: return 12345.678f;
    default: return float(int32_t(rng() % 4001) - 2000) / 8.0f;
    }
}
int32_t Axis(std::mt19937 &rng) { return int32_t(rng() % 9) - 2; } // -2 .. 6

void Operate(const Extension &e, unsigned op, std::mt19937 &rng)
{
    cIBoneDataExtension *const bone = e.bone;
    switch (op) {
    case 0: bone->SetAxisH(Axis(rng)); break;
    case 1: bone->SetAxisM(Axis(rng)); break;
    case 2: bone->SetAxisS(Axis(rng)); break;
    case 3: bone->SetScale(Scale(rng)); break;
    case 4: {
        const float v[4] = {Scale(rng), Scale(rng), Scale(rng), Scale(rng)};
        bone->SetQuaternion(v);
        break;
    }
    case 5: drawfx::Note("h", uint32_t(bone->GetAxisH())); break;
    case 6: drawfx::Note("m", uint32_t(bone->GetAxisM())); break;
    case 7: drawfx::Note("s", uint32_t(bone->GetAxisS())); break;
    case 8: drawfx::Note("scale", Bits(bone->GetScale())); break;
    case 9: drawfx::Note("quaternion at", drawfx::Norm(P(bone->GetQuaternion()))); break;
    case 10: { // QueryInterface through either face
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = rng() % 2 ? e.base->QueryInterface(iid, rng() % 8 ? &out : nullptr)
                                 : e.referent->QueryInterface(iid, rng() % 8 ? &out : nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out && iid == cIBoneDataExtension::kIid) // its own slots 0-2 are setters: released through the base
            e.base->Release();
        else if (out && iid != 0xfb859324)
            static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 11: {
        const uint32_t iid = kIids[rng() % 8];
        drawfx::Note("kind of", {iid, uint8_t(e.base->IsKindOf(iid))});
        break;
    }
    case 12:
        sgfx::NoteText("type name", e.base->GetTypeName());
        drawfx::Note("persistent", uint8_t(e.base->IsPersistent()));
        break;
    case 13:
    case 14: e.base->GetDescription(reinterpret_cast<cIGZString *>(&w.text)); break;
    case 15: { // Clone: the copy (count 0) counted, dumped, then released (destroyed)
        cIExtension *const copy = e.base->Clone();
        drawfx::Note("clone count", copy->AddRef());
        void *face = nullptr;
        drawfx::Note("clone query", uint8_t(copy->QueryInterface(cIBoneDataExtension::kIid, &face)));
        Dump(static_cast<cIBoneDataExtension *>(face));
        copy->GetDescription(reinterpret_cast<cIGZString *>(&w.text));
        drawfx::Note("clone face release", copy->Release());
        drawfx::Note("clone release", copy->Release());
        break;
    }
    case 16: {
        cPersistTypeInfo info = e.referent->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 17: e.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter)); break;
    case 18: {
        cIObjectGraphNode *owner = rng() % 3 ? reinterpret_cast<cIObjectGraphNode *>(&w.owner) : nullptr;
        drawfx::Note("set owner", drawfx::Norm(P(e.base->SetOwner(owner))));
        drawfx::Note("owner", {drawfx::Norm(P(e.base->GetOwner())), drawfx::Norm(P(e.base->GetOwnerConst())),
                               drawfx::Norm(e.referent->GetOwnerInfo())});
        break;
    }
    default: { // 19
        const unsigned k = rng() % 4;
        cIExtension *const next = k < 3 ? reinterpret_cast<cIExtension *>(&w.next[k]) : nullptr;
        const bool changes = next != e.base->GetNext();
        const uint32_t r = e.base->SetNext(next);
        drawfx::Note("set next", {uint32_t(changes), changes ? r : 0});
        drawfx::Note("next", {drawfx::Norm(P(e.base->GetNext())), drawfx::Norm(P(e.base->GetNextConst()))});
        break;
    }
    }
}

void Randomise(cIBoneDataExtension *bone, std::mt19937 &rng)
{
    bone->SetAxisH(Axis(rng));
    bone->SetAxisM(Axis(rng));
    bone->SetAxisS(Axis(rng));
    bone->SetScale(Scale(rng));
    const float v[4] = {Scale(rng), Scale(rng), Scale(rng), Scale(rng)};
    bone->SetQuaternion(v);
}

} // namespace

// Random calls through the three faces of each side's extension.
DIFF_TEST(BoneDataExtension_Slots, 0xa98470)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "slots", 64, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const Extension e = Make(original);
        Dump(e.bone);
        e.base->GetDescription(reinterpret_cast<cIGZString *>(&w.text));
        for (int k = 0; k < 40; ++k) {
            const unsigned op = rng() % 20;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(e, op, rng);
            if (k % 8 == 7) Dump(e.bone);
        }
        Dump(e.bone);
        if (e.base->GetNext()) e.base->SetNext(nullptr);
        Drop(e);
        NoteWorld();
    });
}

// A written extension read back by a fresh one of the same side (the record's version on the tape 3, 4 or 5), dumped
// and written again.
DIFF_TEST(BoneDataExtension_ReadWrite, 0xa98560)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "read write", 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const Extension source = Make(original);
        Randomise(source.bone, rng);
        source.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        drawfx::Note("tape", uint32_t(gTape.size()));
        {
            difftest::NotCounted quiet;
            if (!gTape.empty() && gTape[0].words.size() > 1) gTape[0].words[1] = 3 + rng() % 3;
        }
        const Extension copy = Make(original);
        Randomise(copy.bone, rng);
        copy.referent->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader));
        drawfx::Note("read to", uint32_t(gReader.next));
        Dump(copy.bone);
        copy.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        Drop(copy);
        Drop(source);
        NoteWorld();
    });
}
