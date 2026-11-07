#pragma once
// The world of the particle command tests (tests/gz_particle_commands*.cpp), built on the material tests' world
// (tests/gz_material_commands.h): its parser state block serves as the effects parser (for the `particles` block) or as
// the particles state (for the block's commands), with real strings and vectors in the fields the commands use. The
// game functions the commands hand work to (the description's, the path's, the colliders' and attractors') are patched
// with recording stand-ins on both sides.
#include "gz_material_commands.h"

#include "gz/ParticleCommands.h"
#include "ts/cTSEffectCurve.h"
#include "ts/cTSParticlesDescription.h"

#include <cmath>

namespace ptest {

using namespace mattest;
namespace cp = nGZCommandParser;

// A call to a stand-in of a game function: object id 50 + which, `self` unused.
inline void Log(int which, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
{
    gLog.push_back({50 + which, 0, 0, {a0, a1, a2, a3}});
}
inline uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Where a pointer the commands pass points: the world's blocks are fixed between the sides; stack and heap blocks are
// not (they are logged as markers or offsets).
inline uint8_t *gStateBase = nullptr;  // the particles state (or the effects parser) of the case
inline uint8_t *gObjectBase = nullptr; // the command object of the case
inline uint32_t Where(const void *p)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (gStateBase && b >= gStateBase && b < gStateBase + kStateSize) return 0x57A70000u + static_cast<uint32_t>(b - gStateBase);
    if (gObjectBase && b >= gObjectBase && b < gObjectBase + 0x300) return 0x0B1E0000u + static_cast<uint32_t>(b - gObjectBase);
    if (OnStack(p)) return 0x57AC0000u;
    return p ? 0x4EA90000u : 0; // elsewhere (a heap block)
}

// ---- the description's functions (0xbe15d0 and the others) ------------------------------------------------------------
// The constructor writes a token in the first word (the destructor, the assignment and the copies log the tokens they
// see); the finishing pass and the parsing helpers log. The emit helper sets the two emit volume values from gEmitVolume.
inline uint32_t gToken = 0;
inline float gEmitVolume[2] = {};

inline void *__fastcall DescConstruct(void *self, void *)
{
    *static_cast<uint32_t *>(self) = 0xDE5C0000u + ++gToken;
    Log(0, Where(self), gToken);
    return self;
}
inline void __fastcall DescDestroy(void *self, void *) { Log(1, Where(self), *static_cast<uint32_t *>(self)); }
inline void *__fastcall DescAssign(void *self, void *, const void *from)
{
    Log(2, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
inline void *__fastcall DescCopy(void *self, void *, const void *from)
{
    Log(3, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
inline void __fastcall DescFinish(void *self, void *) { Log(4, Where(self), *static_cast<const uint32_t *>(self)); }
template <int Which> void __fastcall DescParse(void *self, void *, void *face, void *args)
{
    Log(Which, Where(self), reinterpret_cast<uint32_t>(face), reinterpret_cast<uint32_t>(args));
    if constexpr (Which == 11) std::memcpy(static_cast<uint8_t *>(self) + 0x50, gEmitVolume, 8);
}
// The rate parses (cdecl: rate, args, face, default) and the rate switches (args, face, state).
template <int Which> void __cdecl RateParse(void *rate, void *args, void *face, float fallback)
{
    Log(Which, Where(rate), reinterpret_cast<uint32_t>(args), reinterpret_cast<uint32_t>(face), Bits(fallback));
}
inline void __cdecl RateSwitches(void *args, void *face, void *state)
{
    Log(14, reinterpret_cast<uint32_t>(args), reinterpret_cast<uint32_t>(face), Where(state));
}
// The parser's ranged float (cdecl: face, text, least, most): answers from gRanged in turn.
inline float gRanged[4] = {};
inline int gRangedNext = 0;
inline float __cdecl RangedFloat(void *face, const char *text, float least, float most)
{
    Log(15, reinterpret_cast<uint32_t>(face), reinterpret_cast<uint32_t>(text), Bits(least), Bits(most));
    return gRanged[gRangedNext++ % 4];
}

// ---- the path's functions ----------------------------------------------------------------------------------------------
inline void *__fastcall PathConstruct(void *self, void *)
{
    std::memset(self, 0x9a, 0x34);
    Log(16, Where(self));
    return self;
}
template <int Which> void __fastcall PathCall(void *self, void *) { Log(Which, Where(self)); }
inline void __fastcall PathStore(void *self, void *, void *points) { Log(20, Where(self), Where(points)); }
template <int Which> void __fastcall PathVector(void *self, void *, const float *v)
{
    Log(Which, Where(self), Where(v));
    ExtraBytes(v, 12);
}
template <int Which> void __fastcall PathFloat(void *self, void *, float f) { Log(Which, Where(self), Bits(f)); }

// ---- colliders (0x84 bytes: a tag word here, the name at +4) and attractors (0x34: values vector at +0x20) ----------
inline uint8_t gColliderPattern = 0;
inline std::string &ColliderName(void *c) { return *reinterpret_cast<std::string *>(static_cast<uint8_t *>(c) + 4); }
// The strings at +0x54 and +0x6c as real ones (a vector's growth copies them), of the 15 characters that fill the inline
// buffer: every byte is the pattern's or the string's own, so bitwise copies stay valid and nothing is left unset.
inline void ColliderTail(uint8_t *b, uint8_t pattern)
{
    std::memset(b + 0x50, pattern, 0x34);
    for (const uint32_t at : {0x54u, 0x6cu}) {
        b[at + 15] = 0;
        reinterpret_cast<uint32_t *>(b + at + 0x10)[0] = 15; // size
        reinterpret_cast<uint32_t *>(b + at + 0x10)[1] = 15; // capacity
    }
}
inline void *__fastcall ColliderConstruct(void *self, void *)
{
    auto *b = static_cast<uint8_t *>(self);
    *reinterpret_cast<uint32_t *>(b) = 0xC0110000u + ++gToken;
    new (b + 4) std::string();
    std::memset(b + 0x1c, 0x33, 0x84 - 0x1c);
    ColliderTail(b, 0x33);
    Log(30, Where(self), gToken);
    return self;
}
inline void __fastcall ColliderDestroy(void *self, void *)
{
    Log(31, Where(self) & 0xffff0000u, *static_cast<uint32_t *>(self));
    ExtraText(ColliderName(self).c_str());
    ColliderName(self).~basic_string();
}
inline void ColliderCopyInto(uint8_t *to, const uint8_t *from)
{
    *reinterpret_cast<uint32_t *>(to) = *reinterpret_cast<const uint32_t *>(from) | 0x08000000u;
    new (to + 4) std::string(ColliderName(const_cast<uint8_t *>(from)));
    std::memcpy(to + 0x1c, from + 0x1c, 0x84 - 0x1c);
}
inline void *__fastcall ColliderCopy(void *self, void *, const void *from)
{
    Log(32, Where(from), *static_cast<const uint32_t *>(from));
    ExtraBytes(static_cast<const uint8_t *>(from) + 0x1c, 0x84 - 0x1c);
    ColliderCopyInto(static_cast<uint8_t *>(self), static_cast<const uint8_t *>(from));
    return self;
}
inline void __cdecl ColliderParse(void *face, void *args, void *collider)
{
    Log(33, reinterpret_cast<uint32_t>(face), reinterpret_cast<uint32_t>(args), Where(collider));
    ExtraText(ColliderName(collider).c_str());
    ColliderTail(static_cast<uint8_t *>(collider), ++gColliderPattern);
}

inline void *__fastcall AttractorConstruct(void *self, void *)
{
    auto *b = static_cast<uint8_t *>(self);
    std::memset(b, 0x44, 0x34);
    std::memset(b + 0x20, 0, 12);
    *reinterpret_cast<uint32_t *>(b) = 0xA7700000u + ++gToken;
    Log(34, Where(self), gToken);
    return self;
}
// ---- blocks whose freeing is observed: the image's sized operator delete (0x824624) and ours are patched to note when a
// watched block is freed (by the test's own thread), then free it as usual.
inline void *gWatched[4] = {};
inline uint32_t gWatchFreed = 0; // bit i: gWatched[i] was freed
inline DWORD gWatchThread = 0;
inline void Watch(int i, void *block) { gWatched[i] = block; }
inline void WatchFreeing(void *p)
{
    if (!p || GetCurrentThreadId() != gWatchThread) return;
    for (int i = 0; i < 4; ++i)
        if (gWatched[i] == p) {
            gWatchFreed |= 1u << i;
            gWatched[i] = nullptr;
        }
}
inline void __cdecl WatchSizedDelete(void *p, size_t)
{
    WatchFreeing(p);
    std::free(p);
}
inline void __cdecl WatchDelete(void *p)
{
    WatchFreeing(p);
    std::free(p);
}
struct FreeWatch {
    shape::CodePatch image{0x824624u, reinterpret_cast<const void *>(&WatchSizedDelete)};
    shape::CodePatch oursSized{reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&::operator delete)),
                               reinterpret_cast<const void *>(&WatchSizedDelete)};
    shape::CodePatch ours{reinterpret_cast<void *>(static_cast<void (*)(void *)>(&::operator delete)),
                          reinterpret_cast<const void *>(&WatchDelete)};
    FreeWatch() { gWatchThread = GetCurrentThreadId(); }
    ~FreeWatch()
    {
        for (auto &w : gWatched) w = nullptr;
    }
    FreeWatch(const FreeWatch &) = delete;
    FreeWatch &operator=(const FreeWatch &) = delete;
    // Before a side runs / after it: the freed bits are part of the side's result.
    static void Reset() { gWatchFreed = 0; }
    static void Capture() { ExtraWord(gWatchFreed); }
};

inline int gAttractorValues = 0;       // how many values the parse puts in the attractor's vector (watched: 0)
inline bool gAttractorParseResult = true; // what the parse answers (whether there was an attractor)
inline bool __fastcall AttractorParse(void *self, void *, void *face, void *args, uint32_t flag)
{
    Log(35, Where(self), reinterpret_cast<uint32_t>(face), reinterpret_cast<uint32_t>(args), flag);
    auto *v = reinterpret_cast<std::vector<uint32_t> *>(static_cast<uint8_t *>(self) + 0x20);
    for (int i = 0; i < gAttractorValues; ++i) v->push_back(0x1000u + i);
    if (!v->empty()) Watch(0, v->data());
    return gAttractorParseResult;
}
// The attractor's reset (0xbdded0), its preset curve (constructor 0xbdc8f0, filling 0xbdcbd0 from a float vector: its
// values vector at +4 is made here and watched: 1; the attractor's taking it 0xbde620).
inline void __fastcall AttractorReset(void *self, void *) { Log(40, Where(self)); }
inline void *__fastcall CurveConstruct(void *self, void *)
{
    std::memset(self, 0, 0x10);
    *static_cast<uint32_t *>(self) = 0xC0C00000u + ++gToken;
    Log(41, Where(self), gToken);
    return self;
}
inline void __fastcall CurveFill(void *self, void *, const std::vector<float> *values)
{
    Log(42, Where(self), static_cast<uint32_t>(values->size()));
    for (float f : *values) ExtraWord(Bits(f));
    auto *v = reinterpret_cast<std::vector<float> *>(static_cast<uint8_t *>(self) + 4);
    v->assign(values->begin(), values->end());
    if (!v->empty()) Watch(1, v->data());
}
inline void __fastcall CurveApply(void *self, void *, const void *curve)
{
    Log(43, Where(self), Where(curve), *static_cast<const uint32_t *>(curve));
}
// The rotate curve's parse (cdecl: curve, args, face, least) and the screw curves' (curve, values, count, face, default).
inline void __cdecl RotateCurve(void *curve, void *args, void *face, uint32_t least)
{
    Log(44, Where(curve), reinterpret_cast<uint32_t>(args), reinterpret_cast<uint32_t>(face), least);
}
inline void __cdecl ScrewCurve(void *curve, const char *const *values, uint32_t count, void *face, float fallback)
{
    Log(45, Where(curve), reinterpret_cast<uint32_t>(values), count, reinterpret_cast<uint32_t>(face));
    ExtraWord(Bits(fallback));
}
inline void *__fastcall AttractorCopy(void *self, void *, const void *from)
{
    Log(36, Where(from), *static_cast<const uint32_t *>(from));
    auto *v = reinterpret_cast<const std::vector<uint32_t> *>(static_cast<const uint8_t *>(from) + 0x20);
    ExtraWord(static_cast<uint32_t>(v->size()));
    std::memcpy(self, from, 0x34);
    std::memset(static_cast<uint8_t *>(self) + 0x20, 0, 12); // the copy's values: not kept
    return self;
}

// A vector's growing insert at the end (vector, where, element) is library code (0xbe09e0, 0xbe0b90), and ours is the
// toolchain's vector: the image's side runs the same one here, away from the element stand-ins it would otherwise call
// (ours, inlined, can't reach them). What growth leaves is still compared: the vector's elements, sizes and capacity.
template <typename T> void *__fastcall Grow(void *self, void *, uint8_t *, const void *element)
{
    auto *v = static_cast<std::vector<T> *>(self);
    v->push_back(*static_cast<const T *>(element));
    return &v->back();
}

// ---- the description manager (the effects parser's +0x1b0) and a found description ------------------------------------
inline uint32_t gFindResult = 0;
inline void *gFound = nullptr; // what slot 8 writes to its out-parameter
inline uint32_t __fastcall ManagerFind(void *self, void *, const char *name, uint32_t flag, void **out)
{
    Record(self, 8, reinterpret_cast<uint32_t>(name), flag, Arg(out));
    *out = gFound;
    return gFindResult;
}
// Slot 7 (name, component): logs the component's words the block wrote (not its heap address), frees it.
inline uint32_t __fastcall ManagerAdd(void *self, void *, const char *name, void *component)
{
    Record(self, 7, 0, component != nullptr);
    ExtraText(name);
    if (component) {
        const uint32_t *w = static_cast<const uint32_t *>(component);
        ExtraWord(w[0]);
        ExtraWord(w[1]);
        ExtraWord(w[2]);
        ExtraWord(w[4]); // the description's token
        ::operator delete(component);
    }
    return 0;
}

// ---- the face's float and vector slots (answers from tables, in turn) ----------------------------------------------------
inline float gFloats[8] = {};
inline int gFloatNext = 0;
inline float gVectors[4][3] = {};
inline int gVectorNext = 0;
inline float gElsewhere[3] = {};
inline bool gReturnElsewhere = false;
inline float __fastcall FaceEvalFloat(void *self, void *, const char *text)
{
    Record(self, 31, reinterpret_cast<uint32_t>(text));
    return gFloats[gFloatNext++ % 8];
}
inline float *__fastcall FaceEvalVector3(void *self, void *, float *out, const char *text, uint32_t least)
{
    Record(self, 35, Arg(out), reinterpret_cast<uint32_t>(text), least);
    const float *v = gVectors[gVectorNext++ % 4];
    for (int k = 0; k < 3; ++k) out[k] = v[k];
    return gReturnElsewhere ? gElsewhere : out;
}
inline float *__fastcall FaceEvalVector2(void *self, void *, float *out, const char *text, uint32_t least)
{
    Record(self, 34, Arg(out), reinterpret_cast<uint32_t>(text), least);
    const float *v = gVectors[gVectorNext++ % 4];
    for (int k = 0; k < 2; ++k) out[k] = v[k];
    return gReturnElsewhere ? gElsewhere : out;
}
// Slot 2 (TakeSwitchArguments: name, count, least, most): gSwitchArgs[name] with gSwitchCounts' count.
inline std::vector<std::pair<std::string, uint32_t>> gSwitchCounts;
inline const char *const *__fastcall ArgsTakeSwitchArgsRange(void *self, void *, const char *name, uint32_t *count, uint32_t least,
                                                             uint32_t most)
{
    Record(self, 2, reinterpret_cast<uint32_t>(name), Arg(count), least, most);
    for (const auto &s : gSwitchArgs)
        if (s.first == name) {
            for (const auto &c : gSwitchCounts)
                if (c.first == name && count) *count = c.second;
            return s.second;
        }
    return nullptr;
}

// Every stand-in, patched for a test's life (the image's functions and, where ours exist, ours).
struct ParticleStands {
    // The description's constructor, destructor, move and copy, the colliders' and attractors' constructors, destructor,
    // copies and curve setter are ours (lane IX): the stand-ins over the image's and ours alike.
    shape::StandIn descConstruct{0xbe15d0u, &cTSParticlePartsAdapters::ConstructParticles, &DescConstruct};
    shape::StandIn descDestroy{0xbe1be0u, &cTSParticlePartsAdapters::DestroyParticles, &DescDestroy};
    shape::StandIn descAssign{0xbe2320u, &cTSParticlePartsAdapters::MoveParticles, &DescAssign};
    shape::StandIn descCopy{0xbe2700u, &cTSParticlePartsAdapters::CopyParticles, &DescCopy};
    shape::CodePatch descFinish{0xbe58d0u, shape::CodeOf(&DescFinish)};
    shape::CodePatch color{0xbfbc50u, shape::CodeOf(&DescParse<5>)};
    shape::CodePatch alpha{0xbfbb60u, shape::CodeOf(&DescParse<6>)};
    shape::CodePatch size{0xbfc150u, shape::CodeOf(&DescParse<7>)};
    shape::CodePatch life{0xbfc0a0u, shape::CodeOf(&DescParse<8>)};
    shape::CodePatch aspect{0xbfbbf0u, shape::CodeOf(&DescParse<9>)};
    shape::CodePatch repel{0xbfc1b0u, shape::CodeOf(&DescParse<10>)};
    shape::CodePatch emit{0xbfbdd0u, shape::CodeOf(&DescParse<11>)};
    shape::CodePatch rate{0xbd1b70u, shape::CodeOf(&RateParse<12>)};
    shape::CodePatch injectRate{0xbd1ce0u, shape::CodeOf(&RateParse<13>)};
    shape::CodePatch rateSwitches{0xbe7020u, shape::CodeOf(&RateSwitches)};
    shape::CodePatch ranged{0xaf6f80u, shape::CodeOf(&RangedFloat)};
    shape::CodePatch pathConstruct{0xbfa4a0u, shape::CodeOf(&PathConstruct)};
    shape::CodePatch pathRestart{0xbfaaa0u, shape::CodeOf(&PathCall<17>)};
    shape::CodePatch pathFinish{0xbfac30u, shape::CodeOf(&PathCall<18>)};
    shape::CodePatch pathStore{0xbfb1f0u, shape::CodeOf(&PathStore)};
    shape::CodePatch pathDestroy{0xbe19f0u, shape::CodeOf(&PathCall<21>)};
    shape::CodePatch point{0xbfa890u, shape::CodeOf(&PathVector<22>)};
    shape::CodePatch deltaPoint{0xbfa6b0u, shape::CodeOf(&PathVector<23>)};
    shape::CodePatch velocity{0xbfaa10u, shape::CodeOf(&PathVector<24>)};
    shape::CodePatch speed{0xbfa8f0u, shape::CodeOf(&PathFloat<25>)};
    shape::CodePatch time{0xbfa980u, shape::CodeOf(&PathFloat<26>)};
    shape::CodePatch deltaTime{0xbfa790u, shape::CodeOf(&PathFloat<27>)};
    shape::StandIn colliderConstruct{0xbe1530u, &cTSParticlePartsAdapters::ConstructCollider, &ColliderConstruct};
    shape::StandIn colliderDestroy{0xbe1aa0u, &cTSParticlePartsAdapters::DestroyCollider, &ColliderDestroy};
    shape::StandIn colliderCopy{0xbe1490u, &cTSParticlePartsAdapters::CopyCollider, &ColliderCopy};
    shape::CodePatch colliderGrow{0xbe09e0u, shape::CodeOf(&Grow<cTSParticleCollider>)};
    shape::CodePatch colliderParse{0xbdf9c0u, shape::CodeOf(&ColliderParse)};
    shape::StandIn attractorConstruct{0xbddd10u, &cTSParticlePartsAdapters::ConstructAttractor, &AttractorConstruct};
    shape::CodePatch attractorParse{0xbde020u, shape::CodeOf(&AttractorParse)};
    shape::StandIn attractorCopy{0xbe13d0u, &cTSParticlePartsAdapters::CopyAttractor, &AttractorCopy};
    shape::CodePatch attractorGrow{0xbe0b90u, shape::CodeOf(&Grow<cTSParticleAttractor>)};
    shape::CodePatch attractorReset{0xbdded0u, shape::CodeOf(&AttractorReset)};
    // The curve's constructor and SetValues are ours (lane IW): the stand-ins over the image's and ours alike.
    shape::StandIn curveConstruct{0xbdc8f0u, &cTSEffectCurveAdapters::Construct, &CurveConstruct};
    shape::StandIn curveFill{0xbdcbd0u, &cTSEffectCurve::SetValues, &CurveFill};
    shape::StandIn curveApply{0xbde620u, &cTSParticlePartsAdapters::SetCurve, &CurveApply};
    shape::CodePatch rotateCurve{0xbd1a50u, shape::CodeOf(&RotateCurve)};
    shape::CodePatch screwCurve{0xbd1be0u, shape::CodeOf(&ScrewCurve)};
};

// One of the parser's name tables (unset in the harness's image: the alignment names 0x10fd18c, texture -draw's
// 0x10fd180, the screw speed tables' 0x10fd198): filled for the tests, put back after.
struct EnumTableFill {
    difftest::Context &ctx;
    uint32_t table;
    uint8_t saved[sizeof(std::vector<cp::EnumEntry>)];
    EnumTableFill(difftest::Context &c, uint32_t rva, int32_t bias) : ctx(c), table(rva)
    {
        const std::vector<cp::EnumEntry> entries = {{"a", bias}, {"bb", 3 + bias}, {"ccc", 0x101 + bias}, {"dddd", 0x7f + bias}};
        std::memcpy(saved, ctx.image.At(table), sizeof(saved));
        new (ctx.image.At(table)) std::vector<cp::EnumEntry>(entries);
    }
    ~EnumTableFill()
    {
        using V = std::vector<cp::EnumEntry>;
        ctx.image.At<V>(table)->~V();
        std::memcpy(ctx.image.At(table), saved, sizeof(saved));
    }
    EnumTableFill(const EnumTableFill &) = delete;
    EnumTableFill &operator=(const EnumTableFill &) = delete;
};
struct AlignmentTable {
    EnumTableFill align, draw, speeds;
    explicit AlignmentTable(difftest::Context &c) : align(c, 0x10fd18c, 0), draw(c, 0x10fd180, 0x20), speeds(c, 0x10fd198, 0x40) {}
};

// The game's zero vector (0x10fd1a4, the source box's start; the meta source's 0x10fd210): set to the given values for
// a test, put back after.
struct ZeroVector {
    difftest::Context &ctx;
    uint32_t rva;
    float saved[3];
    ZeroVector(difftest::Context &c, const float *values, uint32_t at = 0x10fd1a4) : ctx(c), rva(at)
    {
        std::memcpy(saved, ctx.image.At(rva), sizeof(saved));
        std::memcpy(ctx.image.At(rva), values, sizeof(saved));
    }
    ~ZeroVector() { std::memcpy(ctx.image.At(rva), saved, sizeof(saved)); }
    ZeroVector(const ZeroVector &) = delete;
    ZeroVector &operator=(const ZeroVector &) = delete;
};

inline const char *const kTexts[] = {"", "smoke", "a much longer name than fifteen", "x y", "light", "plane"};

// The particles state's containers, rebuilt before each side's run and read (then freed) after: the three strings, the
// colliders (each with a name), the attractors.
// A vector of plain elements in the state (the pivots, the warp forces): its size and spare room drawn, its elements a
// pattern; after a run its size, capacity, whether its block was kept and its elements are logged, then it is freed.
struct PlainVector {
    size_t offset, size, count = 0, capacity = 0;
    uint8_t *block = nullptr;
    PlainVector(size_t at, size_t elementSize, std::mt19937 &gen) : offset(at), size(elementSize)
    {
        count = gen() % 4;
        capacity = count + gen() % 3;
    }
    RawVector *In(uint8_t *state) const { return reinterpret_cast<RawVector *>(state + offset); }
    void Before(uint8_t *state)
    {
        block = capacity ? std::allocator<uint8_t>().allocate(capacity * size) : nullptr;
        for (size_t i = 0; i < count * size; ++i) block[i] = static_cast<uint8_t>(0x70 + i);
        *In(state) = {block, block + count * size, block + capacity * size};
    }
    void After(uint8_t *state)
    {
        RawVector *v = In(state);
        ExtraWord(static_cast<uint32_t>(v->mEnd - v->mBegin));
        ExtraWord(static_cast<uint32_t>(v->mCapacity - v->mBegin));
        ExtraWord(v->mBegin == block);
        if (v->mBegin != v->mEnd) ExtraBytes(v->mBegin, static_cast<size_t>(v->mEnd - v->mBegin));
        if (v->mBegin) std::allocator<uint8_t>().deallocate(v->mBegin, static_cast<size_t>(v->mCapacity - v->mBegin));
        std::memset(v, 0, sizeof(*v));
    }
};

// A state's attractors after a run, their values destroyed: each one's bytes around its values and the values themselves
// (growth copies them, so the words at +0x20 are this side's own heap pointers). A block that growth replaced holds real
// element copies, which leave the padding after the first byte as the heap had it; in the first block every byte is the
// stand-ins'.
inline void CaptureAttractors(const RawVector &a, const uint8_t *firstBlock)
{
    const bool grown = a.mBegin != firstBlock;
    for (uint8_t *e = a.mBegin; e != a.mEnd; e += 0x34) {
        ExtraBytes(e, grown ? 1 : 4);
        ExtraBytes(e + 4, 0x1c);
        auto *values = reinterpret_cast<std::vector<uint32_t> *>(e + 0x20);
        ExtraWord(static_cast<uint32_t>(values->size()));
        ExtraWord(static_cast<uint32_t>(values->capacity()));
        if (!values->empty()) ExtraBytes(values->data(), values->size() * sizeof(uint32_t));
        ExtraBytes(e + 0x2c, 8);
        values->~vector();
    }
}

struct StateContainers {
    World &w;
    const char *strings[4] = {"", "", "", ""};
    static constexpr size_t kStrings[4] = {0x1d8, 0x238, 0x274, 0x290}; // material, constrain, deathEffect, name
    size_t colliders = 0, colliderCapacity = 0, attractors = 0, attractorCapacity = 0;
    uint8_t *colliderBlock = nullptr, *attractorBlock = nullptr;
    PlainVector pivots, warps;

    explicit StateContainers(World &world) : w(world), pivots(0x1c0, 12, world.gen), warps(0xfc, 0x1c, world.gen)
    {
        for (auto &s : strings) s = kTexts[w.gen() % std::size(kTexts)];
        colliders = w.gen() % 3;
        colliderCapacity = colliders + w.gen() % 2;
        attractors = w.gen() % 3;
        attractorCapacity = attractors + w.gen() % 2;
        Clear();
    }
    void Clear()
    {
        for (size_t off : kStrings) std::memset(w.state + off, 0, 0x18);
        std::memset(w.state + 0x138, 0, 0x18);
        std::memset(w.state + 0x1c0, 0, 0xc);
        std::memset(w.state + 0xfc, 0, 0xc);
    }
    void Before()
    {
        pivots.Before(w.state);
        warps.Before(w.state);
        for (int i = 0; i < 4; ++i) new (w.state + kStrings[i]) std::string(strings[i]);
        auto *c = reinterpret_cast<RawVector *>(w.state + 0x138);
        colliderBlock = colliderCapacity ? std::allocator<uint8_t>().allocate(colliderCapacity * 0x84) : nullptr;
        for (size_t i = 0; i < colliders; ++i) {
            uint8_t *e = colliderBlock + i * 0x84;
            *reinterpret_cast<uint32_t *>(e) = 0xC0000000u + static_cast<uint32_t>(i);
            new (e + 4) std::string(kTexts[i]);
            std::memset(e + 0x1c, static_cast<int>(i), 0x84 - 0x1c);
        }
        *c = {colliderBlock, colliderBlock + colliders * 0x84, colliderBlock + colliderCapacity * 0x84};
        auto *a = reinterpret_cast<RawVector *>(w.state + 0x144);
        attractorBlock = attractorCapacity ? std::allocator<uint8_t>().allocate(attractorCapacity * 0x34) : nullptr;
        for (size_t i = 0; i < attractors; ++i) {
            std::memset(attractorBlock + i * 0x34, 0x60 + static_cast<int>(i), 0x34);
            std::memset(attractorBlock + i * 0x34 + 0x20, 0, 12);
        }
        *a = {attractorBlock, attractorBlock + attractors * 0x34, attractorBlock + attractorCapacity * 0x34};
    }
    void After()
    {
        for (size_t off : kStrings) {
            auto *s = reinterpret_cast<std::string *>(w.state + off);
            ExtraText(s->c_str());
            s->~basic_string();
        }
        auto *c = reinterpret_cast<RawVector *>(w.state + 0x138);
        ExtraWord(static_cast<uint32_t>(c->mEnd - c->mBegin));
        ExtraWord(static_cast<uint32_t>(c->mCapacity - c->mBegin));
        ExtraWord(c->mBegin == colliderBlock);
        for (uint8_t *e = c->mBegin; e != c->mEnd; e += 0x84) {
            ExtraBytes(e, 4);
            ExtraText(ColliderName(e).c_str());
            ExtraBytes(e + 0x1c, 0x84 - 0x1c);
            ColliderName(e).~basic_string();
        }
        if (c->mBegin) std::allocator<uint8_t>().deallocate(c->mBegin, static_cast<size_t>(c->mCapacity - c->mBegin));
        auto *a = reinterpret_cast<RawVector *>(w.state + 0x144);
        ExtraWord(static_cast<uint32_t>(a->mEnd - a->mBegin));
        ExtraWord(static_cast<uint32_t>(a->mCapacity - a->mBegin));
        ExtraWord(a->mBegin == attractorBlock);
        CaptureAttractors(*a, attractorBlock);
        if (a->mBegin) std::allocator<uint8_t>().deallocate(a->mBegin, static_cast<size_t>(a->mCapacity - a->mBegin));
        pivots.After(w.state);
        warps.After(w.state);
        Clear();
    }
};

// ---- the block tests' helpers ------------------------------------------------------------------------------------------

using SetContextFn = void(__thiscall *)(void *, void *, void *);
using ExecuteFn = void(__thiscall *)(void *, void *);
using VoidFn = void(__thiscall *)(void *);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

// Slot 10 of the block under test (AddCommand): logs the child's words that are not heap pointers or left unset (a
// command's state word), its name, a block child's table and its own fields; then destroys it.
inline uint32_t __fastcall ParticleAddCommand(void *self, void *, void *child)
{
    gLog.push_back({-1, 10, Where(self), {child != nullptr, 0, 0, 0}});
    if (!child) return 0;
    const uint32_t *w = static_cast<const uint32_t *>(child);
    const uint32_t size = static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, child));
    ExtraWord(size);
    ExtraWord(w[0]);
    ExtraWord(w[1]);
    ExtraWord(w[2]);
    if (size == sizeof(cEffectsCommand)) {
        auto *c = static_cast<cEffectsCommand *>(child);
        ExtraText(c->mName.c_str());
        c->mName.~basic_string();
    } else {
        auto *b = static_cast<cp::cBlockCommandBase *>(child);
        ExtraWord(w[3]);
        ExtraWord(b->mCommands.size);
        ExtraWord(b->mCommands.bucketCount);
        ExtraWord(w[0x44 / 4]);
        ExtraWord(w[0x48 / 4]);
        ExtraBytes(static_cast<uint8_t *>(child) + 0x4c, 0x34);
        b->Destruct();
    }
    // Scrubbed: the next side's child may be given the same block, and must not find this one's words in it.
    std::memset(child, 0xee, size);
    ::operator delete(child);
    return 0;
}

// cBlockCommand's destructor body (0xaf7970, ours and the image's): logs whether it ran on the object under test.
inline void *gCurrent = nullptr;
inline void __fastcall StandBlockDestruct(void *self, void *) { Log(40, self == gCurrent); }

inline std::vector<Region> With(World &w, void *object, size_t size)
{
    std::vector<Region> r = w.Regions();
    r.push_back({object, size});
    return r;
}

template <class A, class B, class After, class Before>
void Run(difftest::Context &ctx, const char *what, const std::vector<Region> &regions, A a, B b, After after, Before before)
{
    Differential(
        ctx, what, regions, [&] { return RunGuarded(a); }, [&] { return RunGuarded(b); }, after, before);
}

alignas(16) inline uint8_t gObject[0x300];

inline void RandomObject(World &w, size_t size)
{
    for (size_t i = 0; i < size; i += 4) *reinterpret_cast<uint32_t *>(gObject + i) = w.gen();
    gObjectBase = gObject;
    gStateBase = w.state;
}

inline const char *const kBlockWords[] = {"fire", ":", "base", "", "a much longer description name", ";", "::"};

// A deleting destructor's case: the object on the heap (built from gObject before each side's run), the block's
// destructor body logged; the object's bytes after (when it is not freed) and the result (whether it is `this`).
template <class Stands, class Ours>
void DestructorCases(difftest::Context &ctx, uint32_t rva, size_t size, size_t nameOffset, Ours ours, int cases)
{
    auto orig = ctx.Original<DtorFn>(rva);
    Stands stands;
    shape::StandIn destruct{0xaf7970, &cp::cBlockCommandBase::Destruct, &StandBlockDestruct};
    std::mt19937 gen(rva);
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        RandomObject(w, size);
        const uint32_t flags = gen() % 2 ? 1u : (gen() % 2 ? 0u : 0xfffffffeu);
        const char *name = kTexts[gen() % std::size(kTexts)];
        uint8_t *object = nullptr;
        w.before = [&] {
            object = static_cast<uint8_t *>(::operator new(size));
            std::memcpy(object, gObject, size);
            if (nameOffset) new (object + nameOffset) std::string(name);
            gCurrent = object;
            gObjectBase = object; // the stand-ins log pointers into it as offsets
        };
        w.after = [&] {
            if (!(flags & 1)) {
                // A long name's freed block address stays in the string's first word (its first byte cleared): the
                // two sides' blocks differ, so its other three bytes are not compared.
                if (nameOffset && std::strlen(name) > 15) std::memset(object + nameOffset + 1, 0, 3);
                ExtraBytes(object, size);
                ::operator delete(object);
            }
        };
        char what[64];
        std::snprintf(what, sizeof(what), "destructor case %d", i);
        Run(
            ctx, what, w.Regions(), [&] { return orig(object, flags) == object; }, [&] { return ours(object, flags) == object; },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

} // namespace ptest
