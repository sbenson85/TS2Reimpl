// Differential tests of nTSSG::cDiffusionLightExtension (lane 261109c, scenegraph LG7e; src/sg/cDiffusionLightExtension.h).
//
// Each side's extension comes from its own factory: the game's 0x2ea3b0 (new 0xbc, the constructor 0x2ea080) and ours
// Create; each is driven through its own vtables. Right after it is made the object's fields are dumped by a field list
// (the constructor's defaults and cleared flags), then the override values the constructor leaves uninitialised, and
// the padding a pointer into the object can read, are filled with the same seeded bytes on both sides.
//
//   DiffusionLightExtension_Slots      random calls through the extension's three faces: the cIDiffusionLightExtension
//                                      face (every slot: the setters and default setters over special floats, NaN
//                                      payloads and random bit patterns, their pointer argument a quarter of the time
//                                      pointing into the object itself so the copy order shows; the two bool setters
//                                      called with raw dwords; ResetFlags; every getter, its pointer as an offset into
//                                      the object and the floats it shows; the bool getter's al), raw writes of 0 / 1
//                                      to the flags and of any byte to the two directional bytes (what a raw caller or
//                                      a reader's GetBool can leave), cIExtension (QueryInterface over the iids through
//                                      it and the referent, IsKindOf, the type name, IsPersistent, GetDescription, Clone
//                                      with the copy's fields and getters dumped then released; cExtension's part through
//                                      the class's own thunks: owner, next, Connect / Disconnect with listeners on our
//                                      connection manager, Notify / Ask / OnQuery, OnEvent, IsActive / GetEventCount,
//                                      GetEventIds, the event attribute with the registry's statics in place, the empty
//                                      slots 7 / 10) and the referent face (its type-info record, Write through a tape
//                                      writer). The fields and getters dumped every 8 calls and at the end.
//   DiffusionLightExtension_ReadWrite  a random extension written to a tape (its record, cExtension's, the defaults),
//                                      the directional byte on the tape sometimes another byte, a fresh extension of
//                                      the same side with other values reading that tape (Read 0x2ea630), then dumped
//                                      and written again: the tapes and dumps compared between the sides.
//
// RVAs these tests reach (status entries cite them): the factory 0x2ea3b0, the constructors 0x2ea080 / 0x2e9eb0, the
// destructor 0x2ea1d0 and its deleting destructor 0x2ea220, Clone 0x2ea2d0, QueryInterface 0x2ea5d0, Read 0x2ea630,
// GetTypeInfo 0x2ea9e0, Write 0x2eaab0, the type name 0x2ea380 (folded with a cLightCommand's slot 5); the
// cIDiffusionLightExtension face's thunks (each with its body; vtable 0xdb6bdc, slot order):
//   0x2ea27b 0x2ea948 0x2ea908 0x2ea842 0x2ea749 0x2ea8ba 0x2ea7de 0x2ea79e 0x2ea966 0x2ea81e 0x2ea87e (0-10),
//   0x2ea926 0x2ea8de (11, 12), 13 folded (shared_vf_2b6a8a), 0x2ea728 0x2ea8a2 (14, 15), 16 0x2ea7c2 (shared_vf),
//   17 0x2ea77a (shared_vf; body 0x2ea790), 18 folded (shared_vf_2748ea), 19 0x2ea802, 20 0x2ea862 (both shared_vf),
//   0x2ea531 0x2ea50e 0x2ea4ae 0x2ea436 0x2ea4ee 0x2ea46e 0x2ea451 0x2ea551 0x2ea48e 0x2ea4d1 (21-30);
// and the cIExtension vtable 0xdb6b6c's vtordisp thunks the game side calls through: QueryInterface 0x2ea5c2, AddRef
// 0x2ea243, Release 0x2ea70c, OnEvent 0x2ea56e, OnQuery 0x2ea57c, Notify 0x2ea71a, Ask 0x2ea394, slots 7 / 10 0x2ea61f,
// Connect 0x2ea344, Disconnect 0x2ea352, GetEventCount 0x2ea5b4, GetEventIds 0x2ea25f, GetEventAttribute 0x2ea26d,
// Clone 0x2ea2b9, GetPersistReferent 0x2ea251, GetNext 0x2ea5a6, SetOwner 0x2ea982, SetNext 0x2ea990, IsKindOf 0x2ea99e
// (with its body 0x2ea9b0), IsPersistent 0x2ea598, the type name 0x2ea36e, GetDescription 0x2ea360.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"
#include "test_helpers.h"

