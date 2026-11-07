// Differential tests for nTSSG::cTSSGSystem::LoadDataFile (slot 84) and CheckDataFile (slot 85),
// src/sg/cTSSGSystemFiles.cpp. Hook shape: the original runs on the same object image as ours.
//
// The reader class nobody has replaced is stood in over the image (constructor 0x3a6a30, destructor 0x3a6b10, open
// 0x3a8690, the header test 0x3a8660, the steps 0x3a8720 / 0x3a92c0 / 0x3a87f0 and the four list fills); the stand-ins
// log the reader's address (a stack address normalises to one value), the path's cRZString (vtable, text, count) and
// append records to the lists the way the game does (nodes of 0xcc / 0xec bytes whose record owns a vector of 76-byte
// entries at +0xa4, 0..70 entries: past 4 KB the vector's block is the aligned kind). Slot 84 drops both lists: the
// frees are compared through difftest::FreeRecorder (count and sorted sizes). The object's slot 41 goes to a stand-in
// table (null or a recording fake whose slot 8 is logged).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cTSSGSystem.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

enum : uint32_t { evStand = 0x81, evSelf = 0x82 };
enum : unsigned { fTarget = 0 };

struct Draw {
    uint32_t opens, current; // the open and header answers (al)
    bool target;             // slot 41's object
    unsigned nodes[4];       // records each fill appends
    unsigned entries[4];     // their vectors' entries
};
Draw gD;

using Entries = std::vector<std::array<uint8_t, 76>>;
struct Node {
    Node *next, *prev;
};
struct List {
    Node *head;
    uint32_t size;
};

void Append(List *list, size_t nodeSize, unsigned count, unsigned entries)
{
    for (unsigned i = 0; i < count; ++i) {
        auto *node = static_cast<Node *>(::operator new(nodeSize));
        std::memset(node, 0x3c, nodeSize);
        ::new (reinterpret_cast<uint8_t *>(node) + 8 + 0xa4) Entries(entries + i);
        node->next = list->head;
        node->prev = list->head->prev;
        list->head->prev->next = node;
        list->head->prev = node;
        ++list->size;
    }
}

void *__fastcall StandConstruct(void *reader, void *)
{
    Ev(evStand), Ev(0x3a6a30), Ev(Norm(P(reader)));
    std::memset(reader, 0x5a, 16);
    return reader;
}
void __fastcall StandDestruct(void *reader, void *) { Ev(evStand), Ev(0x3a6b10), Ev(Norm(P(reader))), Ev(*static_cast<uint8_t *>(reader)); }
uint32_t __fastcall StandOpen(void *reader, void *, const uint32_t *path, uint32_t flag)
{
    Ev(evStand), Ev(0x3a8690), Ev(Norm(P(reader))), Ev(path[0]), Ev(flag);
    const auto *text = reinterpret_cast<const std::string *>(path + 1);
    for (char c : *text) Ev(uint8_t(c));
    Ev(path[7]);
    return 0xabcd0000u | gD.opens;
}
uint32_t __fastcall StandCurrent(void *reader, void *)
{
    Ev(evStand), Ev(0x3a8660), Ev(Norm(P(reader)));
    return 0xabcd0000u | gD.current;
}
template <uint32_t kRva> void __fastcall StandStep(void *reader, void *) { Ev(evStand), Ev(kRva), Ev(Norm(P(reader))); }
template <uint32_t kRva, unsigned kFill, size_t kNodeSize> void __fastcall StandFill(void *reader, void *, List *list)
{
    Ev(evStand), Ev(kRva), Ev(Norm(P(reader))), Ev(Norm(P(list))), Ev(list->size);
    Append(list, kNodeSize, gD.nodes[kFill], gD.entries[kFill]);
}

void *gSelfVTable[100];
void *__fastcall Self41(void *self, void *)
{
    Ev(evSelf), Ev(41), Ev(Norm(P(self)));
    return gD.target ? &F(fTarget) : nullptr;
}

