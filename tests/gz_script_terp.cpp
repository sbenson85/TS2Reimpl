// Differential tests for the UI script runtime (lane MC, src/gz/cGZScriptTerp.h): cGZScriptVocab and cGZScriptTerp in
// the standalone shape, and the entity decoder.
//
// ScriptVocab_Run: a vocabulary made by the original creator (0x859ff7, constructor 0x85a288, game vtable 0xe47e28)
// against ours (Create), driven through its own vtable by the same random operations, then destroyed by the last Release
// (folded 0x85adc0; the deleting destructor 0x85a355, the destructor 0x85a31d). Operations: QueryInterface (0x85ad9c),
// AddRef (folded 0x148fb0) / Release, AddId (0x85a3b3), AddObject (0x85a430), Remove (0x85aded), Lookup (0x85ab13), Clear
// (0x85a482). The names come from a small pool with case variants (the order is _stricmp's, 0x725865). Compared after
// every operation: the result, what Lookup handed out, every pool name's Lookup (described by content and released),
// every fake's count; at the end of every seed, the blocks and bytes the side left allocated between the creator and
// the last Release (difftest::AllocationCounter: this thread's allocations minus frees, the test's own vectors included,
// which grow the same way on both sides; the event log's growth is not counted). Lane MH: before, every fourth seed
// compared a heap walk's count of blocks holding a variant, which blocks of other threads made fail in the full suite;
// the counter sees every block the vocabulary leaks, not only variants, on every seed.
//
// ScriptTerp_Run: a terp made by the original creator (0x859fe1, constructor 0x85a202 with its vocabulary and attribute
// list (0x85a19a), game vtable 0xe47e50) against ours, driven the same way, then destroyed (folded 0x1af4d8; the deleting
// destructor 0x85a336, the destructor 0x85a2c7). Operations: QueryInterface (0x85ad78), AddRef / Release, SetTarget
// (0x85ae66), SetContext (0x85ae41), GetContext (folded 0x15e550), RunText (0x85a8e9) over generated script text, AddId
// (0x85a374), AddObject (0x85a3a8), Remove (0x85ade2), Clear (0x85add7), PushVocab (0x85ad51) of a vocabulary from the
// same side's creator, PopVocab (0x85acfe). RunText reaches the parser: the run 0x85a563, the tokenizer 0x85a939 (and
// the decoder 0x85aed3 for quoted values), the lookup 0x85a903, the id parser 0x85ab63, BeginRun 0x85b1d2, OnText
// 0x85b0b1, BeginTag 0x85b0c9, BeginAttribute 0x85b10c, FlushAttribute 0x85b045 (the attribute list's push 0x85ad26
// and growth 0x85abdf, whose copies are 0x85afcb and 0x85a50f), CloseTag 0x85aff3, StringValue 0x85b12e, WordValue
// 0x85b167, ClearAttributes 0x85a4b8 (erase 0x85a535, its copy 0x85a0cb), EndRunFailed 0x85b02a, EndRunSucceeded
// 0x85b1e8. The target is a recording C++ object with the three
// target faces (its interpreting and compiling faces handed out or not, per seed). Compared after every operation: the
// result, the event log (the target's calls with their arguments by content: texts, ids, values, the attribute list),
// every field (the vocabulary stack by identity, the pending value and the attribute list by content), every vocabulary's
// Lookup of every pool name, every fake's and the target's count.
//
// ScriptTerp_Sources: RunFile (0x85a76b) and RunResource (0x85a801) on both sides over the same texts. RunFile's cRZFile
// is stood in for on both sides (Construct 0x1c0c07, Exists 0x1c114e, Open 0x1c285d, Length 0x1c230b, ReadWithCount
// 0x1c2c97, Destruct 0x1c0e6d: the file's text and length are the test's); RunResource's resource manager (from the
// service object, 0x20db69), record and serial stream are recording fakes. The image's malloc / free (0x8252b1 /
// 0x6c4b50) are ours. Compared: the result, the event log (with the target's calls), the fakes' counts.
//
// ScriptDecode_Text: the decoder 0x85aed3 (cdecl, a cRZString result) against DecodeScriptEntities on generated texts.
//
// Every test stands in for the game's operator new / delete (0x824632 / 0x824624) with ours, and builds the game's
// allocator service (the attribute list's cGZDefaultAllocator).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZScriptTerp.h"
#include "gz/cIGZString.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZFile.h"
#include "rz/cRZVariant.h"
#include "rz/cRZString.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace script_terp_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

