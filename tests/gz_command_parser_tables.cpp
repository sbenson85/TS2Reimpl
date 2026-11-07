// Differential test for the command parser's own tables (lane RS; src/gz/CommandParserTables.cpp): what an image-free
// program's commands, functions and errors store in place of the image's vtables (standalone/owned_statics.cpp aliases
// the owned statics to them).
//
// Slot for slot: an entry whose image body is hooked is the replacement the hook installs; the image's _purecall is
// ours; an unhooked image body (folded, under 5 bytes) is the member of ours tested against that body elsewhere
// (tests/command_parser*.cpp) or one of the adapters tested here against the image's body. Each table is as long as the
// image's (the word after it is no code). Then dispatch on whole objects: the same bytes at the same address, once with
// the image's tables stored, once with ours, the same results and the same object afterwards, slot for slot.
#include "difftest.h"
#include "free_recorder.h"

#include "game/game.h"
#include "gz/CommandParser.h"
#include "gz/CommandParserTables.h"
#include "gz/ParserFunctions.h"
#include "hook/hooks.h"
#include "rz/TopCallersA.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" int __cdecl _purecall();

using namespace nGZCommandParser;

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
template <typename T> const void *Fn(T fn) { return reinterpret_cast<const void *>(fn); }

constexpr uint32_t kPurecall = 0x8261ba;

// The image's unhooked slot bodies and the entry of ours that does the same.
struct Unhooked { uint32_t rva; const void *ours; };
std::vector<Unhooked> UnhookedEntries()
{
    return {
        {0x12e40, Member(&cCommand::GetHelpDefault)},
        {0x6c6b0, Member(&cCommand::ReturnsZero)},
        {0x140fe0, Member(&cBlockCommandBase::EndBlock)},
        {0x142700, Fn(&cExpressionFunctionFace::FaceGetFunction)},
        {0x145260, Member(&cCommand::AddRef)},
        {0x147480, Member(&cCommand::ReturnsFalse)},
        {0x148fb0, Member(&cMetaCommand::AddRef)},
        {0x153f70, Member(&cMetaCommand::Release)},
        {0x1acd4f, Fn(&cExpressionFunctionFace::ErrorMessage)},
        {0x1b86e0, Member(&cElseCommand::ReturnsMinusOne)},
        {0x1b9a50, Member(&cMetaCommand::SetContext)},
        {0x1d0b40, Member(&cCommand::SetContext)},
        {0x247a60, Member(&cCommandT::DeletingDestructor)},
        {0x247a80, Fn(&cExpressionFunctionFace::DeletingDestructor)},
        {0x24c810, Member(&cCommandT::SetContextAdjusted)},
        {0x24c860, Member(&cMetaCommandT::SetContextAdjusted)},
        {0x2c2160, Member(&cBlockCommandT::DeletingDestructor)},
        {0x2c3c40, Member(&cBlockCommandT::SetContextAdjusted)},
        {0x313300, Member(&cBlockCommandBase::Release)},
        {0x369510, Member(&cCommandT::SetContextExact)},
        {0xacfdc0, Member(&cMetaCommandT::DeletingDestructor)},
        {0xb02350, Member(&cMetaCommandT::RegisterInScope)},
        {0xb03b80, Member(&cAliasStubCommand::GetName)},
    };
}

// True when the RVA is inside an executable section of the image.
bool IsCode(uint32_t rva)
{
    const auto *dos = GameAddress<IMAGE_DOS_HEADER>(0);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(reinterpret_cast<const char *>(dos) + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + s->Misc.VirtualSize)
            return (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    return false;
}

// ---- recording fakes for the calls the adapters make through the object's own vtables ----------------

struct Call { uint32_t self, slot, arg; };
std::vector<Call> gCalls;

uint32_t __fastcall RecAddRef(void *self, void *)
{
    gCalls.push_back({P(self), 1, 0});
    return 0x5151;
}
void *__fastcall RecDeleting(void *self, void *, uint32_t flags)
{
    gCalls.push_back({P(self), 1, flags});
    return self;
}
const void *const kRecTable[] = {nullptr, Fn(&RecAddRef), nullptr};
const void *const kRecDeleteTable[] = {nullptr, Fn(&RecDeleting), nullptr};

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].self != b[i].self || a[i].slot != b[i].slot || a[i].arg != b[i].arg) return false;
    return true;
}

uint32_t Pick(std::mt19937 &gen, std::initializer_list<uint32_t> special)
{
    if (gen() % 2) return gen();
    return *(special.begin() + gen() % special.size());
}

using QiFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
using Ret0Fn = uint32_t(__thiscall *)(void *);
using Ret4Fn = uint32_t(__thiscall *)(void *, uint32_t);
using Ret8Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

