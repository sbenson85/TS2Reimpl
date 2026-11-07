// Differential tests for the XML delegate of the string-keyed property-set serializer
// (src/gz/cGZPropertySetSerializerXml.cpp, lane DV).
//
// Covered, each run on the original by RVA and on ours:
//   0x555430 QueryInterface, 0x551e20 Destruct, 0x552200 DeletingDestructor, 0x556540 KeyFromString,
//   0x555d50 Read, handler face: 0x552f30 StartElement, 0x552630 EndElement, 0x5525f0 CharacterData
//
// Handlers: each side has its own serializer object (game vtables, real cRZStrings) reading into its own real
// property set (cGZPropertySetResourceString built with the original constructor 0x5519c0, so the set code is the
// original's on both sides). Seeded event scripts, mostly well-formed documents with random damage (unknown
// elements and types, missing or empty attributes, attribute names in other cases, text split over several
// CharacterData calls, whitespace around values, bad numbers and booleans, StringSet properties with <Element>
// members, events in the wrong state) run on both; after every event the result, the parser state (state, key,
// text, type, whether a StringSet is open) and a dump of the whole set are compared. Left out: an AnyBoolean's upper
// three payload bytes (EndElement builds the value in a stack slot and writes only its low byte, 0x55280b).
// Read: a fake framework hands out a fake parser; fake streams (seekable or byte by byte, failing reads) supply the
// text; the calls, the bytes handed to Parse and the reference counts are compared.
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {2, "dddd"}};

using svcfx::Blob;
using svcfx::F;
// Both instantiations of the template (string keys, uint32 keys: lane EU) have this layout; the tests run on either through a
// kind table (the game's RVAs and vtables, our construction, the adapters, the set and its key type).
using Xml = cGZPropertySetSerializerXmlString;
using Xml32 = cGZPropertySetSerializerXmlUint32;
static_assert(sizeof(Xml) == sizeof(Xml32) && offsetof(Xml, mTarget) == offsetof(Xml32, mTarget));

struct Kind {
    bool uint32Keys;
    uint32_t ctor, dtor, deleting, qi, read, write, keyFromString, start, end, chars;
    uint32_t vtable, handlerVTable, rootCell, keyCell, typeCell;
    Xml *(*construct)(void *memory); // ours
    void *aConstruct, *aDestruct, *aDeleting, *aQI, *aRead, *aWrite, *aKeyFromString, *aStart, *aEnd, *aChars;
};
template <class X, class A> Kind MakeKind(bool uint32Keys, std::initializer_list<uint32_t> rvas)
{
    Kind k{};
    k.uint32Keys = uint32Keys;
    uint32_t *fields[] = {&k.ctor, &k.dtor, &k.deleting, &k.qi, &k.read, &k.write, &k.keyFromString, &k.start, &k.end, &k.chars,
                          &k.rootCell, &k.keyCell, &k.typeCell};
    size_t i = 0;
    for (uint32_t rva : rvas) *fields[i++] = rva;
    k.vtable = X::kOriginalVTable;
    k.handlerVTable = X::kOriginalHandlerVTable;
    k.construct = [](void *memory) { return reinterpret_cast<Xml *>(::new (memory) X); };
    k.aConstruct = reinterpret_cast<void *>(&A::Construct);
    k.aDestruct = reinterpret_cast<void *>(&A::Destruct);
    k.aDeleting = reinterpret_cast<void *>(&A::DeletingDestructor);
    k.aQI = reinterpret_cast<void *>(&A::QueryInterface);
    k.aRead = reinterpret_cast<void *>(&A::Read);
    k.aWrite = reinterpret_cast<void *>(&A::Write);
    k.aKeyFromString = reinterpret_cast<void *>(&A::KeyFromString);
    k.aStart = reinterpret_cast<void *>(&A::StartElement);
    k.aEnd = reinterpret_cast<void *>(&A::EndElement);
    k.aChars = reinterpret_cast<void *>(&A::CharacterData);
    return k;
}
const Kind kStringKind = MakeKind<Xml, cGZPropertySetSerializerXmlStringAdapters>(
    false, {0x551a60, 0x551e20, 0x552200, 0x555430, 0x555d50, 0x556a30, 0x556540, 0x552f30, 0x552630, 0x5525f0, 0x1015de0,
            0x1015dd4, 0x1015dd8});
const Kind kUint32Kind = MakeKind<Xml32, cGZPropertySetSerializerXmlUint32Adapters>(
    true, {0x241ea6, 0x241f82, 0x241fe8, 0x24249f, 0x242641, 0x242901, 0x24287e, 0x2422ae, 0x242090, 0x242064, 0xff6cc0, 0xff6cb4,
           0xff6cb8});
const Kind *gKind = &kStringKind;
template <class Fn> Fn Adapter(void *p) { return reinterpret_cast<Fn>(p); }
const char *GameText(uint32_t cell) { return *GameAddress<const char *>(cell); }

template <typename R, typename... A> R Slot(void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(object, slot)(object, args...);
}

void PushText(Blob &b, const char *text, size_t n)
{
    b.push_back(uint32_t(n));
    for (size_t i = 0; i < n; ++i) b.push_back(uint8_t(text[i]));
}
void PushText(Blob &b, const char *text) { PushText(b, text, std::strlen(text)); }

