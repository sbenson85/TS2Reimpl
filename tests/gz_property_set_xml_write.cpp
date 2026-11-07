// Differential tests for the XML delegate's Write 0x556a30 (src/gz/cGZPropertySetSerializerXml.cpp, lane DY).
//
// The set is a fake (svcfx F(1): slot 11 GetCount, slots 6 / 7 GetFirst / GetNext over a list of keys and real Any
// values with the game's vtables: every value type, booleans with junk upper bytes, floats with NaN / infinities,
// strings with markup characters, embedded NULs and long texts, string sets of 0-4 members, values of unknown types).
// The framework (svcfx F(0), slot 3 CreateInstance) hands out either
//   - a logging fake XML writer (document face: QI for the writer face, Text, Length; writer face: StartDocument,
//     StartElement, EndElement, Attribute, Text), whose calls are logged with the texts they get, or
//   - the game's own cGZXmlBufferWriter (built with its original constructor 0x9c515d), so the XML text itself
//     (escaping included) reaches the stream and is compared,
// or fails. The stream fake logs SetVoid's bytes (failing with 0 or 0x100) and the record face's SetSize. Compared:
// the result byte, every call in order with its texts, and the fakes' reference counts.
//
// Captured inputs: when build/propset-xml-capture.txt exists (not committed: the 0xebfee33f records of
// N001_Neighborhood.package, hex-encoded one property per line by a local script), each record is also written through
// both writers.
//
// Left out: GetFirst failing while GetCount is not 0 (the original calls GetType on the null value, 0x556c06 /
// 0x556c2d), and a document without the writer face (StartDocument is called on the null writer, 0x556aef).
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"
#include "rz/cRZString.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {1, "d-d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using Xml = cGZPropertySetSerializerXmlString;

difftest::Context *gCtx = nullptr;

template <typename R, typename... A> R Slot(void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(object, slot)(object, args...);
}

void LogBytes(const char *data, uint32_t n)
{
    Ev(0x7e000000u | n);
    for (uint32_t i = 0; i < n && i < 0x4000; ++i) Ev(uint8_t(data[i]));
}
void LogText(const char *text)
{
    if (!text) {
        Ev(0x7effffffu);
        return;
    }
    LogBytes(text, uint32_t(std::strlen(text)));
}

// ---- own fakes (texts logged by content, not by address) ----------------------------------------------------------

struct Fake {
    void **vt;
    int32_t refs;
    uint32_t id;
};

uint32_t gWriterReturn = 1;   // what the writer's calls answer (never read)
uint32_t gQiReturn = 1;       // stream QI for the record face
bool gStreamHasRecord = true;
uint32_t gSetVoidReturn = 1;
uint32_t gPosition = 0;
uint32_t gDocumentLength = 0;
char gDocumentText[64] = "document text";

Fake gDocument, gWriter, gStream, gRecord;

uint32_t __fastcall FakeAddRef(Fake *self, void *)
{
    Ev(0x41);
    Ev(self->id);
    return uint32_t(++self->refs);
}
uint32_t __fastcall FakeRelease(Fake *self, void *)
{
    Ev(0x42);
    Ev(self->id);
    return uint32_t(--self->refs);
}

// Document face (cIGZXmlBufferWriter).
uint32_t __fastcall DocumentQI(Fake *, void *, uint32_t iid, void **out)
{
    Ev(0x43);
    Ev(iid);
    *out = &gWriter;
    ++gWriter.refs;
    return 1;
}
const char *__fastcall DocumentText(Fake *, void *)
{
    Ev(0x44);
    return gDocumentText;
}
uint32_t __fastcall DocumentLength(Fake *, void *)
{
    Ev(0x45);
    return gDocumentLength;
}

// Writer face (cIGZXmlWriter).
uint32_t __fastcall WriterStartDocument(Fake *, void *)
{
    Ev(0x46);
    return gWriterReturn;
}
uint32_t __fastcall WriterStartElement(Fake *, void *, const char *name)
{
    Ev(0x47);
    LogText(name);
    return gWriterReturn;
}
uint32_t __fastcall WriterEndElement(Fake *, void *, const char *name)
{
    Ev(0x48);
    LogText(name);
    return gWriterReturn;
}
uint32_t __fastcall WriterAttribute(Fake *, void *, const char *name, const char *value)
{
    Ev(0x49);
    LogText(name);
    LogText(value);
    return gWriterReturn;
}
uint32_t __fastcall WriterText(Fake *, void *, const char *text)
{
    Ev(0x4a);
    LogText(text);
    return gWriterReturn;
}

