// Differential tests for cTSWantNode (lane IL, src/ts/cTSWantNode.h).
//
// WantNode_Ops: a small forest of game-built nodes (the image's operator new 0x824632, memory filled with 0xcd, the
// constructor 0x5b2750: vtable 0xdd86d8, so every slot runs the original) against ours (`new` on odd seeds, our constructor
// adapter on the same kind of memory on even ones). Each case runs a random sequence through the vtable: SetProperties
// (slot 8) from a cGZPropertySetString holding random subsets of the 15 names the node reads, mostly in the type it reads them
// as, sometimes another; SetId, SetParent (any node or none), AddChild (only to a later node, so the children never form a
// cycle: Detach recurses into children), RemoveChild (a child or not), Detach; the direct field writes the tree loader makes
// (weights, superZero, the three flag maps, the four ranges, the person data index); AddRef / Release; QueryInterface; a
// node with a parent and children released to destruction; and the getters (slots 9-36 over their key ranges) and Matches
// (slot 37) against a fake want instance. GetDefinition asks a fake app singleton (cell 0x10cc288) for its want manager
// (slot 4), whose slot 23 answers a definition fake (name, score, influence, object type, integer operator) or nothing;
// Matches' object subjects go through the singleton's slot 24 to object and data fakes. Compared: every field after each step
// (the padding bytes +0x2f and +0x5f left out: the constructor writes the fields around them, 0x5b2750, and nothing writes
// them), the flag maps, the children (by node) and parent, every slot's result, every fake call, the definition name's count,
// and the counts left after the forest is detached and released to destruction.
//
// WantNode_Compare: WantCompareIntegers (0x5507b0) for operators 0-7 over edge and random values.
//
// Covered through the vtable (game 0xdd86d8 on the original side, ours on the other):
//   slot 0 QueryInterface 0x5b2ec0; slot 2 Release 0x5b36a0; slot 3 Detach 0x5b3890; slot 5 SetParent 0x5b3830;
//   slot 6 AddChild 0x5b2940; slot 7 RemoveChild 0x5b3740; slot 8 SetProperties 0x5b2ef0; slot 14 GetDefinition 0x5b2af0;
//   slot 20 Regenerates 0x5b2c10; slot 23 GetScore 0x5b2ad0; slot 24 GetInfluence 0x5b2a20; slot 25 GetScoreRange 0x5b2a00;
//   slot 26 HasFactor 0x5b3940; slot 27 IsSuperZero 0x5b2c20; slot 28 GetWeight 0x5b2b20; slot 29 GetAgeFlag 0x5b2b30;
//   slot 30 GetAspirationFlag 0x5b2b90; slot 31 GetPersonalityFlag 0x5b2c30; slot 32 GetPersonDataRange 0x5b2a70;
//   slot 33 GetAspirationScoreRange 0x5b29d0; slot 34 GetMoodRange 0x5b2a40; slot 35 GetRelationshipRange 0x5b2aa0;
//   slot 37 Matches 0x5b2c90.
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "rz/cRZString.h"
#include "ts/cTSWantNode.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace wantnode {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "d"}};

using svcfx::Blob;
using svcfx::F;

uint32_t Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}
const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }

constexpr uint32_t kSize = cTSWantNode::kSize;
constexpr uint32_t kAppCell = 0x10cc288;
constexpr uint32_t kFirstName = 0x1017308; // the 15 property name cells, 4 apart
constexpr uint32_t kNames = 15;
enum Type : uint32_t { kU32, kS32, kBool, kStr, kTypes };
// The type SetProperties reads each name as, in cell order (regeneration: a boolean or a uint32).
constexpr Type kNatural[kNames] = {kU32, kU32, kU32, kBool, kBool, kS32, kS32, kBool, kU32, kU32, kS32, kS32, kS32, kS32, kS32};
constexpr uint32_t kRegenerationName = 7, kSubjectTypeName = 2;

enum : unsigned { kApp = 0, kWants = 1, kDefinition = 2, kInstance = 3, kObjects = 4, kObject = 5, kStandIn = 6, kData = 7, kFakeNode = 8, kFakeParent = 9 };

