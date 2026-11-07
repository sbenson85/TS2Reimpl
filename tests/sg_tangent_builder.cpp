// Differential tests of nGZSceneGraph::cTangentSpaceBuilder as a whole class (lane QG, scenegraph D2 part 2;
// src/sg/cTangentSpaceBuilder.h), on our cGeometryBuilder base (src/sg/cGeometryBuilder.h). The harness follows
// tests/sg_geometry_builders.cpp (lane QE): each side's builder comes from its own factory (the game's 0xa86bf0 and ours
// Create), the containers are ours on both sides (lane PA's cGeometryDataContainer, verified) filled with synthetic
// geometry through its face, and persistence runs over a tape writer and reader.
//
//   sg_tangent_builder_slots   random calls through the builder's three faces (the behaviour slots with listeners on our
//                              connection manager, QueryInterface over the iids through either face, the persist face's
//                              type info, Write and Read of a tape the builder wrote with the record versions and the
//                              name count changed, GetName, AddName), the object's words dumped every 8 calls and at the
//                              end.
//   sg_tangent_builder_build   synthetic containers (1-3 linkages with positions, normals and texture coordinates, decoy
//                              elements, alias lists, groups of every primitive type with case-varied names, degenerate
//                              triangles, zero and non-unit normals, values around the thresholds, at most one NaN),
//                              built once or twice; every element (header, floats) and linkage dumped through the
//                              container.
//
// Left out, with the original's reasons: indices past a linkage's alias lists, aliases past an element's count and groups
// whose index counts are not multiples of 3 (each is an unchecked vector index or a read past a range on both sides).
//
// RVAs these tests reach (status entries cite them): the factory 0xa86bf0, the constructor 0xa86920, the destructors
// 0xa86a80 / 0xa86af0, QueryInterface 0xa88230, Read 0xa88280, GetTypeInfo 0xa888c0, Write 0xa88990, GetName 0xa86be0,
// AddName 0xa86b20, Build 0xa86c80, with the helpers 0xaed570, 0xaedcd0, 0xaeda40, 0xa88560 and 0x1a66d0. The thunks the
// slots test calls (ours the compiler's, in our vtables): AddName 0xa86b10, GetName 0xa86bd3, Build 0xa86c76,
// QueryInterface 0xa88224 and the cIGeometryBuilder-face vtordisp thunks of the behaviour slots.
#include "code_patch.h"
#include "difftest.h"
#include "sgfx.h"

#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cTangentSpaceBuilder.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

using FactoryFn = cIGZUnknown *(__cdecl *)();

constexpr uint32_t kContainerTag = 0x6c000000;
constexpr uint32_t kBuilderTag = 0x6e000000;
constexpr uint32_t kFace = 0x2c; // the container's face in its object
constexpr uint32_t kBuilderFace = 0x20; // the cIGeometryBuilder subobject's offset

difftest::Context *gCtx;
bool gOriginal;

