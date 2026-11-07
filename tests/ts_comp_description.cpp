// Differential tests for the effect component descriptions cTSCompDescriptionBaseT<D, N> (src/ts/cTSCompDescription.*),
// lane IT: brush (4), scrubber (5), sound (7), game (9), water (12), sequence (6), model (10).
//
// Three ways to run the same random operations, compared word for word:
//   mode 0: the original factory builds the description (game vtables); the class's own slots are called at their RVAs, the
//           folded ones through the game's vtable;
//   mode 1: our factory builds it; every slot through our vtables (the compiler's thunks on the +4 face);
//   mode 2: the original factory builds it; the adapters (the hook entries) are called.
// After each operation: the result, the call log of the fake streams and stand-ins, the description struct (its bytes,
// padding left out), the reference count, and a copy target's description. At the end the deletion: the frees.
//
// Fakes: an input stream replaying a tape (each getter takes the next word, a few fail without writing) and an output stream
// recording each setter. Stood in on both sides: the sound and water preparation passes 0xc05670 / 0xc09210 (not ours).
//
// Covered RVAs: brush 0xbf8af0 (factory), 0xbf8600 (description constructor), 0xbf6820 (CopyTo, shared with water),
// 0xbf89a0 / 0xbf8b70 (Read / Write), 0xbf89e0 / 0xbf8ba0 (the description serializers), 0xbf6770 (deleting destructor,
// shared with water); scrubber 0xbfa140, 0xbf9f80, 0xbfa020, 0xbfa060, 0xbfa1c0, 0xbfa0a0, 0xbfa1f0, 0xbf9ff0 and the thunk
// 0xbf9fdd; sound 0xbf25d0, 0xbf2110, 0xbf2530 (CreateEffect), 0xbf2170, 0xbf2540, 0xbf26f0, 0xbf2580, 0xbf2720, 0xbf2140 and the
// thunk 0xbf2135; game 0xbfa3a0, 0xbfa420 (GetType), 0xbfa2e0, 0xbfa310, 0xbfa430, 0xbfa350, 0xbfa460, 0xbfa2b0 and the
// thunk 0xbfa29e; water 0xbf6f90, 0xbf66f0 (constructor), 0xbf6c30 (CreateEffect), 0xbf6eb0, 0xbf7170, 0xbf6ef0, 0xbf71a0;
// sequence 0xbf4370, 0xbf38a0, 0xbf3fe0, 0xbf3b00, 0xbf4270, 0xbf4520, 0xbf42b0, 0xbf4550, the entry serializers 0xbf3930 /
// 0xbf3a10, 0xbf3a50 and the thunk 0xbf3a3a; model 0xbf3050, 0xbf2780 (constructor), 0xbf2d40, 0xbf2990, 0xbf2f10,
// 0xbf3230, 0xbf2f50, 0xbf3260, 0xbf2860 and the thunk 0xbf2852. Stood in too: the sequence and model preparation passes
// 0xc07490 / 0xc06c00. Strings go through slot 15 (in) and 14 (out).
#include "ts_effects_collection.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "rz/cRZString.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSDecalEffect.h"
#include "ts/cTSLightEffect.h"
#include "ts/cTSModelEffect.h"
#include "ts/cTSSequenceEffect.h"
#include "ts/cTSSoundEffect.h"
#include "ts/cTSWaterEffect.h"

#include <cstddef>
#include <new>