#include "rz/cRZString.h"
#include "sg/cDiffusionLightExtension.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using nTSSG::cDiffusionLightExtension;
using nTSSG::cIDiffusionLightExtension;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFactory = 0x2ea3b0;
constexpr uint32_t kObjectTag = 0x7b000000;
constexpr uint32_t kSize = 0xbc;
constexpr uint32_t kBaseAt = 0xac; // cIExtension in the object
constexpr uint32_t kFaceAt = 0xb4; // cIDiffusionLightExtension in the object
using FactoryFn = cIGZUnknown *(__cdecl *)();

difftest::Context *gCtx;

// ---- a tape writer and reader (the persist writer's and reader's slots the extension calls) ----
struct Token {
    char kind; // 'w' a word, 'b' a byte (SetUint8), 'a' a float array, 'i' a type-info record
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

#define SGDLE_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGDLE_BAD(0) SGDLE_BAD(1) SGDLE_BAD(2) SGDLE_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { Put("w SetUint8", {'b', {v}, {}}); return 1; } // 4: the whole pushed dword
    SGDLE_BAD(5) SGDLE_BAD(6) SGDLE_BAD(7) SGDLE_BAD(8)
    virtual uint32_t SetSint32(int32_t v) { Put("w SetSint32", {'w', {uint32_t(v)}, {}}); return 1; } // 9
    SGDLE_BAD(10)
    virtual uint32_t SetFloat32(float v) { Put("w SetFloat32", {'w', {Bits(v)}, {}}); return 1; } // 11
    SGDLE_BAD(12) SGDLE_BAD(13) SGDLE_BAD(14) SGDLE_BAD(15) SGDLE_BAD(16) SGDLE_BAD(17) SGDLE_BAD(18) SGDLE_BAD(19)
    virtual uint32_t SetFloat32Array(const float *data, uint32_t count) // 20
    {
        Token t{'a', {}, {}};
        for (uint32_t i = 0; i < count; ++i) t.words.push_back(Bits(data[i]));
        Put("w SetFloat32Array", std::move(t));
        return 1;
    }
    SGDLE_BAD(21) SGDLE_BAD(22) SGDLE_BAD(23) SGDLE_BAD(24) SGDLE_BAD(25) SGDLE_BAD(26) SGDLE_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

// The reader answers from gTape in order, one token a call whatever its kind. GetBool answers the token's whole word in
// eax (the reader's al is what a caller may keep). ReadTypeInfo answers a name fake, counted (each noted at the end).
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
    SGDLE_BAD(0) SGDLE_BAD(1) SGDLE_BAD(2) SGDLE_BAD(3)
    virtual uint32_t GetBool() { return WordOf(Take("r GetBool")); } // 4
    SGDLE_BAD(5) SGDLE_BAD(6) SGDLE_BAD(7) SGDLE_BAD(8)
    virtual int32_t GetSint32() { return int32_t(WordOf(Take("r GetSint32"))); } // 9
    SGDLE_BAD(10)
    virtual float GetFloat32() { return FloatOf(WordOf(Take("r GetFloat32"))); } // 11
    SGDLE_BAD(12) SGDLE_BAD(13) SGDLE_BAD(14) SGDLE_BAD(15) SGDLE_BAD(16) SGDLE_BAD(17) SGDLE_BAD(18) SGDLE_BAD(19)
    virtual void GetFloat32Array(float *data, uint32_t count) // 20
    {
        const Token *t = Take("r GetFloat32Array");
        drawfx::Note("array count", count);
        for (uint32_t i = 0; i < count; ++i) data[i] = FloatOf(t && i < t->words.size() ? t->words[i] : 0x3f800000u + i);
    }
    SGDLE_BAD(21) SGDLE_BAD(22) SGDLE_BAD(23) SGDLE_BAD(24) SGDLE_BAD(25)
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
    Fake listener[2];
    sgfx::Manager manager;
};
World w;

// The plain behaviour type's event registry (0x1485f0: GetEventAttribute 0xa3b990 asks it), as tests/sg_extension.cpp.
constexpr sgfx::RegistryStatics kExtensionRegistry{0x10c94f4, 0x10c94fc, 0x10c9510, 0x10c9518};
constexpr uint32_t kConnectionChanged = 0x5b51e806; // cConnectionChangedEvent: OnEvent syncs the active bit
const uint32_t kEvents[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], 0x5678};

