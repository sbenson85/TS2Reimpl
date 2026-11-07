#pragma once
#include "rz/RZRefCountBase.h"
#include "ts/cTSEffectCurve.h"
#include "ts/cTSEffectsCollection.h" // cITSComponentDescription

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

class cRZString;
// The function-local cRZString the effects code's string reads go through (the original's 0x10fc89c, shared by the resource,
// the trigger reader and the description serializers; a read that fails leaves the previous text in it).
cRZString &EffectsReadBuffer();

// The effect component descriptions (lane IT): cTSCompDescriptionBaseT<D, N> (RTTI: cITSComponentDescription +0,
// cRZRefCount +4), one per component type N + 1 (1..13); the description struct D sits at +0xc (+0x10 for the particle
// kinds, whose D is 8-aligned). The effects parser and the resource create them through one cdecl factory per type (no
// arguments: new, the base parts, the description's defaults); a few parser commands construct them in place.
//
// Standalone shape (docs/standalone-shape.md): a real class template; the compiler emits each instance's vtables. Slots
// 3 (GetDescription), 6 (GetSize), 7 / 8 (AddRef / Release, +4 face 1-4) and for most types 0 and 1 are bodies the linker
// folded across unrelated classes (shared_vf_*): ours in our vtables, never hooked. The hooks: each factory (so the effects
// code's descriptions carry our vtables), the class-specific slots (adapters, for the descriptions original parser code
// still builds with the game's vtables) and the description serializers (cdecl: stream, version, D / stream, D). RVAs
// 1.18.0.187; all /O2.
//
// Streams are cIGZIStream / cIGZOStream used through their vtables (5 Uint8, 6 Sint16, 7 Uint16, 9 Uint32, 12 Float32).

namespace fxdesc {

void GetSint8(void *stream, uint8_t *value); // slot 4
void PutSint8(void *stream, uint8_t value);
// The flags word: read over a temporary that starts as the stream pointer (the originals read into their stream argument's
// stack slot), then masked.
uint32_t GetMasked(void *stream, uint32_t mask);
void GetUint8(void *stream, uint8_t *value);
void GetSint16(void *stream, int16_t *value);
void GetUint16(void *stream, uint16_t *value);
void GetUint32(void *stream, uint32_t *value);
void GetFloat(void *stream, float *value);
void PutUint8(void *stream, uint8_t value);
void PutSint16(void *stream, int16_t value);
void PutUint16(void *stream, uint16_t value);
void PutUint32(void *stream, uint32_t value);
void PutFloat(void *stream, float value);

struct Vector2 {
    float x, y;
};
// The game's generic vector stream operators (0x1a81c0 / 0x1a8260): the two floats in order.
void GetVector2(void *stream, Vector2 *v);
void PutVector2(void *stream, const Vector2 &v);
struct Vector3 {
    float x, y, z;
};
// 0x1a81f0 / 0x1a82a0: the three floats in order.
void GetVector3(void *stream, Vector3 *v);
void PutVector3(void *stream, const Vector3 &v);
// A string: read (slot 15) into the effects code's shared read buffer (src/ts/cTSEffectsResource.cpp), then copied over;
// written (slot 14) as its C string.
void GetText(void *stream, std::string *text);
void PutText(void *stream, const std::string &text);

// A colour of the screen and light descriptions (three floats). Its constructor leaves it unset, as the original's: a
// vector of them grown by resize holds whatever its memory held.
struct Color {
    float r, g, b;
    Color() {}
    Color(float red, float green, float blue) : r(red), g(green), b(blue) {}
};
static_assert(sizeof(Color) == 12);
// The game's vector stream operators (0x1a81f0 / 0x1a82a0) on a colour.
void GetColor(void *stream, Color *c);
void PutColor(void *stream, const Color &c);

} // namespace fxdesc

// ---- the description structs ----

