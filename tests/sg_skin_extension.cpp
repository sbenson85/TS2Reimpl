// Differential tests of nGZSceneGraph::cSkinExtension (lane QP, scenegraph D3 part 1; src/sg/cSkinExtension.h).
//
// Each side's extension comes from its own factory: the game's 0xa97720 (new 0x74, the constructor 0xa97240) and ours
// Create; each is driven through its own vtables.
//
//   SkinExtension_Slots      random calls through the extension's three faces: the cISkinExtension face (every slot:
//                            the name and word, the pair list over six counted fakes and null (append, erase, replace
//                            the second, both getters), the name map over mixed-case names and keys (null keys too), the
//                            pointer list, the counts), cIExtension (QueryInterface over the iids, IsKindOf, the type
//                            name, GetDescription, Clone with the copy dumped then released, cExtension's part through
//                            the class's own thunks: owner, next) and the referent face (its type-info record, Write
//                            through a tape writer). Every AddRef / Release of the fakes is in the log; the lists are
//                            dumped every 8 calls and at the end, and the drop notes the fakes' counts after the
//                            destructor.
//   SkinExtension_ReadWrite  a random extension written to a tape (its record, cExtension's, the name and the word), a
//                            fresh extension of the same side with other values reading that tape (Read 0xa97b50), then
//                            dumped and written again: the tapes and dumps compared between the sides.
//
// RVAs these tests reach (status entries cite them): the factory 0xa97720, the constructors 0xa97240 / 0xa970e0, the
// destructor 0xa97340 and its deleting destructor 0xa97490, Clone 0xa97660, QueryInterface 0xa97af0, Read 0xa97b50,
// GetTypeInfo 0xa97e50, Write 0xa97f50, IsKindOf 0xa97e30, the type name 0xa97700, the face's slots 0xa97da5,
// 0xa97dde, 0xa97dc3, 0xa974c0, 0xa97be0, 0xa97efe, 0xa97880, 0xa975a0, 0xa97ce0, 0xa977d2, 0xa9782a, 0xa977e9,
// 0xa97dfd, 0xa9784a (its others are folded accessors), and the vtordisp thunks of the vtables 0xe74dc0 / 0xe74e30 the
// game side calls through: cIExtension's 0xa97adb, 0xa97620, 0xa97bc6, 0xa977a6, 0xa977b1, 0xa97d8f, 0xa97706,
// 0xa97b3c, 0xa976ce, 0xa976d9, 0xa977bc, 0xa9781f, 0xa97636, 0xa97641, 0xa9764c (Clone), 0xa9762b, 0xa97814, 0xa97d9a,
// 0xa97db8, 0xa97e27 (IsKindOf), 0xa977c7, 0xa976ef (the type name), 0xa976e4, and cISkinExtension's 0xa974b0,
// 0xa97bd1, 0xa9786a, 0xa9758a, 0xa97cc9.
#include "difftest.h"
#include "sgfx.h"
#include "test_helpers.h"

#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "sg/cSkinExtension.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFactory = 0xa97720;
constexpr uint32_t kObjectTag = 0x7b000000;
using FactoryFn = cIGZUnknown *(__cdecl *)();

difftest::Context *gCtx;

// ---- a tape writer and reader (the persist writer's and reader's slots the extension calls) ----
struct Token {
    char kind; // 'w' a word, 's' a string, 'i' a type-info record
    std::vector<uint32_t> words;
    std::string text;
};
std::vector<Token> gTape;

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

