#pragma once
// Fake world for the differential tests of the SimAntics interpreter (src/simantics/TreeSim*.cpp, TreeFrame, TreeStack).
//
// The interpreter functions reach everything through virtual calls: the sim's own slots (RunNode, ReadOperand,
// PushTree ...), the stack's, the frames', the trees', nodes', owners' and the context's. Every object of a test world
// is a fake whose vtable slots record the call (object, slot, arguments) and answer from the world's state, a
// scripted function of the seed, or both; so the original (the mapped image) and ours are compared on the calls made,
// in order, their arguments, the result or thrown error, and the state the run leaves (frame fields, sim fields,
// reference counts, the globals the interpreter keeps, what the trace log wrote to its sink).
//
// Frames use the real field layout (the interpreter reads +8 and +0xc of a frame directly) with fake slots that
// implement the field semantics; the stack is the real vector inside the sim object with fake slots that push, pop
// and look up. Pointers in arguments are normalised to offsets from the world base so the two worlds compare equal.
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "simantics/SimTraceLog.h"
#include "simantics/TreeFrame.h"
#include "simantics/TreeSim.h"
#include "simantics/TreeStack.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace tsw {

using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

constexpr int kFrames = 10;
constexpr int kSpareFrames = 3; // the last frames are handed out by NewFrame
constexpr int kTrees = 6;
constexpr int kNodesPerTree = 6;
constexpr int kNodes = kTrees * kNodesPerTree;
constexpr int kOwners = 3;
constexpr int kObjects = 3;
constexpr int kSlots = 64;

struct Cell {
    void **vt;
    uint32_t refs;
};

struct FrameCell {
    uint8_t real[0x3c]; // a cTSTreeStackElem; vtable at +0, reference count at +4
    uint16_t args[8];
    uint16_t locals[8];
};

struct TreeCell : Cell {
    int16_t version;
    uint8_t type, argCount, localCount, flags, cache;
    int16_t nodeCount;
    int nodeBase;
};

struct NodeCell : Cell {
    uint16_t opcode; // bit 15 is the breakpoint flag
    uint16_t tTrue, tFalse;
    uint8_t param[0x28];
};

struct OwnerCell : Cell {
    uint32_t salt;
    uint32_t nullRate; // 0..7: how many of 8 tree ids resolve to nothing
};

struct GateCell : Cell {
    uint32_t runState;
    uint32_t word33;
    uint32_t halt;
};

struct ContextCell : Cell {
    void *gate;
    void *table;
};

struct TableCell : Cell {
    void *objects[kObjects + 1];
};

struct ObjectCell : Cell {
    uint32_t flags;
};

struct ServiceCell : Cell {
    uint32_t enabled;
};

struct DebugCell : Cell {
    char text[48];
};

struct SinkCell : Cell {};

struct TempsCell : Cell {
    uint16_t value[8];
};

struct World {
    alignas(16) uint8_t sim[0x70];
    uint8_t simTail[0x40];
    void *vtSim[kSlots];
    void *vtUnknown[8];
    void *vtStack[24];
    void *vtFrame[48];
    void *vtTree[32];
    void *vtNode[32];
    void *vtOwner[48];
    void *vtGate[32];
    void *vtContext[32];
    void *vtTable[48];
    void *vtObject[256];
    void *vtService[32];
    void *vtDebug[8];
    void *vtSink[16];
    void *vtTemps[32];
    FrameCell frame[kFrames];
    TreeCell tree[kTrees];
    NodeCell node[kNodes];
    OwnerCell owner[kOwners];
    GateCell gate;
    ContextCell context;
    TableCell table;
    ObjectCell object[kObjects];
    ServiceCell serviceC;
    DebugCell debug;
    SinkCell sink;
    TempsCell temps;
    uint16_t defaultArgs[16];
    uint32_t nextSpare; // next unused spare frame for NewFrame
    uint32_t simRefs;
    uint32_t stepScript;
    uint32_t canary;
};