void PushString(Blob &b, const cRZString &s)
{
    PushText(b, s.mString.data(), s.mString.size());
    b.push_back(s.mRefCount);
}

// ---- the property set each side reads into --------------------------------------------------------------------

// String keys: the string resource's set face (0x5519c0, +0x1c); uint32 keys: the game's uint32 set (0x1bec5f, 0x18 bytes).
void *NewSet(difftest::Context &ctx)
{
    if (gKind->uint32Keys) {
        void *set = ctx.Original<void *(__thiscall *)(void *)>(0x1bec5f)(operator new(0x18));
        Slot<uint32_t>(set, 1); // AddRef
        return set;
    }
    void *memory = operator new(0x50);
    void *resource = ctx.Original<void *(__thiscall *)(void *)>(0x5519c0)(memory);
    void *face = static_cast<uint8_t *>(resource) + 0x1c;
    Slot<uint32_t>(face, 1); // AddRef
    return face;
}
void FreeSet(void *face) { Slot<uint32_t>(face, 2); }

void DumpSet(Blob &b, void *face)
{
    GZServiceString key;
    uint32_t key32 = 0;
    void *keyOut = gKind->uint32Keys ? static_cast<void *>(&key32) : &key;
    void *value = nullptr;
    unsigned guard = 0;
    for (bool more = Slot<uint8_t>(face, 6, keyOut, &value) != 0; more && guard < 64; ++guard) {
        if (gKind->uint32Keys)
            b.push_back(key32);
        else
            PushText(b, key.c_str());
        const uint32_t type = Slot<uint32_t>(value, 4);
        b.push_back(type);
        const uint32_t payload = static_cast<uint32_t *>(value)[1];
        if (type == gzany::kTypeString) {
            PushText(b, payload ? Slot<const char *>(reinterpret_cast<void *>(uintptr_t(payload)), 5) : "<null>");
        } else if (type == gzany::kTypeStringSet) {
            const auto *set = static_cast<AnyStringSet *>(value);
            b.push_back(uint32_t(set->mSet.size()));
            for (const GZString &member : set->mSet) PushText(b, member.c_str());
        } else if (type == gzany::kTypeBoolean) {
            b.push_back(payload & 0xff); // the upper three bytes are stack garbage (header comment)
        } else {
            b.push_back(payload);
        }
        more = Slot<uint8_t>(face, 7, keyOut, &value) != 0;
    }
}

// ---- serializer objects -------------------------------------------------------------------------------------------

// The standalone shape (lane EF): the original side's object comes from the game's constructor 0x551a60 (game vtables), ours
// from our constructor (our vtables); calls on our side go through the object's own vtable slots (CallSlot).
void ConstructXml(Xml *x, void *target, bool original)
{
    std::memset(static_cast<void *>(x), 0x6b, sizeof(*x));
    if (original)
        reinterpret_cast<void *(__thiscall *)(void *)>(GameAddress<void>(gKind->ctor))(x);
    else
        gKind->construct(x);
    x->mTarget = static_cast<cGZPropertySetString *>(target);
}

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}
void *HandlerFace(Xml *x) { return static_cast<cIGZExpatContentHandler *>(x); }
uint32_t GameWord(uint32_t rva) { return uint32_t(uintptr_t(GameAddress<void>(rva))); }

// Our two vtables, from an object built by our constructor.
struct XmlVTables {
    uint32_t face[2];
};
XmlVTables OurXmlVTables()
{
    alignas(8) uint8_t memory[sizeof(Xml)];
    Xml *x = gKind->construct(memory);
    XmlVTables v;
    std::memcpy(v.face, x, sizeof(v.face));
    x->mKey.Destruct();
    x->mText.Destruct();
    return v;
}
// The two vtable words as codes: 1 the side's own vtable of that face (ours are not the game's), else raw.
void PushXmlVTableWords(Blob &b, bool original, const void *object)
{
    const XmlVTables ours = OurXmlVTables();
    const uint32_t game[2] = {GameWord(gKind->vtable), GameWord(gKind->handlerVTable)};
    for (int i = 0; i < 2; ++i) {
        const uint32_t word = static_cast<const uint32_t *>(object)[i];
        b.push_back(word == (original ? game[i] : ours.face[i]) && ours.face[i] != game[i] ? 1u : word);
    }
}

void DumpXml(Blob &b, Xml *x)
{
    b.push_back(uint32_t(x->mState));
    PushString(b, x->mKey);
    PushString(b, x->mText);
    b.push_back(x->mValueType);
    b.push_back(x->mStringSet ? 1u : 0u);
}

// ---- event scripts ------------------------------------------------------------------------------------------------

const char *const kTypes[] = {"0x0b8bea18", "0xeb61e4f7", "0x0c264712", "0xabc78708", "0xcba908e1", "0xad9a861e",
                              "0x12345678", "0", "0X0B8BEA18", "zz"};
const char *const kKeys[] = {"1", "2", "Key", "key", "", "a key that is longer than fifteen chars", "0x10000001"};
const char *const kTexts[] = {"text", "  12 ", "true", "TRUE", "false", "-5", "3.25", "abc", "\t x \t", "", "0x1f",
                              "4294967295", "1e3", " ", "Element text"};
const char *const kElements[] = {"AnyString", "AnyUint32", "Element", "element", "Other", "cGZPropertySetString",
                                 "CGZPROPERTYSETSTRING"};

