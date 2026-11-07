// Differential tests for nGZSceneGraph::cDataListItem (lane OX, src/sg/cDataListItem.*).
//
// Each side's items come from its own factory: the game's 0xac6d50 (new and the constructor 0xac6a80), ours Create. The
// COM director's framework object 0x1ae18d (ours: GetDirectorFrameworkObject) is stood in for on both sides: its
// GetClassObject (slot 3) makes an item of the side being run, counted once, and logs the class and interface ids (Read,
// ReadStream and Parse make their list children through it).
//
// SG_DataListItem_Slots: a seeded run of slot calls on six named items per side (each held once by the test), the same
// on both: every setter (a byte array also over one of the same size, a string over a string), MakeList, AddItem
// (an item only into one before it, so lists form no cycle), RemoveAt (in range, past the end, negative), Remove (in the
// list or not), SetName (short and past the game's 8-byte inline name), SetString(null), and after each the answers of
// GetType / GetName and the getter of the type; GetCount for lists and byte arrays; GetItem / GetItemAt in range (as
// which item), IndexOf and FindByName (present and not), QueryInterface (kIid, kClassId, 1, other). The value words
// read by another type's getter (GetCount of a vector, GetSint32 of a float, ...) are compared only after a quaternion
// filled all four words and while no string, byte array or list replaced the first: the constructor leaves the words
// unwritten (0xac6a80 writes the name, type and count only) and Clear 0xac6c30 leaves a freed pointer in them. Each value
// operation's heap balance (blocks allocated minus freed) is compared too: a type change frees the old string, byte array
// or list (its items released). Every few operations each item is written
// through all three forms (Write 6 through a fake persist writer, WriteStream 8 through a fake stream, Describe 10 at an
// indent of 0-2), each a tape of calls compared between the sides. At the end the test's references are released (the
// answers compared).
// SG_DataListItem_Read: each item's Write tape from the game side read back by a fresh item on each side (Read 7), then
// that item's Write tape and the reader calls compared; also with type bytes changed to 0-10 (unknown types, a list
// count read from a value word) where only Write is compared after.
// SG_DataListItem_ReadStream: the same with WriteStream's tape and ReadStream 9 (types changed alike).
// SG_DataListItem_Parse: Describe's text, split at its line ends, parsed back (Parse 11) line by line; then generated
// lines that fail or pass each check (unknown type words, no name, no "=", a wrong value, quotes missing, byte values
// past 0xff, a line feed at the end, counts of children). Parse's answer and the item's three tapes compared.
//
// Left out, with the original's reasons: a null name or a null string value through WriteStream / Describe (the
// original builds a cRZString from them with the library string constructor, which faults on null) and a null string
// value through Write (the same); GetItem past the end (0xac6dd0 answers the list head's value word, which the list
// never writes); a negative byte count in Parse (0xac6e50 sizes a std::vector by it: the library's length error).
//
// RVAs this file verifies: the factory 0xac6d50 and the constructor 0xac6a80, 0xac6ac0, 0xac6e20, 0x146a40 (AddRef),
// 0xac7cb0, 0xac6e00, 0xac8910, 0xac7ef0, 0xac6ae0, 0x145770, 0xac86d0, 0xac7950, 0xac8490, 0xac75a0, 0xac8060, 0xac6e50,
// 0xac8000, 0xac7e10, 0x16db30, 0xac7f30, 0x149d00, 0xac7ea0, 0x410160, 0xac7fa0, 0xac6d90, 0xac7fd0, 0xac6db0, 0xac7f00,
// 0x49e6e0, 0xac7f50, 0xac7e30, 0xac6d80, 0xac7ed0, 0xac6c70, 0xac6dd0, 0xac6d20, 0xac6ca0, 0xac6b60, 0xac7ce0,
// 0xac7d70, 0xac6e10, 0xac6c30; the name's helpers 0xac6a70 (its constructor, run by every game-side item's constructor)
// and 0xac6ab0 (Free, the destructor's tail: every game-side item released here runs it).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cDataListItem.h"

#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using nGZSceneGraph::cDataListItem;
using nGZSceneGraph::cIDataListItem;

