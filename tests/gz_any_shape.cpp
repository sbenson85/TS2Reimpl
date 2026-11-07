// Differential tests for AnyUint32, AnyBoolean and AnyFloat32 in the standalone shape (src/gz/cGZAny.h, lane GZ).
//
// Original side: an object as the game builds it (the game's vtable and the value), each slot called through the
// game's vtable, i.e. the image's slot functions. Our side: an object built by our constructor, each slot called
// through its own (our) vtable. Compared: results, the object after the call, the calls made (Assign asks the source
// and this for their type: slot 4 of both is a recording fake, through a copy of each side's own vtable), and the heap
// (frees). The only words that differ by design are vtable words, compared as codes: the game's class vtable / ours
// (our vtable's RTTI names the class), cIGZAny's after a destructor (ours: RTTI ".?AVcIGZAny@@").
//
// GZAnyShape_Adapters: the hook entry points on objects with a copy of the game's vtable whose slots trap (except
// slot 4, which Assign legitimately calls on this), against the image's functions.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZAny.h"
#include "heap_poison.h"

#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

enum class Kind { Uint32, Boolean, Float32 };

struct ValueClass {
    Kind kind;
    const char *name;
    uint32_t vtable;   // the game's
    uint32_t slots[6]; // the image's slot functions, in vtable order
    uint32_t type;
};

const ValueClass kClasses[] = {
    {Kind::Uint32, "AnyUint32", 0xd9f534, {0x14c5f0, 0x177b90, 0x177b70, 0x177ab0, 0x179d00, 0x179d10}, 0xeb61e4f7},
    {Kind::Boolean, "AnyBoolean", 0xd9c670, {0x14c5f0, 0x14e760, 0x14e5b0, 0x14df70, 0x1522b0, 0x1522c0}, 0xcba908e1},
    {Kind::Float32, "AnyFloat32", 0xda3d70, {0x1bedcb, 0x1bf068, 0x1befac, 0x1bef35, 0x1bf5bf, 0x1bf5d7}, 0xabc78708},
};

constexpr uint32_t kAnyVTable = 0xd9c654; // cIGZAny's

void *const *VTableOf(const void *object)
{
    return *static_cast<void *const *const *>(object);
}

// The RTTI name of a vtable of ours (complete-object locator before it, its type descriptor's name at +8).
std::string RttiName(const void *vtable)
{
    auto *col = static_cast<const uint32_t *const *>(vtable)[-1];
    auto *td = reinterpret_cast<const char *>(static_cast<uintptr_t>(col[3]));
    return std::string(td + 8);
}

std::string OurName(Kind kind)
{
    switch (kind) {
    case Kind::Uint32: return ".?AUAnyUint32@gz@@";
    case Kind::Boolean: return ".?AUAnyBoolean@gz@@";
    default: return ".?AUAnyFloat32@gz@@";
    }
}

// Our object of the class, in place.
cIGZAny *MakeOurs(Kind kind, void *memory, uint32_t payload)
{
    switch (kind) {
    case Kind::Uint32: return ::new (memory) gz::AnyUint32(payload);
    case Kind::Boolean: return ::new (memory) gz::AnyBoolean(static_cast<uint8_t>(payload));
    default: {
        float value;
        std::memcpy(&value, &payload, 4);
        return ::new (memory) gz::AnyFloat32(value);
    }
    }
}

template <typename R, typename... A> R CallSlot(const void *object, int slot, A... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(const void *, A...)>(VTableOf(object)[slot]);
    return fn(object, args...);
}

// Vtable word codes: 1 the game's class vtable, 2 ours (RTTI), 3 the game's cIGZAny, 4 ours, 0 anything else.
int Code(const difftest::Context &ctx, const ValueClass &c, const void *word)
{
    if (word == ctx.image.At<void>(c.vtable)) return 1;
    if (word == ctx.image.At<void>(kAnyVTable)) return 3;
    const uintptr_t at = reinterpret_cast<uintptr_t>(word);
    if (at && (at < ctx.image.Base() || at >= ctx.image.Base() + ctx.image.Size())) {
        std::string name = RttiName(word);
        if (name == OurName(c.kind)) return 2;
        if (name == ".?AVcIGZAny@@") return 4;
    }
    return 0;
}