namespace {

using namespace fxtest;

enum : uint32_t {
    evGet = 0xd7000000,
    evPut = 0xd8000000,
    evCreateEffect = 0xd9000000,
    evTrap = 0xda000000,
};

const uint8_t *gObj = nullptr; // the description under test (pointers into it are logged as offsets)
size_t gObjSize = 0;
uint32_t Off(const void *p)
{
    const auto *q = static_cast<const uint8_t *>(p);
    if (gObj && q >= gObj && q < gObj + gObjSize) return 0x0f000000u | uint32_t(q - gObj);
    return p ? 0x0c000000u : 0;
}

// ---- streams ----

struct InStream {
    void *const *vt;
    std::vector<uint32_t> words;
    std::vector<uint8_t> fails;
    size_t pos = 0;
};
template <int Slot, int Bytes> bool __fastcall InGet(InStream *s, void *, void *out)
{
    const bool have = s->pos < s->words.size();
    const bool ok = have && !s->fails[s->pos];
    const uint32_t w = have ? s->words[s->pos] : 0;
    if (have) ++s->pos;
    Ev({evGet | uint32_t(Slot), uint32_t(ok), w});
    if (ok) std::memcpy(out, &w, Bytes);
    return ok;
}
void EvText(const char *text)
{
    const size_t n = std::strlen(text);
    Ev({uint32_t(n)});
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text + i, n - i < 4 ? n - i : 4);
        Ev({w});
    }
}
// Slot 15, the string: the next word made into a text (short, or past the 15 characters a string holds in place), put in
// through the cRZString's own slot 3 (FromChar with a length) as a stream does; a failing or missing word writes nothing,
// except in a run's first string read: the read buffer (a static each side keeps: the image's, and ours that the adapters
// share) then always gets a text, so what a later failing read leaves is the run's own.
bool gTextWritten = false;
bool __fastcall InGetText(InStream *s, void *, cRZString *out)
{
    const bool have = s->pos < s->words.size();
    const bool ok = (have && !s->fails[s->pos]) || !gTextWritten;
    gTextWritten = true;
    const uint32_t w = have ? s->words[s->pos] : 0;
    if (have) ++s->pos;
    Ev({evGet | 15u, uint32_t(ok), w});
    if (!ok) return false;
    const std::string text = w % 3 == 0 ? std::string(16 + w % 24, char('a' + w % 26)) : std::to_string(w);
    using FromChar = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<const FromChar *>(out->mVTable)[3](out, text.data(), uint32_t(text.size()));
    return true;
}
struct OutStream {
    void *const *vt;
};
template <int Slot, uint32_t Mask> bool __fastcall OutPut(OutStream *, void *, uint32_t v)
{
    Ev({evPut | uint32_t(Slot), v & Mask});
    return true;
}
bool __fastcall OutPutText(OutStream *, void *, const char *text)
{
    Ev({evPut | 14u});
    EvText(text);
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
        in[4] = reinterpret_cast<void *>(&InGet<4, 1>);
        out[4] = reinterpret_cast<void *>(&OutPut<4, 0xffu>);
        in[5] = reinterpret_cast<void *>(&InGet<5, 1>);
        in[6] = reinterpret_cast<void *>(&InGet<6, 2>);
        in[7] = reinterpret_cast<void *>(&InGet<7, 2>);
        in[9] = reinterpret_cast<void *>(&InGet<9, 4>);
        in[12] = reinterpret_cast<void *>(&InGet<12, 4>);
        out[5] = reinterpret_cast<void *>(&OutPut<5, 0xffu>);
        out[6] = reinterpret_cast<void *>(&OutPut<6, 0xffffu>);
        out[7] = reinterpret_cast<void *>(&OutPut<7, 0xffffu>);
        out[9] = reinterpret_cast<void *>(&OutPut<9, 0xffffffffu>);
        out[12] = reinterpret_cast<void *>(&OutPut<12, 0xffffffffu>);
        in[15] = reinterpret_cast<void *>(&InGetText);
        out[14] = reinterpret_cast<void *>(&OutPutText);
    }
};
const StreamVTables kStreams;

// A tape: the version (1 mostly), then random words (floats now and then), a few failing reads, at times cut short.
// `counted` (the sequence): word 1 is the entry count, small and never failing (the original's count starts as a stack word
// its caller left, so a count read that fails is undefined there).
InStream RandomTape(std::mt19937 &r, bool counted)
{
    InStream s{kStreams.in};
    const uint32_t n = (counted ? 2 : 1) + r() % 20;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t w;
        if (i == 0)
            w = r() % 5 ? 1u : r() % 4;
        else if (i == 1 && counted) {
            s.words.push_back(r() % 5);
            s.fails.push_back(0);
            continue;
        }
        else if (r() % 3 == 0) {
            const float f = float(int(r() % 2000) - 1000) / 8.0f;
            std::memcpy(&w, &f, 4);
        } else
            w = r() % 2 ? r() % 8 : r();
        s.words.push_back(w);
        s.fails.push_back(uint8_t(r() % 12 == 0));
    }
    return s;
}

// A tape laid out field by field (lane IW's descriptions, whose curves and colour counts must not take a random word: a
// curve's word read that fails leaves the stream's address, a huge count when the stream is on the stack). Layout codes:
//   v the version (1 mostly)   w a word   f a float   b a byte   t a string   (each of these fails now and then)
//   B a byte that never fails (the camera's table: a failing read leaves the description address's low byte, which
//     differs between the sides' objects; CompDescription_IWValues covers it on one block)
//   c a curve: its kind and count word (a list of 0-6 values, a wave of 0-6, now and then another kind; never failing),
//     then the floats it reads (a wave at most 4), failing now and then
//   F a flags word that never fails, 0-4 (the screen's: its count shares the temporary)
//   N a colour count 0-4 that may fail (then the flags word is the count), n one that never fails (the light's count
//     starts unset in the original); then three floats per colour, never failing (a failing one would leave a grown
//     vector's unset memory).
// A version other than 1 ends the tape (nothing more is read).
InStream FieldTape(std::mt19937 &r, const char *layout)
{
    InStream s{kStreams.in};
    uint32_t last = 0;
    auto push = [&](uint32_t w, bool mayFail) {
        s.words.push_back(w);
        s.fails.push_back(uint8_t(mayFail && r() % 12 == 0));
    };
    auto randomFloat = [&] {
        const float f = float(int(r() % 2000) - 1000) / 8.0f;
        uint32_t w;
        std::memcpy(&w, &f, 4);
        return w;
    };
    for (const char *p = layout; *p; ++p) {
        switch (*p) {
        case 'v': {
            const uint32_t version = r() % 6 ? 1u : r() % 4;
            push(version, false);
            if (version != 1) return s;
            break;
        }
        case 'w': push(r() % 2 ? r() % 8 : r(), true); break;
        case 'f': push(randomFloat(), true); break;
        case 'b': push(r(), true); break;
        case 'B': push(r(), false); break; // the camera's table: a failing read leaves the description address's low byte
        case 't': push(r() % 40, true); break;
        case 'c': {
            const uint32_t kinds[] = {0u, 0u, 0x81000000u, 0x81000000u, 0x05000000u};
            const uint32_t kind = kinds[r() % 5];
            const uint32_t count = r() % 7;
            push(kind | count, false);
            const uint32_t floats = kind == 0 ? count : kind == 0x81000000u ? (count < 4 ? count : 4) : 0;
            for (uint32_t i = 0; i < floats; ++i) push(randomFloat(), true);
            break;
        }
        case 'F': last = r() % 5; push(last, false); break;
        case 'N':
        case 'n': {
            const uint32_t count = r() % 5;
            push(count, false);
            const bool fails = *p == 'N' && r() % 6 == 0;
            s.fails.back() = uint8_t(fails);
            const uint32_t read = fails ? last : count;
            for (uint32_t i = 0; i < read * 3; ++i) push(randomFloat(), false);
            break;
        }
        }
    }
    return s;
}