constexpr uint32_t kTag = 0x7d000000;       // the object under test
constexpr uint32_t kTargetTag = 0x7b000000; // the recording target
constexpr unsigned kObjects = 4;            // fakes 0..3: objects a vocabulary holds
constexpr unsigned kContextA = 4;
constexpr unsigned kContextB = 5;
constexpr unsigned kService = 6, kManager = 7, kRecord = 8, kStream = 9;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl GameMalloc(size_t n) { return std::malloc(n); }
void __cdecl GameFree(void *p) { std::free(p); }

uint32_t Hash(const char *p, size_t n)
{
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(p[i])) * 0x01000193;
    return h;
}
void LogText(const cIGZString *s)
{
    if (!s) {
        Ev(0xdead);
        return;
    }
    const uint32_t n = s->Strlen();
    Ev(n);
    Ev(Hash(s->Data(), n));
}

bool IsFake(const void *p)
{
    return P(p) >= P(&svcfx::gObjs[0]) && P(p) < P(&svcfx::gObjs[svcfx::kFakes]);
}

// A value the code under test hands out: a fake by name, anything else as the variant it is (type, then the value of an
// id or a text).
void Describe(cIGZUnknown *p)
{
    if (!p) {
        Ev(0);
        return;
    }
    if (IsFake(p)) {
        Ev(svcfx::Norm(P(p)));
        return;
    }
    auto *v = static_cast<cIGZVariant *>(p);
    Ev(0xa0);
    const uint32_t type = v->GetType();
    Ev(type);
    if (type == 6) {
        Ev(*v->AsUint32Ref());
    } else if (type == 0x800e) {
        const uint32_t n = v->GetCount();
        Ev(n);
        Ev(Hash(v->GetRZCharArray(), n));
    }
}

// A cRZString on the test's stack.
struct Text {
    alignas(cRZString) unsigned char storage[sizeof(cRZString)];
    explicit Text(const char *text) { reinterpret_cast<cRZString *>(storage)->Construct(text); }
    explicit Text(const std::string &text) : Text(text.c_str()) {}
    ~Text() { reinterpret_cast<cRZString *>(storage)->Destruct(); }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;
    const cIGZString &Face() { return *reinterpret_cast<cRZString *>(storage)->AsInterface(); }
};

// ---- the recording target ----

class Target final : public cIGZScriptTarget, public cIGZScriptInterpretingTarget, public cIGZScriptCompilingTarget {
public:
    int32_t refs = 1000;
    bool interpreting = true;
    bool compiling = true;