struct Event {
    int kind; // 0 start, 1 characters, 2 end
    std::string name;
    std::vector<std::string> attributes; // name, value pairs
    bool nullAttributes = false;
    std::string text;
};

std::vector<Event> Script(uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<Event> events;
    auto pick = [&](auto &array) { return std::string(array[rng() % (sizeof(array) / sizeof(array[0]))]); };
    auto start = [&](std::string name, std::vector<std::string> attributes) {
        Event e{0, std::move(name), std::move(attributes)};
        e.nullAttributes = rng() % 20 == 0;
        events.push_back(e);
    };
    auto chars = [&](const std::string &text) {
        // Split over one to three calls.
        size_t at = 0;
        for (int parts = 1 + rng() % 3; parts > 1 && at < text.size(); --parts) {
            const size_t n = rng() % (text.size() - at + 1);
            events.push_back({1, {}, {}, false, text.substr(at, n)});
            at += n;
        }
        events.push_back({1, {}, {}, false, text.substr(at)});
    };
    auto end = [&](std::string name) { events.push_back({2, std::move(name), {}}); };

    const std::string root = GameText(gKind->rootCell); // cGZPropertySetString / the uint32 one's
    std::string rootUpper = root;
    rootUpper[0] = 'C';
    if (rng() % 8) start(rng() % 2 ? root : rootUpper, {});
    for (int properties = rng() % 6; properties; --properties) {
        if (rng() % 12 == 0) chars(pick(kTexts));
        std::vector<std::string> attributes;
        const int typeIndex = rng() % 10;
        if (rng() % 10) attributes.insert(attributes.end(), {rng() % 5 ? "key" : "KEY", pick(kKeys)});
        if (rng() % 10) attributes.insert(attributes.end(), {rng() % 5 ? "type" : "Type", kTypes[typeIndex]});
        if (rng() % 6 == 0) attributes.insert(attributes.end(), {"other", "x"});
        if (rng() % 2) std::swap(attributes[0], attributes.size() > 2 ? attributes[2] : attributes[0]);
        // A second key attribute (lane EU): the last one wins (both kinds).
        if (rng() % 7 == 0) attributes.insert(attributes.end(), {rng() % 2 ? "key" : "Key", pick(kKeys)});
        const std::string element = pick(kElements);
        start(element, attributes);
        if (typeIndex == 5 || rng() % 10 == 0) {
            for (int members = rng() % 4; members; --members) {
                const std::string name = rng() % 6 ? "Element" : pick(kElements);
                start(name, {});
                chars(pick(kTexts));
                if (rng() % 8 == 0) start("Element", {});
                end(rng() % 6 ? name : "Other");
            }
        } else {
            chars(pick(kTexts));
            if (rng() % 15 == 0) start("Element", {});
        }
        end(element);
        if (rng() % 15 == 0) end("extra");
    }
    if (rng() % 6) end(root);
    if (rng() % 5 == 0) start(root, {}); // a second document
    return events;
}

// how: 0 the original RVA, 1 our object's own vtable slot, 2 the adapter (the adapters test).
uint32_t Run(difftest::Context &ctx, int how, Xml *x, const Event &e)
{
    const bool orig = how == 0;
    auto *face = static_cast<cIGZExpatContentHandler *>(x);
    void *h = HandlerFace(x);
    std::vector<const char *> attributes;
    for (const std::string &a : e.attributes) attributes.push_back(a.c_str());
    attributes.push_back(nullptr);
    attributes.push_back(nullptr);
    const char **list = e.nullAttributes ? nullptr : attributes.data();
    using AStart = uint8_t(__fastcall *)(void *, void *, const char *, const char **, uint32_t, uint32_t);
    using AChars = uint8_t(__fastcall *)(void *, void *, const char *, uint32_t);
    using AEnd = uint8_t(__fastcall *)(void *, void *, const char *);
    switch (e.kind) {
    case 0:
        return orig      ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, const char **, uint32_t, uint32_t)>(gKind->start)(h, e.name.c_str(), list, 0x11, 0x22)
             : how == 1 ? CallSlot<uint8_t>(h, 3, e.name.c_str(), list, 0x11u, 0x22u)
                        : Adapter<AStart>(gKind->aStart)(face, nullptr, e.name.c_str(), list, 0x11, 0x22);
    case 1:
        return orig      ? ctx.Original<uint8_t(__thiscall *)(void *, const char *, uint32_t)>(gKind->chars)(h, e.text.data(), uint32_t(e.text.size()))
             : how == 1 ? CallSlot<uint8_t>(h, 5, e.text.data(), uint32_t(e.text.size()))
                        : Adapter<AChars>(gKind->aChars)(face, nullptr, e.text.data(), uint32_t(e.text.size()));
    default:
        return orig      ? ctx.Original<uint8_t(__thiscall *)(void *, const char *)>(gKind->end)(h, e.name.c_str())
             : how == 1 ? CallSlot<uint8_t>(h, 4, e.name.c_str())
                        : Adapter<AEnd>(gKind->aEnd)(face, nullptr, e.name.c_str());
    }
}