inline cTSTreeSim *Sim(World &w)
{
    return reinterpret_cast<cTSTreeSim *>(w.sim);
}
inline cTSTreeStackElem *Fr(World &w, int i)
{
    return reinterpret_cast<cTSTreeStackElem *>(w.frame[i].real);
}
// The stack's vector seen as plain frame pointers (the fakes push and pop without touching reference counts through
// the frames' slots; the elements of the real vector are smart pointers).
inline std::vector<void *> &RawFrames(std::vector<TreeFrameRef> &v)
{
    return reinterpret_cast<std::vector<void *> &>(v);
}
inline std::vector<void *> &Frames(World &w)
{
    return RawFrames(Sim(w)->mStack.mFrames);
}

// ---- recording ----------------------------------------------------------------------------------------------------

inline World *gW = nullptr;
inline uint32_t gSeed = 0;
inline std::vector<uint32_t> gTrace;
inline uint32_t gThrowAt = 0;
inline uint32_t gHitCount = 0;
inline uint32_t gStepsLeft = 0;     // the DoNodeAction script of RunToCompletion / CallTree tests
inline uint32_t gStepMode = 0;
inline uint32_t gStepCalls = 0;
inline int gArgWords = 8; // how many argument words PushTree's fake logs (StartTree's block is only 4 words long)
inline std::vector<uint32_t> gEvents; // (kind << 16 | detail) the worlds reached, for the coverage report

// The memory the recorded pointers are measured against: the world being run (or a test's own fixture).
inline const void *gBase = nullptr;
inline size_t gBaseSize = 0;


// A second measured range (the pool fixture's pages), reported in a separate number range.
inline const void *gBase2 = nullptr;
inline size_t gBase2Size = 0;

// An object's identity in the traces: its offset in the measured memory (the second range, the pool, in its own number
// range), so the two worlds of a case give the same numbers.
inline uint32_t Off(const void *p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    const uintptr_t base2 = reinterpret_cast<uintptr_t>(gBase2);
    if (gBase2 && v >= base2 && v < base2 + gBase2Size) return 0x50000000u + static_cast<uint32_t>(v - base2);
    return static_cast<uint32_t>(v - reinterpret_cast<uintptr_t>(gBase));
}

inline uint32_t Norm(uint32_t v)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gBase);
    if (gBase && v >= base && v < base + gBaseSize) return 0x40000000u + static_cast<uint32_t>(v - base);
    const uintptr_t base2 = reinterpret_cast<uintptr_t>(gBase2);
    if (gBase2 && v >= base2 && v < base2 + gBase2Size) return 0x50000000u + static_cast<uint32_t>(v - base2);
    return v;
}

