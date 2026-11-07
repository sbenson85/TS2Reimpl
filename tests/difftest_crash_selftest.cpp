// Self-test of the runner's crash report (harness/monitor.h): each kind makes the runner die (or, for `so`, overflow
// the stack twice) in its own way. Registered only when DIFFTEST_CRASH_SELFTEST names a kind, so normal runs don't see
// these tests:
//   $env:DIFFTEST_CRASH_SELFTEST='gs'; .\tools\difftest.ps1 -Filter CrashSelftest -Summary
// Kinds: gs fastfail abort terminate purecall invparam so sehchain heap exit; adt checks the adt helpers instead.
#include "difftest.h"
#include "app_debugger_fakes.h"

#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

volatile int gSink;

__declspec(noinline) void SmashOwnFrame(size_t n)
{
    char buffer[16];
    std::memset(buffer, 0x41, n); // past the buffer: the /GS cookie check at the return fails
    gSink = buffer[3];
}

__declspec(noinline) void ThrowFrom() { throw std::runtime_error("selftest"); }
__declspec(noinline) void NoexceptThrow() noexcept { ThrowFrom(); }

struct Base {
    Base() { Call(); }
    __declspec(noinline) void Call() { Pure(); }
    virtual void Pure() = 0;
    virtual ~Base() = default;
};
struct Derived : Base {
    void Pure() override {}
};

#pragma warning(push)
#pragma warning(disable : 4717) // recursive on all paths: that is the point
__declspec(noinline) int Recurse(int depth)
{
    volatile char pad[512];
    pad[0] = static_cast<char>(depth);
    return Recurse(depth + 1) + pad[0];
}
#pragma warning(pop)

void Run(difftest::Context &ctx, const std::string &kind)
{
    for (int i = 0; i < 3; ++i) ++ctx.cases;
    ctx.Mark(("ours: " + kind).c_str());
    if (kind == "gs") SmashOwnFrame(64 + gSink);
    else if (kind == "fastfail") __fastfail(7);
    else if (kind == "abort") std::abort();
    else if (kind == "terminate") NoexceptThrow();
    else if (kind == "purecall") { Derived d; }
    else if (kind == "invparam") std::fclose(nullptr);
    else if (kind == "so") gSink = Recurse(0);
    else if (kind == "sehchain") {
        __writefsdword(0, 0x10); // the SEH chain now points at unmapped memory
        *reinterpret_cast<volatile int *>(0x20) = 1;
    } else if (kind == "heap") {
        HANDLE heap = HeapCreate(0, 0, 0);
        auto *p = static_cast<uint8_t *>(HeapAlloc(heap, 0, 32));
        std::memset(p - 8, 0x5a, 48); // the block's header and the next one's
        HeapFree(heap, 0, p);
        HeapDestroy(heap);
    } else if (kind == "exit") std::exit(0);
    else if (kind == "adt") { // not a crash: the adt helpers (labels, long vtables)
        adt::Fake plain(adt::kOther), labelled(adt::kOther, "seg1");
        if (adt::Tag(plain.get()) != "<other>" || adt::Tag(labelled.get()) != "<seg1>") ctx.Fail("adt labels");
        adt::Def(adt::kOther, 54, 1, "i", [](void *, const uint32_t *args) { return args[0] + 1; });
        auto vt = adt::BuildVTable<64>(adt::kOther);
        adt::Fake object(adt::kOther, "obj");
        object.object.vtable = vt.data();
        using Slot = uint32_t(__thiscall *)(void *, uint32_t);
        adt::gLog.clear();
        if (reinterpret_cast<Slot>(vt[54])(object.get(), 41) != 42 || adt::gLog.size() != 1 || adt::gLog[0] != "other.54(<obj>, 0x29)")
            ctx.Fail("adt slot 54: %s", adt::gLog.empty() ? "no log" : adt::gLog[0].c_str());
        if (adt::BuildVTable(adt::kOther).size() != 48) ctx.Fail("default size");
        adt::gSlots[adt::kOther][54] = adt::SlotDef();
    }
    else ctx.Fail("unknown DIFFTEST_CRASH_SELFTEST kind %s", kind.c_str());
}

struct Register {
    Register()
    {
        const char *kind = std::getenv("DIFFTEST_CRASH_SELFTEST");
        if (!kind) return;
        const std::string k = kind;
        difftest::Registrar("CrashSelftest_before", 0, [](difftest::Context &ctx) { ++ctx.cases; });
        difftest::Registrar(k == "so" ? "CrashSelftest_so1" : "CrashSelftest_crash", 0,
                            [k](difftest::Context &ctx) { Run(ctx, k); });
        if (k == "so") {
            difftest::Registrar("CrashSelftest_so2", 0, [k](difftest::Context &ctx) { Run(ctx, k); });
            difftest::Registrar("CrashSelftest_after", 0, [](difftest::Context &ctx) { ++ctx.cases; });
        }
    }
} gRegister;

} // namespace