template <class T> T &At(void *object, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(object) + off); }
template <int Slot, class R = uint32_t, class... A> R Call(void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), Slot>(object, args...);
}

// ---- the plan's fakes ----
struct Subject {
    int32_t type;
    int32_t id;
    int16_t integer;
    int16_t pad;
};
struct Fakes {
    uint32_t keepMask; // the want manager's answers by call (bit set: the definition)
    bool alwaysDefinition;
    std::string name;
    uint32_t score, influence, objectType, integerOperator;
    Subject subject;
    uint32_t instanceWantId;
    bool objectFound, standIn;
    uint32_t dataValue;
    uint32_t fakeDepth;
};
Fakes gFakes;
cRZString *gName;

uint32_t WantsDefinition(svcfx::Obj *self, uint32_t *)
{
    const uint32_t call = self->value[0]++;
    if (gFakes.alwaysDefinition) return svcfx::P(&F(kDefinition));
    return (gFakes.keepMask >> (call % 32)) & 1 ? svcfx::P(&F(kDefinition)) : 0;
}

void SetupFakes()
{
    svcfx::ResetAll();
    F(kApp).Set(4, 0, &F(kWants));
    F(kApp).Set(24, 0, &F(kObjects));
    F(kWants).Set(23, "d", 0, &WantsDefinition);
    F(kDefinition).Set(4, 0, static_cast<const void *>(gName));
    F(kDefinition).Set(6, 0, gFakes.score);
    F(kDefinition).Set(7, 0, gFakes.influence);
    F(kDefinition).Set(16, 0, gFakes.objectType);
    F(kDefinition).Set(17, 0, gFakes.integerOperator);
    F(kInstance).Set(13, 0, gFakes.instanceWantId);
    F(kInstance).Set(14, 0, &gFakes.subject);
    F(kObjects).Set(14, 1, gFakes.objectFound ? svcfx::P(&F(kObject)) : 0);
    F(kObject).Set(27, 0, gFakes.standIn ? svcfx::P(&F(kStandIn)) : 0);
    F(kObject).Set(4, 0, &F(kData));
    F(kStandIn).Set(27, 0, 0u);
    F(kStandIn).Set(4, 0, &F(kData));
    F(kData).Set(3, 1, gFakes.dataValue);
    // A node of another kind (its calls recorded in order): Detach, and its depth for SetParent.
    F(kFakeNode).Set(3, 0);
    F(kFakeNode).Set(13, 0, gFakes.fakeDepth);
    F(kFakeParent).Set(3, 0);
    F(kFakeParent).Set(13, 0, gFakes.fakeDepth + 1);
}

// ---- the plan ----
struct Property {
    uint32_t name;
    Type type;
    uint32_t number;
};
struct Op {
    uint32_t kind, node, other, value;
    std::vector<Property> props;
};
struct Plan {
    uint32_t nodes;
    std::vector<Op> ops;
};
Plan gPlan;

uint32_t RandomNumber(std::mt19937 &rng, uint32_t name)
{
    if (name == kSubjectTypeName && rng() % 3) return rng() % 9;
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return uint32_t(-int32_t(rng() % 100));
    case 2: return rng() % 4;
    case 3: return 0x80000000u + rng() % 3;
    default: return uint32_t(rng());
    }
}

void RandomFakes(std::mt19937 &rng)
{
    Fakes &f = gFakes;
    f.keepMask = rng() % 4 ? rng() | rng() : rng();
    f.alwaysDefinition = false;
    static const char *const names[] = {"", "Want", "Go To Work", "x"};
    f.name = names[rng() % 4];
    f.score = rng() % 2 ? rng() % 500 : uint32_t(rng());
    f.influence = rng() % 2 ? rng() % 50 : uint32_t(rng());
    f.objectType = rng() % 3 ? rng() % 8 : uint32_t(rng());
    f.integerOperator = rng() % 8;
    f.fakeDepth = rng() % 3 ? rng() % 5 : uint32_t(rng());
}