// The record GetEventAttribute copies for an unknown event (0x106a48c) is all zeros in the unstarted image: a pattern
// shows a wrong record address.
struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106a48c;
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i; // ours reads its own
        }
    }
    ~MissingRecordScope()
    {
        std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
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
    sgfx::MakeListener(w.listener[0], 500, true);
    sgfx::MakeListener(w.listener[1], 501, seed % 2 != 0);
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cExtensionBehaviorTraits::kEventManagerCell);
}

void NoteWorld()
{
    for (const Fake &e : w.next) drawfx::Note("next refs", {e.id, uint32_t(e.refs)});
    for (const Fake &l : w.listener) drawfx::Note("listener refs", {l.id, uint32_t(l.refs)});
    drawfx::Note("owner refs", uint32_t(w.owner.refs));
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

// ---- the object's fields, by a list (never the padding) ----
struct Field {
    uint32_t at, size;
};
constexpr Field kFields[] = {
    {0x18, 8}, {0x20, 8}, {0x28, 4}, {0x2c, 12}, {0x38, 1}, {0x3c, 4}, {0x40, 4}, {0x44, 4}, {0x48, 4}, {0x4c, 4},
    {0x50, 8}, {0x58, 1}, {0x5c, 8}, {0x64, 1}, {0x68, 4}, {0x6c, 1}, {0x70, 12}, {0x7c, 1}, {0x7d, 1}, {0x7e, 1},
    {0x80, 4}, {0x84, 1}, {0x88, 4}, {0x8c, 1}, {0x90, 4}, {0x94, 1}, {0x98, 4}, {0x9c, 1}, {0xa0, 4}, {0xa4, 1},
};
// The flags (0 / 1 in the game) and the two directional bytes (any byte: a raw caller's low byte, a reader's al).
constexpr uint32_t kFlags[] = {0x58, 0x64, 0x6c, 0x7c, 0x7e, 0x84, 0x8c, 0x94, 0x9c, 0xa4};
constexpr uint32_t kDirectionalBytes[] = {0x38, 0x7d};

void DumpFields(const char *note, const uint8_t *obj, bool valuesSet = true)
{
    drawfx::Call c;
    c.note = note;
    for (const Field &f : kFields) {
        if (!valuesSet && f.at >= 0x50 && f.size >= 4) continue; // an override value the constructor leaves unset
        if (f.size == 1) {
            c.words.push_back(obj[f.at]);
            continue;
        }
        for (uint32_t k = 0; k < f.size; k += 4) {
            uint32_t v;
            std::memcpy(&v, obj + f.at + k, 4);
            c.words.push_back(v);
        }
    }
    drawfx::gLog.push_back(std::move(c));
}

// The bytes the constructor leaves as the heap had them (the override values, the padding a pointer argument into the
// object can read): the same seeded bytes on both sides. The flags and the directional override byte stay as made.
void FillUnset(uint8_t *obj, std::mt19937 &rng)
{
    for (uint32_t at = 0x39; at < 0x3c; ++at) obj[at] = uint8_t(rng());
    for (uint32_t at = 0x50; at < 0xa8; ++at) {
        bool kept = at == 0x7d;
        for (uint32_t f : kFlags) kept |= at == f;
        if (!kept) obj[at] = uint8_t(rng());
    }
}

// One extension of the side being run, as its three faces (the test holds two references: one for the extension and
// one the face's query gave).
struct Extension {
    cIExtension *base = nullptr;
    cIDiffusionLightExtension *face = nullptr;
    cIGZPersistSerializableReferent *referent = nullptr;
    uint8_t *obj = nullptr;
};

Extension Make(bool original, std::mt19937 &rng)
{
    Extension e;
    cIGZUnknown *const made = original ? gCtx->Original<FactoryFn>(kFactory)() : cDiffusionLightExtension::Create();
    e.base = static_cast<cIExtension *>(made);
    // The object: cIExtension at +0xac in the game's layout (ours the same, or the normalised pointers below differ).
    e.obj = reinterpret_cast<uint8_t *>(e.base) - kBaseAt;
    drawfx::AddRange(e.obj, kSize, kObjectTag);
    drawfx::Note("first count", e.base->AddRef());
    void *face = nullptr;
    drawfx::Note("query face", {uint8_t(e.base->QueryInterface(cIDiffusionLightExtension::kIid, &face)), drawfx::Norm(P(face))});
    drawfx::Note("face at", uint32_t(uint32_t(P(face) - P(e.obj)) == kFaceAt));
    // A null out with the class's own iid (the step's random queries rarely pair the two): false, nothing counted.
    drawfx::Note("query null out", uint8_t(e.base->QueryInterface(cIDiffusionLightExtension::kIid, nullptr)));
    e.face = static_cast<cIDiffusionLightExtension *>(face);
    e.referent = e.base->GetPersistReferent();
    drawfx::Note("referent", drawfx::Norm(P(e.referent)));
    DumpFields("made", e.obj, false);
    FillUnset(e.obj, rng);
    return e;
}

// Every reference the test holds dropped through the base (the face's own slots 0-2 are setters), each answer noted.
void Drop(const Extension &e)
{
    drawfx::Note("face release", e.base->Release());
    drawfx::Note("last release", e.base->Release());
}

// The bool slots called with a raw dword (the setters keep its low byte) and read as the whole eax (the getter writes al).
void SetRawBool(cIDiffusionLightExtension *face, unsigned slot, uint32_t value)
{
    using Fn = void(__thiscall *)(void *, uint32_t);
    (*reinterpret_cast<Fn *const *>(face))[slot](face, value);
}
uint8_t GetRawBool(cIDiffusionLightExtension *face)
{
    using Fn = uint32_t(__thiscall *)(void *);
    return uint8_t((*reinterpret_cast<Fn *const *>(face))[25](face));
}

// A getter's pointer as an offset into its object, and the floats there.
void NotePointer(const char *note, const float *p, unsigned count, const uint8_t *obj)
{
    drawfx::Call c;
    c.note = note;
    c.words.push_back(uint32_t(P(p) - P(obj)));
    for (unsigned i = 0; i < count; ++i) c.words.push_back(Bits(p[i]));
    drawfx::gLog.push_back(std::move(c));
}

void NoteGetter(cIDiffusionLightExtension *face, const uint8_t *obj, unsigned k)
{
    switch (k) {
    case 0: NotePointer("min", face->GetSourceMin(), 2, obj); break;
    case 1: NotePointer("max", face->GetSourceMax(), 2, obj); break;
    case 2: drawfx::Note("intensity", Bits(face->GetSourceIntensity())); break;
    case 3: NotePointer("colour", face->GetSourceColor(), 3, obj); break;
    case 4: drawfx::Note("directional", GetRawBool(face)); break;
    case 5: drawfx::Note("falloff", Bits(face->GetSourceFalloff())); break;
    case 6: drawfx::Note("directional falloff", Bits(face->GetSourceDirectionalFalloff())); break;
    case 7: drawfx::Note("wall scale", Bits(face->GetSourceWallIntensityScale())); break;
    case 8: drawfx::Note("floor scale", Bits(face->GetSourceFloorIntensityScale())); break;
    default: drawfx::Note("inter-floor falloff", Bits(face->GetSourceInterFloorFalloff())); break;
    }
}

void Dump(const Extension &e)
{
    for (unsigned k = 0; k < 10; ++k) NoteGetter(e.face, e.obj, k);
    DumpFields("fields", e.obj);
}

// ---- the values ----
float Value(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return 0.5f;
    case 5: return std::numeric_limits<float>::quiet_NaN();
    case 6: return FloatOf(0x7fa00001u); // a signalling NaN's payload
    case 7: return FloatOf(0xffc12345u);
    case 8: return std::numeric_limits<float>::infinity();
    case 9: return FloatOf(1u); // a denormal
    case 10: return FloatOf(rng());
    default: return float(int32_t(rng() % 2001) - 1000) / 16.0f;
    }
}