    bool QueryInterface(uint32_t iid, void **out) override
    {
        Ev(0x7100);
        Ev(iid);
        if (iid == cIGZScriptInterpretingTarget::kIID && interpreting) {
            *out = static_cast<cIGZScriptInterpretingTarget *>(this);
        } else if (iid == cIGZScriptCompilingTarget::kIID && compiling) {
            *out = static_cast<cIGZScriptCompilingTarget *>(this);
        } else {
            return false;
        }
        ++refs;
        return true;
    }
    uint32_t AddRef() override
    {
        Ev(0x7101);
        return uint32_t(++refs);
    }
    uint32_t Release() override
    {
        Ev(0x7102);
        return uint32_t(--refs);
    }
    bool Attach(void *host) override
    {
        Ev(0x7103);
        Ev(svcfx::Norm(P(host)));
        return Draw(2);
    }
    bool Detach(void *host, bool keep) override
    {
        Ev(0x7104);
        Ev(svcfx::Norm(P(host)));
        Ev(keep);
        return Draw(2);
    }
    bool OnText(void *host, cIGZString *text) override
    {
        Ev(0x7105);
        Ev(svcfx::Norm(P(host)));
        LogText(text);
        return Draw(2);
    }
    bool BeginCompile(void *host, uint32_t id) override
    {
        Ev(0x7106);
        Ev(svcfx::Norm(P(host)));
        Ev(id);
        return Draw(2);
    }
    bool EndCompile(void *host, uint32_t id) override
    {
        Ev(0x7107);
        Ev(svcfx::Norm(P(host)));
        Ev(id);
        return Draw(2);
    }
    bool OnAttribute(void *host, uint32_t id, cIGZUnknown *value) override
    {
        Ev(0x7108);
        Ev(svcfx::Norm(P(host)));
        Ev(id);
        Describe(value);
        return Draw(2);
    }
    bool OnCompile(void *host, int32_t event, void *data) override
    {
        Ev(0x7109);
        Ev(svcfx::Norm(P(host)));
        Ev(uint32_t(event));
        Ev(svcfx::Norm(P(data)));
        const auto *list = static_cast<const cGZScriptAttributes *>(data);
        Ev(list->Size());
        for (const cGZScriptAttribute *a = list->mBegin; a != list->mEnd; ++a) {
            Ev(a->mId);
            Describe(a->mValue);
        }
        return Draw(2);
    }
};

Target gTarget;

// ---- names and script text ----

const char *const kNames[] = {"btn", "BTN", "Win", "cap", "id", "x", "val", "Zz9", "12", "", "Cap"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

const char *const kPieces[] = {
    "hello", " ", "\t\n", "a&amp;b", "x>y", "=", "<", ">", "<btn>", "<BTN cap=\"Hi &lt;there&gt;\">", "<win id=12 val=x>",
    "<12 cap=word>", "<0x1f>", "<nope cap=1>", "<btn nope=3 cap=\"q\">", "<btn cap>", "<btn cap= >", "<btn = >",
    "<btn cap=\"&bogus; &amp &apos;&quot;\">", "<btn cap=\"\">", "<\"q\">", "<btn cap=\"unterminated", "<win cap=\"",
    "<btn cap=x=y>", "<win Zz9=val val=Zz9>", "</btn>", "<btn/>", "<<>>", "<x x x>", "<id id=id>",
    "<btn cap=\"a\"\"b\">", "&lt;", "\"q\"", "<btn\tcap\n=\r\"t\">", "<99 99=99>", "<-1>", "<btn cap='x'>",
    "<Win nope=\"skip\" cap=5>", "<nope nope=nope>", "<btn nope>", "<btn nope cap=x>", "<btn cap=\"&amp;&amp;&lt;\">",
    "<btn cap=\"&verylongname;\">", "<x cap=Cap>", "<btn cap=12 id=0x10>", "text with spaces ", "\"",
    "<btn 1=x>", "<12 2=Cap>",
    "<12 1=x 2=x 3=x 4=x 5=x 6=x 7=x 8=x 9=x 10=x 11=x 12=x 13=x 14=x 15=x 16=x 17=x 18=x>",
    "<btn 1=1 2=2 3=3 4=4 5=5 6=6 7=7 8=8 9=9>",
};
constexpr unsigned kPieceCount = sizeof(kPieces) / sizeof(kPieces[0]);

std::string Script()
{
    std::string s;
    const unsigned n = Draw(11);
    for (unsigned i = 0; i < n; ++i) s += kPieces[Draw(kPieceCount)];
    return s;
}

// ---- calls through a face's own vtable ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

// Every pool name's Lookup in a vocabulary (slot 6), described and released.
void LookupAll(void *vocab)
{
    for (const char *name : kNames) {
        Text text(name);
        cIGZUnknown *out = reinterpret_cast<cIGZUnknown *>(uintptr_t(0x5a5a5a5a));
        const uint32_t r = Slot<uint32_t>(vocab, 6, &text.Face(), &out) & 0xff;
        Ev(0x10c);
        Ev(r);
        if (r) {
            Describe(out);
            out->Release();
        } else {
            Ev(P(out));
        }
    }
}

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    gRng.seed(seed);
    for (unsigned o = 0; o < kObjects; ++o) F(o).Set(3, 0, Draw(2) ? 4 : 0x800e); // GetType, as ParseId asks it
    gTarget.refs = 1000;
    gTarget.interpreting = Draw(4) != 0;
    gTarget.compiling = Draw(4) != 0;
}

