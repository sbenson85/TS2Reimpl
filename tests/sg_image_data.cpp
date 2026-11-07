// Differential tests for nGZSceneGraph::cImageData accessors (src/sg/cImageData.cpp).
//
// The object, its entry array, the block tables and the elements all live in one global fixture. Each run starts from
// the same bytes: the original runs first, its results and the whole fixture are recorded, the fixture is restored,
// and the replacement runs on the same starting bytes. Pointers stored in the fixture point into the fixture itself, so
// they are equal on both sides. Compared per run: the return value (full eax, st0 or al as the signature says) and the
// fixture's bytes afterwards (so writes to entries, values and floats show up).
#include "difftest.h"
#include "sg/cImageData.h"

#include <cstring>
#include <iterator>
#include <memory>
#include <random>
#include <string>

namespace {

using cImageData = nGZSceneGraph::cImageData;

struct Fixture {
    uint8_t object[0x80];         // the cImageData; `this` is object
    uint8_t sub[0x10];            // the subobject that +0x20 points to (dword at +4 is a displacement)
    uint8_t entries[3 * 40];      // three 40-byte entries; begin at object + 0x38
    void *table[4];               // block table pointers (entry + 4 points here)
    void *blocks[4][4];           // each block: four element pointers
    uint8_t elems[16][0x50];      // elements, each with dwords at +0x2c, +0x30, +0x34, +0x48 among random bytes
};

Fixture gFix, gStart, gAfter;

void *ObjectPtr()
{
    return gFix.object;
}

void Put32(uint8_t *at, uint32_t value)
{
    std::memcpy(at, &value, sizeof(value));
}

// Random fixture state. Pointers are set up so that every lookup lands inside the fixture; the counts, first indices,
// mask sizes, the current index and the begin/end pair are randomised within (and sometimes beyond) their ranges.
void Build(std::mt19937 &gen)
{
    uint8_t *raw = reinterpret_cast<uint8_t *>(&gFix);
    for (size_t i = 0; i < sizeof(Fixture); ++i) raw[i] = static_cast<uint8_t>(gen());

    // Subobject displacement at +4 (small values and random ones).
    Put32(gFix.sub + 4, (gen() & 1) ? static_cast<uint32_t>(static_cast<int32_t>(gen() % 0x400) - 0x200) : gen());
    void *subPtr = gFix.sub;
    std::memcpy(gFix.object + 0x20, &subPtr, sizeof(subPtr));

    // Block table: four blocks, each with four random element pointers.
    for (int j = 0; j < 4; ++j) {
        gFix.table[j] = gFix.blocks[j];
        for (int i = 0; i < 4; ++i) gFix.blocks[j][i] = gFix.elems[gen() % 16];
    }
    // Elements: the dwords read by the accessors are zero for about a third of the elements, so the flag and sum tests
    // see both outcomes (random bytes are almost never zero).
    for (int e = 0; e < 16; ++e) {
        if (gen() % 3 == 0) Put32(gFix.elems[e] + 0x48, 0);
        if (gen() % 3 == 0) Put32(gFix.elems[e] + 0x44, 0);
    }

    const uint32_t blockCounts[] = {1, 2, 4};
    void *tablePtr = gFix.table;
    for (int e = 0; e < 3; ++e) {
        uint8_t *entry = gFix.entries + e * 40;
        std::memcpy(entry + 4, &tablePtr, sizeof(tablePtr));
        Put32(entry + 8, blockCounts[gen() % 3]);
        Put32(entry + 0xc, gen() % 3 == 0 ? gen() : gen() % 40);
        Put32(entry + 0x10, gen() % 6);
    }

    uint8_t *begin = gFix.entries;
    uint32_t endValue;
    switch (gen() % 4) {
    case 0: endValue = reinterpret_cast<uintptr_t>(begin); break; // empty
    case 1: endValue = reinterpret_cast<uintptr_t>(begin) + 40u * (gen() % 3); break;
    case 2: endValue = reinterpret_cast<uintptr_t>(begin) + static_cast<uint32_t>(gen()); break; // not a multiple of 40
    default: endValue = reinterpret_cast<uintptr_t>(begin) + 3 * 40; break;
    }
    uint32_t beginValue = reinterpret_cast<uintptr_t>(begin);
    Put32(gFix.object + 0x38, beginValue);
    Put32(gFix.object + 0x3c, endValue);
    Put32(gFix.object + 0x44, gen() % 3);

    // String-shaped buffer at +0x4c: size at +0x5c, capacity at +0x60, data pointer at +0x4c.
    const uint32_t sizes[] = {0, 1, 5, 15, 16, 40};
    const uint32_t caps[] = {0, 7, 15, 16, 31, 0x3fffffff};
    Put32(gFix.object + 0x5c, sizes[gen() % 6]);
    Put32(gFix.object + 0x60, caps[gen() % 6]);
    void *dataPtr = gFix.elems[0];
    if (gen() & 1) std::memcpy(gFix.object + 0x4c, &dataPtr, sizeof(dataPtr));
}

void FloatBits(float f, uint64_t &out)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    out = bits;
}

