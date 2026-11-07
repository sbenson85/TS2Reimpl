// Differential tests of nGZSceneGraph::cDataListExtension (lane OY, scenegraph D1a part 2; src/sg/cDataListExtension.h).
//
// Each side's extension comes from its own factory: the game's 0xa990b0 (new 0x30, the constructor 0xa98950) and ours
// Create. The list items are ours on both sides (lane OX's cDataListItem, verified): the COM director's framework object
// 0x1ae18d (ours: GetDirectorFrameworkObject) is stood in for on both sides, and its GetClassObject (slot 3) logs the
// class and interface ids and answers a new cDataListItem, counted for the caller and once more for the test. At the end
// of a side the test drops its own reference on every item made, in the order they were made, and notes each answer: an
// item the extension leaked or released once too often shows there.
//
//   DataListExtension_Slots      random calls through the extension's three faces: the cIDataListExtension face (every
//                                slot: setters of every type over names old and new, so a setter also changes a found
//                                item's type; Remove, RemoveAll, the list name; every getter, with names present, absent
//                                and of another type, the outs prefilled so what a getter writes shows; GetItem in and
//                                past range), cIExtension (QueryInterface over the iids, IsKindOf, the type name,
//                                GetDescription, Clone with the copy dumped then released; cExtension's part through
//                                the class's own thunks: owner, next, Connect / Disconnect with listeners on our
//                                connection manager, Notify / Ask, OnEvent, the event attribute with the registry's
//                                statics in place) and the referent face (its type-info record, GetOwnerInfo, Write
//                                through a tape writer). Every 8 calls and at the end the list is dumped
//                                through the extension's own getters (each item's name, type, value and the list's count
//                                on it). The last references then dropped (the extension destroyed through its own
//                                deleting destructor, the event manager's DisconnectAll before).
//   DataListExtension_ReadWrite  a random extension written to a tape (its record, cExtension's, the list item's), a
//                                fresh extension of the same side reading that tape (Read 0xa99330), then dumped and
//                                written again: the tapes and dumps compared between the sides.
//
// Left out, with the original's reasons: Clone while the list has no name (the copy 0xa98c90 calls SetName with the
// null an empty name reads back, which 0xac6ae0 dereferences: both sides fault); GetItemName / GetItemType past the end
// (0xa98eb0 / 0xa98ee0 have no range check: they call through the item 0xac6dd0 answers past the end, the list head's
// unwritten value word); names other than short texts for the list itself (a long name set over a short one is
// cDataListItem's name bug, whose heap pointer reads back per side).
//
// RVAs these tests reach (status entries cite them): the factory 0xa990b0, the constructors 0xa98950 / 0xa98840, the
// destructor 0xa98a50 and its deleting destructor 0xa98b00, the list copy 0xa98c90, Clone 0xa98c10, QueryInterface
// 0xa992e0, Read 0xa99330, GetTypeInfo 0xa99dd0, Write 0xa99fa0, IsKindOf 0xa99db0, the type name 0xa99030,
// GetDescription 0xa98f50, and the own face's bodies (slot order):
//   0xa99800 SetListName, 0xa99430 SetBool, 0xa99910 SetSint32, 0xa99620 SetFloat32, 0xa99af0 SetVector2,
//   0xa99be0 SetVector3, 0xa99a00 SetString, 0xa99820 SetQuaternion, 0xa99520 SetByteArray, 0xa99720 MakeList,
//   0xa993b0 Remove, 0xa993f0 RemoveAll, 0xa99220 GetListName, 0xa98b50 GetBool, 0xa99cd0 GetSint32,
//   0xa99050 GetFloat32, 0xa99eb0 GetVector2, 0xa99f30 GetVector3, 0xa99d30 GetString, 0xa99270 GetQuaternion,
//   0xa98bb0 GetByteArray, 0xa991b0 GetList, 0xa98eb0 GetItemName, 0xa98f00 GetType, 0xa98ee0 GetItemType,
//   0xa99160 GetItem, 0xa99250 GetCount;
// and every vtordisp thunk of the vtables 0xe7519c / 0xe7520c, which the game side calls through:
//   0xa98b20 0xa98b2b 0xa98b36 0xa98b41 0xa98ba5 0xa98c07 0xa98c7e 0xa98ea4 0xa98ec6 0xa98ece 0xa98f31 0xa98f3c
//   0xa9901f 0xa99036 0xa99041 0xa990a5 0xa99136 0xa99141 0xa9914c 0xa991a4 0xa9920c 0xa99228 0xa99233 0xa9923e
//   0xa9925b 0xa992d5 0xa9939a 0xa993a5 0xa993db 0xa9941a 0xa99425 0xa9950a 0xa99600 0xa9960b 0xa9970a 0xa997f4
//   0xa99808 0xa99813 0xa998fa 0xa999ea 0xa99ada 0xa99bca 0xa99cba 0xa99d23 0xa99d9a 0xa99e9a 0xa99f19.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"