inline uint32_t Mix(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

// Test-only (TREESIM_INJECT_WORLD=1 in the environment): one in eight of the fakes' random answers (operand reads, gate
// words; and owner salts and some frame-word arguments, see InjectWorldLow16) becomes a word inside the shared world of
// simantics_treesim_world.h (its fixed address plus the random word folded
// into its size: a value, the same on both sides, that happens to look like a world pointer). See SharedWorld there; set
// when that world is first made, so tests without it are unaffected.
inline bool gInjectWorld = std::getenv("TREESIM_INJECT_WORLD") != nullptr;
inline uintptr_t gInjectBase = 0;
inline uint32_t gInjectSize = 0;
inline uint32_t gInjected = 0; // words injected (both sides)
inline uint32_t InjectWorld(uint32_t v)
{
    if (!gInjectWorld || !gInjectBase || (v >> 29) != 0) return v;
    ++gInjected;
    return static_cast<uint32_t>(gInjectBase) + v % gInjectSize;
}
// The word inside the shared world with the low half of `v` (a 16-bit argument whose high half the callee ignores), when
// there is one; else `v`. For the tests' argument makers, under TREESIM_INJECT_WORLD only.
inline uint32_t InjectWorldLow16(uint32_t v)
{
    if (!gInjectWorld || !gInjectBase) return v;
    const uint32_t base = static_cast<uint32_t>(gInjectBase);
    const uint32_t word = base + ((static_cast<uint16_t>(v) - base) & 0xffffu);
    if (word - base >= gInjectSize) return v;
    ++gInjected;
    return word;
}

// Which fake class an object is (for the coverage counters): the index of its vtable among the world's.
inline int ClassOf(void *self);
inline std::map<uint32_t, uint32_t> gSlotHits; // (class << 8 | slot) -> calls, over the whole test

inline bool gVerbose = false; // print every recorded call as it happens (to find a crash)

inline void Rec(void *self, int slot, std::initializer_list<uint32_t> args, bool mayThrow = true)
{
    if (gVerbose) {
        std::printf("  call obj %p slot %d args", self, slot);
        for (uint32_t a : args) std::printf(" %x", a);
        std::printf("\n");
        std::fflush(stdout);
    }
    ++gSlotHits[static_cast<uint32_t>(ClassOf(self)) << 8 | static_cast<uint32_t>(slot)];
    if (mayThrow && ++gHitCount == gThrowAt) {
        gTrace.push_back(0xdead0001u);
        throw static_cast<short>(0x66);
    }
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(args.size()));
    for (uint32_t a : args) gTrace.push_back(Norm(a));
}

// A call recorded under a fixed object id (objects outside the measured memory, like the pool allocator in the image).
inline void RecId(uint32_t id, int slot, std::initializer_list<uint32_t> args)
{
    gTrace.push_back(id);
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(args.size()));
    for (uint32_t a : args) gTrace.push_back(Norm(a));
}

inline void RecText(void *self, int slot, const char *text)
{
    ++gSlotHits[static_cast<uint32_t>(ClassOf(self)) << 8 | static_cast<uint32_t>(slot)];
    if (++gHitCount == gThrowAt) throw static_cast<short>(0x66);
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | 0x80u);
    if (!text) {
        gTrace.push_back(0xdead5001u);
        return;
    }
    const size_t n = std::strlen(text);
    gTrace.push_back(static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) gTrace.push_back(static_cast<uint8_t>(text[i]));
}

inline int ClassOf(void *self)
{
    if (!gW) return 15;
    void **vt = *static_cast<void ***>(self);
    World &w = *gW;
    void **const tables[] = {w.vtSim, w.vtStack, w.vtFrame, w.vtTree, w.vtNode, w.vtOwner, w.vtGate, w.vtContext,
                             w.vtTable, w.vtObject, w.vtService, w.vtDebug, w.vtSink};
    for (int i = 0; i < 13; ++i)
        if (vt == tables[i]) return i;
    return 15;
}

inline void *WorldAt(uint32_t offset)
{
    return offset == 0xffffffffu ? nullptr : reinterpret_cast<uint8_t *>(gW) + offset;
}

// ---- the fakes ----------------------------------------------------------------------------------------------------
// Every function is __fastcall(self, edx-dummy, stack arguments...): the layout of a __thiscall callee that pops its own
// arguments. All return uint32_t (eax).

#define TSW_FN0(name) inline uint32_t __fastcall name(void *self, void *)
#define TSW_FN1(name, a) inline uint32_t __fastcall name(void *self, void *, uint32_t a)
#define TSW_FN2(name, a, b) inline uint32_t __fastcall name(void *self, void *, uint32_t a, uint32_t b)

// Reference counting: frames keep theirs at +4 like the real ones, the other fakes in `refs`. No deletes.
TSW_FN0(AddRefFake)
{
    ++static_cast<Cell *>(self)->refs;
    Rec(self, 1, {}, false);
    return 1;
}
TSW_FN0(ReleaseFake)
{
    --static_cast<Cell *>(self)->refs;
    Rec(self, 2, {}, false);
    return 1;
}