void RandomPlan(std::mt19937 &rng)
{
    Plan &p = gPlan;
    p.nodes = 1 + rng() % 4;
    p.ops.clear();
    for (uint32_t i = 0, n = 5 + rng() % 40; i < n; ++i) {
        Op op{rng() % 16, rng() % p.nodes, rng() % (p.nodes + 2), uint32_t(rng()), {}};
        if (op.kind == 0 || op.kind == 1) {
            op.kind = 0;
            const uint32_t density = 20 + rng() % 80;
            for (uint32_t name = 0; name < kNames; ++name) {
                if (rng() % 100 >= density) continue;
                Property q{name, rng() % 6 ? kNatural[name] : Type(rng() % kTypes), RandomNumber(rng, name)};
                if (name == kRegenerationName && rng() % 2) q.type = kU32;
                if (q.type == kBool) q.number = rng() % 6 ? q.number % 2 : uint8_t(rng());
                op.props.push_back(q);
            }
            for (size_t k = op.props.size(); k > 1; --k) std::swap(op.props[k - 1], op.props[rng() % k]);
        }
        p.ops.push_back(op);
    }
}

void Fill(cGZPropertySetString *set, const std::vector<Property> &props)
{
    for (const Property &q : props) {
        const GZServiceString key(Text(kFirstName + 4 * q.name));
        switch (q.type) {
        case kU32: {
            gz::AnyUint32 v(q.number);
            set->SetProperty(&key, &v);
            break;
        }
        case kS32: {
            AnySint32 v{GameAddress<void>(gzany::kSint32VTable), int32_t(q.number)};
            set->SetProperty(&key, &v);
            break;
        }
        case kBool: {
            gz::AnyBoolean v(uint8_t(q.number));
            set->SetProperty(&key, &v);
            break;
        }
        default: {
            auto *text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            text->Construct();
            text->mString = "7";
            AnyString v{GameAddress<void>(gzany::kStringVTable), text->AsInterface()};
            text->AddRef();
            set->SetProperty(&key, &v);
            v.Destruct();
            break;
        }
        }
    }
}

// ---- the forest ----
std::vector<void *> gNodes; // the case's nodes, and a released victim while it lives
uint32_t Idx(const void *p)
{
    if (!p) return 0xffff;
    if (p == &F(kFakeNode)) return 0x7000;
    if (p == &F(kFakeParent)) return 0x7001;
    for (size_t i = 0; i < gNodes.size(); ++i)
        if (gNodes[i] == p) return uint32_t(i);
    return 0xeeee;
}

void DigestNode(Blob &d, void *n)
{
    d.push_back(0x800);
    for (uint32_t off = 4; off < 0x2c; off += 4) d.push_back(At<uint32_t>(n, off));
    for (uint32_t off = 0x2c; off < 0x2f; ++off) d.push_back(At<uint8_t>(n, off));
    for (uint32_t off = 0x30; off < 0x58; off += 4) d.push_back(At<uint32_t>(n, off));
    for (uint32_t off = 0x58; off < 0x5f; ++off) d.push_back(At<uint8_t>(n, off));
    for (uint32_t off = 0x60; off < 0x68; off += 4) d.push_back(At<uint32_t>(n, off));
    for (uint32_t off : {0x68u, 0x70u, 0x78u}) {
        const auto &flags = At<std::map<int32_t, bool>>(n, off);
        d.push_back(uint32_t(flags.size()));
        for (auto &[k, v] : flags) d.push_back(uint32_t(k)), d.push_back(v);
    }
    for (uint32_t off = 0x80; off < 0xa8; off += 4) d.push_back(At<uint32_t>(n, off));
    d.push_back(Idx(At<void *>(n, 0xa8)));
    const auto &children = At<WantNodeList>(n, 0xac);
    d.push_back(uint32_t(children.size()));
    for (const WantNodeRef &c : children) d.push_back(Idx(c.Get()));
    d.push_back(At<uint32_t>(n, 0xb8));
}