namespace {

constexpr uint32_t kFactory = 0xac6d50;
constexpr int kItems = 6;

std::string Fmt(const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// A cIGZString's text (both sides pass cRZStrings with the game's vtable).
std::string TextOf(void *str) { return str ? reinterpret_cast<cRZString *>(str)->ToChar() : std::string("<null>"); }
// Sets a cIGZString's text through its own FromChar (slot 4).
void SetText(void *str, const std::string &text)
{
    using Fn = void(__thiscall *)(void *, const char *);
    reinterpret_cast<Fn>((*reinterpret_cast<void ***>(str))[4])(str, text.c_str());
}

// ---- the side being run ----
difftest::Context *gCtx = nullptr;
bool gGame = false;
std::vector<std::string> *gLog = nullptr; // the running side's log
uint32_t gComCalls = 0;

void Log(const std::string &line)
{
    difftest::NotCounted quiet;
    gLog->push_back(line);
}

cIDataListItem *NewItem()
{
    void *item = gGame ? gCtx->Original<void *(__cdecl *)()>(kFactory)() : static_cast<void *>(cDataListItem::Create());
    return static_cast<cIDataListItem *>(static_cast<cIGZUnknown *>(item));
}

// The framework object: GetClassObject makes the running side's item, counted once.
struct FakeCom {
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release() { return 1; }
    virtual uint32_t GetClassObject(uint32_t classId, uint32_t iid, void **out)
    {
        ++gComCalls;
        Log(Fmt("COM %08x %08x", classId, iid));
        cIDataListItem *item = NewItem();
        item->AddRef();
        *out = item;
        return 1;
    }
};
FakeCom gCom;
void *__cdecl StandFramework() { return &gCom; }

// ---- tapes ----
struct Token {
    char kind;          // 'w' a word, 'a' a float array, 't' a text, 'b' bytes
    std::vector<uint32_t> words;
    std::string text;
};
using Tape = std::vector<Token>;

std::string Show(const Token &t)
{
    std::string s(1, t.kind);
    for (uint32_t w : t.words) s += Fmt(" %08x", w);
    if (t.kind == 't' || t.kind == 'b') {
        s += " '";
        for (unsigned char c : t.text) s += (c >= 0x20 && c < 0x7f) ? std::string(1, char(c)) : Fmt("\\x%02x", c);
        s += "'";
    }
    return s;
}

#define SGDL_BAD(n) \
    virtual void Bad##n() { Log("BAD SLOT " #n); }

// A cIGZPersistWriter: slots 4, 6, 9, 10, 11, 15, 20, 23, 24 log their arguments to `tape` (the writer's answers are
// not read by the item).
struct FakeWriter {
    Tape tape;
    std::vector<std::string> kinds;
    void Word(const char *kind, uint32_t w)
    {
        difftest::NotCounted quiet;
        kinds.push_back(kind);
        tape.push_back({'w', {w}, {}});
    }
    void Text(const char *kind, std::string text)
    {
        difftest::NotCounted quiet;
        kinds.push_back(kind);
        tape.push_back({'t', {}, std::move(text)});
    }
    SGDL_BAD(0) SGDL_BAD(1) SGDL_BAD(2) SGDL_BAD(3)
    virtual uint32_t SetUint8(uint32_t v) { Word("SetUint8", v & 0xff); return 1; } // 4
    SGDL_BAD(5)
    virtual uint32_t SetByte(uint32_t v) { Word("SetByte", v & 0xff); return 1; } // 6
    SGDL_BAD(7) SGDL_BAD(8)
    virtual uint32_t SetSint32(int32_t v) { Word("SetSint32", uint32_t(v)); return 1; } // 9
    virtual uint32_t SetUint32(uint32_t v) { Word("SetUint32", v); return 1; }         // 10
    virtual uint32_t SetFloat32(float v) { Word("SetFloat32", Bits(v)); return 1; }    // 11
    SGDL_BAD(12) SGDL_BAD(13) SGDL_BAD(14)
    virtual uint32_t SetVoid(void *data, uint32_t size) // 15
    {
        difftest::NotCounted quiet;
        kinds.push_back("SetVoid");
        tape.push_back({'b', {size}, std::string(static_cast<const char *>(data), size)});
        return 1;
    }
    SGDL_BAD(16) SGDL_BAD(17) SGDL_BAD(18) SGDL_BAD(19)
    virtual uint32_t SetFloat32Array(float *data, uint32_t count) // 20
    {
        difftest::NotCounted quiet;
        kinds.push_back("SetFloat32Array");
        Token t{'a', {}, {}};
        for (uint32_t i = 0; i < count; ++i) t.words.push_back(Bits(data[i]));
        tape.push_back(t);
        return 1;
    }
    SGDL_BAD(21) SGDL_BAD(22)
    virtual uint32_t WriteRZCharStr(char *text) { Text("WriteRZCharStr", text ? text : "<null>"); return 1; } // 23
    virtual uint32_t WriteGZStr(void *str) { Text("WriteGZStr", TextOf(str)); return 1; }                   // 24
};

// A cIGZPersistReader answering from a tape, one token a call whatever its kind (a word asked of a text answers its
// length, a text asked of a word answers "w<word>"); past the end, zeros and empty texts. Counts (GetUint32) above 63
// answer their value modulo 8, so changed tapes stay small.
struct FakeReader {
    Tape tape;
    size_t next = 0;
    std::vector<std::string> kinds;
    const Token *Take(const char *kind)
    {
        difftest::NotCounted quiet;
        kinds.push_back(kind);
        return next < tape.size() ? &tape[next++] : nullptr;
    }
    static uint32_t WordOf(const Token *t)
    {
        if (t == nullptr) return 0;
        if (!t->words.empty()) return t->words[0];
        return uint32_t(t->text.size());
    }
    SGDL_BAD(0) SGDL_BAD(1) SGDL_BAD(2) SGDL_BAD(3)
    virtual bool GetBool() { return WordOf(Take("GetBool")) != 0; } // 4
    SGDL_BAD(5)
    virtual uint8_t GetUint8() { return uint8_t(WordOf(Take("GetUint8"))); } // 6
    SGDL_BAD(7) SGDL_BAD(8)
    virtual int32_t GetSint32() { return int32_t(WordOf(Take("GetSint32"))); } // 9
    virtual uint32_t GetUint32()                                               // 10
    {
        const uint32_t w = WordOf(Take("GetUint32"));
        return w < 64 ? w : w % 8;
    }
    virtual float GetFloat32() // 11
    {
        const uint32_t w = WordOf(Take("GetFloat32"));
        float f;
        std::memcpy(&f, &w, 4);
        return f;
    }
    SGDL_BAD(12) SGDL_BAD(13) SGDL_BAD(14)
    virtual bool GetVoid(void *data, uint32_t size) // 15
    {
        const Token *t = Take("GetVoid");
        auto *out = static_cast<uint8_t *>(data);
        for (uint32_t i = 0; i < size; ++i) out[i] = t && i < t->text.size() ? uint8_t(t->text[i]) : uint8_t(0x5a);
        return t != nullptr;
    }
    SGDL_BAD(16) SGDL_BAD(17) SGDL_BAD(18) SGDL_BAD(19)
    virtual void GetFloat32Array(float *data, uint32_t count) // 20
    {
        const Token *t = Take("GetFloat32Array");
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t w = t && i < t->words.size() ? t->words[i] : 0x3f800000u + i;
            std::memcpy(&data[i], &w, 4);
        }
    }
    SGDL_BAD(21)
    virtual bool GetGZStr(void *str) // 22
    {
        const Token *t = Take("GetGZStr");
        std::string text = t == nullptr ? std::string() : t->kind == 't' ? t->text : Fmt("w%u", WordOf(t));
        difftest::NotCounted quiet;
        SetText(str, text);
        return t != nullptr;
    }
};

// A cIGZOStream: slots 5, 8, 9, 12, 14, 15 logged to `tape`, and rendered as text (`text`) for Parse.
struct FakeOStream {
    Tape tape;
    std::string text;
    void Word(char kind, uint32_t w, const std::string &shown)
    {
        difftest::NotCounted quiet;
        Token t{'w', {w}, {}};
        t.text = std::string(1, kind);
        tape.push_back(t);
        text += shown;
    }
    SGDL_BAD(0) SGDL_BAD(1) SGDL_BAD(2) SGDL_BAD(3) SGDL_BAD(4)
    virtual uint8_t SetUint8(uint8_t v) { Word('8', v, Fmt("%u", v)); return 1; } // 5
    SGDL_BAD(6) SGDL_BAD(7)
    virtual uint8_t SetSint32(int32_t v) { Word('i', uint32_t(v), Fmt("%d", v)); return 1; } // 8
    virtual uint8_t SetUint32(uint32_t v) { Word('u', v, Fmt("%u", v)); return 1; }          // 9
    SGDL_BAD(10) SGDL_BAD(11)
    virtual uint8_t SetFloat32(float v) { Word('f', Bits(v), Fmt("%g", double(v))); return 1; } // 12
    SGDL_BAD(13)
    virtual uint8_t SetRZCharStr(const char *s) // 14
    {
        difftest::NotCounted quiet;
        tape.push_back({'t', {}, s ? s : "<null>"});
        text += s ? s : "<null>";
        return 1;
    }
    virtual uint8_t SetGZStr(void *str) // 15
    {
        difftest::NotCounted quiet;
        std::string s = TextOf(str);
        tape.push_back({'t', {1}, s});
        text += s;
        return 1;
    }
};

// A cIGZIStream answering from a tape like FakeReader (GetGZStr: a text token's text, a word "w<word>"; past the end
// false and the string untouched). Counts as FakeReader's.
struct FakeIStream {
    Tape tape;
    size_t next = 0;
    std::vector<std::string> kinds;
    const Token *Take(const char *kind)
    {
        difftest::NotCounted quiet;
        kinds.push_back(kind);
        return next < tape.size() ? &tape[next++] : nullptr;
    }
    static uint32_t WordOf(const Token *t) { return FakeReader::WordOf(t); }
    SGDL_BAD(0) SGDL_BAD(1) SGDL_BAD(2) SGDL_BAD(3) SGDL_BAD(4)
    virtual uint8_t GetUint8(uint8_t *out) // 5
    {
        const Token *t = Take("GetUint8");
        *out = uint8_t(WordOf(t));
        return t != nullptr;
    }
    SGDL_BAD(6) SGDL_BAD(7)
    virtual uint8_t GetSint32(int32_t *out) // 8
    {
        const Token *t = Take("GetSint32");
        *out = int32_t(WordOf(t));
        return t != nullptr;
    }
    virtual uint8_t GetUint32(uint32_t *out) // 9
    {
        const Token *t = Take("GetUint32");
        const uint32_t w = WordOf(t);
        *out = w < 64 ? w : w % 8;
        return t != nullptr;
    }
    SGDL_BAD(10) SGDL_BAD(11)
    virtual uint8_t GetFloat32(float *out) // 12
    {
        const Token *t = Take("GetFloat32");
        const uint32_t w = WordOf(t);
        std::memcpy(out, &w, 4);
        return t != nullptr;
    }
    SGDL_BAD(13) SGDL_BAD(14)
    virtual bool GetGZStr(void *str) // 15
    {
        const Token *t = Take("GetGZStr");
        if (t == nullptr) return false;
        std::string text = t->kind == 't' ? t->text : Fmt("w%u", WordOf(t));
        difftest::NotCounted quiet;
        SetText(str, text);
        return true;
    }
};

// ---- one side's run ----
struct Item {
    cIDataListItem *p = nullptr;
    bool wordsKnown = false; // all four value words written by this run (a quaternion since the last pointer value)
    size_t nameLength = 0;
};

struct Side {
    std::vector<std::string> log;
    Item items[kItems];
};

int IndexIn(const Side &side, cIDataListItem *p)
{
    for (int i = 0; i < kItems; ++i)
        if (side.items[i].p == p) return i;
    return -1;
}

std::string RandomName(std::mt19937 &rng)
{
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    const uint32_t length = rng() % 4 == 0 ? 8 + rng() % 16 : 1 + rng() % 7;
    std::string s;
    for (uint32_t i = 0; i < length; ++i) s += kChars[rng() % (sizeof(kChars) - 1)];
    return s;
}

std::string RandomText(std::mt19937 &rng)
{
    static const char kChars[] = "abc xyz'=,.-0123456789";
    const uint32_t length = rng() % 3 == 0 ? 0 : rng() % 30;
    std::string s;
    for (uint32_t i = 0; i < length; ++i) s += kChars[rng() % (sizeof(kChars) - 1)];
    return s;
}

float RandomFloat(std::mt19937 &rng) { return float(int32_t(rng() % 200001) - 100000) / 100.0f; }

// Returns whether every item written has a name (WriteStream and Describe need one: see the file comment).
bool NoteWrite(cIDataListItem *item, const char *what)
{
    FakeWriter writer;
    item->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&writer));
    std::string s = Fmt("%s Write:", what);
    bool named = true;
    for (size_t i = 0; i < writer.tape.size(); ++i) {
        s += " " + writer.kinds[i] + "=" + Show(writer.tape[i]);
        if (writer.kinds[i] == "WriteRZCharStr" && writer.tape[i].text == "<null>") named = false;
    }
    Log(s);
    return named;
}