// ---- frames
#define FRAME_FIELDS(self) reinterpret_cast<cTSTreeStackElem *>(self)
TSW_FN0(FrGetW18) { Rec(self, 3, {}); return FRAME_FIELDS(self)->mWord18; }
TSW_FN1(FrSetW18, v) { Rec(self, 4, {v}); FRAME_FIELDS(self)->mWord18 = static_cast<uint16_t>(v); return 0; }
TSW_FN0(FrGetW1a) { Rec(self, 5, {}); return FRAME_FIELDS(self)->mWord1a; }
TSW_FN1(FrSetW1a, v) { Rec(self, 6, {v}); FRAME_FIELDS(self)->mWord1a = static_cast<uint16_t>(v); return 0; }
TSW_FN0(FrGetW1c) { Rec(self, 7, {}); return FRAME_FIELDS(self)->mWord1c; }
TSW_FN1(FrSetW1c, v) { Rec(self, 8, {v}); FRAME_FIELDS(self)->mWord1c = static_cast<uint16_t>(v); return 0; }
TSW_FN0(FrGetTreeId)
{
    Rec(self, 9, {});
    const uint32_t w = FRAME_FIELDS(self)->mTreeWord;
    return w == 0xffff ? 0xffffffffu : (w & 0x7fff);
}
TSW_FN1(FrSetTreeId, v)
{
    Rec(self, 10, {v});
    cTSTreeStackElem *f = FRAME_FIELDS(self);
    f->mTreeWord = static_cast<uint16_t>(v) == 0xffff ? static_cast<uint16_t>(0xffff) : static_cast<uint16_t>((f->mTreeWord & 0x8000u) | static_cast<uint16_t>(v));
    return 0;
}
TSW_FN0(FrGetStackObject) { Rec(self, 12, {}); return FRAME_FIELDS(self)->mStackObjectId; }
TSW_FN1(FrSetStackObject, v) { Rec(self, 13, {v}); FRAME_FIELDS(self)->mStackObjectId = static_cast<uint16_t>(v); return 0; }
TSW_FN0(FrGetNode) { Rec(self, 14, {}); return FRAME_FIELDS(self)->mNodeIndex; }
TSW_FN1(FrSetNode, v) { Rec(self, 15, {v}); FRAME_FIELDS(self)->mNodeIndex = static_cast<uint16_t>(v); return 0; }
TSW_FN0(FrGetFlag16) { Rec(self, 16, {}); return FRAME_FIELDS(self)->mByte16; }
TSW_FN1(FrSetFlag16, v) { Rec(self, 17, {v}); FRAME_FIELDS(self)->mByte16 = static_cast<uint8_t>(v); return 0; }
TSW_FN0(FrGetResume) { Rec(self, 18, {}); return FRAME_FIELDS(self)->mResumeState; }
TSW_FN1(FrSetResume, v) { Rec(self, 19, {v}); FRAME_FIELDS(self)->mResumeState = v; return 0; }
TSW_FN0(FrGetOwner) { Rec(self, 20, {}); return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(FRAME_FIELDS(self)->mOwner)); }
TSW_FN1(FrSetOwner, v) { Rec(self, 21, {v}); FRAME_FIELDS(self)->mOwner = reinterpret_cast<void *>(static_cast<uintptr_t>(v)); return 0; }
TSW_FN0(FrIsPaused)
{
    Rec(self, 22, {});
    const uint16_t w = FRAME_FIELDS(self)->mTreeWord;
    return w != 0xffff && (w & 0x8000u) ? 1u : 0u;
}
TSW_FN1(FrSetPaused, v)
{
    Rec(self, 23, {v & 0xff});
    cTSTreeStackElem *f = FRAME_FIELDS(self);
    if (f->mTreeWord != 0xffff) f->mTreeWord = (v & 0xff) ? static_cast<uint16_t>(f->mTreeWord | 0x8000u) : static_cast<uint16_t>(f->mTreeWord & 0x7fffu);
    return 0;
}
TSW_FN0(FrGetArgCount) { Rec(self, 26, {}); return FRAME_FIELDS(self)->mArgCount; }
TSW_FN0(FrGetLocalCount) { Rec(self, 27, {}); return FRAME_FIELDS(self)->mLocalCount; }
TSW_FN1(FrSetArgCount, v) { Rec(self, 28, {v & 0xff}); FRAME_FIELDS(self)->mArgCount = static_cast<uint8_t>(v); return 0; }
TSW_FN1(FrSetLocalCount, v) { Rec(self, 29, {v & 0xff}); FRAME_FIELDS(self)->mLocalCount = static_cast<uint8_t>(v); return 0; }
TSW_FN1(FrGetArg, i)
{
    Rec(self, 30, {i});
    cTSTreeStackElem *f = FRAME_FIELDS(self);
    FrameCell *cell = static_cast<FrameCell *>(self);
    if (static_cast<int32_t>(i) < static_cast<int32_t>(f->mArgCount) && i < 8) return cell->args[i];
    return 0;
}
TSW_FN2(FrSetArgs, n, p)
{
    // Logs the count and the (up to eight) words it is given.
    Rec(self, 33, {n});
    const uint16_t *words = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(p));
    if (!words) {
        gTrace.push_back(0xdead5002u);
        return 0;
    }
    for (int i = 0; i < gArgWords; ++i) gTrace.push_back(words[i]);
    return 0;
}
TSW_FN0(FrSelf) { Rec(self, 41, {}); return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)); }