// type 4. Constructor 0xbf8600 (also inline in the parser's cGroupBrushCommand), serializers 0xbf89e0 / 0xbf8ba0.
struct cTSBrushDescription {
    uint32_t mFlags = 0;                    // +0x00
    float mFloat4 = 1.0f;                   // +0x04
    float mFloat8 = 0.0f;                   // +0x08
    uint8_t mByteC = 0;                     // +0x0c
    uint32_t mValue10 = 0xffffffffu;        // +0x10
    fxdesc::Vector2 mVector14{1.0f, 1.0f};  // +0x14
    fxdesc::Vector2 mVector1C{1.0f, 1.0f};  // +0x1c
    float mFloat24 = 100.0f;                // +0x24

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect() { return nullptr; } // no effect of its own
    static void __cdecl Read(void *stream, uint32_t version, cTSBrushDescription *d); // 0xbf89e0
    static void __cdecl Write(void *stream, const cTSBrushDescription *d);          // 0xbf8ba0
};
static_assert(sizeof(cTSBrushDescription) == 0x28 && offsetof(cTSBrushDescription, mValue10) == 0x10);

// type 5. Constructor 0xbf9f80 (inline in the factory), serializers 0xbfa0a0 / 0xbfa1f0.
struct cTSScrubberDescription {
    uint32_t mFlags = 0;                   // +0x00 (4 bits read)
    uint32_t mValue4 = 0;                  // +0x04
    uint32_t mValue8 = 0;                  // +0x08
    uint32_t mValueC = 0;                  // +0x0c
    float mFloat10 = 0.0f;                 // +0x10
    float mFloat14 = 0.0f;                 // +0x14
    uint32_t mValue18 = 0;                 // +0x18
    uint32_t mValue1C = 0;                 // +0x1c
    float mFloat20 = 16.0f;                // +0x20
    fxdesc::Vector2 mVector24{0.0f, 0.0f}; // +0x24
    float mFloat2C = 0.0f;                 // +0x2c
    float mFloat30 = 0.0f;                 // +0x30

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect() { return nullptr; } // no effect of its own
    static void __cdecl Read(void *stream, uint32_t version, cTSScrubberDescription *d); // 0xbfa0a0
    static void __cdecl Write(void *stream, const cTSScrubberDescription *d);          // 0xbfa1f0
};
static_assert(sizeof(cTSScrubberDescription) == 0x34);

// type 7. Constructor 0xbf2110 (also inline in the parser's cGroupSoundCommand), serializers 0xbf2580 / 0xbf2720; the
// preparation pass is the sound code's 0xc05670 (not ours yet).
struct cTSSoundDescription {
    uint32_t mFlags = 0;     // +0x00 (2 bits read)
    uint32_t mSound = 0;     // +0x04
    float mFloat8 = 0.5f;    // +0x08
    float mFloatC = 0.0f;    // +0x0c
    float mFloat10 = 0.0f;   // +0x10

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSSoundDescription *d); // 0xbf2580
    static void __cdecl Write(void *stream, const cTSSoundDescription *d);          // 0xbf2720
};
static_assert(sizeof(cTSSoundDescription) == 0x14);

// type 9. Constructor inline in the factory (the folded 4-word clear 0x57c720), serializers 0xbfa350 / 0xbfa460.
struct cTSGameDescription {
    uint32_t mFlags = 0;   // +0x00 (3 bits read)
    uint32_t mValue4 = 0;  // +0x04
    uint32_t mValue8 = 0;  // +0x08
    float mFloatC = 0.0f;  // +0x0c

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect() { return nullptr; } // no effect of its own
    static void __cdecl Read(void *stream, uint32_t version, cTSGameDescription *d); // 0xbfa350
    static void __cdecl Write(void *stream, const cTSGameDescription *d);          // 0xbfa460
};
static_assert(sizeof(cTSGameDescription) == 0x10);