void NoteStream(cIDataListItem *item, const char *what)
{
    FakeOStream stream;
    item->WriteStream(reinterpret_cast<ifaces::cIGZOStream *>(&stream));
    std::string s = Fmt("%s WriteStream:", what);
    for (const Token &t : stream.tape) s += " " + t.text.substr(0, t.kind == 'w' ? 1 : std::string::npos) + "=" + Show(t);
    Log(s);
}

void NoteDescribe(cIDataListItem *item, int32_t indent, const char *what)
{
    FakeOStream stream;
    item->Describe(reinterpret_cast<ifaces::cIGZOStream *>(&stream), indent);
    std::string s = Fmt("%s Describe(%d):", what, indent);
    for (const Token &t : stream.tape) s += " " + Show(t);
    Log(s);
}

void NoteAll(cIDataListItem *item, const char *what, int32_t indent)
{
    if (!NoteWrite(item, what)) return;
    NoteStream(item, what);
    NoteDescribe(item, indent, what);
}

// The answers of the queries on item i.
void NoteState(Side &side, int i, std::mt19937 &rng)
{
    Item &it = side.items[i];
    cIDataListItem *p = it.p;
    const uint32_t type = p->GetType();
    const char *name = p->GetName();
    const uint32_t refs = p->AddRef();
    const uint32_t after = p->Release();
    std::string s = Fmt("item %d refs %u/%u type %u name %s", i, refs, after, type, name ? name : "<null>");
    switch (type) {
    case cIDataListItem::kBool: s += Fmt(" bool %d", int(p->GetBool())); break;
    case cIDataListItem::kSint32: s += Fmt(" s32 %d", p->GetSint32()); break;
    case cIDataListItem::kFloat32: s += Fmt(" f32 %08x", Bits(p->GetFloat32())); break;
    case cIDataListItem::kString: {
        const char *text = p->GetString();
        s += Fmt(" string %s", text ? (std::string("'") + text + "'").c_str() : "<null>");
        break;
    }
    case cIDataListItem::kByteArray: {
        uint32_t size = 0xdeadbeef;
        const auto *data = static_cast<const uint8_t *>(p->GetByteArray(&size));
        s += Fmt(" bytes %u count %u null %d:", size, p->GetCount(), int(data == nullptr));
        for (uint32_t k = 0; data && k < size; ++k) s += Fmt(" %02x", data[k]);
        break;
    }
    case cIDataListItem::kList: {
        const uint32_t count = p->GetCount();
        s += Fmt(" count %u:", count);
        for (uint32_t k = 0; k < count; ++k) {
            cIDataListItem *a = (k & 1) ? p->GetItemAt(int32_t(k)) : p->GetItem(int32_t(k));
            s += Fmt(" %d", IndexIn(side, a));
        }
        const int j = int(rng() % kItems);
        s += Fmt(" indexof(%d) %d", j, p->IndexOf(side.items[j].p));
        const char *other = side.items[rng() % kItems].p->GetName();
        s += Fmt(" find %d", p->FindByName(other ? other : "nobody"));
        s += Fmt(" find-none %d", p->FindByName("no such name"));
        if (count != 0) {
            const char *first = p->GetItem(0)->GetName();
            std::string flipped = first ? first : "";
            for (char &c : flipped) c = char(std::isupper(uint8_t(c)) ? std::tolower(uint8_t(c)) : std::toupper(uint8_t(c)));
            s += Fmt(" find-case %d", p->FindByName(flipped.c_str()));
        }
        break;
    }
    default: break;
    }
    // the vector getters, each only where the words it reads were written
    const uint32_t written = it.wordsKnown ? 4 : type == cIDataListItem::kVector2 ? 2 : type == cIDataListItem::kVector3 ? 3
                                               : type == cIDataListItem::kQuaternion ? 4 : 0;
    if (written >= 2) {
        float v2[2];
        s += Fmt(" r%d", p->GetVector2(v2) == v2 ? 1 : 0); // the getters return their out pointer (callers read eax)
        s += Fmt(" v2 %08x %08x", Bits(v2[0]), Bits(v2[1]));
    }
    if (written >= 3) {
        float v3[3];
        s += Fmt(" r%d", p->GetVector3(v3) == v3 ? 1 : 0);
        s += Fmt(" v3 %08x %08x %08x", Bits(v3[0]), Bits(v3[1]), Bits(v3[2]));
    }
    if (written >= 4) {
        float v[4];
        s += Fmt(" r%d", p->GetQuaternion(v) == v ? 1 : 0);
        s += Fmt(" v4 %08x %08x %08x %08x", Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3]));
    }
    if (it.wordsKnown) {
        s += Fmt(" as: bool %d s32 %d f32 %08x count %u", int(p->GetBool()), p->GetSint32(), Bits(p->GetFloat32()),
                 p->GetCount());
    }
    Log(s);
}

