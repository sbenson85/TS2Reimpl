// Differential tests for cRZExceptionNotification in the standalone shape (src/rz/cRZExceptionNotification.h).
//
// Original side: 8 bytes on the heap (junk first), built by the original constructor 0x1c8c84, functions called at
// their RVAs. Our side: built by our constructor (the Construct adapter, as the constructor hook does, or `new`), every
// slot called through its own vtable, in the slot where the game's vtable holds the RVA (SlotOf). MessageBoxA is the
// game's import (IAT 0xd9a568), patched to a recorder for the tests: it logs its window, text, caption and flags, and
// answers from the case (or raises an SEH exception, which the original's catch(...), built /EHa, catches). Compared:
// the result, the out-parameter, the count, the vtable word (the game's or ours: one code), whether the object was freed
// (FreeRecorder), and the log.
#include "difftest.h"
#include "free_recorder.h"
#include "rz/cRZExceptionNotification.h"
#include "shape.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kVTableRva = cRZExceptionNotification::kVTable;
constexpr unsigned kSlots = 5;
constexpr uint32_t kIatMessageBoxA = 0xd9a568;
constexpr uint32_t kUntouched = 0x5a5a5a5a;
constexpr DWORD kRaised = 0xe0484856;

std::string gLog;
int gAnswer = 6;

int WINAPI RecordingMessageBox(HWND window, const char *text, const char *caption, UINT flags)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "box(%x,", static_cast<unsigned>(reinterpret_cast<uintptr_t>(window)));
    gLog += buf;
    gLog += text ? text : "(null)";
    gLog += "|";
    gLog += caption ? caption : "(null)";
    std::snprintf(buf, sizeof(buf), ",%x);", flags);
    gLog += buf;
    if (gAnswer == -1) RaiseException(kRaised, 0, 0, nullptr);
    return gAnswer;
}

struct ImportPatch {
    void **slot;
    void *saved;
    ImportPatch(uint32_t rva, void *fn) : slot(GameAddress<void *>(rva)), saved(*slot)
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &old);
        *slot = fn;
    }
    ~ImportPatch() { *slot = saved; }
};

void **OurVTable()
{
    static void **vt = [] {
        auto *o = new cRZExceptionNotification;
        void **v = shape::VTableAt(o, 0);
        delete o;
        return v;
    }();
    return vt;
}

uint32_t VtCode(uint32_t word)
{
    return word == shape::P(shape::GameVTable(kVTableRva)) || word == shape::P(OurVTable()) ? 1 : word;
}

template <class R, class... A> R CallSlot(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>(shape::VTableAt(object, 0)[slot])(object, args...);
}

void *Junk(std::mt19937 &rng)
{
    auto *w = static_cast<uint32_t *>(::operator new(8));
    w[0] = rng();
    w[1] = rng();
    return w;
}

// The side's object: the original constructor on junk, or ours.
void *Make(difftest::Context &ctx, int side, uint32_t count, std::mt19937 &rng)
{
    void *memory = Junk(rng);
    void *object = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x1c8c84)(memory)
                             : cRZExceptionNotificationAdapters::Construct(memory, nullptr);
    static_cast<uint32_t *>(object)[1] = count;
    return object;
}

struct Outcome {
    uint32_t result = 0, out = 0, count = 0, vt = 0;
    bool freed = false;
    size_t frees = 0;
    std::string log;
    bool operator==(const Outcome &o) const
    {
        return result == o.result && out == o.out && count == o.count && vt == o.vt && freed == o.freed && frees == o.frees &&
               log == o.log;
    }
};

template <class Call> Outcome Run(void *object, Call call)
{
    void *out = reinterpret_cast<void *>(kUntouched);
    gLog.clear();
    gLog.reserve(4096); // the recorders append: no reallocation inside the recorded window
    Outcome o;
    {
        difftest::FreeRecorder rec;
        o.result = call(object, &out);
        rec.Stop();
        o.freed = rec.Freed(object);
        o.frees = rec.Count();
    }
    o.out = out == object ? 1 : shape::P(out);
    o.log += gLog;
    if (!o.freed) {
        o.count = shape::Word(object, 4);
        o.vt = VtCode(shape::Word(object, 0));
        ::operator delete(object);
    }
    return o;
}

void Report(difftest::Context &ctx, const char *what, uint32_t a1, uint32_t a2, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a == b) return;
    ctx.Fail("%s (0x%x, 0x%x): original r=%x out=%x count=%x vt=%x freed=%d/%zu log=%s; ours r=%x out=%x count=%x vt=%x "
             "freed=%d/%zu log=%s",
             what, a1, a2, a.result, a.out, a.count, a.vt, a.freed, a.frees, a.log.c_str(), b.result, b.out, b.count, b.vt,
             b.freed, b.frees, b.log.c_str());
}

uint32_t PickCount(std::mt19937 &rng, int i)
{
    switch (i % 6) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 0xffffffffu;
    case 4: return 0x80000000u;
    default: return rng() % 8;
    }
}

uint32_t PickIid(std::mt19937 &rng, int i)
{
    switch (i % 5) {
    case 0: return cIGZExceptionNotification::kIid;
    case 1: return 1;
    case 2: return 0;
    case 3: return cIGZExceptionNotification::kIid + (rng() % 2 ? 1 : -1);
    default: return rng();
    }
}

