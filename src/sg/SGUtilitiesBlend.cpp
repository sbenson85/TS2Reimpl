// nGZSceneGraph's free helpers: controller blends, controller instances and skeleton scaling (lane 261111j). See the
// header's "controller blends" section.
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h" // the SSE operations in operand order, the x87 helpers
#include "game/vtable.h"
#include "misc/X87.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject

#include <bit>
#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::convex::Add87;
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::FloatOf;
using nGZCompGeom::convex::Mul87;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::Stored;
using nGZCompGeom::convex::SubS;
using nGZCompGeom::convex::X87Pick;

constexpr uint32_t kBlendClass = 0x8b969d64, kBlendIid = 0x8b969d59;
constexpr uint32_t kInstanceClass = 0x2a6f2e97, kInstanceIid = 0x0a6f2e8d;
constexpr uint32_t kTraversalClass = 0x5ca10ca7, kTraversalIid = 0x5ca10ca0;
constexpr uint32_t kQueuedBlendIid = 0xdc1d0085; // asked of the controller's cIGZUnknown
constexpr uint32_t kModelIid = 0x4512dd13;       // asked of the skeleton's owner
constexpr uint32_t kBoneScaleIid = 0xe9075bbe;   // the extension a node scales by
constexpr uint32_t kNoTick = 0x80000100;
constexpr float kForever = std::bit_cast<float>(0x4efffffeu); // 2147483392, the largest float under 2^31
constexpr float kFitSlack = 1e-6f, kOverSlack = 1e-5f;        // 0x358637bd, 0x3727c5ac
constexpr float kShrink = std::bit_cast<float>(0x3f7ff972u);  // 0.9999
constexpr float kPhaseShrink = std::bit_cast<float>(0x3f7ae148u); // 0.98
constexpr float kMilli = std::bit_cast<float>(0x3a83126fu);   // 0.001
constexpr float kHoldPad = std::bit_cast<float>(0x3c23d70au); // 0.01
constexpr int kFrameworkCreate = 3;

// The blend's own slots (class 0x8b969d64; arguments from the pushes).
constexpr int kBlendSetup = 2;                     // (13 words), see MakeAnimationBlend
constexpr int kBlendTime = 10;                     // (): an x87 float
constexpr int kBlendElapsed = 11;                  // (): an x87 float
constexpr int kBlendLength = 14;                   // (): an x87 float
constexpr int kBlendFadeIn = 15, kBlendFadeOut = 16; // (): x87 floats
constexpr int kBlendParam20 = 20, kBlendParam21 = 21, kBlendParam22 = 22; // (): x87 floats, set-up words 3-5
constexpr int kBlendSpan = 24;                     // (from, to): an x87 float
constexpr int kBlendCurve = 26;                    // (0): an object whose slot 7 is an x87 float, or null
constexpr int kCurveLead = 7;
// The controller's slots (class not settled).
constexpr int kCtrlAnnounce = 7;     // (a1, state, 1)
constexpr int kCtrlSetState = 13;    // (state)
constexpr int kCtrlSetFade = 15;     // (float)
constexpr int kCtrlSetBlend = 16;    // (blend face or null)
constexpr int kCtrlSetFlag18 = 18;   // (1)
constexpr int kCtrlSetRate = 19;     // (rate)
constexpr int kCtrlState = 30;       // (): the state word
constexpr int kCtrlPending = 32;     // (): a byte
constexpr int kCtrlLinked = 34;      // (): an object whose slot 36 is an x87 float, or null
constexpr int kCtrlStartTick = 35;   // (): a tick
constexpr int kCtrlTicksFrom = 38;   // (1): a tick count
constexpr int kCtrlNowTick = 39;     // (): a tick, kNoTick when unknown
constexpr int kCtrlFadeTime = 44;    // (): an x87 float
constexpr int kCtrlBlend = 45;       // (): the controller's blend, or null
constexpr int kCtrlBlendIndex = 47;  // (): -1 when it has none
constexpr int kCtrlLinkedFirst = 48; // (): a byte
constexpr int kCtrlRateScale = 49;   // (): an x87 float
constexpr int kCtrlCanFade = 56;     // (): a byte
constexpr int kCtrlBusy = 60;        // (): a byte
constexpr int kLinkedLevel = 36;
constexpr int kQueuedBlend = 25;     // the queued-blend face's (): a blend, or null
constexpr int kQueuedClear = 7;      // (0)