// Payloads: edges and random words; floats with zeros, infinities, NaNs, denormals.
uint32_t Payload(Kind kind, std::mt19937 &rng)
{
    static const uint32_t floats[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001,
                                      0x00000001, 0x3f800000, 0xbf800000, 0x7f7fffff};
    switch (kind) {
    case Kind::Float32: {
        if (rng() % 3 == 0) return floats[rng() % 10];
        float f = std::uniform_real_distribution<float>(-4.0f, 4.0f)(rng);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
    }
    case Kind::Boolean: // only the low byte is the value; the others must not count
        return (rng() & 0xffffff00u) | (rng() % 3 == 0 ? rng() & 0xff : rng() % 2);
    default:
        switch (rng() % 5) {
        case 0: return rng() % 3;
        case 1: return 0x80000000u + rng() % 3 - 1;
        case 2: return 0xffffffffu - rng() % 2;
        default: return rng();
        }
    }
}

// What our object holds of a payload (AnyBoolean: the byte; the other bytes stay as they were in memory).
uint32_t Held(Kind kind, uint32_t payload, uint32_t before)
{
    return kind == Kind::Boolean ? (before & 0xffffff00u) | (payload & 0xff) : payload;
}

// ---- recording fakes for Assign: slot 4 of this (through a vtable copy) and of the source ----

std::vector<uint32_t> gEvents;
uint32_t gThisType = 0;

uint32_t __fastcall ThisGetType(void *, void *)
{
    gEvents.push_back(0x401);
    return gThisType;
}

struct FakeAny {
    void **vt;
    uint32_t value; // +4
    uint32_t type;
};

uint32_t __fastcall SourceGetType(void *self, void *)
{
    gEvents.push_back(0x402);
    return static_cast<FakeAny *>(self)->type;
}

void *gSourceVTable[6] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&SourceGetType), nullptr};

const uint32_t kTypes[] = {0xeb61e4f7, 0x0c264712, 0xabc78708, 0xcba908e1, 0x0b8bea18, 0xad9a861e, 0xae24d4f7, 0x12345678};

// One side's object: the game's (vtable + payload in a buffer) or ours (our constructor on the buffer).
struct Object {
    alignas(8) uint32_t words[2];
    void *Build(const difftest::Context &ctx, const ValueClass &c, bool orig, uint32_t payload)
    {
        words[0] = 0xdeadbeef;
        words[1] = payload;
        if (orig) {
            words[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(c.vtable)));
            return words;
        }
        return MakeOurs(c.kind, words, payload);
    }
};

void TestConstruct(difftest::Context &ctx, const ValueClass &c, std::mt19937 &rng)
{
    for (int i = 0; i < 200; ++i) {
        uint32_t payload = Payload(c.kind, rng), junk[4] = {rng(), rng(), rng(), rng()};
        uint32_t mem[4];
        std::memcpy(mem, junk, sizeof mem);
        MakeOurs(c.kind, mem + 1, payload);
        ++ctx.cases;
        if (mem[0] != junk[0] || mem[3] != junk[3] || Code(ctx, c, reinterpret_cast<void *>(mem[1])) != 2 ||
            mem[2] != Held(c.kind, payload, junk[2]))
            ctx.Fail("%s: our constructor of %08x wrote %08x %08x %08x %08x", c.name, payload, mem[0], mem[1], mem[2], mem[3]);
    }
}

