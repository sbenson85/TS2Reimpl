// Differential tests for the rest of cRZMessage2Standard (src/rz/cRZMessage2Standard.cpp, hook shape): the constructor
// and destructor, Create (slot 3), the string / object / extra setters (slots 30, 33, 36), HasExtra (34), the string and
// extra getters (29, 35) and the deleting destructor (37). The objects the message holds are drawfx fakes of cIGZUnknown
// (their AddRef / Release / QueryInterface calls are logged in order; two answer iid 0x1234 with a face fake). Where the
// code calls the message's own slots (Create: slot 4; the getters: slots 28 and 34), a fake is embedded in the message,
// so those calls are logged and answered independently of the flag byte. Every object word is noted after each call,
// pointers normalised (fakes by id, words into the message by offset). The memory Create and the deleting destructor
// use comes from one test GeneralAllocator behind both the image's RZMalloc / RZFree and our RZMemory (as in
// tests/gz_dbpf_index_modifiable.cpp); frees are counted on both routes.
#include "difftest.h"
#include "code_patch.h"
#include "drawfx.h"
#include "game_allocator.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using Self = cRZMessage2Standard;
using drawfx::P;

constexpr uint32_t kCtor = 0x1c033d;
constexpr uint32_t kDtor = 0x1c0388;
constexpr uint32_t kCreate = 0x1c04ea;
constexpr uint32_t kGetString = 0x1c05f4;
constexpr uint32_t kSetString = 0x1c08f2;
constexpr uint32_t kSetObject = 0x1c08cf;
constexpr uint32_t kHasExtra = 0x1c05c3;
constexpr uint32_t kGetExtra = 0x1c0582;
constexpr uint32_t kSetExtra = 0x1c08ac;
constexpr uint32_t kDeleting = 0x1c042a;
constexpr uint32_t kTagObject = 0x7e000000;
constexpr uint32_t kFaceIid = 0x1234;
constexpr uint32_t kSentinel = 0xa5a5a5a5;

using U0 = uint32_t(__thiscall *)(void *);
using U1 = uint32_t(__thiscall *)(void *, uint32_t);
using U2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

// ---- the allocator behind RZMalloc on both sides ----

GeneralAllocator *gAlloc = nullptr;
uint32_t gFrees = 0;
void *gFreed = nullptr;

void *__cdecl TestMalloc(uint32_t bytes) { return gAlloc->Malloc(bytes, 0); }

void __cdecl TestFree(void *mem)
{
    ++gFrees;
    gFreed = mem;
    gAlloc->Free(mem);
}

struct AllocScope {
    shape::CodePatch malloc_{0x1be3b0, reinterpret_cast<const void *>(&TestMalloc)};
    shape::CodePatch free_{0x1be3a1, reinterpret_cast<const void *>(&TestFree)};
    alignas(16) uint8_t memory[sizeof(GeneralAllocator)] = {};
    AllocScope()
    {
        gAlloc = reinterpret_cast<GeneralAllocator *>(memory);
        gAlloc->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        RZMemory::testGlobalAllocator = gAlloc;
    }
    AllocScope(const AllocScope &) = delete;
    ~AllocScope()
    {
        RZMemory::testGlobalAllocator = nullptr;
        difftest_alloc::ShutdownAndRelease(gAlloc);
        gAlloc = nullptr;
    }
};

// ---- fakes ----

inline constexpr drawfx::Slot kUnknownSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

// The message's own slots as the code under test calls them: 4 (GetType), 28 (HasString), 34 (HasExtra); any other
// call fails the test.
constexpr std::array<drawfx::Slot, 38> MakeSelfSlots()
{
    std::array<drawfx::Slot, 38> slots{};
    for (drawfx::Slot &s : slots) s = {0, drawfx::Ret::Int, false, "", ""};
    slots[4] = {0, drawfx::Ret::Int, true, "GetType", ""};
    slots[28] = {0, drawfx::Ret::Int, true, "HasString", ""};
    slots[34] = {0, drawfx::Ret::Int, true, "HasExtra", ""};
    return slots;
}
inline constexpr std::array<drawfx::Slot, 38> kSelfSlots = MakeSelfSlots();
inline constexpr drawfx::Iface kSelf{"cIGZMessage2Standard", 38, kSelfSlots.data()};