// Runs the original and the replacement on the same fixture bytes and compares the packed result and the fixture.
template <typename Original, typename Ours> void Run(difftest::Context &ctx, const char *name, uint32_t iteration, Original original, Ours ours)
{
    gStart = gFix;
    const uint64_t a = original();
    gAfter = gFix;
    gFix = gStart;
    const uint64_t b = ours();
    ++ctx.cases;
    if (a != b || std::memcmp(&gAfter, &gFix, sizeof(Fixture)) != 0) {
        ctx.Fail("%s iteration %u: original %llx, ours %llx, fixture %s", name, iteration, static_cast<unsigned long long>(a),
                 static_cast<unsigned long long>(b), std::memcmp(&gAfter, &gFix, sizeof(Fixture)) ? "differs" : "same");
    }
}

uint64_t Packed(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

} // namespace

// 0xab0330 (19 bytes): this + 0x20 + the displacement at +4 of the subobject at this + 0x20. Null `this` gives null.
DIFF_TEST(sg_image_data_subobject_by_displacement, 0xab0330)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0xab0330);
    std::mt19937 gen(0x1001);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        Run(ctx, "SubobjectByDisplacement", i, [&] { return Packed(original(ObjectPtr())); },
            [&] { return Packed(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::GetResource()); });
    }
    // Null `this`: the original returns null without reading anything.
    Run(ctx, "SubobjectByDisplacement null", 0, [&] { return Packed(original(nullptr)); },
        [&] { return Packed(reinterpret_cast<cImageData *>(nullptr)->cImageData::GetResource()); });
}

// 0xab03b0 (14 bytes): the float at the current entry's +0x24, returned in st0.
DIFF_TEST(sg_image_data_current_entry_float, 0xab03b0)
{
    auto original = ctx.Original<float(__thiscall *)(void *)>(0xab03b0);
    std::mt19937 gen(0x1002);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        Run(ctx, "CurrentEntryFloat", i, [&] { uint64_t v; FloatBits(original(ObjectPtr()), v); return v; },
            [&] { uint64_t v; FloatBits(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::GetEntryScale(), v); return v; });
    }
}

// 0xab3340 (24 bytes, `ret 4`): stores the float argument at the current entry's +0x24.
DIFF_TEST(sg_image_data_set_current_entry_float, 0xab3340)
{
    auto original = ctx.Original<void(__thiscall *)(void *, float)>(0xab3340);
    std::mt19937 gen(0x1003);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        uint32_t bits = i < 4 ? (i == 0 ? 0x7fc00001u : i == 1 ? 0xffffffffu : i == 2 ? 0u : 0x80000000u) : gen();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        Run(ctx, "SetCurrentEntryFloat", i, [&] { original(ObjectPtr(), value); return uint64_t(0); },
            [&] { reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::SetEntryScale(value); return uint64_t(0); });
    }
}