// SetName. A long name (8 or more characters) over a short one leaves the name in the state of 0xac6ae0's bug: the
// name read back starts in the item itself with the old name's first bytes, then the heap pointer's (different on each
// side, so not compared); the test notes that much, then empties the name and sets it again.
void SetName(Item &it, const std::string &name)
{
    it.p->SetName(name.c_str());
    if (name.size() >= 8 && it.nameLength >= 1 && it.nameLength <= 7) {
        const char *got = it.p->GetName();
        const char *self = reinterpret_cast<const char *>(it.p);
        const bool inside = got >= self && got < self + 0x24;
        std::string s = Fmt("broken name: at %d", inside ? int(got - self) : -1);
        for (size_t k = 0; inside && k < it.nameLength && k < 4; ++k) s += Fmt(" %02x", uint8_t(got[k]));
        Log(s);
        it.p->SetName("");
        Log(Fmt("emptied: %d", int(it.p->GetName() == nullptr)));
        it.p->SetName(name.c_str());
    }
    it.nameLength = name.size();
}

void RunSlots(difftest::Context &ctx, Side &side, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (int i = 0; i < kItems; ++i) {
        Item &it = side.items[i];
        it.p = NewItem();
        Log(Fmt("new %d: addref %u", i, it.p->AddRef()));
        SetName(it, RandomName(rng));
        if (rng() % 2) {
            float q[4] = {RandomFloat(rng), RandomFloat(rng), RandomFloat(rng), RandomFloat(rng)};
            it.p->SetQuaternion(q);
            it.wordsKnown = true;
        }
        NoteState(side, i, rng);
    }
    // QueryInterface
    for (uint32_t iid : {cIDataListItem::kIid, cDataListItem::kClassId, 1u, 0x12345678u, 0u}) {
        void *out = nullptr;
        const bool ok = side.items[0].p->QueryInterface(iid, &out);
        Log(Fmt("QI %08x: %d same %d", iid, int(ok), int(out == side.items[0].p)));
        if (ok) Log(Fmt("release %u", side.items[0].p->Release()));
    }
    const int ops = 60 + int(rng() % 60);
    for (int op = 0; op < ops; ++op) {
        const int i = int(rng() % kItems);
        Item &it = side.items[i];
        cIDataListItem *p = it.p;
        const uint32_t kind = rng() % 18;
        const bool isList = p->GetType() == cIDataListItem::kList;
        difftest::AllocationCounter heap;
        std::string what;
        switch (kind) {
        case 0: p->SetBool(rng() % 2 != 0); what = "SetBool"; break;
        case 1: p->SetSint32(int32_t(rng())); what = "SetSint32"; break;
        case 2: p->SetFloat32(RandomFloat(rng)); what = "SetFloat32"; break;
        case 3: {
            float v[2] = {RandomFloat(rng), RandomFloat(rng)};
            p->SetVector2(v);
            what = "SetVector2";
            break;
        }
        case 4: {
            float v[3] = {RandomFloat(rng), RandomFloat(rng), RandomFloat(rng)};
            p->SetVector3(v);
            what = "SetVector3";
            break;
        }
        case 5: {
            float v[4] = {RandomFloat(rng), RandomFloat(rng), RandomFloat(rng), RandomFloat(rng)};
            p->SetQuaternion(v);
            it.wordsKnown = true;
            what = "SetQuaternion";
            break;
        }
        case 6: {
            const std::string text = RandomText(rng);
            p->SetString(text.c_str());
            it.wordsKnown = false;
            what = "SetString";
            break;
        }
        case 7: {
            // a size the item already has, half the time when it holds bytes
            uint32_t size = rng() % 13;
            if (p->GetType() == cIDataListItem::kByteArray && rng() % 2) p->GetByteArray(&size);
            std::vector<uint8_t> bytes(size + 1);
            for (auto &b : bytes) b = uint8_t(rng());
            p->SetByteArray(bytes.data(), size);
            it.wordsKnown = false;
            what = Fmt("SetByteArray %u", size);
            break;
        }
        case 8: p->MakeList(); it.wordsKnown = false; what = "MakeList"; break;
        case 9:
        case 10:
        case 11: {
            if (i == kItems - 1) break;
            const int j = i + 1 + int(rng() % uint32_t(kItems - 1 - i));
            const int32_t index = p->AddItem(side.items[j].p);
            it.wordsKnown = false;
            what = Fmt("AddItem %d: %d", j, index);
            break;
        }
        case 12: {
            if (!isList) break;
            const uint32_t count = p->GetCount();
            const uint32_t pick = rng() % 4;
            const int32_t index = pick == 0 ? -1 - int32_t(rng() % 3) : pick == 1 ? int32_t(count + rng() % 3)
                                                                                    : int32_t(rng() % (count + 1));
            p->RemoveAt(index);
            what = Fmt("RemoveAt %d", index);
            break;
        }
        case 13: {
            if (!isList) break;
            const int j = int(rng() % kItems);
            p->Remove(side.items[j].p);
            what = Fmt("Remove %d", j);
            break;
        }
        case 14: {
            heap.Stop();
            SetName(it, RandomName(rng));
            what = "SetName";
            break;
        }
        case 15: {
            heap.Stop();
            // SetString(null), then a number again (a null string is not written: see the file comment)
            p->SetString(nullptr);
            Log(Fmt("null string: type %u null %d", p->GetType(), int(p->GetString() == nullptr)));
            p->SetSint32(int32_t(rng()));
            it.wordsKnown = false;
            what = "SetString(null)";
            break;
        }
        case 16: {
            heap.Stop();
            // an empty name answers null; then a name again
            SetName(it, "");
            Log(Fmt("empty name: %d", int(p->GetName() == nullptr)));
            SetName(it, RandomName(rng));
            what = "SetName(empty)";
            break;
        }
        default: {
            heap.Stop();
            for (int k = 0; k < kItems; ++k) NoteAll(side.items[k].p, Fmt("op %d item %d", op, k).c_str(), int32_t(rng() % 3));
            what = "notes";
            break;
        }
        }
        heap.Stop();
        Log(Fmt("op %d item %d %s: blocks %lld", op, i, what.c_str(), heap.Blocks()));
        NoteState(side, i, rng);
        std::string refs = "counts:";
        for (int k = 0; k < kItems; ++k) {
            refs += Fmt(" %u", side.items[k].p->AddRef());
            side.items[k].p->Release();
        }
        Log(refs);
    }
    for (int k = 0; k < kItems; ++k) NoteAll(side.items[k].p, Fmt("end item %d", k).c_str(), 0);
    (void)ctx;
}

