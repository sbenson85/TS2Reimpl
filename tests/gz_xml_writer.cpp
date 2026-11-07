// Differential tests for src/gz/cGZXmlWriter.* (standalone shape): cGZXmlWriter's slots on both writers, the buffer and
// file writers' own slots, constructors, creators, Release and the buffer writer's destructor.
//
// The original side's object comes from the image's creator (0x9c5261 / 0x9c528e, through the constructors 0x9c515d /
// 0x9c518f) with the game's operator new (0x824632) standing in for ours, ours from our creator; every call goes through
// the object's own vtable slot. The vtable words at +0 and +0x18 are the only bytes normalised (the side's own class's
// table, the game's on the original side and ours on ours); every other field is compared, the buffer writer's string by
// its text (its words hold heap addresses). The file writer writes through a fake stream (slot 16 records each write and
// answers from a script, some failing with junk above al), so every early return on a failed write is reached.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZXmlWriter.h"
#include "rz/cRZString.h"

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCreateBuffer = 0x9c5261;
constexpr uint32_t kCreateFile = 0x9c528e;
constexpr uint32_t kConstructBuffer = 0x9c515d;
constexpr uint32_t kConstructFile = 0x9c518f;
constexpr uint32_t kWriterVTable = 0xe616e8;
constexpr uint32_t kBufferVTable = 0xe61614;
constexpr uint32_t kBufferFaceVTable = 0xe61650;
constexpr uint32_t kFileVTable = 0xe615c8;
constexpr uint32_t kFileFaceVTable = 0xe61600;
constexpr uint32_t kFaceOffset = 0x18;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kGameSizedDelete = 0x824624;

// ---- the heap: the game's operator new to ours; the sized deletes (the image's and ours) note watched blocks ----

void *gWatched = nullptr;
size_t gFreedSize = 0; // the size the watched block was freed with (0: not freed)
DWORD gWatchThread = 0;

void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}

void __cdecl NoteSizedDelete(void *p, size_t n)
{
    if (p != nullptr && p == gWatched && GetCurrentThreadId() == gWatchThread) {
        gFreedSize = n;
        gWatched = nullptr;
    }
    std::free(p);
}

struct Heap {
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{kGameSizedDelete, reinterpret_cast<const void *>(&NoteSizedDelete)};
    shape::CodePatch oursDelete{reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&::operator delete)),
                                reinterpret_cast<const void *>(&NoteSizedDelete)};
    Heap() { gWatchThread = GetCurrentThreadId(); }
    ~Heap() { gWatched = nullptr; }
    Heap(const Heap &) = delete;
    Heap &operator=(const Heap &) = delete;
};

// ---- our vtables, read from objects our constructors build ----

struct OurVTables {
    void *buffer, *bufferFace, *file, *fileFace, *writer;
};

const OurVTables &Ours()
{
    static OurVTables v{};
    static bool done = false;
    if (!done) {
        alignas(8) static uint8_t buffer[cGZXmlBufferWriter::kSize];
        auto *b = ::new (buffer) cGZXmlBufferWriter;
        v.buffer = shape::VTableAt(buffer, 0);
        v.bufferFace = shape::VTableAt(buffer, kFaceOffset);
        b->~cGZXmlBufferWriter();
        v.writer = shape::VTableAt(buffer, 0);
        alignas(8) static uint8_t file[cGZXmlFileWriter::kSize];
        ::new (file) cGZXmlFileWriter;
        v.file = shape::VTableAt(file, 0);
        v.fileFace = shape::VTableAt(file, kFaceOffset);
        done = true;
    }
    return v;
}

// A vtable word as a code: 1..5 for the side's own buffer, buffer face, file, file face and writer vtables, else the word.
uint32_t VtCode(uint32_t word, bool ours)
{
    const OurVTables &o = Ours();
    const uint32_t own[5] = {
        shape::P(ours ? o.buffer : GameAddress<void>(kBufferVTable)),
        shape::P(ours ? o.bufferFace : GameAddress<void>(kBufferFaceVTable)),
        shape::P(ours ? o.file : GameAddress<void>(kFileVTable)),
        shape::P(ours ? o.fileFace : GameAddress<void>(kFileFaceVTable)),
        shape::P(ours ? o.writer : GameAddress<void>(kWriterVTable)),
    };
    for (uint32_t i = 0; i < 5; ++i)
        if (word == own[i]) return 0xd0000001u + i;
    return word;
}