#define SGSKE_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGSKE_BAD(0) SGSKE_BAD(1) SGSKE_BAD(2) SGSKE_BAD(3) SGSKE_BAD(4) SGSKE_BAD(5) SGSKE_BAD(6) SGSKE_BAD(7) SGSKE_BAD(8)
    virtual uint32_t SetSint32(int32_t v) { Put("w SetSint32", {'w', {uint32_t(v)}, {}}); return 1; } // 9
    virtual uint32_t SetUint32(uint32_t v) { Put("w SetUint32", {'w', {v}, {}}); return 1; }         // 10
    SGSKE_BAD(11) SGSKE_BAD(12) SGSKE_BAD(13) SGSKE_BAD(14) SGSKE_BAD(15) SGSKE_BAD(16) SGSKE_BAD(17) SGSKE_BAD(18)
    SGSKE_BAD(19) SGSKE_BAD(20) SGSKE_BAD(21) SGSKE_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) // 23
    {
        Put("w WriteRZCharStr", {'s', {}, text ? std::string(text) : std::string("<null>")});
        return 1;
    }
    SGSKE_BAD(24) SGSKE_BAD(25) SGSKE_BAD(26) SGSKE_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

// The reader answers from gTape in order, one token a call whatever its kind. ReadTypeInfo answers a name fake, counted
// (each noted at the end); GetGZStr gives the string the token's text through its FromChar (slot 4).
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
    SGSKE_BAD(0) SGSKE_BAD(1) SGSKE_BAD(2) SGSKE_BAD(3) SGSKE_BAD(4) SGSKE_BAD(5) SGSKE_BAD(6) SGSKE_BAD(7) SGSKE_BAD(8)
    virtual int32_t GetSint32() { return int32_t(WordOf(Take("r GetSint32"))); } // 9
    virtual uint32_t GetUint32() { return WordOf(Take("r GetUint32")); }          // 10
    SGSKE_BAD(11) SGSKE_BAD(12) SGSKE_BAD(13) SGSKE_BAD(14) SGSKE_BAD(15) SGSKE_BAD(16) SGSKE_BAD(17) SGSKE_BAD(18)
    SGSKE_BAD(19) SGSKE_BAD(20) SGSKE_BAD(21)
    virtual bool GetGZStr(void *str) // 22
    {
        const Token *t = Take("r GetGZStr");
        drawfx::Note("string at", drawfx::Norm(P(str)));
        if (str) reinterpret_cast<cIGZString *>(str)->FromChar(t ? t->text.c_str() : "<end>");
        return t != nullptr;
    }
    SGSKE_BAD(23) SGSKE_BAD(24) SGSKE_BAD(25)
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
    Fake held[6];  // ids 500-505: what the lists count
};
World w;

