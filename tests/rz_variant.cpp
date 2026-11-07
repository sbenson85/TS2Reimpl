// Differential tests for cRZVariant (src/rz/cRZVariant.h, standalone shape).
//
// cRZVariant_VTable / cRZVariant_Adapters: a script, generated from a seed, runs on three routes and their transcripts are
// compared line by line:
// - original: variants built by the image's constructors (0x9c7c75 ... 0x9c7ce2), assigned by 0x9c7d18, destroyed by
//   0x9c7d0c or slot 132 (0x9c8603), and every slot called at the function the game's vtable 0xe6180c holds;
// - vtable: our variants (the constructor hooks' adapters), every slot called through the object's own vtable;
// - adapter: variants built by the image's constructors (the game's vtable), every slot called through its RVA hook's
//   adapter (the helper a slot's jmp stub reaches, for the stubs), with a trap in the object's (copied) vtable wherever the
//   game's vtable holds that function, so an adapter that dispatched virtually fails. Folded slots (not ours to hook) call
//   the image's function on this route.
// Variants live on the heap (Release and slot 132 free them). The game's operator new / sized delete (0x824632 / 0x824624)
// and malloc / free (0x8252b1 / 0x6c4b50, under its new[] / delete[]) are ours, so blocks cross sides. Each transcript line:
// the step, its result (pointers as names: the object's own fields, its array, the fakes; texts as text), the frees of
// the blocks the step could free (the variants and their arrays, FreeRecorder), the variant(s) involved (vtable word as a
// code, payload (arrays: their contents), reference count, tag, count) and the fakes' state.
// The unknowns are fakes at fixed addresses (the same pointer text on every route): one that is not a string, one whose
// QueryInterface(cIGZString) hands out a cRZString (the original's code: its own slots), whose text is shown.
// Kept out (each crashes or reads freed or uninitialised memory in the original too): GetValString / CreateValString of an
// unknown variant holding null (0x9cb791 calls through it with no test), the bytes constructor 0x9c7be0 with a multi-byte
// kind (the count is the byte size, the element reads run past the block), a 0x8010 "array" with no elements (ToString
// prints its block's address), SetValVoid's kept-buffer path with no data (memcpy from null), SetFromString to an unknown
// with a non-zero number (the number is AddRef'd as an object), characters from 0x80 in texts that are parsed (the CRT's
// isdigit on a negative char: undefined in ours), and float arrays with huge elements ("%f" of 1e300 in the original's
// fixed Sprintf buffer).
//
// cRZVariant_Statics: the __cdecl helpers Convert 0x9c986c, ToFloat64 0x9c91d0, ToInt64 0x9c979c, ToString 0x9c92ac,
// FromString 0x9c8636, IsTrueText 0x9c9169, ParsePointer 0x9c91b3, FormatPointer 0x9c9152 on every tag, against ours, on
// variants with the game's vtable on both sides (their slots run the image's code on both).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "game/game.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using A = cRZVariantAdapters;

constexpr uint32_t kVTable = cRZVariant::kVTable;
constexpr unsigned kSlots = 133;
constexpr uint32_t kIIDString = 0x089b7dc8;

enum class Route { Original, VTable, Adapter };

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl GameMalloc(size_t n) { return std::malloc(n); }
void __cdecl GameFree(void *p) { std::free(p); }

struct Allocators {
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameMalloc{0x8252b1, shape::CodeOf(&GameMalloc)};
    shape::CodePatch gameFree{0x6c4b50, shape::CodeOf(&GameFree)};
};

std::string *gTranscript = nullptr;
int gTrapHits = 0;

void Note(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (gTranscript) *gTranscript += buf;
}

// ---- the fakes ----

// The string the string-capable unknown hands out: a cRZString built by our Construct (the game's vtable, so its slots run
// the image's code on every route); its reference count starts high so the variants' Releases never delete it.
union FakeString {
    cRZString s;
    FakeString() {}
    ~FakeString() {}
} gString;

void ResetString()
{
    static bool built = false;
    if (built) gString.s.Destruct();
    gString.s.Construct();
    gString.s.mString = "7";
    gString.s.mRefCount = 1000;
    built = true;
}

struct FakeUnknown {
    void *const *vtable;
    uint32_t refs;
    char name;
    bool string;
};

bool __fastcall FakeQueryInterface(FakeUnknown *self, void *, uint32_t iid, void **out)
{
    Note("[QI %c %x]", self->name, iid);
    if (iid == kIIDString && self->string) {
        *out = gString.s.AsInterface();
        gString.s.AsInterface()->AddRef();
        return true;
    }
    if (iid == 1) {
        *out = self;
        ++self->refs;
        return true;
    }
    return false;
}
uint32_t __fastcall FakeAddRef(FakeUnknown *self, void *)
{
    Note("[+%c]", self->name);
    return ++self->refs;
}
uint32_t __fastcall FakeRelease(FakeUnknown *self, void *)
{
    Note("[-%c]", self->name);
    return --self->refs;
}

void *const kFakeVTable[] = {reinterpret_cast<void *>(&FakeQueryInterface), reinterpret_cast<void *>(&FakeAddRef),
                             reinterpret_cast<void *>(&FakeRelease)};
FakeUnknown gPlain{kFakeVTable, 0, 'P', false};
FakeUnknown gStringy{kFakeVTable, 0, 'S', true};

cIGZUnknown *Fake(unsigned i)
{
    switch (i % 3) {
    case 0: return nullptr;
    case 1: return reinterpret_cast<cIGZUnknown *>(&gPlain);
    default: return reinterpret_cast<cIGZUnknown *>(&gStringy);
    }
}

void ResetFakes()
{
    gPlain.refs = 100;
    gStringy.refs = 100;
    ResetString();
}

std::string FakeState()
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), " P%u S%u str%u '", gPlain.refs, gStringy.refs, gString.s.mRefCount);
    std::string out = buf;
    for (unsigned char c : gString.s.mString) {
        if (c >= 0x20 && c < 0x7f && c != '\\') out += static_cast<char>(c);
        else {
            std::snprintf(buf, sizeof(buf), "\\%02x", c);
            out += buf;
        }
    }
    return out + "'";
}

// ---- describing ----

void **gOurVTable = nullptr;
void *gTrapCopy[4][kSlots];

void **OurVTable()
{
    if (!gOurVTable) {
        alignas(8) static uint8_t scratch[sizeof(cRZVariant)];
        cRZVariant *v = A::Construct(scratch, nullptr);
        gOurVTable = *reinterpret_cast<void ***>(scratch);
        v->cRZVariant::~cRZVariant();
    }
    return gOurVTable;
}

std::string VtCode(const void *object)
{
    const uint32_t word = shape::Word(object, 0);
    if (word == shape::P(GameAddress<void>(kVTable)) || word == shape::P(OurVTable())) return "VT";
    for (auto &copy : gTrapCopy)
        if (word == shape::P(copy)) return "VT";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "raw%08x", word);
    return buf;
}

std::string Escaped(const char *p, size_t n)
{
    std::string out;
    char buf[8];
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        if (c >= 0x20 && c < 0x7f && c != '\\') out += static_cast<char>(c);
        else {
            std::snprintf(buf, sizeof(buf), "\\%02x", c);
            out += buf;
        }
    }
    return out;
}

uint32_t ElementBytes(uint16_t tag)
{
    switch (tag & 0x7fff) {
    case 4: case 5: case 0xd: return 2;
    case 6: case 7: case 0xa: case 0xf: return 4;
    case 8: case 9: case 0xb: return 8;
    default: return 1; // the byte kinds, untyped bytes, and the bytes constructor's other tags (count = bytes)
    }
}

const cRZVariant *V(const void *object) { return static_cast<const cRZVariant *>(object); }

// A pointer as a name: null, a fake, the string, the object's array, a field of the object, or its value.
std::string Ptr(uint32_t p, const void *object)
{
    char buf[32];
    if (p == 0) return "null";
    if (p == shape::P(&gPlain)) return "P";
    if (p == shape::P(&gStringy)) return "S";
    if (p == shape::P(&gString.s)) return "str";
    if (object) {
        const cRZVariant *v = V(object);
        if ((v->mType & 0x8000) && p == shape::P(v->mArray)) return "arr";
        if (p >= shape::P(object) && p < shape::P(object) + sizeof(cRZVariant)) {
            std::snprintf(buf, sizeof(buf), "obj+%u", p - shape::P(object));
            return buf;
        }
    }
    std::snprintf(buf, sizeof(buf), "%08x", p);
    return buf;
}