// Stream (cIGZOStream) and its record face.
uint32_t __fastcall StreamQI(Fake *, void *, uint32_t iid, void **out)
{
    Ev(0x50);
    Ev(iid);
    if (gStreamHasRecord) {
        *out = &gRecord;
        ++gRecord.refs;
    }
    return gQiReturn;
}
uint32_t __fastcall StreamSetVoid(Fake *, void *, const char *data, uint32_t length)
{
    Ev(0x51);
    Ev(data == gDocumentText ? 1u : 0u);
    LogBytes(data, length);
    return gSetVoidReturn;
}
uint32_t __fastcall RecordSetSize(Fake *, void *, uint32_t size)
{
    Ev(0x52);
    Ev(size);
    return 1;
}
uint32_t __fastcall RecordGetPosition(Fake *, void *)
{
    Ev(0x53);
    return gPosition;
}

void *gDocumentVT[8], *gWriterVT[12], *gStreamVT[20], *gRecordVT[16];

void ResetFakes()
{
    auto fn = [](auto f) { return reinterpret_cast<void *>(f); };
    gDocumentVT[0] = fn(&DocumentQI);
    gDocumentVT[1] = fn(&FakeAddRef);
    gDocumentVT[2] = fn(&FakeRelease);
    gDocumentVT[4] = fn(&DocumentText);
    gDocumentVT[5] = fn(&DocumentLength);
    gWriterVT[1] = fn(&FakeAddRef);
    gWriterVT[2] = fn(&FakeRelease);
    gWriterVT[3] = fn(&WriterStartDocument);
    gWriterVT[4] = fn(&WriterStartElement);
    gWriterVT[5] = fn(&WriterEndElement);
    gWriterVT[8] = fn(&WriterAttribute);
    gWriterVT[9] = fn(&WriterText);
    gStreamVT[0] = fn(&StreamQI);
    gStreamVT[1] = fn(&FakeAddRef);
    gStreamVT[2] = fn(&FakeRelease);
    gStreamVT[17] = fn(&StreamSetVoid);
    gRecordVT[1] = fn(&FakeAddRef);
    gRecordVT[2] = fn(&FakeRelease);
    gRecordVT[11] = fn(&RecordSetSize);
    gRecordVT[12] = fn(&RecordGetPosition);
    gDocument = {gDocumentVT, 1000, 1};
    gWriter = {gWriterVT, 1000, 2};
    gStream = {gStreamVT, 1000, 3};
    gRecord = {gRecordVT, 1000, 4};
}

void CaptureRefs(Blob &b)
{
    for (const Fake *f : {&gDocument, &gWriter, &gStream, &gRecord}) b.push_back(uint32_t(f->refs));
}

// Framework slot 3: value[0] 0 nothing, 1 the logging fake, 2 the game's cGZXmlBufferWriter; returns value[1].
uint32_t FrameworkCreate(svcfx::Obj *self, uint32_t *args)
{
    void **out = reinterpret_cast<void **>(uintptr_t(args[2]));
    if (self->value[0] == 1) {
        *out = &gDocument;
        ++gDocument.refs;
    } else if (self->value[0] == 2) {
        void *memory = operator new(0x3c);
        gCtx->Original<void *(__thiscall *)(void *)>(0x9c515d)(memory);
        void *face = static_cast<uint8_t *>(memory) + 0x18; // cIGZXmlBufferWriter
        Slot<uint32_t>(face, 1);
        *out = face;
    }
    return self->value[1];
}

// ---- the set ------------------------------------------------------------------------------------------------------

struct Property {
    std::string key;
    void *value;
};
std::vector<Property> gProperties;

// The uint32 instantiation (lane EU): keys are numbers (the key text's number when it starts with a digit, else a hash of it).
bool gUint32Keys = false;
void StoreKey(uint32_t out, const std::string &key)
{
    if (!gUint32Keys) {
        reinterpret_cast<GZServiceString *>(uintptr_t(out))->assign(key.data(), key.size());
        return;
    }
    uint32_t number = 2166136261u;
    if (!key.empty() && key[0] >= '0' && key[0] <= '9')
        number = uint32_t(std::strtoul(key.c_str(), nullptr, 0));
    else
        for (char c : key) number = (number ^ uint8_t(c)) * 16777619u;
    *reinterpret_cast<uint32_t *>(uintptr_t(out)) = number;
}