// ---- stand-ins ----

// Each pass logs its own RVA, so a type calling another type's pass is seen.
template <uint32_t kRva> void *__cdecl StandCreateEffect(void *d)
{
    Ev({evCreateEffect, Off(d), kRva});
    return reinterpret_cast<void *>(uintptr_t(kRva));
}
int __cdecl StubAtexit(void (*)())
{
    return 0;
}
struct StandIns {
    shape::CodePatch atexit{0x824cbeu, shape::CodeOf(&StubAtexit)};
    shape::StandIn sound{0xc05670u, &cTSSoundEffect::Create, &StandCreateEffect<0xc05670u>};
    shape::StandIn water{0xc09210u, &cTSWaterEffect::Create, &StandCreateEffect<0xc09210u>};
    shape::StandIn sequence{0xc07490u, &cTSSequenceEffect::Create, &StandCreateEffect<0xc07490u>};
    shape::StandIn model{0xc06c00u, &cTSModelEffect::Create, &StandCreateEffect<0xc06c00u>};
    shape::CodePatch camera{0xc08b90u, shape::CodeOf(&StandCreateEffect<0xc08b90u>)};
    shape::CodePatch screen{0xc09ee0u, shape::CodeOf(&StandCreateEffect<0xc09ee0u>)};
    shape::StandIn light{0xc0a140u, &cTSLightEffect::Create, &StandCreateEffect<0xc0a140u>};
    shape::StandIn decal{0xbfd440u, &cTSDecalEffect::Create, &StandCreateEffect<0xbfd440u>};
};

// ---- the types ----

struct TypeInfo {
    const char *name;
    int32_t type;
    size_t size;      // the object
    size_t dSize;     // the description (at +0xc)
    uint32_t padding; // bit i: byte i of the description is padding (left out)
    uint32_t factory, dCtor, getType, createEffect, copy, read, write, dtor;
    cITSComponentDescription *(__cdecl *create)();
    void *aDCtor, *aGetType, *aCreateEffect, *aCopy, *aRead, *aWrite, *aDtor;
    void (*placeDescription)(void *); // ours: the description struct constructed in place
    void (*pushDescription)(Blob &, const void *); // a description with strings or vectors (null: its bytes)
    const char *tape;                              // the tape's layout (FieldTape), null: random words
};

void PushWords(Blob &b, const void *p, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += 4) b.push_back(*reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(p) + i));
}
void PushDescriptionOf(Blob &b, const cTSModelDescription &d)
{
    PushWords(b, &d, 8);
    PushString(b, d.mName);
    PushWords(b, &d.mFloat20, 0x14);
}
void PushDescriptionOf(Blob &b, const cTSSequenceDescription &d)
{
    b.push_back(d.mId);
    b.push_back(uint32_t(d.mEntries.size()));
    b.push_back(uint32_t(d.mEntries.capacity()));
    for (const cTSSequenceEntry &e : d.mEntries) {
        PushWords(b, &e.mPosition, 8);
        PushString(b, e.mName);
    }
    b.push_back(d.mFlags);
}