drawfx::Fake gObject[3], gFace, gSelf;

void ResetFakes()
{
    for (unsigned i = 0; i < 3; ++i) gObject[i].Reset<kUnknown>(0x501 + i);
    gFace.Reset<kUnknown>(0x510);
    gObject[0].AddFace(kFaceIid, gFace);
    gObject[1].AddFace(kFaceIid, gFace);
    gSelf.Reset<kSelf>(0x520);
}

void NoteRefs()
{
    drawfx::Note("refs", {uint32_t(gObject[0].refs), uint32_t(gObject[1].refs), uint32_t(gObject[2].refs),
                          uint32_t(gFace.refs)});
}

// One of null and the three object fakes.
uint32_t PickObject(std::mt19937 &rng)
{
    const uint32_t pick = rng() % 4;
    return pick ? P(&gObject[pick - 1]) : 0u;
}

// ---- the message image ----

struct Image {
    uint32_t words[Self::kSize / 4];
};

// Random words throughout; the three pointer fields null or a fake (the fakes are globals: the same on both sides).
Image RandomImage(std::mt19937 &rng)
{
    Image image;
    for (uint32_t &w : image.words) w = rng();
    image.words[0x20 / 4] = PickObject(rng);
    image.words[0x24 / 4] = PickObject(rng);
    image.words[0x28 / 4] = PickObject(rng);
    return image;
}

Self *Install(void *memory, const Image &image)
{
    std::memcpy(memory, image.words, sizeof image.words);
    drawfx::AddRange(memory, Self::kSize, kTagObject);
    return static_cast<Self *>(memory);
}

// Every word of the message, normalised; the vtable word as 1 when it is the game's.
void NoteObject(const Self *message)
{
    const auto *words = reinterpret_cast<const uint32_t *>(message);
    drawfx::Note("vtable", words[0] == P(GameAddress<const void>(Self::kVTable)) ? 1u : drawfx::Norm(words[0]));
    for (uint32_t i = 1; i < Self::kSize / 4; ++i) drawfx::Note("word", drawfx::Norm(words[i]));
}

template <class Run> bool Compare(difftest::Context &ctx, const char *what, uint32_t seed, Run &&run)
{
    auto a = drawfx::Side([&] { ResetFakes(); }, [&] { run(true); });
    auto b = drawfx::Side([&] { ResetFakes(); }, [&] { run(false); });
    return drawfx::Same(ctx, a.first, b.first, what, seed, a.second.empty() ? b.second : a.second);
}

} // namespace

// The constructor over a random image: the game's vtable, the count, the flag byte (bytes 9-11 left), every other field
// zero; the result is the object.
DIFF_TEST(RZMessage2Standard_Construct, 0x1c033d)
{
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 rng(seed);
        const Image image = RandomImage(rng);
        const bool ok = Compare(ctx, "construct", seed, [&](bool original) {
            alignas(4) uint8_t memory[Self::kSize];
            Self *m = Install(memory, image);
            void *result = original ? reinterpret_cast<void *>(ctx.Original<U0>(kCtor)(m)) : m->Construct();
            drawfx::Note("result is this", result == m ? 1u : 0u);
            NoteObject(m);
        });
        if (!ok) return;
    }
}