const OwnTable *Find(uint32_t rva)
{
    for (size_t t = 0; t < kOwnTableCount; ++t)
        if (kOwnTables[t].rva == rva) return &kOwnTables[t];
    return nullptr;
}

} // namespace

DIFF_TEST(CommandParser_OwnTables, 0xe7d72c)
{
    const uint32_t base = P(GameAddress<void>(0));
    const std::vector<Unhooked> unhooked = UnhookedEntries();
    for (size_t t = 0; t < kOwnTableCount; ++t) {
        const OwnTable &table = kOwnTables[t];
        const uint32_t *image = GameAddress<uint32_t>(table.rva);
        for (uint32_t i = 0; i < table.count; ++i) {
            ++ctx.cases;
            const uint32_t irva = image[i] - base;
            const void *ours = table.slots[i];
            if (!IsCode(irva)) {
                ctx.Fail("%x slot %u: the image's entry %x is no code (table too long)", table.rva, i, irva);
                continue;
            }
            if (irva == kPurecall) {
                if (ours != Fn(&_purecall)) ctx.Fail("%x slot %u: the image's is _purecall, ours %p", table.rva, i, ours);
                continue;
            }
            if (const HookSpec *h = HookAt(irva)) {
                if (h->replacement != ours)
                    ctx.Fail("%x slot %u: ours is %p, the hook of %x installs %p", table.rva, i, ours, irva, h->replacement);
                continue;
            }
            const Unhooked *u = nullptr;
            for (const Unhooked &e : unhooked)
                if (e.rva == irva) u = &e;
            if (!u) ctx.Fail("%x slot %u: the image's entry %x is neither hooked nor listed", table.rva, i, irva);
            else if (u->ours != ours) ctx.Fail("%x slot %u: ours is %p, the member doing %x's work is %p", table.rva, i, ours, irva, u->ours);
        }
        ++ctx.cases;
        if (IsCode(image[table.count] - base)) ctx.Fail("%x: the image's table has more than %u slots", table.rva, table.count);
    }
}

// The face's QueryInterface: ids 1 and 0xcc1d5da0 give the face, AddRef'd through its own slot 1; the result is a
// byte (al).
DIFF_TEST(cExpressionFunctionFace_QueryInterface, 0xaf7c60)
{
    auto orig = ctx.Original<QiFn>(0xaf7c60);
    std::mt19937 gen(0xaf7c60);
    for (int i = 0; i < 300; ++i) {
        uint32_t words[8];
        for (auto &w : words) w = gen();
        words[1] = P(kRecTable);
        uint32_t saved[8];
        std::memcpy(saved, words, sizeof(words));
        void *face = &words[1];
        const uint32_t iid = Pick(gen, {1, 0xcc1d5da0, 0, 2, 0xcae872f1});
        const bool nullOut = i % 5 == 0;
        void *outA = reinterpret_cast<void *>(0x2468aceu), *outB = outA;
        gCalls.clear();
        const uint32_t ra = orig(face, iid, nullOut ? nullptr : &outA);
        const auto callsA = gCalls;
        uint32_t after[8];
        std::memcpy(after, words, sizeof(words));
        std::memcpy(words, saved, sizeof(words));
        gCalls.clear();
        const uint32_t rb = cExpressionFunctionFace::FaceQueryInterface(face, nullptr, iid, nullOut ? nullptr : &outB);
        ++ctx.cases;
        if ((ra & 0xff) != (rb & 0xff)) ctx.Fail("face QI iid %x out %d: returned %u, original %u", iid, !nullOut, rb & 0xff, ra & 0xff);
        if (outA != outB) ctx.Fail("face QI iid %x: out %p, original %p", iid, outB, outA);
        if (!SameCalls(callsA, gCalls)) ctx.Fail("face QI iid %x: %zu calls, original %zu (or different)", iid, gCalls.size(), callsA.size());
        if (std::memcmp(after, words, sizeof(words)) != 0) ctx.Fail("face QI iid %x: object differs", iid);
    }
}

// The face's Release: the count at face+4; at zero the object's deleting destructor (its slot 1) with 1.
DIFF_TEST(cExpressionFunctionFace_Release, 0xaf7d80)
{
    auto orig = ctx.Original<Ret0Fn>(0xaf7d80);
    std::mt19937 gen(0xaf7d80);
    for (int i = 0; i < 300; ++i) {
        uint32_t words[8];
        for (auto &w : words) w = gen();
        words[0] = P(kRecDeleteTable);
        words[2] = Pick(gen, {1, 2, 0, 0x80000000u, 0x80000001u, 0xffffffffu});
        uint32_t saved[8];
        std::memcpy(saved, words, sizeof(words));
        void *face = &words[1];
        gCalls.clear();
        const uint32_t ra = orig(face);
        const auto callsA = gCalls;
        uint32_t after[8];
        std::memcpy(after, words, sizeof(words));
        std::memcpy(words, saved, sizeof(words));
        gCalls.clear();
        const uint32_t rb = cExpressionFunctionFace::FaceRelease(face, nullptr);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("face Release count %x: returned %x, original %x", saved[2], rb, ra);
        if (!SameCalls(callsA, gCalls)) ctx.Fail("face Release count %x: %zu calls, original %zu (or different)", saved[2], gCalls.size(), callsA.size());
        if (std::memcmp(after, words, sizeof(words)) != 0) ctx.Fail("face Release count %x: object differs", saved[2]);
    }
}

