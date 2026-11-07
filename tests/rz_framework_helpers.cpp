// Differential tests for the framework helpers in rz/RZFrameworkHelpers.h. Objects carry recording
// vtables (AddRef/Release and list visits are logged in order), so side effects are compared as well
// as return values. Each helper is compared against the original on fresh copies of the same input.
#include "difftest.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstdarg>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// Recording vtable slots. The callees take no stack arguments (or just the one a thiscall passes),
// so __fastcall with a dummy edx matches the thiscall convention.
std::vector<int> gEvents;
void *gCountedSlots[3];

struct Counted {
    void *vt;
    uint32_t refs;
    int id;
};

void *__fastcall RecAddRef(void *self, void * /*edx*/)
{
    auto *o = static_cast<Counted *>(self);
    gEvents.push_back(o->id * 4 + 1);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(++o->refs));
}

void *__fastcall RecRelease(void *self, void * /*edx*/)
{
    auto *o = static_cast<Counted *>(self);
    gEvents.push_back(o->id * 4 + 2);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(--o->refs));
}

void SetupCountedSlots()
{
    gCountedSlots[1] = reinterpret_cast<void *>(&RecAddRef);
    gCountedSlots[2] = reinterpret_cast<void *>(&RecRelease);
}

// List nodes for the visitor test: prev at +0, next at +4.
struct Node {
    void *prev;
    void *next;
};
Node *gNodes = nullptr;
std::vector<int> gVisited;

// Slot 3 of the visitor's host: records the node index and clears its next pointer, so a visitor
// that re-reads next after the call would diverge.
void *gHostSlots[4];
void __fastcall RecVisit(void *self, void * /*edx*/, void *node)
{
    (void)self;
    gVisited.push_back(static_cast<int>(static_cast<Node *>(node) - gNodes));
    static_cast<Node *>(node)->next = nullptr;
}

struct Host {
    void *vt;
};

// Strings over a small alphabet, with NULs and lengths around the 15-char small-string boundary.
std::string RandomText(std::mt19937 &rng)
{
    static const char alphabet[] = {'a', 'B', '\0', 'z', '_'};
    int len = std::uniform_int_distribution<int>(0, 40)(rng);
    if (rng() % 4 == 0) len = std::uniform_int_distribution<int>(14, 17)(rng);
    std::string s;
    for (int i = 0; i < len; ++i) s += alphabet[rng() % 5];
    return s;
}

// Index of a counted object in `objs`, or -1 for null / 9 for anything else.
int IndexOf(const void *p, const Counted *objs)
{
    if (!p) return -1;
    for (int k = 0; k < 3; ++k)
        if (p == &objs[k]) return k;
    return 9;
}

} // namespace

// AddRef the new object, store it, Release the old one. Self-assignment does nothing.
DIFF_TEST(RZFramework_UnknownSlotAssign, 0x142080)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0x142080);
    SetupCountedSlots();
    std::mt19937 rng(11);
    for (int i = 0; i < 40000; ++i) {
        uint32_t refs[3] = {rng() % 4, rng() % 4, rng() % 4};
        int initial = static_cast<int>(rng() % 4) - 1; // -1 is null
        int next = static_cast<int>(rng() % 4) - 1;
        int result[2];
        std::vector<int> events[2];
        uint32_t finalRefs[2][3];
        for (int run = 0; run < 2; ++run) {
            Counted objs[3];
            for (int k = 0; k < 3; ++k) objs[k] = {gCountedSlots, refs[k], k};
            RZUnknownSlot holder{initial < 0 ? nullptr : &objs[initial]};
            void *value = next < 0 ? nullptr : &objs[next];
            gEvents.clear();
            void *ret;
            if (run == 0) ret = orig(&holder, value);
            else ret = static_cast<RZUnknownSlot *>(&holder)->Assign(value);
            result[run] = ret == &holder ? IndexOf(holder.mObject, objs) : 100;
            events[run] = gEvents;
            for (int k = 0; k < 3; ++k) finalRefs[run][k] = objs[k].refs;
            if (ret != &holder) ctx.Fail("UnknownSlot::Assign returned a different slot");
        }
        ++ctx.cases;
        if (result[0] != result[1] || events[0] != events[1]) {
            ctx.Fail("UnknownSlot::Assign initial %d next %d: slot %d/%d, %zu/%zu events", initial, next, result[0], result[1],
                     events[0].size(), events[1].size());
        }
        for (int k = 0; k < 3; ++k)
            if (finalRefs[0][k] != finalRefs[1][k]) ctx.Fail("UnknownSlot::Assign refs differ for object %d", k);
    }
}