uint32_t RawBool(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return 0;
    case 1: return 1;
    case 2: return rng() & 0xffffff00u; // a false low byte under set bits
    default: return rng();
    }
}

// A pointer argument of `count` floats: most often the test's own block of values, a quarter of the time a place in
// the object (the defaults and overrides, 4-aligned, the whole read inside the object's fields).
const float *Arg(const Extension &e, unsigned count, float *own, std::mt19937 &rng)
{
    if (rng() % 4 == 0) {
        const uint32_t slots = (0xa8 - 0x18 - 4 * count) / 4 + 1;
        return reinterpret_cast<const float *>(e.obj + 0x18 + 4 * (rng() % slots));
    }
    for (unsigned i = 0; i < count; ++i) own[i] = Value(rng);
    return own;
}

void Set(const Extension &e, unsigned which, std::mt19937 &rng)
{
    cIDiffusionLightExtension *const f = e.face;
    float own[3];
    switch (which) {
    case 0: f->SetSourceMin(Arg(e, 2, own, rng)); break;
    case 1: f->SetSourceMax(Arg(e, 2, own, rng)); break;
    case 2: f->SetSourceIntensity(Value(rng)); break;
    case 3: f->SetSourceColor(Arg(e, 3, own, rng)); break;
    case 4: SetRawBool(f, 5, RawBool(rng)); break;
    case 5: f->SetSourceFalloff(Value(rng)); break;
    case 6: f->SetSourceDirectionalFalloff(Value(rng)); break;
    case 7: f->SetSourceWallIntensityScale(Value(rng)); break;
    case 8: f->SetSourceFloorIntensityScale(Value(rng)); break;
    case 9: f->SetSourceInterFloorFalloff(Value(rng)); break;
    case 10: f->SetDefaultSourceMin(Arg(e, 2, own, rng)); break;
    case 11: f->SetDefaultSourceMax(Arg(e, 2, own, rng)); break;
    case 12: f->SetDefaultSourceIntensity(Value(rng)); break;
    case 13: f->SetDefaultSourceColor(Arg(e, 3, own, rng)); break;
    case 14: SetRawBool(f, 15, RawBool(rng)); break;
    case 15: f->SetDefaultSourceFalloff(Value(rng)); break;
    case 16: f->SetDefaultSourceDirectionalFalloff(Value(rng)); break;
    case 17: f->SetDefaultSourceWallIntensityScale(Value(rng)); break;
    case 18: f->SetDefaultSourceFloorIntensityScale(Value(rng)); break;
    default: f->SetDefaultSourceInterFloorFalloff(Value(rng)); break;
    }
}