void DigestGetters(Blob &d, void *n, std::mt19937 &rng)
{
    d.push_back(0x810);
    d.push_back(Call<9>(n));
    d.push_back(Call<10>(n));
    d.push_back(Idx(Call<11, void *>(n)));
    d.push_back(uint32_t(static_cast<uint8_t *>(Call<12, void *>(n)) - static_cast<uint8_t *>(n)));
    d.push_back(Call<13>(n));
    d.push_back(svcfx::Norm(Call<14>(n)));
    d.push_back(Call<15>(n));
    d.push_back(Call<16>(n));
    d.push_back(Call<17>(n));
    d.push_back(Call<18>(n) & 0xff);
    d.push_back(Call<19>(n) & 0xff);
    d.push_back(Call<20>(n) & 0xff);
    d.push_back(Call<21>(n));
    d.push_back(Call<22>(n));
    d.push_back(Call<23>(n));
    d.push_back(Call<24>(n));
    int32_t lo = 0x1111, hi = 0x2222;
    Call<25, void>(n, &lo, &hi);
    d.push_back(uint32_t(lo)), d.push_back(uint32_t(hi));
    for (int32_t f = 0; f < cTSWantNode::kFactors; ++f) {
        d.push_back(Call<26>(n, f) & 0xff);
        d.push_back(Call<27>(n, f) & 0xff);
        d.push_back(Call<28>(n, f));
    }
    for (int32_t k = -1; k <= 14; ++k) d.push_back(Call<29>(n, k) & 0xff);
    for (int32_t k : {0, 1, 2, 3, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, -1})
        d.push_back(Call<30>(n, k) & 0xff);
    for (int32_t k = -1; k <= 14; ++k) d.push_back(Call<31>(n, k) & 0xff);
    for (int slot = 32; slot <= 35; ++slot) {
        lo = int32_t(rng()), hi = int32_t(rng());
        uint32_t r = 0;
        switch (slot) {
        case 32: r = Call<32>(n, &lo, &hi); break;
        case 33: r = Call<33>(n, &lo, &hi); break;
        case 34: r = Call<34>(n, &lo, &hi); break;
        default: r = Call<35>(n, &lo, &hi); break;
        }
        d.push_back(r & 0xff), d.push_back(uint32_t(lo)), d.push_back(uint32_t(hi));
    }
    d.push_back(Call<36>(n));
}

// Matches (slot 37) against the instance fake; the want manager answers the definition throughout (without one the
// original calls through null).
void DigestMatches(Blob &d, void *n, std::mt19937 &rng)
{
    gFakes.alwaysDefinition = true;
    for (int i = 0; i < 6; ++i) {
        Subject &s = gFakes.subject;
        const uint32_t objectField = At<uint32_t>(n, 0x24), integerField = At<uint32_t>(n, 0x28);
        s.type = int32_t(rng() % 5);
        switch (rng() % 4) {
        case 0: s.id = int32_t(objectField); break;
        case 1: s.id = int32_t((objectField & 0xffff) | (rng() << 16)); break;
        case 2: s.id = 0; break;
        default: s.id = int32_t(rng()); break;
        }
        s.integer = int16_t(rng() % 3 ? integerField + rng() % 3 - 1 : rng());
        s.pad = int16_t(rng());
        gFakes.instanceWantId = rng() % 4 ? At<uint32_t>(n, 0x1c) : uint32_t(rng());
        gFakes.objectFound = rng() % 4 != 0;
        gFakes.standIn = rng() % 3 == 0;
        gFakes.dataValue = rng() % 2 ? objectField : uint32_t(rng());
        gFakes.integerOperator = rng() % 8;
        F(kInstance).Set(13, 0, gFakes.instanceWantId);
        F(kObjects).Set(14, "d", gFakes.objectFound ? svcfx::P(&F(kObject)) : 0);
        F(kObject).Set(27, 0, gFakes.standIn ? svcfx::P(&F(kStandIn)) : 0);
        F(kData).Set(3, "d", gFakes.dataValue);
        F(kDefinition).Set(17, 0, gFakes.integerOperator);
        d.push_back(Call<37>(n, static_cast<void *>(&F(kInstance))) & 0xff);
    }
    gFakes.alwaysDefinition = false;
}