DIFF_TEST(RZFramework_GlobalGetter, 0x1427b5)
{
    auto orig = ctx.Original<uint32_t(__cdecl *)()>(0x1427b5);
    auto *global = ctx.image.At<uint32_t>(0x10c9378);
    uint32_t saved = *global;
    std::mt19937 rng(12);
    for (int i = 0; i < 2000; ++i) {
        uint32_t value = i < 2 ? static_cast<uint32_t>(i) : static_cast<uint32_t>(rng());
        *global = value;
        ++ctx.cases;
        if (orig() != RZGetGlobal10C9378()) ctx.Fail("global read differs for %08x", value);
    }
    *global = saved;
}

// riid 1 stores this and AddRefs it; other ids fail and leave the out pointer alone.
DIFF_TEST(RZFramework_UnknownQueryInterface, 0x1b945c)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x1b945c);
    SetupCountedSlots();
    std::mt19937 rng(13);
    const uint32_t fixedIds[] = {0, 1, 2, 3, 0x89b7dc8, 0xffffffffu};
    for (int i = 0; i < 20000; ++i) {
        uint32_t riid = i < 6 ? fixedIds[i] : (rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng()));
        uint32_t refs = rng() % 4;
        bool result[2];
        int stored[2];
        std::vector<int> events[2];
        for (int run = 0; run < 2; ++run) {
            Counted obj{gCountedSlots, refs, 0};
            void *out = reinterpret_cast<void *>(0x12345678);
            gEvents.clear();
            if (run == 0) result[run] = orig(&obj, riid, &out);
            else result[run] = reinterpret_cast<RZUnknownQueryInterface *>(&obj)->QueryInterfaceUnknown(riid, &out);
            stored[run] = out == reinterpret_cast<void *>(&obj) ? 1 : (out == reinterpret_cast<void *>(0x12345678) ? 0 : 2);
            events[run] = gEvents;
            if (obj.refs != refs + (result[run] ? 1 : 0)) ctx.Fail("QueryInterface refcount differs");
        }
        ++ctx.cases;
        if (result[0] != result[1] || stored[0] != stored[1] || events[0] != events[1])
            ctx.Fail("QueryInterface riid %08x: %d/%d, stored %d/%d", riid, result[0], result[1], stored[0], stored[1]);
    }
}

// The string assignment copies (data, size) with self-assignment skipped, so the capacity and
// contents match the original exactly.
DIFF_TEST(RZFramework_FormattedErrorCtor, 0x1d099e)
{
    using OrigCtor = void *(__cdecl *)(void *, const char *, ...);
    auto orig = ctx.Original<OrigCtor>(0x1d099e);
    std::mt19937 rng(15);
    std::string longText(1900, 'q');
    for (int i = 0; i < 4000; ++i) {
        alignas(8) unsigned char a[32], b[32];
        for (auto &x : a) x = static_cast<unsigned char>(rng());
        std::memcpy(b, a, sizeof(a));
        std::string text = RandomText(rng);
        for (auto &c : text) if (c == '\0') c = 'n'; // NUL would end the formatted text early
        int k = i % 8;
        // Each case runs the same format on both sides, so the original and ours see identical varargs.
        void *ra = nullptr, *rb = nullptr;
        switch (k) {
        case 0: ra = orig(a, "plain text"); rb = RZFormattedErrorCtor(b, "plain text"); break;
        case 1: { int v = static_cast<int>(rng()); ra = orig(a, "value %d", v); rb = RZFormattedErrorCtor(b, "value %d", v); break; }
        case 2: ra = orig(a, "%s=%s", text.c_str(), "x"); rb = RZFormattedErrorCtor(b, "%s=%s", text.c_str(), "x"); break;
        case 3: ra = orig(a, "%.3f %x", 1.5, 255u); rb = RZFormattedErrorCtor(b, "%.3f %x", 1.5, 255u); break;
        case 4: ra = orig(a, "%s", longText.c_str()); rb = RZFormattedErrorCtor(b, "%s", longText.c_str()); break;
        case 5: ra = orig(a, ""); rb = RZFormattedErrorCtor(b, ""); break;
        case 6: ra = orig(a, "%c%%", 'z'); rb = RZFormattedErrorCtor(b, "%c%%", 'z'); break;
        default: ra = orig(a, "%s", text.c_str()); rb = RZFormattedErrorCtor(b, "%s", text.c_str()); break;
        }
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("formatted error ctor returned a different object");
        if (std::memcmp(a, b, 4) != 0) ctx.Fail("formatted error ctor vtable differs");
        auto *sa = reinterpret_cast<std::string *>(a + 4);
        auto *sb = reinterpret_cast<std::string *>(b + 4);
        if (sa->size() != sb->size() || sa->capacity() != sb->capacity() || *sa != *sb) {
            ctx.Fail("formatted error ctor message differs (k=%d): '%s' vs '%s'", k, sa->c_str(), sb->c_str());
        }
        sa->~basic_string();
        sb->~basic_string();
    }
}