// 0xab1870 (21 bytes): the character data at +0x4c, or null when the size at +0x5c is zero (inline buffer when the
// capacity is 15 or less).
DIFF_TEST(sg_image_data_name_data, 0xab1870)
{
    auto original = ctx.Original<const char *(__thiscall *)(void *)>(0xab1870);
    std::mt19937 gen(0x1004);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        Run(ctx, "NameData", i, [&] { return Packed(original(ObjectPtr())); },
            [&] { return Packed(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::GetNameText()); });
    }
}

// 0xab3360 (40 bytes, `ret 4`): std::string::assign(s, strlen(s)) on the buffer at +0x4c. Each side gets its own empty
// string object (a fresh one per side, since assign can reallocate and free), and the two results are compared by
// size, capacity and contents.
DIFF_TEST(sg_image_data_set_name, 0xab3360)
{
    auto original = ctx.Original<void(__thiscall *)(void *, const char *)>(0xab3360);
    const std::string names[] = {"", "a", "tex", std::string(15, 'x'), std::string(16, 'y'), std::string(31, 'z'),
                                 std::string(64, 'w')};
    uint32_t i = 0;
    for (const std::string &name : names) {
        alignas(16) uint8_t objA[0x80];
        alignas(16) uint8_t objB[0x80];
        std::memset(objA, 0, sizeof(objA));
        std::memset(objB, 0, sizeof(objB));
        new (objA + 0x4c) std::string();
        new (objB + 0x4c) std::string();

        const char *text = name.c_str();
        original(objA, text);
        auto &sa = *reinterpret_cast<std::string *>(objA + 0x4c);
        cImageData *self = reinterpret_cast<cImageData *>(objB);
        self->cImageData::SetName(text);
        auto &sb = *reinterpret_cast<std::string *>(objB + 0x4c);
        ++ctx.cases;
        if (sa.size() != sb.size() || sa.capacity() != sb.capacity() || sa != sb || sa.size() != std::strlen(text))
            ctx.Fail("SetName case %u: original size %zu cap %zu, ours size %zu cap %zu", i, sa.size(), sa.capacity(), sb.size(),
                     sb.capacity());
        std::destroy_at(&sa);
        std::destroy_at(&sb);
        ++i;
    }
}

// 0xab1c20 (22 bytes): the element count of the current entry, or 0 for an empty array.
DIFF_TEST(sg_image_data_current_entry_count, 0xab1c20)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0xab1c20);
    std::mt19937 gen(0x1005);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        Run(ctx, "CurrentEntryCount", i, [&] { return uint64_t(original(ObjectPtr())); },
            [&] { return uint64_t(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelCount()); });
    }
}

// 0xab1c40 (24 bytes): (end - begin) / 40 with the reciprocal multiply. Ends that are not multiples of 40 and ends below
// begin (negative differences) are included, since the sequence is compared on its own results.
DIFF_TEST(sg_image_data_entry_count, 0xab1c40)
{
    auto original = ctx.Original<int32_t(__thiscall *)(void *)>(0xab1c40);
    std::mt19937 gen(0x1006);
    const uint32_t edges[] = {0u, 39u, 40u, 79u, 80u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0xfffffff0u};
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        const uint32_t begin = reinterpret_cast<uintptr_t>(gFix.entries);
        const uint32_t delta = i < std::size(edges) ? edges[i] : gen();
        Put32(gFix.object + 0x3c, begin + delta);
        Run(ctx, "EntryCount", i, [&] { return uint64_t(static_cast<uint32_t>(original(ObjectPtr()))); },
            [&] { return uint64_t(static_cast<uint32_t>(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::EntryCount())); });
    }
}

// 0xab32d0 (24 bytes, `ret 4`): copies 16 bytes from the current entry's +0x14 to the argument
// and returns the argument.
DIFF_TEST(sg_image_data_get_current_entry_value, 0xab32d0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *, void *)>(0xab32d0);
    std::mt19937 gen(0x1007);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        uint8_t outA[16], outB[16];
        for (auto &b : outA) b = static_cast<uint8_t>(gen());
        std::memcpy(outB, outA, sizeof(outA));
        Run(ctx, "GetCurrentEntryValue", i, [&] { return uint64_t(original(ObjectPtr(), outA) == outA); },
            [&] {
                float *const out = reinterpret_cast<float *>(outB);
                return uint64_t(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::GetEntryColour(out) == out);
            });
        if (std::memcmp(outA, outB, sizeof(outA)) != 0) ctx.Fail("GetCurrentEntryValue iteration %u: output differs", i);
    }
}