void TestDeletingDestructor(difftest::Context &ctx, const ValueClass &c, std::mt19937 &rng)
{
    for (int i = 0; i < 2000; ++i) {
        uint32_t flags = i < 4 ? static_cast<uint32_t>(i) : rng();
        uint32_t payload = Payload(c.kind, rng);
        int code[2] = {};
        uint32_t value[2] = {};
        bool freed[2] = {};
        for (int side = 0; side < 2; ++side) {
            auto *obj = static_cast<uint32_t *>(::operator new(8));
            obj[1] = payload;
            if (side == 0) obj[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(c.vtable)));
            else MakeOurs(c.kind, obj, payload);
            const uint32_t held = obj[1];
            difftest::FreeRecorder rec;
            void *ret = CallSlot<void *>(obj, 0, flags);
            rec.Stop();
            freed[side] = rec.Freed(obj);
            if (ret != obj) ctx.Fail("%s: deleting destructor side %d returned %p, not this", c.name, side, ret);
            if (freed[side] != ((flags & 1) != 0) || rec.Count() != (freed[side] ? 1u : 0u))
                ctx.Fail("%s: deleting destructor side %d flags %08x: freed %d, %zu frees", c.name, side, flags, freed[side],
                         rec.Count());
            if (!freed[side]) {
                code[side] = Code(ctx, c, reinterpret_cast<void *>(obj[0]));
                value[side] = obj[1];
                if (obj[1] != held) ctx.Fail("%s: deleting destructor side %d changed the value", c.name, side);
                ::operator delete(obj);
            }
        }
        ++ctx.cases;
        if (!freed[0] && (code[0] != 3 || code[1] != 4 || value[0] != Held(c.kind, payload, payload) ||
                          value[1] != value[0]))
            ctx.Fail("%s: deleting destructor flags %08x left vtable codes %d/%d, values %08x/%08x", c.name, flags, code[0],
                     code[1], value[0], value[1]);
    }
}

void TestAssign(difftest::Context &ctx, const ValueClass &c, std::mt19937 &rng)
{
    for (int i = 0; i < 10000; ++i) {
        uint32_t typeThis = rng() % 2 ? c.type : kTypes[rng() % 8];
        uint32_t typeSource = rng() % 2 ? typeThis : kTypes[rng() % 8];
        uint32_t valueThis = Payload(c.kind, rng), valueSource = Payload(c.kind, rng);
        bool self = rng() % 16 == 0;
        uint8_t result[2];
        uint32_t after[2];
        std::vector<uint32_t> events[2];
        for (int side = 0; side < 2; ++side) {
            Object o;
            void *obj = o.Build(ctx, c, side == 0, valueThis);
            const uint32_t before = o.words[1];
            void *copy[6];
            std::memcpy(copy, VTableOf(obj), sizeof copy);
            copy[4] = reinterpret_cast<void *>(&ThisGetType);
            void *own = o.words[0] == 0 ? nullptr : reinterpret_cast<void *>(o.words[0]);
            o.words[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy));
            FakeAny source{gSourceVTable, valueSource, typeSource};
            gThisType = typeThis;
            gEvents.clear();
            result[side] = CallSlot<uint8_t>(obj, 1, self ? obj : static_cast<void *>(&source));
            o.words[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(own));
            after[side] = o.words[1];
            events[side] = gEvents;
            if (source.value != valueSource) ctx.Fail("%s: Assign wrote the source", c.name);
            (void)before;
        }
        ++ctx.cases;
        if (result[0] != result[1] || after[0] != after[1] || events[0] != events[1])
            ctx.Fail("%s: Assign %08x <- %08x (self %d): %u/%u, value %08x/%08x, %zu/%zu calls", c.name, typeThis,
                     typeSource, self, result[0], result[1], after[0], after[1], events[0].size(), events[1].size());
    }
}