bool Create(uint32_t classId, uint32_t iid, void **out)
{
    // receiver: the framework (cIGZFrameworkW32's slot 3, CreateInstance).
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkCreate>(
        GetDirectorFrameworkObject(), classId, iid, out);
}

// A face's virtual base number `index`, the vbptr at `vbptrOffset`.
void *VirtualBaseAt(void *face, int vbptrOffset, int index)
{
    char *p = static_cast<char *>(face) + vbptrOffset;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}
void AddRefAt4(void *face) { RawSlot<1>(VirtualBaseAt(face, 4, 1)); }  // receiver: its cIGZUnknown
void ReleaseAt4(void *face) { RawSlot<2>(VirtualBaseAt(face, 4, 1)); } // receiver: its cIGZUnknown

float CountAsFloat(uint32_t count) { return float(double(count)); } // unsigned, through a double
float MaxZero(float x) { return 0.0f > x ? 0.0f : x; }              // maxss 0, x: a NaN x stays
float Half(float x) { return float(double(x) * 0.5); }              // cvtps2pd, mulsd 0.5, cvtpd2ps
float Magnitude(float x) { return FloatOf(BitsOf(x) & 0x7fffffffu); }

// One x87 operation between the register `reg` and the float in memory `mem` (whichever side it is on): a NaN in the
// register beats a signalling memory NaN, two NaNs otherwise go by X87Pick; else `plain`.
double WithNaNs87(double reg, float mem, double plain)
{
    if (reg != reg && mem != mem)
        return (BitsOf(mem) & 0x00400000u) ? X87Pick(reg, static_cast<double>(mem)) : reg;
    return x87::Rounded(plain);
}
double SubMem87(double reg, float mem) { return WithNaNs87(reg, mem, reg - mem); }  // fsub m32
double SubRMem87(float mem, double reg) { return WithNaNs87(reg, mem, mem - reg); } // fsubr m32
double DivMem87(double reg, float mem) { return WithNaNs87(reg, mem, reg / mem); }  // fdiv m32
double DivRMem87(float mem, double reg) { return WithNaNs87(reg, mem, mem / reg); } // fdivr m32

using FloatGetter = float(__thiscall *)(void *);
template <int kSlot> float GetFloat(void *object) { return VirtualCall<FloatGetter, kSlot>(object); }

// The controller's ticks since its start, as a float not under 0 (the difference converted as an int).
float ElapsedTicks(void *controller)
{
    const uint32_t now = RawSlot<kCtrlNowTick>(controller);
    const uint32_t start = RawSlot<kCtrlStartTick>(controller);
    return MaxZero(static_cast<float>(static_cast<int32_t>(now - start)));
}

// A blend's slot 2 with the fade words read from it in the original's order (16, 15, 22, 21, 20).
bool SetupFromOwn(void *blend, uint32_t a, uint32_t b, float length)
{
    const float fadeOut = GetFloat<kBlendFadeOut>(blend);
    const float fadeIn = GetFloat<kBlendFadeIn>(blend);
    const float p22 = GetFloat<kBlendParam22>(blend);
    const float p21 = GetFloat<kBlendParam21>(blend);
    const float p20 = GetFloat<kBlendParam20>(blend);
    return 0 != RawSlot<kBlendSetup, uint8_t>(blend, a, b, p20, p21, p22, fadeIn, fadeOut, length, 0u, 0u, 0u, 0u, 0u);
}

} // namespace