// ---- the fake stream of the file writer: slot 16 records (text, length) and answers from a script ----

struct FakeStream {
    void **vtable;
    uint32_t refs;
    std::vector<std::string> writes;
    std::vector<uint32_t> answers;
    size_t next = 0;
};

bool __fastcall FakeQueryInterface(FakeStream *, void *, uint32_t, void **)
{
    std::abort();
}
uint32_t __fastcall FakeAddRef(FakeStream *s, void *)
{
    return ++s->refs;
}
uint32_t __fastcall FakeRelease(FakeStream *s, void *)
{
    return --s->refs;
}
uint32_t __fastcall FakeWrite(FakeStream *s, void *, const char *text, uint32_t length)
{
    s->writes.emplace_back(text, length);
    const uint32_t r = s->next < s->answers.size() ? s->answers[s->next] : 0xabcd0001u;
    ++s->next;
    return r;
}
void __fastcall FakeTrap(FakeStream *, void *)
{
    std::abort();
}

void *gFakeVTable[cGZXmlFileWriter::kStreamWriteSlot + 1] = {};

void InitFakeVTable()
{
    for (void *&slot : gFakeVTable) slot = reinterpret_cast<void *>(&FakeTrap);
    gFakeVTable[0] = reinterpret_cast<void *>(&FakeQueryInterface);
    gFakeVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gFakeVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gFakeVTable[cGZXmlFileWriter::kStreamWriteSlot] = reinterpret_cast<void *>(&FakeWrite);
}

// ---- calls through an object's vtable ----

template <class Fn> Fn SlotAt(void *object, uint32_t offset, int slot)
{
    void **vt = shape::VTableAt(object, offset);
    return reinterpret_cast<Fn>(vt[slot]);
}

void *FaceOf(void *object, uint32_t offset)
{
    return static_cast<uint8_t *>(object) + offset;
}

using NoArgs = uint32_t(__thiscall *)(void *);
using OneText = uint32_t(__thiscall *)(void *, const char *);
using TwoTexts = uint32_t(__thiscall *)(void *, const char *, const char *);
using TextLength = uint32_t(__thiscall *)(void *, const char *, uint32_t);
using Query = uint32_t(__thiscall *)(void *, uint32_t, void **);
using SetStreamFn = void(__thiscall *)(void *, void *, uint32_t);
using Deleting = void *(__thiscall *)(void *, uint32_t);

// Text with the characters the writer treats specially, dash runs (and a dash last), some empty, some long.
std::string RandomText(std::mt19937 &rng)
{
    static const char kChars[] = "ab<>&'\"--- xyz-\t\n";
    std::string s;
    const uint32_t n = rng() % 5 == 0 ? 0 : rng() % 8 == 0 ? 40 + rng() % 60 : rng() % 12;
    for (uint32_t i = 0; i < n; ++i) s += rng() % 6 ? kChars[rng() % (sizeof(kChars) - 1)] : static_cast<char>(1 + rng() % 255);
    return s;
}

enum Op : int {
    kStartDocument = 3,
    kStartElement = 4,
    kEndElement = 5,
    kStartPI = 6,
    kEndPI = 7,
    kAttribute = 8,
    kText = 9,
    kCData = 10,
    kComment = 11,
    kWrite = 12,
    kQuery = 20,
    kFaceQuery,
    kAddRef,
    kFaceAddRef,
    kRelease,     // only while the count is above 1
    kFaceRelease, // likewise
    kFaceSlot3,   // buffer: GetString; file: SetStream
    kFaceSlot4,   // buffer: GetText
    kFaceSlot5,   // buffer: GetLength
};

struct Call {
    int op;
    std::string a, b;
    uint32_t length;
    uint32_t riid;
    uint32_t flags; // SetStream: the indent word; which fake
};