#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cDataListExtension.h"

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

constexpr uint32_t kFactory = 0xa990b0;
constexpr uint32_t kObjectTag = 0x7b000000;
using FactoryFn = cIGZUnknown *(__cdecl *)();

difftest::Context *gCtx;
bool gOriginal;

// ---- the COM director: our items on both sides, each held once more by the test ----
std::vector<cIDataListItem *> gMade;

struct FakeCom {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release() { return 1; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out)
    {
        drawfx::Note("COM", {classId, iid});
        auto *item = static_cast<cIDataListItem *>(cDataListItem::Create());
        item->AddRef();
        item->AddRef();
        {
            difftest::NotCounted quiet;
            gMade.push_back(item);
        }
        *out = item;
        return 1;
    }
};
FakeCom gCom;
void *__cdecl StandFramework() { return &gCom; }

void ReleaseMade()
{
    for (cIDataListItem *item : gMade) drawfx::Note("item refs left", item->Release());
    difftest::NotCounted quiet;
    gMade.clear();
}

// ---- a tape writer and reader (the persist writer's and reader's slots the extension and its items call) ----
struct Token {
    char kind; // 'w' a word, 'a' a float array, 't' a text, 'b' bytes, 'i' a type-info record
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
    virtual uint32_t SetUint8(uint32_t v) { Put("w SetUint8", {'w', {v & 0xff}, {}}); return 1; } // 4
    SGDLE_BAD(5)
    virtual uint32_t SetByte(uint32_t v) { Put("w SetByte", {'w', {v & 0xff}, {}}); return 1; } // 6
    SGDLE_BAD(7) SGDLE_BAD(8)
    virtual uint32_t SetSint32(int32_t v) { Put("w SetSint32", {'w', {uint32_t(v)}, {}}); return 1; } // 9
    virtual uint32_t SetUint32(uint32_t v) { Put("w SetUint32", {'w', {v}, {}}); return 1; }        // 10
    virtual uint32_t SetFloat32(float v) { Put("w SetFloat32", {'w', {Bits(v)}, {}}); return 1; }   // 11
    SGDLE_BAD(12) SGDLE_BAD(13) SGDLE_BAD(14)
    virtual uint32_t SetVoid(const void *data, uint32_t size) // 15
    {
        Put("w SetVoid", {'b', {size}, std::string(static_cast<const char *>(data), size)});
        return 1;
    }
    SGDLE_BAD(16) SGDLE_BAD(17) SGDLE_BAD(18) SGDLE_BAD(19)
    virtual uint32_t SetFloat32Array(const float *data, uint32_t count) // 20
    {
        Token t{'a', {}, {}};
        for (uint32_t i = 0; i < count; ++i) t.words.push_back(Bits(data[i]));
        Put("w SetFloat32Array", std::move(t));
        return 1;
    }
    SGDLE_BAD(21) SGDLE_BAD(22)
    virtual uint32_t WriteRZCharStr(const char *text) { Put("w WriteRZCharStr", {'t', {}, text ? text : "<null>"}); return 1; } // 23
    virtual uint32_t WriteGZStr(const void *str) { Put("w WriteGZStr", {'t', {}, TextOf(str)}); return 1; }                    // 24
    SGDLE_BAD(25) SGDLE_BAD(26) SGDLE_BAD(27)
    virtual uint32_t WriteTypeInfo(const cPersistTypeInfo *info) // 28
    {
        Put("w WriteTypeInfo", {'i', {info->mWord4, info->mVersion}, TextOf(info->mName)});
        return 1;
    }
};
TapeWriter gWriter;