void ReleaseAll(Side &side)
{
    for (int i = 0; i < kItems; ++i) Log(Fmt("release %d: %u", i, side.items[i].p->Release()));
}

struct Run {
    difftest::Context &ctx;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    explicit Run(difftest::Context &c) : ctx(c) { gCtx = &c; }
    ~Run() { gLog = nullptr; }
    void Use(Side &side, bool game)
    {
        gGame = game;
        gLog = &side.log;
        ctx.Mark(game ? "game side" : "our side");
    }
};

bool Compare(difftest::Context &ctx, uint32_t seed, const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    ++ctx.cases;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            ctx.Fail("seed %u line %zu (after: %s | %s):\n  original %s\n  ours     %s", seed, i,
                     i >= 2 ? a[i - 2].c_str() : "", i >= 1 ? a[i - 1].c_str() : "", a[i].c_str(), b[i].c_str());
            return false;
        }
    }
    if (a.size() != b.size()) {
        ctx.Fail("seed %u: %zu lines from the original, %zu from ours (next: %s)", seed, a.size(), b.size(),
                 (a.size() > n ? a[n] : b[n]).c_str());
        return false;
    }
    return true;
}

// A read target: fresh, or holding a Sint32 or a string already (Read / ReadStream clear it first; an unknown type then
// leaves it empty; Parse clears it only past its checks).
void Prefill(cIDataListItem *item, int how)
{
    if (how == 1) item->SetSint32(1234);
    if (how == 2) item->SetString("old value");
}