// A node, game-built (side 0) or ours.
void *NewNode(difftest::Context &ctx, int side, uint32_t seed)
{
    if (side == 1 && seed % 2) return static_cast<cITSWantNode *>(new cTSWantNode);
    void *memory = ctx.Original<void *(__cdecl *)(size_t)>(0x824632)(kSize);
    std::memset(memory, 0xcd, kSize);
    return side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x5b2750)(memory)
                     : static_cast<cITSWantNode *>(cTSWantNodeAdapters::Construct(memory, nullptr));
}

// The tree loader's direct writes.
void WriteFields(void *n, std::mt19937 &rng)
{
    const int32_t factor = int32_t(rng() % cTSWantNode::kFactors);
    switch (rng() % 5) {
    case 0:
        At<int32_t>(n, 0x3c + 4 * factor) = int32_t(rng() % 3 ? rng() % 4 : rng()) - 1;
        At<uint8_t>(n, 0x58 + factor) = uint8_t(rng() % 4 == 0);
        break;
    case 1:
    case 2: {
        static const uint32_t offs[] = {0x68, 0x70, 0x78};
        static const int32_t keys[] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 0x10, 0x20, 0x40, 0x80, 0x100, 5, -1};
        At<std::map<int32_t, bool>>(n, offs[rng() % 3])[keys[rng() % std::size(keys)]] = rng() % 3 != 0;
        break;
    }
    case 3: {
        static const uint32_t offs[] = {0x80, 0x88, 0x90, 0x98};
        const uint32_t off = offs[rng() % 4];
        At<int32_t>(n, off) = rng() % 3 ? 0 : int32_t(rng() % 200) - 100;
        At<int32_t>(n, off + 4) = rng() % 3 ? 0 : int32_t(rng() % 200) - 100;
        break;
    }
    default: At<int32_t>(n, 0xa0) = int32_t(rng() % 10); break;
    }
}

Blob Run(difftest::Context &ctx, int side, uint32_t seed)
{
    SetupFakes();
    std::mt19937 rng(seed * 13 + 5);
    gNodes.clear();
    std::vector<uint32_t> extra(gPlan.nodes); // references the test added (case 9) and may drop again (case 10)
    Blob d;
    for (uint32_t i = 0; i < gPlan.nodes; ++i) {
        void *n = NewNode(ctx, side, seed);
        gNodes.push_back(n);
        d.push_back(Call<1>(n)); // the test's reference
        DigestNode(d, n);
    }
    for (const Op &op : gPlan.ops) {
        void *n = gNodes[op.node];
        // The other node: one of the forest, none, or the fake.
        void *other = op.other < gPlan.nodes ? gNodes[op.other] : op.other == gPlan.nodes ? nullptr : &F(kFakeNode);
        d.push_back(0x900 + op.kind);
        switch (op.kind) {
        case 0: {
            auto *set = new cGZPropertySetString;
            Fill(set, op.props);
            Call<8, void>(n, static_cast<cIGZPropertySetString *>(set));
            delete static_cast<cIGZPropertySetString *>(set);
            break;
        }
        case 2: Call<4, void>(n, op.value % 3 ? op.value % 5 : op.value); break;
        case 3: Call<5, void>(n, other); break;
        case 4:
            // Only to a later node or the fake (no cycles); sometimes every later node after it, so lists grow long and
            // hold duplicates.
            if (other && op.other > op.node) {
                Call<6, void>(n, other);
                if (op.value % 3 == 0)
                    for (uint32_t j = op.node + 1; j < gPlan.nodes; ++j) Call<6, void>(n, gNodes[j]);
            }
            break;
        case 5: {
            // A child of the node (from anywhere in its list) or any other.
            const auto &children = At<WantNodeList>(n, 0xac);
            if (op.value % 3 && !children.empty()) other = children[(op.value / 3) % children.size()].Get();
            Call<7, void>(n, other);
            break;
        }
        case 6:
            if (op.value % 3 == 0) Call<3, void>(n);
            break;
        case 7:
        case 8: WriteFields(n, rng); break;
        case 9: d.push_back(Call<1>(n)), ++extra[op.node]; break;
        case 10:
            if (extra[op.node]) d.push_back(Call<2>(n)), --extra[op.node];
            break;
        case 11:
            for (uint32_t iid : {1u, cTSWantNode::kIid, 0x0d82acb7u, op.value}) {
                void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                d.push_back(Call<0>(n, iid, &out) & 0xff);
                d.push_back(out == n ? 1u : svcfx::P(out));
            }
            break;
        case 12: {
            // A node with a parent and children, released to destruction (the parent and children let go).
            void *victim = NewNode(ctx, side, seed);
            gNodes.push_back(victim);
            d.push_back(Call<1>(victim));
            // Its parent and children may be fakes (two, so the order the destruction lets go of them shows).
            if (other) Call<5, void>(victim, other == &F(kFakeNode) ? &F(kFakeParent) : other);
            Call<6, void>(victim, n);
            if (op.value % 2) Call<6, void>(victim, gNodes[op.value % gPlan.nodes]);
            if (op.value % 5 < 2) Call<6, void>(victim, static_cast<void *>(&F(kFakeNode)));
            DigestNode(d, victim);
            d.push_back(Call<2>(victim));
            gNodes.pop_back();
            break;
        }
        case 13:
        case 14: DigestGetters(d, n, rng); break;
        default: DigestMatches(d, n, rng); break;
        }
        for (void *node : gNodes) DigestNode(d, node);
    }
    // Detach the forest, then release the test's references down to destruction.
    for (void *n : gNodes) Call<3, void>(n);
    for (void *n : gNodes) DigestNode(d, n);
    std::vector<void *> nodes = gNodes;
    for (size_t i = 0; i < nodes.size(); ++i) {
        void *n = nodes[i];
        for (int32_t refs = At<int32_t>(n, 4);; --refs) {
            d.push_back(Call<2>(n));
            if (refs <= 1) break;
        }
        gNodes[i] = nullptr;
    }
    gNodes.clear();
    d.insert(d.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::CaptureFakes(d, kValueKinds);
    d.push_back(gName->mRefCount);
    d.push_back(Hash(gName->mString));
    return d;
}

} // namespace wantnode