uint32_t SetGetFirst(svcfx::Obj *self, uint32_t *args)
{
    self->value[0] = 0;
    if (gProperties.empty()) return self->value[1];
    StoreKey(args[0], gProperties[0].key);
    *reinterpret_cast<void **>(uintptr_t(args[1])) = gProperties[0].value;
    self->value[0] = 1;
    return 1;
}
uint32_t SetGetNext(svcfx::Obj *self, uint32_t *args)
{
    const uint32_t i = self->value[0];
    if (i >= gProperties.size()) return self->value[2]; // 0, or 0x100 (al 0)
    StoreKey(args[0], gProperties[i].key);
    *reinterpret_cast<void **>(uintptr_t(args[1])) = gProperties[i].value;
    self->value[0] = i + 1;
    return 1;
}

// Values (game vtables): built for a case, freed after both sides ran.
struct Values {
    std::vector<uint32_t *> words;  // two-word Anys
    std::vector<cRZString *> strings;
    std::vector<AnyStringSet *> sets;

    ~Values()
    {
        for (uint32_t *w : words) delete[] w;
        for (cRZString *s : strings) {
            s->Destruct();
            operator delete(s);
        }
        for (AnyStringSet *s : sets) {
            reinterpret_cast<GZStringSetOps *>(&s->mSet)->Destruct();
            operator delete(s);
        }
    }
    void *Word(uint32_t vtable, uint32_t value)
    {
        auto *w = new uint32_t[2]{svcfx::P(GameAddress<const void>(vtable)), value};
        words.push_back(w);
        return w;
    }
    void *Raw(const void *vtable, uint32_t value)
    {
        auto *w = new uint32_t[2]{svcfx::P(vtable), value};
        words.push_back(w);
        return w;
    }
    void *String(const std::string &text)
    {
        auto *s = static_cast<cRZString *>(operator new(sizeof(cRZString)));
        s->Construct();
        s->mString.assign(text.data(), text.size());
        s->mRefCount = 1;
        strings.push_back(s);
        return Word(gzany::kStringVTable, svcfx::P(s));
    }
    void *Set(const std::vector<std::string> &members)
    {
        auto *s = static_cast<AnyStringSet *>(operator new(sizeof(AnyStringSet)));
        s->mVTable = GameAddress<const void>(gzany::kStringSetVTable);
        reinterpret_cast<GZStringSetOps *>(&s->mSet)->Construct();
        for (const std::string &m : members) reinterpret_cast<GZStringSetOps *>(&s->mSet)->Insert(GZString(m.data(), m.size()));
        sets.push_back(s);
        return s;
    }
};

// A cIGZAny of a type the writer does not know: slot 4 answers the type in its payload.
uint32_t __fastcall OddGetType(uint32_t *self, void *) { return self[1]; }
void *gOddVT[6] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&OddGetType), nullptr};

const char *const kKeys[] = {"0", "1", "2", "key", "", "a key that is longer than fifteen chars", "a<b>&\"'c",
                             "\xc3\xa9t\xc3\xa9"};
const char *const kTexts[] = {"", "text", "a text that is longer than fifteen characters", "<tag attr=\"v\">&amp;</tag>",
                              " spaces  ", "line\nbreak\ttab", "\xe2\x82\xac uro", "]]>", "'quoted'"};

std::string Pick(std::mt19937 &rng, const char *const *array, size_t n) { return array[rng() % n]; }