struct Fixture {
    shape::CodePatch construct{GameAddress<void>(0x3a6a30), shape::CodeOf(&StandConstruct)};
    shape::CodePatch destruct{GameAddress<void>(0x3a6b10), shape::CodeOf(&StandDestruct)};
    shape::CodePatch open{GameAddress<void>(0x3a8690), shape::CodeOf(&StandOpen)};
    shape::CodePatch current{GameAddress<void>(0x3a8660), shape::CodeOf(&StandCurrent)};
    shape::CodePatch step1{GameAddress<void>(0x3a8720), shape::CodeOf(&StandStep<0x3a8720>)};
    shape::CodePatch step2{GameAddress<void>(0x3a92c0), shape::CodeOf(&StandStep<0x3a92c0>)};
    shape::CodePatch step3{GameAddress<void>(0x3a87f0), shape::CodeOf(&StandStep<0x3a87f0>)};
    shape::CodePatch fill1{GameAddress<void>(0x3a90b0), shape::CodeOf(&StandFill<0x3a90b0, 0, 0xcc>)};
    shape::CodePatch fill2{GameAddress<void>(0x3a7220), shape::CodeOf(&StandFill<0x3a7220, 1, 0xcc>)};
    shape::CodePatch fill3{GameAddress<void>(0x3a87c0), shape::CodeOf(&StandFill<0x3a87c0, 2, 0xec>)};
    shape::CodePatch fill4{GameAddress<void>(0x3a71e0), shape::CodeOf(&StandFill<0x3a71e0, 3, 0xec>)};
    Fixture() { gSelfVTable[41] = reinterpret_cast<void *>(&Self41); }
};

alignas(16) uint8_t gObject[0x3b8];

void DrawCase(std::mt19937 &gen)
{
    gD = Draw{};
    gD.opens = gen() % 4 == 0 ? 0 : gen() % 2 ? 1 : 0x100;
    gD.current = gen() % 2 ? 0 : gen() % 2 ? 1 : 0x100;
    gD.target = gen() % 2;
    for (unsigned i = 0; i < 4; ++i) {
        gD.nodes[i] = gen() % 4;
        gD.entries[i] = gen() % 5 == 0 ? 52 + gen() % 18 : gen() % 4; // 54 entries of 76 bytes pass 4 KB
    }
}

const char *const kPaths[] = {"", "a", "some\\path\\file.dat", "0123456789abcdef-longer-than-sixteen"}; // test text

template <class Run> void Compare(difftest::Context &ctx, const char *what, uint32_t seed, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        F(fTarget).Set(8, 0);
        std::memset(gObject, 0x11, sizeof(gObject));
        void *vt = gSelfVTable;
        std::memcpy(gObject, &vt, 4);
        AddRange(gObject, sizeof(gObject), 0x5e000000);
        ctx.Mark(side ? "ours" : "original");
        gEv.reserve(1 << 16); // the log must not reallocate (and free) while frees are recorded
        difftest::FreeRecorder frees;
        const uint32_t result = run(side == 0);
        frees.Stop();
        Blob b = gEv;
        b.push_back(result);
        b.push_back(uint32_t(frees.Count()));
        size_t sizes[512];
        const size_t n = frees.SortedSizes(sizes, 512);
        for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
        b.push_back(frees.Overflowed());
        CaptureFakes(b);
        outcome[side] = b;
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x24ad90 LoadDataFile: the file opens or not (al only), slot 41's object or none, 0..3 records from each fill with
// 0..3 or 52..69 entries; four paths (empty, short, and past the 15-character inline buffer).
DIFF_TEST(sg_tssg_system_load_data_file, 0x24ad90)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, const char *)>(0x24ad90);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24ad90u);
        DrawCase(gen);
        const char *path = kPaths[gen() % 4];
        Compare(ctx, "load_data_file", seed, [&](bool useOriginal) {
            if (useOriginal) original(gObject, path);
            else Ours()->LoadDataFile(path);
            return 0u;
        });
    }
}

// 0x24b2d0 CheckDataFile: the file opens or not, its header current or not (al only); the four paths.
DIFF_TEST(sg_tssg_system_check_data_file, 0x24b2d0)
{
    Fixture fixture;
    auto original = ctx.Original<int32_t(__thiscall *)(void *, const char *)>(0x24b2d0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24b2d0u);
        DrawCase(gen);
        const char *path = kPaths[gen() % 4];
        Compare(ctx, "check_data_file", seed, [&](bool useOriginal) {
            return uint32_t(useOriginal ? original(gObject, path) : Ours()->CheckDataFile(path));
        });
    }
}