// ---- the vocabulary ----

enum : unsigned { vQuery, vAddRef, vAddId, vAddObject, vRemove, vLookup, vClear, vCount };

void VocabOp(Blob &b, void *vocab, unsigned kind)
{
    Text name(kNames[Draw(kNameCount)]);
    switch (kind) {
    case vQuery: {
        const uint32_t iids[] = {1, cIGZScriptVocab::kIID, cIGZScriptTerp::kIID, 0};
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = Slot<uint32_t>(vocab, 0, iids[Draw(4)], &out) & 0xff;
        b.push_back(r);
        b.push_back(svcfx::NormOut(P(out), 0x5a5a5a5a));
        if (r) b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    case vAddRef:
        b.push_back(Slot<uint32_t>(vocab, 1));
        b.push_back(Slot<uint32_t>(vocab, 2));
        break;
    case vAddId: b.push_back(Slot<uint32_t>(vocab, 3, &name.Face(), Draw(3) ? Draw(50) : gRng()) & 0xff); break;
    case vAddObject: b.push_back(Slot<uint32_t>(vocab, 4, &name.Face(), &F(Draw(kObjects))) & 0xff); break;
    case vRemove: b.push_back(Slot<uint32_t>(vocab, 5, &name.Face()) & 0xff); break;
    case vLookup: {
        cIGZUnknown *out = reinterpret_cast<cIGZUnknown *>(uintptr_t(0x5a5a5a5a));
        const uint32_t r = Slot<uint32_t>(vocab, 6, &name.Face(), &out) & 0xff;
        b.push_back(r);
        if (r) {
            Describe(out);
            out->Release();
        } else {
            b.push_back(P(out));
        }
        break;
    }
    case vClear: b.push_back(Slot<uint32_t>(vocab, 7) & 0xff); break;
    }
}

// ---- the terp ----

std::vector<void *> gVocabs; // the vocabularies a side pushed, in order (identities for the dump)

uint32_t VocabName(uint32_t p)
{
    for (size_t i = gVocabs.size(); i-- > 0;)
        if (P(gVocabs[i]) == p) return 0x300 + uint32_t(i);
    return 0x3ee; // the terp's own
}

// The constructor (0x85a202) stores neither +0x30 (the open-tag flag) nor +0x34 (the tag id): BeginRun (0x85b1d2) and
// BeginTag (0x85b0c9) set them before any read. A terp from a creator has them as each side's heap had them, so those
// two words are compared only for terps the test built on its own pre-filled memory (gMaskTag false).
bool gMaskTag;

void DumpTerp(Blob &b, void *t, bool ours, const void *ourTable)
{
    b.push_back(ours ? Word(t, 0) == P(ourTable) : Word(t, 0) == P(GameAddress<void>(cGZScriptTerp::kVTable)));
    b.push_back(Word(t, 4));
    for (uint32_t off : {8u, 0xcu, 0x10u}) b.push_back(svcfx::Norm(Word(t, off)));
    const uint32_t vb = Word(t, 0x14), ve = Word(t, 0x18);
    b.push_back((ve - vb) / 4);
    for (uint32_t p = vb; p < ve; p += 4) b.push_back(VocabName(*reinterpret_cast<const uint32_t *>(uintptr_t(p))));
    b.push_back(svcfx::Norm(Word(t, 0x20)));
    b.push_back(Word(t, 0x24));
    b.push_back(Word(t, 0x28) != 0);
    b.push_back(Word(t, 0x2c) != 0);
    b.push_back(gMaskTag ? 0 : Word(t, 0x30) & 0xff);
    b.push_back(gMaskTag ? 0 : Word(t, 0x34));
    b.push_back(Word(t, 0x38) & 0xff);
    b.push_back(Word(t, 0x3c));
    svcfx::gEv.swap(b);
    Describe(reinterpret_cast<cIGZUnknown *>(uintptr_t(Word(t, 0x40))));
    const auto *list = reinterpret_cast<const cGZScriptAttributes *>(static_cast<const uint8_t *>(t) + 0x44);
    Ev(list->Size());
    Ev(uint32_t(list->mCapacity - list->mBegin));
    for (const cGZScriptAttribute *a = list->mBegin; a != list->mEnd; ++a) {
        Ev(a->mId);
        Describe(a->mValue);
    }
    Ev(list->mSpare0c);
    Ev(list->mAllocator != nullptr);
    Ev(list->mSpare14);
    for (uint32_t p = vb; p < ve; p += 4) LookupAll(reinterpret_cast<void *>(uintptr_t(*reinterpret_cast<const uint32_t *>(uintptr_t(p)))));
    svcfx::gEv.swap(b);
}

enum : unsigned {
    tQuery, tAddRef, tSetTarget, tSetContext, tGetContext, tRun, tRun2, tRun3, tAddId, tAddObject, tRemove, tClear, tPush,
    tPop, tCount
};

void TerpOp(Blob &b, void *terp, unsigned kind, difftest::Context &ctx, bool ours)
{
    Text name(kNames[Draw(kNameCount)]);
    switch (kind) {
    case tQuery: {
        const uint32_t iids[] = {1, cIGZScriptTerp::kIID, cIGZScriptVocab::kIID, 0};
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = Slot<uint32_t>(terp, 0, iids[Draw(4)], &out) & 0xff;
        b.push_back(r);
        b.push_back(svcfx::NormOut(P(out), 0x5a5a5a5a));
        if (r) b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    case tAddRef:
        b.push_back(Slot<uint32_t>(terp, 1));
        b.push_back(Slot<uint32_t>(terp, 2));
        break;
    case tSetTarget:
        b.push_back(Slot<uint32_t>(terp, 3, Draw(4) ? static_cast<cIGZScriptTarget *>(&gTarget) : nullptr) & 0xff);
        break;
    case tSetContext: {
        const unsigned which = Draw(3);
        b.push_back(Slot<uint32_t>(terp, 4, which == 2 ? nullptr : &F(which ? kContextA : kContextB)) & 0xff);
        break;
    }
    case tGetContext: b.push_back(svcfx::Norm(Slot<uint32_t>(terp, 5))); break;
    case tRun:
    case tRun2:
    case tRun3: {
        Text script(Script());
        b.push_back(Slot<uint32_t>(terp, 6, &script.Face()) & 0xff);
        break;
    }
    case tAddId: b.push_back(Slot<uint32_t>(terp, 9, &name.Face(), Draw(3) ? Draw(50) : gRng()) & 0xff); break;
    case tAddObject: b.push_back(Slot<uint32_t>(terp, 10, &name.Face(), &F(Draw(kObjects))) & 0xff); break;
    case tRemove: b.push_back(Slot<uint32_t>(terp, 11, &name.Face()) & 0xff); break;
    case tClear: b.push_back(Slot<uint32_t>(terp, 12) & 0xff); break;
    case tPush: {
        void *vocab = nullptr;
        if (Draw(6)) {
            vocab = ours ? static_cast<void *>(cGZScriptVocab::Create()) : ctx.Original<void *(__cdecl *)()>(0x859ff7)();
            gVocabs.push_back(vocab);
            if (Draw(2)) { // with a name of its own
                Text own(kNames[Draw(kNameCount)]);
                Slot<uint32_t>(vocab, 3, &own.Face(), Draw(50));
            }
        }
        b.push_back(Slot<uint32_t>(terp, 13, vocab) & 0xff);
        break;
    }
    case tPop: b.push_back(Slot<uint32_t>(terp, 14) & 0xff); break;
    }
}

bool Compare(difftest::Context &ctx, const std::vector<Blob> (&results)[2], const char *what, uint32_t seed,
             const std::vector<unsigned> &ops)
{
    for (size_t i = 0; i < results[0].size(); ++i) {
        if (!svcfx::Same(ctx, results[0][i], results[1][i], what, seed)) {
            const size_t op = i ? i - 1 : 0;
            ctx.Fail("  at step %u of %u (op %u)", unsigned(i), unsigned(ops.size() + 2), i && op < ops.size() ? ops[op] : 99u);
            return false;
        }
    }
    return true;
}

void CaptureTarget(Blob &b)
{
    svcfx::CaptureFakes(b);
    b.push_back(uint32_t(gTarget.refs));
}

// ---- file and resource stand-ins ----

std::string gFileText;
uint32_t gFileLength;
uint32_t gExists;

cRZFile *__fastcall StandConstruct(cRZFile *self, void *, const cIGZString &path)
{
    Ev(0xf1);
    LogText(&path);
    return self;
}
bool __fastcall StandExists(cRZFile *, void *)
{
    Ev(0xf2);
    return gExists != 0;
}
bool __fastcall StandOpen(cRZFile *, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    Ev(0xf3);
    Ev(access);
    Ev(creation);
    Ev(sharing);
    return Draw(2) != 0;
}
uint32_t __fastcall StandLength(cRZFile *, void *)
{
    Ev(0xf4);
    return gFileLength;
}
bool __fastcall StandRead(cRZFile *, void *, void *dst, uint32_t &length)
{
    Ev(0xf5);
    Ev(length);
    const uint32_t n = length < gFileText.size() ? length : uint32_t(gFileText.size());
    std::memcpy(dst, gFileText.data(), n);
    length = n;
    return Draw(2) != 0;
}
void __fastcall StandDestruct(cRZFile *, void *) { Ev(0xf6); }

uint32_t HandOut(unsigned fake, uint32_t out, uint32_t yes)
{
    if (yes & 1) {
        ++F(fake).refs;
        *reinterpret_cast<void **>(uintptr_t(out)) = &F(fake);
    }
    return yes;
}
uint32_t ServiceQuery(Obj *, uint32_t *a)
{
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc) return HandOut(kManager, a[2], 0x101);
    return 0x100;
}
// The manager's OpenRecord (key, &record, write) and CloseRecord (key, &record): the key logged.
uint32_t ManagerOpen(Obj *, uint32_t *a)
{
    const auto *k = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    Ev(a[2]);
    if (Draw(6) == 0) return 0x100;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kRecord);
    return 0x101;
}
uint32_t ManagerClose(Obj *, uint32_t *a)
{
    const auto *k = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    Ev(svcfx::Norm(*reinterpret_cast<const uint32_t *>(uintptr_t(a[1]))));
    return Bool();
}
uint32_t RecordQuery(Obj *, uint32_t *a)
{
    if (a[0] != 0xc56b8f08) return 0x100;
    return HandOut(kStream, a[1], Draw(6) ? 0x101 : 0x100);
}
// The stream's GetFieldVoid (data, size), in place of the generic slot (which would log the buffer's address, a block
// each side allocates its own way): logged like a generic call with the size, the resource's text written, as much as
// fits (the rest 'z').
uint32_t __fastcall StreamReadShim(Obj *self, void *, uint32_t data, uint32_t size)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(27);
    Ev(2);
    Ev(data != 0);
    Ev(size);
    const uint32_t n = size < gFileText.size() ? size : uint32_t(gFileText.size());
    std::memcpy(reinterpret_cast<void *>(uintptr_t(data)), gFileText.data(), n);
    std::memset(reinterpret_cast<uint8_t *>(uintptr_t(data)) + n, 'z', size - n);
    return Bool();
}

} // namespace script_terp_test

