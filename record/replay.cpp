#include "replay.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

namespace record {
namespace {

constexpr uint32_t kMagic = 0x43325354; // "TS2C"
constexpr uint32_t kVersion = 4;
constexpr uint32_t kChunk = 0x10000;    // allocation granularity
constexpr uint8_t kPattern = 0xCD;      // unrecorded bytes in replayed regions
constexpr DWORD kArmCode = 0xE0545352;  // raised in the child to set the boundary breakpoint
constexpr DWORD kDisarmCode = 0xE0545353;

template <typename T> void Put(std::vector<uint8_t> &out, const T &v)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

struct Reader {
    const std::vector<uint8_t> &data;
    size_t pos = 0;
    bool ok = true;
    template <typename T> T Get()
    {
        T v{};
        if (pos + sizeof(T) > data.size()) {
            ok = false;
            return v;
        }
        std::memcpy(&v, data.data() + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }
};

void PutBytes(std::vector<uint8_t> &out, const Map<uint32_t, uint8_t> &m)
{
    Put(out, static_cast<uint32_t>(m.size()));
    for (auto &[a, v] : m) {
        Put(out, a);
        Put(out, v);
    }
}

void GetBytes(Reader &r, Map<uint32_t, uint8_t> &m)
{
    const uint32_t n = r.Get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok; ++i) {
        const uint32_t a = r.Get<uint32_t>();
        m[a] = r.Get<uint8_t>();
    }
}

bool InImage(const CaptureFile &f, uint32_t a) { return a >= f.imageBase && a - f.imageBase < f.imageSize; }

// The 64 KB chunks outside the image that a replay needs: every recorded byte, and the capture stack.
std::set<uint32_t> Chunks(const CaptureFile &f)
{
    std::set<uint32_t> chunks;
    for (auto *m : {&f.capture.reads, &f.capture.before})
        for (auto &[a, v] : *m)
            if (!InImage(f, a)) chunks.insert(a & ~(kChunk - 1));
    for (auto &b : f.capture.boundaries)
        for (auto &[a, v] : b.writes)
            if (!InImage(f, a)) chunks.insert(a & ~(kChunk - 1));
    for (uint32_t c = kStackBottom; c < kStackBottom + kStackSize; c += kChunk) chunks.insert(c);
    return chunks;
}

// What a chunk holds before the call: the pattern, then the recorded values read.
std::vector<uint8_t> ExpectedBefore(const CaptureFile &f, uint32_t chunk)
{
    std::vector<uint8_t> bytes(kChunk, kPattern);
    for (auto it = f.capture.reads.lower_bound(chunk); it != f.capture.reads.end() && it->first < chunk + kChunk; ++it)
        bytes[it->first - chunk] = it->second;
    return bytes;
}

// The TEB's stack bounds (NT_TIB StackBase and StackLimit), switched while running on the capture stack.
struct StackBounds {
    uint32_t base, limit;
};
StackBounds SwapStackBounds(StackBounds b)
{
    StackBounds old{__readfsdword(4), __readfsdword(8)};
    __writefsdword(4, b.base);
    __writefsdword(8, b.limit);
    return old;
}

void *CaptureStack()
{
    static void *stack = VirtualAlloc(reinterpret_cast<void *>(kStackBottom), kStackSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return stack;
}

__declspec(naked) void __cdecl CallOnStack(void (*)(void *), void *, void *)
{
    __asm {
        push ebp
        mov ebp, esp
        mov eax, [ebp + 16]
        mov ecx, [ebp + 8]
        mov edx, [ebp + 12]
        mov esp, eax
        push ebp
        push edx
        call ecx
        add esp, 4
        pop ebp
        mov esp, ebp
        pop ebp
        ret
    }
}

// Replay call state (child). Registers go in through gIn; RunCaptured switches to the capture's
// esp with ReturnStub as the return address, and ReturnStub switches back.
struct Regs {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, target, fpuControl, mxcsr;
} gIn;
uint32_t gSavedEsp, gOutEax, gOutEdx, gOutEsp;
uint32_t gSeh, gSavedSeh; // the SEH chain head during the call (as captured), and ours
volatile bool gReplaying, gFaulted;
uint32_t gFaultCode, gFaultAddress, gFaultTarget;

__declspec(naked) void ReturnStub()
{
    __asm {
        mov gOutEax, eax
        mov gOutEdx, edx
        mov gOutEsp, esp
        mov esp, gSavedEsp
        mov eax, gSavedSeh
        mov dword ptr fs:[0], eax
        popfd
        popad
        ret
    }
}

// Where a faulting call resumes (ReplayVeh): back on our stack and SEH chain.
__declspec(naked) void FaultStub()
{
    __asm {
        mov esp, gSavedEsp
        mov eax, gSavedSeh
        mov dword ptr fs:[0], eax
        popfd
        popad
        ret
    }
}

// Boundary calls (calls that left the image when captured) are not run: a debug-register breakpoint
// on the next one's target stops the call there, and the handler applies what the capture recorded
// (registers, esp, the bytes it changed, an x87 push) and resumes at its return address.
const Vector<BoundaryCall> *gBoundaries;
size_t gNextBoundary;
bool gOurs; // running a replacement: its frames differ, so boundaries match by target and order only
// Code a replayed call's boundary calls come from: the image, and (for a replacement) our exe.
uint32_t gImageLo, gImageHi, gOwnLo, gOwnHi;
volatile LONG gHandlerDepth; // ReplayVeh frames active on this thread

bool CalledFromReplayedCode(const CONTEXT *ctx)
{
    const uint32_t ret = *reinterpret_cast<const uint32_t *>(ctx->Esp);
    if (ret >= gImageLo && ret < gImageHi) return true;
    return gOurs && ret >= gOwnLo && ret < gOwnHi;
}
char gDiverged[200];

// Targets the image reaches through its import table (HeapAlloc, the SRW locks...) are system code
// that the exception dispatcher runs too, so a breakpoint on them would fire inside the dispatcher.
// Their import slots point at one int3 stub each instead; other targets (vtable calls into our
// exe's fakes) get the debug-register breakpoint.
uint8_t *gStubs;
Vector<uint32_t> gStubTargets;

bool HasStub(uint32_t target)
{
    for (uint32_t t : gStubTargets)
        if (t == target) return true;
    return false;
}

void ArmBoundary(CONTEXT *ctx)
{
    const bool on = gBoundaries && gNextBoundary < gBoundaries->size() && !HasStub((*gBoundaries)[gNextBoundary].target);
    ctx->Dr0 = on ? (*gBoundaries)[gNextBoundary].target : 0;
    ctx->Dr7 = (ctx->Dr7 & ~0x000F0003u) | (on ? 1u : 0u);
}

// Points the image's import slots at stubs.
void RedirectImports(uintptr_t imageBase)
{
    auto *base = reinterpret_cast<uint8_t *>(imageBase);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY &iat = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    if (!iat.VirtualAddress) return;
    auto *slots = reinterpret_cast<uint32_t *>(base + iat.VirtualAddress);
    const uint32_t count = iat.Size / 4;
    // Every slot, not only the boundary targets: an import the capture didn't call then ends the
    // replay as a divergence instead of running real system code on recorded pointers.
    for (uint32_t s = 0; s < count; ++s) {
        const uint32_t target = slots[s];
        if (!target || HasStub(target) || gStubTargets.size() >= 0x1000) continue;
        const uint32_t stubsLo = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gStubs));
        if (gStubs && target - stubsLo < 0x1000) continue; // already a stub
        if (!gStubs) {
            gStubs = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
            if (!gStubs) return;
            std::memset(gStubs, 0xCC, 0x1000);
        }
        const uint32_t stub = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gStubs)) + static_cast<uint32_t>(gStubTargets.size());
        gStubTargets.push_back(target);
        for (uint32_t i = s; i < count; ++i)
            if (slots[i] == target) slots[i] = stub;
    }
}