using namespace wantnode;

DIFF_TEST(WantNode_Ops, 0x5b2ef0)
{
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(kAppCell, &F(kApp));
    const uint32_t seeds = std::getenv("WANTNODE_SEEDS") ? uint32_t(std::atoi(std::getenv("WANTNODE_SEEDS"))) : 2000;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        std::mt19937 plan(seed);
        RandomFakes(plan);
        RandomPlan(plan);
        Blob r[2];
        for (int side = 0; side < 2; ++side) {
            gName = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            gName->Construct();
            gName->mString = gFakes.name;
            r[side] = Run(ctx, side, seed);
            gName->Destruct();
            ::operator delete(gName);
            gName = nullptr;
        }
        if (!svcfx::Same(ctx, r[0], r[1], "WantNode", seed)) break;
    }
}

DIFF_TEST(WantNode_Compare, 0x5507b0)
{
    const int32_t edges[] = {0, 1, -1, 2, INT32_MIN, INT32_MAX, INT32_MIN + 1, INT32_MAX - 1, 100, -100};
    std::mt19937 rng(7);
    Blob r[2];
    for (int side = 0; side < 2; ++side) {
        rng.seed(7);
        for (uint32_t op = 0; op < 8; ++op)
            for (int i = 0; i < 400; ++i) {
                const int32_t a = i < 100 ? edges[i % 10] : int32_t(rng() % 4 ? rng() % 7 : rng());
                const int32_t b = i < 100 ? edges[i / 10] : int32_t(rng() % 4 ? rng() % 7 : rng());
                const uint32_t which = op == 7 ? uint32_t(rng()) : op;
                const bool result = side == 0
                    ? ctx.Original<bool(__stdcall *)(uint32_t, int32_t, int32_t)>(0x5507b0)(which, a, b)
                    : WantCompareIntegers(which, a, b);
                r[side].push_back(result);
            }
    }
    svcfx::Same(ctx, r[0], r[1], "Compare", 0);
}
