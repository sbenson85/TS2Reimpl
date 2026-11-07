// difftest::FillOut / OutSlot (stack_out.h): out-buffers sized by the original caller's frame.
//
// No heap use here (fixed tables): fakes run inside FreeRecorder windows and allocator checks.
#include "stack_out.h"

#include "difftest.h"
#include "game/game.h"
#include "hook/hooks.h"

#include <Zydis.h>

#include <intrin.h>
#include <windows.h>

#include <climits>
#include <cstring>
#include <utility>

namespace difftest {
namespace {

struct Span {
    uintptr_t lo = 0, hi = 0;
    bool Has(uintptr_t a) const { return a >= lo && a < hi; }
};

Span ImageSpan()
{
    static Span s = [] {
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(kGameBase);
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(kGameBase + dos->e_lfanew);
        return Span{kGameBase, kGameBase + nt->OptionalHeader.SizeOfImage};
    }();
    return s;
}

Span OurCode()
{
    static Span s = [] {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
        return Span{base + nt->OptionalHeader.BaseOfCode, base + nt->OptionalHeader.BaseOfCode + nt->OptionalHeader.SizeOfCode};
    }();
    return s;
}

// `w` is a code address in the image or in ours, right after a call instruction (E8 rel32, or FF /2 in its register and
// memory forms).
bool ReturnAddress(uintptr_t w)
{
    const Span image = ImageSpan(), ours = OurCode();
    if (!((image.Has(w) && image.Has(w - 8)) || (ours.Has(w) && ours.Has(w - 8)))) return false;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(w);
    auto call2 = [](uint8_t op, uint8_t m) { return op == 0xff && (m & 0x38) == 0x10; };
    if (b[-5] == 0xe8) return true;
    if (call2(b[-2], b[-1]) && ((b[-1] >> 6) == 3 || ((b[-1] >> 6) == 0 && (b[-1] & 7) != 4 && (b[-1] & 7) != 5))) return true;
    if (call2(b[-3], b[-2]) && (((b[-2] >> 6) == 1 && (b[-2] & 7) != 4) || ((b[-2] >> 6) == 0 && (b[-2] & 7) == 4))) return true;
    if (call2(b[-4], b[-3]) && (b[-3] >> 6) == 1 && (b[-3] & 7) == 4) return true;
    if (call2(b[-6], b[-5]) && (((b[-5] >> 6) == 2 && (b[-5] & 7) != 4) || ((b[-5] >> 6) == 0 && (b[-5] & 7) == 5))) return true;
    if (call2(b[-7], b[-6]) && (b[-6] >> 6) == 2 && (b[-6] & 7) == 4) return true;
    return false;
}

// ---- frame analysis of one original function (following its flow from the entry) ----

constexpr unsigned kMaxExtent = 0x8000; // bytes of code followed from the entry
constexpr unsigned kMaxWork = 1024;
constexpr unsigned kMaxEdges = 1024;
constexpr unsigned kMaxCalls = 1024;
constexpr unsigned kCache = 64;

struct Frame {
    uint32_t start = 0;    // RVA; 0: empty slot
    bool ok = false;       // esp depth known at every call (no `and esp`, no other esp writes it can't follow)
    bool probe = false;    // the function moves esp by eax (the stack probe: `mov eax, N; call probe`)
    bool returns = false;  // a `ret` was reached
    uint16_t pops = 0;     // its `ret N`
    unsigned nEdges = 0, nCalls = 0;
    int32_t edges[kMaxEdges];     // entry-relative offsets the function writes, pushes or takes the address of (sorted)
    uint32_t callRet[kMaxCalls];  // return RVA of each call
    int32_t callDepth[kMaxCalls]; // bytes below the entry esp at that call (its arguments pushed)
};

Frame gCache[kCache];
unsigned gNext;
constexpr int kHops = 3; // thunks followed from a direct callee
Frame gCalleeScratch[kHops + 1];
uint8_t gSeen[kHops + 2][kMaxExtent / 8]; // per level: the caller, then each callee hop
struct WorkItem {
    uint32_t at;
    int32_t depth;
};
WorkItem gWork[kHops + 2][kMaxWork];
struct CalleeInfo {
    uint32_t rva;
    uint16_t pops;
    bool probe, known;
};
CalleeInfo gCallees[4096];
unsigned gCalleeCount;
ZydisDecoder gDecoder;
bool gDecoderReady;

void AddEdge(Frame &f, int32_t e)
{
    if (e >= 0 || e < -0x100000) return;
    for (unsigned i = 0; i < f.nEdges; ++i)
        if (f.edges[i] == e) return;
    if (f.nEdges < kMaxEdges) f.edges[f.nEdges++] = e;
    else f.ok = false;
}

bool Fill(Frame &f, uint32_t start, bool calleeOnly, int hops = 0);

// A direct callee's `ret N` and whether it is the stack probe (its own callees aren't followed).
CalleeInfo Callee(uint32_t rva, int hops = 0)
{
    for (unsigned i = 0; i < gCalleeCount; ++i)
        if (gCallees[i].rva == rva) return gCallees[i];
    CalleeInfo info{rva, 0, false, false};
    Frame &scratch = gCalleeScratch[hops];
    if (Fill(scratch, rva, true, hops)) info = {rva, scratch.pops, scratch.probe, scratch.returns};
    if (gCalleeCount < 4096) gCallees[gCalleeCount++] = info;
    return info;
}

bool Fill(Frame &f, uint32_t start, bool calleeOnly, int hops)
{
    if (!gDecoderReady) {
        ZydisDecoderInit(&gDecoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
        gDecoderReady = true;
    }
    const int level = calleeOnly ? hops + 1 : 0;
    uint8_t *seen = gSeen[level];
    std::memset(seen, 0, sizeof(gSeen[level]));
    WorkItem *work = gWork[level];
    using Item = WorkItem;
    unsigned nWork = 0;
    const Span image = ImageSpan();
    const uintptr_t base = kGameBase + start;
    if (!image.Has(base)) return false;
    f = Frame{};
    f.start = start;
    f.ok = true;
    bool ebpFrame = false;
    int32_t ebpAt = 0;
    work[nWork++] = {0, 0};
    while (nWork) {
        const Item it = work[--nWork];
        uint32_t at = it.at;
        int32_t depth = it.depth;
        int32_t args = 0;        // bytes pushed since the last call (an indirect callee is assumed to pop them)
        int64_t eaxImm = -1;     // the last `mov eax, imm` (the stack probe's size)
        int32_t assumed = -1;    // after an indirect call: what it was assumed to pop (a cdecl `add esp, N` corrects it)
        for (;;) {
            if (at >= kMaxExtent) {
                f.ok = false;
                break;
            }
            if (seen[at / 8] & (1 << (at % 8))) break;
            seen[at / 8] |= uint8_t(1 << (at % 8));
            ZydisDecodedInstruction ins;
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
            const uintptr_t addr = base + at;
            if (!image.Has(addr + 15)
                || !ZYAN_SUCCESS(ZydisDecoderDecodeFull(&gDecoder, reinterpret_cast<const void *>(addr), 15, &ins, ops))) {
                f.ok = false;
                break;
            }
            const uint32_t next = at + ins.length;
            const auto m = ins.mnemonic;
            const unsigned visible = ins.operand_count_visible;
            auto isReg = [&](unsigned i, ZydisRegister r) {
                return i < visible && ops[i].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[i].reg.value == r;
            };
            auto imm = [&](unsigned i) -> int64_t {
                return i < visible && ops[i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE ? ops[i].imm.value.s : INT64_MIN;
            };
            const int32_t wasAssumed = assumed;
            assumed = -1;
            // stack references (with this instruction's esp before its own push or pop)
            for (unsigned i = 0; i < visible; ++i) {
                const ZydisDecodedOperand &op = ops[i];
                if (op.type != ZYDIS_OPERAND_TYPE_MEMORY) continue;
                int32_t e;
                if (op.mem.base == ZYDIS_REGISTER_ESP) e = int32_t(op.mem.disp.value) - depth;
                else if (ebpFrame && op.mem.base == ZYDIS_REGISTER_EBP) e = int32_t(op.mem.disp.value) - ebpAt;
                else continue;
                // an object's own boundary: written here, its address taken, or indexed (an array's start); words
                // only read are fields of an object a callee filled
                if (op.mem.type == ZYDIS_MEMOP_TYPE_AGEN || (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)
                    || op.mem.index != ZYDIS_REGISTER_NONE)
                    AddEdge(f, e);
            }
            if (m == ZYDIS_MNEMONIC_MOV && isReg(0, ZYDIS_REGISTER_EBP) && isReg(1, ZYDIS_REGISTER_ESP)) {
                if (!ebpFrame) {
                    ebpFrame = true;
                    ebpAt = depth;
                }
            } else if (m == ZYDIS_MNEMONIC_MOV && isReg(0, ZYDIS_REGISTER_ESP) && isReg(1, ZYDIS_REGISTER_EBP) && ebpFrame) {
                depth = ebpAt;
            } else if (m == ZYDIS_MNEMONIC_LEA && isReg(0, ZYDIS_REGISTER_ESP)) {
                if (ebpFrame && ops[1].mem.base == ZYDIS_REGISTER_EBP && ops[1].mem.index == ZYDIS_REGISTER_NONE)
                    depth = ebpAt - int32_t(ops[1].mem.disp.value);
                else if (ops[1].mem.base == ZYDIS_REGISTER_ESP && ops[1].mem.index == ZYDIS_REGISTER_NONE)
                    depth -= int32_t(ops[1].mem.disp.value);
                else
                    f.ok = false;
            } else if (m == ZYDIS_MNEMONIC_MOV && isReg(0, ZYDIS_REGISTER_EAX) && imm(1) != INT64_MIN) {
                eaxImm = imm(1);
            } else if (m == ZYDIS_MNEMONIC_PUSH || m == ZYDIS_MNEMONIC_PUSHFD) {
                depth += 4;
                args += 4;
                AddEdge(f, -depth);
            } else if (m == ZYDIS_MNEMONIC_POP || m == ZYDIS_MNEMONIC_POPFD) {
                depth -= 4;
                args = args > 4 ? args - 4 : 0;
            } else if (m == ZYDIS_MNEMONIC_PUSHAD) {
                depth += 32;
            } else if (m == ZYDIS_MNEMONIC_POPAD) {
                depth -= 32;
            } else if ((m == ZYDIS_MNEMONIC_SUB || m == ZYDIS_MNEMONIC_ADD) && isReg(0, ZYDIS_REGISTER_ESP)) {
                const int64_t v = imm(1);
                if (v == INT64_MIN) {
                    f.ok = false;
                } else if (m == ZYDIS_MNEMONIC_ADD && wasAssumed >= 0) {
                    depth -= int32_t(v) - wasAssumed; // a cdecl indirect callee: the caller pops, not the callee
                    args = 0;
                } else {
                    depth += m == ZYDIS_MNEMONIC_SUB ? int32_t(v) : -int32_t(v);
                    args = m == ZYDIS_MNEMONIC_SUB ? 0 : (args > v ? args - int32_t(v) : 0);
                }
            } else if (m == ZYDIS_MNEMONIC_XCHG && (isReg(0, ZYDIS_REGISTER_ESP) || isReg(1, ZYDIS_REGISTER_ESP))) {
                f.probe = true;
                f.ok = false;
            } else if (visible && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && ops[0].reg.value == ZYDIS_REGISTER_ESP
                       && (ops[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) {
                f.ok = false; // `and esp, -N` (an aligned frame) or another write: depths unknown from here
            }
            if (ins.meta.category == ZYDIS_CATEGORY_CALL) {
                int32_t popped = args;
                bool noReturn = false;
                ZyanU64 target = 0;
                const bool direct = ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&ins, &ops[0], addr, &target));
                if (direct) {
                    popped = 0;
                    if (!calleeOnly) {
                        if (!image.Has(uintptr_t(target))) {
                            f.ok = false;
                        } else {
                            const CalleeInfo c = Callee(uint32_t(target - kGameBase));
                            noReturn = !c.known && !c.probe;
                            popped = c.probe && eaxImm >= 0 ? -int32_t(eaxImm) : c.pops; // the probe moves esp down by eax
                        }
                    }
                } else {
                    assumed = popped;
                }
                if (!calleeOnly && f.nCalls < kMaxCalls) {
                    f.callRet[f.nCalls] = start + next;
                    f.callDepth[f.nCalls] = depth;
                    ++f.nCalls;
                } else if (!calleeOnly) {
                    f.ok = false;
                }
                if (noReturn) break; // no `ret` found (noreturn, or not analysable): this path ends here
                depth -= popped;
                args = 0;
                eaxImm = -1;
                at = next;
                continue;
            }
            if (ins.meta.category == ZYDIS_CATEGORY_RET) {
                f.returns = true;
                if (imm(0) != INT64_MIN) f.pops = uint16_t(imm(0));
                break;
            }
            if (m == ZYDIS_MNEMONIC_INT3 || m == ZYDIS_MNEMONIC_HLT || m == ZYDIS_MNEMONIC_UD2) break;
            if (ins.meta.category == ZYDIS_CATEGORY_COND_BR || ins.meta.category == ZYDIS_CATEGORY_UNCOND_BR) {
                ZyanU64 target = 0;
                if (ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    && ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&ins, &ops[0], addr, &target))) {
                    const int64_t t = int64_t(target) - int64_t(base);
                    if (t >= 0 && t < kMaxExtent) {
                        if (nWork < kMaxWork) work[nWork++] = {uint32_t(t), depth};
                        else f.ok = false;
                    } else if (calleeOnly && ins.meta.category == ZYDIS_CATEGORY_UNCOND_BR && hops < 3
                               && image.Has(uintptr_t(target))) {
                        // a thunk: the function it jumps to returns for it (a copy: the scratch frame is reused)
                        const CalleeInfo c = Callee(uint32_t(target - kGameBase), hops + 1);
                        f.returns = f.returns || c.known;
                        f.pops = c.known ? c.pops : f.pops;
                        f.probe = f.probe || c.probe;
                    }
                } else if (ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY && ops[0].mem.base == ZYDIS_REGISTER_NONE
                           && ops[0].mem.index != ZYDIS_REGISTER_NONE && ops[0].mem.scale == 4) {
                    // a jump table: its entries while they point into this function's neighbourhood
                    const auto *table = reinterpret_cast<const uint32_t *>(uintptr_t(uint32_t(ops[0].mem.disp.value)));
                    for (unsigned k = 0; k < 256 && image.Has(uintptr_t(table + k)); ++k) {
                        const int64_t t = int64_t(table[k]) - int64_t(base);
                        if (t < 0 || t >= kMaxExtent) break;
                        if (nWork < kMaxWork) work[nWork++] = {uint32_t(t), depth};
                    }
                }
                // other indirect jumps: not followed (calls only reached past them are missed, not mis-measured)
                if (ins.meta.category == ZYDIS_CATEGORY_UNCOND_BR) break;
            }
            at = next;
        }
    }
    for (unsigned i = 1; i < f.nEdges; ++i)
        for (unsigned j = i; j > 0 && f.edges[j - 1] > f.edges[j]; --j) std::swap(f.edges[j - 1], f.edges[j]);
    return true;
}

const Frame *Analyse(uint32_t start)
{
    for (const Frame &f : gCache)
        if (f.start == start) return &f;
    Frame &f = gCache[gNext];
    gNext = (gNext + 1) % kCache;
    if (!Fill(f, start, false)) {
        f.start = 0;
        return nullptr;
    }
    return &f;
}

// The original function whose call returns to `ret` (an RVA): among the functions the harness knows (hooked or
// tested), the nearest ones starting before it whose code reaches that call.
const Frame *Owner(uint32_t ret, unsigned &call)
{
    uint32_t best[8] = {};
    unsigned n = 0;
    auto consider = [&](uint32_t s) {
        if (s >= ret || ret - s > kMaxExtent) return;
        for (unsigned i = 0; i < n; ++i)
            if (best[i] == s) return;
        if (n < 8) {
            best[n++] = s;
            return;
        }
        unsigned lo = 0;
        for (unsigned i = 1; i < 8; ++i)
            if (best[i] < best[lo]) lo = i;
        if (s > best[lo]) best[lo] = s;
    };
    for (const HookSpec *h : RegisteredHooks()) consider(h->rva);
    for (const Test &t : Registry()) consider(t.rva);
    for (unsigned i = 1; i < n; ++i)
        for (unsigned j = i; j > 0 && best[j - 1] < best[j]; --j) std::swap(best[j - 1], best[j]);
    for (unsigned i = 0; i < n; ++i) {
        const Frame *f = Analyse(best[i]);
        if (!f) continue;
        for (unsigned c = 0; c < f->nCalls; ++c)
            if (f->callRet[c] == ret) {
                call = c;
                return f->ok ? f : nullptr;
            }
    }
    return nullptr;
}

size_t SlotAt(const Frame &f, int32_t e);

// Who owns the stack object at p: scanning down from p for the closest return address (the call its frame's function
// made). An image return address counts only when the analysis of its function puts that function's own return
// address where it should be (a stale one in an uninitialised local would not).
size_t Measure(const void *ptr, uintptr_t floor, bool &onStack, bool &original)
{
    onStack = original = false;
    const auto p = reinterpret_cast<uintptr_t>(ptr);
    ULONG_PTR lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    if (p < floor || p >= hi) return 0;
    onStack = true;
    for (uintptr_t q = (p - 4) & ~uintptr_t(3); q >= floor && p - q <= 0x4000; q -= 4) {
        const uintptr_t w = *reinterpret_cast<const uintptr_t *>(q);
        if (!ReturnAddress(w)) continue;
        if (!ImageSpan().Has(w)) return 0; // ours (or the test's)
        unsigned call = 0;
        const Frame *f = Owner(uint32_t(w - kGameBase), call);
        if (!f) continue;
        // esp at the call was q + 4 (the return address just below it), `callDepth` bytes below the entry esp
        const uintptr_t entry = q + 4 + uintptr_t(f->callDepth[call]);
        if (entry >= hi || entry <= p || !ReturnAddress(*reinterpret_cast<const uintptr_t *>(entry))) continue;
        original = true;
        return SlotAt(*f, int32_t(int64_t(p) - int64_t(entry)));
    }
    return 0;
}

struct Remembered {
    uintptr_t key;
    size_t size;
};
Remembered gRemembered[1024];
unsigned gRememberedCount;

size_t &RememberedFor(uintptr_t key)
{
    for (unsigned i = 0; i < gRememberedCount; ++i)
        if (gRemembered[i].key == key) return gRemembered[i].size;
    static size_t overflow;
    if (gRememberedCount == 1024) return overflow = 0;
    gRemembered[gRememberedCount] = {key, 0};
    return gRemembered[gRememberedCount++].size;
}

size_t SlotAt(const Frame &f, int32_t e)
{
    for (unsigned i = 0; i < f.nEdges; ++i)
        if (f.edges[i] > e) return size_t(f.edges[i] - e);
    return size_t(-e); // up to the return address
}

} // namespace

size_t OriginalSlot(uint32_t function, int32_t entry)
{
    const Frame *f = Analyse(function);
    return f && f->ok && entry < 0 ? SlotAt(*f, entry) : 0;
}

int32_t OriginalCallDepth(uint32_t function, uint32_t ret)
{
    const Frame *f = Analyse(function);
    if (!f || !f->ok) return -1;
    for (unsigned c = 0; c < f->nCalls; ++c)
        if (f->callRet[c] == ret) return f->callDepth[c];
    return -1;
}

__declspec(noinline) size_t OutSlot(const void *p)
{
    bool onStack = false, original = false;
    return Measure(p, reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()), onStack, original);
}

__declspec(noinline) size_t FillOut(void *p, size_t wrote, size_t cap)
{
    const auto key = reinterpret_cast<uintptr_t>(_ReturnAddress());
    bool onStack = false, original = false;
    const size_t measured = Measure(p, reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()), onStack, original);
    size_t &remembered = RememberedFor(key);
    size_t size = wrote;
    if (original && measured > remembered) remembered = measured;
    if (original && measured > size) size = measured;
    if (!original && onStack && remembered > size) size = remembered;
    if (size > cap) size = cap < wrote ? wrote : cap;
    if (size > wrote) std::memset(static_cast<uint8_t *>(p) + wrote, kOutPattern, size - wrote);
    return size;
}

} // namespace difftest