// Lane IW's descriptions: curves (kind, count, capacity, values), colours (count, capacity, values), strings.
void PushCurve(Blob &b, const cTSEffectCurve &c)
{
    b.push_back(c.mKind);
    b.push_back(uint32_t(c.mValues.size()));
    b.push_back(uint32_t(c.mValues.capacity()));
    PushWords(b, c.mValues.data(), c.mValues.size() * 4);
}
void PushColors(Blob &b, const std::vector<fxdesc::Color> &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    PushWords(b, v.data(), v.size() * sizeof(fxdesc::Color));
}
void PushDescriptionOf(Blob &b, const cTSCameraDescription &d)
{
    PushWords(b, &d, 0x10);
    PushCurve(b, d.mAmplitude);
    PushCurve(b, d.mFrequency);
    PushWords(b, &d.mShakeAspect, 8);
    for (const cTSEffectCurve *c : {&d.mHeading, &d.mPitch, &d.mRoll, &d.mOrbit, &d.mFov, &d.mNearClip, &d.mFarClip}) PushCurve(b, *c);
    b.push_back(d.mZoom);
    b.push_back(d.mRotate);
    PushString(b, d.mSelect);
    PushWords(b, &d.mAttachRadius, 4);
}
void PushDescriptionOf(Blob &b, const cTSScreenDescription &d)
{
    PushWords(b, &d, 0xc);
    PushColors(b, d.mColors);
    PushCurve(b, d.mStrength);
    PushWords(b, &d.mLength, 8);
    PushString(b, d.mTexture);
}
void PushDescriptionOf(Blob &b, const cTSLightDescription &d)
{
    PushWords(b, &d, 0xc);
    PushColors(b, d.mColors);
    PushCurve(b, d.mCurve18);
    PushCurve(b, d.mCurve28);
    PushWords(b, &d.mFloat38, 4);
}
// The decal's bytes +0x22 / +0x23 are padding (neither constructor writes them), so not compared.
void PushDescriptionOf(Blob &b, const cTSDecalDescription &d)
{
    PushWords(b, &d, 8);
    PushString(b, d.mTexture);
    b.push_back(d.mDraw);
    b.push_back(d.mLifeMode);
    PushWords(b, &d.mLife, 4);
    PushCurve(b, d.mRotate);
    PushCurve(b, d.mSize);
    PushCurve(b, d.mAlpha);
    PushColors(b, d.mColors);
    PushCurve(b, d.mAspect);
    PushWords(b, &d.mAlphaVary, 0x18);
}

template <class D> void (*DescriptionPusher())(Blob &, const void *)
{
    if constexpr (std::is_trivially_copyable_v<D>)
        return nullptr;
    else
        return [](Blob &b, const void *d) { PushDescriptionOf(b, *static_cast<const D *>(d)); };
}

template <class T, class AdaptersOf = T> TypeInfo Info(const char *name, uint32_t factory, uint32_t dCtor, uint32_t getType,
                                                       uint32_t createEffect, uint32_t copy, uint32_t read, uint32_t write,
                                                       uint32_t dtor, uint32_t padding = 0, const char *tape = nullptr)
{
    using A = cTSCompDescriptionAdapters<T>;
    using AC = cTSCompDescriptionAdapters<AdaptersOf>; // the shared bodies' hook (brush's for water)
    return {name,
            T::kComponentType,
            sizeof(T),
            sizeof(typename T::Description),
            padding,
            factory,
            dCtor,
            getType,
            createEffect,
            copy,
            read,
            write,
            dtor,
            &T::Create,
            dCtor ? reinterpret_cast<void *>(&A::ConstructDescription) : nullptr,
            getType ? reinterpret_cast<void *>(&A::GetType) : nullptr,
            createEffect ? reinterpret_cast<void *>(&A::CreateEffect) : nullptr,
            reinterpret_cast<void *>(&AC::CopyTo),
            reinterpret_cast<void *>(&A::Read),
            reinterpret_cast<void *>(&A::Write),
            reinterpret_cast<void *>(&AC::DeletingDestructor),
            [](void *at) { ::new (at) typename T::Description; },
            DescriptionPusher<typename T::Description>(),
            tape};
}

const TypeInfo kTypes[] = {
    Info<cTSBrushComponentDescription>("brush", 0xbf8af0, 0xbf8600, 0, 0, 0xbf6820, 0xbf89a0, 0xbf8b70, 0xbf6770, 0xe000),
    Info<cTSScrubberComponentDescription>("scrubber", 0xbfa140, 0xbf9f80, 0, 0, 0xbfa020, 0xbfa060, 0xbfa1c0, 0xbf9ff0),
    Info<cTSSoundComponentDescription>("sound", 0xbf25d0, 0xbf2110, 0, 0xbf2530, 0xbf2170, 0xbf2540, 0xbf26f0, 0xbf2140),
    Info<cTSGameComponentDescription>("game", 0xbfa3a0, 0, 0xbfa420, 0, 0xbfa2e0, 0xbfa310, 0xbfa430, 0xbfa2b0),
    Info<cTSWaterComponentDescription, cTSBrushComponentDescription>("water", 0xbf6f90, 0, 0, 0xbf6c30, 0xbf6820, 0xbf6eb0,
                                                                      0xbf7170, 0xbf6770),
    Info<cTSSequenceComponentDescription>("sequence", 0xbf4370, 0xbf38a0, 0, 0xbf3fe0, 0xbf3b00, 0xbf4270, 0xbf4520, 0xbf3a50),
    Info<cTSModelComponentDescription>("model", 0xbf3050, 0, 0, 0xbf2d40, 0xbf2990, 0xbf2f10, 0xbf3230, 0xbf2860),
    // lane IW (the +4 face's deleting-destructor thunks: camera 0xbf4edb, screen 0xbf7453, light 0xbf8e11)
    Info<cTSCameraComponentDescription>("camera", 0xbf6530, 0xbf4610, 0, 0xbf5700, 0xbf4fb0, 0xbf6110, 0xbf65c0, 0xbf4ef0, 0,
                                        "vwffccfBcccccccbbft"),
    Info<cTSScreenComponentDescription>("screen", 0xbf8360, 0xbf7280, 0, 0xbf7bf0, 0xbf7520, 0xbf8150, 0xbf8520, 0xbf7460, 0,
                                        "vbFNcfft"),
    Info<cTSLightComponentDescription>("light", 0xbf9370, 0, 0xbf9560, 0xbf9140, 0xbf8ee0, 0xbf9240, 0xbf9570, 0xbf8e20, 0,
                                       "vwnccf"),
    // decal: slot 0 is the folded 0x18c4d0 (not hooked); +4 face thunk 0xbeb88e
    Info<cTSDecalComponentDescription>("decal", 0xbecb10, 0xbeb2c0, 0, 0xbec240, 0xbeb960, 0xbec8a0, 0xbecce0, 0xbeb8a0, 0,
                                       "vwtbbfcccncffffff"),
};