// Module names: null (the default text), empty, short, with conversions (passed as text, not as a format), and long
// enough that the 200-byte box text is cut (the last byte is cleared).
std::string gLong(400, 'm');
const char *PickModule(std::mt19937 &rng, int i, char *scratch)
{
    switch (i % 7) {
    case 0: return nullptr;
    case 1: return "";
    case 2: return "cRZFile.cpp";
    case 3: return "%s%d%n";
    case 4: return gLong.c_str() + (rng() % 400);
    case 5: {
        // Exactly around the cut: "Module: " (8) + n + "\nLine: " (7) + digits.
        size_t n = 170 + rng() % 30;
        std::memset(scratch, 'a' + static_cast<char>(rng() % 26), n);
        scratch[n] = 0;
        return scratch;
    }
    default: return "C:\\Sims2\\Source\\cGZFramework.cpp";
    }
}

int32_t PickLine(std::mt19937 &rng, int i)
{
    switch (i % 4) {
    case 0: return 0;
    case 1: return -1;
    case 2: return static_cast<int32_t>(0x80000000u);
    default: return static_cast<int32_t>(rng());
    }
}

int PickAnswer(std::mt19937 &rng, int i)
{
    switch (i % 6) {
    case 0: return 6;  // IDYES
    case 1: return 7;  // IDNO
    case 2: return 0;
    case 3: return -1; // raises
    case 4: return 0x106;
    default: return static_cast<int>(rng() % 12);
    }
}

} // namespace

// The constructor (adapter, as the hook runs it) and our destructor (no hook: in effect through its callers) on junk,
// against 0x1c8c84 / 0x1c8c91.
DIFF_TEST(RZExceptionNotification_Construct, 0x1c8c84)
{
    std::mt19937 rng(0x48560010);
    for (int i = 0; i < 300; ++i) {
        uint32_t w0 = rng(), w1 = rng();
        Outcome o[2];
        for (int side = 0; side < 2; ++side) {
            auto *m = static_cast<uint32_t *>(::operator new(8));
            m[0] = w0;
            m[1] = w1;
            o[side] = Run(m, [&](void *obj, void **) {
                void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x1c8c84)(obj)
                                    : cRZExceptionNotificationAdapters::Construct(obj, nullptr);
                return r == obj ? 1u : shape::P(r);
            });
        }
        Report(ctx, "Construct", w0, w1, o[0], o[1]);
        // The destructor body (0x1c8c91) on an object built by each side's constructor, then junk in the count.
        for (int side = 0; side < 2; ++side) {
            void *obj = Make(ctx, side, w1, rng);
            o[side] = Run(obj, [&](void *p, void **) {
                if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x1c8c91)(p);
                else static_cast<cRZExceptionNotification *>(p)->cRZExceptionNotification::~cRZExceptionNotification();
                return 0u;
            });
        }
        Report(ctx, "Destruct", w1, 0, o[0], o[1]);
    }
}

// Every slot through our vtable, against the original at the slot's RVA.
DIFF_TEST(RZExceptionNotification_Slots, 0x1c8d3f)
{
    ImportPatch box(kIatMessageBoxA, reinterpret_cast<void *>(&RecordingMessageBox));
    const uint32_t kRvas[kSlots] = {0x1c8d3f, 0x148fb0, 0x1c8d77, 0x1c8cb7, 0x1c8c98};
    unsigned slot[kSlots];
    for (unsigned s = 0; s < kSlots; ++s) {
        slot[s] = shape::SlotOf(kVTableRva, kSlots, kRvas[s]);
        if (slot[s] == ~0u) {
            ctx.Fail("0x%x not in the game's vtable", kRvas[s]);
            return;
        }
    }
    std::mt19937 rng(0x48560011);
    char scratch[256];
    for (int i = 0; i < 700; ++i) {
        uint32_t count = PickCount(rng, i);
        uint32_t riid = PickIid(rng, i);
        uint32_t seed = rng();
        Outcome o[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            o[side] = Run(Make(ctx, side, count, r), [&](void *p, void **out) {
                return (side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x1c8d3f)(p, riid, out)
                                  : CallSlot<bool>(p, slot[0], riid, out)) & 0xff;
            });
        }
        Report(ctx, "QueryInterface", riid, count, o[0], o[1]);
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            o[side] = Run(Make(ctx, side, count, r), [&](void *p, void **) {
                return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x148fb0)(p) : CallSlot<uint32_t>(p, slot[1]);
            });
        }
        Report(ctx, "AddRef", count, 0, o[0], o[1]);
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            o[side] = Run(Make(ctx, side, count, r), [&](void *p, void **) {
                return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x1c8d77)(p) : CallSlot<uint32_t>(p, slot[2]);
            });
        }
        Report(ctx, "Release", count, 0, o[0], o[1]);
        const char *module = PickModule(rng, i, scratch);
        int32_t line = PickLine(rng, i / 7);
        gAnswer = PickAnswer(rng, i / 3);
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            o[side] = Run(Make(ctx, side, count, r), [&](void *p, void **) {
                return (side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const char *, int32_t)>(0x1c8cb7)(p, module, line)
                                  : CallSlot<bool>(p, slot[3], module, line)) & 0xff;
            });
        }
        Report(ctx, "Notify", static_cast<uint32_t>(line), static_cast<uint32_t>(gAnswer), o[0], o[1]);
        uint32_t flags = i % 4 == 3 ? rng() : i % 2;
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            o[side] = Run(Make(ctx, side, count, r), [&](void *p, void **) {
                void *res = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1c8c98)(p, flags)
                                      : CallSlot<void *>(p, slot[4], flags);
                return res == p ? 1u : shape::P(res);
            });
        }
        Report(ctx, "DeletingDestructor", flags, count, o[0], o[1]);
    }
}