std::string Dump(const void *object)
{
    const cRZVariant *v = V(object);
    char buf[96];
    std::string out = " {" + VtCode(object);
    std::snprintf(buf, sizeof(buf), " rc%u tag%04x n%u ", v->mRefCount, v->mType, v->mCount);
    out += buf;
    if (v->mType & 0x8000) {
        if (!v->mArray) {
            out += "arr null";
        } else {
            out += "[";
            const uint8_t *bytes = static_cast<const uint8_t *>(v->mArray);
            const uint32_t n = v->mCount * ElementBytes(v->mType);
            for (uint32_t i = 0; i < n && i < 512; ++i) {
                std::snprintf(buf, sizeof(buf), "%02x", bytes[i]);
                out += buf;
            }
            out += "]";
        }
        // The other payload word.
        std::snprintf(buf, sizeof(buf), " hi%08x", shape::Word(object, 8));
        out += buf;
    } else if (v->mType == cRZVariant::kTypeUnknown) {
        out += "unk " + Ptr(shape::Word(object, 4), nullptr);
        std::snprintf(buf, sizeof(buf), " hi%08x", shape::Word(object, 8));
        out += buf;
    } else {
        for (uint8_t b : v->mPayload) {
            std::snprintf(buf, sizeof(buf), "%02x", b);
            out += buf;
        }
    }
    return out + "}";
}

// The text of a cIGZString that is one of our cRZStrings (or the image's: same layout).
std::string TextOf(const void *string)
{
    const cRZString *s = static_cast<const cRZString *>(string);
    return "'" + Escaped(s->mString.data(), s->mString.size()) + "'";
}

// ---- values ----

const uint64_t kInts[] = {0, 1, 2, 0x30, 0x41, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff,
                          0x80000000, 0xffffffff, 0x100000000ull, 0x7fffffffffffffffull, 0x8000000000000000ull,
                          0xffffffffffffffffull, 0x123456789abcdef0ull, 0xfffffffffffffffeull, 0xffffffffffffff7full,
                          0xffffffff80000000ull};
const double kDoubles[] = {0.0, -0.0, 1.0, -1.0, 1.5, -2.5, 0.1, 255.9, -128.5, 65535.5, 3e9, -3e9, 4294967296.0,
                           1e19, 9.3e18, -9.3e18, 1.8446744073709552e19, 9.2233720368547758e18, 1e300, -1e300,
                           std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN(), 1e-300, 1e39, 2.5e-40, 123456.789, -7.0};
const char *const kTexts[] = {"", "t", "f", "true", "false", "TRUE", "False", "0", "1", "-1", "12", "-45", "255", "256",
                              "-129", "65536", "4294967295", "4294967296", "-2147483649", "18446744073709551615",
                              "-9223372036854775808", "99999999999999999999", "0x1F", "0xffffffff", "0x123456789a",
                              "0x", "0xZZ", "1.5", "-2.75", "1e5", "e5", "1.2.3", "--5", "-1e-5", "1e5e", ".", "1..2",
                              "1.e.5", "3-4", "abc", " 12 ", "  -7", "\t1,2,3", "1,2,3", "4 5 6", "a;b;c", "\"x y\",z",
                              "1, ,3", "true,false,0", "7;", ",", "1e400", "-1e400", "nan", "0.1e-3", "5e", "-",
                              "1-", "1e-", "2e-3-4", "0x10 20", "x", " ", "  \t", "\t"};
constexpr unsigned kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);
const uint16_t kTypes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x7fff, 0x8000,
                           0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007, 0x8008, 0x8009, 0x800a, 0x800b,
                           0x800c, 0x800d, 0x800e, 0x800f, 0x8010, 0x8011, 0xffff};
constexpr unsigned kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);
const char kSeparators[] = {' ', ',', ';', '-'};
const float kArrayFloats[] = {0.0f, 1.0f, -1.5f, 0.25f, 1e6f, -3e5f, 7.0f, 100.125f};
const double kArrayDoubles[] = {0.0, 2.0, -0.5, 1e8, 3.25, -12345.5, 0.001, 42.0};

// The values of an array setter's elements (`kind` & 0x7fff), `count` of them, into `data`: ASCII for the characters,
// moderate numbers for the floats, anything for the rest.
void FillArray(std::mt19937 &g, uint16_t kind, uint32_t count, uint8_t *data)
{
    for (uint32_t i = 0; i < count; ++i) {
        switch (kind & 0x7fff) {
        case 0xa: std::memcpy(data + 4 * i, &kArrayFloats[g() % 8], 4); break;
        case 0xb: std::memcpy(data + 8 * i, &kArrayDoubles[g() % 8], 8); break;
        case 0xc: case 0xe: case 0: data[i] = static_cast<uint8_t>(0x20 + g() % 0x5f); break;
        case 0xd: {
            const uint16_t c = static_cast<uint16_t>(0x20 + g() % 0x5f);
            std::memcpy(data + 2 * i, &c, 2);
            break;
        }
        case 1: data[i] = static_cast<uint8_t>(g() % 3 == 0 ? g() : g() % 2); break;
        default: {
            const uint32_t size = ElementBytes(kind);
            const uint64_t v = g() % 2 ? kInts[g() % (sizeof(kInts) / sizeof(kInts[0]))] : (uint64_t(g()) << 32 | g());
            std::memcpy(data + size * i, &v, size);
            break;
        }
        }
    }
}

// ---- the script ----

enum Op : unsigned { kOpCtor, kOpSlot, kOpAssign, kOpDestroy };

struct Step {
    unsigned object;
    unsigned other;
    Op op;
    unsigned what; // constructor kind, slot
    uint64_t integer;
    double real;
    uint32_t b, c;
    std::string text;
    alignas(8) uint8_t data[64];
};

// The constructors: 0 default, 1 copy, 2 text, 3 variant, 4 sint8, 5 uint8, 6 sint16, 7 uint16, 8 sint32, 9 uint32,
// 10 float, 11 double, 12 unknown, 13 bytes, 14 chars, 15 void pointer, 16 sint64, 17 uint64, 18 bool.
constexpr uint32_t kCtorRva[] = {0x9c7c75, 0x9c79da, 0x9c7a04, 0x9c7a40, 0x9c7a6a, 0x9c7a94, 0x9c7abe,
                                 0x9c7aea, 0x9c7b16, 0x9c7b3d, 0x9c7b64, 0x9c7b8f, 0x9c7bb6, 0x9c7be0,
                                 0x9c7c17, 0x9c7c4e, 0x9c7c8e, 0x9c7cb8, 0x9c7ce2};
constexpr unsigned kCtorKinds = sizeof(kCtorRva) / sizeof(kCtorRva[0]);

// Slots worth calling, weighted (each listed once; the getters and setters often).
std::vector<unsigned> SlotPool()
{
    std::vector<unsigned> pool;
    for (unsigned s = 0; s < kSlots - 1; ++s) pool.push_back(s);
    for (unsigned s = 10; s <= 54; ++s) pool.push_back(s); // the typed getters and setters twice
    for (unsigned s : {7u, 55u, 56u, 57u, 58u, 58u, 58u, 58u, 64u, 64u, 65u, 66u}) pool.push_back(s);
    for (unsigned s = 97; s <= 127; s += 2) pool.push_back(s); // the array setters twice
    return pool;
}

const std::vector<unsigned> &Pool()
{
    static const std::vector<unsigned> pool = SlotPool();
    return pool;
}

void FillStep(std::mt19937 &g, Step &s)
{
    s.integer = g() % 3 ? kInts[g() % (sizeof(kInts) / sizeof(kInts[0]))] : (uint64_t(g()) << 32 | g());
    s.real = kDoubles[g() % (sizeof(kDoubles) / sizeof(kDoubles[0]))];
    s.b = g();
    s.c = g();
    s.text = kTexts[g() % kTextCount];
    for (uint8_t &byte : s.data) byte = static_cast<uint8_t>(g());
}