// 0xae50e0
int32_t BlendOverlapMode(void *blend)
{
    const float time = GetFloat<kBlendTime>(blend); // receiver: the blend
    void *const curve = RawSlot<kBlendCurve, void *>(blend, 0u);
    if (!curve || !(-kFitSlack > time)) return 0;
    // receiver: the curve object. fsubr: 1e-6 minus its value, on the x87.
    const float lead = Stored(x87::Rounded(static_cast<double>(kFitSlack) - GetFloat<kCurveLead>(curve)));
    return lead > time ? 2 : 1;
}

// 0xae4d10
bool MakeBlendForSource(void **out, float rate, void *source, float a4, float a5, int32_t loops, float start,
                        float end, int32_t mode, float blendIn, float blendOut, float a12, float a13, float a14)
{
    *out = nullptr;
    Create(kBlendClass, kBlendIid, out);
    float length = kForever;
    if (loops != -1 && rate > 0.0f) {
        if (source) {
            length = GetFloat<kBlendLength>(source); // receiver: the source (its slot 14)
        } else {
            const float sum =
                AddS(MulS(SubS(end, start), rate), MulS(CountAsFloat(static_cast<uint32_t>(loops) - 1u), rate));
            length = DivS(MaxZero(sum), MulS(MulS(a4, a5), kMilli));
            if (0.0f > length) length = Negated(length);
        }
    }
    const float total = AddS(blendIn, blendOut);
    bool fits = true;
    if (mode == 0) {
        if (total > AddS(length, kFitSlack)) {
            const float scale = MulS(DivS(length, total), kShrink);
            fits = !(total > AddS(length, kOverSlack));
            blendOut = MulS(blendOut, scale);
            blendIn = MulS(blendIn, scale);
        }
    } else if (mode == 1) {
        const float half = MulS(total, 0.5f);
        if (half > AddS(length, kFitSlack)) {
            fits = !(half > AddS(length, kOverSlack));
            mode = 2;
        }
    }
    // receiver: the new blend (its own slot 2). ORIGINAL-BUG: a failed creation is called through null.
    RawSlot<kBlendSetup>(*out, 1u, mode, a12, a13, a14, blendIn, blendOut, length, 0u, 0u, 0u, 0u, 0u);
    return fits;
}

// 0xae4900
bool MakeTransitionBlend(void **out, float rate, int32_t loops, float start, float end, float a6, float a7, float a8,
                         float a9, float a10, float a11, uint32_t a12, float a13, float a14, float a15, float a16)
{
    a9 = MulS(a9, kMilli);
    a10 = MulS(a10, kMilli);
    a11 = MulS(a11, kMilli);
    a15 = MulS(a15, kMilli);
    a16 = MulS(a16, kMilli);
    *out = nullptr;
    Create(kBlendClass, kBlendIid, out);
    float length = kForever;
    if (loops != -1 && rate > 0.0f)
        length = MaxZero(
            AddS(MulS(SubS(end, start), rate), MulS(CountAsFloat(static_cast<uint32_t>(loops) - 1u), rate)));

    const bool split = (a12 & 0xff) != 0;
    float p1, p2, p3, p4;
    auto phases = [&] {
        if (split) {
            p1 = MulS(a9, a7);
            p2 = MulS(a11, a8);
            p3 = Half(MulS(AddS(a15, a10), a13));
            p4 = Half(MulS(AddS(a16, a10), a14));
        } else {
            p1 = Half(MulS(AddS(a10, a9), a7));
            p2 = Half(MulS(AddS(a11, a10), a8));
            p3 = 0.0f;
            p4 = 0.0f;
        }
    };
    phases();
    const float total = AddS(AddS(AddS(p2, p1), p3), p4);
    bool fits = true;
    if (total > AddS(length, kFitSlack)) {
        fits = !(total > AddS(length, kOverSlack));
        const float s = static_cast<float>(std::sqrt(static_cast<double>(DivS(length, total))));
        a13 = MulS(s, a13);
        a7 = MulS(s, a7);
        a9 = MulS(s, a9);
        a11 = MulS(s, a11);
        a10 = MulS(MulS(s, a10), kPhaseShrink);
        a8 = MulS(s, a8);
        a14 = MulS(s, a14);
        a16 = MulS(MulS(s, a16), kPhaseShrink);
        a15 = MulS(MulS(s, a15), kPhaseShrink);
        phases();
    }
    a6 = Magnitude(a6);
    float hold = kForever;
    if (!(length == kForever)) {
        const float rest = SubS(SubS(SubS(SubS(length, p1), p2), p3), p4);
        hold = DivS(AddS(AddS(AddS(AddS(Magnitude(DivS(AddS(rest, kHoldPad), a10)), a7), a8), a13), a14), a6);
    }
    // receiver: the new blend (its own slot 2). ORIGINAL-BUG: a failed creation is called through null.
    RawSlot<kBlendSetup>(*out, 2u, 0u, a9, a10, a11, DivS(a7, a6), DivS(a8, a6), hold, a12, DivS(a13, a6),
                         DivS(a14, a6), a15, a16);
    return fits;
}

