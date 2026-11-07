// The framework's constructors and destructors (gz::cGZFramework, gz::cCTGFramework, through their adapters): the base class
// (0x141d76, 0x141f5c) and the game's (0xc2ccdb, 0xc2ce90, deleting 0xc2cec9). The objects hold
// heap pointers, so objects are compared with those fields zeroed, plus structural checks of the
// container heads. The original's constructors are called on one object, ours on another, over the
// same fill. See gz_com_support.h for the helpers.
#include "free_recorder.h"
#include "gz_com_support.h"

#include "gz/Framework.h"
#include "gz/FrameworkShape.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZExceptionNotification.h"

#include <list>
#include <set>
#include <unordered_map>

namespace {

constexpr uint32_t kBaseCtor = 0x141d76, kBaseDtor = 0x141f5c, kCtgCtor = 0xc2ccdb, kCtgDtor = 0xc2ce90, kCtgDeleting = 0xc2cec9;
constexpr size_t kBaseSize = 0x180, kCtgSize = 0x198;

using IdList = std::list<uint32_t>;
using IdHash = std::unordered_map<uint32_t, uint32_t>;
using IdMultimap = std::multimap<int32_t, std::pair<uint32_t, uint32_t>>;

template <typename T> T &At(void *object, size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(object) + offset);
}

// gz::cGZFramework's two vtables and gz::cCTGFramework's three as our constructors install them (lanes ER, EX: the
// constructors and destructors are our classes', so our side's +0 / +4 / +0x180 words are our vtables where the
// original's are the game's: base 0xd9b544 / 0xd9b60c, cCTGFramework 0xe995cc / 0xe99694 / 0xe996b4). Read from
// reference objects.
uint32_t gOurFwVt[2], gOurCtgVt[3];
void InitFwVtables()
{
    if (gOurFwVt[0]) return;
    void *p = ::operator new(kCtgSize);
    gz::cGZFrameworkAdapters::Construct(p, nullptr);
    gOurFwVt[0] = At<uint32_t>(p, 0);
    gOurFwVt[1] = At<uint32_t>(p, 4);
    gz::cGZFrameworkAdapters::Destruct(static_cast<gz::cGZFramework *>(p), nullptr);
    gz::cCTGFrameworkAdapters::Construct(p, nullptr);
    gOurCtgVt[0] = At<uint32_t>(p, 0);
    gOurCtgVt[1] = At<uint32_t>(p, 4);
    gOurCtgVt[2] = At<uint32_t>(p, 0x180);
    gz::cCTGFrameworkAdapters::Destruct(static_cast<gz::cCTGFramework *>(p), nullptr);
    ::operator delete(p);
}

// The framework's +0 / +4 (/ +0x180) words: each side's own vtables (base or derived, per face) become codes; anything
// else stays raw.
void NormFwVtables(std::vector<unsigned char> &bytes, int side)
{
    InitFwVtables();
    bool original = side == kOriginal;
    const uint32_t base[2] = {original ? reinterpret_cast<uint32_t>(GameAddress(0xd9b544)) : gOurFwVt[0],
                              original ? reinterpret_cast<uint32_t>(GameAddress(0xd9b60c)) : gOurFwVt[1]};
    const uint32_t derived[3] = {original ? reinterpret_cast<uint32_t>(GameAddress(0xe995cc)) : gOurCtgVt[0],
                                 original ? reinterpret_cast<uint32_t>(GameAddress(0xe99694)) : gOurCtgVt[1],
                                 original ? reinterpret_cast<uint32_t>(GameAddress(0xe996b4)) : gOurCtgVt[2]};
    const size_t offsets[3] = {0, 4, 0x180};
    for (int i = 0; i < 3; ++i) {
        if (offsets[i] + 4 > bytes.size()) continue;
        uint32_t word;
        std::memcpy(&word, bytes.data() + offsets[i], 4);
        if (i < 2 && word == base[i]) word = 0x0f0f0f00u + static_cast<uint32_t>(i);
        else if (word == derived[i]) word = 0x0f0f0f10u + static_cast<uint32_t>(i);
        std::memcpy(bytes.data() + offsets[i], &word, 4);
    }
}