// ---- Read fakes ---------------------------------------------------------------------------------------------------
// F(0) framework: slot 3 CreateInstance(class, iid, out) hands out F(1) when value[0]; returns value[1].
// F(1) parser: slot 8 SetContentHandler(handler) returns ret; slot 3 Parse(data, length, final): logs the bytes,
//      returns ret. F(2) stream: slot 0 QI(iid, out): hands out F(3) when value[0]; slot 4 GetUint8 reads the source
//      (fails after value[2] bytes); slot 17 GetVoid(buffer, length): copies when value[3]. F(3) seekable: slot 10 size.
std::string gSource;
// The read buffer (GetVoid's and Parse's first argument, declared 'x'): a std::vector's, whose offset inside the heap
// block is the library allocators' own (our STL aligns blocks of 4 KB and more to 32 bytes inside a larger block; the
// offsets differ by side and by seed), not the code's. Logged instead: null or not, its heap block (the record without the
// offset), and whether it is the buffer GetVoid was handed; the bytes Parse gets are logged by content.
uint32_t gVoidBuffer;
void LogBuffer(uint32_t p)
{
    svcfx::Ev(p ? 1u : 0u);
    if (!p) return;
    svcfx::Ev(svcfx::Norm(p) & 0xffff0000u);
    svcfx::Ev(p == gVoidBuffer ? 1u : 0u);
}

uint32_t FwCreate(svcfx::Obj *self, uint32_t *args)
{
    if (self->value[0]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(1);
        ++F(1).refs;
    }
    return self->value[1];
}
uint32_t ParserParse(svcfx::Obj *self, uint32_t *args)
{
    LogBuffer(args[0]);
    const char *data = reinterpret_cast<const char *>(uintptr_t(args[0]));
    svcfx::Ev(data ? 1u : 0u);
    PushText(svcfx::gEv, data ? data : "", data ? args[1] : 0);
    return self->ret[3];
}
uint32_t StreamQI(svcfx::Obj *self, uint32_t *args)
{
    if (self->value[0]) {
        *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(3);
        ++F(3).refs;
        return 1;
    }
    return 0;
}
uint32_t StreamGetUint8(svcfx::Obj *self, uint32_t *args)
{
    if (self->value[1] >= self->value[2] || self->value[1] >= gSource.size()) return 0;
    *reinterpret_cast<uint8_t *>(uintptr_t(args[0])) = uint8_t(gSource[self->value[1]++]);
    return 0x101;
}
uint32_t StreamGetVoid(svcfx::Obj *self, uint32_t *args)
{
    LogBuffer(args[0]);
    gVoidBuffer = args[0];
    if (!self->value[3]) return 0;
    char *buffer = reinterpret_cast<char *>(uintptr_t(args[0]));
    for (uint32_t i = 0; i < args[1]; ++i) buffer[i] = i < gSource.size() ? gSource[i] : '#';
    return 1;
}

// QI's AddRef through slot 1.
uint32_t __fastcall XmlAddRef(Xml *self, void *)
{
    svcfx::Ev(0x71);
    return ++self->mRefCount;
}
void *gXmlVT[6] = {nullptr, reinterpret_cast<void *>(&XmlAddRef), nullptr, nullptr, nullptr, nullptr};

} // namespace

// The adapters' trap vtables: copies of the game's two with the forwarded slots replaced by traps of the slot's arity.
Blob gXmlTraps;
void *gXmlTrapVT[6];
void *gXmlTrapHandlerVT[12];
uint32_t __fastcall XTrap1(void *, void *, uint32_t) { gXmlTraps.push_back(0x7a7a0001); return 0; }
uint32_t __fastcall XTrap2(void *, void *, uint32_t, uint32_t) { gXmlTraps.push_back(0x7a7a0002); return 0; }
uint32_t __fastcall XTrap3(void *, void *, uint32_t, uint32_t, uint32_t) { gXmlTraps.push_back(0x7a7a0003); return 0; }
uint32_t __fastcall XTrap4(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t) { gXmlTraps.push_back(0x7a7a0004); return 0; }
void InstallXmlTraps(Xml *x)
{
    std::memcpy(gXmlTrapVT, GameAddress<void>(gKind->vtable), sizeof(gXmlTrapVT));
    std::memcpy(gXmlTrapHandlerVT, GameAddress<void>(gKind->handlerVTable), sizeof(gXmlTrapHandlerVT));
    gXmlTrapVT[0] = reinterpret_cast<void *>(&XTrap2);
    gXmlTrapVT[3] = reinterpret_cast<void *>(&XTrap2);
    gXmlTrapVT[4] = reinterpret_cast<void *>(&XTrap3);
    gXmlTrapVT[5] = reinterpret_cast<void *>(&XTrap1);
    gXmlTrapHandlerVT[3] = reinterpret_cast<void *>(&XTrap4);
    gXmlTrapHandlerVT[4] = reinterpret_cast<void *>(&XTrap1);
    gXmlTrapHandlerVT[5] = reinterpret_cast<void *>(&XTrap2);
    reinterpret_cast<void **>(x)[0] = gXmlTrapVT;
    reinterpret_cast<void **>(x)[1] = gXmlTrapHandlerVT;
}