// 0xab3950 (26 bytes, `ret 4`): copies 16 bytes from the argument to the current entry's +0x14.
DIFF_TEST(sg_image_data_set_current_entry_value, 0xab3950)
{
    auto original = ctx.Original<void(__thiscall *)(void *, const void *)>(0xab3950);
    std::mt19937 gen(0x1008);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        uint8_t in[16];
        for (auto &b : in) b = static_cast<uint8_t>(gen());
        Run(ctx, "SetCurrentEntryValue", i, [&] { original(ObjectPtr(), in); return uint64_t(0); },
            [&] { reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::SetEntryColour(reinterpret_cast<const float *>(in)); return uint64_t(0); });
    }
}

// 0xab3ae0 / 0xab1830 / 0xab1c60 (51 bytes each, `ret 4`): dwords +0x2c, +0x30 and +0x34 of element (first + k) of the
// current entry. The block table has four entries and the mask is at most 3 for these fixtures, so every k is inside.
DIFF_TEST(sg_image_data_element_field_2c, 0xab3ae0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab3ae0);
    std::mt19937 gen(0x1009);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        const uint32_t k = gen() % 64;
        Run(ctx, "ElementField2C", i, [&] { return uint64_t(original(ObjectPtr(), k)); },
            [&] { return uint64_t(static_cast<uint32_t>(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelWidth(k))); });
    }
}

DIFF_TEST(sg_image_data_element_field_30, 0xab1830)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab1830);
    std::mt19937 gen(0x100a);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        const uint32_t k = gen() % 64;
        Run(ctx, "ElementField30", i, [&] { return uint64_t(original(ObjectPtr(), k)); },
            [&] { return uint64_t(static_cast<uint32_t>(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelHeight(k))); });
    }
}

DIFF_TEST(sg_image_data_element_field_34, 0xab1c60)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab1c60);
    std::mt19937 gen(0x100b);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        const uint32_t k = gen() % 64;
        Run(ctx, "ElementField34", i, [&] { return uint64_t(original(ObjectPtr(), k)); },
            [&] { return uint64_t(static_cast<uint32_t>(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelPitch(k))); });
    }
}

// 0xab1460 (107 bytes): the sum of the dword +0x48 over the current entry's elements.
DIFF_TEST(sg_image_data_sum_element_field_48, 0xab1460)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0xab1460);
    std::mt19937 gen(0x100c);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        Run(ctx, "SumElementField48", i, [&] { return uint64_t(original(ObjectPtr())); },
            [&] { return uint64_t(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::TotalBytes()); });
    }
}

// 0xab1b90 (68 bytes, `ret 4`): the dword +0x48 of element (first + k), or 0 when there are no entries or no elements.
DIFF_TEST(sg_image_data_element_field_48_at, 0xab1b90)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab1b90);
    std::mt19937 gen(0x100d);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        const uint32_t k = gen() % 64;
        Run(ctx, "ElementField48", i, [&] { return uint64_t(original(ObjectPtr(), k)); },
            [&] { return uint64_t(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelBytes(k)); });
    }
}