// ---- the stack (this = &sim.mStack; its vector is the real one)
inline std::vector<void *> &StackVec(void *stack)
{
    return RawFrames(reinterpret_cast<cTSTreeStack *>(stack)->mFrames);
}
TSW_FN2(StNewFrame, iid, out)
{
    Rec(self, 4, {iid, out ? 1u : 0u});
    World &w = *gW;
    if (w.nextSpare >= static_cast<uint32_t>(kSpareFrames)) return 0;
    FrameCell &cell = w.frame[kFrames - kSpareFrames + static_cast<int>(w.nextSpare++)];
    ++reinterpret_cast<uint32_t *>(cell.real)[1];
    *reinterpret_cast<void **>(static_cast<uintptr_t>(out)) = &cell;
    return 1;
}
TSW_FN1(StReset, n)
{
    Rec(self, 3, {n});
    for (void *f : StackVec(self)) --reinterpret_cast<Cell *>(f)->refs;
    StackVec(self).clear();
    return 0;
}
inline uint32_t __fastcall StPush3(void *self, void *, uint32_t f, uint32_t flag, uint32_t env)
{
    Rec(self, 5, {f, flag, env});
    ++reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(f))[1];
    StackVec(self).push_back(reinterpret_cast<void *>(static_cast<uintptr_t>(f)));
    return 0;
}
TSW_FN2(StPop, flag, env)
{
    Rec(self, 6, {flag, env});
    std::vector<void *> &v = StackVec(self);
    if (v.empty()) throw static_cast<short>(0x3e9);
    --reinterpret_cast<uint32_t *>(v.back())[1];
    v.pop_back();
    return 0;
}
TSW_FN0(StClear)
{
    Rec(self, 7, {});
    for (void *f : StackVec(self)) --reinterpret_cast<uint32_t *>(f)[1];
    StackVec(self).clear();
    return 0;
}
TSW_FN0(StClearAll)
{
    Rec(self, 15, {});
    for (void *f : StackVec(self)) --reinterpret_cast<uint32_t *>(f)[1];
    StackVec(self).clear();
    return 0;
}
TSW_FN1(StGetAt, i)
{
    Rec(self, 9, {i});
    std::vector<void *> &v = StackVec(self);
    const int32_t index = static_cast<int32_t>(i);
    if (index < 0 || static_cast<uint32_t>(index) >= v.size()) return 0;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v[static_cast<uint32_t>(index)]));
}
TSW_FN0(StCount)
{
    Rec(self, 10, {});
    return static_cast<uint32_t>(StackVec(self).size());
}