// 0xae5140
bool StartControllerBlend(uint32_t a1, void *controller, int32_t mode, float a4, uint32_t a5, float a6)
{
    // receiver: the controller throughout (its own slots), unless noted.
    if (RawSlot<kCtrlBusy, uint8_t>(controller)) return false;
    if (RawSlot<kCtrlPending, uint8_t>(controller)) {
        RawSlot<kCtrlAnnounce>(controller, a1, RawSlot<kCtrlState>(controller), 1u);
        return false;
    }
    const uint8_t linkedFirst = RawSlot<kCtrlLinkedFirst, uint8_t>(controller);
    RawSlot<kCtrlSetFlag18>(controller, 1u);
    const bool counted = (a5 & 0xff) != 0;
    if (!counted && 0.0f >= a6) {
        if (!RawSlot<kCtrlCanFade, uint8_t>(controller) || kMilli >= GetFloat<kCtrlFadeTime>(controller)) {
            RawSlot<kCtrlSetState>(controller, 2u);
            RawSlot<kCtrlSetFade>(controller, kMilli);
            RawSlot<kCtrlSetBlend>(controller, 0u);
            RawSlot<kCtrlAnnounce>(controller, a1, 3u, 1u);
            return true;
        }
    }
    void *blend = RawSlot<kCtrlBlend, void *>(controller);
    if (blend)
        AddRefAt4(blend);
    else
        Create(kBlendClass, kBlendIid, &blend);
    const float elapsed = ElapsedTicks(controller);
    float lead;
    if (counted) {
        RawSlot<kCtrlSetState>(controller, 5u);
        lead = AddS(a6, static_cast<float>(static_cast<int32_t>(RawSlot<kCtrlTicksFrom>(controller, 1u))));
    } else {
        RawSlot<kCtrlSetState>(controller, 4u);
        lead = a6;
    }
    if (mode == 0)
        lead = SubS(lead, a4);
    else if (mode == 1)
        lead = SubS(lead, MulS(a4, 0.5f));
    float fade = a4;
    if (0.0f > lead) {
        fade = MaxZero(AddS(lead, a4));
        lead = 0.0f;
    }
    lead = AddS(lead, elapsed);
    float width = fade;
    if (linkedFirst && RawSlot<kCtrlLinked, void *>(controller)) {
        void *const linked = RawSlot<kCtrlLinked, void *>(controller);
        if (0.0f >= GetFloat<kLinkedLevel>(linked)) width = a4; // receiver: the linked object
    }
    const float first = GetFloat<kCtrlFadeTime>(controller);
    const float second = GetFloat<kCtrlFadeTime>(controller);
    // receiver: the blend (its own slot 2). ORIGINAL-BUG: a failed creation is called through null.
    RawSlot<kBlendSetup>(blend, 1u, 2u, second, first, kMilli, 0u, width, lead, 0u, 0u, 0u, 0u, 0u);
    RawSlot<kCtrlSetBlend>(controller, blend);
    RawSlot<kCtrlAnnounce>(controller, a1, RawSlot<kCtrlState>(controller), 1u);
    if (blend) ReleaseAt4(blend);
    return true;
}

