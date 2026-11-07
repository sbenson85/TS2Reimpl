// SimAntics primitive trace (diagnostic, test control like host/control.cpp; nothing here stands in
// for the game's code).
//
// The tree simulator's step (cTSTreeSim slot 13, 0x5762d0) runs each primitive node through the
// object's secondary vtable slot 6 at 0x5768cd:
//     mov eax,[ebx]; mov ecx,ebx; push edi; push esi; call [eax+0x18]; inc eax; ...
// (ebx: the object's interface at +4, esi: the stack frame being run, edi: the node; the callee is
// cTSObject::vf4_6 0x52184d -> vf246 0x53e560, or a subclass's). The "simtrace <n>" control command
// replaces those nine bytes with a call to a stub that makes the same call and writes one line per
// primitive to simtrace.txt next to the executable: framework tick, object (and its vtable as an
// RVA), opcode (node slot 3, which vf246 itself reads), result (0 false, 1 true, 2 yield so far; the
// caller switches on -1..3), BHAV id and node index (stack frame +8 and +0xc), the object ids (frame +0x18: two 16-bit ids, equal so far; stable between runs), the node's true and
// false targets and its 16 operand bytes. Layouts seen in-game: a node is 16 bytes {vtable, u16 ?,
// u16 opcode, u16 true target, u16 false target, operand pointer}; stack frames have vtable
// 0xdd771c. Each line also lists the operands the primitive read and their values ("vals scope:id=value",
// through the object's interface slot 7, cTSObject::vf4_7 0x531700, the operand reader: wrapped on the
// first traced call of each interface vtable, so traces compare data values, not only results). The
// first entries also dump the first 16 dwords of the frame and node objects, so the
// layout can be rechecked. The original code runs unchanged
// (reference runs) or our replacements do. When our interpreter step replaces 0x5762d0, it calls the same two
// functions through src/simantics/StepObserver.h instead of the patched call sites.
#include "control.h"

#include "game/game.h"
#include "hook/log.h"
#include "simantics/StepObserver.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr uint32_t kStepCall = 0x5768cd;
constexpr uint8_t kStepBytes[9] = {0x8B, 0x03, 0x8B, 0xCB, 0x57, 0x56, 0xFF, 0x50, 0x18};
constexpr uint32_t kDumpEntries = 40;

FILE *gFile;
volatile LONG gRemaining;
uint32_t gWritten;
CRITICAL_SECTION gFileLock;
bool gInstalled;

uint32_t Rva(const void *p)
{
    uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
    uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    return v - base < 0x2000000 ? v - base : v;
}

template <typename Fn> Fn Slot(void *object, int slot)
{
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot]);
}

void Dump(const char *what, const void *p)
{
    __try {
        auto *d = static_cast<const uint32_t *>(p);
        std::fprintf(gFile, "  %s %p:", what, p);
        for (int i = 0; i < 16; ++i) std::fprintf(gFile, " %08x", d[i]);
        std::fprintf(gFile, "\n");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::fprintf(gFile, "  %s %p: unreadable\n", what, p);
    }
}

// What a line records, read before the primitive runs (it may change the frame).
struct Entry {
    uint32_t bhav, index, ids;
    uint16_t onTrue, onFalse;
    uint8_t operands[16];
    bool readable;
};

void Capture(Entry &e, const void *frame, const void *node)
{
    __try {
        auto *f = static_cast<const uint32_t *>(frame);
        auto *n = static_cast<const uint16_t *>(node);
        e.bhav = f[2];
        e.index = f[3];
        e.ids = f[6];
        e.onTrue = n[4];
        e.onFalse = n[5];
        std::memcpy(e.operands, *reinterpret_cast<const uint8_t *const *>(n + 6), 16);
        e.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        e.readable = false;
    }
}

// ---- operand values ----
// The reader is __thiscall with 7 stack arguments (scope, id, destination, float destination, three
// zeros; ret 0x1c) and returns the value in ax. A __fastcall wrapper with a dummy edx has the same
// stack contract. The wrapper finds the original through the vtable it was called with.
using ReadFn = int(__fastcall *)(void *, void *, uint32_t, uint32_t, void *, void *, uint32_t, uint32_t, uint32_t);
constexpr int kReaderSlot = 7;
constexpr uint32_t kMaxVtables = 64, kMaxValues = 16;
void **gWrapped[kMaxVtables];
ReadFn gOriginalRead[kMaxVtables];
uint32_t gWrappedCount;
DWORD gTraceThread; // the thread the control commands run on (the main thread)
bool gInPrimitive; // that thread only: set while a traced primitive runs
uint32_t gValueCount;
struct Value { uint16_t scope, id; int16_t value; } gValues[kMaxValues];