// ---- the sim
// The temps interface the record/replay code asks the sim for (iid 0x9b25d761): eight words, read by slot 19 and written
// by slot 20.
TSW_FN2(SimQuery, iid, out)
{
    Rec(self, 0, {iid, out ? 1u : 0u});
    if (iid != 0x9b25d761u || Mix(gSeed, 0, 77) % 5 == 0) return 0;
    ++gW->temps.refs;
    *reinterpret_cast<void **>(static_cast<uintptr_t>(out)) = &gW->temps;
    return 1;
}
TSW_FN1(TmGet, i)
{
    Rec(self, 19, {i});
    return static_cast<TempsCell *>(self)->value[i & 7];
}
TSW_FN2(TmSet, i, v)
{
    Rec(self, 20, {i, v});
    static_cast<TempsCell *>(self)->value[i & 7] = static_cast<uint16_t>(v);
    return 0;
}
TSW_FN1(OwKey, id)
{
    Rec(self, 12, {id});
    return Mix(static_cast<OwnerCell *>(self)->salt, id & 0xffff, 12);
}

inline cTSTreeSim *SimOf(void *self)
{
    return static_cast<cTSTreeSim *>(self);
}
TSW_FN0(SimAddRef)
{
    ++gW->simRefs;
    Rec(self, 1, {}, false);
    return 1;
}
TSW_FN2(SimNewFrame, iid, out)
{
    Rec(self, 5, {iid, out ? 1u : 0u});
    World &w = *gW;
    if (w.nextSpare >= static_cast<uint32_t>(kSpareFrames)) return 0;
    FrameCell &cell = w.frame[kFrames - kSpareFrames + static_cast<int>(w.nextSpare++)];
    ++reinterpret_cast<uint32_t *>(cell.real)[1];
    *reinterpret_cast<void **>(static_cast<uintptr_t>(out)) = &cell;
    return 1;
}
TSW_FN2(SimRunNode, frame, node)
{
    Rec(self, 6, {frame, node});
    const int32_t table[] = {-1, 0, 1, 2, 3, 0, 1, 1, 0, 4, -2, 0x77};
    return static_cast<uint32_t>(table[Mix(gSeed, static_cast<uint32_t>(Off(reinterpret_cast<void *>(static_cast<uintptr_t>(node)))), 6) % 12]);
}
inline uint32_t __fastcall SimReadOperand(void *self, void *, uint32_t owner, uint32_t id, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                                          uint32_t e)
{
    Rec(self, 7, {owner, id, a, b, c, d, e});
    return InjectWorld(Mix(gSeed, owner, id));
}
TSW_FN1(SimHandleError, code)
{
    Rec(self, 8, {code & 0xffff});
    gTrace.push_back(0xe44c0000u | *reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(self) + 0x34)); // the sim's error word, as stored so far
    return 0;
}
TSW_FN0(SimFramePopped)
{
    Rec(self, 9, {});
    return 0;
}
inline uint32_t __fastcall SimPushTree(void *self, void *, uint32_t owner, uint32_t args, uint32_t treeId, uint32_t flag16, uint32_t w18, uint32_t w1a,
                                       uint32_t w1c)
{
    Rec(self, 12, {owner, args ? 1u : 0u, treeId, flag16, w18, w1a, w1c});
    const uint16_t *words = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(args));
    if (words) {
        for (int i = 0; i < gArgWords; ++i) gTrace.push_back(words[i]);
    }
    // Sometimes the push really happens: the new frame becomes the top (so the loop tests see the stack change).
    const uint32_t v = Mix(gSeed, treeId, 12);
    if (v % 4 == 0) return 0;
    World &w = *gW;
    if (w.nextSpare < static_cast<uint32_t>(kSpareFrames) && v % 4 == 1) {
        FrameCell &cell = w.frame[kFrames - kSpareFrames + static_cast<int>(w.nextSpare++)];
        cTSTreeStackElem *f = reinterpret_cast<cTSTreeStackElem *>(cell.real);
        f->mTreeWord = static_cast<uint16_t>(treeId & 0x7fff);
        // The pushed frame runs in the given owner, or the top frame's (as the real PushTree does).
        void *runOwner = reinterpret_cast<void *>(static_cast<uintptr_t>(owner));
        if (!runOwner && !Frames(w).empty()) runOwner = reinterpret_cast<cTSTreeStackElem *>(Frames(w).back())->mOwner;
        f->mOwner = runOwner;
        ++reinterpret_cast<uint32_t *>(cell.real)[1];
        Frames(w).push_back(&cell);
    }
    return 1;
}
TSW_FN2(SimDoNodeAction, frame, x)
{
    Rec(self, 13, {frame, x});
    gTrace.push_back(0xf1a90000u | *GameAddress<uint8_t>(0x1016910)); // the run flag CallTree clears while a tree runs
    // The script: a number of ordinary steps, then by mode: the frames above the bottom one (tree id -1) are gone and
    // the run is back at it (modes 0 and 3, the sim remembering a false or true return), or the step reports the
    // stack empty (1) or a yield (2).
    World &w = *gW;
    if (gStepsLeft > 0) {
        --gStepsLeft;
        return 0;
    }
    if (++gStepCalls > 40) return 1; // a script that never ends the run is ended by the stack reporting empty
    std::vector<void *> &v = Frames(w);
    switch (gStepMode) {
    case 0:
    case 3:
        while (!v.empty() && reinterpret_cast<cTSTreeStackElem *>(v.back())->mTreeWord != 0xffff) {
            --reinterpret_cast<uint32_t *>(v.back())[1];
            v.pop_back();
        }
        Sim(w)->mNextNode = gStepMode == 3 ? 0xfffd : 0xfffe;
        return 0;
    case 1: return 1;
    default: return 2;
    }
}
TSW_FN2(SimResume, frame, node)
{
    Rec(self, 14, {frame, node});
    return Mix(gSeed, Off(reinterpret_cast<void *>(static_cast<uintptr_t>(frame))), 14) % 3;
}
TSW_FN0(SimTopFrame)
{
    Rec(self, 20, {});
    std::vector<void *> &v = Frames(*gW);
    return v.empty() ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v.back()));
}
TSW_FN1(SimFrameAt, i)
{
    Rec(self, 22, {i});
    std::vector<void *> &v = Frames(*gW);
    const int32_t index = static_cast<int16_t>(i);
    if (index < 0 || static_cast<uint32_t>(index) >= v.size()) return 0;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v[static_cast<uint32_t>(index)]));
}
TSW_FN0(SimReturnedTrue)
{
    Rec(self, 28, {});
    return Sim(*gW)->mNextNode == 0xfffd;
}

