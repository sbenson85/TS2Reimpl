// Differential tests for the effects' value curve cTSEffectCurve (src/ts/cTSEffectCurve.*), lane IW.
//
// Two ways to run the same random operations, compared word for word: mode 0 the original functions at their RVAs on a curve
// the original constructor built, mode 1 our members on a curve ours built (the hooks point at the members themselves; the
// constructors' adapters are run by EffectCurve_Construct). After each operation: the result, the call log of the fake
// streams and of the check stand-in, the kind, the values (count, capacity, bits).
//
// Covered RVAs: 0xbdc8f0 / 0xbdc880 (constructors), 0xbdcb80 (Reset), 0xbdcd10 (SetConstant), 0xbdcd40 (SetKind),
// 0xbdcd60 (Simplify), 0xbdcbd0 (SetValues), 0xbdca50 (GetMax), 0xbdcac0 (Read), 0xbdce40 (Write), 0xbdcdb0 and the error
// 0xbdc6f0, 0xbdc760 (tested through what is thrown, at the end). Stood in on both sides in the
// operations: 0xbdcdb0, which EffectCurve_CheckSinCoefficients tests on its own.
#include "ts_effects_collection.h"

#include "code_patch.h"
#include "ts/cTSEffectCurve.h"

#include <cfloat>
#include <cmath>
#include <windows.h>

namespace {

using namespace fxtest;

enum : uint32_t {
    evGet = 0xd7000000,
    evPut = 0xd8000000,
    evCheck = 0xd9000000,
    evTrap = 0xda000000,
};

// ---- streams (cIGZIStream / cIGZOStream: slot 9 Uint32, slot 12 Float32) ----

struct InStream {
    void *const *vt;
    std::vector<uint32_t> words;
    std::vector<uint8_t> fails;
    size_t pos = 0;
};
bool __fastcall InGet(InStream *s, void *, uint32_t *out)
{
    const bool have = s->pos < s->words.size();
    const bool ok = have && !s->fails[s->pos];
    const uint32_t w = have ? s->words[s->pos] : 0;
    if (have) ++s->pos;
    Ev({evGet, uint32_t(ok), w});
    if (ok) *out = w;
    return ok;
}
bool __fastcall OutPut(void *, void *, uint32_t v)
{
    Ev({evPut, v});
    return true;
}
void __fastcall StreamTrap(void *, void *)
{
    Ev({evTrap});
}
struct StreamVTables {
    void *in[21];
    void *out[21];
    StreamVTables()
    {
        for (void *&p : in) p = reinterpret_cast<void *>(&StreamTrap);
        for (void *&p : out) p = reinterpret_cast<void *>(&StreamTrap);
        in[9] = in[12] = reinterpret_cast<void *>(&InGet);
        out[9] = out[12] = reinterpret_cast<void *>(&OutPut);
    }
};
const StreamVTables kStreams;

uint32_t FloatBits(float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}
float RandomFloat(std::mt19937 &r)
{
    switch (r() % 10) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return std::nanf("");
    case 3: return 1.0f;
    default: return float(int(r() % 2000) - 1000) / 8.0f;
    }
}
// Values for SetValues: 0 to 6 of them, often all equal (Simplify's case).
std::vector<float> RandomValues(std::mt19937 &r)
{
    const uint32_t n = r() % 7;
    const bool same = r() % 3 == 0;
    const float first = RandomFloat(r);
    std::vector<float> v;
    for (uint32_t i = 0; i < n; ++i) v.push_back(same ? first : RandomFloat(r));
    return v;
}
uint32_t RandomKind(std::mt19937 &r)
{
    switch (r() % 4) {
    case 0: return cTSEffectCurve::kList;
    case 1: return cTSEffectCurve::kWave;
    case 2: return 0x05000000u;
    default: return r() % 2 ? cTSEffectCurve::kWave : 0x81000001u;
    }
}
// A tape: the kind and count word (a list's count 0 to 7, a wave's 0 to 6, now and then another kind), then floats; a few
// float reads fail, the tape at times cut short.
void FillTape(InStream &s, std::mt19937 &r)
{
    const uint32_t kinds[] = {0u, 0u, cTSEffectCurve::kWave, cTSEffectCurve::kWave, 0x05000000u, 0x80000000u};
    const uint32_t kind = kinds[r() % 6];
    const uint32_t count = r() % (kind == 0 ? 8 : 7);
    s.words.push_back(kind | count);
    s.fails.push_back(0);
    const uint32_t floats = r() % 4 ? count : r() % (count + 1);
    for (uint32_t i = 0; i < floats; ++i) {
        s.words.push_back(FloatBits(RandomFloat(r)));
        s.fails.push_back(uint8_t(r() % 10 == 0));
    }
}