// 0xae5470
bool RetimeControllerBlends(void *controller, float a2)
{
    // receiver: the controller (its own slots), unless noted.
    if (RawSlot<kCtrlNowTick>(controller) == kNoTick) return false;
    void *queued = nullptr;
    // receiver: the controller's cIGZUnknown (QueryInterface).
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(VirtualBaseAt(controller, 4, 1), kQueuedBlendIid,
                                                                   &queued);
    const float elapsed = ElapsedTicks(controller);
    if (queued) {
        void *const blend = RawSlot<kQueuedBlend, void *>(queued); // receiver: the queued-blend face
        // receiver: that face's second virtual base.
        if (blend && RawSlot<kCtrlBlendIndex, int32_t>(VirtualBaseAt(queued, 4, 2)) != -1) {
            using Span = float(__thiscall *)(void *, float, float);
            // receiver: the queued blend.
            const float before = VirtualCall<float(__thiscall *)(void *, uint32_t, float), kBlendSpan>(blend, 0u, elapsed);
            const float length = GetFloat<kBlendLength>(blend);
            const float after = VirtualCall<Span, kBlendSpan>(blend, elapsed, length);
            float span = after;
            if (GetFloat<kCtrlRateScale>(controller) != 0.0f && a2 != 0.0f)
                span = Stored(DivMem87(Mul87(GetFloat<kCtrlRateScale>(controller), after), a2));
            if (!SetupFromOwn(blend, 2u, 0u, AddS(span, before))) RawSlot<kQueuedClear>(queued, 0u);
        }
    }
    void *const own = RawSlot<kCtrlBlend, void *>(controller);
    if (own && RawSlot<kCtrlBlendIndex, int32_t>(controller) != -1) {
        // receiver: the controller's blend.
        const float fadeOut = GetFloat<kBlendFadeOut>(own);
        const float fades = Stored(Add87(GetFloat<kBlendFadeIn>(own), fadeOut));
        float left = Stored(SubMem87(GetFloat<kBlendElapsed>(own), elapsed));
        int32_t overlap = BlendOverlapMode(own);
        if (overlap == 1)
            left = Stored(SubRMem87(left, Mul87(GetFloat<kBlendFadeOut>(own), 0.5f)));
        else if (overlap == 2)
            left = Stored(SubRMem87(left, GetFloat<kBlendFadeOut>(own)));
        float at = elapsed;
        if (GetFloat<kCtrlRateScale>(controller) != 0.0f && a2 != 0.0f)
            at = AddS(elapsed, DivS(MulS(GetFloat<kCtrlRateScale>(controller), left), a2));
        if (overlap == 0) {
            if (fades > AddS(at, kFitSlack)) {
                overlap = 1;
                if (MulS(fades, 0.5f) > AddS(at, kFitSlack)) overlap = 2;
            }
        } else if (overlap == 1) {
            if (MulS(fades, 0.5f) > AddS(at, kFitSlack)) overlap = 2;
        }
        SetupFromOwn(own, 1u, static_cast<uint32_t>(overlap), at);
    }
    RawSlot<kCtrlSetRate>(controller, a2);
    if (queued) ReleaseAt4(queued);
    return true;
}