uint8_t *DescOf(void *obj)
{
    return static_cast<uint8_t *>(obj) + 0xc;
}
void *Face4(void *obj)
{
    return static_cast<uint8_t *>(obj) + 4;
}

void PushDesc(Blob &b, const TypeInfo &t, void *obj)
{
    const uint8_t *d = DescOf(obj);
    if (t.pushDescription) return t.pushDescription(b, d);
    for (size_t i = 0; i < t.dSize; ++i)
        if (!(t.padding >> i & 1)) b.push_back(d[i]);
}
void PushObject(Blob &b, const TypeInfo &t, void *obj)
{
    PushDesc(b, t, obj);
    b.push_back(*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(obj) + 8)); // the count
}

// The class's own slot: the original at `rva` (mode 0), our vtable (1), the adapter (2); a folded slot (rva 0) through the
// object's vtable in every mode.
template <class R, class... Args> R Slot(difftest::Context &ctx, int mode, void *obj, uint32_t rva, void *adapter, int slot, Args... args)
{
    if (rva && mode == 0) return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(obj, args...);
    if (rva && mode == 2) return reinterpret_cast<R(__fastcall *)(void *, void *, Args...)>(adapter)(obj, nullptr, args...);
    return CallSlot<R>(obj, slot, args...);
}

void *Create(difftest::Context &ctx, int mode, const TypeInfo &t)
{
    return mode == 1 ? static_cast<void *>(t.create()) : ctx.Original<void *(__cdecl *)()>(t.factory)();
}

void Operate(difftest::Context &ctx, int mode, const TypeInfo &t, void *obj, void *target, Blob &b, uint32_t op, std::mt19937 &r)
{
    switch (op) {
    case 0: // GetType, GetDescription, GetSize
        b.push_back(uint32_t(Slot<int32_t>(ctx, mode, obj, t.getType, t.aGetType, 0)));
        b.push_back(Off(CallSlot<void *>(obj, 3)));
        b.push_back(CallSlot<uint32_t>(obj, 6));
        break;
    case 1: // CreateEffect (the effect it makes)
        b.push_back(uint32_t(uintptr_t(Slot<void *>(ctx, mode, obj, t.createEffect, t.aCreateEffect, 1))));
        break;
    case 2: // CopyTo the target (of this type or another)
        Slot<void, void *>(ctx, mode, obj, t.copy, t.aCopy, 2, target);
        break;
    case 3:
    case 4: { // Read a random tape
        InStream s = t.tape ? FieldTape(r, t.tape) : RandomTape(r, t.type == 6);
        Slot<void, void *>(ctx, mode, obj, t.read, t.aRead, 4, &s);
        b.push_back(uint32_t(s.pos));
        break;
    }
    case 5: { // Write
        OutStream s{kStreams.out};
        Slot<void, void *>(ctx, mode, obj, t.write, t.aWrite, 5, &s);
        break;
    }
    case 6: { // the counts through both faces (kept above 0)
        const bool face4 = r() % 2;
        b.push_back(face4 ? CallSlot<uint32_t>(Face4(obj), 2) : CallSlot<uint32_t>(obj, 7));
        b.push_back(face4 ? CallSlot<uint32_t>(Face4(obj), 1) : CallSlot<uint32_t>(obj, 8));
        b.push_back(CallSlot<uint32_t>(Face4(obj), 4));
        b.push_back(CallSlot<uint32_t>(Face4(obj), 2));
        b.push_back(CallSlot<uint32_t>(Face4(obj), 3));
        break;
    }
    default: // the description's constructor over it
        if (!t.dCtor) break;
        if (mode == 0)
            b.push_back(Off(ctx.Original<void *(__thiscall *)(void *)>(t.dCtor)(DescOf(obj))));
        else if (mode == 1)
            t.placeDescription(DescOf(obj)), b.push_back(Off(DescOf(obj)));
        else
            b.push_back(Off(reinterpret_cast<void *(__fastcall *)(void *, void *)>(t.aDCtor)(DescOf(obj), nullptr)));
        break;
    }
}

} // namespace