Call RandomCall(std::mt19937 &rng, bool file)
{
    static const int kOps[] = {kStartDocument, kStartElement, kStartElement, kStartElement, kEndElement, kEndElement,
                               kEndElement, kStartPI, kEndPI, kAttribute, kAttribute, kText, kText, kCData, kComment,
                               kComment, kWrite, kQuery, kFaceQuery, kAddRef, kFaceAddRef, kRelease, kFaceRelease,
                               kFaceSlot3, kFaceSlot4, kFaceSlot5};
    Call c;
    do {
        c.op = kOps[rng() % std::size(kOps)];
    } while (file && (c.op == kFaceSlot4 || c.op == kFaceSlot5));
    c.a = RandomText(rng);
    c.b = RandomText(rng);
    c.length = static_cast<uint32_t>(c.a.size()) + 1 - rng() % 2; // the NUL too, now and then
    static const uint32_t kIids[] = {1, cIGZXmlWriter::kIid, cIGZXmlBufferWriter::kIid, cIGZXmlFileWriter::kIid, 0x12345678};
    c.riid = kIids[rng() % std::size(kIids)];
    static const uint32_t kIndent[] = {0, 1, 0x80, 0xff};
    c.flags = (rng() & 0xffffff00u) | kIndent[rng() % std::size(kIndent)];
    if (rng() % 2) c.flags |= 0x100; // the second fake
    return c;
}

// What a side's call produced, besides the object.
struct Outcome {
    uint32_t result;
    uint32_t out; // QI: the out-parameter as an offset from the object (0xeeeeeeee untouched); face slots: same for pointers
    std::string text;
};

uint32_t Offset(const void *p, const void *object)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(object));
}

// One call on `object` (a side's buffer or file writer); `fakes` are the side's two streams (file writer).
Outcome Run(void *object, bool file, const Call &c, FakeStream *fakes)
{
    Outcome o{0, 0, {}};
    void *face = FaceOf(object, kFaceOffset);
    switch (c.op) {
    case kStartDocument:
    case kEndPI:
        o.result = SlotAt<NoArgs>(object, 0, c.op)(object) & 0xff;
        break;
    case kStartElement:
    case kEndElement:
    case kStartPI:
    case kText:
    case kCData:
    case kComment:
        o.result = SlotAt<OneText>(object, 0, c.op)(object, c.a.c_str()) & 0xff;
        break;
    case kAttribute:
        o.result = SlotAt<TwoTexts>(object, 0, c.op)(object, c.a.c_str(), c.b.c_str()) & 0xff;
        break;
    case kWrite:
        o.result = SlotAt<TextLength>(object, 0, 12)(object, c.a.c_str(), c.length) & 0xff;
        break;
    case kQuery:
    case kFaceQuery: {
        // The derived classes' own iid stores without a null test: always give them somewhere to write.
        void *out = reinterpret_cast<void *>(0xeeeeeeeeu);
        void *self = c.op == kQuery ? object : face;
        o.result = SlotAt<Query>(self, 0, 0)(self, c.riid, &out) & 0xff;
        o.out = out == reinterpret_cast<void *>(0xeeeeeeeeu) ? 0xeeeeeeeeu : Offset(out, object);
        break;
    }
    case kAddRef:
        o.result = SlotAt<NoArgs>(object, 0, 1)(object);
        break;
    case kFaceAddRef:
        o.result = SlotAt<NoArgs>(face, 0, 1)(face);
        break;
    case kRelease:
        o.result = SlotAt<NoArgs>(object, 0, 2)(object);
        break;
    case kFaceRelease:
        o.result = SlotAt<NoArgs>(face, 0, 2)(face);
        break;
    case kFaceSlot3:
        if (file) {
            SlotAt<SetStreamFn>(face, 0, 3)(face, &fakes[(c.flags >> 8) & 1], c.flags);
        } else {
            o.out = Offset(SlotAt<void *(__thiscall *)(void *)>(face, 0, 3)(face), object);
        }
        break;
    case kFaceSlot4:
        o.text = SlotAt<const char *(__thiscall *)(void *)>(face, 0, 4)(face);
        break;
    case kFaceSlot5:
        o.result = SlotAt<NoArgs>(face, 0, 5)(face);
        break;
    default:
        std::abort();
    }
    return o;
}