uint32_t Bits(float f)
{
    if (std::isnan(f)) return 0x7fc00000; // one NaN per case: its payload and sign are not the code's
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::string TextOf(const void *str)
{
    return str ? reinterpret_cast<const cRZString *>(str)->ToChar() : std::string("<null>");
}

std::vector<cIGeometryDataContainer *> gBoxes;

cIGeometryDataContainer *NewBox()
{
    cISGResource *r = cGeometryDataContainer::Create();
    void *out = nullptr;
    r->QueryInterface(cIGeometryDataContainer::kIid, &out);
    auto *face = static_cast<cIGeometryDataContainer *>(out);
    difftest::NotCounted quiet;
    drawfx::AddRange(reinterpret_cast<uint8_t *>(face) - kFace, cGeometryDataContainer::kSize,
                     kContainerTag | uint32_t(gBoxes.size()) << 12);
    gBoxes.push_back(face);
    return face;
}

// The framework object the behaviour slots reach (GetClassObject is never asked here).
struct FakeCom {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release() { return 1; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out)
    {
        drawfx::Note("COM", {classId, iid});
        *out = nullptr;
        return 0;
    }
};
FakeCom gCom;
void *__cdecl StandFramework() { return &gCom; }

// ---- a tape writer and reader ----
struct Token {
    char kind; // 'w' a word, 't' a text, 'i' a type-info record
    std::vector<uint32_t> words;
    std::string text;
};
std::vector<Token> gTape;

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

#define SGGB_BAD(n) \
    virtual uint32_t Bad##n() { drawfx::Note("bad slot " #n, n); return 0; }

struct TapeWriter {
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { Put("w SetUint8", {'w', {v & 0xff}, {}}); return 1; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t SetUint32(uint32_t v) { Put("w SetUint32", {'w', {v}, {}}); return 1; } // 10
    SGGB_BAD(11) SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14) SGGB_BAD(15) SGGB_BAD(16) SGGB_BAD(17) SGGB_BAD(18)
    SGGB_BAD(19) SGGB_BAD(20) SGGB_BAD(21) SGGB_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) // 23
    {
        Put("w WriteRZCharStr", {'t', {}, text ? text : "<null>"});
        return 1;
    }
    SGGB_BAD(24) SGGB_BAD(25) SGGB_BAD(26) SGGB_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

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
    SGGB_BAD(0) SGGB_BAD(1) SGGB_BAD(2) SGGB_BAD(3)
    virtual bool GetBool() { return WordOf(Take("r GetBool")) != 0; } // 4
    SGGB_BAD(5) SGGB_BAD(6) SGGB_BAD(7) SGGB_BAD(8) SGGB_BAD(9)
    virtual uint32_t GetUint32() { return WordOf(Take("r GetUint32")); } // 10
    SGGB_BAD(11) SGGB_BAD(12) SGGB_BAD(13) SGGB_BAD(14) SGGB_BAD(15) SGGB_BAD(16) SGGB_BAD(17) SGGB_BAD(18)
    SGGB_BAD(19) SGGB_BAD(20) SGGB_BAD(21)
    virtual bool GetGZStr(cIGZString *str) // 22
    {
        const Token *t = Take("r GetGZStr");
        std::string text;
        {
            difftest::NotCounted quiet;
            text = t ? t->text : std::string("tail");
        }
        str->FromChar(text.c_str());
        return t != nullptr;
    }
    SGGB_BAD(23) SGGB_BAD(24) SGGB_BAD(25)
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

#undef SGGB_BAD

ifaces::cIGZPersistWriter *Writer() { return reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter); }
ifaces::cIGZPersistReader *Reader() { return reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader); }

void ClearTape()
{
    difftest::NotCounted quiet;
    gTape.clear();
    gReader.next = 0;
}

// ---- the world ----
struct World {
    Fake listener[2]; // ids 500, 501: connected through the manager
    Fake extra[2];    // ids 510, 511: the extra listener SetListener stores
    sgfx::Manager manager;
};
World w;

constexpr sgfx::RegistryStatics kBuilderRegistry{0x10f60e8, 0x10f60f0, 0x10f60f8, 0x10f6100};
constexpr uint32_t kConnectionChanged = 0x5b51e806;
const uint32_t kEvents[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2], 0x5678};

struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106ab04;
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    sgfx::gPool.Reset(cells);
    for (std::string &text : sgfx::gPool.texts) std::string().swap(text);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.manager.Returns(9, seed % 3);
    sgfx::gResources.manager.Returns(14, (seed + 1) % 3);
    for (unsigned i = 0; i < 4; ++i) gNames[i].Reset<sgfx::kUnknown>(440 + i);
    gNameAt = 0;
    ClearTape();
    sgfx::MakeListener(w.listener[0], 500, true);
    sgfx::MakeListener(w.listener[1], 501, seed % 2 != 0);
    sgfx::MakeListener(w.extra[0], 510, true);
    sgfx::MakeListener(w.extra[1], 511, false);
    w.manager.Drop();
    w.manager.Use(cells, cGeometryBuilderBehaviorTraits::kEventManagerCell);
}