// StartElement / EndElement / CharacterData over seeded event scripts, into real property sets. ours: 1 our object through
// its vtable, 2 the adapters on a game-built object with trap vtables.
void CompareHandlers(difftest::Context &ctx, int ours, uint32_t seeds)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        const std::vector<Event> events = Script(seed);
        alignas(8) uint8_t memory[2][sizeof(Xml)];
        Xml *xs[2] = {reinterpret_cast<Xml *>(memory[0]), reinterpret_cast<Xml *>(memory[1])};
        void *sets[2] = {NewSet(ctx), NewSet(ctx)};
        for (int s = 0; s < 2; ++s) ConstructXml(xs[s], sets[s], s == 0 || ours == 2);
        if (ours == 2) InstallXmlTraps(xs[1]);
        for (size_t i = 0; i < events.size(); ++i) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                gXmlTraps.clear();
                result[s].push_back(Run(ctx, s == 0 ? 0 : ours, xs[s], events[i]) & 0xff);
                result[s].insert(result[s].end(), gXmlTraps.begin(), gXmlTraps.end());
                DumpXml(result[s], xs[s]);
                DumpSet(result[s], sets[s]);
            }
            if (!svcfx::Same(ctx, result[0], result[1], "Handlers", seed * 1000 + uint32_t(i))) break;
        }
        for (int s = 0; s < 2; ++s) {
            xs[s]->mKey.Destruct();
            xs[s]->mText.Destruct();
            FreeSet(sets[s]);
        }
    }
}

DIFF_TEST(PropertySetXml_Handlers, 0x552f30)
{
    gKind = &kStringKind;
    CompareHandlers(ctx, 1, 600);
}

// Read: parser creation, the handler, the stream paths, Parse's input and the target. ours: as CompareHandlers.
void CompareXmlRead(difftest::Context &ctx, int ours, uint32_t seeds)
{
    using ARead = uint8_t(__fastcall *)(void *, void *, void *, void *);
    const char *const sources[] = {"", "<cGZPropertySetString>\n</cGZPropertySetString>", "x",
                                   "<?xml version=\"1.0\"?><a b=\"c\">text that is long enough to matter</a>"};
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            std::mt19937 rng(seed);
            svcfx::ResetAll();
            gVoidBuffer = 0;
            svcfx::Globals globals;
            svcfx::gFrameworkIndex = 0;
            F(0).Set(3, "ddp", 0, &FwCreate);
            F(0).value[0] = rng() % 6 != 0;
            F(0).value[1] = rng() % 2;
            F(1).Set(8, "p", rng() % 5 ? 1u : (rng() % 2 ? 0u : 0x100u));
            F(1).Set(3, "xdd", rng() % 2 ? 1u : 0x200u, &ParserParse); // the buffer: LogBuffer
            F(2).Set(0, "dp", 0, &StreamQI);
            F(2).value[0] = rng() % 2;
            F(2).Set(4, "p", 0, &StreamGetUint8);
            F(2).value[2] = rng() % 3 ? 0xffffffffu : uint32_t(rng() % 8);
            F(2).Set(17, "xd", 0, &StreamGetVoid); // the buffer: LogBuffer
            F(2).value[3] = rng() % 4 != 0;
            gSource = sources[rng() % 4];
            F(3).Set(10, 0, rng() % 4 ? uint32_t(gSource.size()) : uint32_t(rng() % 40));
            alignas(8) uint8_t memory[sizeof(Xml)];
            Xml *x = reinterpret_cast<Xml *>(memory);
            ConstructXml(x, nullptr, s == 0 || ours == 2);
            if (s == 1 && ours == 2) InstallXmlTraps(x);
            gXmlTraps.clear();
            svcfx::AddRange(x, sizeof(Xml), 0x0b000000);
            void *target = reinterpret_cast<void *>(uintptr_t(0x5e700000));
            auto *set = static_cast<cGZPropertySetString *>(target);
            const uint32_t r = s == 0     ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(gKind->read)(x, &F(2), target)
                             : ours == 1 ? CallSlot<uint8_t>(x, 3, static_cast<void *>(&F(2)), set)
                                         : Adapter<ARead>(gKind->aRead)(x, nullptr, &F(2), set);
            result[s] = {r & 0xff, svcfx::P(x->mTarget)};
            result[s].insert(result[s].end(), gXmlTraps.begin(), gXmlTraps.end());
            result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::CaptureFakes(result[s], kValueKinds);
            x->mKey.Destruct();
            x->mText.Destruct();
        }
        svcfx::Same(ctx, result[0], result[1], "Read", seed);
    }
}

DIFF_TEST(PropertySetXml_Read, 0x555d50)
{
    gKind = &kStringKind;
    CompareXmlRead(ctx, 1, 400);
}

