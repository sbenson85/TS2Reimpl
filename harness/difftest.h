#pragma once
// Minimal differential test framework. A test calls the original (via Original()) and the
// reimplementation on the same inputs and reports mismatches with Fail().
#include "free_recorder.h"
#include "image.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace difftest {

struct Context {
    const OriginalImage &image;
    uint64_t cases = 0;
    std::vector<std::string> failures;

    // Typed pointer to an original function by RVA, e.g. Original<uint32_t(__thiscall *)(void *)>(0x1b8d75).
    template <typename Fn> Fn Original(uint32_t rva) const { return reinterpret_cast<Fn>(image.At(rva)); }

    // Optional: names where the test is (e.g. ctx.Mark("ours: seed 17")); if the runner dies, its report shows the last
    // mark with ctx.cases (harness/monitor.h). Copied, at most 95 characters.
    void Mark(const char *label);

    template <typename... Args> void Fail(const char *fmt, Args... args)
    {
        if (failures.size() < 20) {
            char buf[512];
            std::snprintf(buf, sizeof(buf), fmt, args...);
            failures.emplace_back(buf);
        } else if (failures.size() == 20) {
            failures.emplace_back("(further failures omitted)");
        }
    }
};

// The last drawfx::Note label (a string literal) and how many notes the test has made so far; the error line of a crash
// in the original or ours prints them ("last note"). The runner clears both before each test.
void NoteBreadcrumb(const char *label);

// Fills `bytes` of the stack below the caller with 0xCDCDCDCD (noinline; the frames of the calls after it sit on it), to
// make reads of uninitialised locals visible. The runner calls it before each test when TS2_STACK_FILL is set
// (difftest.ps1 -StackFill); a test may call it before each side of a case.
__declspec(noinline) void FillStackBelow(unsigned bytes = 65536);

struct Test {
    const char *name;
    uint32_t rva; // original function under test
    std::function<void(Context &)> body;
    const char *file; // the source file that registered it (--filter also matches its stem)
};

std::vector<Test> &Registry();

// Fixtures that can explain a failure register here (tests/norm_audit.h: a word normalised on one side only). The runner
// calls every begin() before each test, and every report() after it: `failed` says whether the test failed, and the lines
// a report adds go to the test's failures (only when it failed; a passing test's lines are printed as notes when the
// environment sets TS2_FAILURE_NOTES, for sweeps). The lines errors() adds are failures whatever else happened (an unregistered
// pointer or an undeclared word the fakes logged: tests/norm_words.h).
struct FailureNotes {
    void (*begin)();
    void (*report)(bool failed, std::vector<std::string> &out);
    void (*errors)(std::vector<std::string> &out) = nullptr;
};
inline std::vector<FailureNotes> &FailureNoteSources()
{
    static std::vector<FailureNotes> sources;
    return sources;
}

// Per-run temporary directory (with trailing backslash) where the original may use files; file
// imports anywhere else trap. Our code should use the same directory. Deleted when the run ends.
const std::wstring &SandboxDir();
std::wstring SandboxPath(const std::wstring &name);
void EmptySandbox(); // remove everything in it
// CreateFileW on a sandbox file, registering the handle so original code may use it too.
void *SandboxCreateFile(const std::wstring &name, uint32_t access, uint32_t creation, uint32_t sharing = 3);
// Closes a handle from SandboxCreateFile (if original code hasn't closed it already).
void SandboxCloseFile(void *handle);
// The sandboxed version of a KERNEL32 file function (e.g. "CreateFileW"), for tests that hand the
// original a function pointer; nullptr if it isn't sandboxed.
void *SandboxFunction(const char *name);

// x87 precision control (docs/lane/test.md, "x87 precision"). The game creates its Direct3D 9 device without
// D3DCREATE_FPU_PRESERVE, so D3D9 sets the creating thread's x87 to 24-bit precision; the harness runs at the CRT's
// 53-bit. Precision control rounds the significand of fadd, fsub, fmul, fdiv and fsqrt (not the exponent range, loads,
// stores or transcendentals), so it changes what the original's x87 code computes, never our SSE2 code.
// TS2_X87_PC=24 (or 53, 64) runs every test at that precision (applied by the runner around the whole test, both sides).
enum class X87Pc : unsigned { Bits24 = 0x20000, Bits53 = 0x10000, Bits64 = 0x00000 }; // _PC_24, _PC_53, _PC_64 (CRT encoding)
// Sets the precision for a scope and restores the control word after it.
class X87Precision {
public:
    explicit X87Precision(X87Pc pc);
    ~X87Precision();
    X87Precision(const X87Precision &) = delete;
    X87Precision &operator=(const X87Precision &) = delete;

private:
    unsigned saved_;
};
// The precision the game's x87 code runs at, for tests whose outcome depends on it (difftest::AtGamePrecision): 24-bit
// once the in-game probe (TS2HOST_X87PROBE) has confirmed it (kGameX87PcConfirmed in harness/main.cpp), until then the
// harness's 53-bit unless TS2_X87_GAME=1 asks for 24.
X87Pc GameX87Precision();
struct AtGamePrecision : X87Precision {
    AtGamePrecision() : X87Precision(GameX87Precision()) {}
};

struct Registrar {
    // file: the caller's source file (for macros and runtime registrations alike, the test file).
    Registrar(const char *name, uint32_t rva, std::function<void(Context &)> body, const char *file = __builtin_FILE())
    {
        Registry().push_back({name, rva, std::move(body), file});
    }
};

} // namespace difftest

#define DIFF_TEST_CAT2(a, b) a##b
#define DIFF_TEST_CAT(a, b) DIFF_TEST_CAT2(a, b)
// DIFF_TEST(name, rva) { ...body using ctx... }
#define DIFF_TEST(name, rva)                                                                         \
    static void DIFF_TEST_CAT(DiffTest_, name)(difftest::Context & ctx);                              \
    static difftest::Registrar DIFF_TEST_CAT(DiffTestReg_, name)(#name, rva, DIFF_TEST_CAT(DiffTest_, name)); \
    static void DIFF_TEST_CAT(DiffTest_, name)(difftest::Context & ctx)