void TestCompare(difftest::Context &ctx, const ValueClass &c, std::mt19937 &rng)
{
    for (int i = 0; i < 20000; ++i) {
        uint32_t a = Payload(c.kind, rng), b = rng() % 4 == 0 ? a : Payload(c.kind, rng);
        if (rng() % 8 == 0) b = a ^ (1u << (rng() % 32)); // neighbours, sign flips
        uint32_t other[2] = {rng(), b};                   // the other's vtable is not read
        int32_t got[2];
        for (int side = 0; side < 2; ++side) {
            Object o;
            void *obj = o.Build(ctx, c, side == 0, a);
            got[side] = CallSlot<int32_t>(obj, 2, static_cast<void *>(other));
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("%s: Compare %08x with %08x: %d/%d", c.name, a, b, got[0], got[1]);
    }
}

void TestClone(difftest::Context &ctx, const ValueClass &c, std::mt19937 &rng)
{
    for (int i = 0; i < 3000; ++i) {
        uint32_t payload = Payload(c.kind, rng);
        heappoison::Scope poison(ctx, i % 2 ? 0x5a : 0xcd); // every heap block poisoned on both sides
        uint32_t made[2][2];
        int code[2];
        for (int side = 0; side < 2; ++side) {
            Object o;
            void *obj = o.Build(ctx, c, side == 0, payload);
            void *copy = CallSlot<void *>(obj, 3);
            if (!copy) {
                ctx.Fail("%s: Clone returned null", c.name);
                return;
            }
            std::memcpy(made[side], copy, 8);
            code[side] = Code(ctx, c, reinterpret_cast<void *>(made[side][0]));
            ::operator delete(copy);
        }
        ++ctx.cases;
        // AnyBoolean's clone leaves bytes 5-7 as operator new returned them: the poison on both sides.
        uint32_t mask = c.kind == Kind::Boolean ? 0xffu : 0xffffffffu;
        if (code[0] != 1 || code[1] != 2 || made[0][1] != made[1][1] || (made[0][1] & mask) != (payload & mask))
            ctx.Fail("%s: Clone of %08x: codes %d/%d, %08x/%08x", c.name, payload, code[0], code[1], made[0][1], made[1][1]);
    }
}

void TestTypeSlots(difftest::Context &ctx, const ValueClass &c)
{
    uint32_t type[2];
    const char *name[2];
    for (int side = 0; side < 2; ++side) {
        Object o;
        void *obj = o.Build(ctx, c, side == 0, 0);
        type[side] = CallSlot<uint32_t>(obj, 4);
        name[side] = CallSlot<const char *>(obj, 5);
    }
    ++ctx.cases;
    if (type[0] != type[1] || type[0] != c.type) ctx.Fail("%s: GetType %08x/%08x", c.name, type[0], type[1]);
    // The original returns the literal the image placed right after the vtable; ours our own literal: the text.
    ++ctx.cases;
    if (name[0] != ctx.image.At<char>(c.vtable + 0x18) || std::strcmp(name[0], name[1]) != 0 ||
        std::strcmp(name[1], c.name) != 0)
        ctx.Fail("%s: GetTypeName %s/%s", c.name, name[0], name[1]);
}

} // namespace

DIFF_TEST(GZAnyShape_Uint32, 0x177b90)
{
    const ValueClass &c = kClasses[0];
    std::mt19937 rng(21);
    TestConstruct(ctx, c, rng);
    TestDeletingDestructor(ctx, c, rng); // 0x14c5f0 through the game's vtable / our slot 0
    TestAssign(ctx, c, rng);             // 0x177b90
    TestCompare(ctx, c, rng);            // 0x177b70
    TestClone(ctx, c, rng);              // 0x177ab0
    TestTypeSlots(ctx, c);               // 0x179d00, 0x179d10
}

DIFF_TEST(GZAnyShape_Boolean, 0x14e760)
{
    const ValueClass &c = kClasses[1];
    std::mt19937 rng(22);
    TestConstruct(ctx, c, rng);
    TestDeletingDestructor(ctx, c, rng); // 0x14c5f0
    TestAssign(ctx, c, rng);             // 0x14e760
    TestCompare(ctx, c, rng);            // 0x14e5b0
    TestClone(ctx, c, rng);              // 0x14df70
    TestTypeSlots(ctx, c);               // 0x1522b0, 0x1522c0
}

DIFF_TEST(GZAnyShape_Float32, 0x1befac)
{
    const ValueClass &c = kClasses[2];
    std::mt19937 rng(23);
    TestConstruct(ctx, c, rng);
    TestDeletingDestructor(ctx, c, rng); // 0x1bedcb
    TestAssign(ctx, c, rng);             // 0x1bf068
    TestCompare(ctx, c, rng);            // 0x1befac
    TestClone(ctx, c, rng);              // 0x1bef35
    TestTypeSlots(ctx, c);               // 0x1bf5bf, 0x1bf5d7
}

// ---- the adapters (hook entry points) on game-built objects ----