// ---- the check (0xbdcdb0) stand-in ----

void __fastcall StandCheck(const cTSEffectCurve *self, void *, const std::vector<float> *values)
{
    Ev({evCheck, self->mKind, uint32_t(values->size())});
}

void PushCurve(Blob &b, const cTSEffectCurve *c)
{
    b.push_back(c->mKind);
    b.push_back(uint32_t(c->mValues.size()));
    b.push_back(uint32_t(c->mValues.capacity()));
    for (float f : c->mValues) b.push_back(FloatBits(f));
}

// One operation by the original (mode 0) or ours (mode 1); `in` is the stream a read uses.
void Operate(difftest::Context &ctx, int mode, cTSEffectCurve *c, uint32_t op, std::mt19937 &r, InStream &in, Blob &b)
{
    switch (op) {
    case 0:
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbdcb80)(c);
        else
            c->Reset();
        break;
    case 1: {
        const float v = RandomFloat(r);
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *, float)>(0xbdcd10)(c, v);
        else
            c->SetConstant(v);
        break;
    }
    case 2: {
        const uint32_t kind = RandomKind(r);
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xbdcd40)(c, kind);
        else
            c->SetKind(kind);
        break;
    }
    case 3:
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbdcd60)(c);
        else
            c->Simplify();
        break;
    case 4:
    case 5: {
        std::vector<float> values = RandomValues(r);
        const bool own = r() % 8 == 0; // its own values
        const std::vector<float> *from = own ? &c->mValues : &values;
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *, const void *)>(0xbdcbd0)(c, from);
        else
            c->SetValues(*from);
        break;
    }
    case 6: {
        const float got = mode == 0 ? ctx.Original<float(__thiscall *)(const void *)>(0xbdca50)(c) : c->GetMax();
        b.push_back(FloatBits(got));
        break;
    }
    case 7:
    case 8: {
        in.vt = kStreams.in;
        in.words.clear();
        in.fails.clear();
        in.pos = 0;
        FillTape(in, r);
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *, void *)>(0xbdcac0)(c, &in);
        else
            c->Read(&in);
        b.push_back(uint32_t(in.pos));
        break;
    }
    default: {
        void *const *out = kStreams.out;
        if (mode == 0)
            ctx.Original<void(__thiscall *)(const void *, void *)>(0xbdce40)(c, &out);
        else
            c->Write(&out);
        break;
    }
    }
}

} // namespace

// Random operations over every member on a curve each side's default constructor built.
DIFF_TEST(EffectCurve_Operations, 0xbdcbd0)
{
    shape::StandIn check{0xbdcdb0u, &cTSEffectCurve::CheckSinCoefficients, &StandCheck};
    InStream in{kStreams.in};
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            gEv.clear();
            std::mt19937 r(seed);
            alignas(8) uint8_t block[sizeof(cTSEffectCurve)];
            for (uint8_t &x : block) x = uint8_t(r());
            auto *c = reinterpret_cast<cTSEffectCurve *>(block);
            if (mode == 0)
                ctx.Original<void *(__thiscall *)(void *)>(0xbdc8f0)(block);
            else
                ::new (block) cTSEffectCurve;
            PushCurve(b, c);
            for (int step = 0; step < 30; ++step) {
                const uint32_t op = r() % 10;
                b.push_back(0x0b000000u | op);
                Operate(ctx, mode, c, op, r, in, b);
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushCurve(b, c);
            }
            c->~cTSEffectCurve();
        }
        Same(ctx, blobs[0], blobs[1], "curve", seed);
    }
}

// The constructors (default 0xbdc8f0 and from a value 0xbdc880) on garbage: the original, ours, the adapters.
DIFF_TEST(EffectCurve_Construct, 0xbdc880)
{
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed);
            alignas(8) uint8_t block[sizeof(cTSEffectCurve)];
            for (uint8_t &x : block) x = uint8_t(r());
            const bool value = seed % 2;
            const float v = RandomFloat(r);
            void *got;
            if (mode == 0)
                got = value ? ctx.Original<void *(__thiscall *)(void *, float)>(0xbdc880)(block, v)
                            : ctx.Original<void *(__thiscall *)(void *)>(0xbdc8f0)(block);
            else if (mode == 1)
                got = value ? ::new (block) cTSEffectCurve(v) : ::new (block) cTSEffectCurve;
            else
                got = value ? cTSEffectCurveAdapters::ConstructValue(block, nullptr, v)
                            : cTSEffectCurveAdapters::Construct(block, nullptr);
            b.push_back(uint32_t(got == block));
            PushCurve(b, reinterpret_cast<cTSEffectCurve *>(block));
            reinterpret_cast<cTSEffectCurve *>(block)->~cTSEffectCurve();
        }
        Same(ctx, blobs[0], blobs[1], "construct (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "construct (adapters)", seed);
    }
}