// ---- the operations ----
const uint32_t kIids[] = {cIDiffusionLightExtension::kIid, cIExtension::kIid, 0x5b0aaa74, 0xbb11a53e, 0x3b253dc7,
                          0xfb859324, 1, 0x12345678};

void NoteIdRange(nGZEvent::cAttributeIDRange ids)
{
    unsigned steps = 0;
    while (steps < 20 && !ids.mCurrent->IsEqual(ids.mEnd)) {
        drawfx::Note("event id", ids.mCurrent->Get());
        ids.mCurrent->Next();
        ++steps;
    }
    drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
}

void Operate(const Extension &e, unsigned op, std::mt19937 &rng)
{
    if (op < 20) return Set(e, op, rng);
    switch (op) {
    case 20: e.face->ResetFlags(); break;
    case 21: { // the flags as the setters and ResetFlags leave them; the directional bytes any byte
        if (rng() % 3) e.obj[kFlags[rng() % 10]] = uint8_t(rng() % 2);
        else e.obj[kDirectionalBytes[rng() % 2]] = uint8_t(rng());
        break;
    }
    case 22:
    case 23: NoteGetter(e.face, e.obj, rng() % 10); break;
    case 24: { // QueryInterface through either face
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = rng() % 2 ? e.base->QueryInterface(iid, rng() % 8 ? &out : nullptr)
                                 : e.referent->QueryInterface(iid, rng() % 8 ? &out : nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out && iid == cIDiffusionLightExtension::kIid) // its own slots 0-2 are setters: released through the base
            e.base->Release();
        else if (out && iid != 0xfb859324)
            static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 25: {
        const uint32_t iid = kIids[rng() % 8];
        drawfx::Note("kind of", {iid, uint8_t(e.base->IsKindOf(iid))});
        break;
    }
    case 26:
        sgfx::NoteText("type name", e.base->GetTypeName());
        drawfx::Note("persistent", uint8_t(e.base->IsPersistent()));
        e.base->GetDescription(reinterpret_cast<cIGZString *>(&w.text));
        break;
    case 27: { // Clone: the copy (count 0) counted, its fields and getters dumped, then released (destroyed)
        cIExtension *const copy = e.base->Clone();
        const uint8_t *const copyObj = reinterpret_cast<const uint8_t *>(copy) - kBaseAt;
        drawfx::Note("clone count", copy->AddRef());
        void *face = nullptr;
        drawfx::Note("clone query", {uint8_t(copy->QueryInterface(cIDiffusionLightExtension::kIid, &face)),
                                     uint32_t(P(face) - P(copyObj))});
        auto *const copyFace = static_cast<cIDiffusionLightExtension *>(face);
        for (unsigned k = 0; k < 10; ++k) NoteGetter(copyFace, copyObj, k);
        DumpFields("clone fields", copyObj);
        drawfx::Note("clone owner", {drawfx::Norm(P(copy->GetOwner())), drawfx::Norm(P(copy->GetNext()))});
        drawfx::Note("clone face release", copy->Release());
        drawfx::Note("clone release", copy->Release());
        break;
    }
    case 28: {
        cPersistTypeInfo info = e.referent->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 29: e.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter)); break;
    case 30: {
        cIObjectGraphNode *owner = rng() % 3 ? reinterpret_cast<cIObjectGraphNode *>(&w.owner) : nullptr;
        drawfx::Note("set owner", drawfx::Norm(P(e.base->SetOwner(owner))));
        drawfx::Note("owner", {drawfx::Norm(P(e.base->GetOwner())), drawfx::Norm(P(e.base->GetOwnerConst())),
                               drawfx::Norm(e.referent->GetOwnerInfo())});
        break;
    }
    case 31: {
        const unsigned k = rng() % 4;
        cIExtension *const next = k < 3 ? reinterpret_cast<cIExtension *>(&w.next[k]) : nullptr;
        const bool changes = next != e.base->GetNext();
        const uint32_t r = e.base->SetNext(next);
        drawfx::Note("set next", {uint32_t(changes), changes ? r : 0});
        drawfx::Note("next", {drawfx::Norm(P(e.base->GetNext())), drawfx::Norm(P(e.base->GetNextConst()))});
        break;
    }
    case 32: {
        cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
        if (rng() % 2)
            e.base->Connect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        else
            e.base->Disconnect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        break;
    }
    case 33:
        switch (rng() % 3) {
        case 0: e.base->NotifyListeners(kEvents[rng() % 4], rng(), nullptr); break;
        case 1: drawfx::Note("asked", uint8_t(e.base->AskListeners(kEvents[rng() % 4], rng(), nullptr))); break;
        default: drawfx::Note("on query", uint8_t(e.base->OnQuery(kEvents[rng() % 4], rng(), nullptr))); break;
        }
        break;
    case 34:
        e.base->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {e.base->IsActive(), e.base->GetEventCount()});
        break;
    case 35: {
        const nGZEvent::cEventAttribute a = e.base->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        break;
    }
    case 36:
        drawfx::Note("event count", e.base->GetEventCount());
        NoteIdRange(e.base->GetEventIds());
        break;
    default: // 37: the empty slots 7 and 10
        e.base->SetAttribute(rng(), rng(), rng());
        e.base->GetAttribute(rng(), rng(), rng());
        break;
    }
}