// type 12. The whole object's constructor 0xbf66f0 (the factory's, and the parser's cWaterEffectCommand::EndBlock),
// serializers 0xbf6ef0 / 0xbf71a0; the preparation pass is the water code's 0xc09210 (not ours yet). Bit 0 of the flags
// picks the layout of +8..+0x27: two bytes and a 16-bit number then five floats at +0x14, or three floats at +8.
struct cTSWaterDescription {
    uint32_t mId = 0x0c92aee0u;  // +0x00 (not serialized)
    uint32_t mFlags = 0;         // +0x04 (1 bit read)
    union {
        struct {
            uint8_t mByte8;
            uint8_t mByte9;
            int16_t mShortA;
        };
        float mFloat8;           // +0x08
    };
    float mFloatC = 1.0f;        // +0x0c
    float mFloat10 = 0.0f;       // +0x10
    float mFloat14 = 30.0f;      // +0x14
    float mFloat18 = 0.01f;      // +0x18
    float mFloat1C = 100.0f;     // +0x1c
    float mFloat20 = 0.01f;      // +0x20
    float mFloat24 = 0.25f;      // +0x24

    cTSWaterDescription() : mFloat8(1.0f) {}
    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSWaterDescription *d); // 0xbf6ef0
    static void __cdecl Write(void *stream, const cTSWaterDescription *d);          // 0xbf71a0
};
static_assert(sizeof(cTSWaterDescription) == 0x28 && offsetof(cTSWaterDescription, mFloat14) == 0x14);

// type 6. Constructor 0xbf38a0 (the factory's and the parser's cSequenceEffectCommand's), serializers 0xbf42b0 / 0xbf4550
// with the entry serializers 0xbf3930 / 0xbf3a10; the preparation pass is 0xc07490 (not ours yet).
struct cTSSequenceEntry {
    fxdesc::Vector2 mPosition{0.0f, 0.0f}; // +0x00
    std::string mName;                     // +0x08

    static void __cdecl Read(void *stream, cTSSequenceEntry *e);        // 0xbf3930
    static void __cdecl Write(void *stream, const cTSSequenceEntry *e); // 0xbf3a10
};
static_assert(sizeof(cTSSequenceEntry) == 0x20 && offsetof(cTSSequenceEntry, mName) == 8);

struct cTSSequenceDescription {
    uint32_t mId = 0xebb8e525u;              // +0x00 (not serialized)
    std::vector<cTSSequenceEntry> mEntries;  // +0x04
    uint32_t mFlags = 0;                     // +0x10 (4 bits read)

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSSequenceDescription *d); // 0xbf42b0
    static void __cdecl Write(void *stream, const cTSSequenceDescription *d);          // 0xbf4550
};
static_assert(sizeof(cTSSequenceDescription) == 0x14 && offsetof(cTSSequenceDescription, mFlags) == 0x10);

// type 10. The whole object's constructor 0xbf2780 (the factory's and the parser's cGroupModelCommand / cModelEffectCommand),
// serializers 0xbf2f50 / 0xbf3260; the preparation pass is 0xc06c00 (not ours yet).
struct cTSModelDescription {
    uint32_t mId = 0xebb8e524u;                    // +0x00 (not serialized)
    uint32_t mValue4 = 0;                          // +0x04 (not serialized)
    std::string mName;                             // +0x08
    float mFloat20 = 1.0f;                         // +0x20
    fxdesc::Vector3 mVector24{1.0f, 1.0f, 1.0f};   // +0x24
    float mFloat30 = 1.0f;                         // +0x30

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSModelDescription *d); // 0xbf2f50
    static void __cdecl Write(void *stream, const cTSModelDescription *d);          // 0xbf3260
};
static_assert(sizeof(cTSModelDescription) == 0x34 && offsetof(cTSModelDescription, mFloat20) == 0x20);