// Pushes a recorded st0 onto the x87 stack in both register images of the context.
void PushX87(CONTEXT *ctx, const uint8_t (&value)[10])
{
    const uint32_t top = (ctx->FloatSave.StatusWord >> 11) & 7, newTop = (top - 1) & 7;
    ctx->FloatSave.StatusWord = (ctx->FloatSave.StatusWord & ~0x3800u) | (newTop << 11);
    ctx->FloatSave.TagWord &= ~(3u << (newTop * 2));
    std::memmove(ctx->FloatSave.RegisterArea + 10, ctx->FloatSave.RegisterArea, 70);
    std::memcpy(ctx->FloatSave.RegisterArea, value, 10);
    if ((ctx->ContextFlags & CONTEXT_EXTENDED_REGISTERS) == CONTEXT_EXTENDED_REGISTERS) {
        uint8_t *fx = ctx->ExtendedRegisters;
        auto &fsw = *reinterpret_cast<uint16_t *>(fx + 2);
        fsw = static_cast<uint16_t>((fsw & ~0x3800u) | (newTop << 11));
        fx[4] |= static_cast<uint8_t>(1u << newTop);
        std::memmove(fx + 48, fx + 32, 7 * 16);
        std::memset(fx + 32, 0, 16);
        std::memcpy(fx + 32, value, 10);
    }
}