// 0xae2b50
bool MakeControllerInstance(void **out, void *a2, uint32_t a3, uint32_t a4, const float *a5, float a6, float a7,
                            uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13,
                            uint32_t a14, uint32_t a15)
{
    void *instance = nullptr;
    Create(kInstanceClass, kInstanceIid, &instance);
    // receiver: the instance's own face, unless noted. ORIGINAL-BUG: a failed creation is called through null.
    RawSlot<0>(instance, a3);
    float frame[7]; // a5's position, no rotation (0, 0, 0, 1)
    std::memcpy(frame, a5, 3 * sizeof(float));
    frame[3] = frame[4] = frame[5] = 0.0f;
    frame[6] = 1.0f;
    RawSlot<0>(VirtualBaseAt(instance, 4, 3), a4, static_cast<const float *>(frame)); // receiver: its third virtual base
    RawSlot<1>(instance, a6);
    RawSlot<2>(instance, a7);
    RawSlot<3>(instance, a8);
    RawSlot<6>(instance, a8);
    RawSlot<4>(instance, a9);
    void *const settings = VirtualBaseAt(instance, 4, 2); // receiver: its second virtual base (the settings)
    RawSlot<10>(settings, a10);
    RawSlot<11>(settings, 0.0f);
    RawSlot<12>(settings, 1.0f);
    RawSlot<13>(settings, a11);
    RawSlot<14>(settings, 8u);
    RawSlot<15>(settings, 1.0f);
    RawSlot<17>(settings, 0u);
    RawSlot<18>(settings, 1u);
    RawSlot<19>(settings, 1.0f);
    RawSlot<24>(settings, a12);
    RawSlot<7>(instance, a13);
    RawSlot<8>(instance, a14);
    RawSlot<25>(settings, a15);
    if (a2) RawSlot<10>(a2, settings); // receiver: a2
    *out = instance;
    AddRefAt4(instance);
    ReleaseAt4(instance);
    return true;
}

// 0xae3cd0
bool ScaleSkeleton(void *skeleton, float scale, uint32_t skipModel)
{
    // receiver: the skeleton's cIExtension (slot 18, GetOwner), asked twice.
    void *const extension = VirtualBaseAt(skeleton, 4, 1);
    if (!RawSlot<18, void *>(extension)) return false;
    void *const owner = RawSlot<18, void *>(extension);
    void *model = nullptr;
    bool result = false;
    // receiver: the owner (QueryInterface).
    if (VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(owner, kModelIid, &model)) {
        void *traversal = nullptr;
        Create(kTraversalClass, kTraversalIid, &traversal);
        // receiver: the traversal (its own slots: 16 filter, 17 (0), 3 traverse, 22 count, 21 (i) a node), but slot 66.
        // ORIGINAL-BUG: a failed creation is called through null.
        RawSlot<16>(traversal, RawSlot<66>(skeleton));
        RawSlot<17>(traversal, 0u);
        RawSlot<3>(traversal, model);
        const uint32_t count = RawSlot<22>(traversal);
        if (count) {
            const float ratio = Stored(DivRMem87(scale, GetFloat<67>(skeleton))); // receiver: the skeleton
            for (uint32_t i = 0; i < count; ++i) {
                void *const node = RawSlot<21, void *>(traversal, i);
                if ((skipModel & 0xff) && node == model) continue;
                float factor = ratio;
                int32_t axis = 1;
                void *bone = nullptr;
                // receiver: the node (FindExtension), then the extension found (its own slots 7 axis, 8 weight).
                if (RawSlot<24, uint8_t>(node, kBoneScaleIid, &bone, 0u)) {
                    axis = RawSlot<7, int32_t>(bone);
                    const float weight = GetFloat<8>(bone);
                    const float numerator = AddS(MulS(SubS(scale, 1.0f), weight), 1.0f);
                    const float denominator = AddS(MulS(SubS(GetFloat<67>(skeleton), 1.0f), weight), 1.0f);
                    factor = DivS(numerator, denominator);
                }
                if (axis != 3) {
                    float local[7];
                    // receiver: the node (slot 53, GetLocalTransform: the transform's address back in eax).
                    const float *const transform = RawSlot<53, const float *>(node, local);
                    float position[3];
                    std::memcpy(position, transform, sizeof position);
                    if (axis == 4) {
                        for (float &p : position) p = MulS(p, factor);
                    } else if (axis >= 0 && axis < 3) {
                        position[axis] = MulS(factor, position[axis]);
                    }
                    // ORIGINAL-BUG: any other axis writes outside the three floats (the original's own frame); not
                    // reproduced.
                    RawSlot<64>(node, static_cast<const float *>(position)); // SetTranslation
                }
                if (bone) ReleaseAt4(bone);
            }
            RawSlot<31>(skeleton, scale);
            result = true;
        }
        if (traversal) RawSlot<2>(traversal);
    }
    if (model) RawSlot<2>(model);
    return result;
}

} // namespace nGZSceneGraph