// Visits the nodes after the head in list order, then the head. Null or empty lists visit only
// what the original does.
DIFF_TEST(RZFramework_ListNodeVisitor, 0x1be23b)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x1be23b);
    for (int k = 0; k < 3; ++k) gHostSlots[k] = nullptr;
    gHostSlots[3] = reinterpret_cast<void *>(&RecVisit);
    Host hostA{gHostSlots}, hostB{gHostSlots};
    std::mt19937 rng(16);
    for (int i = 0; i < 20000; ++i) {
        int n = static_cast<int>(rng() % 8); // number of nodes after the head (0 is an empty list)
        bool nullHead = i % 50 == 0;
        std::vector<int> order(n);
        for (int k = 0; k < n; ++k) order[k] = k + 1;
        for (int k = n - 1; k > 0; --k) std::swap(order[k], order[rng() % (k + 1)]);

        std::vector<int> visited[2];
        for (int run = 0; run < 2; ++run) {
            Node nodes[9];
            gNodes = nodes;
            gVisited.clear();
            void *head = nullHead ? nullptr : &nodes[0];
            if (!nullHead) {
                // Circular list: head, then the permutation, then back to head.
                Node *prev = &nodes[0];
                for (int k = 0; k < n; ++k) {
                    Node *cur = &nodes[order[k]];
                    prev->next = cur;
                    cur->prev = prev;
                    prev = cur;
                }
                prev->next = &nodes[0];
                nodes[0].prev = prev;
            }
            if (run == 0) orig(&hostA, head);
            else reinterpret_cast<RZListNodeVisitor *>(&hostB)->VisitNodesThenHead(head);
            visited[run] = gVisited;
        }
        ++ctx.cases;
        if (visited[0] != visited[1]) {
            ctx.Fail("list visit differs (n=%d): %zu vs %zu visits", n, visited[0].size(), visited[1].size());
        }
    }
}

DIFF_TEST(RZFramework_UnknownLayoutConstruct, 0x1b9052)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1b9052);
    std::mt19937 rng(17);
    for (int i = 0; i < 5000; ++i) {
        alignas(8) unsigned char a[16], b[16];
        for (auto &x : a) x = static_cast<unsigned char>(rng());
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a);
        void *rb = reinterpret_cast<RZUnknownLayout *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("cRZUnknown construct returned a different this");
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("cRZUnknown construct differs");
    }
}

DIFF_TEST(RZFramework_WinProc4SetVTable, 0xb6d930)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xb6d930);
    std::mt19937 rng(18);
    for (int i = 0; i < 2000; ++i) {
        alignas(8) unsigned char a[8], b[8];
        for (auto &x : a) x = static_cast<unsigned char>(rng());
        std::memcpy(b, a, sizeof(a));
        orig(a);
        reinterpret_cast<RZWinProc4Vtable *>(b)->SetVTable();
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("cRZWinProc4 vtable store differs");
    }
}

// cRZWinProc4's constructor: the whole 12-byte buffer is compared (bytes past the object included).
DIFF_TEST(RZFramework_WinProc4Construct, 0xb6d920)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xb6d920);
    std::mt19937 rng(19);
    for (int i = 0; i < 2000; ++i) {
        alignas(8) unsigned char a[12], b[12];
        for (auto &x : a) x = static_cast<unsigned char>(rng());
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a);
        void *rb = reinterpret_cast<RZWinProc4Vtable *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("cRZWinProc4 constructor returned a different this");
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("cRZWinProc4 constructor differs");
    }
}

DIFF_TEST(RZFramework_EnclosedFieldGetters, 0x9e8850)
{
    auto dword = ctx.Original<uint32_t(__thiscall *)(void *)>(0x9e8850);
    auto byte = ctx.Original<uint8_t(__thiscall *)(void *)>(0x9d9570);
    std::mt19937 rng(19);
    for (int i = 0; i < 20000; ++i) {
        alignas(8) unsigned char buf[32];
        for (auto &x : buf) x = static_cast<unsigned char>(rng());
        void *self = buf + 12;
        ++ctx.cases;
        if (dword(self) != reinterpret_cast<RZEnclosedFields *>(self)->GetDwordMinus12()) ctx.Fail("dword at this-12 differs");
        // Only al is defined for a byte result; the other bits of eax are whatever the caller left.
        if (byte(self) != reinterpret_cast<RZEnclosedFields *>(self)->GetByteMinus8()) ctx.Fail("byte at this-8 differs");
    }
}