using namespace script_terp_test;

DIFF_TEST(ScriptVocab_Run, 0x859ff7)
{
    // Also reached: every RVA in the file comment's ScriptVocab_Run paragraph.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch gameMalloc(0x8252b1, shape::CodeOf(&GameMalloc));
    shape::CodePatch gameFree(0x6c4b50, shape::CodeOf(&GameFree));
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 opRng(seed * 7919);
        std::vector<unsigned> ops(1 + opRng() % 24);
        for (unsigned &op : ops) op = opRng() % vCount;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            // What the side leaves allocated, from the creator to the last Release (the file comment).
            difftest::AllocationCounter left;
            void *vocab = side ? static_cast<void *>(cGZScriptVocab::Create()) : ctx.Original<void *(__cdecl *)()>(0x859ff7)();
            svcfx::AddRange(vocab, cGZScriptVocab::kSize, kTag);
            Blob first{Word(vocab, 4), side ? 1u : Word(vocab, 0) == P(GameAddress<void>(cGZScriptVocab::kVTable))};
            results[side].push_back(std::move(first));
            Slot<uint32_t>(vocab, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    VocabOp(b, vocab, ops[i]);
                    b.push_back(Word(vocab, 4));
                    LookupAll(vocab);
                } else {
                    b.push_back(Slot<uint32_t>(vocab, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
            left.Stop();
            svcfx::gRanges.clear();
            results[side].back().push_back(uint32_t(left.Blocks()));
            results[side].back().push_back(uint32_t(left.Bytes()));
        }
        if (!Compare(ctx, results, "ScriptVocab_Run", seed, ops)) break;
    }
}

DIFF_TEST(ScriptTerp_Run, 0x859fe1)
{
    // Also reached: every RVA in the file comment's ScriptTerp_Run paragraph.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch gameMalloc(0x8252b1, shape::CodeOf(&GameMalloc));
    shape::CodePatch gameFree(0x6c4b50, shape::CodeOf(&GameFree));
    svcfx::gEv.clear();
    const void *ourTable = nullptr;
    {
        cIGZScriptTerp *probe = cGZScriptTerp::Create();
        ourTable = *reinterpret_cast<void **>(probe);
        probe->AddRef();
        probe->Release();
    }

    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        std::mt19937 opRng(seed * 7919);
        std::vector<unsigned> ops(1 + opRng() % 20);
        for (unsigned &op : ops) op = opRng() % tCount;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gVocabs.clear();
            gMaskTag = seed % 3 == 0;
            void *terp;
            Blob first;
            if (gMaskTag) { // the creators
                terp = side ? static_cast<void *>(cGZScriptTerp::Create()) : ctx.Original<void *(__cdecl *)()>(0x859fe1)();
            } else { // the constructors, on memory filled alike
                void *memory = ::operator new(cGZScriptTerp::kSize);
                std::memset(memory, 0xc5, cGZScriptTerp::kSize);
                terp = side ? static_cast<void *>(::new (memory) cGZScriptTerp)
                            : ctx.Original<void *(__thiscall *)(void *)>(0x85a202)(memory);
                first.push_back(terp == memory);
            }
            svcfx::AddRange(terp, cGZScriptTerp::kSize, kTag);
            svcfx::AddRange(&gTarget, sizeof(gTarget), kTargetTag);
            DumpTerp(first, terp, side == 1, ourTable);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(terp, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    TerpOp(b, terp, ops[i], ctx, side == 1);
                    DumpTerp(b, terp, side == 1, ourTable);
                } else {
                    b.push_back(Slot<uint32_t>(terp, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                CaptureTarget(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        if (!Compare(ctx, results, "ScriptTerp_Run", seed, ops)) break;
    }
}

DIFF_TEST(ScriptTerp_Sources, 0x85a76b)
{
    // Also reached: RunResource 0x85a801, and the parser (ScriptTerp_Run's list).
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch gameMalloc(0x8252b1, shape::CodeOf(&GameMalloc));
    shape::CodePatch gameFree(0x6c4b50, shape::CodeOf(&GameFree));
    using ConstructPath = cRZFile *(cRZFile::*)(const cIGZString &);
    shape::StandIn construct(0x1c0c07, static_cast<ConstructPath>(&cRZFile::Construct), &StandConstruct);
    shape::StandIn exists(0x1c114e, &cRZFile::Exists, &StandExists);
    shape::StandIn open(0x1c285d, &cRZFile::Open, &StandOpen);
    shape::StandIn length(0x1c230b, &cRZFile::Length, &StandLength);
    shape::StandIn read(0x1c2c97, &cRZFile::ReadWithCount, &StandRead);
    shape::StandIn destruct(0x1c0e6d, &cRZFile::Destruct, &StandDestruct);
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::vector<Blob> results[2];
        const bool resource = seed % 2 == 0;
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gFileText = Script();
            const uint32_t sizes[] = {uint32_t(gFileText.size()), uint32_t(gFileText.size()) + Draw(4), Draw(8), 0};
            gFileLength = sizes[Draw(4)];
            gExists = Draw(6) != 0;
            F(kService).Set(5, "pdp", 0, &ServiceQuery);
            F(kManager).Set(39, "pp-", 0, &ManagerOpen);
            F(kManager).Set(40, "pp", 0, &ManagerClose);
            F(kRecord).Set(0, "dp", 0, &RecordQuery);
            F(kStream).Set(52, 0, gFileLength);
            F(kStream).Set(27, 2, 0u);
            F(kStream).table[27] = reinterpret_cast<void *>(&StreamReadShim);
            globals.SetWord(0x10c9378, P(&F(kService)));
            void *terp = side ? static_cast<void *>(cGZScriptTerp::Create()) : ctx.Original<void *(__cdecl *)()>(0x859fe1)();
            svcfx::AddRange(terp, cGZScriptTerp::kSize, kTag);
            svcfx::AddRange(&gTarget, sizeof(gTarget), kTargetTag);
            Slot<uint32_t>(terp, 1);
            Slot<uint32_t>(terp, 3, static_cast<cIGZScriptTarget *>(&gTarget));
            Blob b;
            if (resource) {
                uint32_t key[4] = {gRng(), gRng(), gRng(), gRng()};
                b.push_back(Slot<uint32_t>(terp, 8, key) & 0xff);
            } else {
                Text path(kNames[Draw(kNameCount)]);
                b.push_back(Slot<uint32_t>(terp, 7, &path.Face()) & 0xff);
            }
            b.push_back(Word(terp, 0x24));
            b.push_back(Word(terp, 0x2c));
            b.push_back(Slot<uint32_t>(terp, 2));
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            CaptureTarget(b);
            results[side].push_back(std::move(b));
            svcfx::gRanges.clear();
        }
        if (!Compare(ctx, results, resource ? "ScriptTerp_Sources (resource)" : "ScriptTerp_Sources (file)", seed, {}))
            break;
    }
}

DIFF_TEST(ScriptDecode_Text, 0x85aed3)
{
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    const char *const kParts[] = {"&", ";", "lt", "gt", "amp", "quot", "apos", "&lt;", "&gt;", "&amp;", "&quot;", "&apos;",
                                  "&LT;", "&ltx;", "&abcdefgh;", "&abcdefghi;", "x", " ", "&&", "&;", "&amp;lt;", "<"};
    for (uint32_t seed = 1; seed <= 20000; ++seed) {
        gRng.seed(seed);
        std::string in;
        const unsigned n = Draw(9);
        for (unsigned i = 0; i < n; ++i) in += kParts[Draw(sizeof(kParts) / sizeof(kParts[0]))];
        Text input(in);
        alignas(cRZString) unsigned char out[sizeof(cRZString)];
        void *r = ctx.Original<void *(__cdecl *)(void *, const void *)>(0x85aed3)(out, input.storage);
        const std::string theirs = reinterpret_cast<cRZString *>(out)->mString;
        reinterpret_cast<cRZString *>(out)->Destruct();
        const std::string ours = DecodeScriptEntities(in);
        ++ctx.cases;
        if (r != out || theirs != ours) {
            ctx.Fail("ScriptDecode_Text seed %u: \"%s\" -> \"%s\" (ours \"%s\")", seed, in.c_str(), theirs.c_str(), ours.c_str());
            break;
        }
    }
}