std::vector<Step> MakeScript(uint32_t seed)
{
    std::mt19937 g(seed);
    std::vector<Step> script;
    bool live[3] = {false, false, false};
    auto ctor = [&](unsigned object) {
        Step s{};
        FillStep(g, s);
        s.object = object;
        s.op = kOpCtor;
        s.what = g() % kCtorKinds;
        // Copies are of another live object.
        s.other = g() % 3;
        if ((s.what == 1 || s.what == 3) && (!live[s.other] || s.other == object)) s.what = 0;
        if (s.what == 13) {
            // The bytes constructor: a single-byte kind (or none), and a 0x8010 with at least one pointer.
            static const uint16_t kByteKinds[] = {0, 1, 2, 3, 0xc, 0xe, 0x10, 0x11, 0x7fff};
            s.b = kByteKinds[g() % 9];
            s.c = 1 + g() % 12;
            FillArray(g, 0xe, s.c, s.data);
            if (s.b == 0x10) {
                s.c = 4 * (1 + g() % 3);
                for (uint32_t i = 0; i < s.c; i += 4) {
                    const uint32_t p = shape::P(Fake(g()));
                    std::memcpy(s.data + i, &p, 4);
                }
            }
        }
        if (s.what == 14) FillArray(g, 0xc, 15, s.data), s.data[g() % 16] = 0;
        script.push_back(s);
        live[object] = true;
    };
    for (unsigned i = 0; i < 3; ++i) ctor(i);
    const unsigned steps = 30 + g() % 40;
    for (unsigned n = 0; n < steps; ++n) {
        const unsigned object = g() % 3;
        if (!live[object]) {
            ctor(object);
            continue;
        }
        Step s{};
        FillStep(g, s);
        s.object = object;
        s.other = g() % 3;
        if (!live[s.other]) s.other = object;
        const unsigned r = g() % 100;
        if (r < 6) {
            ctor(object);
            continue;
        }
        if (r < 10) {
            s.op = kOpAssign;
        } else if (r < 13) {
            s.op = kOpDestroy;
            s.what = g() % 3; // 0 destructor, 1 slot 132 flags 1, 2 slot 132 flags 0
            live[object] = false;
        } else {
            s.op = kOpSlot;
            s.what = Pool()[g() % Pool().size()]; // Release (2) may delete: the runner rebuilds the object then
            const unsigned slot = s.what;
            if (slot >= 97 && slot <= 127 && slot % 2 == 1) {
                // Array setters: a count and elements of the kind.
                static const uint16_t kKinds[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0};
                const uint16_t kind = kKinds[(slot - 97) / 2];
                s.c = g() % 7;
                if (kind == 0) s.c = g() % 17;
                FillArray(g, kind, s.c, s.data);
                if (g() % 3 == 0) {
                    // Narrow: 2-4 elements, each 0 or 1 (1.0 for the floats), so that two objects' arrays often agree
                    // in some elements and not others (IsEqual must compare every element).
                    s.c = 2 + g() % 3;
                    const uint32_t size = kind == 0 ? 1 : ElementBytes(0x8000 | kind);
                    std::memset(s.data, 0, sizeof(s.data));
                    for (uint32_t i = 0; i < s.c; ++i) {
                        if (g() % 2 == 0) continue;
                        if (kind == 0xa) std::memcpy(s.data + 4 * i, &kArrayFloats[1], 4);
                        else if (kind == 0xb) std::memcpy(s.data + 8 * i, &kArrayDoubles[1], 8);
                        else s.data[size * i] = kind == 0xc || kind == 0xd || kind == 0xe ? 'b' : 1;
                    }
                    if (kind == 0xc || kind == 0xd || kind == 0xe)
                        for (uint32_t i = 0; i < s.c; ++i) if (!s.data[size * i]) s.data[size * i] = 'a';
                }
            }
            if (slot == 60) {
                s.c = g() % 17;
                FillArray(g, 0, s.c, s.data);
            }
            if (slot == 58) {
                s.b = kTypes[g() % kTypeCount];
                s.c = g() % 5 == 0 ? g() % 4 : 0;
                if (s.b == 0x10) s.text = g() % 2 ? "zz" : "0";
            }
        }
        script.push_back(s);
    }
    // Release (slot 2) may delete; the runner tracks it. Everything left is destroyed at the end.
    return script;
}

// ---- running ----

template <class R, class... Args> R CallThis(void *fn, const void *self, Args... args)
{
    return reinterpret_cast<R(__thiscall *)(const void *, Args...)>(fn)(self, args...);
}

void *Adapter(unsigned slot)
{
    switch (slot) {
    case 0: return reinterpret_cast<void *>(&A::QueryInterface);
    case 1: return reinterpret_cast<void *>(&A::AddRef);
    case 2: return reinterpret_cast<void *>(&A::Release);
    case 3: return reinterpret_cast<void *>(&A::GetType);
    case 5: return reinterpret_cast<void *>(&A::IsUndefined);
    case 6: return reinterpret_cast<void *>(&A::Erase);
    case 7: return reinterpret_cast<void *>(&A::CopyFrom);
    case 8: return reinterpret_cast<void *>(&A::CreateCopy);
    case 10: return reinterpret_cast<void *>(&A::ValBool);
    case 11: return reinterpret_cast<void *>(&A::GetValBool);
    case 12: return reinterpret_cast<void *>(&A::StoreBool);
    case 13: return reinterpret_cast<void *>(&A::ValUint8);
    case 14: return reinterpret_cast<void *>(&A::GetValUint8);
    case 15: return reinterpret_cast<void *>(&A::StoreUint8);
    case 16: return reinterpret_cast<void *>(&A::ValSint8);
    case 17: return reinterpret_cast<void *>(&A::GetValSint8);
    case 18: return reinterpret_cast<void *>(&A::StoreSint8);
    case 19: return reinterpret_cast<void *>(&A::ValUint16);
    case 20: return reinterpret_cast<void *>(&A::GetValUint16);
    case 21: return reinterpret_cast<void *>(&A::StoreUint16);
    case 22: return reinterpret_cast<void *>(&A::ValSint16);
    case 23: return reinterpret_cast<void *>(&A::GetValSint16);
    case 24: return reinterpret_cast<void *>(&A::StoreSint16);
    case 25: return reinterpret_cast<void *>(&A::ValUint32);
    case 26: return reinterpret_cast<void *>(&A::GetValUint32);
    case 27: return reinterpret_cast<void *>(&A::StoreUint32);
    case 28: return reinterpret_cast<void *>(&A::ValSint32);
    case 29: return reinterpret_cast<void *>(&A::GetValSint32);
    case 30: return reinterpret_cast<void *>(&A::StoreSint32);
    case 31: return reinterpret_cast<void *>(&A::ValUint64);
    case 32: return reinterpret_cast<void *>(&A::GetValUint64);
    case 33: return reinterpret_cast<void *>(&A::StoreUint64);
    case 34: return reinterpret_cast<void *>(&A::ValSint64);
    case 35: return reinterpret_cast<void *>(&A::GetValSint64);
    case 36: return reinterpret_cast<void *>(&A::StoreSint64);
    case 37: return reinterpret_cast<void *>(&A::ValFloat32);
    case 38: return reinterpret_cast<void *>(&A::GetValFloat32);
    case 39: return reinterpret_cast<void *>(&A::StoreFloat32);
    case 40: return reinterpret_cast<void *>(&A::ValFloat64);
    case 41: return reinterpret_cast<void *>(&A::GetValFloat64);
    case 42: return reinterpret_cast<void *>(&A::StoreFloat64);
    case 43: return reinterpret_cast<void *>(&A::ValChar);
    case 44: return reinterpret_cast<void *>(&A::GetValChar);
    case 45: return reinterpret_cast<void *>(&A::StoreChar);
    case 46: return reinterpret_cast<void *>(&A::ValRZUnicodeChar);
    case 47: return reinterpret_cast<void *>(&A::GetValRZUnicodeChar);
    case 48: return reinterpret_cast<void *>(&A::StoreRZUnicodeChar);
    case 49: return reinterpret_cast<void *>(&A::ValRZChar);
    case 50: return reinterpret_cast<void *>(&A::GetValRZChar);
    case 51: return reinterpret_cast<void *>(&A::StoreRZChar);
    case 52: return reinterpret_cast<void *>(&A::ValVoidPtr);
    case 53: return reinterpret_cast<void *>(&A::GetValVoidPtr);
    case 54: return reinterpret_cast<void *>(&A::StoreVoidPtr);
    case 55: return reinterpret_cast<void *>(&A::CreateValString);
    case 56: return reinterpret_cast<void *>(&A::TextOf);
    case 57: return reinterpret_cast<void *>(&A::StoreText);
    case 58: return reinterpret_cast<void *>(&A::SetFromString);
    case 59: return reinterpret_cast<void *>(&A::GetValVoid);
    case 60: return reinterpret_cast<void *>(&A::SetValVoid);
    case 61: return reinterpret_cast<void *>(&A::ValUnknown);
    case 62: case 130: return reinterpret_cast<void *>(&A::QueryUnknown);
    case 63: case 129: return reinterpret_cast<void *>(&A::StoreUnknown);
    case 64: return reinterpret_cast<void *>(&A::IsEqual);
    case 65: return reinterpret_cast<void *>(&A::IsNotEqual);
    case 66: return reinterpret_cast<void *>(&A::AssignFrom);
    case 81: case 83: return reinterpret_cast<void *>(&A::AsUint64);
    case 97: return reinterpret_cast<void *>(&A::SetBoolArray);
    case 99: return reinterpret_cast<void *>(&A::SetUint8Array);
    case 101: return reinterpret_cast<void *>(&A::SetSint8Array);
    case 103: return reinterpret_cast<void *>(&A::SetUint16Array);
    case 105: return reinterpret_cast<void *>(&A::SetSint16Array);
    case 107: return reinterpret_cast<void *>(&A::SetUint32Array);
    case 109: return reinterpret_cast<void *>(&A::SetSint32Array);
    case 111: return reinterpret_cast<void *>(&A::SetUint64Array);
    case 113: return reinterpret_cast<void *>(&A::SetSint64Array);
    case 115: return reinterpret_cast<void *>(&A::SetFloat32Array);
    case 117: return reinterpret_cast<void *>(&A::SetFloat64Array);
    case 119: return reinterpret_cast<void *>(&A::SetCharArray);
    case 121: return reinterpret_cast<void *>(&A::SetRZUnicodeCharArray);
    case 123: return reinterpret_cast<void *>(&A::SetRZCharArray);
    case 125: return reinterpret_cast<void *>(&A::SetVoidPtrArray);
    case 127: return reinterpret_cast<void *>(&A::SetVoidArray);
    case 132: return reinterpret_cast<void *>(&A::DeletingDestructor);
    default: return nullptr; // folded (not ours to hook): the image's
    }
}