// Pointer fields to zero before comparing (heap blocks that differ between the two objects).
// side: whose code built or destroyed the embedded COM object (its vtable words are named as that side's own).
std::vector<unsigned char> CanonFw(const void *object, size_t size, int side)
{
    InitVtables();
    std::vector<unsigned char> bytes(size);
    std::memcpy(bytes.data(), object, size);
    NormFwVtables(bytes, side);
    // The embedded COM object (+8): lock pointers, tree heads, the clock-seeded generator.
    // The framework: container heads, the hash vector, the lock block, the notification object.
    for (size_t off : {0x28u, 0x30u, 0x3cu, 0x44u, 0x58u, 0x5cu, 0x7cu, 0x84u, 0x90u, 0x98u, 0x9cu, 0xa0u, 0xacu, 0xb4u, 0xbcu, 0xc4u, 0xf8u, 0x104u, 0x108u, 0x10cu, 0x114u, 0x158u, 0x15cu, 0x160u, 0x168u, 0x184u, 0x18cu}) {
        if (off + 4 <= size) std::memset(bytes.data() + off, 0, 4);
    }
    NormComVtables(bytes, 8, side);
    return bytes;
}

// What the zeroed pointers must satisfy: empty containers (heads linked to themselves).
std::string Structure(void *object, size_t size, bool derived)
{
    std::string s;
    char buf[96];
    auto head = [&](size_t off) { return At<void *>(object, off); };
    for (size_t off : {0x7cu, 0xacu, 0xb4u, 0xbcu, 0xc4u, 0x90u}) {
        auto *node = static_cast<void **>(head(off));
        s += (node[0] == node && node[1] == node) ? "L" : "l";
    }
    s += HeadOk(head(0x84)) ? "T" : "t";
    s += HeadOk(head(0x3c)) ? "C" : "c"; // the COM object's class map
    s += HeadOk(head(0x44)) ? "C" : "c"; // and library set
    char *vecFirst = At<char *>(object, 0x98), *vecLast = At<char *>(object, 0x9c), *vecEnd = At<char *>(object, 0xa0);
    std::snprintf(buf, sizeof(buf), " vec=%d/%d", static_cast<int>(vecLast - vecFirst), static_cast<int>(vecEnd - vecFirst));
    s += buf;
    // The notification object's vtable: the game's (the original constructor) or our cRZExceptionNotification's (ours,
    // lane HV) are one code.
    auto *notification = At<uint32_t *>(object, 0x168);
    static const uint32_t ourNotificationVt = [] {
        cRZExceptionNotification probe;
        return *reinterpret_cast<uint32_t *>(&probe);
    }();
    bool isNotification = notification[0] == reinterpret_cast<uint32_t>(GameAddress(0xda44d4)) || notification[0] == ourNotificationVt;
    std::snprintf(buf, sizeof(buf), " note=%d/%d", isNotification, static_cast<int>(notification[1]));
    s += buf;
    if (derived) {
        s += HeadOk(head(0x184)) ? " S" : " s";
        std::set<uint32_t> &messages = At<std::set<uint32_t>>(object, 0x184);
        std::set<uint32_t> &services = At<std::set<uint32_t>>(object, 0x18c);
        for (uint32_t id : messages) {
            std::snprintf(buf, sizeof(buf), " m%x", id);
            s += buf;
        }
        for (uint32_t id : services) {
            std::snprintf(buf, sizeof(buf), " s%x", id);
            s += buf;
        }
    }
    (void)size;
    return s;
}

// A fake cRZUnknown-style object for the references the destructor releases.
struct FakeRef {
    void **vt;
    void *table[4];
    const char *name;
};
uint32_t __fastcall FakeRefAddRef(void *self, void *)
{
    Log("%s.addref", static_cast<FakeRef *>(self)->name);
    return 1;
}
uint32_t __fastcall FakeRefRelease(void *self, void *)
{
    Log("%s.release", static_cast<FakeRef *>(self)->name);
    return 1;
}
void InitRef(FakeRef &r, const char *name)
{
    std::memset(r.table, 0, sizeof(r.table));
    r.table[1] = reinterpret_cast<void *>(&FakeRefAddRef);
    r.table[2] = reinterpret_cast<void *>(&FakeRefRelease);
    r.vt = r.table;
    r.name = name;
}