// A list's items' counts (the list's references and the reader's: Read and Parse release their own after AddItem).
void NoteChildren(cIDataListItem *item)
{
    if (item->GetType() != cIDataListItem::kList) return;
    std::string s = "children:";
    const uint32_t count = item->GetCount();
    for (uint32_t k = 0; k < count; ++k) {
        cIDataListItem *child = item->GetItem(int32_t(k));
        s += Fmt(" %u", child->AddRef());
        child->Release();
        NoteChildren(child);
    }
    Log(s);
}

// The game side's tapes of an item, taken once for both sides' reads.
Tape WriteTape(cIDataListItem *item)
{
    FakeWriter writer;
    item->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&writer));
    return writer.tape;
}

FakeOStream StreamOf(cIDataListItem *item, bool describe, int32_t indent)
{
    FakeOStream stream;
    if (describe) item->Describe(reinterpret_cast<ifaces::cIGZOStream *>(&stream), indent);
    else item->WriteStream(reinterpret_cast<ifaces::cIGZOStream *>(&stream));
    return stream;
}

// Type tokens changed to 0-10 (the first token of each item written).
void ChangeTypes(Tape &tape, std::mt19937 &rng, bool stream)
{
    for (size_t i = 0; i + 1 < tape.size(); ++i) {
        Token &t = tape[i];
        const bool isType = stream ? (t.kind == 'w' && t.text == "8" && tape[i + 1].kind == 't')
                                   : (t.kind == 'w' && tape[i + 1].kind == 't' && t.words[0] <= 9);
        if (isType && rng() % 3 == 0) t.words[0] = rng() % 11;
    }
}