// 0xab1b40 (69 bytes): al = 1 when the entries are non-empty, the current entry has elements and its first element is
// non-null with a non-zero dword at +0x48. Only al is compared: the rest of eax is not part of the result.
DIFF_TEST(sg_image_data_has_first_element_flag, 0xab1b40)
{
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(0xab1b40);
    std::mt19937 gen(0x100e);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        // A quarter of the cases null the current entry's first element, the path that answers 0 before the +0x48 read.
        if (gen() % 4 == 0) {
            const uint8_t *entry = gFix.entries + *reinterpret_cast<const uint32_t *>(gFix.object + 0x44) * 40;
            const uint32_t first = *reinterpret_cast<const uint32_t *>(entry + 0xc);
            const uint32_t mask = *reinterpret_cast<const uint32_t *>(entry + 8) - 1;
            gFix.blocks[(first >> 2) & mask][first & 3] = nullptr;
        }
        Run(ctx, "HasFirstElementFlag", i, [&] { return uint64_t(original(ObjectPtr())); },
            [&] { return uint64_t(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::HasPixels()); });
    }
}

namespace {

// Fakes for the calls the element-based accessors make through the element's and its subobject's vtables. The fake
// slots are __thiscall member functions (the same convention as the game's slots) and append to gCalls, so the test
// compares the calls made (object, argument, slot) as well as the results.
struct CallLog {
    struct Entry {
        uintptr_t self;
        uint32_t arg;
        uint32_t slot;
    };
    uint32_t count;
    Entry calls[8];
};

CallLog gCalls;

void LogCall(const void *self, uint32_t arg, uint32_t slot)
{
    if (gCalls.count < 8) gCalls.calls[gCalls.count] = {reinterpret_cast<uintptr_t>(self), arg, slot};
    ++gCalls.count;
}

struct Fake {
    uint32_t Slot14()
    {
        LogCall(this, 0, 14);
        return 0x14000000u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    }
    uint32_t Slot15(uint8_t arg) // the level's SetFlag15 takes a byte
    {
        LogCall(this, arg, 15);
        return 0x15000000u + arg;
    }
    void Release() { LogCall(this, 0, 2); }
};

template <typename PM> uintptr_t AddressOf(PM pm)
{
    static_assert(sizeof(PM) == sizeof(uintptr_t), "single-inheritance member pointer is a plain address");
    uintptr_t address;
    std::memcpy(&address, &pm, sizeof(address));
    return address;
}

uintptr_t gElementVtable[16];   // slots 14 and 15 are used by the element accessors
uintptr_t gSubobjectVtable[3];  // slot 2 (Release) is used by ReleaseFrontElements
uint32_t gVbTable[16][2];       // per element: vbtable whose dword at +4 is the displacement

void InitFakes()
{
    gElementVtable[14] = AddressOf(&Fake::Slot14);
    gElementVtable[15] = AddressOf(&Fake::Slot15);
    gSubobjectVtable[2] = AddressOf(&Fake::Release);
}

void PutPointer(uint8_t *at, const void *p)
{
    const uintptr_t value = reinterpret_cast<uintptr_t>(p);
    std::memcpy(at, &value, sizeof(value));
}

// Every element gets the fake element vtable (its first dword), so the slot calls land in Fake.
void SetupElementVtables()
{
    for (int e = 0; e < 16; ++e) PutPointer(gFix.elems[e], &gElementVtable[0]);
}

// Every element gets a virtual base at +0x1c whose displacement (in its vbtable at +4) puts the subobject at element +0x30,
// where the fake subobject vtable pointer is written.
void SetupElementVirtualBases()
{
    for (int e = 0; e < 16; ++e) {
        gVbTable[e][1] = 0x14;
        PutPointer(gFix.elems[e] + 0x1c, gVbTable[e]);
        PutPointer(gFix.elems[e] + 0x30, &gSubobjectVtable[0]);
    }
}

// Like Run, but also compares the calls the fakes logged on each side.
template <typename Original, typename Ours>
void RunWithCalls(difftest::Context &ctx, const char *name, uint32_t iteration, Original original, Ours ours)
{
    gStart = gFix;
    gCalls = CallLog{};
    const uint64_t a = original();
    gAfter = gFix;
    const CallLog callsA = gCalls;
    gFix = gStart;
    gCalls = CallLog{};
    const uint64_t b = ours();
    const CallLog callsB = gCalls;
    ++ctx.cases;
    const bool fixtureSame = std::memcmp(&gAfter, &gFix, sizeof(Fixture)) == 0;
    const bool callsSame = std::memcmp(&callsA, &callsB, sizeof(CallLog)) == 0;
    if (a != b || !fixtureSame || !callsSame)
        ctx.Fail("%s iteration %u: original %llx, ours %llx, fixture %s, calls %s (original %u, ours %u)", name, iteration,
                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(b), fixtureSame ? "same" : "differs",
                 callsSame ? "same" : "differ", callsA.count, callsB.count);
}

// A scratch arena for the subobject thunks (their `this` is a subobject with the displacement at this - 4).
struct Arena {
    alignas(16) uint8_t bytes[0x400];
};

Arena gArena, gArenaStart, gArenaAfter;

template <typename Original, typename Ours> void RunArena(difftest::Context &ctx, const char *name, uint32_t iteration, Original original, Ours ours)
{
    gArenaStart = gArena;
    const uint64_t a = original();
    gArenaAfter = gArena;
    gArena = gArenaStart;
    const uint64_t b = ours();
    ++ctx.cases;
    if (a != b || std::memcmp(&gArenaAfter, &gArena, sizeof(Arena)) != 0)
        ctx.Fail("%s iteration %u: original %llx, ours %llx", name, iteration, static_cast<unsigned long long>(a),
                 static_cast<unsigned long long>(b));
}

} // namespace