template <typename Fn> void *BuildObject(size_t size, int fill, Fn construct)
{
    auto *p = ::operator new(size);
    std::memset(p, fill, size);
    construct(p);
    return p;
}

} // namespace

DIFF_TEST(GZFramework_ConstructBase, 0x141d76)
{
    uint32_t *clockStart = ctx.image.At<uint32_t>(0x10dfcd0);
    uint32_t saved[2] = {clockStart[0], clockStart[1]};
    for (int fill : {0xCD, 0x00, 0xFF}) {
        clockStart[0] = clockStart[1] = 0xffffffff; // the COM constructor's clock() (see gz_com_support.h)
        void *a = BuildObject(kBaseSize, fill, [&](void *p) { ctx.Original<void *(__thiscall *)(void *)>(kBaseCtor)(p); });
        clockStart[0] = saved[0];
        clockStart[1] = saved[1];
        void *b = BuildObject(kBaseSize, fill, [&](void *p) { gz::cGZFrameworkAdapters::Construct(p, nullptr); });
        ExpectSameBytes(ctx, "base constructor: bytes", CanonFw(a, kBaseSize, kOriginal), CanonFw(b, kBaseSize, kOurs));
        ExpectSame(ctx, "base constructor: structure", Structure(a, kBaseSize, false), Structure(b, kBaseSize, false));
        ++ctx.cases;
        if (At<uint32_t>(a, 0x174) != At<uint32_t>(b, 0x174) || At<uint32_t>(b, 0x174) == 0) ctx.Fail("base constructor: instance handle");
        // Tear both down with our destructor (tested below) to free their blocks.
        gz::cGZFrameworkAdapters::Destruct(static_cast<gz::cGZFramework *>(a), nullptr);
        gz::cGZFrameworkAdapters::Destruct(static_cast<gz::cGZFramework *>(b), nullptr);
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(GZFramework_Construct, 0xc2ccdb)
{
    uint32_t *clockStart = ctx.image.At<uint32_t>(0x10dfcd0);
    uint32_t saved[2] = {clockStart[0], clockStart[1]};
    for (int fill : {0xCD, 0x00, 0xFF}) {
        clockStart[0] = clockStart[1] = 0xffffffff;
        void *a = BuildObject(kCtgSize, fill, [&](void *p) { ctx.Original<void *(__thiscall *)(void *)>(kCtgCtor)(p); });
        clockStart[0] = saved[0];
        clockStart[1] = saved[1];
        void *b = BuildObject(kCtgSize, fill, [&](void *p) { gz::cCTGFrameworkAdapters::Construct(p, nullptr); });
        ExpectSameBytes(ctx, "constructor: bytes", CanonFw(a, kCtgSize, kOriginal), CanonFw(b, kCtgSize, kOurs));
        ExpectSame(ctx, "constructor: structure", Structure(a, kCtgSize, true), Structure(b, kCtgSize, true));
        // The sets must have the same shape, not only the same members: compare the node order
        // of a walk from the root by walking both and comparing the sequence of keys at each depth.
        for (size_t off : {0x184u, 0x18cu}) {
            std::string shapeA, shapeB;
            auto shape = [&](void *object, std::string &out) {
                auto *head = At<uint8_t *>(object, off);
                std::vector<uint8_t *> level{*reinterpret_cast<uint8_t **>(head + 4)}; // root = head->parent
                while (!level.empty()) {
                    std::vector<uint8_t *> next;
                    for (uint8_t *n : level) {
                        if (n[0xd]) { // nil
                            out += "- ";
                            continue;
                        }
                        char buf[24];
                        std::snprintf(buf, sizeof(buf), "%x%c ", *reinterpret_cast<uint32_t *>(n + 0x10), n[0xc] ? 'b' : 'r');
                        out += buf;
                        next.push_back(*reinterpret_cast<uint8_t **>(n));
                        next.push_back(*reinterpret_cast<uint8_t **>(n + 8));
                    }
                    out += "/ ";
                    level = next;
                    if (out.size() > 4000) break;
                }
            };
            shape(a, shapeA);
            shape(b, shapeB);
            ExpectSame(ctx, "constructor: set shape", shapeA, shapeB);
        }
        gz::cCTGFrameworkAdapters::Destruct(static_cast<gz::cCTGFramework *>(a), nullptr);
        gz::cCTGFrameworkAdapters::Destruct(static_cast<gz::cCTGFramework *>(b), nullptr);
        ::operator delete(a);
        ::operator delete(b);
    }
}

namespace {

// Fills a framework built by the original constructor with elements in every container and fake
// references; returns the blocks it owns (checked freed after the destructor).
struct Populated {
    void *object;
    FakeRef refs[4];
    FakeRef pool[64]; // the objects the containers hold
    char names[64][8];
    int used = 0;
    uint32_t Next()
    {
        std::snprintf(names[used], sizeof(names[used]), "e%d", used);
        InitRef(pool[used], names[used]);
        return reinterpret_cast<uint32_t>(&pool[used++]);
    }
    std::vector<void *> blocks;
};

void Populate(Populated &p, bool derived)
{
    char *o = static_cast<char *>(p.object);
    InitRef(p.refs[0], "notification");
    InitRef(p.refs[1], "stdin");
    InitRef(p.refs[2], "stderr");
    InitRef(p.refs[3], "stdout");
    // The real notification object (made by the constructor) is dropped: its count would go to 0.
    ::operator delete(At<void *>(p.object, 0x168));
    At<void *>(p.object, 0x168) = &p.refs[0];
    At<void *>(p.object, 0x160) = &p.refs[1];
    At<void *>(p.object, 0x15c) = &p.refs[2];
    At<void *>(p.object, 0x158) = &p.refs[3];
    p.blocks.reserve(128);
    auto listOf = [&](size_t off, int n) {
        IdList &l = At<IdList>(p.object, off);
        for (int i = 0; i < n; ++i) l.push_back(p.Next());
        p.blocks.push_back(At<void *>(p.object, off));
        for (auto &v : l) p.blocks.push_back(reinterpret_cast<char *>(&v) - 8);
    };
    // A command line with heap-allocated text and arguments (rebuilt with the verified class).
    auto *cmd = reinterpret_cast<cRZCmdLine *>(o + 0xfc);
    cmd->Destruct();
    cmd->Construct("prog.exe -flag value another_long_argument_to_force_a_heap_allocation_in_the_string");
    p.blocks.push_back(cmd->mArgs.mFirst);
    for (cRZString *arg = cmd->mArgs.mFirst; arg != cmd->mArgs.mLast; ++arg) {
        if (arg->mString.capacity() > 15) p.blocks.push_back(*reinterpret_cast<void **>(reinterpret_cast<char *>(arg) + 4));
    }
    if (cmd->mText.mString.capacity() > 15) p.blocks.push_back(*reinterpret_cast<void **>(reinterpret_cast<char *>(&cmd->mText) + 4));
    listOf(0x7c, 2);
    listOf(0xac, 3);
    listOf(0xb4, 1);
    listOf(0xbc, 2);
    listOf(0xc4, 4);
    IdMultimap &services = At<IdMultimap>(p.object, 0x84);
    for (int i = 0; i < 3; ++i) services.insert({i * 7, {static_cast<uint32_t>(i), p.Next()}});
    p.blocks.push_back(At<void *>(p.object, 0x84));
    for (auto &kv : services) p.blocks.push_back(reinterpret_cast<char *>(&kv) - 0x10);
    IdHash &byId = At<IdHash>(p.object, 0x8c);
    for (uint32_t i = 0; i < 20; ++i) byId.insert({i * 31, p.Next()}); // forces a rehash
    p.blocks.push_back(At<void *>(p.object, 0x90));
    p.blocks.push_back(At<void *>(p.object, 0x98));
    for (auto &kv : byId) p.blocks.push_back(reinterpret_cast<char *>(&kv) - 8);
    // The embedded COM object's containers and lock.
    p.blocks.push_back(At<void *>(p.object, 0x3c));
    p.blocks.push_back(At<void *>(p.object, 0x44));
    p.blocks.push_back(At<void *>(p.object, 0x30));
    p.blocks.push_back(At<void *>(p.object, 0xf8)); // the framework's own lock block
    if (derived) {
        p.blocks.push_back(At<void *>(p.object, 0x184));
        p.blocks.push_back(At<void *>(p.object, 0x18c));
        for (auto &v : At<std::set<uint32_t>>(p.object, 0x184)) p.blocks.push_back(reinterpret_cast<char *>(const_cast<uint32_t *>(&v)) - 0x10);
        for (auto &v : At<std::set<uint32_t>>(p.object, 0x18c)) p.blocks.push_back(reinterpret_cast<char *>(const_cast<uint32_t *>(&v)) - 0x10);
    }
    (void)o;
}

void RunDestructTest(difftest::Context &ctx, bool derived, uint32_t originalRva, const char *what)
{
    uint32_t *clockStart = ctx.image.At<uint32_t>(0x10dfcd0);
    uint32_t saved[2] = {clockStart[0], clockStart[1]};
    size_t size = derived ? kCtgSize : kBaseSize;
    std::string results[2];
    std::vector<unsigned char> bytes[2];
    for (int side = 0; side < 2; ++side) {
        auto *p = new Populated{};
        clockStart[0] = clockStart[1] = 0xffffffff;
        p->object = BuildObject(size, 0xCD, [&](void *o) {
            ctx.Original<void *(__thiscall *)(void *)>(derived ? kCtgCtor : kBaseCtor)(o);
        });
        clockStart[0] = saved[0];
        clockStart[1] = saved[1];
        Populate(*p, derived);
        gLog.clear();
        difftest::FreeRecorder rec;
        if (side == 0) {
            ctx.Original<void(__thiscall *)(void *)>(originalRva)(p->object);
        } else if (derived) {
            gz::cCTGFrameworkAdapters::Destruct(static_cast<gz::cCTGFramework *>(p->object), nullptr);
        } else {
            gz::cGZFrameworkAdapters::Destruct(static_cast<gz::cGZFramework *>(p->object), nullptr);
        }
        rec.Stop();
        int live = 0;
        for (void *block : p->blocks) live += rec.Freed(block) ? 0 : 1;
        ++ctx.cases;
        if (live) ctx.Fail("%s (%s): %d of %zu blocks still allocated", what, side ? "ours" : "original", live, p->blocks.size());
        results[side] = Joined(gLog);
        bytes[side] = CanonFw(p->object, size, side);
        ::operator delete(p->object);
        delete p;
    }
    ExpectSame(ctx, what, results[0], results[1]);
    ExpectSameBytes(ctx, what, bytes[0], bytes[1]);
}

} // namespace

DIFF_TEST(GZFramework_DestructBase, 0x141f5c)
{
    RunDestructTest(ctx, false, kBaseDtor, "base destructor");
}

DIFF_TEST(GZFramework_Destruct, 0xc2ce90)
{
    RunDestructTest(ctx, true, kCtgDtor, "destructor");
}

// gz::cCTGFramework's deleting destructor (slot 40, 0xc2cec9; lane EX): the original on an original-built object; ours on
// the adapter route (an original-built object, the hook's adapter) and the vtable route (our object from our constructor,
// our vtable's slot 40, the compiler's scalar deleting destructor). Bytes compared when the object survives (flags 0).
DIFF_TEST(GZFramework_DeletingDestructor, 0xc2cec9)
{
    uint32_t *clockStart = ctx.image.At<uint32_t>(0x10dfcd0);
    uint32_t saved[2] = {clockStart[0], clockStart[1]};
    for (uint32_t flags : {0u, 1u, 2u, 3u, 0xfffffffeu, 0xffffffffu}) {
        void *objects[3];
        void *rets[3];
        bool live[3];
        std::vector<unsigned char> bytes[3];
        for (int side = 0; side < 3; ++side) {
            clockStart[0] = clockStart[1] = 0xffffffff;
            if (side == 2) {
                objects[side] = BuildObject(kCtgSize, 0xCD, [&](void *o) { gz::cCTGFrameworkAdapters::Construct(o, nullptr); });
            } else {
                objects[side] = BuildObject(kCtgSize, 0xCD, [&](void *o) { ctx.Original<void *(__thiscall *)(void *)>(kCtgCtor)(o); });
            }
            clockStart[0] = saved[0];
            clockStart[1] = saved[1];
            difftest::FreeRecorder rec;
            if (side == 0) {
                rets[0] = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtgDeleting)(objects[0], flags);
            } else if (side == 1) {
                rets[1] = gz::cCTGFrameworkAdapters::DeletingDestructor(static_cast<cIGZFrameWork *>(static_cast<gz::cCTGFramework *>(objects[1])), nullptr, flags);
            } else {
                void **vt = *static_cast<void ***>(objects[2]);
                rets[2] = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(vt[40])(objects[2], flags);
            }
            rec.Stop();
            live[side] = !rec.Freed(objects[side]);
            if (live[side]) bytes[side] = CanonFw(objects[side], kCtgSize, side == 0 ? kOriginal : kOurs);
        }
        for (int side = 1; side < 3; ++side) {
            ++ctx.cases;
            if (rets[0] != objects[0] || rets[side] != objects[side]) ctx.Fail("deleting destructor %x (route %d): wrong return", flags, side);
            if (live[0] != live[side] || live[0] != ((flags & 1) == 0))
                ctx.Fail("deleting destructor %x (route %d): freed original=%d ours=%d", flags, side, !live[0], !live[side]);
            if (live[0] && live[side]) ExpectSameBytes(ctx, "deleting destructor: bytes", bytes[0], bytes[side]);
        }
        if (!(flags & 1)) {
            for (void *object : objects) ::operator delete(object);
        }
    }
}

// gz::cGZFramework's deleting destructor (slot 40, 0x142207; lane ER): the destructor, then the sized delete (0x180) on
// bit 0, returning this. The original on a base object; ours on the vtable route (our object, our vtable's slot 40, the
// compiler's scalar deleting destructor) and the adapter route (an original-built object, the hook's adapter).
DIFF_TEST(GZFramework_DeletingDestructorBase, 0x142207)
{
    uint32_t *clockStart = ctx.image.At<uint32_t>(0x10dfcd0);
    uint32_t saved[2] = {clockStart[0], clockStart[1]};
    for (uint32_t flags : {0u, 1u, 2u, 3u, 0xfffffffeu, 0xffffffffu}) {
        void *objects[3];
        void *rets[3];
        bool live[3];
        std::vector<unsigned char> bytes[3];
        for (int side = 0; side < 3; ++side) {
            clockStart[0] = clockStart[1] = 0xffffffff;
            if (side == 2) {
                objects[side] = BuildObject(kBaseSize, 0xCD, [&](void *o) { gz::cGZFrameworkAdapters::Construct(o, nullptr); });
            } else {
                objects[side] = BuildObject(kBaseSize, 0xCD, [&](void *o) { ctx.Original<void *(__thiscall *)(void *)>(kBaseCtor)(o); });
            }
            clockStart[0] = saved[0];
            clockStart[1] = saved[1];
            difftest::FreeRecorder rec;
            if (side == 0) {
                rets[0] = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x142207)(objects[0], flags);
            } else if (side == 1) {
                rets[1] = gz::cGZFrameworkAdapters::DeletingDestructor(static_cast<cIGZFrameWork *>(objects[1]), nullptr, flags);
            } else {
                void **vt = *static_cast<void ***>(objects[2]);
                rets[2] = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(vt[40])(objects[2], flags);
            }
            rec.Stop();
            live[side] = !rec.Freed(objects[side]);
            if (live[side]) bytes[side] = CanonFw(objects[side], kBaseSize, side == 0 ? kOriginal : kOurs);
        }
        for (int side = 1; side < 3; ++side) {
            ++ctx.cases;
            if (rets[0] != objects[0] || rets[side] != objects[side]) ctx.Fail("deleting destructor %x (route %d): wrong return", flags, side);
            if (live[0] != live[side] || live[0] != ((flags & 1) == 0))
                ctx.Fail("deleting destructor %x (route %d): freed original=%d ours=%d", flags, side, !live[0], !live[side]);
            if (live[0] && live[side]) ExpectSameBytes(ctx, "deleting destructor: bytes", bytes[0], bytes[side]);
        }
        if (!(flags & 1)) {
            for (void *object : objects) ::operator delete(object);
        }
    }
}