void Randomise(const Extension &e, std::mt19937 &rng)
{
    for (unsigned which = 0; which < 20; ++which)
        if (rng() % 4) Set(e, which, rng);
}

} // namespace

// Random calls through the three faces of each side's extension.
DIFF_TEST(DiffusionLightExtension_Slots, 0x2ea3b0)
{
    gCtx = &ctx;
    sgfx::RegistryScope registry(kExtensionRegistry);
    shape::StandIn registryStand{0x1485f0, &nGZEvent::cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    MissingRecordScope missing;
    sgfx::CheckSides(ctx, "slots", 64, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const Extension e = Make(original, rng);
        Dump(e);
        for (int k = 0; k < 56; ++k) {
            const unsigned op = rng() % 38;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(e, op, rng);
            if (k % 8 == 7) Dump(e);
        }
        Dump(e);
        if (e.base->GetNext()) e.base->SetNext(nullptr);
        Drop(e);
        NoteWorld();
    });
    w.manager.Drop();
}

// A written extension read back by a fresh one of the same side (the directional byte on the tape sometimes another
// byte), dumped and written again.
DIFF_TEST(DiffusionLightExtension_ReadWrite, 0x2ea630)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "read write", 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        const Extension source = Make(original, rng);
        Randomise(source, rng);
        source.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        drawfx::Note("tape", uint32_t(gTape.size()));
        {
            difftest::NotCounted quiet;
            const uint32_t raw = rng();
            if (raw % 2)
                for (Token &t : gTape)
                    if (t.kind == 'b') t.words[0] = raw >> 8; // the bool's token
        }
        const Extension copy = Make(original, rng);
        Randomise(copy, rng);
        copy.referent->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader));
        drawfx::Note("read to", uint32_t(gReader.next));
        Dump(copy);
        copy.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        Drop(copy);
        Drop(source);
        NoteWorld();
    });
    w.manager.Drop();
}