void NoteWorld()
{
    for (const Fake &l : w.listener) drawfx::Note("listener refs", {l.id, uint32_t(l.refs)});
    for (const Fake &l : w.extra) drawfx::Note("extra listener refs", {l.id, uint32_t(l.refs)});
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

struct SideScope {
    difftest::AllocationCounter counter;
    SideScope(difftest::Context &ctx, bool original)
    {
        gCtx = &ctx;
        gOriginal = original;
        difftest::NotCounted quiet;
        gBoxes.clear();
    }
    void End()
    {
        for (cIGeometryDataContainer *f : gBoxes) drawfx::Note("container released", f->Release());
        NoteWorld();
        drawfx::Note("blocks left", uint32_t(counter.Blocks()));
    }
};

// ---- one builder of the side being run, as its three faces (the test holds one reference on each) ----
struct Builder {
    cIGeometryBuilder *base = nullptr;
    cIGZPersistSerializable2 *persist = nullptr;
    cITangentSpaceBuilder *own = nullptr;
    uint8_t *object = nullptr;

    cIGeometryBuilder *OwnBase() const { return static_cast<cIGeometryBuilder *>(own); }
};

Builder Make()
{
    Builder b;
    cIGZUnknown *const made = gOriginal ? gCtx->Original<FactoryFn>(0xa86bf0)() : cTangentSpaceBuilder::Create();
    b.base = reinterpret_cast<cIGeometryBuilder *>(made);
    b.object = reinterpret_cast<uint8_t *>(made) - kBuilderFace;
    {
        difftest::NotCounted quiet;
        drawfx::AddRange(b.object, 0x30, kBuilderTag);
    }
    drawfx::Note("first count", b.base->AddRef());
    void *out = nullptr;
    drawfx::Note("query persist", {uint8_t(b.base->QueryInterface(cGeometryBuilder::kIidSerializable, &out)),
                                   drawfx::Norm(P(out))});
    b.persist = static_cast<cIGZPersistSerializable2 *>(out);
    out = nullptr;
    drawfx::Note("query own", {uint8_t(b.base->QueryInterface(cITangentSpaceBuilder::kIid, &out)),
                               drawfx::Norm(P(out))});
    b.own = static_cast<cITangentSpaceBuilder *>(out);
    return b;
}

void Drop(const Builder &b)
{
    drawfx::Note("persist release", b.persist->Release());
    drawfx::Note("own release", b.OwnBase()->Release());
    drawfx::Note("last release", b.base->Release());
}

// The object's words that are not vtables: the count, the extra listener, the vbtables' entries, the vtordisps, the
// name set's size.
void NoteWords(const Builder &b)
{
    auto word = [&](uint32_t off) { return *reinterpret_cast<const uint32_t *>(b.object + off); };
    drawfx::Call c;
    c.note = "words";
    c.words = {word(8), drawfx::Norm(word(0x10))};
    const auto *vbtable = reinterpret_cast<const int32_t *>(word(4));
    c.words.insert(c.words.end(), {uint32_t(vbtable[0]), uint32_t(vbtable[1]), uint32_t(vbtable[2])});
    const uint32_t ownFace = kBuilderFace + 8;
    const auto *ownVbtable = reinterpret_cast<const int32_t *>(word(ownFace + 4));
    c.words.insert(c.words.end(), {uint32_t(ownVbtable[0]), uint32_t(ownVbtable[1])});
    c.words.push_back(word(kBuilderFace - 4));
    c.words.push_back(word(ownFace - 4));
    c.words.push_back(word(0x18));
    drawfx::gLog.push_back(std::move(c));
}

const char *const kNames[] = {"body", "BODY", "Body", "hair_lod0", "", "tops", "a_group_name_longer_than_sixteen", "x"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

void WriteRead(const Builder &from, const Builder &into, std::mt19937 &rng)
{
    ClearTape();
    from.persist->Write(Writer());
    drawfx::Note("tape", uint32_t(gTape.size()));
    {
        difftest::NotCounted quiet;
        const uint32_t versions[] = {0, 1, 2, 3, 0xffffffffu};
        if (rng() % 3 == 0 && !gTape.empty()) gTape[0].words[1] = versions[rng() % 5];
        if (rng() % 4 == 0 && gTape.size() > 1) gTape[1].words[1] = versions[rng() % 5];
        // The name count is the word after the base's records: change the last count-like word in some seeds.
        for (Token &t : gTape)
            if (t.kind == 'w' && rng() % 12 == 0) t.words[0] = rng() % 3 == 0 ? 0xffffffffu : t.words[0] + rng() % 3;
        if (rng() % 6 == 0) gTape.resize(gTape.size() > 0 ? rng() % gTape.size() : 0); // a short tape
    }
    into.persist->Read(Reader());
    drawfx::Note("read to", uint32_t(gReader.next));
}

const uint32_t kIids[] = {0,          1,          cGeometryBuilder::kIidSerializable,   cGeometryBuilder::kIidBehavior,
                          0xfb859324, 0xbbb79c90, cITangentSpaceBuilder::kIid, 0xdd054221, 0x12345678};

void Operate(const Builder &b, const Builder &other, unsigned op, std::mt19937 &rng)
{
    switch (op) {
    case 0: {
        const uint32_t iid = kIids[rng() % 9];
        void *out = nullptr;
        void **const where = rng() % 8 ? &out : nullptr;
        const bool r = rng() % 2 ? b.base->QueryInterface(iid, where) : b.persist->QueryInterface(iid, where);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (!out || iid == 0xfb859324) break;
        if (iid == cITangentSpaceBuilder::kIid)
            static_cast<cIGeometryBuilder *>(static_cast<cITangentSpaceBuilder *>(out))->Release();
        else
            drawfx::Note("query release", static_cast<cIGZUnknown *>(out)->Release());
        break;
    }
    case 1:
        drawfx::Note("addref", b.persist->AddRef());
        drawfx::Note("release", b.base->Release());
        break;
    case 2: {
        cPersistTypeInfo info = b.persist->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 3:
        ClearTape();
        b.persist->Write(Writer());
        break;
    case 4: WriteRead(rng() % 2 ? other : b, b, rng); break;
    case 5: sgfx::NoteText("name", b.base->GetName()); break;
    case 6: {
        const unsigned k = rng() % 3;
        nGZEvent::cIAbstractBehaviorType *const listener =
            k < 2 ? reinterpret_cast<nGZEvent::cIAbstractBehaviorType *>(&w.extra[k]) : nullptr;
        drawfx::Note("set listener", drawfx::Norm(P(b.base->SetListener(listener))));
        break;
    }
    case 7: {
        cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
        if (rng() % 2)
            b.base->Connect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        else
            b.base->Disconnect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        break;
    }
    case 8:
        if (rng() % 2)
            b.base->NotifyListeners(kEvents[rng() % 4], rng(), nullptr);
        else
            drawfx::Note("asked", uint8_t(b.base->AskListeners(kEvents[rng() % 4], rng(), nullptr)));
        break;
    case 9:
        b.base->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {b.base->IsActive(), b.base->GetEventCount()});
        break;
    case 10: drawfx::Note("on query", uint8_t(b.base->OnQuery(kEvents[rng() % 4], rng(), nullptr))); break;
    case 11: {
        const nGZEvent::cEventAttribute a = b.base->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        nGZEvent::cAttributeIDRange ids = b.base->GetEventIds();
        unsigned steps = 0;
        while (steps < 20 && !ids.mCurrent->IsEqual(ids.mEnd)) {
            drawfx::Note("event id", ids.mCurrent->Get());
            ids.mCurrent->Next();
            ++steps;
        }
        drawfx::Note("event ids", {steps, ids.mCurrent->Release(), ids.mBegin->Release(), ids.mEnd->Release()});
        break;
    }
    default: // 12-15: AddName (the Write and Read of 3 and 4 show the set)
        b.own->AddName(kNames[rng() % kNameCount]);
        break;
    }
}

void RunSlots(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "tangent space", 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        const Builder b = Make();
        const Builder other = Make();
        for (unsigned k = rng() % 5; k; --k) b.own->AddName(kNames[rng() % kNameCount]);
        for (unsigned k = rng() % 5; k; --k) other.own->AddName(kNames[rng() % kNameCount]);
        NoteWords(b);
        for (int k = 0; k < 48; ++k) {
            const unsigned op = rng() % 16;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(b, other, op, rng);
            if (k % 8 == 7) NoteWords(b);
        }
        NoteWords(b);
        ClearTape();
        b.persist->Write(Writer());
        b.base->SetListener(nullptr);
        Drop(other);
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

// ---- synthetic geometry ----
constexpr uint32_t kPosition = 0x5b830781;
constexpr uint32_t kNormal = 0x3b83078b;
constexpr uint32_t kTexCoord = 0xbb8307ab;

float SmallFloat(std::mt19937 &rng)
{
    const int32_t i = int32_t(rng() % 41) - 20;
    return rng() % 2 ? float(i) : float(i) * 0.25f + 0.125f;
}

std::vector<uint16_t> Words(uint32_t n, uint32_t below, std::mt19937 &rng)
{
    std::vector<uint16_t> v(n);
    for (uint16_t &x : v) x = uint16_t(rng() % below);
    return v;
}

// A texture coordinate or normal component: a few special values among smallv ones, to reach the thresholds.
float Special(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return -1.0f;
    case 3: return std::bit_cast<float>(0x1e8637beu);     // the squared-length threshold itself
    case 4: return std::bit_cast<float>(0x1e8637bfu);     // just over
    case 5: return std::bit_cast<float>(0x1e8637bdu);     // just under
    case 6: return std::bit_cast<float>(0x2f03126fu);     // the triangle threshold
    case 7: return std::bit_cast<float>(0x2f031270u);
    case 8: return 1e-10f;
    default: return SmallFloat(rng);
    }
}

void DumpFloats(const char *label, cGeometryRange<float> &r)
{
    drawfx::Call c;
    c.note = label;
    uint32_t n = 0;
    while (!r.mCurrent->IsEqual(r.mEnd) && n < 2048) {
        c.words.push_back(Bits(r.mCurrent->Get()));
        r.mCurrent->Next();
        ++n;
    }
    c.words.push_back(n);
    c.words.push_back(r.mCurrent->Release());
    c.words.push_back(r.mBegin->Release());
    c.words.push_back(r.mEnd->Release());
    drawfx::gLog.push_back(std::move(c));
}

void DumpWords16(const char *label, cGeometryRange<uint16_t> &r)
{
    drawfx::Call c;
    c.note = label;
    uint32_t n = 0;
    while (!r.mCurrent->IsEqual(r.mEnd) && n < 512) {
        c.words.push_back(r.mCurrent->Get());
        r.mCurrent->Next();
        ++n;
    }
    c.words.push_back(n);
    c.words.push_back(r.mCurrent->Release());
    c.words.push_back(r.mBegin->Release());
    c.words.push_back(r.mEnd->Release());
    drawfx::gLog.push_back(std::move(c));
}

// Every element (header, floats) and every linkage (header, element list), through the container.
void DumpContainer(cIGeometryDataContainer *f)
{
    const uint32_t ne = f->CountElements(), nl = f->CountLinkages();
    drawfx::Note("counts", {ne, nl, f->CountGroups()});
    for (uint32_t i = 0; i < ne; ++i) {
        cGeometryElementHeader h{};
        f->GetElementHeader(&h, i);
        drawfx::Note("element", {h.mIndex, h.mNumber, h.mIdentity, h.mRepetition, h.mBlockFormat, h.mSetFormat});
        cGeometryRange<float> r;
        f->GetElementFloats(&r, i);
        DumpFloats("element floats", r);
    }
    for (uint32_t i = 0; i < nl; ++i) {
        cGeometryLinkageHeader h{};
        f->GetLinkageHeader(&h, i);
        drawfx::Note("linkage", {h.mIndex, h.mActiveCount, h.mReferencedSize});
        cGeometryRange<uint16_t> r;
        f->GetLinkageElements(&r, i);
        DumpWords16("linkage elements", r);
    }
}

// One element of a linkage: `count` vectors of `width` floats made by `gen`; its index is added to the linkage.
template <typename Gen>
uint16_t AddVectors(cIGeometryDataContainer *f, uint32_t link, uint32_t identity, uint32_t repetition, uint32_t blockFormat,
                    uint32_t setFormat, uint32_t count, uint32_t width, Gen gen)
{
    const uint32_t e = f->AddElement(identity, repetition, blockFormat, setFormat);
    std::vector<float> data(count * width);
    for (float &x : data) x = gen();
    static const float none = 0.0f;
    f->SetElementData(e, count, count ? data.data() : &none, uint32_t(data.size() * 4), nullptr, 0);
    f->AddLinkageElement(link, uint16_t(e));
    return uint16_t(e);
}

// 1-3 linkages of `nv` vertices each: a decoy first or not (the same identity with another repetition or block format),
// the positions, the normals, the texture coordinates (each present or not), a later decoy, and the three alias lists
// (values below the element counts, which are nv or fewer). Groups of every primitive type, names case-varied, a
// linkage that has no elements, indices below nv (multiples of 3).
void MakeGeometry(cIGeometryDataContainer *f, std::mt19937 &rng, bool &nan)
{
    nan = rng() % 16 == 0;
    const uint32_t links = 1 + rng() % 3;
    std::vector<uint32_t> sizes;
    for (uint32_t l = 0; l < links; ++l) {
        const uint32_t nv = 3 + rng() % 8;
        sizes.push_back(nv);
        f->AddLinkage();
        f->SetLinkageReferencedSize(l, rng() % 10 == 0 ? nv + 2 : nv);
        auto smallv = [&] {
            if (nan && rng() % 40 == 0) { // at most one NaN in a case
                nan = false;
                return std::numeric_limits<float>::quiet_NaN();
            }
            return SmallFloat(rng);
        };
        auto special = [&] { return Special(rng); };
        if (rng() % 4 == 0) AddVectors(f, l, kNormal, 0, 1, 0, nv, 3, smallv); // decoys: other block formats
        if (rng() % 4 == 0) AddVectors(f, l, kPosition, 1, 2, 0, nv, 3, smallv);
        if (rng() % 4 == 0) AddVectors(f, l, kTexCoord, 0, 2, 0, nv, 2, special);
        const uint32_t counts[3] = {nv - rng() % 2, nv - rng() % 2, nv - rng() % 2}; // never below nv - 1
        const uint32_t sets[3] = {uint32_t(rng() % 3), uint32_t(rng() % 3), uint32_t(rng() % 3)};
        if (rng() % 10 != 0) AddVectors(f, l, kPosition, 0, 2, sets[0], counts[0], 3, smallv);
        if (rng() % 10 != 0) {
            const bool unit = rng() % 2;
            const bool empty = rng() % 12 == 0;
            auto normal = [&, k = 0u]() mutable {
                const unsigned axis = k++ % 3;
                if (unit) return axis == rng() % 3 ? 1.0f : 0.0f;
                return rng() % 5 == 0 ? 0.0f : Special(rng);
            };
            AddVectors(f, l, kNormal, 0, 2, sets[1], empty ? 0 : counts[1], 3, normal);
        }
        if (rng() % 10 != 0) {
            // Sometimes from a handful of coordinates whose triangles have a texture-space determinant of exactly the
            // threshold (0,0), (1,0), (0,eps): the comparison's equality case.
            const bool few = rng() % 5 == 0;
            auto coordinate = [&, k = 0u, kind = 0u]() mutable {
                const unsigned component = k++ % 2;
                if (component == 0) kind = rng() % 4;
                static const float table[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, std::bit_cast<float>(0x2f03126fu)},
                                                  {0.0f, 1.0f}};
                return few ? table[kind][component] : Special(rng);
            };
            AddVectors(f, l, kTexCoord, 0, 1, sets[2], counts[2], 2, coordinate);
        }
        if (rng() % 4 == 0) AddVectors(f, l, kPosition, 0, 2, 0, nv, 3, smallv); // never reached: the first one wins
        for (uint32_t which = 0; which < 3; ++which) {
            // The list of set format `which` serves every element of that set format: values below all their counts.
            const std::vector<uint16_t> list = Words(nv, nv - 1, rng);
            f->SetLinkageAliases(l, which, list.data(), nv);
        }
    }
    const uint32_t groups = 1 + rng() % 6;
    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t link = rng() % links;
        const uint32_t prim = rng() % 5 ? 2 : rng() % 4;
        f->AddGroup(prim, kNames[rng() % kNameCount], link, int32_t(rng() % 4));
        const uint32_t count = rng() % 6 == 0 ? 0 : 3 * (1 + rng() % 5);
        if (count == 0) continue;
        const std::vector<uint16_t> list = Words(count, sizes[link], rng);
        f->SetGroupVertexIndices(g, list.data(), count);
    }
}