// The deleting destructor (flags 0-3) and the destructor alone: the vtables stored back (the handler word is seeded with junk
// on both sides; the primary one stays valid for our side's call through slot 5), the strings reset, the object freed only
// on flags & 1. ours: 1 our object through slot 5 (the destructor alone: slot 5 with 0), 2 the adapters on a game-built
// object with trap vtables.
void CompareDestroy(difftest::Context &ctx, int ours)
{
    using ADeleting = void *(__fastcall *)(void *, void *, uint32_t);
    using ADestruct = void(__fastcall *)(void *, void *);
    // The string buffers' words (heap pointers after a long text) are masked: each side allocated its own buffer.
    auto pushWords = [](Blob &b, bool original, const Xml *x) {
        PushXmlVTableWords(b, original, x);
        const auto *words = reinterpret_cast<const uint32_t *>(x);
        for (size_t i = 2; i < sizeof(Xml) / 4; ++i) {
            const bool pointer = (i >= 5 && i <= 8) || (i >= 13 && i <= 16);
            b.push_back(pointer && words[i] > 0x10000 ? 0xbbbbbbbbu : words[i]);
        }
    };
    for (uint32_t flags : {0u, 1u, 2u, 3u}) {
        for (int longText = 0; longText < 2; ++longText) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                auto *x = static_cast<Xml *>(operator new(sizeof(Xml)));
                ConstructXml(x, nullptr, s == 0 || ours == 2);
                if (s == 1 && ours == 2) InstallXmlTraps(x);
                reinterpret_cast<void **>(x)[1] = nullptr;
                x->mKey.mString.assign(longText ? "a key that is longer than fifteen characters" : "k");
                x->mText.mString.assign(longText ? "a text that is longer than fifteen characters" : "t");
                x->mValueType = 0x77;
                gXmlTraps.clear();
                bool freed;
                void *r;
                {
                    difftest::FreeRecorder rec;
                    r = s == 0     ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(gKind->deleting)(x, flags)
                      : ours == 1 ? CallSlot<void *>(x, 5, flags)
                                  : Adapter<ADeleting>(gKind->aDeleting)(x, nullptr, flags);
                    rec.Stop();
                    freed = rec.Freed(x);
                }
                result[s] = {r == x, freed};
                result[s].insert(result[s].end(), gXmlTraps.begin(), gXmlTraps.end());
                if (!freed) {
                    pushWords(result[s], s == 0, x);
                    operator delete(x);
                }
            }
            svcfx::Same(ctx, result[0], result[1], "DeletingDestructor", flags * 2 + uint32_t(longText));
        }
    }
    // Destruct alone.
    Blob result[2];
    for (int s = 0; s < 2; ++s) {
        alignas(8) uint8_t memory[sizeof(Xml)];
        Xml *x = reinterpret_cast<Xml *>(memory);
        ConstructXml(x, nullptr, s == 0 || ours == 2);
        if (s == 1 && ours == 2) InstallXmlTraps(x);
        x->mText.mString.assign("a text that is longer than fifteen characters");
        const void *buffer = x->mText.mString.data();
        gXmlTraps.clear();
        {
            difftest::FreeRecorder rec;
            if (s == 0)
                ctx.Original<void(__thiscall *)(void *)>(gKind->dtor)(x);
            else if (ours == 1)
                CallSlot<void *>(x, 5, 0u);
            else
                Adapter<ADestruct>(gKind->aDestruct)(x, nullptr);
            rec.Stop();
            result[s].push_back(rec.Freed(buffer)); // the long text's buffer is released
        }
        result[s].insert(result[s].end(), gXmlTraps.begin(), gXmlTraps.end());
        // The freed long text leaves its buffer address in the small-string bytes (the STL's deallocation only clears
        // the first byte), and each side allocated its own buffer: masked as above, else the comparison depends on
        // where the heap put the two buffers (it failed about one run in four in the full group).
        pushWords(result[s], s == 0, x);
    }
    if (result[0].empty() || result[0][0] != 1) ctx.Fail("Destruct: the original did not free the text's buffer (test setup)");
    svcfx::Same(ctx, result[0], result[1], "Destruct", 0);
}

// The key KeyFromString made (string: its text; uint32: the number).
void PushKey(Blob &b, const GZServiceString &key, uint32_t key32)
{
    if (gKind->uint32Keys)
        b.push_back(key32);
    else
        PushText(b, key.data(), key.size());
}

