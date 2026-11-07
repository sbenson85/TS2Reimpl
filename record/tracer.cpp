#include "tracer.h"

#include <Zydis.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <new>
#include <set>

namespace record {
namespace {

constexpr DWORD kArmCode = 0xE0545341;    // raised to set the debug registers from the handler
constexpr DWORD kDisarmCode = 0xE0545344; // and to clear them
constexpr DWORD kTrapFlag = 0x100;
constexpr uint32_t kOwnStackSpan = 0x100000; // how far below the entry esp counts as the call's own stack

uintptr_t gLo = 0, gHi = 0;
uintptr_t gEntry = 0;
uint32_t gLimit = 0;
DWORD gThread = 0;
PVOID gHandler = nullptr;
ZydisDecoder gDecoder;
ZydisRegisterContext gRegs;

bool gTracing = false;
bool gInBoundary = false;
uint32_t gEntryEsp = 0, gReturnAddr = 0;
Capture gCurrent;
std::set<uint32_t, std::less<uint32_t>, PrivateAllocator<uint32_t>> gTebWritten;
Vector<Capture> gCaptures;
// What the call has seen of memory (bytes it read or wrote, its own frame included), as it last saw
// them: a boundary that changes one of these is recorded in its `writes`. Bytes the last instruction
// wrote are refreshed at the next step.
Map<uint32_t, uint8_t> gSeen;
Vector<uint32_t> gPending;
uint16_t gBoundaryFsw = 0;
// The call's frame ([esp, entry esp + 4)) when a boundary was entered: what the boundary wrote there
// (out-parameters in locals, which the call reads later and replay doesn't otherwise place).
Vector<uint8_t> gFrameSnapshot;
uint32_t gFrameSnapshotAt = 0;
// The image's executable sections as SetImage first saw them: (address, bytes) per section.
Vector<std::pair<uint32_t, Vector<uint8_t>>> gCodeBaseline;
uint64_t gMaxSteps = 0;
uint32_t gAttempts = 0; // calls traced since Arm, including abandoned ones
HANDLE gHeap = nullptr;
TraceStats gStats;

bool SafeRead(uintptr_t at, uint8_t &value)
{
    __try {
        value = *reinterpret_cast<volatile uint8_t *>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void RefreshPending()
{
    for (uint32_t at : gPending) {
        uint8_t now = 0;
        if (SafeRead(at, now)) gSeen[at] = now;
    }
    gPending.clear();
}

bool HasFxsave(const CONTEXT *ctx) { return (ctx->ContextFlags & CONTEXT_EXTENDED_REGISTERS) == CONTEXT_EXTENDED_REGISTERS; }

uint16_t StatusWord(const CONTEXT *ctx)
{
    if (HasFxsave(ctx)) return *reinterpret_cast<const uint16_t *>(ctx->ExtendedRegisters + 2);
    return static_cast<uint16_t>(ctx->FloatSave.StatusWord);
}

// At a boundary's return: the bytes it changed among those the call has seen, the registers it
// returned, and what it did to the x87 stack.
void EndBoundary(CONTEXT *ctx, BoundaryCall &call)
{
    call.eax = ctx->Eax;
    call.ecx = ctx->Ecx;
    call.edx = ctx->Edx;
    call.retEsp = ctx->Esp;
    const uint32_t topBefore = (gBoundaryFsw >> 11) & 7, topAfter = (StatusWord(ctx) >> 11) & 7;
    if (topAfter == ((topBefore - 1) & 7)) {
        call.fpu = 1;
        const uint8_t *st0 = HasFxsave(ctx) ? ctx->ExtendedRegisters + 32 : ctx->FloatSave.RegisterArea;
        for (int i = 0; i < 10; ++i) call.st0[i] = st0[i];
    } else if (topAfter != topBefore) {
        call.fpu = 2;
    }
    // The frame above the returned esp, against its snapshot.
    const auto *frame = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(gFrameSnapshotAt));
    for (uint32_t at = ctx->Esp; at < gEntryEsp + 4; ++at) {
        const uint32_t i = at - gFrameSnapshotAt;
        if (i >= gFrameSnapshot.size() || frame[i] == gFrameSnapshot[i]) continue;
        call.writes[at] = frame[i];
        gSeen[at] = frame[i];
    }
    for (auto it = gSeen.begin(); it != gSeen.end();) {
        const uint32_t at = it->first;
        uint8_t now = 0;
        // Below the esp is the boundary's dead frame (and now this handler's).
        if ((at < ctx->Esp && at >= gEntryEsp - kOwnStackSpan) || !SafeRead(at, now)) {
            it = gSeen.erase(it);
            continue;
        }
        if (now != it->second) {
            call.writes[at] = now;
            it->second = now;
        }
        ++it;
    }
}

uint32_t Teb() { return __readfsdword(0x18); }

// A byte of the TEB as the traced code sees it. The SEH chain head (offset 0) differs inside this
// handler: the exception dispatcher links its own records first, on the stack below the traced
// code's esp, so the traced code's head is the first record at or above its esp.
uint8_t TebByte(const CONTEXT *ctx, uint32_t off)
{
    if (off >= 4) return *reinterpret_cast<uint8_t *>(Teb() + off);
    uint32_t head = __readfsdword(0);
    while (head != 0xffffffff && head < ctx->Esp) head = *reinterpret_cast<uint32_t *>(head);
    return reinterpret_cast<uint8_t *>(&head)[off];
}

void SetEntryBreakpoint(CONTEXT *ctx, bool on)
{
    ctx->Dr0 = gEntry;
    ctx->Dr7 = (ctx->Dr7 & ~0x000F0003u) | (on ? 1u : 0u); // L0, execute, length 1
}

void SetBoundaryBreakpoint(CONTEXT *ctx, uint32_t at)
{
    ctx->Dr1 = at;
    ctx->Dr7 = (ctx->Dr7 & ~0x00F0000Cu) | (at ? 4u : 0u); // L1, execute, length 1
}

void Abandon(CONTEXT *ctx)
{
    ++gStats.abandoned;
    gTracing = gInBoundary = false;
    ctx->EFlags &= ~kTrapFlag;
    SetBoundaryBreakpoint(ctx, 0);
    SetEntryBreakpoint(ctx, gAttempts < gLimit);
}

void Finish(CONTEXT *ctx)
{
    Capture &c = gCurrent;
    // Writes below the esp after the return (the call's own argument slots, popped by `ret n`) are
    // dead, and this handler's frames have overwritten them by now.
    for (auto it = c.before.begin(); it != c.before.end();) {
        if (it->first < ctx->Esp && it->first >= gEntryEsp - kOwnStackSpan) it = c.before.erase(it);
        else ++it;
    }
    for (auto &[at, value] : c.before) {
        uint8_t now = 0;
        if (SafeRead(at, now)) c.writes[at] = now;
    }
    const uint32_t teb = Teb();
    for (uint32_t off : gTebWritten) c.tebWrites[off] = *reinterpret_cast<uint8_t *>(teb + off);
    c.retEax = ctx->Eax;
    c.retEdx = ctx->Edx;
    c.retEsp = ctx->Esp;
    gCaptures.push_back(std::move(c));
    ++gStats.captured;
    gTracing = false;
    ctx->EFlags &= ~kTrapFlag;
    SetEntryBreakpoint(ctx, gAttempts < gLimit);
}

// Records the memory the instruction at ctx->Eip is about to access, then lets it run one step.
void Step(CONTEXT *ctx)
{
    const uint32_t eip = ctx->Eip;
    RefreshPending();
    if (eip == gReturnAddr && ctx->Esp > gEntryEsp) return Finish(ctx);
    if (eip < gLo || eip >= gHi) {
        // Left the image (an import, or a call into code outside it): run it untraced and resume
        // at its return address.
        const uint32_t ret = *reinterpret_cast<uint32_t *>(ctx->Esp);
        if (ret < gLo || ret >= gHi) return Abandon(ctx);
        BoundaryCall call;
        call.target = eip;
        call.returnAddr = ret;
        call.esp = ctx->Esp;
        gCurrent.boundaries.push_back(std::move(call));
        gBoundaryFsw = StatusWord(ctx);
        gFrameSnapshotAt = ctx->Esp;
        const auto *frame = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(ctx->Esp));
        gFrameSnapshot.assign(frame, frame + (gEntryEsp + 4 - ctx->Esp));
        gInBoundary = true;
        ctx->EFlags &= ~kTrapFlag;
        SetBoundaryBreakpoint(ctx, ret);
        return;
    }

    if (gCurrent.steps >= gMaxSteps) {
        ++gStats.tooLong;
        return Abandon(ctx);
    }
    ZydisDecodedInstruction ins;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&gDecoder, reinterpret_cast<const void *>(eip), 15, &ins, ops))) {
        ++gStats.decodeErrors;
        return Abandon(ctx);
    }
    gRegs.values[ZYDIS_REGISTER_EAX] = ctx->Eax;
    gRegs.values[ZYDIS_REGISTER_EBX] = ctx->Ebx;
    gRegs.values[ZYDIS_REGISTER_ECX] = ctx->Ecx;
    gRegs.values[ZYDIS_REGISTER_EDX] = ctx->Edx;
    gRegs.values[ZYDIS_REGISTER_ESI] = ctx->Esi;
    gRegs.values[ZYDIS_REGISTER_EDI] = ctx->Edi;
    gRegs.values[ZYDIS_REGISTER_EBP] = ctx->Ebp;
    gRegs.values[ZYDIS_REGISTER_ESP] = ctx->Esp;
    gRegs.values[ZYDIS_REGISTER_EIP] = eip;

    for (uint32_t i = 0; i < ins.operand_count; ++i) {
        const ZydisDecodedOperand &op = ops[i];
        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.mem.type != ZYDIS_MEMOP_TYPE_MEM) continue;
        const bool reads = op.actions & ZYDIS_OPERAND_ACTION_MASK_READ;
        const bool writes = op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE;
        if (!reads && !writes) continue;
        ZyanU64 address = 0;
        if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddressEx(&ins, &op, eip, &gRegs, &address))) {
            ++gStats.unhandled[ZydisMnemonicGetString(ins.mnemonic)];
            continue;
        }
        const uint32_t size = op.size / 8;
        if (size == 0) {
            ++gStats.unhandled[ZydisMnemonicGetString(ins.mnemonic)];
            continue;
        }
        if (op.mem.segment == ZYDIS_REGISTER_FS) {
            // Thread state (SEH chain, TLS pointer), by TEB offset; replay sets the read values in
            // its own TEB around the call.
            for (uint32_t b = 0; b < size; ++b) {
                const uint32_t off = static_cast<uint32_t>(address) + b;
                if (reads && !gTebWritten.count(off) && off < 0x1000)
                    gCurrent.tebReads.emplace(off, TebByte(ctx, off));
                if (writes) gTebWritten.insert(off);
            }
            continue;
        }
        for (uint32_t b = 0; b < size; ++b) {
            const uint32_t at = static_cast<uint32_t>(address) + b;
            uint8_t value = 0;
            if (!SafeRead(at, value)) continue; // the instruction itself will fault
            if (reads) gSeen[at] = value;
            if (writes) gPending.push_back(at);
            if (at < gEntryEsp + 4 && at >= gEntryEsp - kOwnStackSpan) continue; // own frame
            if (reads && !gCurrent.before.count(at)) gCurrent.reads.emplace(at, value);
            if (writes) gCurrent.before.emplace(at, value);
        }
    }
    // popfd would clear the trap flag; keep it set in the value it loads.
    if (ins.mnemonic == ZYDIS_MNEMONIC_POPFD) *reinterpret_cast<uint32_t *>(ctx->Esp) |= kTrapFlag;
    ++gCurrent.steps;
    ++gStats.steps;
    ctx->EFlags |= kTrapFlag;
}