void RunBuild(difftest::Context &ctx)
{
    sgfx::RegistryScope registry(kBuilderRegistry);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "tangent space build", 160, BuildWorld, [&](bool original, std::mt19937 &rng) {
        SideScope side(ctx, original);
        cIGeometryDataContainer *f = NewBox();
        bool nan = false;
        MakeGeometry(f, rng, nan);
        DumpContainer(f);
        const Builder b = Make();
        const unsigned names = rng() % 5;
        for (unsigned k = 0; k < names; ++k) b.own->AddName(kNames[rng() % kNameCount]);
        NoteWords(b);
        const unsigned builds = 1 + (rng() % 5 == 0);
        for (unsigned k = 0; k < builds; ++k) {
            sgfx::MarkStep(ctx, k ? "second build" : "build");
            b.base->Build(f);
            DumpContainer(f);
        }
        if (rng() % 4 == 0) { // a container without linkages or groups: nothing happens
            cIGeometryDataContainer *empty = NewBox();
            if (rng() % 2) empty->AddLinkage();
            b.base->Build(empty);
            DumpContainer(empty);
        }
        Drop(b);
        side.End();
    });
    w.manager.Drop();
}

} // namespace

DIFF_TEST(sg_tangent_builder_slots, 0xa86bf0) { RunSlots(ctx); }

DIFF_TEST(sg_tangent_builder_build, 0xa86c80) { RunBuild(ctx); }