// 0xab2030 (51 bytes, `ret 4`; slots 8 and 9): dword +0x44 of element (first + k) of the current entry.
DIFF_TEST(sg_image_data_element_field_44, 0xab2030)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab2030);
    std::mt19937 gen(0x100f);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        const uint32_t k = gen() % 64;
        Run(ctx, "ElementField44", i, [&] { return uint64_t(original(ObjectPtr(), k)); },
            [&] { return Packed(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelData(k)); });
    }
}

// 0xab0370 (53 bytes, `ret 4`): calls slot 14 of element (first + k) with no arguments and returns its eax. The elements
// carry the fake vtable, so the calls and the results are compared as well.
DIFF_TEST(sg_image_data_call_element_slot_14, 0xab0370)
{
    InitFakes();
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab0370);
    std::mt19937 gen(0x1010);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        SetupElementVtables();
        const uint32_t k = gen() % 6;
        RunWithCalls(ctx, "CallElementSlot14", i, [&] { return uint64_t(original(ObjectPtr(), k)); },
                     [&] { return Packed(reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::LevelResource(k)); });
    }
}

// 0xab3300 (57 bytes, `ret 8`): calls slot 15 of element (first + a) with the byte b. Void: the calls are compared.
DIFF_TEST(sg_image_data_call_element_slot_15, 0xab3300)
{
    InitFakes();
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint8_t)>(0xab3300);
    std::mt19937 gen(0x1011);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        SetupElementVtables();
        const uint32_t a = gen() % 6;
        const uint8_t b = static_cast<uint8_t>(i < 2 ? (i == 0 ? 0u : 0xffu) : gen());
        RunWithCalls(ctx, "SetLevelFlag", i, [&] { original(ObjectPtr(), a, b); return uint64_t(0); },
                     [&] { reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::SetLevelFlag(a, b); return uint64_t(0); });
    }
}

// 0xab14d0 (172 bytes, `ret 4`, a cleanup-only EH frame): releases up to n elements from the front of the current entry.
// Count 0 with n != 0 is not tested: the original then loops for about four billion iterations (ORIGINAL-BUG in the .cpp).
DIFF_TEST(sg_image_data_release_front_elements, 0xab14d0)
{
    InitFakes();
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xab14d0);
    std::mt19937 gen(0x1012);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        SetupElementVirtualBases();
        // A quarter of the cases null the entry's first element, the path that skips the release.
        const uint8_t *entry = gFix.entries + *reinterpret_cast<const uint32_t *>(gFix.object + 0x44) * 40;
        const uint32_t first = *reinterpret_cast<const uint32_t *>(entry + 0xc);
        const uint32_t mask = *reinterpret_cast<const uint32_t *>(entry + 8) - 1;
        if (gen() % 4 == 0) gFix.blocks[(first >> 2) & mask][first & 3] = nullptr;

        const uint32_t count = *reinterpret_cast<const uint32_t *>(entry + 0x10);
        uint32_t n = gen() % 6;
        if (gen() % 5 == 0) n = 0;
        if (count == 0) n = 0; // ORIGINAL-BUG path, see the test comment
        RunWithCalls(ctx, "ReleaseFrontElements", i, [&] { original(ObjectPtr(), n); return uint64_t(0); },
                     [&] { reinterpret_cast<cImageData *>(ObjectPtr())->cImageData::DropTopLevels(n); return uint64_t(0); });
    }
}