// Code bytes that differ from the baseline (patched by the test around this call).
void RecordCodePatches(Capture &c)
{
    for (auto &[at, bytes] : gCodeBaseline) {
        const auto *now = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(at));
        if (std::memcmp(now, bytes.data(), bytes.size()) == 0) continue;
        for (size_t i = 0; i < bytes.size(); ++i)
            if (now[i] != bytes[i]) c.code[at + static_cast<uint32_t>(i)] = now[i];
    }
}

void Begin(CONTEXT *ctx)
{
    gCurrent = Capture{};
    RecordCodePatches(gCurrent);
    gTebWritten.clear();
    gSeen.clear();
    gPending.clear();
    Capture &c = gCurrent;
    c.eax = ctx->Eax; c.ebx = ctx->Ebx; c.ecx = ctx->Ecx; c.edx = ctx->Edx;
    c.esi = ctx->Esi; c.edi = ctx->Edi; c.ebp = ctx->Ebp; c.esp = ctx->Esp; c.eflags = ctx->EFlags;
    c.fpuControl = static_cast<uint16_t>(ctx->FloatSave.ControlWord);
    if ((ctx->ContextFlags & CONTEXT_EXTENDED_REGISTERS) == CONTEXT_EXTENDED_REGISTERS)
        c.mxcsr = *reinterpret_cast<const uint32_t *>(ctx->ExtendedRegisters + 24);
    gEntryEsp = ctx->Esp;
    gReturnAddr = *reinterpret_cast<uint32_t *>(ctx->Esp);
    gTracing = true;
    gInBoundary = false;
    ++gAttempts;
    SetEntryBreakpoint(ctx, false); // recursive calls are traced as part of this one
    Step(ctx);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS *info)
{
    if (GetCurrentThreadId() != gThread) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *ctx = info->ContextRecord;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == kArmCode || code == kDisarmCode) {
        if (code == kDisarmCode && gTracing) Abandon(ctx);
        SetEntryBreakpoint(ctx, code == kArmCode);
        SetBoundaryBreakpoint(ctx, 0);
        gTracing = gInBoundary = false;
        ctx->EFlags &= ~kTrapFlag;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD dr6 = ctx->Dr6;
    ctx->Dr6 = 0;
    if (gTracing && gInBoundary && (dr6 & 2)) {
        BoundaryCall &call = gCurrent.boundaries.back();
        // The return address can also run inside the call (recursion through a callback); only the
        // return itself has popped the address.
        if (ctx->Eip != call.returnAddr || ctx->Esp <= call.esp || ctx->Esp > call.esp + 0x400) {
            ctx->EFlags |= 0x10000; // RF: step past the breakpoint
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        EndBoundary(ctx, call);
        gInBoundary = false;
        SetBoundaryBreakpoint(ctx, 0);
        Step(ctx);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (gTracing && !gInBoundary && (dr6 & 0x4000)) {
        Step(ctx);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (!gTracing && (dr6 & 1) && ctx->Eip == gEntry) {
        Begin(ctx);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void *PrivateAllocate(size_t bytes)
{
    if (!gHeap) gHeap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0);
    void *p = HeapAlloc(gHeap, 0, bytes);
    if (!p) throw std::bad_alloc();
    return p;
}

void PrivateFree(void *p) { HeapFree(gHeap, 0, p); }

void SetImage(uintptr_t lo, uintptr_t hi)
{
    if (lo != gLo || hi != gHi) {
        gCodeBaseline.clear();
        const auto *base = reinterpret_cast<const uint8_t *>(lo);
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
        const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint8_t *at = base + section->VirtualAddress;
            gCodeBaseline.emplace_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at)),
                                       Vector<uint8_t>(at, at + section->Misc.VirtualSize));
        }
    }
    gLo = lo;
    gHi = hi;
}

void Arm(uintptr_t entry, uint32_t limit, uint64_t maxSteps)
{
    gMaxSteps = maxSteps;
    if (!gHandler) {
        ZydisDecoderInit(&gDecoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
        gHandler = AddVectoredExceptionHandler(1, Handler);
    }
    gThread = GetCurrentThreadId();
    gEntry = entry;
    gLimit = limit;
    gCaptures.clear();
    gAttempts = 0;
    RaiseException(kArmCode, 0, 0, nullptr);
}

Vector<Capture> Disarm()
{
    RaiseException(kDisarmCode, 0, 0, nullptr);
    gEntry = 0;
    Vector<Capture> out = std::move(gCaptures);
    gCaptures.clear();
    return out;
}

const TraceStats &Stats() { return gStats; }
void ResetStats() { gStats = TraceStats{}; }

} // namespace record