// The reader answers from gTape in order, one token a call whatever its kind (the tapes it reads are the writer's own).
// ReadTypeInfo answers a name fake, counted (each noted at the end).
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
    virtual bool GetBool() { return WordOf(Take("r GetBool")) != 0; } // 4
    SGDLE_BAD(5)
    virtual uint8_t GetUint8() { return uint8_t(WordOf(Take("r GetUint8"))); } // 6
    SGDLE_BAD(7) SGDLE_BAD(8)
    virtual int32_t GetSint32() { return int32_t(WordOf(Take("r GetSint32"))); } // 9
    virtual uint32_t GetUint32() { return WordOf(Take("r GetUint32")); }         // 10
    virtual float GetFloat32()                                                   // 11
    {
        const uint32_t w = WordOf(Take("r GetFloat32"));
        float f;
        std::memcpy(&f, &w, 4);
        return f;
    }
    SGDLE_BAD(12) SGDLE_BAD(13) SGDLE_BAD(14)
    virtual bool GetVoid(void *data, uint32_t size) // 15
    {
        const Token *t = Take("r GetVoid");
        auto *out = static_cast<uint8_t *>(data);
        for (uint32_t i = 0; i < size; ++i) out[i] = t && i < t->text.size() ? uint8_t(t->text[i]) : uint8_t(0x5a);
        return t != nullptr;
    }
    SGDLE_BAD(16) SGDLE_BAD(17) SGDLE_BAD(18) SGDLE_BAD(19)
    virtual void GetFloat32Array(float *data, uint32_t count) // 20
    {
        const Token *t = Take("r GetFloat32Array");
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t w = t && i < t->words.size() ? t->words[i] : 0x3f800000u + i;
            std::memcpy(&data[i], &w, 4);
        }
    }
    SGDLE_BAD(21)
    virtual bool GetGZStr(cIGZString *str) // 22
    {
        const Token *t = Take("r GetGZStr");
        std::string text;
        {
            difftest::NotCounted quiet;
            text = t ? t->text : std::string();
        }
        str->FromChar(text.c_str());
        return t != nullptr;
    }
    SGDLE_BAD(23) SGDLE_BAD(24) SGDLE_BAD(25)
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

// ---- a cIGZString as GetString (FromChar, slot 4) and GetDescription (operator=, slot 16) use it ----
inline constexpr drawfx::Slot kTextSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {2, drawfx::Ret::Int, false, "", ""},
    {1, drawfx::Ret::Int, true, "FromChar", "s"},        {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {3, drawfx::Ret::Int, false, "", ""},
    {2, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, false, "", ""},
    {2, drawfx::Ret::Int, false, "", ""},                {3, drawfx::Ret::Int, false, "", ""},
    {2, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, false, "", ""},
    {1, drawfx::Ret::Int, true, "Assign", "S"},
};
inline constexpr drawfx::Iface kText{"Text", 17, kTextSlots};
uint32_t TextFromChar(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("from char", reinterpret_cast<const char *>(uintptr_t(a[0])));
    drawfx::Note("empty text is the image's", uint32_t(a[0] == P(GameAddress<void>(cExtension::kEmptyText))));
    return P(&self);
}
uint32_t TextAssign(Fake &self, const uint32_t *a)
{
    // The source is a cRZString (the game's vtable on both sides: ours is the hook-shaped class).
    sgfx::NoteText("assigned", reinterpret_cast<const cRZString *>(uintptr_t(a[0]))->ToChar());
    return P(&self);
}