namespace {

bool gTrapped = false;
uint32_t __fastcall Trap0(void *, void *)
{
    gTrapped = true;
    return 0;
}
uint32_t __fastcall Trap1(void *, void *, uint32_t)
{
    gTrapped = true;
    return 0;
}

struct AdapterSet {
    void *assign, *compare, *clone, *getType, *getTypeName;
};

template <class V> AdapterSet AdaptersOf()
{
    using A = gz::AnyAdapters<V>;
    return {reinterpret_cast<void *>(&A::Assign), reinterpret_cast<void *>(&A::Compare), reinterpret_cast<void *>(&A::Clone),
            reinterpret_cast<void *>(&A::GetType), reinterpret_cast<void *>(&A::GetTypeName)};
}

void TestAdapters(difftest::Context &ctx, const ValueClass &c, const AdapterSet &a, std::mt19937 &rng)
{
    // The game's vtable with every slot a trap but slot 4 (this's type, which Assign asks for).
    void *traps[6] = {reinterpret_cast<void *>(&Trap1), reinterpret_cast<void *>(&Trap1), reinterpret_cast<void *>(&Trap1),
                      reinterpret_cast<void *>(&Trap0), reinterpret_cast<void *>(&ThisGetType),
                      reinterpret_cast<void *>(&Trap0)};
    const uint32_t trapWord = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(traps));
    for (int i = 0; i < 4000; ++i) {
        uint32_t payload = Payload(c.kind, rng), other = Payload(c.kind, rng);
        uint32_t typeSource = rng() % 2 ? c.type : kTypes[rng() % 8];
        gThisType = rng() % 4 ? c.type : kTypes[rng() % 8];
        heappoison::Scope poison(ctx, 0x5a);
        uint32_t results[2][5];
        uint32_t after[2];
        std::vector<uint32_t> events[2];
        int cloneCode[2];
        uint32_t cloneValue[2];
        for (int side = 0; side < 2; ++side) {
            uint32_t obj[2] = {trapWord, payload};
            uint32_t otherObj[2] = {rng(), other};
            FakeAny source{gSourceVTable, other, typeSource};
            gTrapped = false;
            gEvents.clear();
            using Fn1 = uint32_t(__thiscall *)(void *, const void *);
            using Fn0 = uint32_t(__thiscall *)(void *);
            auto pick = [&](int slot, void *adapter) {
                return side == 0 ? ctx.Original<void *>(c.slots[slot]) : adapter;
            };
            results[side][0] = reinterpret_cast<Fn1>(pick(1, a.assign))(obj, &source) & 0xff;
            after[side] = obj[1];
            obj[1] = payload;
            results[side][1] = reinterpret_cast<Fn1>(pick(2, a.compare))(obj, otherObj);
            void *copy = reinterpret_cast<void *(__thiscall *)(void *)>(pick(3, a.clone))(obj);
            results[side][2] = copy != nullptr;
            cloneCode[side] = copy ? Code(ctx, c, reinterpret_cast<void *>(static_cast<uint32_t *>(copy)[0])) : -1;
            cloneValue[side] = copy ? static_cast<uint32_t *>(copy)[1] : 0;
            ::operator delete(copy);
            results[side][3] = reinterpret_cast<Fn0>(pick(4, a.getType))(obj);
            const char *name = reinterpret_cast<const char *(__thiscall *)(void *)>(pick(5, a.getTypeName))(obj);
            results[side][4] = std::strcmp(name, c.name) == 0;
            events[side] = gEvents;
            if (gTrapped) ctx.Fail("%s: side %d called a trapped slot", c.name, side);
        }
        ++ctx.cases;
        if (std::memcmp(results[0], results[1], sizeof results[0]) != 0 || after[0] != after[1] || events[0] != events[1] ||
            cloneCode[0] != 1 || cloneCode[1] != 2 || cloneValue[0] != cloneValue[1])
            ctx.Fail("%s: adapters differ from the image's slots (payload %08x)", c.name, payload);
    }
}

} // namespace

DIFF_TEST(GZAnyShape_Adapters, 0x177ab0)
{
    std::mt19937 rng(24);
    TestAdapters(ctx, kClasses[0], AdaptersOf<gz::AnyUint32>(), rng);
    TestAdapters(ctx, kClasses[1], AdaptersOf<gz::AnyBoolean>(), rng);
    TestAdapters(ctx, kClasses[2], AdaptersOf<gz::AnyFloat32>(), rng);
}