// Ends the replayed call early with a verdict (resumes at FaultStub).
void Diverge(CONTEXT *ctx, const char *why)
{
    std::snprintf(gDiverged, sizeof(gDiverged), "%s", why);
    gReplaying = false;
    gFaulted = true;
    ctx->Eip = reinterpret_cast<uintptr_t>(&FaultStub);
}

// The call reached the next boundary's target: skip the boundary.
void EmulateBoundary(CONTEXT *ctx)
{
    const BoundaryCall &b = (*gBoundaries)[gNextBoundary];
    const uint32_t ret = *reinterpret_cast<uint32_t *>(ctx->Esp);
    if (!gOurs && (ctx->Esp != b.esp || ret != b.returnAddr)) {
        char why[200];
        std::snprintf(why, sizeof(why), "boundary call %zu to 0x%x made with esp 0x%lx return 0x%x, captured esp 0x%x return 0x%x",
                      gNextBoundary, b.target, ctx->Esp, ret, b.esp, b.returnAddr);
        return Diverge(ctx, why);
    }
    for (auto &[a, v] : b.writes) *reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a)) = v;
    ctx->Eax = b.eax;
    ctx->Ecx = b.ecx;
    ctx->Edx = b.edx;
    ctx->Esp += b.retEsp - b.esp;
    ctx->Eip = ret;
    if (b.fpu == 1) PushX87(ctx, b.st0);
    ++gNextBoundary;
    ArmBoundary(ctx);
}