// Stack words of each slot (its `ret N` / 4).
unsigned Words(unsigned slot)
{
    switch (slot) {
    case 0: case 59: case 60: case 62: case 130: return 2;
    case 7: case 8: case 9: case 57: case 63: case 64: case 65: case 66: case 129: case 132: return 1;
    case 33: case 36: case 42: return 2;
    case 55: case 56: return 3;
    case 58: return 4;
    default: break;
    }
    if (slot >= 10 && slot <= 54) return (slot - 10) % 3 == 0 ? 0 : 1;
    if (slot >= 97 && slot <= 128) return slot % 2 ? 2 : 0;
    return 0;
}

// The adapter route: a copy of the object's vtable with a trap wherever the game's vtable holds the slot's function.
struct TrapScope {
    void **object = nullptr;
    void **saved = nullptr;
    void **copy;
    TrapScope(Route route, void *target, unsigned index, unsigned slot) : copy(gTrapCopy[index])
    {
        if (route != Route::Adapter) return;
        object = static_cast<void **>(target);
        saved = static_cast<void **>(*object);
        std::memcpy(copy, saved, sizeof(gTrapCopy[0]));
        void *const *game = shape::GameVTable(kVTable);
        for (unsigned s = 0; s < kSlots; ++s)
            if (game[s] == game[slot]) copy[s] = shape::TrapFor<shape::Count<gTrapHits>>(Words(s));
        *object = copy;
    }
    void Restore()
    {
        if (object && *object == copy) *object = saved;
        object = nullptr;
    }
    ~TrapScope() { Restore(); }
};

void *SlotFunction(Route route, const void *object, unsigned slot)
{
    if (route == Route::Original) return shape::GameVTable(kVTable)[slot];
    if (route == Route::VTable) return (*static_cast<void *const *const *>(object))[slot];
    void *adapter = Adapter(slot);
    return adapter ? adapter : shape::GameVTable(kVTable)[slot];
}

// A cRZString on the heap built by our Construct (the game's vtable), for the text arguments.
struct ArgString {
    cRZString *s;
    explicit ArgString(const std::string &text)
    {
        s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
        s->Construct();
        s->mString = text;
    }
    ~ArgString()
    {
        s->Destruct();
        ::operator delete(s);
    }
    cIGZString *Face() { return s->AsInterface(); }
};

std::string Bits(const void *p, size_t n)
{
    std::string out;
    char buf[4];
    for (size_t i = n; i-- > 0;) {
        std::snprintf(buf, sizeof(buf), "%02x", static_cast<const uint8_t *>(p)[i]);
        out += buf;
    }
    return out;
}

// Runs slot `slot` of `object` with the step's arguments; returns the result's description.
std::string RunSlot(Route route, const Step &s, void *object, void *other, bool &deleted)
{
    const unsigned slot = s.what;
    const cRZVariant *v = V(object);
    void *fn = SlotFunction(route, object, slot);
    TrapScope trap(route, object, s.object, slot);
    char buf[160];
    auto byte = [](uint32_t r) { return std::to_string(r & 0xff); };
    if (slot >= 10 && slot <= 54) {
        const unsigned kind = (slot - 10) / 3, which = (slot - 10) % 3;
        static const unsigned kSize[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 1, 2, 1, 4};
        const unsigned size = kSize[kind];
        if (which == 0) {
            // ValX
            uint64_t r = 0;
            if (kind == 9) {
                const float f = CallThis<float>(fn, object);
                std::memcpy(&r, &f, 4);
            } else if (kind == 10) {
                const double d = CallThis<double>(fn, object);
                std::memcpy(&r, &d, 8);
            } else if (size == 8) {
                r = CallThis<uint64_t>(fn, object);
            } else {
                const uint32_t w = CallThis<uint32_t>(fn, object);
                r = size == 4 ? w : size == 2 ? (w & 0xffff) : (w & 0xff);
            }
            if (kind == 14) return " -> " + Ptr(static_cast<uint32_t>(r), object);
            return " -> " + Bits(&r, size);
        }
        if (which == 1) {
            // GetValX: the out word starts as a pattern; what is written shows.
            uint64_t out = 0x5a5a5a5a5a5a5a5aull;
            const uint32_t r = CallThis<uint32_t>(fn, object, &out);
            if (kind == 14) return " -> " + byte(r) + " " + Ptr(static_cast<uint32_t>(out), object);
            return " -> " + byte(r) + " " + Bits(&out, 8);
        }
        // SetValX
        if (kind == 9) CallThis<void>(fn, object, static_cast<float>(s.real));
        else if (kind == 10) CallThis<void>(fn, object, s.real);
        else if (size == 8) CallThis<void>(fn, object, s.integer);
        else if (kind == 14) CallThis<void>(fn, object, static_cast<uint32_t>(s.integer));
        else {
            const uint32_t mask = size == 4 ? 0xffffffffu : size == 2 ? 0xffffu : 0xffu;
            CallThis<void>(fn, object, static_cast<uint32_t>(s.integer) & mask);
        }
        return "";
    }
    if (slot >= 67 && slot <= 96) {
        if (slot == 96) return " -> " + Ptr(CallThis<uint32_t>(fn, object), object);
        const unsigned kind = (slot - 67) / 2;
        if (slot % 2 == 0) return " -> " + Ptr(CallThis<uint32_t>(fn, object), object); // AsXRef
        static const unsigned kSize[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 1, 2, 1, 4};
        uint64_t r = 0;
        if (kind == 9) {
            const float f = CallThis<float>(fn, object);
            std::memcpy(&r, &f, 4);
        } else if (kind == 10) {
            const double d = CallThis<double>(fn, object);
            std::memcpy(&r, &d, 8);
        } else if (kSize[kind] == 8) {
            r = CallThis<uint64_t>(fn, object);
        } else {
            const uint32_t w = CallThis<uint32_t>(fn, object);
            r = kSize[kind] == 4 ? w : kSize[kind] == 2 ? (w & 0xffff) : (w & 0xff);
        }
        if (kind == 14) return " -> " + Ptr(static_cast<uint32_t>(r), object);
        if ((v->mType & 0x8000) && v->mArray) {
            // The payload is the array's address (differs per route): its bytes as "the address's", the rest as they are.
            uint64_t address = shape::P(v->mArray) | uint64_t(shape::Word(object, 8)) << 32;
            const uint64_t mask = kSize[kind] == 8 ? ~0ull : (1ull << (8 * kSize[kind])) - 1;
            return (r & mask) == (address & mask) ? " -> addr" : " -> " + Bits(&r, kSize[kind]);
        }
        return " -> " + Bits(&r, kSize[kind]);
    }
    if (slot >= 97 && slot <= 128) {
        if (slot % 2 == 0) return " -> " + Ptr(CallThis<uint32_t>(fn, object), object);
        CallThis<void>(fn, object, static_cast<const void *>(s.data), s.c);
        return "";
    }
    switch (slot) {
    case 0: {
        static const uint32_t kIids[] = {1, 0x60fd4d2b, 2, kIIDString};
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = CallThis<uint32_t>(fn, object, kIids[s.b % 4], &out);
        return " -> " + byte(r) + " " + Ptr(shape::P(out), object);
    }
    case 1: return " -> " + std::to_string(CallThis<uint32_t>(fn, object));
    case 2: {
        const uint32_t refs = v->mRefCount;
        const uint32_t r = CallThis<uint32_t>(fn, object);
        trap.Restore();
        deleted = refs <= 1;
        return " -> " + std::to_string(r);
    }
    case 3: case 4: return " -> " + std::to_string(CallThis<uint32_t>(fn, object));
    case 5: case 6: return " -> " + byte(CallThis<uint32_t>(fn, object));
    case 7: case 64: case 65: return " -> " + byte(CallThis<uint32_t>(fn, object, other));
    case 66: {
        const uint32_t r = CallThis<uint32_t>(fn, object, other);
        return r == shape::P(object) ? " -> self" : " -> other";
    }
    case 8: {
        void *copy = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = CallThis<uint32_t>(fn, object, &copy);
        trap.Restore();
        std::string out = " -> " + byte(r) + Dump(copy);
        // The copy goes through its own Release (reference count 1: deleted).
        CallThis<uint32_t>((*static_cast<void *const *const *>(copy))[2], copy);
        return out;
    }
    case 9: return " -> " + byte(CallThis<uint32_t>(fn, object, s.b % 0x20));
    case 55: {
        if (v->mType == cRZVariant::kTypeUnknown && !v->mUnknown) return " skipped";
        void *text = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = CallThis<uint32_t>(fn, object, &text, s.b % 4, static_cast<uint32_t>(kSeparators[s.c % 4]));
        trap.Restore();
        std::string out = " -> " + byte(r) + (text ? " " + TextOf(text) : " null");
        if (text) {
            std::snprintf(buf, sizeof(buf), " rc%u", static_cast<cRZString *>(text)->mRefCount);
            out += buf;
            CallThis<uint32_t>((*static_cast<void *const *const *>(text))[2], text);
        }
        return out;
    }
    case 56: {
        if (v->mType == cRZVariant::kTypeUnknown && !v->mUnknown) return " skipped";
        ArgString text("old");
        const uint32_t r = CallThis<uint32_t>(fn, object, text.Face(), s.b % 4, static_cast<uint32_t>(kSeparators[s.c % 4]));
        return " -> " + byte(r) + " " + TextOf(text.s);
    }
    case 57: {
        ArgString text(s.text);
        CallThis<void>(fn, object, static_cast<const cIGZString *>(text.Face()));
        return "";
    }
    case 58: {
        ArgString text(s.text);
        const uint32_t r = CallThis<uint32_t>(fn, object, static_cast<const cIGZString *>(text.Face()), uint32_t(s.b), s.c,
                                              static_cast<uint32_t>(kSeparators[s.integer % 4]));
        std::snprintf(buf, sizeof(buf), "(%04x,%u,'%c') '", s.b, s.c, kSeparators[s.integer % 4]);
        return buf + Escaped(s.text.data(), s.text.size()) + "' -> " + byte(r);
    }
    case 59: {
        uint8_t out[80];
        std::memset(out, 0xa5, sizeof(out));
        // The capacity: around the stored count (one under, equal, one over) or anything up to 23. The count is the same
        // on every route (the transcripts agree up to here).
        uint32_t size = s.b % 24;
        if (v->mType == 0x8000 && s.b % 4 != 0) size = (std::min)(v->mCount + s.b % 4 - 2 + (v->mCount == 0), 23u);
        const uint32_t r = CallThis<uint32_t>(fn, object, static_cast<void *>(out), &size);
        return " -> " + byte(r) + " size " + std::to_string(size) + " " + Bits(out, 24);
    }
    case 60: CallThis<void>(fn, object, static_cast<const void *>(s.data), s.c); return "";
    case 61: return " -> " + Ptr(CallThis<uint32_t>(fn, object), object);
    case 62: case 130: {
        static const uint32_t kIids[] = {1, kIIDString, 0x60fd4d2b, 5};
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = CallThis<uint32_t>(fn, object, kIids[s.b % 4], &out);
        trap.Restore();
        std::string result = " -> " + byte(r) + " " + Ptr(shape::P(out), object);
        if ((r & 0xff) && out) CallThis<uint32_t>((*static_cast<void *const *const *>(out))[2], out);
        return result;
    }
    case 63: case 129: CallThis<void>(fn, object, Fake(s.b)); return "";
    case 131: return " -> " + Ptr(CallThis<uint32_t>(fn, object), object);
    default: return " ?";
    }
}