void *RandomValue(std::mt19937 &rng, Values &values)
{
    switch (rng() % 9) {
    case 0: {
        const uint32_t v[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 12345};
        return values.Word(gzany::kUint32VTable, rng() % 2 ? v[rng() % 6] : uint32_t(rng()));
    }
    case 1: {
        const uint32_t v[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, uint32_t(-42)};
        return values.Word(gzany::kSint32VTable, rng() % 2 ? v[rng() % 6] : uint32_t(rng()));
    }
    case 2: {
        const float v[] = {0.0f, -0.0f, 1.5f, -3.25f, 1e-7f, 3.0e38f, 123456.789f, std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 0.1f};
        float f = rng() % 2 ? v[rng() % 11] : float(int32_t(rng())) / float(1 + rng() % 100000);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return values.Word(gzany::kFloat32VTable, bits);
    }
    case 3: {
        const uint32_t v[] = {0, 1, 0x80, 0xff, 0xabcdef00, 0x12345601};
        return values.Word(gzany::kBooleanVTable, v[rng() % 6]);
    }
    case 4:
    case 5: {
        std::string text = Pick(rng, kTexts, std::size(kTexts));
        if (rng() % 8 == 0) text.insert(text.size() / 2, 1, '\0');
        return values.String(text);
    }
    case 6: {
        std::vector<std::string> members;
        for (int n = rng() % 5; n; --n) members.push_back(Pick(rng, kTexts, std::size(kTexts)));
        return values.Set(members);
    }
    case 7:
        if (rng() % 2) return values.Word(gzany::kAnyAnyVTable, 0);
        [[fallthrough]];
    default: {
        // Mostly known types are wanted; an odd type now and then.
        if (rng() % 3) return values.Word(gzany::kUint32VTable, rng());
        const uint32_t types[] = {0, 0x12345678, gzany::kTypeAnyAny};
        return values.Raw(gOddVT, types[rng() % 3]);
    }
    }
}

uint32_t gOutcomes[3][2]; // per writer kind: cases returning 0 / 1 (printed, to show what the seeds reach)

struct Setup {
    uint32_t create;       // framework: 0 none, 1 fake, 2 game writer
    uint32_t createReturn; // its result
    uint32_t count;
    uint32_t nextEnd;      // GetNext's answer at the end
};

Blob RunSide(bool original, const Setup &setup)
{
    svcfx::ResetAll();
    ResetFakes();
    svcfx::Globals globals;
    svcfx::gFrameworkIndex = 0;
    F(0).Set(3, "ddp", 0, &FrameworkCreate);
    F(0).value[0] = setup.create;
    F(0).value[1] = setup.createReturn;
    F(1).Set(11, 0, setup.count);
    F(1).Set(6, "pp", 0, &SetGetFirst);
    F(1).Set(7, "pp", 0, &SetGetNext);
    F(1).value[2] = setup.nextEnd;

    // Standalone shape (lane EF): the original side's object from the game's constructor 0x551a60, ours from our constructor,
    // called through its own vtable slot 4.
    // The uint32 instantiation: 0x241ea6 / 0x242901 and our cGZPropertySetSerializerXmlUint32 (the same layout).
    alignas(8) uint8_t memory[sizeof(Xml)] = {};
    Xml *x = original ? static_cast<Xml *>(reinterpret_cast<void *(__thiscall *)(void *)>(GameAddress<void>(gUint32Keys ? 0x241ea6 : 0x551a60))(memory))
           : gUint32Keys ? reinterpret_cast<Xml *>(::new (memory) cGZPropertySetSerializerXmlUint32)
                         : ::new (memory) Xml;
    using SlotWrite = uint8_t(__thiscall *)(void *, void *, void *, uint32_t);
    const uint32_t r = original ? gCtx->Original<SlotWrite>(gUint32Keys ? 0x242901 : 0x556a30)(x, &gStream, &F(1), 1)
                                : (*reinterpret_cast<SlotWrite *const *>(x))[4](x, &gStream, &F(1), 1);
    if (!original) ++gOutcomes[setup.create][r & 1];
    Blob b{r & 0xff};
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    CaptureRefs(b);
    svcfx::CaptureFakes(b, kValueKinds);
    x->mKey.Destruct();
    x->mText.Destruct();
    return b;
}

bool Compare(difftest::Context &ctx, const Setup &setup, const char *what, uint32_t seed)
{
    const Blob a = RunSide(true, setup);
    const Blob b = RunSide(false, setup);
    return svcfx::Same(ctx, a, b, what, seed);
}

void SetStream(std::mt19937 &rng)
{
    gStreamHasRecord = rng() % 3 != 0;
    gQiReturn = gStreamHasRecord ? (rng() % 6 ? 1u : 0x100u) : (rng() % 2 ? 0u : 0x200u);
    gSetVoidReturn = rng() % 5 ? 1u : (rng() % 2 ? 0u : 0x100u);
    gPosition = rng() % 2 ? 0 : uint32_t(rng() % 5000);
    gDocumentLength = rng() % 4 ? uint32_t(std::strlen(gDocumentText)) : uint32_t(rng() % 20);
    gWriterReturn = rng() % 2 ? 1u : uint32_t(rng());
}