// Lines of Describe's text (each with its line end, as the text stream's GetGZStr gives them: "\r\n" ends become "\n"
// ends half the time, the rest without).
Tape LinesOf(const std::string &text, std::mt19937 &rng)
{
    Tape lines;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find("\r\n", at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (rng() % 2) line += "\n";
        lines.push_back({'t', {}, line});
        at = end + 2;
    }
    return lines;
}

std::string GeneratedLine(std::mt19937 &rng)
{
    static const char *const kTypes[] = {"Bool", "Sint32", "Float32", "Vector2f", "Vector3f", "String", "List",
                                         "Quaternionf", "ByteArray", "Undefined", "bool", "Floats", ""};
    static const char *const kValues[] = {"true", "false", "TRUE", "it is true", "12", "-7", "x12", "3.5", "1.5, 2",
                                          "1, 2, 3", "1,2,3,4", "1, 2, 3, 4", "'quoted text'", "'open", "no quotes",
                                          "''", "'a' 'b'", "3 1 2 3", "2 255 256", "0", "2 7", "1 x", "4 1 2 3 4 5",
                                          "", "  9  ", "5"};
    static const char *const kEquals[] = {"=", "=", "=", ":", "==", ""};
    // lines whose answer turns on one check: the line feed erased before trimming ("= \n" leaves no separator), a byte
    // just past 0xff, case in the type word
    static const char *const kEdges[] = {"Bool b = \n", "String s = \n", "Sint32 n = \n", "ByteArray b = 2 255 256",
                                         "ByteArray b = 2 256 7 x", "ByteArray b = 2 255 7 x", "bool b = true",
                                         "String s = 'it''s'\n", "List L 0\n", "Float32 f = 2.5\n"};
    if (rng() % 3 == 0) return kEdges[rng() % 10];
    std::string line = std::string("  ") + kTypes[rng() % 13];
    if (rng() % 8) line += " " + RandomName(rng);
    if (rng() % 6) line += std::string(" ") + kEquals[rng() % 6];
    line += std::string(" ") + kValues[rng() % 26];
    if (rng() % 3 == 0) line += "\n";
    return line;
}

} // namespace