struct Run {
    Route route;
    difftest::Context &ctx;
    void *objects[3] = {};
};

void Construct(Run &run, const Step &s)
{
    void *memory = ::operator new(sizeof(cRZVariant));
    std::memset(memory, 0xcd, sizeof(cRZVariant));
    run.objects[s.object] = memory;
    void *other = run.objects[s.other];
    const uint32_t rva = kCtorRva[s.what];
    const char *chars = reinterpret_cast<const char *>(s.data);
    ArgString text(s.text);
    if (run.route != Route::VTable) {
        auto f = [&](auto... args) { CallThis<void *>(run.ctx.Original<void *>(rva), memory, args...); };
        switch (s.what) {
        case 0: f(); break;
        case 1: case 3: f(other); break;
        case 2: f(text.Face()); break;
        case 4: case 5: f(static_cast<uint32_t>(s.integer & 0xff)); break;
        case 6: case 7: f(static_cast<uint32_t>(s.integer & 0xffff)); break;
        case 8: case 9: f(static_cast<uint32_t>(s.integer)); break;
        case 10: f(static_cast<float>(s.real)); break;
        case 11: f(s.real); break;
        case 12: f(Fake(s.b)); break;
        case 13: f(static_cast<const void *>(s.data), s.c, s.b); break;
        case 14: f(chars); break;
        case 15: f(static_cast<uint32_t>(s.integer)); break;
        case 16: case 17: f(s.integer); break;
        case 18: f(static_cast<uint32_t>(s.integer & 1)); break;
        }
        return;
    }
    switch (s.what) {
    case 0: A::Construct(memory, nullptr); break;
    case 1: A::ConstructCopy(memory, nullptr, V(other)); break;
    case 2: A::ConstructText(memory, nullptr, text.Face()); break;
    case 3: A::ConstructVariant(memory, nullptr, static_cast<const cIGZVariant *>(V(other))); break;
    case 4: A::ConstructSint8(memory, nullptr, static_cast<int8_t>(s.integer)); break;
    case 5: A::ConstructUint8(memory, nullptr, static_cast<uint8_t>(s.integer)); break;
    case 6: A::ConstructSint16(memory, nullptr, static_cast<int16_t>(s.integer)); break;
    case 7: A::ConstructUint16(memory, nullptr, static_cast<uint16_t>(s.integer)); break;
    case 8: A::ConstructSint32(memory, nullptr, static_cast<int32_t>(s.integer)); break;
    case 9: A::ConstructUint32(memory, nullptr, static_cast<uint32_t>(s.integer)); break;
    case 10: A::ConstructFloat32(memory, nullptr, static_cast<float>(s.real)); break;
    case 11: A::ConstructFloat64(memory, nullptr, s.real); break;
    case 12: A::ConstructUnknown(memory, nullptr, Fake(s.b)); break;
    case 13: A::ConstructBytes(memory, nullptr, s.data, s.c, s.b); break;
    case 14: A::ConstructChars(memory, nullptr, chars); break;
    case 15: A::ConstructVoidPtr(memory, nullptr, reinterpret_cast<void *>(static_cast<uintptr_t>(s.integer))); break;
    case 16: A::ConstructSint64(memory, nullptr, static_cast<int64_t>(s.integer)); break;
    case 17: A::ConstructUint64(memory, nullptr, s.integer); break;
    case 18: A::ConstructBool(memory, nullptr, (s.integer & 1) != 0); break;
    }
}

// The blocks a step could free: the variants and their arrays.
struct Watched {
    const void *p;
    std::string name;
};

std::vector<Watched> Watch(const Run &run)
{
    std::vector<Watched> list;
    for (unsigned i = 0; i < 3; ++i) {
        const void *o = run.objects[i];
        if (!o) continue;
        list.push_back({o, "o" + std::to_string(i)});
        if ((V(o)->mType & 0x8000) && V(o)->mArray) list.push_back({V(o)->mArray, "a" + std::to_string(i)});
    }
    return list;
}

std::string Frees(const difftest::FreeRecorder &rec, const std::vector<Watched> &list)
{
    std::string out;
    for (const Watched &w : list)
        if (rec.Freed(w.p)) out += " free:" + w.name;
    return out;
}

void Destroy(Run &run, unsigned index, unsigned how, std::string &line)
{
    void *object = run.objects[index];
    if (how == 0) {
        if (run.route == Route::Original) CallThis<void>(run.ctx.Original<void *>(0x9c7d0c), object);
        else A::Destruct(static_cast<cRZVariant *>(object), nullptr);
        line += Dump(object);
        ::operator delete(object);
    } else {
        void *fn = SlotFunction(run.route, object, 132);
        TrapScope trap(run.route, object, index, 132);
        const uint32_t r = CallThis<uint32_t>(fn, object, how == 1 ? 1u : 0u);
        trap.Restore();
        line += r == shape::P(object) ? " -> self" : " -> other";
        if (how == 2) {
            line += Dump(object);
            ::operator delete(object);
        }
    }
    run.objects[index] = nullptr;
}