// The object's state, comparable across sides: vtable words as codes, the writer's fields, the string's text and words
// that are not heap addresses, or the stream as which fake (and the fakes' counts and writes).
std::vector<std::string> State(const void *object, bool file, bool ours, const FakeStream *fakes)
{
    std::vector<std::string> s;
    char line[64];
    std::snprintf(line, sizeof(line), "vt %08x face %08x", VtCode(shape::Word(object, 0), ours),
                  VtCode(shape::Word(object, kFaceOffset), ours));
    s.push_back(line);
    for (uint32_t off = 4; off < kFaceOffset; off += 4) {
        // Bytes 0xa and 0xb are never written (the heap's on a created object): the construct test compares them.
        const uint32_t word = shape::Word(object, off) & (off == 8 ? 0xffffu : 0xffffffffu);
        std::snprintf(line, sizeof(line), "+%02x %08x", off, word);
        s.push_back(line);
    }
    if (file) {
        const uint32_t stream = shape::Word(object, 0x1c);
        const uint32_t which = stream == shape::P(&fakes[0]) ? 1 : stream == shape::P(&fakes[1]) ? 2 : stream == 0 ? 0 : 3;
        std::snprintf(line, sizeof(line), "stream %u refs %u %u", which, fakes[0].refs, fakes[1].refs);
        s.push_back(line);
        for (int f = 0; f < 2; ++f)
            for (const std::string &w : fakes[f].writes) s.push_back(std::string(f ? "B:" : "A:") + w);
    } else {
        const auto *text = reinterpret_cast<const cRZString *>(static_cast<const uint8_t *>(object) + 0x1c);
        std::snprintf(line, sizeof(line), "string vt %08x refs %08x", shape::P(text->mVTable), text->mRefCount);
        s.push_back(line);
        s.push_back(text->mString);
    }
    return s;
}

// A side's writer: the original's from the image's creator, ours from ours.
void *Create(difftest::Context &ctx, bool file, bool ours)
{
    if (ours) {
        return file ? static_cast<void *>(cGZXmlWriterAdapters::CreateFileWriter())
                    : static_cast<void *>(cGZXmlWriterAdapters::CreateBufferWriter());
    }
    return ctx.Original<void *(__cdecl *)()>(file ? kCreateFile : kCreateBuffer)();
}

// Random fields before the script (both sides alike): what is open (4 is no state the writer makes), the depth (a
// little below zero now and then: EndElement decrements without a test), the inline flag and indenting as raw bytes.
void Preset(void *object, std::mt19937 &rng)
{
    auto *b = static_cast<uint8_t *>(object);
    b[8] = static_cast<uint8_t>(rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : 0x80);
    b[9] = static_cast<uint8_t>(rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : 0x40);
    *reinterpret_cast<uint32_t *>(b + 0xc) = rng() % 5;
    *reinterpret_cast<uint32_t *>(b + 0x10) = rng() % 6 == 0 ? static_cast<uint32_t>(-int(rng() % 4)) : rng() % 45;
}

void Compare(difftest::Context &ctx, int i, size_t k, int op, const Outcome &a, const Outcome &b,
             const std::vector<std::string> &sa, const std::vector<std::string> &sb)
{
    if (a.result != b.result || a.out != b.out || a.text != b.text) {
        ctx.Fail("case %d call %zu op %d: original %08x %08x \"%.40s\", ours %08x %08x \"%.40s\"", i, k, op, a.result, a.out,
                 a.text.c_str(), b.result, b.out, b.text.c_str());
    }
    if (sa != sb) {
        size_t n = 0;
        while (n < sa.size() && n < sb.size() && sa[n] == sb[n]) ++n;
        ctx.Fail("case %d call %zu op %d: state line %zu differs: \"%.60s\" / \"%.60s\"", i, k, op, n,
                 n < sa.size() ? sa[n].c_str() : "(none)", n < sb.size() ? sb[n].c_str() : "(none)");
    }
}