// QueryInterface, KeyFromString, Destruct, DeletingDestructor.
void CompareObject(difftest::Context &ctx)
{
    svcfx::AllocatorScope allocator;
    // QueryInterface on both faces, through each side's own vtable (the original's face-4 slot 0 is its adjustor thunk
    // 0x555479, ours the compiler's): the face handed out and the AddRef through slot 1 (a
    // copy of the side's own vtable with a logging slot 1; slot 0 stays the side's own).
    for (uint32_t face : {0u, 4u})
        for (uint32_t iid : {0u, 1u, 0xebe701f3u, 0x4b6b9c9bu, 0x4bb0d269u}) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                svcfx::ResetAll();
                alignas(8) uint8_t memory[sizeof(Xml)];
                Xml *x = reinterpret_cast<Xml *>(memory);
                ConstructXml(x, nullptr, s == 0);
                std::memcpy(gXmlVT, *reinterpret_cast<void **>(x), sizeof(gXmlVT));
                gXmlVT[1] = reinterpret_cast<void *>(&XmlAddRef);
                void *ownVTable = *reinterpret_cast<void **>(x);
                *reinterpret_cast<void **>(x) = gXmlVT;
                x->mRefCount = 5;
                void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                const uint32_t r = s == 0 && face == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(gKind->qi)(x, iid, &out)
                                                       : CallSlot<uint8_t>(memory + face, 0, iid, &out);
                *reinterpret_cast<void **>(x) = ownVTable;
                result[s] = {r & 0xff, uint32_t(static_cast<uint8_t *>(out) - memory), x->mRefCount};
                result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
                PushXmlVTableWords(result[s], s == 0, x);
                x->mKey.Destruct();
                x->mText.Destruct();
            }
            svcfx::Same(ctx, result[0], result[1], "QueryInterface", face << 8 | (iid & 0xff));
        }
    // AddRef / Release (both faces) and the handler face's default slots 6-11, through each side's own vtable, on heap objects
    // (Release at 1 or below deletes: the slot 5 deleting destructor).
    for (uint32_t face : {0u, 4u})
        for (int slot : {1, 2, 6, 7, 8, 9, 10, 11})
            for (uint32_t count : {0u, 1u, 2u, 0x80000001u}) {
                if (face == 0 && slot > 2) continue;
                Blob result[2];
                for (int s = 0; s < 2; ++s) {
                    auto *x = static_cast<Xml *>(operator new(sizeof(Xml)));
                    ConstructXml(x, nullptr, s == 0);
                    x->mRefCount = count;
                    void *f = reinterpret_cast<uint8_t *>(x) + face;
                    difftest::FreeRecorder rec;
                    uint32_t r;
                    switch (slot) {
                    case 8: case 10: r = CallSlot<uint8_t>(f, slot, 0x11u, 0x22u); break;
                    case 9: r = CallSlot<uint8_t>(f, slot, 0x11u); break;
                    case 11: r = CallSlot<uint8_t>(f, slot, 0x11u, 0x22u, 0x33u, 0x44u); break;
                    case 6: case 7: r = CallSlot<uint8_t>(f, slot); break;
                    default: r = CallSlot<uint32_t>(f, slot); break;
                    }
                    rec.Stop();
                    const bool freed = rec.Freed(x);
                    result[s] = {r, freed};
                    if (!freed) {
                        result[s].push_back(x->mRefCount);
                        PushXmlVTableWords(result[s], s == 0, x);
                        x->mKey.Destruct();
                        x->mText.Destruct();
                        operator delete(x);
                    }
                }
                svcfx::Same(ctx, result[0], result[1], "Slots", face << 16 | uint32_t(slot) << 8 | (count & 0xff));
            }
    // KeyFromString.
    for (const char *text : {"", "1", "a key that is longer than fifteen characters", "0x8000001f", "4294967296", " 12"}) {
        for (const char *previous : {"", "previous text that is long, long, long"}) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                cRZString source;
                source.Construct(text);
                GZServiceString key(previous);
                uint32_t key32 = 0x5555;
                void *keyOut = gKind->uint32Keys ? static_cast<void *>(&key32) : &key;
                alignas(8) uint8_t memory[sizeof(Xml)] = {};
                Xml *x = reinterpret_cast<Xml *>(memory);
                const uint32_t r = s == 0                ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(gKind->keyFromString)(x, source.AsInterface(), keyOut)
                                 : gKind->uint32Keys ? reinterpret_cast<Xml32 *>(x)->KeyFromString(source.AsInterface(), keyOut)
                                                     : x->KeyFromString(source.AsInterface(), keyOut);
                result[s] = {r & 0xff};
                PushKey(result[s], key, key32);
                source.Destruct();
            }
            svcfx::Same(ctx, result[0], result[1], "KeyFromString", uint32_t(std::strlen(text)));
        }
    }
    CompareDestroy(ctx, 1);
}

DIFF_TEST(PropertySetXml_Object, 0x555430)
{
    gKind = &kStringKind;
    CompareObject(ctx);
}