DIFF_TEST(cExpressionFunctionFace_GetFunction, 0x142700)
{
    auto orig = ctx.Original<Ret0Fn>(0x142700);
    std::mt19937 gen(0x142700);
    for (int i = 0; i < 200; ++i) {
        void *face = reinterpret_cast<void *>(static_cast<uintptr_t>(Pick(gen, {0, 4, 8, 0xffffffffu, 0x10000})));
        ++ctx.cases;
        const uint32_t ra = orig(face), rb = P(cExpressionFunctionFace::FaceGetFunction(face, nullptr));
        if (ra != rb) ctx.Fail("face GetFunction %p: %x, original %x", face, rb, ra);
    }
}

// cError's slot 0 (cRZString's body): the message string's characters, inline or on the heap by its capacity.
DIFF_TEST(cError_Message, 0x1acd4f)
{
    auto orig = ctx.Original<Ret0Fn>(0x1acd4f);
    std::mt19937 gen(0x1acd4f);
    for (int i = 0; i < 200; ++i) {
        uint32_t words[8];
        for (auto &w : words) w = gen();
        words[6] = Pick(gen, {0, 15, 16, 17, 0xffffffffu});
        uint32_t saved[8];
        std::memcpy(saved, words, sizeof(words));
        ++ctx.cases;
        const uint32_t ra = orig(words);
        const uint32_t rb = P(cExpressionFunctionFace::ErrorMessage(reinterpret_cast<const cError *>(words), nullptr));
        if (ra != rb) ctx.Fail("cError message capacity %x: %x, original %x", saved[6], rb, ra);
        if (std::memcmp(saved, words, sizeof(words)) != 0) ctx.Fail("cError message: object written");
    }
}