// 0xab32f3 (11 bytes, thunk: sub ecx,[ecx-4]; sub ecx,0x38; jmp 0xa3c750): the cSGSecondary virtual base, when the object
// (secondary - 0x30) is not null. Covers a null object (secondary = 0x30, which must not read the vbptr) and secondaries
// in the arena, whose vbptr points to a table in the arena.
DIFF_TEST(sg_image_data_subobject_virtual_base_if_object, 0xab32f3)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0xab32f3);
    std::mt19937 gen(0x1013);
    for (uint32_t i = 0; i < 2000; ++i) {
        uint8_t *raw = gArena.bytes;
        for (size_t j = 0; j < sizeof(gArena.bytes); ++j) raw[j] = static_cast<uint8_t>(gen());
        const uintptr_t sub = reinterpret_cast<uintptr_t>(raw + 0x200);
        const uintptr_t secondary = gen() % 4 == 0 ? 0x30u : reinterpret_cast<uintptr_t>(raw) + 0x40 + gen() % 0x140;
        Put32(raw + 0x200 - 4, static_cast<uint32_t>(sub - 0x38 - secondary));
        if (secondary != 0x30) {
            PutPointer(reinterpret_cast<uint8_t *>(secondary - 0x14), raw + 0x300);
            Put32(raw + 0x304, gen());
        }
        RunArena(ctx, "VirtualBaseIfObject", i, [&] { return Packed(original(reinterpret_cast<void *>(sub))); },
                 [&] { return Packed(reinterpret_cast<nGZSceneGraph::cImageDataSubobject *>(sub)->VirtualBaseIfObject()); });
    }
}

// 0xab1bf6 and 0xab1bd4 (14 bytes each, thunks that adjust this and return a constant): slots 18 and 19 of the +0x6c
// subobject. Their results are image addresses, so they match only if both sides point into the same mapped image.
DIFF_TEST(sg_image_data_subobject_constant_slot_18, 0xab1bf6)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0xab1bf6);
    std::mt19937 gen(0x1014);
    for (uint32_t i = 0; i < 200; ++i) {
        uint8_t *raw = gArena.bytes;
        for (size_t j = 0; j < sizeof(gArena.bytes); ++j) raw[j] = static_cast<uint8_t>(gen());
        const uintptr_t sub = reinterpret_cast<uintptr_t>(raw + 0x200);
        RunArena(ctx, "ConstantSlot18", i, [&] { return Packed(original(reinterpret_cast<void *>(sub))); },
                 [&] { return Packed(reinterpret_cast<nGZSceneGraph::cImageDataSubobject *>(sub)->ConstantSlot18()); });
    }
}

DIFF_TEST(sg_image_data_subobject_constant_slot_19, 0xab1bd4)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0xab1bd4);
    std::mt19937 gen(0x1015);
    for (uint32_t i = 0; i < 200; ++i) {
        uint8_t *raw = gArena.bytes;
        for (size_t j = 0; j < sizeof(gArena.bytes); ++j) raw[j] = static_cast<uint8_t>(gen());
        const uintptr_t sub = reinterpret_cast<uintptr_t>(raw + 0x200);
        RunArena(ctx, "ConstantSlot19", i, [&] { return Packed(original(reinterpret_cast<void *>(sub))); },
                 [&] { return Packed(reinterpret_cast<nGZSceneGraph::cImageDataSubobject *>(sub)->ConstantSlot19()); });
    }
}