// The constructor (0x551a60 against our constructor, on 0x6b-filled memory; the adapter on the same) and the adapters of
// QueryInterface, KeyFromString, Read, Write, the handlers, Destruct and DeletingDestructor on a game-built object whose
// vtables are copies with traps in each forwarded slot.
void CompareAdapters(difftest::Context &ctx)
{
    using AConstruct = Xml *(__fastcall *)(void *, void *);
    using AQI = uint8_t(__fastcall *)(void *, void *, uint32_t, void **);
    using AKey = uint8_t(__fastcall *)(void *, void *, void *, void *);
    using AWrite = uint8_t(__fastcall *)(void *, void *, void *, void *, uint32_t);
    svcfx::AllocatorScope allocator;
    for (int how = 1; how <= 2; ++how) {
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            alignas(8) uint8_t memory[sizeof(Xml)];
            std::memset(memory, 0x6b, sizeof(memory));
            Xml *x = s == 0 ? static_cast<Xml *>(reinterpret_cast<void *(__thiscall *)(void *)>(GameAddress<void>(gKind->ctor))(memory))
                   : how == 1 ? gKind->construct(memory)
                              : Adapter<AConstruct>(gKind->aConstruct)(memory, nullptr);
            result[s].push_back(uint32_t(static_cast<void *>(x) == memory));
            PushXmlVTableWords(result[s], s == 0, x);
            const auto *words = reinterpret_cast<const uint32_t *>(memory);
            for (size_t i = 2; i < sizeof(Xml) / 4; ++i) result[s].push_back(words[i]);
            x->mKey.Destruct();
            x->mText.Destruct();
        }
        svcfx::Same(ctx, result[0], result[1], "Construct", uint32_t(how));
    }
    // QueryInterface (slot 0 trapped: the AddRef through slot 1 stays the game's) and KeyFromString.
    for (uint32_t iid : {0u, 1u, 0xebe701f3u, 0x4b6b9c9bu}) {
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            alignas(8) uint8_t memory[sizeof(Xml)];
            Xml *x = reinterpret_cast<Xml *>(memory);
            ConstructXml(x, nullptr, true);
            InstallXmlTraps(x);
            x->mRefCount = 3;
            gXmlTraps.clear();
            void *out = nullptr;
            const uint32_t r = s == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(gKind->qi)(x, iid, &out)
                                      : Adapter<AQI>(gKind->aQI)(x, nullptr, iid, &out);
            result[s] = {r & 0xff, out ? uint32_t(static_cast<uint8_t *>(out) - memory) : 0xeeu, x->mRefCount};
            cRZString source;
            source.Construct("0x1f key");
            GZServiceString key("old");
            uint32_t key32 = 0x5555;
            void *keyOut = gKind->uint32Keys ? static_cast<void *>(&key32) : &key;
            result[s].push_back(s == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(gKind->keyFromString)(x, source.AsInterface(), keyOut)
                                       : Adapter<AKey>(gKind->aKeyFromString)(x, nullptr, source.AsInterface(), keyOut));
            PushKey(result[s], key, key32);
            source.Destruct();
            // Write on an empty set (slot 4 trapped): GetCount 0 returns 0 before anything else.
            svcfx::ResetAll();
            F(1).Set(11, 0, 0u);
            result[s].push_back(s == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *, uint32_t)>(gKind->write)(x, &F(2), &F(1), 1)
                                       : Adapter<AWrite>(gKind->aWrite)(x, nullptr, &F(2), &F(1), 1));
            svcfx::CaptureFakes(result[s]);
            result[s].insert(result[s].end(), gXmlTraps.begin(), gXmlTraps.end());
            x->mKey.Destruct();
            x->mText.Destruct();
        }
        svcfx::Same(ctx, result[0], result[1], "Adapters QI / KeyFromString / Write", iid);
    }
    // The uint32 instantiation's own helpers (inline in the string one): AssignRootName 0x24204a and KeyToText 0x242482 on a
    // text holding an old value; the original, our member, the adapter.
    if (gKind->uint32Keys) {
        using ARoot = void(__fastcall *)(void *, void *, cRZString *, const void *);
        using AKeyText = uint8_t(__fastcall *)(void *, void *, const uint32_t *, cRZString *);
        for (uint32_t key : {0u, 1u, 0x7fffffffu, 0xffffffffu, 0xabcdef12u}) {
            Blob result[3];
            for (int route = 0; route < 3; ++route) {
                alignas(8) uint8_t memory[sizeof(Xml)] = {};
                auto *x = reinterpret_cast<Xml32 *>(memory);
                cRZString text;
                text.Construct("an old text that is longer than the small buffer");
                if (route == 0)
                    ctx.Original<void(__thiscall *)(void *, cRZString *, const void *)>(0x24204a)(x, &text, &key);
                else if (route == 1)
                    x->AssignRootName(&text, &key);
                else
                    reinterpret_cast<ARoot>(&cGZPropertySetSerializerXmlUint32Adapters::AssignRootName)(x, nullptr, &text, &key);
                PushString(result[route], text);
                const uint32_t r = route == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *, cRZString *)>(0x242482)(x, &key, &text)
                                 : route == 1 ? uint8_t(x->KeyToText(&key, &text))
                                              : reinterpret_cast<AKeyText>(&cGZPropertySetSerializerXmlUint32Adapters::KeyToText)(x, nullptr, &key, &text);
                result[route].push_back(r & 0xff);
                PushString(result[route], text);
                text.Destruct();
            }
            svcfx::Same(ctx, result[0], result[1], "AssignRootName / KeyToText member", key);
            svcfx::Same(ctx, result[0], result[2], "AssignRootName / KeyToText adapter", key);
        }
    }
    CompareHandlers(ctx, 2, 60);
    CompareXmlRead(ctx, 2, 60);
    CompareDestroy(ctx, 2);
}

DIFF_TEST(PropertySetXml_Adapters, 0x551a60)
{
    gKind = &kStringKind;
    CompareAdapters(ctx);
}

// ---- the uint32 instantiation (lane EU): the same comparisons with the uint32 kind -------------------------------------
// 0x2422ae StartElement, 0x242090 EndElement, 0x242064 CharacterData (handler face, through our vtable: slots 3-5)
DIFF_TEST(PropertySetXml32_Handlers, 0x2422ae)
{
    gKind = &kUint32Kind;
    CompareHandlers(ctx, 1, 600);
    gKind = &kStringKind;
}

// 0x242641 Read
DIFF_TEST(PropertySetXml32_Read, 0x242641)
{
    gKind = &kUint32Kind;
    CompareXmlRead(ctx, 1, 400);
    gKind = &kStringKind;
}

// 0x24249f QueryInterface (and the handler face's thunk 0x2424f2 through our handler vtable's slot 0), 0x24287e KeyFromString,
// AddRef / Release (folded 0x145260 / 0x242830 and the thunks 0x1448e9 / 0x242845), the defaults 6-11, 0x241f82 Destruct,
// 0x241fe8 DeletingDestructor.
DIFF_TEST(PropertySetXml32_Object, 0x24249f)
{
    gKind = &kUint32Kind;
    CompareObject(ctx);
    gKind = &kStringKind;
}

// 0x241ea6 Construct and every adapter on a game-built uint32 object with trap vtables (0x24249f, 0x24287e, 0x242901, 0x2422ae,
// 0x242090, 0x242064, 0x242641, 0x241f82, 0x241fe8).
DIFF_TEST(PropertySetXml32_Adapters, 0x241ea6)
{
    gKind = &kUint32Kind;
    CompareAdapters(ctx);
    gKind = &kStringKind;
}