// ---- trees, nodes, owners
#define TREE(self) static_cast<TreeCell *>(self)
TSW_FN0(TrVersion) { Rec(self, 6, {}); return static_cast<uint16_t>(TREE(self)->version); }
TSW_FN0(TrType) { Rec(self, 10, {}); return TREE(self)->type; }
TSW_FN0(TrArgCount) { Rec(self, 12, {}); return TREE(self)->argCount; }
TSW_FN0(TrLocalCount) { Rec(self, 14, {}); return TREE(self)->localCount; }
TSW_FN0(TrFlags) { Rec(self, 16, {}); return TREE(self)->flags; }
TSW_FN0(TrCache) { Rec(self, 18, {}); return TREE(self)->cache; }
TSW_FN0(TrNodeCount) { Rec(self, 20, {}); return static_cast<uint16_t>(TREE(self)->nodeCount); }
TSW_FN1(TrGetNode, i)
{
    Rec(self, 22, {i});
    TreeCell *t = TREE(self);
    if (i >= static_cast<uint32_t>(t->nodeCount)) return 0;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->node[t->nodeBase + static_cast<int>(i)]));
}
#define NODE(self) static_cast<NodeCell *>(self)
TSW_FN0(NdOpcode) { Rec(self, 3, {}); return NODE(self)->opcode & 0x7fff; }
TSW_FN0(NdTrue) { Rec(self, 6, {}); return NODE(self)->tTrue; }
TSW_FN0(NdFalse) { Rec(self, 8, {}); return NODE(self)->tFalse; }
TSW_FN0(NdParam) { Rec(self, 16, {}); return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(NODE(self)->param)); }
TSW_FN0(NdFlag) { Rec(self, 18, {}); return NODE(self)->opcode >> 15; }
TSW_FN1(NdSetFlag, v)
{
    Rec(self, 19, {v & 0xff});
    NodeCell *n = NODE(self);
    n->opcode = (v & 0xff) ? static_cast<uint16_t>(n->opcode | 0x8000u) : static_cast<uint16_t>(n->opcode & 0x7fffu);
    return 0;
}
TSW_FN1(OwGetTree, id)
{
    Rec(self, 23, {id});
    OwnerCell *o = static_cast<OwnerCell *>(self);
    if (Mix(o->salt, id, 0) % 8 < o->nullRate) return 0;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->tree[Mix(o->salt, id, 1) % kTrees]));
}
TSW_FN1(OwPrepare, id)
{
    Rec(self, 17, {id});
    return 0;
}
TSW_FN2(OwTreeName, id, str)
{
    Rec(self, 34, {id, str ? 1u : 0u});
    cRZString *s = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(str));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "bhav%x", id & 0xfff);
    s->mString.assign(buf);
    return 0;
}