// type 8 (lane IW). Constructor 0xbf4610, destructor 0xbf4720, move assignment 0xbf4a50 and copy assignment 0xbf4d20 (all
// the compiler's, member by member; the parser's camera and shake blocks call them), serializers 0xbf6150 / 0xbf65f0; the
// preparation pass is the camera code's 0xc08b90 (not ours). What the parser's commands set: src/gz/EffectCommands.h.
struct cTSCameraDescription {
    uint32_t mId = 0xcbb908f4u;  // +0x00 (not serialized)
    uint32_t mFlags = 0;         // +0x04 (18 bits read)
    float mLife = 0.0f;          // +0x08 camera life, shake length
    float mFade = 1.0f;          // +0x0c shake -fade
    cTSEffectCurve mAmplitude;   // +0x10 shake amplitude
    cTSEffectCurve mFrequency;   // +0x20 shake frequency
    float mShakeAspect = 1.0f;   // +0x30
    uint32_t mTable = 0;         // +0x34 shake table (serialized as a byte)
    cTSEffectCurve mHeading;     // +0x38
    cTSEffectCurve mPitch;       // +0x48
    cTSEffectCurve mRoll;        // +0x58
    cTSEffectCurve mOrbit;       // +0x68
    cTSEffectCurve mFov;         // +0x78
    cTSEffectCurve mNearClip;    // +0x88
    cTSEffectCurve mFarClip;     // +0x98
    uint8_t mZoom = 0;           // +0xa8
    uint8_t mRotate = 0;         // +0xa9
    std::string mSelect;         // +0xac
    float mAttachRadius = 0.0f;  // +0xc4

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSCameraDescription *d); // 0xbf6150
    static void __cdecl Write(void *stream, const cTSCameraDescription *d);          // 0xbf65f0
};
static_assert(offsetof(cTSCameraDescription, mShakeAspect) == 0x30 && offsetof(cTSCameraDescription, mHeading) == 0x38 &&
              offsetof(cTSCameraDescription, mZoom) == 0xa8 && offsetof(cTSCameraDescription, mSelect) == 0xac &&
              sizeof(cTSCameraDescription) == 0xc8);

// type 11 (lane IW). Constructor 0xbf7280, destructor 0xbf7320, copy assignment 0xbf73c0 (the compiler's), serializers
// 0xbf8190 / 0xbf8550; the preparation pass is 0xc09ee0 (not ours).
struct cTSScreenDescription {
    uint32_t mId = 0xcbce1184u;          // +0x00 (not serialized)
    uint32_t mMode = 0;                  // +0x04 (serialized as a byte)
    uint32_t mFlags = 0;                 // +0x08 (2 bits read)
    std::vector<fxdesc::Color> mColors;  // +0x0c
    cTSEffectCurve mStrength{1.0f};      // +0x18
    float mLength = 2.0f;                // +0x28
    float mDelay = 0.0f;                 // +0x2c
    std::string mTexture;                // +0x30

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSScreenDescription *d); // 0xbf8190
    static void __cdecl Write(void *stream, const cTSScreenDescription *d);          // 0xbf8550
};
static_assert(offsetof(cTSScreenDescription, mStrength) == 0x18 && offsetof(cTSScreenDescription, mTexture) == 0x30 &&
              sizeof(cTSScreenDescription) == 0x48);

// type 13 (lane IW). The whole object's constructor 0xbf8c20 (the factory's and the parser's light commands', not ours),
// destructor 0xbf8cd0, copy assignment 0xbf8d80 (the compiler's), serializers 0xbf9280 / 0xbf95a0; the preparation pass is
// 0xc0a140 (not ours).
struct cTSLightDescription {
    uint32_t mId = 0xcd338355u;          // +0x00 (not serialized)
    uint32_t mFlags = 0;                 // +0x04 (2 bits read)
    uint32_t mValue8 = 1;                // +0x08 (not serialized)
    std::vector<fxdesc::Color> mColors;  // +0x0c
    cTSEffectCurve mCurve18;             // +0x18
    cTSEffectCurve mCurve28;             // +0x28
    float mFloat38 = 0.0f;               // +0x38

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSLightDescription *d); // 0xbf9280
    static void __cdecl Write(void *stream, const cTSLightDescription *d);          // 0xbf95a0
};
static_assert(offsetof(cTSLightDescription, mCurve28) == 0x28 && sizeof(cTSLightDescription) == 0x3c);