void BuildWorld(uint32_t seed, drawfx::Cells &)
{
    w.text.Reset<kText>(330);
    w.text.On(4, &TextFromChar);
    for (unsigned i = 0; i < 4; ++i) gNames[i].Reset<sgfx::kUnknown>(440 + i);
    for (unsigned i = 0; i < 6; ++i) w.held[i].Reset<sgfx::kUnknown>(500 + i);
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
    for (const Fake &h : w.held) drawfx::Note("held refs", {h.id, uint32_t(h.refs)});
    drawfx::Note("owner refs", uint32_t(w.owner.refs));
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

// One extension of the side being run, as its three faces (the test holds two references: one for the extension and
// one the face's query gave).
struct Extension {
    cIExtension *base = nullptr;
    cISkinExtension *skin = nullptr;
    cIGZPersistSerializableReferent *referent = nullptr;
};

Extension Make(bool original)
{
    Extension e;
    cIGZUnknown *const made = original ? gCtx->Original<FactoryFn>(kFactory)() : cSkinExtension::Create();
    e.base = static_cast<cIExtension *>(made);
    // The object: cIExtension at +0x64 in the game's layout (ours the same, or the normalised pointers below differ).
    drawfx::AddRange(reinterpret_cast<uint8_t *>(e.base) - 0x64, 0x74, kObjectTag);
    drawfx::Note("first count", e.base->AddRef());
    void *face = nullptr;
    drawfx::Note("query skin face", {uint8_t(e.base->QueryInterface(cISkinExtension::kIid, &face)), drawfx::Norm(P(face))});
    e.skin = static_cast<cISkinExtension *>(face);
    e.referent = e.base->GetPersistReferent();
    drawfx::Note("referent", drawfx::Norm(P(e.referent)));
    return e;
}

// Every reference the test holds dropped, each answer noted.
void Drop(const Extension &e)
{
    drawfx::Note("face release", e.base->Release());
    drawfx::Note("last release", e.base->Release());
}

void Dump(cISkinExtension *skin)
{
    sgfx::NoteText("name", skin->GetName());
    drawfx::Note("word", skin->GetWord());
    const uint32_t pairs = skin->GetPairCount();
    drawfx::Note("pairs", pairs);
    for (uint32_t i = 0; i < pairs; ++i)
        drawfx::Note("pair", {drawfx::Norm(P(skin->GetPairFirstConst(i))), drawfx::Norm(P(skin->GetPairFirst(i))),
                              drawfx::Norm(P(skin->GetPairSecondConst(i))), drawfx::Norm(P(skin->GetPairSecond(i)))});
    const uint32_t names = skin->GetNameMapCount();
    drawfx::Note("names", names);
    for (uint32_t i = 0; i < names; ++i) {
        sgfx::NoteText("mapped name", skin->GetMappedName(i));
        sgfx::NoteText("mapped key", skin->GetMappedKey(i));
    }
    const uint32_t refs = skin->GetRefCount();
    drawfx::Note("refs", refs);
    for (uint32_t i = 0; i < refs; ++i) drawfx::Note("ref", drawfx::Norm(P(skin->GetRef(i))));
    for (const Fake &h : w.held) drawfx::Note("held refs", {h.id, uint32_t(h.refs)});
}

// ---- the operations ----
const uint32_t kIids[] = {cISkinExtension::kIid, cIExtension::kIid, 0x5b0aaa74, 0xbb11a53e, 0x3b253dc7, 0xfb859324, 1,
                          0x12345678};
const char *const kNames[] = {"", "Spine", "spine", "SPINE", "l_Hand", "L_HAND", "r_hand", "Root_Bone", "neck", "Neck2"};

const char *Name(std::mt19937 &rng) { return kNames[rng() % 10]; }
cIGZUnknown *Held(std::mt19937 &rng)
{
    const unsigned k = rng() % 7;
    return k < 6 ? reinterpret_cast<cIGZUnknown *>(&w.held[k]) : nullptr;
}
uint32_t Word(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return 0;
    case 1: return rng() % 8;
    case 2: return 0xffffffffu;
    default: return rng();
    }
}