namespace {

// The functions' deleting destructors: both vtables stored; with bit 0 the block freed.
void FunctionDeletingTest(difftest::Context &ctx, uint32_t rva,
                          cExpressionFunction *(__fastcall *ours)(cExpressionFunction *, void *, uint32_t), size_t size)
{
    auto orig = ctx.Original<Ret4Fn>(rva);
    std::mt19937 gen(rva);
    for (int i = 0; i < 40; ++i) {
        const uint32_t flags = gen() % 4 | (i % 3 == 0 ? gen() & 0xfffffff0 : 0);
        auto *a = static_cast<uint32_t *>(::operator new(size));
        auto *b = static_cast<uint32_t *>(::operator new(size));
        for (size_t w = 0; w < size / 4; ++w) a[w] = b[w] = gen();
        uint32_t ra, rb;
        bool freedA, freedB;
        {
            difftest::FreeRecorder rec;
            ra = orig(a, flags);
            rec.Stop();
            freedA = rec.Freed(a);
        }
        {
            difftest::FreeRecorder rec;
            rb = P(ours(reinterpret_cast<cExpressionFunction *>(b), nullptr, flags));
            rec.Stop();
            freedB = rec.Freed(b);
        }
        ++ctx.cases;
        if (ra != P(a) || rb != P(b)) ctx.Fail("deleting destructor %x flags %x: wrong return", rva, flags);
        if (freedA != freedB) ctx.Fail("deleting destructor %x flags %x: freed original %d, ours %d", rva, flags, freedA, freedB);
        if (flags & 1) {
            if (!freedA) ctx.Fail("deleting destructor %x flags %x: original did not free", rva, flags);
        } else {
            if (std::memcmp(a, b, size) != 0) ctx.Fail("deleting destructor %x flags %x: object differs", rva, flags);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

} // namespace

DIFF_TEST(cExpressionFunction_DeletingDestructor, 0xaf7a30)
{
    FunctionDeletingTest(ctx, 0xaf7a30, &cExpressionFunctionFace::BaseDeletingDestructor, 0xc);
}
DIFF_TEST(cParserFunction_DeletingDestructor, 0x247a80)
{
    FunctionDeletingTest(ctx, 0x247a80, &cExpressionFunctionFace::DeletingDestructor, 0x10);
}

// Dispatch on whole objects. The commands: slots 0-3 and 5-8 (4, Execute, and the block and meta slots past 8 are run
// by the parser tests, and so is slot 3 of the block commands, which queries its argument, and cEndIfCommand's slot 8, which reads
// through a member pointer); the faces: slots 0-6 (the object's own table before the face); the functions' own slot 1 with
// flags 0; cError's slot 0. Pure slots are not called. The same random bytes at the same address (no word is 1, so no
// count reaches zero), the image's tables stored for the original run and ours for ours; results compared where the
// slot returns one, and the object afterwards with our table words read as the image's.
DIFF_TEST(CommandParser_OwnTablesDispatch, 0xe7d758)
{
    const uint32_t base = P(GameAddress<void>(0));
    std::mt19937 gen(0xe7d758);
    for (size_t t = 0; t < kOwnTableCount; ++t) {
        const OwnTable &table = kOwnTables[t];
        const uint32_t *image = GameAddress<uint32_t>(table.rva);
        const bool command = table.count >= 10, face = table.count == 7, function = table.count == 2;
        // A face's object: its own table is the one before it in kOwnTables (the image's and ours alike); cFunctionT's
        // face, which Initialise stores before the derived class's, sits on cExpressionFunction's own table.
        const OwnTable *own = !face ? nullptr : table.rva == 0xe7eefc ? Find(0xe7d7c4) : &kOwnTables[t - 1];
        if (face && (!own || own->count != 2 || (own->rva + 0xc != table.rva && table.rva != 0xe7eefc))) {
            ctx.Fail("%x: no function table before the face", table.rva);
            continue;
        }
        for (uint32_t i = 0; i < table.count; ++i) {
            const uint32_t irva = image[i] - base;
            if (irva == kPurecall) continue;
            if (command && (i == 4 || i > 8 || (i == 3 && (irva == 0xaf7bd0 || irva == 0x2c3c40)) || irva == 0xb02860)) continue;
            if (function && i != 1) continue;
            for (int c = 0; c < 30; ++c) {
                uint32_t words[32];
                for (auto &w : words)
                    do w = gen(); while (w == 1);
                const size_t at = face ? 1 : 0;
                words[at] = P(image);
                if (face) words[0] = P(GameAddress<uint32_t>(own->rva));
                uint32_t saved[32];
                std::memcpy(saved, words, sizeof(words));
                void *self = &words[at];
                const uint32_t a1 = i == 0 ? Pick(gen, {1, 0xcc1d5da0, 0xaada39c7, 0xcada39cd, 0xcae872f1, 0, 2})
                                  : i == 1 && function ? 0
                                  : i == 6 ? gen() % 4 : gen();
                const uint32_t a2 = gen();
                const bool nullOut = gen() % 4 == 0;
                void *outA = reinterpret_cast<void *>(0x2468aceu), *outB = outA;
                auto run = [&](const void *const *slots, void **out) -> uint32_t {
                    const void *fn = slots[i];
                    if (table.count == 1 || i == 5 || i == 7 || i == 8 || (i == 1 && !function) || i == 2 || (face && i == 4))
                        return reinterpret_cast<Ret0Fn>(fn)(self);
                    if (i == 0) return reinterpret_cast<QiFn>(fn)(self, a1, nullOut ? nullptr : out);
                    if (i == 1 || i == 6) return reinterpret_cast<Ret4Fn>(fn)(self, a1);
                    return reinterpret_cast<Ret8Fn>(fn)(self, a1, a2);
                };
                const uint32_t ra = run(reinterpret_cast<const void *const *>(image), &outA);
                uint32_t after[32];
                std::memcpy(after, words, sizeof(words));
                std::memcpy(words, saved, sizeof(words));
                words[at] = P(table.slots);
                if (face) words[0] = P(own->slots);
                const uint32_t rb = run(table.slots, &outB);
                // Ours stored where the image's were: read as the image's.
                if (words[at] == P(table.slots)) words[at] = P(image);
                if (face && words[0] == P(own->slots)) words[0] = P(GameAddress<uint32_t>(own->rva));
                ++ctx.cases;
                // Slot 3 returns nothing; QI and slot 7 return a byte.
                const bool byteResult = i == 0 || i == 7;
                if (i != 3 && (byteResult ? (ra & 0xff) != (rb & 0xff) : ra != rb))
                    ctx.Fail("%x slot %u (%x) case %d: returned %x, the image's table %x", table.rva, i, irva, c, rb, ra);
                if (outA != outB) ctx.Fail("%x slot %u case %d: out %p, the image's table %p", table.rva, i, c, outB, outA);
                if (std::memcmp(after, words, sizeof(words)) != 0)
                    ctx.Fail("%x slot %u (%x) case %d: object differs from the image's table's run", table.rva, i, irva, c);
            }
        }
    }
    // Every table took part.
    ++ctx.cases;
    if (!Find(0xda5060) || !Find(0xe7eefc)) ctx.Fail("cError's or cFunctionT's table missing from kOwnTables");
}