std::string RunScript(difftest::Context &ctx, Route route, uint32_t seed, const std::vector<Step> &script)
{
    std::string transcript;
    gTranscript = &transcript;
    ResetFakes();
    Run run{route, ctx};
    char buf[96];
    for (const Step &s : script) {
        std::snprintf(buf, sizeof(buf), "route %d seed %u step %u", static_cast<int>(route), seed,
                      static_cast<unsigned>(&s - script.data()));
        ctx.Mark(buf);
        std::string line;
        const std::vector<Watched> watched = Watch(run);
        difftest::FreeRecorder rec;
        switch (s.op) {
        case kOpCtor:
            if (run.objects[s.object]) Destroy(run, s.object, 0, line);
            Construct(run, s);
            rec.Stop();
            std::snprintf(buf, sizeof(buf), "ctor%u o%u", s.what, s.object);
            line = buf + line + Dump(run.objects[s.object]);
            break;
        case kOpAssign: {
            void *object = run.objects[s.object];
            const void *other = run.objects[s.other];
            uint32_t r;
            if (route == Route::Original)
                r = CallThis<uint32_t>(ctx.Original<void *>(0x9c7d18), object, other);
            else
                r = shape::P(A::Assign(static_cast<cRZVariant *>(object), nullptr, V(other)));
            rec.Stop();
            std::snprintf(buf, sizeof(buf), "assign o%u = o%u%s", s.object, s.other, r == shape::P(object) ? " -> self" : " -> ?");
            line = buf + Dump(object);
            break;
        }
        case kOpDestroy:
            std::snprintf(buf, sizeof(buf), "destroy%u o%u", s.what, s.object);
            line = buf;
            Destroy(run, s.object, s.what, line);
            rec.Stop();
            break;
        case kOpSlot: {
            void *object = run.objects[s.object];
            void *other = run.objects[s.other] ? run.objects[s.other] : object;
            bool deleted = false;
            std::snprintf(buf, sizeof(buf), "slot%u o%u o%u", s.what, s.object, s.other);
            line = buf + RunSlot(route, s, object, other, deleted);
            rec.Stop();
            if (deleted) {
                run.objects[s.object] = nullptr;
                line += " deleted";
            } else {
                line += Dump(object);
            }
            if (other != object && (s.what == 7 || s.what == 64 || s.what == 65 || s.what == 66)) line += Dump(other);
            break;
        }
        }
        line += Frees(rec, watched) + FakeState() + "\n";
        transcript += line;
        // Objects a Release deleted are rebuilt by the next step that needs them.
        if (!run.objects[s.object] && s.op == kOpSlot) {
            Step rebuild{};
            rebuild.object = s.object;
            rebuild.op = kOpCtor;
            Construct(run, rebuild);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!run.objects[i]) continue;
        const std::vector<Watched> watched = Watch(run);
        difftest::FreeRecorder rec;
        std::string line = "end o" + std::to_string(i);
        Destroy(run, i, 0, line);
        rec.Stop();
        transcript += line + Frees(rec, watched) + FakeState() + "\n";
    }
    gTranscript = nullptr;
    return transcript;
}

void CompareTranscripts(difftest::Context &ctx, uint32_t seed, const char *route, const std::string &a, const std::string &b)
{
    if (a == b) return;
    size_t start = 0;
    for (;;) {
        size_t ea = a.find('\n', start), eb = b.find('\n', start);
        std::string la = a.substr(start, ea == std::string::npos ? std::string::npos : ea - start);
        std::string lb = b.substr(start, eb == std::string::npos ? std::string::npos : eb - start);
        if (la != lb || ea == std::string::npos || eb == std::string::npos) {
            ctx.Fail("seed %u (%s): original: %.230s", seed, route, la.c_str());
            ctx.Fail("seed %u (%s): ours:     %.230s", seed, route, lb.c_str());
            return;
        }
        start = ea + 1;
    }
}

void RunRoutes(difftest::Context &ctx, Route ours, const char *name, uint32_t cases)
{
    Allocators allocators;
    OurVTable();
    for (uint32_t seed = 1; seed <= cases && ctx.failures.size() < 4; ++seed) {
        const std::vector<Step> script = MakeScript(seed);
        const std::string original = RunScript(ctx, Route::Original, seed, script);
        gTrapHits = 0;
        const std::string replacement = RunScript(ctx, ours, seed, script);
        if (gTrapHits != 0) ctx.Fail("seed %u (%s): %d trap hits (an adapter dispatched virtually)", seed, name, gTrapHits);
        CompareTranscripts(ctx, seed, name, original, replacement);
        ++ctx.cases;
    }
}

// ---- the statics ----

// A variant with the game's vtable (the image's constructor), then the fields set raw.
struct RawVariant {
    alignas(8) uint8_t bytes[sizeof(cRZVariant)];
    cRZVariant *Get() { return reinterpret_cast<cRZVariant *>(bytes); }
};

void MakeRaw(difftest::Context &ctx, RawVariant &raw, uint16_t tag, std::mt19937 &g)
{
    CallThis<void *>(ctx.Original<void *>(0x9c7c75), raw.bytes);
    cRZVariant *v = raw.Get();
    v->mType = tag;
    v->mRefCount = static_cast<uint16_t>(g() % 3);
    if (tag & 0x8000) {
        uint32_t count = 1 + g() % 5;
        uint8_t data[64];
        FillArray(g, tag, count, data);
        if (tag == 0x8010) {
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t p = shape::P(Fake(g()));
                std::memcpy(data + 4 * i, &p, 4);
            }
            count *= 4; // the bytes constructor's count: bytes
        }
        const uint32_t bytes = count * ElementBytes(tag);
        v->mArray = std::malloc(bytes ? bytes : 1);
        std::memcpy(v->mArray, data, bytes);
        v->mCount = count;
        if (g() % 8 == 0 && tag != 0x8010) {
            std::free(v->mArray);
            v->mArray = nullptr;
            v->mCount = 0; // no buffer, no elements (the formatters read the elements unchecked)
        }
    } else if (tag == cRZVariant::kTypeUnknown) {
        v->mUnknown = Fake(g());
    } else if (g() % 2) {
        const double d = kDoubles[g() % (sizeof(kDoubles) / sizeof(kDoubles[0]))];
        if (tag == 0xa) {
            const float f = static_cast<float>(d);
            std::memcpy(v->mPayload, &f, 4);
            v->mPayload[4] = static_cast<uint8_t>(g());
        } else {
            std::memcpy(v->mPayload, &d, 8);
        }
    } else {
        const uint64_t x = kInts[g() % (sizeof(kInts) / sizeof(kInts[0]))];
        std::memcpy(v->mPayload, &x, 8);
    }
}

// Frees what a raw variant holds without calling anything of it.
void FreeRaw(RawVariant &raw)
{
    cRZVariant *v = raw.Get();
    if ((v->mType & 0x8000) && v->mArray) std::free(v->mArray);
}

std::string DumpRaw(RawVariant &raw) { return Dump(raw.bytes); }

} // namespace

DIFF_TEST(cRZVariant_VTable, 0x9c9a1e)
{
    RunRoutes(ctx, Route::VTable, "vtable", 1500);
}

DIFF_TEST(cRZVariant_Adapters, 0x9c9a1e)
{
    RunRoutes(ctx, Route::Adapter, "adapter", 1000);
}