// The SEH chain head is the captured one during the call, so faults are caught here, first.
// Only hardware faults: other exceptions (C++ throws) go to the call's own handlers.
LONG ReplayVehBody(EXCEPTION_POINTERS *info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    // Under WOW64 a debug trap in some system code arrives as STATUS_WX86_SINGLE_STEP.
    if (code == 0x4000001E) code = EXCEPTION_SINGLE_STEP;
    CONTEXT *ctx = info->ContextRecord;
    if (code == kArmCode || code == kDisarmCode) {
        if (code == kDisarmCode) gBoundaries = nullptr;
        ArmBoundary(ctx);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    const bool atBoundary = gReplaying && gBoundaries && gNextBoundary < gBoundaries->size()
                            && ctx->Eip == (*gBoundaries)[gNextBoundary].target;
    const uint32_t at = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));
    const uint32_t stubs = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gStubs));
    if (code == EXCEPTION_BREAKPOINT && gStubs && at - stubs < gStubTargets.size()) {
        // An import call through a redirected slot.
        const uint32_t target = gStubTargets[at - stubs];
        ctx->Eip = target;
        if (!gReplaying) return EXCEPTION_CONTINUE_EXECUTION; // not the replayed call: run the import
        if (gNextBoundary < gBoundaries->size() && (*gBoundaries)[gNextBoundary].target == target) {
            EmulateBoundary(ctx);
        } else {
            char why[200];
            std::snprintf(why, sizeof(why), "import call to 0x%x where boundary call %zu was to 0x%x", target, gNextBoundary,
                          gNextBoundary < gBoundaries->size() ? (*gBoundaries)[gNextBoundary].target : 0u);
            Diverge(ctx, why);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && (ctx->Dr6 & 1)) {
        ctx->Dr6 = 0;
        // Boundary targets are often system functions (HeapAlloc...) that the exception dispatcher,
        // this handler and the replay process itself also call: only the replayed code's call is
        // the boundary. Others run, past the breakpoint (RF).
        if (atBoundary && gHandlerDepth == 1 && CalledFromReplayedCode(ctx)) EmulateBoundary(ctx);
        else ctx->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && gReplaying && gHandlerDepth == 1) {
        char why[200];
        std::snprintf(why, sizeof(why), "unexpected single step at 0x%lx (dr6 0x%lx, dr0 0x%lx, dr7 0x%lx, next boundary %zu)", ctx->Eip,
                      ctx->Dr6, ctx->Dr0, ctx->Dr7, gNextBoundary);
        Diverge(ctx, why);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (atBoundary && code == EXCEPTION_ACCESS_VIOLATION && gHandlerDepth == 1 && CalledFromReplayedCode(ctx)) {
        // Executing an unmapped target (should the breakpoint not fire).
        EmulateBoundary(ctx);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (!gReplaying || (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION
                        && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW))
        return EXCEPTION_CONTINUE_SEARCH;
    gReplaying = false;
    gFaulted = true;
    gFaultCode = code;
    gFaultAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));
    gFaultTarget = info->ExceptionRecord->NumberParameters >= 2 ? static_cast<uint32_t>(info->ExceptionRecord->ExceptionInformation[1]) : 0;
    info->ContextRecord->Eip = reinterpret_cast<uintptr_t>(&FaultStub);
    return EXCEPTION_CONTINUE_EXECUTION;
}

LONG CALLBACK ReplayVeh(EXCEPTION_POINTERS *info)
{
    InterlockedIncrement(&gHandlerDepth);
    const LONG result = ReplayVehBody(info);
    InterlockedDecrement(&gHandlerDepth);
    return result;
}

__declspec(naked) void RunCaptured()
{
    __asm {
        pushad
        pushfd
        mov gSavedEsp, esp
        mov eax, dword ptr fs:[0]
        mov gSavedSeh, eax
        mov eax, gSeh
        mov dword ptr fs:[0], eax
        mov esp, gIn.esp
        mov dword ptr [esp], offset ReturnStub
        cld
        fldcw word ptr gIn.fpuControl
        ldmxcsr gIn.mxcsr
        mov eax, gIn.eax
        mov ebx, gIn.ebx
        mov ecx, gIn.ecx
        mov edx, gIn.edx
        mov esi, gIn.esi
        mov edi, gIn.edi
        mov ebp, gIn.ebp
        jmp gIn.target
    }
}

LONG CALLBACK LastChance(EXCEPTION_POINTERS *info)
{
    std::printf("fail: crashed: exception 0x%08lx at 0x%p (accessing 0x%x)\n", info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress,
                info->ExceptionRecord->NumberParameters >= 2 ? static_cast<uint32_t>(info->ExceptionRecord->ExceptionInformation[1]) : 0);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 1);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Installed before the TEB is patched: nothing between the patch and the restore may use our own