// The script on one writer class: random calls through each side's vtables, then the end: Release from a count of 1
// through either face (the block freed with 0x18 bytes, ORIGINAL-BUG in cGZXmlWriter.h), or for the buffer writer its
// deleting destructor (slot 13) with flags 0 (the destroyed object compared) or 1 (freed with 0x3c bytes).
void Script(difftest::Context &ctx, bool file, uint32_t seed, int cases)
{
    InitFakeVTable();
    Heap heap;
    std::mt19937 rng(seed);
    for (int i = 0; i < cases; ++i) {
        const uint32_t caseSeed = rng();
        std::mt19937 setup(caseSeed);
        std::vector<uint32_t> answers;
        // The stream's slot 16 returns a bool: al 0 or 1 (one write in ten fails), junk above it.
        for (int k = 0; k < 400; ++k) answers.push_back((setup() & 0xffffff00u) | (setup() % 10 == 0 ? 0u : 1u));
        const bool preset = setup() % 2;
        const int calls = int(setup() % 40);
        std::vector<Call> script;
        for (int k = 0; k < calls; ++k) script.push_back(RandomCall(setup, file));
        const uint32_t end = setup() % 4;
        const uint32_t endFlags = setup() & ~1u;
        std::mt19937 presetA(caseSeed ^ 0x5a5a5a5au), presetB(caseSeed ^ 0x5a5a5a5au);

        FakeStream fakes[2][2];
        void *objects[2];
        for (int side = 0; side < 2; ++side) {
            for (FakeStream &f : fakes[side]) {
                f.vtable = gFakeVTable;
                f.refs = 0x100;
                f.answers = answers;
            }
            ctx.Mark(side ? "xml ours: create" : "xml original: create");
            objects[side] = Create(ctx, file, side == 1);
            if (preset) Preset(objects[side], side ? presetB : presetA);
            if (file) {
                Call first{kFaceSlot3, {}, {}, 0, 0, 1};
                Run(objects[side], true, first, fakes[side]);
            }
        }
        ++ctx.cases;
        Compare(ctx, i, 0, -1, Outcome{}, Outcome{}, State(objects[0], file, false, fakes[0]), State(objects[1], file, true, fakes[1]));
        for (size_t k = 0; k < script.size(); ++k) {
            const Call &c = script[k];
            if ((c.op == kRelease || c.op == kFaceRelease) && shape::Word(objects[1], 4) <= 1) continue;
            ctx.Mark("xml original");
            const Outcome a = Run(objects[0], file, c, fakes[0]);
            ctx.Mark("xml ours");
            const Outcome b = Run(objects[1], file, c, fakes[1]);
            ++ctx.cases;
            Compare(ctx, i, k, c.op, a, b, State(objects[0], file, false, fakes[0]), State(objects[1], file, true, fakes[1]));
            if (ctx.failures.size() >= 20) return;
        }
        // The end.
        uint32_t results[2] = {};
        size_t freed[2] = {};
        std::vector<std::string> after[2];
        for (int side = 0; side < 2; ++side) {
            void *object = objects[side];
            gWatched = object;
            gFreedSize = 0;
            if (file || end < 2) {
                *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + 4) = 1;
                void *self = end % 2 ? FaceOf(object, kFaceOffset) : object;
                results[side] = SlotAt<NoArgs>(self, 0, 2)(self);
            } else {
                const uint32_t flags = endFlags | (end == 3 ? 1u : 0u);
                results[side] = SlotAt<Deleting>(object, 0, 13)(object, flags) == object;
                if (end == 2) {
                    after[side] = State(object, false, side == 1, fakes[side]);
                    ::operator delete(object);
                }
            }
            freed[side] = gFreedSize;
            gWatched = nullptr;
        }
        ++ctx.cases;
        if (results[0] != results[1] || freed[0] != freed[1] || after[0] != after[1]) {
            ctx.Fail("case %d end %u: original %08x freed 0x%zx, ours %08x freed 0x%zx%s", i, end, results[0], freed[0],
                     results[1], freed[1], after[0] != after[1] ? " (destroyed objects differ)" : "");
        }
        if (ctx.failures.size() >= 20) return;
    }
}

} // namespace

// The buffer writer (framework class 0xed50b6ac) from the creators 0x9c5261 / ours, through 0x9c515d and the base
// constructor 0x9c53de: cGZXmlWriter's slots StartDocument 0x9c5906, StartElement 0x9c548c, EndElement 0x9c557f,
// StartProcessingInstruction 0x9c54eb, EndProcessingInstruction 0x9c561e, Attribute 0x9c540f, Text 0x9c5739, CData
// 0x9c56ee, Comment 0x9c5761 and their helpers (close 0x9c553c, escape 0x9c57e7, indent 0x9c58b3), the buffer writer's
// Write 0x9c5384 (slot 12), QueryInterface 0x9c533a (and cGZXmlWriter's 0x9c567e behind it; the face's thunk 0x9c5367),
// AddRef (folded 0x146a50, 0x148fb0; the face's 0x242026), Release 0x9c5377 / 0x9c56b1 (the face's 0x9c537c), the
// face's GetString (folded 0x18ea60), GetText 0x9c52bb, GetLength 0x9c52c3, and the end: Release to zero (the base
// destructor 0x9c5408) or the deleting destructor 0x9c523f (destructor 0x9c5218).
DIFF_TEST(XmlWriter_BufferScript, 0x9c548c)
{
    Script(ctx, false, 7201, 3000);
}