// ---- the world ----
struct World {
    Fake text;      // id 330
    Fake owner;     // id 300: a node whose referent face is `ownerRef`
    Fake ownerRef;  // id 310: its GetOwnerInfo (6) answers 0x600 + seed
    Fake next[3];   // ids 320-322: extensions for the node's list
    Fake listener[2];
    sgfx::Manager manager;
    uint32_t seed = 0;
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
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    w.seed = seed;
    w.text.Reset<kText>(330);
    w.text.On(4, &TextFromChar);
    w.text.On(16, &TextAssign);
    for (unsigned i = 0; i < 4; ++i) gNames[i].Reset<sgfx::kUnknown>(440 + i);
    gNameAt = 0;
    gReader.next = 0;
    {
        difftest::NotCounted quiet;
        gTape.clear();
        gMade.clear();
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
}

void NoteNames()
{
    for (const Fake &n : gNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
}

// One extension of the side being run, as its three faces (the test holds one reference on the extension and one on
// the cIDataListExtension face).
struct Extension {
    cIExtension *base = nullptr;
    cIDataListExtension *list = nullptr;
    cIGZPersistSerializableReferent *referent = nullptr;
};

Extension Make(bool original)
{
    Extension e;
    cIGZUnknown *const made = original ? gCtx->Original<FactoryFn>(kFactory)() : cDataListExtension::Create();
    e.base = static_cast<cIExtension *>(made);
    // The object: cIExtension at +0x20 in the game's layout (ours the same, or the normalised pointers below differ).
    drawfx::AddRange(reinterpret_cast<uint8_t *>(e.base) - 0x20, 0x30, kObjectTag);
    drawfx::Note("first count", e.base->AddRef());
    void *face = nullptr;
    drawfx::Note("query list face", {uint8_t(e.base->QueryInterface(cIDataListExtension::kIid, &face)), drawfx::Norm(P(face))});
    e.list = static_cast<cIDataListExtension *>(face);
    e.referent = e.base->GetPersistReferent();
    drawfx::Note("referent", drawfx::Norm(P(e.referent)));
    return e;
}

// Every reference the test holds dropped, each answer noted (the last destroys the extension).
void Drop(const Extension &e)
{
    drawfx::Note("list face release", e.list->Release());
    drawfx::Note("last release", e.base->Release());
}

// ---- dumps ----
void DumpItem(cIDataListItem *item, int depth);

void DumpValue(cIDataListItem *item, int depth)
{
    switch (item->GetType()) {
    case cIDataListItem::kBool: drawfx::Note("bool", item->GetBool()); break;
    case cIDataListItem::kSint32: drawfx::Note("sint32", uint32_t(item->GetSint32())); break;
    case cIDataListItem::kFloat32: drawfx::Note("float32", Bits(item->GetFloat32())); break;
    case cIDataListItem::kVector2:
    case cIDataListItem::kVector3:
    case cIDataListItem::kQuaternion: {
        float v[4] = {};
        const float *p = item->GetType() == cIDataListItem::kVector2   ? item->GetVector2(v)
                         : item->GetType() == cIDataListItem::kVector3 ? item->GetVector3(v)
                                                                       : item->GetQuaternion(v);
        drawfx::Note("floats", {Bits(p[0]), Bits(p[1]), Bits(p[2]), Bits(p[3])});
        break;
    }
    case cIDataListItem::kString: sgfx::NoteText("string", item->GetString()); break;
    case cIDataListItem::kByteArray: {
        uint32_t size = 0;
        const auto *data = static_cast<const uint8_t *>(item->GetByteArray(&size));
        drawfx::Call c;
        c.note = "bytes";
        c.words.push_back(size);
        for (uint32_t i = 0; i < size; ++i) c.words.push_back(data[i]);
        drawfx::gLog.push_back(std::move(c));
        break;
    }
    case cIDataListItem::kList: {
        const uint32_t count = item->GetCount();
        drawfx::Note("list count", count);
        for (uint32_t i = 0; i < count && i < 16; ++i) DumpItem(item->GetItem(int32_t(i)), depth + 1);
        break;
    }
    default: break;
    }
}

void DumpItem(cIDataListItem *item, int depth)
{
    drawfx::Note("item depth", uint32_t(depth));
    sgfx::NoteText("item name", item->GetName());
    drawfx::Note("item type", item->GetType());
    DumpValue(item, depth);
}

// The extension's list through its own getters: the name and count, then each item through GetItem (counted: the
// answer of the test's Release is the count the list and the test's hold leave).
void Dump(cIDataListExtension *list)
{
    sgfx::NoteText("list name", list->GetListName());
    const uint32_t count = list->GetCount();
    drawfx::Note("count", count);
    for (uint32_t i = 0; i <= count; ++i) {
        cIDataListItem *item = nullptr;
        const bool got = list->GetItem(i, &item);
        drawfx::Note("get item", {i, uint8_t(got)});
        if (!got) continue;
        drawfx::Note("item type by index", list->GetItemType(int32_t(i)));
        sgfx::NoteText("item name by index", list->GetItemName(int32_t(i)));
        DumpItem(item, 0);
        drawfx::Note("item release", item->Release());
    }
}

// ---- the operations ----
const char *const kNames[] = {"alpha", "beta", "gamma_long_name", "d", "missing"};
const char *const kTexts[] = {"text", "", "a longer string value", nullptr};
const uint32_t kIids[] = {cIDataListExtension::kIid, cIExtension::kIid, 0x5b0aaa74, 0xbb11a53e, 0x3b253dc7, 0xfb859324, 1,
                          0x12345678};

float Value(std::mt19937 &rng) { return float(int32_t(rng() % 4001) - 2000) / 8.0f; }
const char *Name(std::mt19937 &rng) { return kNames[rng() % 5]; }

void Set(cIDataListExtension *list, unsigned which, std::mt19937 &rng)
{
    const char *const name = Name(rng);
    drawfx::Note("set", which);
    switch (which) {
    // Short names only: a long name set over a short one is cDataListItem's name bug (its heap pointer reads back).
    case 0: list->SetListName(rng() % 2 ? "list" : "other"); break;
    case 1: { // the argument byte as a caller may leave it (the original stores and copies the byte, not a 0 / 1)
        const uint8_t bytes[] = {0, 1, 2, 0x80};
        bool value;
        std::memcpy(&value, &bytes[rng() % 4], 1);
        list->SetBool(name, value);
        break;
    }
    case 2: list->SetSint32(name, int32_t(rng())); break;
    case 3: list->SetFloat32(name, Value(rng)); break;
    case 4:
    case 5:
    case 7: {
        const float v[4] = {Value(rng), Value(rng), Value(rng), Value(rng)};
        if (which == 4) list->SetVector2(name, v);
        else if (which == 5) list->SetVector3(name, v);
        else list->SetQuaternion(name, v);
        break;
    }
    case 6: list->SetString(name, kTexts[rng() % 4]); break;
    case 8: {
        uint8_t bytes[6];
        const uint32_t size = rng() % 7;
        for (uint8_t &b : bytes) b = uint8_t(rng());
        list->SetByteArray(name, bytes, size);
        break;
    }
    default: list->MakeList(name); break;
    }
}

// A child item (from the COM, held like the extension's) added to a list item the test holds: a value, or a list with
// a value in it, so Clone and Write meet lists inside lists.
void AddChild(cIDataListItem *list, std::mt19937 &rng)
{
    void *made = nullptr;
    gCom.GetClassObject(cDataListItem::kClassId, cIDataListItem::kIid, &made);
    auto *const child = static_cast<cIDataListItem *>(made);
    child->SetName(rng() % 2 ? "child" : "x");
    if (rng() % 3 == 0) {
        child->MakeList();
        AddChild(child, rng);
    } else {
        child->SetSint32(int32_t(rng() % 1000));
    }
    drawfx::Note("child index", uint32_t(list->AddItem(child)));
    child->Release();
}

void Get(cIDataListExtension *list, unsigned which, std::mt19937 &rng)
{
    const char *const name = Name(rng);
    uint32_t out[5];
    for (uint32_t &o : out) o = 0xcccccccc;
    switch (which) {
    case 13: drawfx::Note("get bool", uint8_t(list->GetBool(name, reinterpret_cast<bool *>(out)))); break;
    case 14: drawfx::Note("get sint32", uint8_t(list->GetSint32(name, reinterpret_cast<int32_t *>(out)))); break;
    case 15: drawfx::Note("get float32", uint8_t(list->GetFloat32(name, reinterpret_cast<float *>(out)))); break;
    case 16: drawfx::Note("get vector2", uint8_t(list->GetVector2(name, reinterpret_cast<float *>(out)))); break;
    case 17: drawfx::Note("get vector3", uint8_t(list->GetVector3(name, reinterpret_cast<float *>(out)))); break;
    case 19: drawfx::Note("get quaternion", uint8_t(list->GetQuaternion(name, reinterpret_cast<float *>(out)))); break;
    case 18:
        drawfx::Note("get string", uint8_t(list->GetString(name, reinterpret_cast<cIGZString *>(&w.text))));
        break;
    case 20: {
        const void *data = reinterpret_cast<const void *>(uintptr_t(0xcccccccc));
        uint32_t size = 0xcccccccc;
        const bool got = list->GetByteArray(name, &data, &size);
        drawfx::Note("get byte array", {uint8_t(got), size});
        if (got) {
            drawfx::Call c;
            c.note = "byte array";
            for (uint32_t i = 0; i < size; ++i) c.words.push_back(static_cast<const uint8_t *>(data)[i]);
            drawfx::gLog.push_back(std::move(c));
        }
        return;
    }
    default: { // 21
        cIDataListItem *item = reinterpret_cast<cIDataListItem *>(uintptr_t(0xcccccccc));
        const bool got = list->GetList(name, &item);
        drawfx::Note("get list", uint8_t(got));
        if (got) {
            if (rng() % 2) AddChild(item, rng);
            DumpItem(item, 0);
            drawfx::Note("list release", item->Release());
        }
        return;
    }
    }
    drawfx::Note("out", {out[0], out[1], out[2], out[3], out[4]});
}

void Operate(const Extension &e, unsigned op, std::mt19937 &rng)
{
    cIDataListExtension *const list = e.list;
    if (op <= 9) {
        Set(list, op, rng);
        return;
    }
    if (op >= 13 && op <= 21) {
        Get(list, op, rng);
        return;
    }
    switch (op) {
    case 10: list->Remove(Name(rng)); break;
    case 11:
        if (rng() % 4 == 0) list->RemoveAll();
        break;
    case 12: sgfx::NoteText("list name", list->GetListName()); break;
    case 22: {
        const uint32_t count = list->GetCount();
        if (count) sgfx::NoteText("item name", list->GetItemName(int32_t(rng() % count)));
        break;
    }
    case 23: {
        const char *const name = Name(rng);
        drawfx::Note("type", list->GetType(name));
        break;
    }
    case 24: {
        const uint32_t count = list->GetCount();
        if (count) drawfx::Note("item type", list->GetItemType(int32_t(rng() % count)));
        break;
    }
    case 25: { // GetItem past the end too (an unsigned compare: -1 is past it)
        const uint32_t count = list->GetCount();
        const uint32_t index = rng() % 4 ? rng() % (count + 2) : 0xffffffffu;
        cIDataListItem *item = reinterpret_cast<cIDataListItem *>(uintptr_t(0xcccccccc));
        const bool got = list->GetItem(index, &item);
        drawfx::Note("get item", {index, uint8_t(got), got ? 0 : P(item)});
        if (got) drawfx::Note("item release", item->Release());
        break;
    }
    case 26: drawfx::Note("count", list->GetCount()); break;
    case 27: { // QueryInterface through either face
        const uint32_t iid = kIids[rng() % 8];
        void *out = nullptr;
        const bool r = rng() % 2 ? e.base->QueryInterface(iid, rng() % 8 ? &out : nullptr)
                                 : e.referent->QueryInterface(iid, rng() % 8 ? &out : nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
        if (out && iid == cIDataListExtension::kIid) // its own slots 0-2 are setters: released through its virtual base
            static_cast<cIDataListExtension *>(out)->Release();
        else if (out && iid != 0xfb859324)
            static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 28: {
        const uint32_t iid = kIids[rng() % 8];
        drawfx::Note("kind of", {iid, uint8_t(e.base->IsKindOf(iid))});
        break;
    }
    case 29:
        sgfx::NoteText("type name", e.base->GetTypeName());
        drawfx::Note("persistent", uint8_t(e.base->IsPersistent()));
        break;
    case 30: e.base->GetDescription(reinterpret_cast<cIGZString *>(&w.text)); break;
    case 31: { // Clone: the copy (count 0) counted, its list dumped, then released (destroyed)
        // Not while the list has no name: the copy 0xa98c90 names the new list with SetName(GetName()), and an empty
        // name reads back null, which the item's SetName (0xac6ae0) dereferences (the original faults the same way).
        if (!list->GetListName()) {
            drawfx::Note("clone skipped", 0);
            break;
        }
        cIExtension *const copy = e.base->Clone();
        drawfx::Note("clone", uint32_t(copy != nullptr));
        drawfx::Note("clone count", copy->AddRef());
        void *face = nullptr;
        copy->QueryInterface(cIDataListExtension::kIid, &face);
        Dump(static_cast<cIDataListExtension *>(face));
        static_cast<cIDataListExtension *>(face)->Release(); // through its virtual base (the face has no IUnknown slots)
        drawfx::Note("clone release", copy->Release());
        break;
    }
    case 32: {
        cPersistTypeInfo info = e.referent->GetTypeInfo();
        sgfx::NoteTypeInfo("type info", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 33:
        e.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        break;
    // cExtension's part through the class's own thunks (the game's vtable 0xe7519c; ours our compiler's).
    case 34: {
        cIObjectGraphNode *owner = rng() % 3 ? reinterpret_cast<cIObjectGraphNode *>(&w.owner) : nullptr;
        drawfx::Note("set owner", drawfx::Norm(P(e.base->SetOwner(owner))));
        drawfx::Note("owner", {drawfx::Norm(P(e.base->GetOwner())), drawfx::Norm(P(e.base->GetOwnerConst())),
                               drawfx::Norm(e.referent->GetOwnerInfo())});
        break;
    }
    case 35: {
        const unsigned k = rng() % 4;
        cIExtension *const next = k < 3 ? reinterpret_cast<cIExtension *>(&w.next[k]) : nullptr;
        const bool changes = next != e.base->GetNext();
        const uint32_t r = e.base->SetNext(next);
        drawfx::Note("set next", {uint32_t(changes), changes ? r : 0});
        drawfx::Note("next", {drawfx::Norm(P(e.base->GetNext())), drawfx::Norm(P(e.base->GetNextConst()))});
        for (const Fake &n : w.next) drawfx::Note("next refs", {n.id, uint32_t(n.refs)});
        break;
    }
    case 36: {
        cIGZUnknown *const listener = reinterpret_cast<cIGZUnknown *>(&w.listener[rng() % 2]);
        if (rng() % 2)
            e.base->Connect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        else
            e.base->Disconnect(kEvents[rng() % 4], listener, 0x500 + rng() % 2);
        break;
    }
    case 37:
        if (rng() % 2)
            e.base->NotifyListeners(kEvents[rng() % 4], rng(), nullptr);
        else
            drawfx::Note("asked", uint8_t(e.base->AskListeners(kEvents[rng() % 4], rng(), nullptr)));
        break;
    case 38:
        e.base->OnEvent(rng() % 2 ? kConnectionChanged : 0x1234, rng(), nullptr, rng());
        drawfx::Note("active", {e.base->IsActive(), e.base->GetEventCount()});
        break;
    default: { // 39
        const nGZEvent::cEventAttribute a = e.base->GetEventAttribute(kEvents[rng() % 4]);
        drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        break;
    }
    }
}

} // namespace

// Random calls through the three faces of each side's extension.
DIFF_TEST(DataListExtension_Slots, 0xa990b0)
{
    gCtx = &ctx;
    sgfx::RegistryScope registry(kExtensionRegistry);
    shape::StandIn registryStand{0x1485f0, &nGZEvent::cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<pdfx::kBaseRegistryMap>};
    MissingRecordScope missing;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "slots", 64, BuildWorld, [&](bool original, std::mt19937 &rng) {
        gOriginal = original;
        const Extension e = Make(original);
        // Most seeds start named (Clone needs a name: case 31) and with an "inner" list holding children, so a clone
        // copies lists inside lists.
        if (rng() % 4) e.list->SetListName("list");
        if (rng() % 2) {
            e.list->MakeList("inner");
            cIDataListItem *inner = nullptr;
            if (e.list->GetList("inner", &inner)) {
                for (unsigned k = 1 + rng() % 3; k; --k) AddChild(inner, rng);
                inner->Release();
            }
        }
        for (int k = 0; k < 48; ++k) {
            const unsigned op = rng() % 40;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            Operate(e, op, rng);
            if (k % 8 == 7) Dump(e.list);
        }
        Dump(e.list);
        Drop(e);
        NoteWorld();
        ReleaseMade();
        NoteNames();
    });
    w.manager.Drop();
}

// A written extension read back by a fresh one of the same side, dumped and written again.
DIFF_TEST(DataListExtension_ReadWrite, 0xa99330)
{
    gCtx = &ctx;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "read write", 48, BuildWorld, [&](bool original, std::mt19937 &rng) {
        gOriginal = original;
        const Extension source = Make(original);
        const int sets = 2 + int(rng() % 12);
        for (int k = 0; k < sets; ++k) Set(source.list, rng() % 10, rng);
        if (rng() % 2) { // items inside a list item
            source.list->MakeList("inner");
            cIDataListItem *inner = nullptr;
            if (source.list->GetList("inner", &inner)) {
                for (unsigned k = 1 + rng() % 3; k; --k) AddChild(inner, rng);
                inner->Release();
            }
        }
        source.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        const size_t written = gTape.size();
        drawfx::Note("tape", uint32_t(written));
        const Extension copy = Make(original);
        copy.referent->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&gReader));
        drawfx::Note("read to", uint32_t(gReader.next));
        Dump(copy.list);
        copy.referent->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&gWriter));
        Drop(copy);
        Drop(source);
        ReleaseMade();
        NoteNames();
    });
    w.manager.Drop();
}