// GetMax (0xbdca50) over chosen lists: NaN first, last and alone, the empty list (only Read can leave one: kind 0 with
// no values is reset, so it is built by hand), equal values, a wave, another kind.
DIFF_TEST(EffectCurve_Max, 0xbdca50)
{
    const float nan = std::nanf("");
    const std::vector<float> lists[] = {{nan}, {nan, 1.0f}, {1.0f, nan}, {-2.0f, nan, 3.0f}, {}, {-0.0f, 0.0f},
                                        {-FLT_MAX}, {5.0f, -7.0f, 2.0f}, {-1.0f, 2.0f, 0.5f, 4.0f}};
    const uint32_t kinds[] = {cTSEffectCurve::kList, cTSEffectCurve::kWave, 0x05000000u};
    uint32_t seed = 0;
    for (uint32_t kind : kinds)
        for (const std::vector<float> &values : lists) {
            Blob blobs[2];
            for (int mode = 0; mode < 2; ++mode) {
                cTSEffectCurve c;
                c.mKind = kind;
                c.mValues = values;
                if (kind == cTSEffectCurve::kWave) c.mValues.resize(4, 1.5f);
                const float got = mode == 0 ? ctx.Original<float(__thiscall *)(const void *)>(0xbdca50)(&c) : c.GetMax();
                blobs[mode].push_back(FloatBits(got));
            }
            Same(ctx, blobs[0], blobs[1], "max", seed++);
        }
}

// Read's kind and count word when the read fails: the original reads it over its stream argument's stack slot, so the word
// is the stream's address. A stream placed at 0x81000003 (if that address is free) gives a wave with three values.
DIFF_TEST(EffectCurve_ReadFailedWord, 0xbdcac0)
{
    void *page = VirtualAlloc(reinterpret_cast<void *>(0x81000000u), 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!page) {
        std::printf("  EffectCurve_ReadFailedWord: 0x81000000 not free, skipped\n");
        return;
    }
    Blob blobs[2];
    for (int mode = 0; mode < 2; ++mode) {
        Blob &b = blobs[mode];
        gEv.clear();
        auto *in = ::new (static_cast<uint8_t *>(page) + 3) InStream{kStreams.in};
        in->words = {0x12345678u, FloatBits(2.5f), FloatBits(-1.0f), FloatBits(7.0f), FloatBits(9.0f)};
        in->fails = {1, 0, 0, 0, 0};
        cTSEffectCurve c;
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *, void *)>(0xbdcac0)(&c, in);
        else
            c.Read(in);
        b.push_back(uint32_t(in->pos));
        b.insert(b.end(), gEv.begin(), gEv.end());
        PushCurve(b, &c);
        in->~InStream();
    }
    VirtualFree(page, 0, MEM_RELEASE);
    Same(ctx, blobs[0], blobs[1], "failed word", 0);
}

// ---- what the curve throws -----------------------------------------------------------------------------------------