// ---- decal (type 3, lane IW) ----
// cTSDecalDescription (0x8c). Constructor 0xbeb2c0, destructor 0xbeb410, move 0xbeb5a0, copy 0xbeb760 (member-wise),
// serializers 0xbec8e0 / 0xbecd10. The parser's `decal` block names the fields (src/gz/ParticleCommands.h DecalDescription,
// the same layout, with its flag bits).
struct cTSDecalDescription {
    uint32_t mId = 0x2c0fcaceu;          // +0x00 (not serialized)
    uint32_t mFlags = 0;                 // +0x04 (7 bits read)
    std::string mTexture;                // +0x08
    uint8_t mDraw = 0;                   // +0x20
    uint8_t mLifeMode = 0;               // +0x21
    float mLife = 5.0f;                  // +0x24
    cTSEffectCurve mRotate;              // +0x28
    cTSEffectCurve mSize{1.0f};          // +0x38
    cTSEffectCurve mAlpha{1.0f};         // +0x48
    std::vector<fxdesc::Color> mColors{fxdesc::Color(1.0f, 1.0f, 1.0f)}; // +0x58 one white
    cTSEffectCurve mAspect{1.0f};        // +0x64
    float mAlphaVary = 0.0f;             // +0x74
    float mSizeVary = 0.0f;              // +0x78
    float mRotateVary = 0.0f;            // +0x7c
    float mRepeat = 1.0f;                // +0x80
    fxdesc::Vector2 mOffset{0.0f, 0.0f}; // +0x84

    static constexpr uint16_t kVersion = 1;
    static bool Accepts(uint16_t version) { return version == 1; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSDecalDescription *d); // 0xbec8e0
    static void __cdecl Write(void *stream, const cTSDecalDescription *d);          // 0xbecd10
};
static_assert(offsetof(cTSDecalDescription, mLife) == 0x24 && offsetof(cTSDecalDescription, mColors) == 0x58 &&
              offsetof(cTSDecalDescription, mAspect) == 0x64 && offsetof(cTSDecalDescription, mOffset) == 0x84 &&
              sizeof(cTSDecalDescription) == 0x8c);

// ---- the template ----

template <class D, int32_t kType> class cTSCompDescriptionBaseT : public cITSComponentDescription, public rz::cRZRefCount {
public:
    using Description = D;
    static constexpr int32_t kComponentType = kType;

    cTSCompDescriptionBaseT() = default;
    ~cTSCompDescriptionBaseT() override = default; // +4 face slot 0: the scalar deleting destructor
    cTSCompDescriptionBaseT(const cTSCompDescriptionBaseT &) = delete;
    cTSCompDescriptionBaseT &operator=(const cTSCompDescriptionBaseT &) = delete;

    // The factory (cdecl): new, null on failure. Out of line (src/ts/cTSCompDescription.cpp), so a test can stand in for it.
    __declspec(noinline) static cITSComponentDescription *__cdecl Create();

    int32_t GetType() override { return kType; }
    cITSComponentEffect *CreateEffect() override { return mDescription.CreateEffect(); }
    // The target's type, then ours (both through the vtables); the same: the description copied over.
    void CopyTo(cITSComponentDescription *to) override
    {
        const int32_t theirs = to->GetType();
        if (theirs == GetType()) CopyDescription(static_cast<cTSCompDescriptionBaseT *>(to)->mDescription, mDescription);
    }
    void *GetDescription() override { return &mDescription; }
    void Read(void *stream) override
    {
        uint16_t version;
        fxdesc::GetUint16(stream, &version);
        if (D::Accepts(version)) D::Read(stream, version, &mDescription);
    }
    void Write(void *stream) override
    {
        fxdesc::PutUint16(stream, D::kVersion);
        D::Write(stream, &mDescription);
    }
    uint32_t GetSize() override { return sizeof(cTSCompDescriptionBaseT); }
    uint32_t AddRef() override { return rz::cRZRefCount::AddRef(); }
    uint32_t Release() override { return rz::cRZRefCount::Release(); }

    D mDescription;

private:
    // The plain descriptions are copied as their bytes (the original's block moves, padding included); the others member by
    // member (a string or vector assigned unless it is the same one).
    static void CopyDescription(D &to, const D &from)
    {
        if constexpr (std::is_trivially_copyable_v<D>)
            std::memcpy(static_cast<void *>(&to), &from, sizeof(D));
        else
            to = from;
    }
};