// Per type: a description and a copy target (of its type, or the game type's), random operations over every slot, then the
// deletion (the last Release, or the deleting destructor with flags 1 or 0).
DIFF_TEST(CompDescription_Operations, 0xbfa060)
{
    StandIns stands;
    for (const TypeInfo &t : kTypes) {
        for (uint32_t seed = 0; seed < 40; ++seed) {
            Blob blobs[3];
            for (int mode = 0; mode < 3; ++mode) {
                Blob &b = blobs[mode];
                gEv.clear();
                gTextWritten = false;
                std::mt19937 r(seed * 31u + uint32_t(t.type));
                void *obj = Create(ctx, mode, t);
                const TypeInfo &tt = r() % 4 ? t : kTypes[t.type == 9 ? 0 : 3];
                void *target = Create(ctx, mode, tt);
                gObj = static_cast<uint8_t *>(obj);
                gObjSize = t.size;
                CallSlot<uint32_t>(obj, 7);
                CallSlot<uint32_t>(target, 7);
                PushObject(b, t, obj);
                PushDesc(b, tt, target);
                for (int step = 0; step < 25; ++step) {
                    const uint32_t op = r() % 8;
                    b.push_back(0x0b000000u | op);
                    Operate(ctx, mode, t, obj, target, b, op, r);
                    b.insert(b.end(), gEv.begin(), gEv.end());
                    gEv.clear();
                    PushObject(b, t, obj);
                    PushDesc(b, tt, target);
                }
                const uint32_t how = seed % 3;
                difftest::FreeRecorder rec;
                if (how == 0) {
                    const bool face4 = r() % 2; // the last Release: slot 8 of the +0 face or slot 1 of the +4 face
                    b.push_back(face4 ? CallSlot<uint32_t>(Face4(obj), 1) : CallSlot<uint32_t>(obj, 8));
                } else {
                    const uint32_t flags = how == 1 ? 1u : 0u;
                    void *got = mode == 0   ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(t.dtor)(obj, flags)
                                : mode == 2 ? reinterpret_cast<void *(__fastcall *)(void *, void *, uint32_t)>(t.aDtor)(obj, nullptr, flags)
                                            : CallSlot<void *>(Face4(obj), 0, flags);
                    b.push_back(uint32_t(got == obj));
                }
                rec.Stop();
                b.push_back(uint32_t(rec.Freed(obj)));
                size_t sizes[16];
                const size_t n = rec.SortedSizes(sizes, 16);
                b.push_back(uint32_t(n));
                for (size_t i = 0; i < n && i < 16; ++i) b.push_back(uint32_t(sizes[i]));
                if (how == 2) ::operator delete(obj);
                b.push_back(CallSlot<uint32_t>(target, 8));
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                gObj = nullptr;
            }
            char what[64];
            std::snprintf(what, sizeof what, "%s (ours)", t.name);
            Same(ctx, blobs[0], blobs[1], what, seed);
            std::snprintf(what, sizeof what, "%s (adapters)", t.name);
            Same(ctx, blobs[0], blobs[2], what, seed);
        }
    }
}

// The water description's own constructor 0xbf66f0 (the parser's cWaterEffectCommand::EndBlock builds in place) on a
// garbage block: everything but the two vtable words compared, then the deleting destructor 0xbf6770 with flags 0.
DIFF_TEST(CompDescription_WaterConstruct, 0xbf66f0)
{
    using W = cTSWaterComponentDescription;
    using A = cTSCompDescriptionAdapters<W>;
    using AB = cTSCompDescriptionAdapters<cTSBrushComponentDescription>;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed);
            alignas(8) uint8_t block[sizeof(W)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *got = mode == 0   ? ctx.Original<void *(__thiscall *)(void *)>(0xbf66f0)(block)
                        : mode == 1 ? static_cast<void *>(::new (block) W)
                                    : static_cast<void *>(A::Construct(block, nullptr));
            b.push_back(uint32_t(got == block));
            for (size_t i = 8; i < sizeof(W); ++i) b.push_back(block[i]);
            b.push_back(uint32_t(CallSlot<int32_t>(block, 0)));
            difftest::FreeRecorder rec;
            if (mode == 0)
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbf6770)(block, 0);
            else if (mode == 1)
                CallSlot<void *>(Face4(block), 0, 0u);
            else
                AB::DeletingDestructor(reinterpret_cast<cTSBrushComponentDescription *>(block), nullptr, 0);
            rec.Stop();
            size_t sizes[8];
            b.push_back(uint32_t(rec.SortedSizes(sizes, 8)));
        }
        Same(ctx, blobs[0], blobs[1], "water constructor (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "water constructor (adapters)", seed);
    }
}