// The statics on every tag: each side's output variant starts the same (the game's vtable, a tag, a payload), and the
// slots they call on it are the image's on both sides.
DIFF_TEST(cRZVariant_Statics, 0x9c986c)
{
    Allocators allocators;
    using ConvertFn = bool(__cdecl *)(const cRZVariant *, cRZVariant *);
    using ToStringFn = bool(__cdecl *)(const cRZVariant *, cRZString *, uint32_t, char);
    using FromStringFn = bool(__cdecl *)(const cRZString *, cRZVariant *);
    using IsTrueFn = bool(__cdecl *)(const cRZString *);
    using ParsePointerFn = void(__cdecl *)(const cRZString *, void *);
    using FormatPointerFn = void(__cdecl *)(const void *, cRZString *);
    const ConvertFn convert[2] = {ctx.Original<ConvertFn>(0x9c986c), &cRZVariant::Convert};
    const ConvertFn toFloat[2] = {ctx.Original<ConvertFn>(0x9c91d0), &cRZVariant::ToFloat64};
    const ConvertFn toInt[2] = {ctx.Original<ConvertFn>(0x9c979c), &cRZVariant::ToInt64};
    const ToStringFn toString[2] = {ctx.Original<ToStringFn>(0x9c92ac), &cRZVariant::ToString};
    const FromStringFn fromString[2] = {ctx.Original<FromStringFn>(0x9c8636), &cRZVariant::FromString};
    const IsTrueFn isTrue[2] = {ctx.Original<IsTrueFn>(0x9c9169), &cRZVariant::IsTrueText};
    const ParsePointerFn parsePointer[2] = {ctx.Original<ParsePointerFn>(0x9c91b3), &cRZVariant::ParsePointer};
    const FormatPointerFn formatPointer[2] = {ctx.Original<FormatPointerFn>(0x9c9152), &cRZVariant::FormatPointer};

    for (uint32_t seed = 1; seed <= 40000 && ctx.failures.size() < 8; ++seed) {
        const unsigned kind = seed % 6;
        std::string out[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 g(seed);
            std::string &line = out[side];
            gTranscript = &line;
            ResetFakes();
            char buf[96];
            std::snprintf(buf, sizeof(buf), "statics seed %u side %d", seed, side);
            ctx.Mark(buf);
            const uint16_t fromTag = kTypes[g() % kTypeCount];
            RawVariant from, to;
            MakeRaw(ctx, from, fromTag, g);
            ArgString text(kTexts[g() % kTextCount]);
            switch (kind) {
            case 0: { // Convert into the kinds the getters convert to, and the others
                const uint16_t toTag = g() % 4 ? static_cast<uint16_t>(1 + g() % 0xf) : kTypes[g() % kTypeCount];
                CallThis<void *>(ctx.Original<void *>(0x9c7c75), to.bytes);
                to.Get()->mType = toTag == cRZVariant::kTypeUnknown ? cRZVariant::kTypeSint32 : toTag;
                std::memset(to.Get()->mPayload, 0x5a, 8);
                if (to.Get()->mType & 0x8000) to.Get()->mArray = nullptr;
                const bool r = convert[side](from.Get(), to.Get());
                line += "convert " + DumpRaw(from) + " -> " + std::to_string(r) + DumpRaw(to);
                FreeRaw(to);
                break;
            }
            case 1:
            case 2: {
                CallThis<void *>(ctx.Original<void *>(0x9c7c75), to.bytes);
                std::memset(to.Get()->mPayload, 0x5a, 8);
                to.Get()->mRefCount = 7;
                to.Get()->mCount = 9;
                const bool r = (kind == 1 ? toFloat : toInt)[side](from.Get(), to.Get());
                // ToInt64 reads an array's payload as a sint32: its address (differs per side), shown as a marker.
                if ((fromTag & 0x8000) && from.Get()->mArray && to.Get()->mSint64 == int64_t(int32_t(shape::P(from.Get()->mArray))))
                    to.Get()->mUint64 = 0xadd0add0add0add0ull;
                line +=(kind == 1 ? "float " : "int ") + DumpRaw(from) + " -> " + std::to_string(r) + DumpRaw(to);
                break;
            }
            case 3: { // ToString
                const uint32_t flags = g() % 4;
                const char sep = kSeparators[g() % 4];
                text.s->mString = "previous";
                const bool r = toString[side](from.Get(), text.s, flags, sep);
                line += "tostring " + DumpRaw(from) + " " + std::to_string(flags) + " -> " + std::to_string(r) + " " +
                        TextOf(text.s);
                break;
            }
            case 4: { // FromString into every tag
                const uint16_t toTag = kTypes[g() % kTypeCount];
                CallThis<void *>(ctx.Original<void *>(0x9c7c75), to.bytes);
                to.Get()->mType = toTag;
                if (toTag == cRZVariant::kTypeUnknown) to.Get()->mUnknown = Fake(g());
                if (toTag & 0x8000) to.Get()->mArray = nullptr;
                const bool r = fromString[side](text.s, to.Get());
                line += "fromstring " + TextOf(text.s) + " -> " + std::to_string(r) + DumpRaw(to);
                FreeRaw(to);
                break;
            }
            default: { // IsTrueText, ParsePointer, FormatPointer
                const bool r = isTrue[side](text.s);
                uint32_t pointer[2] = {0x5a5a5a5a, 0x5a5a5a5a};
                parsePointer[side](text.s, pointer);
                std::snprintf(buf, sizeof(buf), " -> %d %08x %08x", r, pointer[0], pointer[1]);
                line += "text " + TextOf(text.s) + buf;
                formatPointer[side](reinterpret_cast<const void *>(static_cast<uintptr_t>(g())), text.s);
                line += " " + TextOf(text.s);
                break;
            }
            }
            FreeRaw(from);
            line += FakeState();
            gTranscript = nullptr;
        }
        ++ctx.cases;
        if (out[0] != out[1]) {
            ctx.Fail("seed %u original: %.240s", seed, out[0].c_str());
            ctx.Fail("seed %u ours:     %.240s", seed, out[1].c_str());
        }
    }
}

// Slot 132 with flags 1 frees the object (and its array) on both sides; Release at a count of 1 does the same.
DIFF_TEST(cRZVariant_DeletingDestructorFrees, 0x9c8603)
{
    Allocators allocators;
    for (int side = 0; side < 2; ++side) {
        for (int release = 0; release < 2; ++release) {
            void *memory = ::operator new(sizeof(cRZVariant));
            const uint32_t values[3] = {1, 2, 3};
            void *fn;
            if (side == 0) {
                CallThis<void *>(ctx.Original<void *>(0x9c7c75), memory);
                CallThis<void>(GameAddress<void>(0x9cab59), memory, static_cast<const void *>(values), 3u);
                CallThis<uint32_t>(GameAddress<void>(0x9c8622), memory);
                fn = release ? GameAddress<void>(0x9cad76) : GameAddress<void>(0x9c8603);
            } else {
                A::Construct(memory, nullptr);
                static_cast<cRZVariant *>(memory)->SetUint32Array(values, 3);
                static_cast<cRZVariant *>(memory)->AddRef();
                fn = (*static_cast<void *const *const *>(memory))[release ? 2 : 132];
            }
            const void *array = V(memory)->mArray;
            difftest::FreeRecorder rec;
            const uint32_t r = release ? CallThis<uint32_t>(fn, memory) : CallThis<uint32_t>(fn, memory, 1u);
            rec.Stop();
            if (!rec.Freed(memory) || !rec.Freed(array))
                ctx.Fail("side %d release %d: object freed %d, array freed %d", side, release, rec.Freed(memory), rec.Freed(array));
            if (release ? r != 0 : r != shape::P(memory))
                ctx.Fail("side %d release %d: returned %08x", side, release, r);
            ++ctx.cases;
        }
    }
}

// IsEqual / IsNotEqual (slots 64, 65) on two arrays of the same kind and count that are equal or differ in exactly one
// element (each index in turn, in the element's top byte): every element of every kind must be compared. The random
// scripts rarely line two such arrays up.
DIFF_TEST(cRZVariant_IsEqualArrays, 0x9c804e)
{
    Allocators allocators;
    OurVTable();
    std::mt19937 g(0x9c804e);
    std::vector<Step> script;
    auto add = [&](unsigned object, Op op, unsigned what) -> Step & {
        Step s{};
        s.object = object;
        s.other = 1 - object;
        s.op = op;
        s.what = what;
        script.push_back(s);
        return script.back();
    };
    static const uint16_t kKinds[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0};
    add(0, kOpCtor, 0);
    add(1, kOpCtor, 0);
    add(2, kOpCtor, 0);
    for (unsigned setter = 97; setter <= 127; setter += 2) {
        const uint16_t kind = kKinds[(setter - 97) / 2];
        const uint32_t size = kind == 0 ? 1 : ElementBytes(0x8000 | kind);
        for (uint32_t count = 1; count <= 4; ++count) {
            for (int differ = -1; differ < static_cast<int>(count); ++differ) {
                Step &a = add(0, kOpSlot, setter);
                a.c = count;
                FillArray(g, kind, count, a.data);
                Step &b = add(1, kOpSlot, setter);
                b.c = count;
                std::memcpy(b.data, script[script.size() - 2].data, sizeof(b.data));
                if (differ >= 0) b.data[size * differ + size - 1] ^= 1;
                add(0, kOpSlot, 64);
                add(1, kOpSlot, 65);
            }
        }
    }
    const std::string original = RunScript(ctx, Route::Original, 0, script);
    for (Route ours : {Route::VTable, Route::Adapter}) {
        gTrapHits = 0;
        const std::string replacement = RunScript(ctx, ours, 0, script);
        const char *name = ours == Route::VTable ? "vtable" : "adapter";
        if (gTrapHits != 0) ctx.Fail("%s: %d trap hits (an adapter dispatched virtually)", name, gTrapHits);
        CompareTranscripts(ctx, 0, name, original, replacement);
    }
    ctx.cases += script.size();
}

// ---- how the scripts reach each function ----