using cTSBrushComponentDescription = cTSCompDescriptionBaseT<cTSBrushDescription, 4>;
using cTSScrubberComponentDescription = cTSCompDescriptionBaseT<cTSScrubberDescription, 5>;
using cTSSoundComponentDescription = cTSCompDescriptionBaseT<cTSSoundDescription, 7>;
using cTSGameComponentDescription = cTSCompDescriptionBaseT<cTSGameDescription, 9>;
using cTSWaterComponentDescription = cTSCompDescriptionBaseT<cTSWaterDescription, 12>;
using cTSSequenceComponentDescription = cTSCompDescriptionBaseT<cTSSequenceDescription, 6>;
using cTSModelComponentDescription = cTSCompDescriptionBaseT<cTSModelDescription, 10>;
using cTSCameraComponentDescription = cTSCompDescriptionBaseT<cTSCameraDescription, 8>;
using cTSScreenComponentDescription = cTSCompDescriptionBaseT<cTSScreenDescription, 11>;
using cTSLightComponentDescription = cTSCompDescriptionBaseT<cTSLightDescription, 13>;
using cTSDecalComponentDescription = cTSCompDescriptionBaseT<cTSDecalDescription, 3>;
static_assert(sizeof(cTSDecalComponentDescription) == 0x98);
static_assert(sizeof(cTSSequenceComponentDescription) == 0x20 && sizeof(cTSModelComponentDescription) == 0x40);
static_assert(sizeof(cTSCameraComponentDescription) == 0xd4 && sizeof(cTSScreenComponentDescription) == 0x54 &&
              sizeof(cTSLightComponentDescription) == 0x48);

static_assert(offsetof(cTSBrushComponentDescription, mDescription) == 0xc && sizeof(cTSBrushComponentDescription) == 0x34);
static_assert(sizeof(cTSScrubberComponentDescription) == 0x40 && sizeof(cTSSoundComponentDescription) == 0x20);
static_assert(sizeof(cTSGameComponentDescription) == 0x1c && sizeof(cTSWaterComponentDescription) == 0x34);

// The RVA hooks' entry points (src/hook/tables/cTSCompDescription.cpp): reached through the game's vtables (descriptions the
// original parser commands build). Original calling convention (this in ecx), our members called without virtual dispatch.
template <class T> struct cTSCompDescriptionAdapters {
    using D = typename T::Description;
    // Out of line: the parser's cWaterEffectCommand::EndBlock calls it, and its test stands in for it.
    __declspec(noinline) static T *__fastcall Construct(void *memory, void *) { return ::new (memory) T; }
    // The description struct's own constructor, destructor, move and copy assignment (this = the struct). Out of line: the
    // parser's camera and screen blocks call them, and their tests stand in for them.
    __declspec(noinline) static D *__fastcall ConstructDescription(void *memory, void *) { return ::new (memory) D; }
    __declspec(noinline) static void __fastcall DestroyDescription(D *self, void *) { self->~D(); }
    __declspec(noinline) static D *__fastcall MoveDescription(D *self, void *, D *from)
    {
        *self = std::move(*from);
        return self;
    }
    __declspec(noinline) static D *__fastcall CopyDescription(D *self, void *, const D *from)
    {
        *self = *from;
        return self;
    }
    static uint32_t __fastcall GetSize(T *self, void *) { [[msvc::forceinline_calls]] return self->T::GetSize(); }
    static T *__fastcall DeletingDestructor(T *self, void *, uint32_t flags)
    {
        [[msvc::forceinline_calls]] self->T::~T();
        if (flags & 1) ::operator delete(self, sizeof(T));
        return self;
    }
    static int32_t __fastcall GetType(T *self, void *) { [[msvc::forceinline_calls]] return self->T::GetType(); }
    static cITSComponentEffect *__fastcall CreateEffect(T *self, void *) { [[msvc::forceinline_calls]] return self->T::CreateEffect(); }
    static void __fastcall CopyTo(T *self, void *, cITSComponentDescription *to) { [[msvc::forceinline_calls]] self->T::CopyTo(to); }
    static void __fastcall Read(T *self, void *, void *stream) { [[msvc::forceinline_calls]] self->T::Read(stream); }
    static void __fastcall Write(T *self, void *, void *stream) { [[msvc::forceinline_calls]] self->T::Write(stream); }
};