// The model description's whole-object constructor 0xbf2780 (the factory's, and the parser's cGroupModelCommand /
// cModelEffectCommand build in place) on a garbage block: everything but the two vtable words compared (the name's in-place
// buffer included), a name long enough to leave its buffer given, then the deleting destructor 0xbf2860 with flags 0: the
// frees, and the name's words after it.
DIFF_TEST(CompDescription_ModelConstruct, 0xbf2780)
{
    using M = cTSModelComponentDescription;
    using A = cTSCompDescriptionAdapters<M>;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed);
            alignas(8) uint8_t block[sizeof(M)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *got = mode == 0   ? ctx.Original<void *(__thiscall *)(void *)>(0xbf2780)(block)
                        : mode == 1 ? static_cast<void *>(::new (block) M)
                                    : static_cast<void *>(A::Construct(block, nullptr));
            b.push_back(uint32_t(got == block));
            for (size_t i = 8; i < sizeof(M); ++i) b.push_back(block[i]);
            b.push_back(uint32_t(CallSlot<int32_t>(block, 0)));
            auto *name = reinterpret_cast<std::string *>(block + 0x14);
            if (seed % 2) name->assign(16 + seed, 'n');
            difftest::FreeRecorder rec;
            if (mode == 0)
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbf2860)(block, 0);
            else if (mode == 1)
                CallSlot<void *>(Face4(block), 0, 0u);
            else
                A::DeletingDestructor(reinterpret_cast<M *>(block), nullptr, 0);
            rec.Stop();
            size_t sizes[8];
            const size_t n = rec.SortedSizes(sizes, 8);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 8; ++i) b.push_back(uint32_t(sizes[i]));
            PushWords(b, block + 0x24, 8);
            b.push_back(block[0x14]);
        }
        Same(ctx, blobs[0], blobs[1], "model constructor (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "model constructor (adapters)", seed);
    }
}

// ---- lane IW: the camera, screen and light descriptions' own functions ----

namespace {

// One description type's functions: the originals (mode 0) or the hooks' entries (mode 1).
template <class T> struct IWDescription {
    using D = typename T::Description;
    using A = cTSCompDescriptionAdapters<T>;
    uint32_t construct, destroy, move, copy, read, write; // RVAs (construct 0: the light's whole-object constructor)
    const char *layout;

    void Construct(difftest::Context &ctx, int mode, uint8_t *object) const
    {
        if (!construct) {
            void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0xbf8c20)(object) : A::Construct(object, nullptr);
            Ev({uint32_t(got == object)});
            return;
        }
        void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(construct)(object + 0xc)
                              : A::ConstructDescription(object + 0xc, nullptr);
        Ev({uint32_t(got == object + 0xc)});
    }
    void Read(difftest::Context &ctx, int mode, D *d, InStream &s) const
    {
        if (mode == 0)
            ctx.Original<void(__cdecl *)(void *, uint32_t, D *)>(read)(&s, 1, d);
        else
            D::Read(&s, 1, d);
    }
    void Write(difftest::Context &ctx, int mode, const D *d) const
    {
        OutStream s{kStreams.out};
        if (mode == 0)
            ctx.Original<void(__cdecl *)(void *, const D *)>(write)(&s, d);
        else
            D::Write(&s, d);
    }
    void Copy(difftest::Context &ctx, int mode, D *to, const D *from) const
    {
        void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, const void *)>(copy)(to, from) : A::CopyDescription(to, nullptr, from);
        Ev({uint32_t(got == to)});
    }
    void Move(difftest::Context &ctx, int mode, D *to, D *from) const
    {
        void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, void *)>(move)(to, from) : A::MoveDescription(to, nullptr, from);
        Ev({uint32_t(got == to)});
    }
    void Destroy(difftest::Context &ctx, int mode, D *d) const
    {
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(destroy)(d);
        else
            A::DestroyDescription(d, nullptr);
    }

    // Two descriptions built on garbage blocks (the light's whole objects: their count word compared), filled from tapes,
    // then copies (each way and onto itself), the move (the camera's), writes, and both destroyed: their frees compared.
    void Run(difftest::Context &ctx, const char *name) const
    {
        for (uint32_t seed = 0; seed < 60; ++seed) {
            Blob blobs[2];
            for (int mode = 0; mode < 2; ++mode) {
                Blob &b = blobs[mode];
                gEv.clear();
                gTextWritten = false;
                std::mt19937 r(seed * 7u + T::kComponentType);
                alignas(8) uint8_t blockA[sizeof(T)], blockB[sizeof(T)];
                for (uint8_t &x : blockA) x = uint8_t(r());
                for (uint8_t &x : blockB) x = uint8_t(r());
                Construct(ctx, mode, blockA);
                Construct(ctx, mode, blockB);
                if (!construct) {
                    b.push_back(*reinterpret_cast<uint32_t *>(blockA + 8));
                    b.push_back(*reinterpret_cast<uint32_t *>(blockB + 8));
                }
                auto *a = reinterpret_cast<D *>(blockA + 0xc);
                auto *bb = reinterpret_cast<D *>(blockB + 0xc);
                for (int step = 0; step < 8; ++step) {
                    const uint32_t op = r() % 6;
                    b.push_back(0x0b000000u | op);
                    switch (op) {
                    case 0:
                    case 1: {
                        InStream s = FieldTape(r, layout);
                        Read(ctx, mode, op == 0 ? a : bb, s);
                        b.push_back(uint32_t(s.pos));
                        break;
                    }
                    case 2: Copy(ctx, mode, a, bb); break;
                    case 3: Copy(ctx, mode, bb, bb); break;
                    case 4:
                        if (move) Move(ctx, mode, r() % 2 ? a : bb, r() % 2 ? bb : a);
                        break;
                    default: Write(ctx, mode, r() % 2 ? a : bb); break;
                    }
                    b.insert(b.end(), gEv.begin(), gEv.end());
                    gEv.clear();
                    PushDescriptionOf(b, *a);
                    PushDescriptionOf(b, *bb);
                }
                difftest::FreeRecorder rec;
                Destroy(ctx, mode, a);
                Destroy(ctx, mode, bb);
                rec.Stop();
                size_t sizes[64];
                const size_t n = rec.SortedSizes(sizes, 64);
                b.push_back(uint32_t(n));
                for (size_t i = 0; i < n && i < 64; ++i) b.push_back(uint32_t(sizes[i]));
            }
            Same(ctx, blobs[0], blobs[1], name, seed);
        }
    }
};

} // namespace