namespace {

// Every function the hook table replaces (and the two it leaves), with how the tests above reach it: the game vtable
// slot holding it or a 5-byte jmp stub to it (Pool() calls every slot on all three routes of cRZVariant_VTable and
// _Adapters), a constructor or static the tests call by RVA (slot -1, caller 0), or a caller in this list whose body
// calls or jumps to it. Checked against the image, so the table cannot drift from the code the scripts run.
struct Reach {
    uint32_t rva;
    int slot;        // -1: no slot reaches it
    uint32_t caller; // 0: none (a slot, or called by RVA)
    uint32_t callerBytes;
};

const Reach kReach[] = {
    {0x9c79da, -1, 0, 0}, // ConstructCopy
    {0x9c7a04, -1, 0, 0}, // ConstructText
    {0x9c7a40, -1, 0, 0}, // ConstructVariant
    {0x9c7a6a, -1, 0, 0}, // ConstructSint8
    {0x9c7a94, -1, 0, 0}, // ConstructUint8
    {0x9c7abe, -1, 0, 0}, // ConstructSint16
    {0x9c7aea, -1, 0, 0}, // ConstructUint16
    {0x9c7b16, -1, 0, 0}, // ConstructSint32
    {0x9c7b3d, -1, 0, 0}, // ConstructUint32 (UtilObjectB's hook)
    {0x9c7b64, -1, 0, 0}, // ConstructFloat32
    {0x9c7b8f, -1, 0, 0}, // ConstructFloat64
    {0x9c7bb6, -1, 0, 0}, // ConstructUnknown
    {0x9c7be0, -1, 0, 0}, // ConstructBytes
    {0x9c7c17, -1, 0, 0}, // ConstructChars
    {0x9c7c4e, -1, 0, 0}, // ConstructVoidPtr
    {0x9c7c75, -1, 0, 0}, // Construct
    {0x9c7c8e, -1, 0, 0}, // ConstructSint64
    {0x9c7cb8, -1, 0, 0}, // ConstructUint64
    {0x9c7ce2, -1, 0, 0}, // ConstructBool
    {0x9c7d0c, -1, 0, 0}, // Destruct
    {0x9c7d18, -1, 0, 0}, // Assign
    {0x9c803c, 66, 0, 0}, // AssignFrom
    {0x9c804e, 64, 0, 0}, // IsEqual
    {0x9c85ee, 65, 0, 0}, // IsNotEqual
    {0x9c8603, 132, 0, 0}, // vf132
    {0x9c8622, 1, 0, 0}, // AddRef
    {0x9c862b, 87, 0, 0}, // AsFloat64 (4 bytes, not hooked)
    {0x9c862f, 81, 0, 0}, // AsUint64
    {0x9c8636, -1, 0, 0}, // FromString
    {0x9c9152, -1, 0, 0}, // FormatPointer
    {0x9c9169, -1, 0, 0}, // IsTrueText
    {0x9c91b3, -1, 0, 0}, // ParsePointer
    {0x9c91d0, -1, 0, 0}, // ToFloat64
    {0x9c92ac, -1, 0, 0}, // ToString
    {0x9c979c, -1, 0, 0}, // ToInt64
    {0x9c986c, -1, 0, 0}, // Convert
    {0x9c9a1e, 7, 0, 0}, // CopyFrom
    {0x9c9e86, 8, 0, 0}, // CreateCopy
    {0x9c9eb6, 55, 0, 0}, // CreateValString
    {0x9c9f0d, -1, 0x9c9a1e, 997}, // Clear
    {0x9c9f48, 6, 0, 0}, // Erase
    {0x9c9f50, 3, 0, 0}, // GetType
    {0x9c9f55, 11, 0, 0}, // GetValBool
    {0x9c9fa8, 10, 0, 0}, // ValBool
    {0x9c9fbe, 43, 0, 0}, // ValChar
    {0x9c9fd7, 44, 0, 0}, // GetValChar
    {0x9ca033, 37, 0, 0}, // ValFloat32
    {0x9ca04c, 38, 0, 0}, // GetValFloat32
    {0x9ca0a8, 40, 0, 0}, // ValFloat64
    {0x9ca0c6, 41, 0, 0}, // GetValFloat64
    {0x9ca12b, 49, 0, 0}, // ValRZChar
    {0x9ca144, 50, 0, 0}, // GetValRZChar
    {0x9ca1a0, 46, 0, 0}, // ValRZUnicodeChar
    {0x9ca1ba, 47, 0, 0}, // GetValRZUnicodeChar
    {0x9ca21f, 22, 0, 0}, // ValSint16
    {0x9ca236, 23, 0, 0}, // GetValSint16
    {0x9ca29b, 28, 0, 0}, // ValSint32
    {0x9ca2b1, 29, 0, 0}, // GetValSint32
    {0x9ca30d, 34, 0, 0}, // ValSint64
    {0x9ca32e, 35, 0, 0}, // GetValSint64
    {0x9ca39d, 16, 0, 0}, // ValSint8
    {0x9ca3b3, 17, 0, 0}, // GetValSint8
    {0x9ca414, 19, 0, 0}, // ValUint16
    {0x9ca42b, 20, 0, 0}, // GetValUint16
    {0x9ca481, 25, 0, 0}, // ValUint32
    {0x9ca497, 26, 0, 0}, // GetValUint32
    {0x9ca4e9, 31, 0, 0}, // ValUint64
    {0x9ca50a, 32, 0, 0}, // GetValUint64
    {0x9ca569, 13, 0, 0}, // ValUint8
    {0x9ca57f, 14, 0, 0}, // GetValUint8
    {0x9ca5d1, 61, 0, 0}, // ValUnknown
    {0x9ca5e4, 59, 0, 0}, // GetValVoid
    {0x9ca61b, 52, 0, 0}, // ValVoidPtr
    {0x9ca634, 53, 0, 0}, // GetValVoidPtr
    {0x9ca686, 5, 0, 0}, // IsUndefined
    {0x9ca68f, 0, 0, 0}, // QueryInterface
    {0x9ca6b3, 97, 0, 0}, // SetBoolArray
    {0x9ca715, 119, 0, 0}, // SetCharArray
    {0x9ca777, 115, 0, 0}, // SetFloat32Array
    {0x9ca7ea, 117, 0, 0}, // SetFloat64Array
    {0x9ca862, 123, 0, 0}, // SetRZCharArray
    {0x9ca8c4, 121, 0, 0}, // SetRZUnicodeCharArray
    {0x9ca933, 105, 0, 0}, // SetSint16Array
    {0x9ca9a2, 109, 0, 0}, // SetSint32Array
    {0x9caa15, 113, 0, 0}, // SetSint64Array
    {0x9caa88, 101, 0, 0}, // SetSint8Array
    {0x9caaea, 103, 0, 0}, // SetUint16Array
    {0x9cab59, 107, 0, 0}, // SetUint32Array
    {0x9cabcc, 111, 0, 0}, // SetUint64Array
    {0x9cac3f, 99, 0, 0}, // SetUint8Array
    {0x9caca1, 127, 0, 0}, // SetVoidArray
    {0x9cad03, 125, 0, 0}, // SetVoidPtrArray
    {0x9cad76, 2, 0, 0}, // Release
    {0x9cad97, -1, 0x9cb3af, 76}, // ParseArray
    {0x9cb3af, 58, 0, 0}, // SetFromString
    {0x9cb405, 39, 0, 0}, // SetValFloat32
    {0x9cb419, 42, 0, 0}, // SetValFloat64
    {0x9cb47b, 60, 0, 0}, // SetValVoid
    {0x9cb4e1, -1, 0x9cb3af, 76}, // ParseScalar
    {0x9cb791, 56, 0, 0}, // TextOf
    {0x9cb848, 62, 0, 0}, // QueryUnknown
    {0x9cb85f, 12, 0, 0}, // StoreBool
    {0x9cb889, 45, 0, 0}, // StoreChar
    {0x9cb8b3, -1, 0x9c9a1e, 997}, // StoreFloat32
    {0x9cb8e1, -1, 0x9c9a1e, 997}, // StoreFloat64
    {0x9cb90f, 51, 0, 0}, // StoreRZChar
    {0x9cb939, 48, 0, 0}, // StoreRZUnicodeChar
    {0x9cb965, 24, 0, 0}, // StoreSint16
    {0x9cb991, 30, 0, 0}, // StoreSint32
    {0x9cb9bb, -1, 0x9c9a1e, 997}, // StoreSint64
    {0x9cb9ec, 18, 0, 0}, // StoreSint8
    {0x9cba16, 57, 0, 0}, // StoreText
    {0x9cba3f, 21, 0, 0}, // StoreUint16
    {0x9cba6b, 27, 0, 0}, // StoreUint32
    {0x9cba95, -1, 0x9c9a1e, 997}, // StoreUint64
    {0x9cbac6, 15, 0, 0}, // StoreUint8
    {0x9cbaf0, 63, 0, 0}, // StoreUnknown
    {0x9cbb36, 54, 0, 0}, // StoreVoidPtr
};

// The slot of the game's vtable holding `rva` or a jmp stub to it; -1 when none does.
int SlotReaching(uint32_t rva)
{
    void *const *game = shape::GameVTable(kVTable);
    const uint8_t *target = GameAddress<uint8_t>(rva);
    for (unsigned s = 0; s < kSlots; ++s) {
        const uint8_t *entry = static_cast<const uint8_t *>(game[s]);
        if (entry == target) return static_cast<int>(s);
        int32_t rel;
        std::memcpy(&rel, entry + 1, 4);
        if (entry[0] == 0xe9 && entry + 5 + rel == target) return static_cast<int>(s);
    }
    return -1;
}

// Whether the body at `caller` (its first `span` bytes) calls or jumps (rel32) to `rva`.
bool Calls(uint32_t caller, uint32_t rva, uint32_t span)
{
    const uint8_t *body = GameAddress<uint8_t>(caller);
    const uint8_t *target = GameAddress<uint8_t>(rva);
    for (uint32_t i = 0; i + 5 <= span; ++i) {
        if (body[i] != 0xe8 && body[i] != 0xe9) continue;
        int32_t rel;
        std::memcpy(&rel, body + i + 1, 4);
        if (body + i + 5 + rel == target) return true;
    }
    return false;
}

} // namespace

DIFF_TEST(cRZVariant_Reach, 0x9c9f0d)
{
    for (const Reach &r : kReach) {
        ++ctx.cases;
        const int slot = SlotReaching(r.rva);
        if (slot != r.slot) ctx.Fail("%06x: expected slot %d, the image's vtable reaches it at %d", r.rva, r.slot, slot);
        if (r.caller && !Calls(r.caller, r.rva, r.callerBytes)) ctx.Fail("%06x: %06x does not call it", r.rva, r.caller);
    }
}