// ---- the context's objects
TSW_FN0(GtRunState) { Rec(self, 22, {}); return static_cast<GateCell *>(self)->runState; }
TSW_FN1(GtSetRunState, v) { Rec(self, 23, {v}); static_cast<GateCell *>(self)->runState = v; return 0; }
TSW_FN1(GtWord, i)
{
    Rec(self, 15, {i});
    return i == 0x33 ? static_cast<GateCell *>(self)->word33 : InjectWorld(Mix(gSeed, i, 15));
}
TSW_FN0(GtHalt) { Rec(self, 27, {}); return static_cast<GateCell *>(self)->halt; }
TSW_FN0(CxGate) { Rec(self, 3, {}); return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<ContextCell *>(self)->gate)); }
TSW_FN0(CxTable) { Rec(self, 26, {}); return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<ContextCell *>(self)->table)); }
TSW_FN1(TbFind, id)
{
    Rec(self, 29, {id});
    TableCell *t = static_cast<TableCell *>(self);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t->objects[Mix(gSeed, id, 29) % (kObjects + 1)]));
}
TSW_FN0(ObFlags) { Rec(self, 28, {}); return static_cast<ObjectCell *>(self)->flags; }
TSW_FN1(ObCancel, v) { Rec(self, 237, {v}); return 0; }
TSW_FN0(SvEnabled) { Rec(self, 18, {}); return static_cast<ServiceCell *>(self)->enabled; }
TSW_FN1(SkWord, v) { RecText(self, 7, nullptr); gTrace.push_back(v); return 0; }
TSW_FN1(SkInt, v) { RecText(self, 9, nullptr); gTrace.push_back(v); return 0; }
TSW_FN1(SkText, p)
{
    RecText(self, 14, reinterpret_cast<const char *>(static_cast<uintptr_t>(p)));
    return 0;
}
inline uint32_t __fastcall DbNodeText(void *self, void *, uint32_t owner, uint32_t treeId, uint32_t nodeIndex)
{
    Rec(self, 4, {owner, treeId, nodeIndex});
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<DebugCell *>(self)->text));
}

} // namespace tsw