// The destructor: the game's vtable over a random one, then the extra, the object and the string released, in that
// order, each only when set (the fakes log the calls).
DIFF_TEST(RZMessage2Standard_Destruct, 0x1c0388)
{
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 rng(seed);
        const Image image = RandomImage(rng);
        const bool ok = Compare(ctx, "destruct", seed, [&](bool original) {
            alignas(4) uint8_t memory[Self::kSize];
            Self *m = Install(memory, image);
            if (original)
                ctx.Original<U0>(kDtor)(m);
            else
                m->Destruct();
            NoteObject(m);
            NoteRefs();
        });
        if (!ok) return;
    }
}

// The three pointer setters: the flag set for a non-null object and cleared for null (the other bits kept), the new
// object counted, the old one released (nothing when it is the same), the field's address returned.
DIFF_TEST(RZMessage2Standard_Setters, 0x1c08f2)
{
    constexpr uint32_t rvas[3] = {kSetString, kSetObject, kSetExtra};
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng(seed);
        const Image image = RandomImage(rng);
        const unsigned which = seed % 3;
        const uint32_t argument = PickObject(rng);
        const bool ok = Compare(ctx, "setter", seed, [&](bool original) {
            alignas(4) uint8_t memory[Self::kSize];
            Self *m = Install(memory, image);
            void *object = reinterpret_cast<void *>(uintptr_t(argument));
            uint32_t result = 0;
            if (original) {
                result = ctx.Original<U1>(rvas[which])(m, argument);
            } else {
                switch (which) {
                case 0: result = P(m->SetString(object)); break;
                case 1: result = P(m->SetObject(object)); break;
                default: result = P(m->SetExtra(object)); break;
                }
            }
            drawfx::Note("setter, result", {which, drawfx::Norm(result)});
            NoteObject(m);
            NoteRefs();
        });
        if (!ok) return;
    }
}

// HasExtra over every flag byte: bit 5, in al (the rest of eax is not the original's concern: callers read al).
DIFF_TEST(RZMessage2Standard_HasExtra, 0x1c05c3)
{
    auto orig = ctx.Original<U0>(kHasExtra);
    for (uint32_t flags = 0; flags < 256; ++flags) {
        Image image{};
        alignas(4) uint8_t a[Self::kSize], b[Self::kSize];
        std::memcpy(a, image.words, Self::kSize);
        std::memcpy(b, image.words, Self::kSize);
        reinterpret_cast<Self *>(a)->mFlags = uint8_t(flags);
        reinterpret_cast<Self *>(b)->mFlags = uint8_t(flags);
        const uint8_t ra = uint8_t(orig(a));
        const uint8_t rb = uint8_t(reinterpret_cast<Self *>(b)->HasExtra());
        ++ctx.cases;
        if (ra != rb || ra != ((flags >> 5) & 1)) ctx.Fail("HasExtra flags %02x: original %u, ours %u", flags, ra, rb);
        if (std::memcmp(a, b, Self::kSize) != 0) ctx.Fail("HasExtra flags %02x: object differs", flags);
    }
}

// GetString / GetExtra: the message's own slot 28 / 34 (the embedded fake, answering independently of the flags; its
// al decides, so answers with only high bits set count as false), then the held object's QueryInterface with the
// arguments, its result and out.
DIFF_TEST(RZMessage2Standard_Getters, 0x1c05f4)
{
    constexpr uint32_t answers[4] = {0, 1, 0x100, 0xff01};
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng(seed);
        Image image = RandomImage(rng);
        const bool extra = seed % 2;
        const uint32_t field = extra ? 0x28 / 4 : 0x20 / 4;
        uint32_t answer = answers[rng() % 4];
        if (!image.words[field] && (answer & 0xff)) answer &= ~0xffu; // a null object is never asked
        const uint32_t iid = rng() % 3 ? kFaceIid : 0x5678;
        const bool ok = Compare(ctx, "getter", seed, [&](bool original) {
            alignas(4) uint8_t memory[Self::kSize];
            Self *m = Install(memory, image);
            gSelf.Embed(m);
            gSelf.Returns(extra ? 34 : 28, answer);
            uint32_t out = kSentinel;
            uint32_t result = 0;
            if (original)
                result = ctx.Original<U2>(extra ? kGetExtra : kGetString)(m, iid, P(&out));
            else
                result = (extra ? m->GetExtra(iid, reinterpret_cast<void **>(&out))
                                : m->GetString(iid, reinterpret_cast<void **>(&out)))
                             ? 1u
                             : 0u;
            drawfx::Note("result, out", {result & 0xff, drawfx::Norm(out)});
            NoteObject(m);
            NoteRefs();
        });
        if (!ok) return;
    }
}