int __fastcall ReadAndRecord(void *self, void *edx, uint32_t scope, uint32_t id, void *dest, void *fdest, uint32_t a,
                             uint32_t b, uint32_t c)
{
    void **vt = *static_cast<void ***>(self);
    ReadFn original = nullptr;
    for (uint32_t i = 0; i < gWrappedCount; ++i)
        if (gWrapped[i] == vt) original = gOriginalRead[i];
    int result = original(self, edx, scope, id, dest, fdest, a, b, c);
    if (gInPrimitive && gValueCount < kMaxValues)
        gValues[gValueCount++] = {static_cast<uint16_t>(scope), static_cast<uint16_t>(id), static_cast<int16_t>(result)};
    return result;
}

void WrapReader(void *object)
{
    void **vt = *static_cast<void ***>(object);
    for (uint32_t i = 0; i < gWrappedCount; ++i)
        if (gWrapped[i] == vt) return;
    if (gWrappedCount == kMaxVtables) return;
    gWrapped[gWrappedCount] = vt;
    gOriginalRead[gWrappedCount] = reinterpret_cast<ReadFn>(vt[kReaderSlot]);
    ++gWrappedCount;
    DWORD old;
    VirtualProtect(&vt[kReaderSlot], 4, PAGE_READWRITE, &old);
    vt[kReaderSlot] = reinterpret_cast<void *>(&ReadAndRecord);
    VirtualProtect(&vt[kReaderSlot], 4, old, &old);
}

uint32_t gLastTick;
uint32_t gSimFrames, gLastSimFrame = UINT32_MAX; // frames in which a primitive ran (main thread)

int __cdecl TraceStep(void *object, void *frame, void *node)
{
    uint32_t now = ControlFrame();
    if (now != gLastSimFrame) gLastSimFrame = now, ++gSimFrames;
    bool trace = gRemaining > 0 && InterlockedDecrement(&gRemaining) >= 0;
    int16_t opcode = 0;
    Entry e{};
    const bool mainThread = GetCurrentThreadId() == gTraceThread;
    if (trace) {
        opcode = Slot<int16_t(__thiscall *)(void *)>(node, 3)(node);
        Capture(e, frame, node);
        if (mainThread) {
            WrapReader(object);
            gValueCount = 0;
            gInPrimitive = true;
        }
    }
    int result = Slot<int(__thiscall *)(void *, void *, void *)>(object, 6)(object, frame, node);
    if (trace && mainThread) gInPrimitive = false;
    if (trace) {
        EnterCriticalSection(&gFileLock);
        uint32_t tick = ControlFrame();
        if (tick != gLastTick) std::fflush(gFile), gLastTick = tick;
        std::fprintf(gFile, "%u obj %p vt %x op %x -> %d", tick, object, Rva(*static_cast<void **>(object)),
                     static_cast<uint16_t>(opcode), result);
        if (e.readable) {
            std::fprintf(gFile, " ids %08x bhav %x node %u t/f %x/%x ops", e.ids, e.bhav, e.index, e.onTrue, e.onFalse);
            for (int i = 0; i < 16; ++i) std::fprintf(gFile, i % 2 ? "%02x" : " %02x", e.operands[i]);
        }
        if (mainThread) {
            std::fprintf(gFile, " vals");
            for (uint32_t i = 0; i < gValueCount; ++i)
                std::fprintf(gFile, " %x:%x=%d", gValues[i].scope, gValues[i].id, gValues[i].value);
        }
        std::fprintf(gFile, "\n");
        if (gWritten++ < kDumpEntries) {
            Dump("frame", frame);
            Dump("node", node);
        }
        if (gRemaining <= 0) {
            std::fprintf(gFile, "end\n");
            std::fflush(gFile);
        }
        LeaveCriticalSection(&gFileLock);
    }
    return result;
}

// The node loop reads each node's opcode at 0x576544 (mov eax,[edi]; mov ecx,edi; call [eax+0xc];
// edi: the node, esi: the stack frame, ebx: the object) and runs opcodes from 0x100 up as
// subroutine calls; those get a "call" line: the called BHAV id and the calling frame's BHAV and node.
constexpr uint32_t kOpcodeRead = 0x576544;
constexpr uint8_t kOpcodeBytes[7] = {0x8B, 0x07, 0x8B, 0xCF, 0xFF, 0x50, 0x0C};