void Operate(const Extension &e, unsigned op, std::mt19937 &rng)
{
    cISkinExtension *const skin = e.skin;
    switch (op) {
    case 0: skin->SetName(Name(rng)); break;
    case 1: skin->SetWord(Word(rng)); break;
    case 2:
    case 3: {
        const uint32_t n = skin->GetPairCount();
        if (!n) break;
        const uint32_t i = rng() % n;
        drawfx::Note("pair get", {i, drawfx::Norm(P(op == 2 ? skin->GetPairFirstConst(i) : skin->GetPairFirst(i))),
                                  drawfx::Norm(P(op == 2 ? skin->GetPairSecondConst(i) : skin->GetPairSecond(i)))});
        break;
    }
    case 4:
    case 5: {
        cIGZUnknown *const a = Held(rng);
        skin->AddPair(a, Held(rng));
        break;
    }
    case 6: {
        const uint32_t n = skin->GetPairCount();
        if (n) skin->ErasePair(rng() % n);
        break;
    }
    case 7: {
        const uint32_t n = skin->GetPairCount();
        if (n) {
            const uint32_t i = rng() % n;
            skin->SetPairSecond(i, rng() % 3 ? Held(rng) : skin->GetPairSecond(i));
        }
        break;
    }
    case 8:
    case 9: {
        const char *const name = Name(rng);
        skin->MapName(name, rng() % 6 ? Name(rng) : nullptr);
        break;
    }
    case 10: {
        const uint32_t n = skin->GetRefCount();
        if (n) {
            const uint32_t i = rng() % n;
            drawfx::Note("ref get", {i, drawfx::Norm(P(skin->GetRef(i)))});
        }
        break;
    }
    case 11: skin->PushRef(Held(rng)); break;
    case 12: {
        const uint32_t n = skin->GetRefCount();
        if (n) skin->EraseRef(rng() % n);
        break;
    }
    case 13: { // QueryInterface through either face
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = rng() % 2 ? e.base->QueryInterface(iid, rng() % 8 ? &out : nullptr)
                                 : e.referent->QueryInterface(iid, rng() % 8 ? &out : nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out && iid == cISkinExtension::kIid) // its own slots 0-2 are setters and getters: released through the base
            e.base->Release();
        else if (out && iid != 0xfb859324)
            static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 14: {
        const uint32_t iid = kIids[rng() % 8];
        drawfx::Note("kind of", {iid, uint8_t(e.base->IsKindOf(iid))});
        break;
    }
    case 15:
        sgfx::NoteText("type name", e.base->GetTypeName());
        drawfx::Note("persistent", uint8_t(e.base->IsPersistent()));
        e.base->GetDescription(reinterpret_cast<cIGZString *>(&w.text));
        break;
    case 16: { // Clone: the copy (count 0) counted, dumped, then released (destroyed)
        cIExtension *const copy = e.base->Clone();
        drawfx::Note("clone count", copy->AddRef());
        void *face = nullptr;
        drawfx::Note("clone query", uint8_t(copy->QueryInterface(cISkinExtension::kIid, &face)));
        Dump(static_cast<cISkinExtension *>(face));
        drawfx::Note("clone face release", copy->Release());
        drawfx::Note("clone release", copy->Release());
        break;
    }
    case 17: {
        cPersistTypeInfo info = e.referent->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        e.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        break;
    }
    case 18: {
        cIObjectGraphNode *owner = rng() % 3 ? reinterpret_cast<cIObjectGraphNode *>(&w.owner) : nullptr;
        drawfx::Note("set owner", drawfx::Norm(P(e.base->SetOwner(owner))));
        drawfx::Note("owner", {drawfx::Norm(P(e.base->GetOwner())), drawfx::Norm(e.referent->GetOwnerInfo())});
        break;
    }
    default: { // 19
        const unsigned k = rng() % 4;
        cIExtension *const next = k < 3 ? reinterpret_cast<cIExtension *>(&w.next[k]) : nullptr;
        const bool changes = next != e.base->GetNext();
        const uint32_t r = e.base->SetNext(next);
        drawfx::Note("set next", {uint32_t(changes), changes ? r : 0});
        drawfx::Note("next", drawfx::Norm(P(e.base->GetNext())));
        break;
    }
    }
}

void Randomise(cISkinExtension *skin, std::mt19937 &rng)
{
    skin->SetName(Name(rng));
    skin->SetWord(Word(rng));
    for (unsigned n = rng() % 3; n; --n) {
        cIGZUnknown *const a = Held(rng);
        skin->AddPair(a, Held(rng));
    }
    for (unsigned n = rng() % 3; n; --n) {
        const char *const name = Name(rng);
        skin->MapName(name, Name(rng));
    }
    for (unsigned n = rng() % 3; n; --n) skin->PushRef(Held(rng));
}

} // namespace

// Random calls through the three faces of each side's extension.
DIFF_TEST(SkinExtension_Slots, 0xa97720)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "slots", 64, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const Extension e = Make(original);
        Dump(e.skin);
        for (int k = 0; k < 60; ++k) {
            const unsigned op = rng() % 20;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(e, op, rng);
            if (k % 8 == 7) Dump(e.skin);
        }
        Dump(e.skin);
        if (e.base->GetNext()) e.base->SetNext(nullptr);
        Drop(e);
        NoteWorld();
    });
}

// A written extension read back by a fresh one of the same side, dumped and written again.
DIFF_TEST(SkinExtension_ReadWrite, 0xa97b50)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "read write", 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const Extension source = Make(original);
        Randomise(source.skin, rng);
        source.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        drawfx::Note("tape", uint32_t(gTape.size()));
        const Extension copy = Make(original);
        Randomise(copy.skin, rng);
        copy.referent->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader));
        drawfx::Note("read to", uint32_t(gReader.next));
        Dump(copy.skin);
        copy.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        Drop(copy);
        Drop(source);
        NoteWorld();
    });
}