namespace {

// The object read from the exception record, as command_parser.h's Thrown does for a cError; the message is a cRZString here.
int CaptureCurveError(EXCEPTION_POINTERS *p, std::string *out)
{
    const EXCEPTION_RECORD *r = p->ExceptionRecord;
    if (r->ExceptionCode != 0xE06D7363 || r->NumberParameters < 3) return EXCEPTION_CONTINUE_SEARCH;
    const auto *error = reinterpret_cast<const cTSCurveError *>(r->ExceptionInformation[1]);
    char head[64];
    std::snprintf(head, sizeof(head), "%08x %08x %u %u ", uint32_t(reinterpret_cast<uintptr_t>(error->mVTable)),
                  uint32_t(reinterpret_cast<uintptr_t>(error->mMessage.mVTable)), error->mMessage.mRefCount,
                  uint32_t(error->mMessage.mString.capacity()));
    *out = head + error->mMessage.mString;
    const auto *info = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[2]);
    const auto *types = reinterpret_cast<const uint32_t *>(uintptr_t(info[3]));
    for (uint32_t i = 0; i < types[0]; ++i) {
        const auto *type = reinterpret_cast<const uint32_t *>(uintptr_t(types[1 + i]));
        char one[96];
        std::snprintf(one, sizeof(one), ";%s:%x", reinterpret_cast<const char *>(uintptr_t(type[1])) + 8, type[5]);
        *out += one;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

using CheckFn = void(__thiscall *)(const void *, const void *);
struct CheckCall {
    CheckFn original; // null: ours
    const cTSEffectCurve *curve;
    const std::vector<float> *values;
};
void RunCheck(const CheckCall &c)
{
    if (c.original) c.original(c.curve, c.values);
    else c.curve->CheckSinCoefficients(*c.values);
}
bool Guarded(const CheckCall &c, std::string *out)
{
    __try {
        RunCheck(c);
        return false;
    } __except (CaptureCurveError(GetExceptionInformation(), out)) {
        return true;
    }
}

const float kCheckValues[] = {0.0f, -0.0f, 1.0f, -2.5f, 1e-30f};

} // namespace

// Lists and waves, 0 to 4 values, each of the first two zero (either sign) or not.
DIFF_TEST(EffectCurve_CheckSinCoefficients, 0xbdcdb0)
{
    const CheckFn original = ctx.Original<CheckFn>(0xbdcdb0);
    std::mt19937 r(0xbdcdb0);
    uint32_t threw = 0;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        cTSEffectCurve curve;
        const uint32_t kinds[] = {cTSEffectCurve::kWave, cTSEffectCurve::kWave, cTSEffectCurve::kList, 2u, 0x80000000u};
        curve.mKind = kinds[r() % 5];
        std::vector<float> values(r() % 5);
        for (float &v : values) v = kCheckValues[r() % 5];
        std::string thrown[2];
        const bool threwOriginal = Guarded({original, &curve, &values}, &thrown[0]);
        const bool threwOurs = Guarded({nullptr, &curve, &values}, &thrown[1]);
        if (threwOriginal != threwOurs || thrown[0] != thrown[1])
            ctx.Fail("seed %u: threw %d '%s', ours %d '%s'", seed, int(threwOriginal), thrown[0].c_str(), int(threwOurs),
                     thrown[1].c_str());
        ++ctx.cases;
        threw += threwOurs;
    }
    if (threw < 500 || threw > 2500) ctx.Fail("%u of the seeds threw", threw);
}

// The constructor's formatting (the game's four texts have no conversions: this is what any would do) and the copy.
DIFF_TEST(EffectCurve_Error, 0xbdc6f0)
{
    using Make = void *(__cdecl *)(void *, const char *, ...);
    using Copy = void *(__thiscall *)(void *, const void *);
    const auto make = ctx.Original<Make>(0xbdc6f0);
    const auto copy = ctx.Original<Copy>(0xbdc760);
    const size_t lengths[] = {0, 1, 15, 16, 17, 100, 2046, 2047};
    std::mt19937 r(0xbdc6f0);
    const auto same = [&](const cTSCurveError &a, const cTSCurveError &b, const char *what, uint32_t seed) {
        if (a.mVTable != b.mVTable || a.mMessage.mVTable != b.mMessage.mVTable || a.mMessage.mRefCount != b.mMessage.mRefCount ||
            a.mMessage.mString != b.mMessage.mString || a.mMessage.mString.capacity() != b.mMessage.mString.capacity())
            ctx.Fail("%s differs, seed %u: count %u/%u, sizes %u/%u", what, seed, a.mMessage.mRefCount, b.mMessage.mRefCount,
                     uint32_t(a.mMessage.mString.size()), uint32_t(b.mMessage.mString.size()));
    };
    for (uint32_t seed = 0; seed < 400; ++seed) {
        const std::string text(lengths[seed % 8], char('a' + r() % 26));
        const uint32_t number = r();
        alignas(8) uint8_t a[0x24], b[0x24], c[0x24], d[0x24];
        const char *format = seed % 2 && text.size() < 2000 ? "%s=%x" : "%s";
        make(a, format, text.c_str(), number);
        auto &first = *reinterpret_cast<cTSCurveError *>(a);
        auto &ours = *new (b) cTSCurveError(format, text.c_str(), number);
        same(first, ours, "constructed", seed);
        if (ours.mVTable != ctx.image.At<void>(cTSCurveError::kVTable)) ctx.Fail("the error's table");
        // A source with references: the copy starts with none.
        first.mMessage.mRefCount = ours.mMessage.mRefCount = 7;
        copy(c, a);
        auto &second = *new (d) cTSCurveError(ours);
        same(*reinterpret_cast<cTSCurveError *>(c), second, "copied", seed);
        ++ctx.cases;
        for (uint8_t *object : {a, b, c, d}) reinterpret_cast<cTSCurveError *>(object)->mMessage.mString.~basic_string();
    }
}