// The capture file: "R" starts a record; "P <type hex> <key hex> <text hex>" one AnyString property ("-" = empty).
std::vector<std::vector<std::pair<std::string, std::string>>> LoadCapture()
{
    std::vector<std::vector<std::pair<std::string, std::string>>> records;
    std::ifstream in("build/propset-xml-capture.txt");
    auto unhex = [](const std::string &h) {
        std::string s;
        if (h == "-") return s;
        for (size_t i = 0; i + 1 < h.size(); i += 2) s.push_back(char(std::stoi(h.substr(i, 2), nullptr, 16)));
        return s;
    };
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string tag, type, key, text;
        words >> tag;
        if (tag == "R") records.emplace_back();
        else if (tag == "P" && !records.empty() && (words >> type >> key >> text) && type == "0b8bea18")
            records.back().push_back({unhex(key), unhex(text)});
    }
    return records;
}

} // namespace

// Seeded sets through the logging fake writer and the game's writer; framework, set and stream failures.
void CompareSeeds(difftest::Context &ctx, uint32_t seeds)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        std::mt19937 rng(seed);
        Values values;
        gProperties.clear();
        const uint32_t n = seed % 7 == 0 ? 0u : (seed % 5 == 0 ? 1u : uint32_t(rng() % 12));
        for (uint32_t i = 0; i < n; ++i) {
            std::string key = Pick(rng, kKeys, std::size(kKeys));
            if (rng() % 12 == 0) key.push_back('\0'), key += "tail";
            gProperties.push_back({key, RandomValue(rng, values)});
        }
        Setup setup;
        setup.create = rng() % 8 == 0 ? 0u : (rng() % 3 == 0 ? 2u : 1u);
        setup.createReturn = setup.create == 0 ? (rng() % 2 ? 0u : 0x100u) : (rng() % 10 ? 1u : 0u);
        setup.count = n ? (rng() % 6 ? n : uint32_t(1 + rng() % 30)) : (rng() % 3 ? 0u : uint32_t(1 + rng() % 3));
        if (n == 0 && setup.count) setup.count = 0; // GetFirst failing with a count: left out (header)
        setup.nextEnd = rng() % 4 ? 0u : 0x100u;
        SetStream(rng);
        if (!Compare(ctx, setup, "Write", seed)) break;
    }
}

DIFF_TEST(PropertySetXml_Write, 0x556a30)
{
    gCtx = &ctx;
    gUint32Keys = false;
    svcfx::AllocatorScope allocator;
    CompareSeeds(ctx, 1500);

    // Captured records, through both writers.
    const auto records = LoadCapture();
    std::printf("PropertySetXml_Write: results 0/1 without writer %u/%u, fake %u/%u, game writer %u/%u; %u captured records\n",
                gOutcomes[0][0], gOutcomes[0][1], gOutcomes[1][0], gOutcomes[1][1], gOutcomes[2][0], gOutcomes[2][1],
                unsigned(records.size()));
    for (size_t r = 0; r < records.size(); ++r) {
        Values values;
        gProperties.clear();
        for (const auto &[key, text] : records[r]) gProperties.push_back({key, values.String(text)});
        for (uint32_t create : {1u, 2u}) {
            std::mt19937 rng{static_cast<uint32_t>(r)};
            SetStream(rng);
            gSetVoidReturn = 1;
            const Setup setup{create, 1, uint32_t(gProperties.size()), 0};
            if (!Compare(ctx, setup, "Write captured", uint32_t(r * 2 + create))) break;
        }
    }
}

// The uint32 instantiation's Write 0x242901 (lane EU): the same seeded sets with number keys (the key attribute's text is
// "0x%08x" of the key, KeyToText 0x242482; the root name by AssignRootName 0x24204a), through our class's slot 4.
DIFF_TEST(PropertySetXml32_Write, 0x242901)
{
    gCtx = &ctx;
    gUint32Keys = true;
    svcfx::AllocatorScope allocator;
    CompareSeeds(ctx, 1000);
    gUint32Keys = false;
}