// CRT's thread state (function-local statics, errno), which the patched TLS pointer would redirect.
void InstallReplayHandlers()
{
    SetUnhandledExceptionFilter(LastChance);
    AddVectoredExceptionHandler(1, ReplayVeh);
}

bool RunGuardedCapture(DWORD &code)
{
    gFaulted = false;
    gDiverged[0] = 0;
    gNextBoundary = 0;
    RaiseException(kArmCode, 0, 0, nullptr);
    gReplaying = true;
    RunCaptured();
    gReplaying = false;
    RaiseException(kDisarmCode, 0, 0, nullptr);
    code = gFaultCode;
    return !gFaulted;
}

} // namespace

bool WriteCapture(const std::wstring &path, const CaptureFile &f)
{
    std::vector<uint8_t> out;
    const Capture &c = f.capture;
    Put(out, kMagic);
    Put(out, kVersion);
    for (uint32_t v : {f.rva, f.imageBase, f.imageSize, c.eax, c.ebx, c.ecx, c.edx, c.esi, c.edi, c.ebp, c.esp, c.eflags,
                       static_cast<uint32_t>(c.fpuControl), c.mxcsr, c.retEax, c.retEdx, c.retEsp})
        Put(out, v);
    Put(out, c.steps);
    PutBytes(out, c.reads);
    PutBytes(out, c.before);
    PutBytes(out, c.writes);
    PutBytes(out, c.tebReads);
    PutBytes(out, c.code);
    Put(out, static_cast<uint32_t>(c.boundaries.size()));
    for (auto &b : c.boundaries) {
        for (uint32_t v : {b.target, b.returnAddr, b.esp, b.eax, b.ecx, b.edx, b.retEsp}) Put(out, v);
        Put(out, b.fpu);
        for (uint8_t v : b.st0) Put(out, v);
        PutBytes(out, b.writes);
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

bool ReadCapture(const std::wstring &path, CaptureFile &f, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Reader r{data};
    if (r.Get<uint32_t>() != kMagic || r.Get<uint32_t>() != kVersion) {
        error = "not a version 4 capture file";
        return false;
    }
    Capture &c = f.capture;
    uint32_t *fields[] = {&f.rva, &f.imageBase, &f.imageSize, &c.eax, &c.ebx, &c.ecx, &c.edx, &c.esi, &c.edi, &c.ebp, &c.esp, &c.eflags};
    for (uint32_t *p : fields) *p = r.Get<uint32_t>();
    c.fpuControl = static_cast<uint16_t>(r.Get<uint32_t>());
    c.mxcsr = r.Get<uint32_t>();
    c.retEax = r.Get<uint32_t>();
    c.retEdx = r.Get<uint32_t>();
    c.retEsp = r.Get<uint32_t>();
    c.steps = r.Get<uint64_t>();
    GetBytes(r, c.reads);
    GetBytes(r, c.before);
    GetBytes(r, c.writes);
    GetBytes(r, c.tebReads);
    GetBytes(r, c.code);
    const uint32_t n = r.Get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok; ++i) {
        BoundaryCall b;
        b.target = r.Get<uint32_t>();
        b.returnAddr = r.Get<uint32_t>();
        b.esp = r.Get<uint32_t>();
        b.eax = r.Get<uint32_t>();
        b.ecx = r.Get<uint32_t>();
        b.edx = r.Get<uint32_t>();
        b.retEsp = r.Get<uint32_t>();
        b.fpu = r.Get<uint8_t>();
        for (uint8_t &v : b.st0) v = r.Get<uint8_t>();
        GetBytes(r, b.writes);
        c.boundaries.push_back(std::move(b));
    }
    if (!r.ok) error = "truncated capture file";
    return r.ok;
}

void RunOnCaptureStack(void (*fn)(void *), void *arg)
{
    auto *bottom = static_cast<uint8_t *>(CaptureStack());
    if (!bottom) return fn(arg); // the address is taken: capture on the normal stack
    const StackBounds old = SwapStackBounds({kStackBottom + kStackSize, kStackBottom});
    CallOnStack(fn, arg, bottom + kStackSize - 16);
    SwapStackBounds(old);
}

namespace {
ReplayResult ReplayOnce(const std::wstring &exe, const std::wstring &childArgs, const std::wstring &capturePath, bool &collided);
}

ReplayResult ReplayInChildProcess(const std::wstring &exe, const std::wstring &childArgs, const std::wstring &capturePath)
{
    // A fresh process puts its initial thread stacks somewhere different each time, so a capture
    // whose memory collides with them usually fits in another attempt.
    ReplayResult result;
    for (int attempt = 0; attempt < 8; ++attempt) {
        bool collided = false;
        result = ReplayOnce(exe, childArgs, capturePath, collided);
        if (!collided) break;
    }
    return result;
}

namespace {
ReplayResult ReplayOnce(const std::wstring &exe, const std::wstring &childArgs, const std::wstring &capturePath, bool &collided)
{
    ReplayResult result;
    CaptureFile f;
    if (!ReadCapture(capturePath, f, result.detail)) return result;
    for (auto &b : f.capture.boundaries) {
        if (b.fpu == 2) {
            result.detail = "a boundary call changes the x87 stack other than by one push";
            return result;
        }
    }
    if (f.capture.esp < kStackBottom || f.capture.esp >= kStackBottom + kStackSize) {
        result.detail = "not captured on the capture stack";
        return result;
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.detail = "CreatePipe failed";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + childArgs;
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        result.detail = "CreateProcess failed";
        return result;
    }
    CloseHandle(writePipe);

    auto skip = [&](const std::string &why) {
        TerminateProcess(pi.hProcess, 3);
        result.outcome = ReplayOutcome::Skipped;
        result.detail = why;
    };
    char buf[96];
    bool placed = VirtualAllocEx(pi.hProcess, reinterpret_cast<void *>(static_cast<uintptr_t>(f.imageBase)), f.imageSize, MEM_RESERVE, PAGE_READWRITE) != nullptr;
    if (!placed) {
        std::snprintf(buf, sizeof(buf), "image range 0x%x is taken in the child", f.imageBase);
        skip(buf);
        collided = true;
    }
    for (uint32_t chunk : placed ? Chunks(f) : std::set<uint32_t>{}) {
        void *at = reinterpret_cast<void *>(static_cast<uintptr_t>(chunk));
        // Our own exe is mapped at the same address in the child: the child writes the recorded
        // bytes of its globals itself, after its static initialisers have run.
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQueryEx(pi.hProcess, at, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE) continue;
        if (!VirtualAllocEx(pi.hProcess, at, kChunk, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
            std::snprintf(buf, sizeof(buf), "address 0x%x is taken in the child", chunk);
            skip(buf);
            collided = true;
            placed = false;
            break;
        }
        const std::vector<uint8_t> bytes = ExpectedBefore(f, chunk);
        WriteProcessMemory(pi.hProcess, at, bytes.data(), bytes.size(), nullptr);
    }
    if (placed) {
        ResumeThread(pi.hThread);
        if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            result.outcome = ReplayOutcome::Fail;
            result.detail = "timed out";
        }
    }
    std::string output;
    DWORD got = 0;
    char chunkBuf[512];
    while (ReadFile(readPipe, chunkBuf, sizeof(chunkBuf), &got, nullptr) && got) output.append(chunkBuf, got);
    DWORD code = 3;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(readPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (!placed) return result;
    if (result.detail.empty()) {
        // The verdict is the child's last line.
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
        const size_t nl = output.find_last_of('\n');
        result.detail = nl == std::string::npos ? output : output.substr(nl + 1);
        result.outcome = code == 0 ? ReplayOutcome::Pass : code == 3 ? ReplayOutcome::Skipped : ReplayOutcome::Fail;
        if (result.detail.empty()) {
            std::snprintf(buf, sizeof(buf), "child exited 0x%lx with no verdict", code);
            result.detail = buf;
            result.outcome = ReplayOutcome::Fail;
        }
    }
    return result;
}

} // namespace

int ReplayInChild(uintptr_t imageBase, const std::wstring &capturePath, void *replacement)
{
    CaptureFile f;
    std::string error;
    if (!ReadCapture(capturePath, f, error)) {
        std::printf("skip: %s\n", error.c_str());
        return 3;
    }
    gOurs = replacement != nullptr;
    if (gOurs) {
        // A boundary's writes into the original's own frame (out-parameters in its locals) have no
        // counterpart in the replacement's frame.
        for (auto &b : f.capture.boundaries)
            for (auto &[a, v] : b.writes)
                if (a < f.capture.esp + 4 && a >= f.capture.esp - 0x100000) {
                    std::printf("skip: a boundary call writes into the original's own frame\n");
                    return 3;
                }
    }
    if (f.imageBase != imageBase) {
        std::printf("skip: image at 0x%x, captured at 0x%x\n", static_cast<uint32_t>(imageBase), f.imageBase);
        return 3;
    }
    // The image's recorded bytes (globals the call read); the rest of the replay memory was placed
    // by the parent.
    DWORD oldProtect = 0;
    VirtualProtect(reinterpret_cast<void *>(imageBase), f.imageSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    for (auto &[a, v] : f.capture.reads)
        if (InImage(f, a)) *reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a)) = v;
    for (auto &[a, v] : f.capture.code)
        if (InImage(f, a)) *reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a)) = v;
    // Recorded bytes in our own exe's image (test globals), which the parent couldn't place.
    for (auto &[a, v] : f.capture.reads) {
        if (InImage(f, a)) continue;
        MEMORY_BASIC_INFORMATION mbi{};
        void *at = reinterpret_cast<void *>(static_cast<uintptr_t>(a));
        if (!VirtualQuery(at, &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) continue;
        DWORD prot = 0;
        VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &prot);
        *static_cast<uint8_t *>(at) = v;
    }
    for (auto &b : f.capture.boundaries) {
        for (auto &[a, v] : b.writes) {
            MEMORY_BASIC_INFORMATION mbi{};
            void *at = reinterpret_cast<void *>(static_cast<uintptr_t>(a));
            DWORD prot = 0;
            if (!InImage(f, a) && VirtualQuery(at, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE)
                VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &prot);
        }
    }
    gBoundaries = &f.capture.boundaries;
    RedirectImports(imageBase);
    gImageLo = f.imageBase;
    gImageHi = f.imageBase + f.imageSize;
    {
        auto *own = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(own + reinterpret_cast<const IMAGE_DOS_HEADER *>(own)->e_lfanew);
        gOwnLo = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(own));
        gOwnHi = gOwnLo + nt->OptionalHeader.SizeOfImage;
    }
    const std::set<uint32_t> chunks = Chunks(f);

    gIn = {f.capture.eax, f.capture.ebx, f.capture.ecx, f.capture.edx, f.capture.esi, f.capture.edi, f.capture.ebp,
           f.capture.esp, gOurs ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement)) : static_cast<uint32_t>(imageBase) + f.rva,
           f.capture.fpuControl,
           f.capture.mxcsr ? f.capture.mxcsr : 0x1f80};
    // The TEB bytes the call read (the TLS array pointer and the like), except the SEH chain head,
    // which our exception guard needs, the stack bounds (set below) and the self pointer.
    auto *teb = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(__readfsdword(0x18)));
    InstallReplayHandlers();
    std::vector<std::pair<uint32_t, uint8_t>> tebSaved;
    tebSaved.reserve(f.capture.tebReads.size());
    gSeh = __readfsdword(0);
    for (uint32_t b = 0; b < 4; ++b) {
        auto it = f.capture.tebReads.find(b);
        if (it != f.capture.tebReads.end()) reinterpret_cast<uint8_t *>(&gSeh)[b] = it->second;
    }
    for (auto &[off, v] : f.capture.tebReads) {
        if (off < 0xc || (off >= 0x18 && off < 0x1c)) continue;
        tebSaved.emplace_back(off, teb[off]);
        teb[off] = v;
    }
    const StackBounds old = SwapStackBounds({kStackBottom + kStackSize, kStackBottom});
    DWORD code = 0;
    const bool returned = RunGuardedCapture(code);
    SwapStackBounds(old);
    for (auto &[off, v] : tebSaved) teb[off] = v;
    if (gDiverged[0]) {
        std::printf("fail: %s\n", gDiverged);
        return 1;
    }
    if (returned && gNextBoundary != f.capture.boundaries.size()) {
        std::printf("fail: made %zu of %zu boundary calls\n", gNextBoundary, f.capture.boundaries.size());
        return 1;
    }
    if (!returned) {
        // Self-check failure: the capture missed something the call needs.
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(gFaultTarget)), &mbi, sizeof(mbi));
        std::printf("skip: self-check: exception 0x%08lx at 0x%x (accessing 0x%x: state 0x%lx, protect 0x%lx; %zu of %zu boundary calls made)\n",
                    code, gFaultAddress, gFaultTarget, mbi.State, mbi.Protect, gNextBoundary, f.capture.boundaries.size());
        return 3;
    }

    // Compare: return registers, recorded writes, and no other byte changed in the replay chunks.
    char line[256];
    if (gOutEsp != f.capture.retEsp) {
        std::snprintf(line, sizeof(line), "fail: esp after return 0x%x, captured 0x%x\n", gOutEsp, f.capture.retEsp);
        std::fputs(line, stdout);
        return 1;
    }
    // A replacement is held to eax only, and separately: a void function leaves whatever it last
    // computed there.
    if (gOurs && gOutEax != f.capture.retEax) {
        std::printf("fail: eax only: returned 0x%x, captured 0x%x\n", gOutEax, f.capture.retEax);
        return 1;
    }
    if (!gOurs && (gOutEax != f.capture.retEax || gOutEdx != f.capture.retEdx)) {
        std::printf("fail: returned eax=0x%x edx=0x%x, captured eax=0x%x edx=0x%x (seh 0x%x, %zu teb bytes read)\n", gOutEax, gOutEdx,
                    f.capture.retEax, f.capture.retEdx, gSeh, f.capture.tebReads.size());
        return 1;
    }
    for (auto &[a, v] : f.capture.writes) {
        const uint8_t now = *reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a));
        if (now != v) {
            std::printf("fail: byte 0x%x is %02x, captured %02x\n", a, now, v);
            return 1;
        }
    }
    for (uint32_t chunk : chunks) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(chunk)), &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE)
            continue; // our exe's globals: only the recorded writes are checked
        std::vector<uint8_t> expected = ExpectedBefore(f, chunk);
        for (auto &b : f.capture.boundaries)
            for (auto it = b.writes.lower_bound(chunk); it != b.writes.end() && it->first < chunk + kChunk; ++it)
                expected[it->first - chunk] = it->second;
        for (auto it = f.capture.writes.lower_bound(chunk); it != f.capture.writes.end() && it->first < chunk + kChunk; ++it)
            expected[it->first - chunk] = it->second;
        const auto *now = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(chunk));
        for (uint32_t i = 0; i < kChunk; ++i) {
            const uint32_t a = chunk + i;
            if (a < gOutEsp && a >= kStackBottom) continue; // the call's own frame and popped arguments
            if (now[i] != expected[i]) {
                std::printf("fail: unrecorded write at 0x%x (%02x, expected %02x)\n", a, now[i], expected[i]);
                return 1;
            }
        }
    }
    std::printf("pass: %llu steps, %zu bytes read, %zu written\n", f.capture.steps, f.capture.reads.size(), f.capture.writes.size());
    return 0;
}

} // namespace record