int __cdecl TraceOpcode(void *object, void *frame, void *node)
{
    int16_t opcode = Slot<int16_t(__thiscall *)(void *)>(node, 3)(node);
    if (static_cast<uint16_t>(opcode) >= 0x100 && gRemaining > 0 && InterlockedDecrement(&gRemaining) >= 0) {
        Entry e{};
        Capture(e, frame, node);
        EnterCriticalSection(&gFileLock);
        std::fprintf(gFile, "%u obj %p vt %x call %x ids %08x from bhav %x node %u t/f %x/%x ops", ControlFrame(), object,
                     Rva(*static_cast<void **>(object)), static_cast<uint16_t>(opcode), e.ids, e.bhav, e.index, e.onTrue, e.onFalse);
        for (int i = 0; i < 16; ++i) std::fprintf(gFile, i % 2 ? "%02x" : " %02x", e.operands[i]);
        std::fprintf(gFile, "\n");
        LeaveCriticalSection(&gFileLock);
    }
    return opcode; // the caller compares ax
}

// Replaces `length` bytes holding `expected` at `rva` with a call to a stub that pushes edi, esi
// and ebx (node, frame, object), calls `fn` (cdecl) and returns its eax; the rest are nops.
bool Patch(uint32_t rva, const uint8_t *expected, size_t length, int(__cdecl *fn)(void *, void *, void *))
{
    auto *site = GameAddress<uint8_t>(rva);
    if (std::memcmp(site, expected, length) != 0) {
        Log("simtrace: 0x%x is not the expected code; not traced", rva);
        return false;
    }
    auto *stub = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    uint8_t *p = stub;
    // push edi; push esi; push ebx; call fn; add esp,12; ret
    const uint8_t save[] = {0x57, 0x56, 0x53, 0xE8};
    std::memcpy(p, save, sizeof(save));
    p += sizeof(save);
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(fn) - reinterpret_cast<uintptr_t>(p + 4));
    std::memcpy(p, &rel, 4);
    p += 4;
    const uint8_t restore[] = {0x83, 0xC4, 0x0C, 0xC3};
    std::memcpy(p, restore, sizeof(restore));
    DWORD old;
    VirtualProtect(stub, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), stub, 64);
    VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &old);
    site[0] = 0xE8;
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - reinterpret_cast<uintptr_t>(site + 5));
    std::memcpy(site + 1, &rel, 4);
    std::memset(site + 5, 0x90, length - 5);
    VirtualProtect(site, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, length);
    return true;
}

bool Install()
{
    // Our interpreter step (src/simantics/TreeSimStep.cpp) reports the same two points when it replaces 0x5762d0;
    // the patches cover the original's. Only one of the two runs, so each primitive is traced once.
    stepobserver::gOpcodeRead = &TraceOpcode;
    stepobserver::gRunPrimitive = &TraceStep;
    return Patch(kStepCall, kStepBytes, sizeof(kStepBytes), &TraceStep) &&
           Patch(kOpcodeRead, kOpcodeBytes, sizeof(kOpcodeBytes), &TraceOpcode);
}

} // namespace

bool StartSimFrameCounter()
{
    if (!gInstalled) gInstalled = Install();
    return gInstalled;
}

uint32_t SimFrames()
{
    return gSimFrames;
}

void SimTrace(const std::wstring &directory, uint32_t count)
{
    if (!gFile) {
        InitializeCriticalSection(&gFileLock);
        std::wstring path = directory + L"simtrace.txt";
        gFile = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
        if (!gFile) {
            Log("simtrace: cannot open simtrace.txt");
            return;
        }
        std::setvbuf(gFile, nullptr, _IOFBF, 1 << 20);
        std::fprintf(gFile, "image %p\n", GameAddress<void>(0));
    }
    if (!gInstalled && !(gInstalled = Install())) return;
    gTraceThread = GetCurrentThreadId();
    EnterCriticalSection(&gFileLock);
    std::fprintf(gFile, "start %u at frame %u\n", count, ControlFrame());
    LeaveCriticalSection(&gFileLock);
    InterlockedExchange(&gRemaining, static_cast<LONG>(count));
    Log("simtrace: tracing the next %u primitives", count);
}