DIFF_TEST(SG_DataListItem_Slots, 0xac6d50)
{
    Run run(ctx);
    for (uint32_t seed = 1; seed <= 150; ++seed) {
        Side game, ours;
        run.Use(game, true);
        RunSlots(ctx, game, seed);
        ReleaseAll(game);
        run.Use(ours, false);
        RunSlots(ctx, ours, seed);
        ReleaseAll(ours);
        Compare(ctx, seed, game.log, ours.log);
    }
}

// Read (7) and ReadStream (9): the game side's tapes of seeded items read back on both sides.
static void ReadBack(difftest::Context &ctx, bool stream)
{
    Run run(ctx);
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        // the items, built on the game side
        Side source;
        run.Use(source, true);
        RunSlots(ctx, source, seed);
        std::mt19937 rng(seed * 7919u);
        for (int k = 0; k < kItems; ++k) {
            for (int changed = 0; changed < 2; ++changed) {
                Tape tape;
                if (stream) tape = StreamOf(source.items[k].p, false, 0).tape;
                else tape = WriteTape(source.items[k].p);
                if (changed) ChangeTypes(tape, rng, stream);
                std::vector<std::string> logs[2];
                for (int s = 0; s < 2; ++s) {
                    Side side;
                    run.Use(side, s == 0);
                    cIDataListItem *item = NewItem();
                    item->AddRef();
                    Prefill(item, (k + changed) % 3);
                    std::vector<std::string> kinds;
                    if (stream) {
                        FakeIStream in;
                        in.tape = tape;
                        item->ReadStream(reinterpret_cast<ifaces::cIGZIStream *>(&in));
                        kinds = in.kinds;
                        Log(Fmt("consumed %zu of %zu", in.next, in.tape.size()));
                    } else {
                        FakeReader in;
                        in.tape = tape;
                        item->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&in));
                        kinds = in.kinds;
                        Log(Fmt("consumed %zu of %zu", in.next, in.tape.size()));
                    }
                    std::string s1 = "reads:";
                    for (const std::string &kind : kinds) s1 += " " + kind;
                    Log(s1);
                    Log(Fmt("type %u", item->GetType()));
                    NoteChildren(item);
                    if (changed) NoteWrite(item, "read");
                    else NoteAll(item, "read", 1);
                    Log(Fmt("release %u", item->Release()));
                    logs[s] = side.log;
                }
                Compare(ctx, seed * 100 + uint32_t(k) * 10 + uint32_t(changed), logs[0], logs[1]);
            }
        }
        run.Use(source, true);
        ReleaseAll(source);
    }
}

DIFF_TEST(SG_DataListItem_Read, 0xac7950) { ReadBack(ctx, false); }

DIFF_TEST(SG_DataListItem_ReadStream, 0xac75a0) { ReadBack(ctx, true); }

DIFF_TEST(SG_DataListItem_Parse, 0xac6e50)
{
    Run run(ctx);
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        Side source;
        run.Use(source, true);
        RunSlots(ctx, source, seed);
        std::mt19937 rng(seed * 104729u);
        for (int k = 0; k < kItems + 8; ++k) {
            Tape lines;
            if (k < kItems) {
                lines = LinesOf(StreamOf(source.items[k].p, true, int32_t(rng() % 2)).text, rng);
            } else {
                const int n = 1 + int(rng() % 4);
                for (int l = 0; l < n; ++l) lines.push_back({'t', {}, GeneratedLine(rng)});
            }
            std::vector<std::string> logs[2];
            for (int s = 0; s < 2; ++s) {
                Side side;
                run.Use(side, s == 0);
                FakeIStream in;
                in.tape = lines;
                // until the lines run out: each Parse on a fresh item
                for (int guard = 0; guard < 40 && in.next < in.tape.size(); ++guard) {
                    cIDataListItem *item = NewItem();
                    item->AddRef();
                    Prefill(item, (k + guard) % 3);
                    const bool ok = item->Parse(reinterpret_cast<ifaces::cIGZIStream *>(&in));
                    Log(Fmt("parse %d at %zu: type %u", int(ok), in.next, item->GetType()));
                    NoteChildren(item);
                    NoteAll(item, "parsed", 0);
                    Log(Fmt("release %u", item->Release()));
                }
                logs[s] = side.log;
            }
            Compare(ctx, seed * 100 + uint32_t(k), logs[0], logs[1]);
        }
        run.Use(source, true);
        ReleaseAll(source);
    }
}