// Create: a new message (RZMalloc, the constructor) asked for the iid; answered, it takes this message's type (this
// slot 4, the embedded fake) and the result is true; refused, it is deleted (its slot 37: the original deleting
// destructor through the game's vtable, on both sides) and the result false. The new message's words are noted, then it
// is deleted the same way; the frees are counted.
DIFF_TEST(RZMessage2Standard_Create, 0x1c04ea)
{
    AllocScope alloc;
    constexpr uint32_t iids[6] = {1, 0x65297976, 0xa52294b4, 0, 0x65297977, 0x1234};
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 rng(seed);
        const Image image = RandomImage(rng);
        const uint32_t iid = rng() % 4 ? iids[rng() % 6] : uint32_t(rng());
        const uint32_t type = rng() % 3 ? rng() % 4 : uint32_t(rng());
        const bool ok = Compare(ctx, "create", seed, [&](bool original) {
            alignas(4) uint8_t memory[Self::kSize];
            Self *m = Install(memory, image);
            gSelf.Embed(m);
            gSelf.Returns(4, type);
            gFrees = 0;
            uint32_t out = kSentinel;
            uint32_t result = 0;
            if (original)
                result = ctx.Original<U2>(kCreate)(m, iid, P(&out));
            else
                result = m->Create(iid, reinterpret_cast<void **>(&out)) ? 1u : 0u;
            drawfx::Note("result, frees, out written", {result & 0xff, gFrees, out == kSentinel ? 0u : 1u});
            if (out != kSentinel && out) {
                Self *created = reinterpret_cast<Self *>(uintptr_t(out));
                NoteObject(created);
                gFrees = 0;
                gFreed = nullptr;
                ctx.Original<U1>(kDeleting)(created, 1);
                drawfx::Note("created freed", {gFrees, gFreed == created ? 1u : 0u});
            }
            NoteObject(m);
            NoteRefs();
        });
        if (!ok) return;
    }
}

// The deleting destructor over flag words 0-7 and random ones: the destructor's releases, then the block freed (our
// RZMemory::Free is patched to the counting free too) for flag 1 without flag 4; the result is the object. A block that
// stays is noted and freed afterwards.
DIFF_TEST(RZMessage2Standard_DeletingDestructor, 0x1c042a)
{
    AllocScope alloc;
    shape::CodePatch ourFree(reinterpret_cast<void *>(&RZMemory::Free), reinterpret_cast<const void *>(&TestFree));
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 rng(seed);
        const Image image = RandomImage(rng);
        const uint32_t flags = seed % 3 ? seed % 8 : uint32_t(rng());
        const bool ok = Compare(ctx, "deleting destructor", seed, [&](bool original) {
            Self *m = Install(TestMalloc(Self::kSize), image);
            gFrees = 0;
            gFreed = nullptr;
            void *result = original ? reinterpret_cast<void *>(uintptr_t(ctx.Original<U1>(kDeleting)(m, flags)))
                                    : m->DeletingDestructor(flags);
            const bool freed = gFrees != 0;
            drawfx::Note("result is this, frees, freed this", {result == m ? 1u : 0u, gFrees, gFreed == m ? 1u : 0u});
            if (!freed) {
                NoteObject(m);
                gAlloc->Free(m);
            }
            NoteRefs();
        });
        if (!ok) return;
    }
}