// The camera (constructor 0xbf4610, destructor 0xbf4720, move 0xbf4a50, copy 0xbf4d20, serializers 0xbf6150 / 0xbf65f0),
// screen (0xbf7280, 0xbf7320, copy 0xbf73c0, serializers 0xbf8190 / 0xbf8550) and light (whole-object constructor 0xbf8c20,
// destructor 0xbf8cd0, copy 0xbf8d80, serializers 0xbf9280 / 0xbf95a0) descriptions' own functions against the hooks'
// entries. The camera's table byte may fail here: both sides' descriptions sit on the same stack block, so the byte a
// failing read leaves (the description address's low byte) is the same.
DIFF_TEST(CompDescription_IWValues, 0xbf4d20)
{
    StandIns stands;
    IWDescription<cTSCameraComponentDescription>{0xbf4610, 0xbf4720, 0xbf4a50, 0xbf4d20, 0xbf6150, 0xbf65f0,
                                                  "wffccfbcccccccbbft"}
        .Run(ctx, "camera values");
    IWDescription<cTSScreenComponentDescription>{0xbf7280, 0xbf7320, 0, 0xbf73c0, 0xbf8190, 0xbf8550, "bFNcfft"}.Run(ctx, "screen values");
    IWDescription<cTSLightComponentDescription>{0, 0xbf8cd0, 0, 0xbf8d80, 0xbf9280, 0xbf95a0, "wnccf"}.Run(ctx, "light values");
}

// The decal description's (constructor 0xbeb2c0, destructor 0xbeb410, move 0xbeb5a0, copy 0xbeb760, serializers
// 0xbec8e0 / 0xbecd10). The colour count never fails (the original's temporary is a reused stack slot).
DIFF_TEST(CompDescription_IWDecal, 0xbeb760)
{
    StandIns stands;
    IWDescription<cTSDecalComponentDescription>{0xbeb2c0, 0xbeb410, 0xbeb5a0, 0xbeb760, 0xbec8e0, 0xbecd10,
                                                "wtbbfcccncffffff"}
        .Run(ctx, "decal values");
}

// GetSize (slot 6: camera 0xbf5710, screen 0xbf7c00, light 0xbf9150, decal 0xbec250) and the light's GetType (0xbf9560)
// against the adapters; the camera's, screen's and decal's GetType are folded bodies (shared_vf).
DIFF_TEST(CompDescription_IWSizes, 0xbf5710)
{
    alignas(8) uint8_t block[0x100] = {};
    const uint32_t rvas[] = {0xbf5710, 0xbf7c00, 0xbf9150, 0xbf9560, 0xbec250};
    const uint32_t ours[] = {
        cTSCompDescriptionAdapters<cTSCameraComponentDescription>::GetSize(reinterpret_cast<cTSCameraComponentDescription *>(block), nullptr),
        cTSCompDescriptionAdapters<cTSScreenComponentDescription>::GetSize(reinterpret_cast<cTSScreenComponentDescription *>(block), nullptr),
        cTSCompDescriptionAdapters<cTSLightComponentDescription>::GetSize(reinterpret_cast<cTSLightComponentDescription *>(block), nullptr),
        uint32_t(cTSCompDescriptionAdapters<cTSLightComponentDescription>::GetType(reinterpret_cast<cTSLightComponentDescription *>(block),
                                                                                   nullptr)),
        cTSCompDescriptionAdapters<cTSDecalComponentDescription>::GetSize(reinterpret_cast<cTSDecalComponentDescription *>(block), nullptr)};
    for (int i = 0; i < 5; ++i) {
        Blob a{ctx.Original<uint32_t(__thiscall *)(void *)>(rvas[i])(block)}, b{ours[i]};
        Same(ctx, a, b, "sizes", uint32_t(i));
    }
}