// The file writer (framework class 0x0c7e8d17) from the creators 0x9c528e / ours, through 0x9c518f: SetStream 0x9c56d2
// (with RZUnknownSlot::Assign 0x142080), Write 0x9c53d2 (the fake stream's slot 16, failing now and then), so every
// slot of cGZXmlWriter above stops on a failed write somewhere; QueryInterface 0x9c564f (the face's 0x9c536f), AddRef,
// Release 0x9c5377 / 0x9c56b1 (face 0x9c537c) to zero.
DIFF_TEST(XmlWriter_FileScript, 0x9c53d2)
{
    Script(ctx, true, 7202, 3000);
}

// The constructors 0x9c515d and 0x9c518f (and the base's 0x9c53de) on filled memory against ours (the constructor hooks'
// adapters): every byte, the vtable words as codes and the string's words by its text; then the creators 0x9c5261 /
// 0x9c528e against ours: the sizes they allocate (the image's operator new stood in for) and the objects.
DIFF_TEST(XmlWriter_Construct, 0x9c515d)
{
    using Ctor = void *(__thiscall *)(void *);
    InitFakeVTable();
    const FakeStream none[2] = {};
    for (int file = 0; file < 2; ++file) {
        for (uint8_t fill : {uint8_t(0x00), uint8_t(0x5c), uint8_t(0xff)}) {
            alignas(8) uint8_t a[0x40], b[0x40];
            std::memset(a, fill, sizeof(a));
            std::memset(b, fill, sizeof(b));
            void *ra = ctx.Original<Ctor>(file ? kConstructFile : kConstructBuffer)(a);
            void *rb = file ? static_cast<void *>(cGZXmlWriterAdapters::ConstructFileWriter(b, nullptr))
                            : static_cast<void *>(cGZXmlWriterAdapters::ConstructBufferWriter(b, nullptr));
            ++ctx.cases;
            if ((ra == a) != (rb == b)) ctx.Fail("file %d fill %02x: results differ", file, fill);
            if (State(a, file, false, none) != State(b, file, true, none)) ctx.Fail("file %d fill %02x: objects differ", file, fill);
            // Bytes past the fields the state names: +8's two unwritten bytes, and after the object.
            const uint32_t size = file ? cGZXmlFileWriter::kSize : cGZXmlBufferWriter::kSize;
            if (std::memcmp(a + 0xa, b + 0xa, 2) != 0 || std::memcmp(a + size, b + size, sizeof(a) - size) != 0) {
                ctx.Fail("file %d fill %02x: untouched bytes differ", file, fill);
            }
            if (!file) {
                reinterpret_cast<cRZString *>(a + 0x1c)->Destruct();
                reinterpret_cast<cRZString *>(b + 0x1c)->Destruct();
            }
        }
    }
    // The creators: the size each asks for and the object it returns.
    static size_t requested;
    struct Sized {
        static void *__cdecl New(size_t n)
        {
            requested = n;
            return ::operator new(n);
        }
    };
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&Sized::New)};
    for (int file = 0; file < 2; ++file) {
        requested = 0;
        void *a = ctx.Original<void *(__cdecl *)()>(file ? kCreateFile : kCreateBuffer)();
        const size_t sizeA = requested;
        void *b = file ? static_cast<void *>(cGZXmlWriterAdapters::CreateFileWriter())
                       : static_cast<void *>(cGZXmlWriterAdapters::CreateBufferWriter());
        const size_t sizeB = file ? sizeof(cGZXmlFileWriter) : sizeof(cGZXmlBufferWriter);
        ++ctx.cases;
        if (sizeA != sizeB) ctx.Fail("file %d: the creator allocates 0x%zx, ours 0x%zx", file, sizeA, sizeB);
        if (State(a, file, false, none) != State(b, file, true, none)) ctx.Fail("file %d: created objects differ", file);
        if (!file) {
            reinterpret_cast<cRZString *>(static_cast<uint8_t *>(a) + 0x1c)->Destruct();
            reinterpret_cast<cRZString *>(static_cast<uint8_t *>(b) + 0x1c)->Destruct();
        }
        ::operator delete(a);
        ::operator delete(b);
    }
}
