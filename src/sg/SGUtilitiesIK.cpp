// nGZSceneGraph's look-at and IK helpers (lane 261111i). See the header's "look-at and IK" section.
#include "sg/SGUtilities.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/StaticGetters.h"  // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h" // GetCachedObjectB: the string pool
#include "sg/SGNodeInterfaces.h"
#include "sg/cStringPool.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <emmintrin.h>
#include <string>
#include <string.h> // _stricmp

namespace nGZSceneGraph {

namespace {

// ClampLookAngles' limits: .data words 0x1070f1c-0x1070f28, read by nothing else and written by nothing.
constexpr float kPitchLow = std::bit_cast<float>(0xbf63dec4u);  // -51 degrees
constexpr float kPitchHigh = std::bit_cast<float>(0x3f20d97cu); // 36 degrees
constexpr float kYawLimit = std::bit_cast<float>(0x3f9c61aau);  // 70 degrees
constexpr float kRollLimit = std::bit_cast<float>(0x3f32b8c2u); // 40 degrees

// The look-at subject (nTSSG::cAnimatable's object; its class is not settled): its slots by what the callers show.
constexpr int kSubjectNode = 49;    // (id): a node of the subject's model, or null
constexpr int kSubjectCanLook = 73; // (): whether the subject looks at things at all
constexpr int kSubjectTurn = 76;    // (): an object whose +0xc holds the head's turn quaternion
constexpr uint32_t kHeadNodeId = 0xd08f50db;
constexpr size_t kTurnOffset = 0xc;

// The IK rig and target (their classes are not settled).
constexpr int kTargetKind = 15;           // (): 1 the caller's node, 2 the rig's node
constexpr int kTargetNameId = 22;         // (): the target's name id
constexpr int kTargetAltNameId = 23;      // (): the id the rig's virtual base slot 46 picks instead
constexpr int kRigNode = 5;               // (0): the rig's node
constexpr int kRigHasNode = 7;            // (): whether the rig has one
constexpr int kRigBaseUsesAltId = 46;     // the rig's second virtual base: (): which id
constexpr int kRigBaseSettings = 28;      // the rig's second virtual base: (): an object whose slot 66 is the skip flag
constexpr int kSettingsSkipFlag = 66;     // ()
constexpr int kAnimatedFindTarget = 51;   // the animated face: (id, bone out): the node for the id
constexpr uint32_t kRootTargetId = 0x6e80a11a;
constexpr uint32_t kBoneDataIid = 0xe9075bbe; // cIBoneDataExtension (sg/cBoneDataExtension.h)
constexpr uint32_t kKindRigNode = 2, kKindSelf = 1;

// The original's scalar operations with their operand order (a NaN pair gives the first operand's NaN, quieted).
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;

// cvtps2pd; sqrtsd; cvtsd2ss: the square root in double, a NaN kept as it is (quieted). (The original calls the CRT's
// sqrt only below zero, which a sum of squares never is.)
float Root(float v) { return static_cast<float>(_mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(v)))); }

// The CRT's atan2 (0x831270, on the x87) of two floats widened to double, rounded back to float. A NaN operand comes
// back as itself (quieted); of two, the x87's choice (the larger significand, equal ones the positive).
float Atan2(float y, float x)
{
    const double dy = y, dx = x;
    if (dy != dy && dx != dx) return static_cast<float>(nGZCompGeom::convex::X87Pick(dy, dx));
    if (dy != dy) return static_cast<float>(dy);
    if (dx != dx) return static_cast<float>(dx);
    return static_cast<float>(std::atan2(dy, dx));
}

// Matrix3ToEuler's gimbal threshold: entry 8 of the .data table at 0x10859e8 (read by 0xc643a0, written by nothing):
// 0x387f0000, about 6.08e-5.
constexpr float kEulerEpsilon = std::bit_cast<float>(0x387f0000u);

nGZCompGeom::RigidTransform3 *Rigid(cRigidTransform *t) { return reinterpret_cast<nGZCompGeom::RigidTransform3 *>(t); }

// A face's virtual base number `index` (vbptr at +4).
void *VirtualBase(void *face, int index)
{
    char *p = static_cast<char *>(face) + 4;
    return p + (*reinterpret_cast<int32_t **>(p))[index];
}

bool HasFlag(cICompositionTreeNode *node, int32_t flag) { return flag >= 0 && node->TestFlag(static_cast<uint32_t>(flag)); }

// A held cell emptied before it is filled again (the cell cleared, then the old pointer Released).
void Drop(cITransformNode *&cell)
{
    if (cITransformNode *old = cell) {
        cell = nullptr;
        old->Release();
    }
}

// The subject's head node's parent and grandparent as transform nodes, counted in the cells. False when one is missing.
bool FindNeck(cICompositionTreeNode *head, cITransformNode *&parent, cITransformNode *&grandparent)
{
    // ORIGINAL-BUG: with no head node the original goes on as though both were found and calls through the empty
    // grandparent cell (a null fault).
    if (!head) return true;
    if (!head->GetParent()) return false;
    cICompositionTreeNode *up = head->GetParent();
    Drop(parent);
    if (!up->QueryInterface(cITransformNode::kIid, reinterpret_cast<void **>(&parent))) return false;
    if (!parent->GetParent()) return false;
    up = parent->GetParent();
    Drop(grandparent);
    return up->QueryInterface(cITransformNode::kIid, reinterpret_cast<void **>(&grandparent));
}

// A counted object whose cIGZUnknown is its first virtual base: AddRef (slot 1) and Release (slot 2) there.
void AddRefThroughBase(void *p) { VirtualCall<uint32_t(__thiscall *)(void *), 1>(VirtualBase(p, 1)); }
void ReleaseThroughBase(void *p) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(VirtualBase(p, 1)); }

// CanFindIKTargets' objects (their classes are not settled; slots by what the body asks).
constexpr int kSubjectModel = 18;      // the subject's first virtual base: (): an object asked for the model node
constexpr int kSubjectSkipFlag = 66;   // (): the flag FindNodeByNameConst skips
constexpr int kChainsCount = 51;       // ()
constexpr int kChainsChain = 20;       // (i)
constexpr int kChainLinkCount = 36;    // ()
constexpr int kChainLink = 20;         // (j)
constexpr int kLinkNodeId = 42;        // (): the subject node the link sits on
constexpr int kLinkAltNodeId = 43;     // (): the same when `alt`
constexpr int kLinkTargetCount = 47;   // ()
constexpr int kLinkTarget = 23;        // (k)
constexpr int kTargetName = 16;        // (): the target's node name
constexpr int kTargetAltName = 17;     // (): the same when `alt`
constexpr int kAnimatedFindNode = 49;  // the animated face: (id): the node, or null
constexpr uint32_t kKindOther = 2;

// ShowPlayingAnimations' objects.
constexpr uint32_t kQueueIid = 0x1aff3bd4;     // the animated face (cIChoreoQueue)
constexpr uint32_t kAnimationIid = 0xc9de69ad; // an animation instance
constexpr int kQueueCount = 58;                // the queue: ()
constexpr int kQueueEntry = 9;                 // the queue: (i): an entry, or null
constexpr int kEntryActive = 56;               // an entry: ()
constexpr int kEntryDone = 60;                 // an entry: ()
constexpr int kAnimationResource = 34;         // the animation's second virtual base: (): an object, or null
constexpr int kResourceName = 17;              // that object's first virtual base: (): its name, or null
constexpr uint32_t kLabelColour = 0xffffff00;
// MakeIKTargetsFromNames' objects (classes not settled; slots by what the body asks, arguments from the pushes).
constexpr uint32_t kIkSetClass = 0xfb00791d, kIkSetIid = 0x7b007916;           // made in the caller's cell
constexpr uint32_t kTargetListClass = 0xa9de45ba, kTargetListIid = 0xa9de45ae; // the targets' list
constexpr uint32_t kRootSpaceClass = 0x5b0aff38, kRootSpaceIid = 0xbb0aff2f;   // handed to the root's target
constexpr uint32_t kBoneSpaceClass = 0x1b115b24, kBoneSpaceIid = 0x1b115b28;   // handed to every other target
constexpr uint32_t kTargetClass = 0x5d35d710, kTargetIid = 0x7d35d70a;         // one per named node
constexpr uint32_t kModelIid = 0x4512dd13;                                     // asked of the subject's model holder
constexpr uint32_t kRootSpaceKind = 0xfb5b3383, kBoneSpaceKind = 0x9b5b3506;
constexpr uint32_t kEulerOrder = 0x1020100;                                    // QuatToEuler's order word
constexpr float kDegreesPerRadian = std::bit_cast<float>(0x42652ee1u);        // 57.29578
constexpr int kSetMode3 = 3, kSetMode14 = 14, kSetList = 18;                    // the set: (0), (2), (list)
constexpr int kListSlot9 = 9, kListAdd = 10, kListCount = 31;                   // the list: (1), (target), ()
constexpr int kAnimatableFirst = 45, kAnimatableNext = 46;                      // (cursor out): a node, or null
constexpr int kAnimatableRoot = 74;                                             // (): the root node
constexpr int kCursorRotation = 9;                                              // the cursor's parent: (): a quaternion
constexpr int kTargetSpace = 7, kTargetTag = 9, kTargetSlot12 = 12;            // the target: (space, kind), (tag), ()
constexpr int kTargetAxis = 29, kTargetValue = 30, kTargetSlot39 = 39;          // (1, axis), (0, v, 0, 0, axis, 1), (1)
constexpr const char *kSkippedSuffixes[] = {"_grip",   "target_surface", "_ikpole",     "_hair",      "_ikctr",
                                            "control0", "control1",      "control0rot", "control1rot"};

bool CreateObject(uint32_t classId, uint32_t iid, void **out)
{
    // receiver: the framework (cIGZFrameworkW32's slot 3, CreateInstance).
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(GetDirectorFrameworkObject(), classId,
                                                                                    iid, out);
}

// The space objects have their vbptr at +0: the face handed on is virtual base 2, the cIGZUnknown virtual base 1.
void *FaceAtZero(void *p)
{
    return p ? static_cast<char *>(p) + (*reinterpret_cast<int32_t **>(p))[2] : nullptr;
}
void ReleaseAtZero(void *p)
{
    char *const base = static_cast<char *>(p) + (*reinterpret_cast<int32_t **>(p))[1];
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(base);
}

// Whether `tag` ends in one of the suffixes the targets leave out (case blind).
bool SkippedName(const char *tag, size_t length)
{
    for (const char *suffix : kSkippedSuffixes) {
        const size_t n = std::strlen(suffix);
        if (length >= n && _stricmp(tag + (length - n), suffix) == 0) return true;
    }
    return false;
}

} // namespace

// 0xc63600
float *QuatInverse(float *out, const float *q)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float norm = AddS(AddS(AddS(MulS(x, x), MulS(y, y)), MulS(z, z)), MulS(w, w));
    const float scale = DivS(1.0f, norm);
    out[0] = MulS(Negated(x), scale);
    out[1] = MulS(Negated(y), scale);
    out[2] = MulS(Negated(z), scale);
    out[3] = MulS(w, scale);
    return out;
}

// 0xc63690
float *QuatMultiply(float *out, const float *a, const float *b)
{
    const float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    const float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = SubS(AddS(AddS(MulS(bw, ax), MulS(bx, aw)), MulS(ay, bz)), MulS(az, by));
    out[1] = AddS(AddS(SubS(MulS(by, aw), MulS(bz, ax)), MulS(ay, bw)), MulS(bx, az));
    out[2] = AddS(SubS(AddS(MulS(by, ax), MulS(bz, aw)), MulS(ay, bx)), MulS(bw, az));
    out[3] = SubS(SubS(SubS(MulS(bw, aw), MulS(bx, ax)), MulS(ay, by)), MulS(az, bz));
    return out;
}

// 0xc63380 (lane 261111k)
float *QuatRotate(float *out, const float *v, const float *q)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float yy = MulS(y, y), xx = MulS(x, x), ww = MulS(w, w), zz = MulS(z, z);
    const float vz = v[2];
    const float yx = MulS(y, x), zx = MulS(z, x), wx = MulS(w, x), zy = MulS(z, y), wz = MulS(w, z);
    const float wwMinusXx = SubS(ww, xx);
    const float wy = MulS(w, y);
    const float vy = v[1];
    const float vz2 = AddS(vz, vz), vy2 = AddS(vy, vy);
    const float vx = v[0];
    const float vx2 = AddS(vx, vx);
    out[0] = AddS(AddS(MulS(SubS(SubS(AddS(ww, xx), yy), zz), vx), MulS(SubS(yx, wz), vy2)), MulS(AddS(wy, zx), vz2));
    out[1] = AddS(AddS(MulS(SubS(AddS(wwMinusXx, yy), zz), vy), MulS(AddS(wz, yx), vx2)), MulS(SubS(zy, wx), vz2));
    out[2] = AddS(AddS(MulS(AddS(zy, wx), vy2), MulS(SubS(zx, wy), vx2)), MulS(AddS(SubS(wwMinusXx, yy), zz), vz));
    return out;
}

namespace {

// x87 fsincos of a float (the original loads it with fld and stores both results as floats). The x87 ignores precision
// control here. A NaN comes back quieted in both; an infinity gives the default NaN in both. From 2^63 up the x87 leaves
// the operand unreduced and pushes nothing: the "cosine" store takes the operand itself and the "sine" store pops an
// empty register (the default NaN; the x87 stack is left one short, not reproduced). Below that the toolchain's double
// sine and cosine rounded to float: the x87 reduces by a 66-bit pi, which agrees to float precision while the operand's
// reduction error (about |x| * 2^-66) stays far under a float ulp of the result, that is for |x| below about 2^20.
void SinCos87(float x, float &s, float &c)
{
    constexpr float kDefaultNaN = std::bit_cast<float>(0xffc00000u);
    const uint32_t bits = std::bit_cast<uint32_t>(x);
    const uint32_t magnitude = bits & 0x7fffffffu;
    if (magnitude > 0x7f800000u) {
        s = c = std::bit_cast<float>(bits | 0x00400000u);
    } else if (magnitude == 0x7f800000u) {
        s = c = kDefaultNaN;
    } else if (magnitude >= 0x5f000000u) { // 2^63
        c = x;
        s = kDefaultNaN;
    } else {
        s = static_cast<float>(std::sin(double(x)));
        c = static_cast<float>(std::cos(double(x)));
    }
}

} // namespace

// 0xc63510 (lane 261111k)
float *QuatFromAxisAngle(float *out, const float *axis, int32_t stride, float angle)
{
    float s, c;
    SinCos87(MulS(angle, 0.5f), s, c);
    const float x = axis[0], y = axis[stride], z = axis[stride * 2];
    const float sum = AddS(AddS(MulS(x, x), MulS(y, y)), MulS(z, z));
    // cvtps2pd; sqrtsd (the CRT's sqrt only below zero, which a sum of squares never is); 1.0 / it in double.
    const double length = _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(double(sum))));
    const float scale = MulS(static_cast<float>(_mm_cvtsd_f64(_mm_div_sd(_mm_set_sd(1.0), _mm_set_sd(length)))), s);
    out[0] = MulS(x, scale);
    out[1] = MulS(y, scale);
    out[3] = c;
    out[2] = MulS(z, scale);
    return out;
}

// 0xc63b20
void QuatToMatrix3(float *m, int32_t stride, const float *q)
{
    const float x = q[0], y = q[1], w = q[3], z = q[2];
    const float xx = MulS(x, x), wx = MulS(w, x), yy = MulS(y, y), zz = MulS(z, z), zx = MulS(z, x), zy = MulS(z, y);
    const float ww = MulS(w, w), yx = MulS(y, x), wz = MulS(w, z), wy = MulS(w, y);
    m[0] = SubS(SubS(AddS(ww, xx), yy), zz);
    const float a = AddS(wz, yx), b = SubS(yx, wz);
    m[stride] = AddS(a, a);
    const float c = SubS(zx, wy), d = AddS(wy, zx);
    m[stride * 2] = AddS(c, c);
    m[1] = AddS(b, b);
    const float e = SubS(ww, xx);
    m[stride + 1] = SubS(AddS(e, yy), zz);
    const float f = AddS(zy, wx), g = SubS(zy, wx);
    m[stride * 2 + 1] = AddS(f, f);
    m[2] = AddS(d, d);
    m[stride + 2] = AddS(g, g);
    m[stride * 2 + 2] = AddS(SubS(e, yy), zz);
}

// 0xc63c50
float *Matrix3ToEuler(float *out, const float *m, uint32_t order)
{
    const uint32_t i = order & 0xff, j = (order >> 8) & 0xff, h = (order >> 16) & 3;
    out[3] = std::bit_cast<float>(order);
    float last = 0.0f;
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    float first;
    if ((order & 3) == h) { // a repeated axis (i, j, i): k from bits 20-21
        const uint32_t k = (order >> 20) & 3;
        const float a = m[3 * i + j], b = m[3 * i + k];
        const float s = Root(AddS(MulS(b, b), MulS(a, a)));
        out[1] = Atan2(s, m[4 * i]);
        if (kEulerEpsilon >= std::fabs(s)) {
            first = Atan2(Negated(m[3 * j + k]), m[4 * j]);
        } else {
            first = Atan2(m[3 * i + j], m[3 * i + k]);
            out[0] = first;
            last = Atan2(m[3 * j + i], Negated(m[3 * k + i]));
        }
    } else { // three axes (i, j, k = h)
        const uint32_t k = h;
        const float a = m[3 * j + i], b = m[4 * i];
        const float c = Root(AddS(MulS(a, a), MulS(b, b)));
        out[1] = Atan2(Negated(m[3 * k + i]), c);
        if (kEulerEpsilon >= std::fabs(c)) {
            last = 0.0f;
            first = Atan2(Negated(m[3 * j + k]), m[4 * j]);
        } else {
            first = Atan2(m[3 * k + j], m[4 * k]);
            out[0] = first;
            last = Atan2(m[3 * j + i], m[4 * i]);
        }
    }
    // The sign: the order's top byte, read back from out.
    const float sign = static_cast<float>(reinterpret_cast<const int8_t *>(out)[15]);
    out[0] = MulS(sign, first);
    out[1] = MulS(sign, out[1]);
    out[2] = MulS(sign, last);
    return out;
}

// 0xc63af0
float *QuatToEuler(float *out, const float *q, uint32_t order)
{
    float m[9];
    QuatToMatrix3(m, 3, q);
    Matrix3ToEuler(out, m, order);
    return out;
}

namespace {

// 1.0 / the double root of a float, rounded to float (cvtps2pd; sqrtsd; divsd from 1.0; cvtpd2ps). The CRT's sqrt
// below zero is never reached: the callers pass sums of squares.
float InverseRoot(float v)
{
    const __m128d root = _mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(double(v)));
    return static_cast<float>(_mm_cvtsd_f64(_mm_div_sd(_mm_set_sd(1.0), root)));
}

} // namespace

// 0xc63790 (lane 261111l)
float *QuatSlerp(float *out, float t, const float *a, const float *b)
{
    const float b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3];
    const float a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
    const float dot = AddS(AddS(AddS(MulS(b0, a0), MulS(b1, a1)), MulS(b2, a2)), MulS(b3, a3));
    const int32_t sign = std::bit_cast<int32_t>(dot) >= 0 ? 1 : -1;
    const float absDot = std::bit_cast<float>(std::bit_cast<uint32_t>(dot) & 0x7fffffffu);
    constexpr float kSlerpLimit = std::bit_cast<float>(0x3f733333u); // 0.95f, .rdata 0xe64e88
    float r0, r1, r2, r3;
    if (kSlerpLimit > absDot) {
        const float c2 = SubS(b2, MulS(a2, dot)), c3 = SubS(b3, MulS(a3, dot));
        const float c0 = SubS(b0, MulS(a0, dot)), c1 = SubS(b1, MulS(a1, dot));
        // |dot| < 0.95 here, so 1 - dot * dot is positive and both atan2 operands are finite.
        const double s = _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(double(SubS(1.0f, MulS(dot, dot))))));
        const double angle = std::atan2(s, double(absDot));
        float sinv, cosv;
        SinCos87(static_cast<float>(_mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(angle), _mm_set_sd(double(t))))), sinv, cosv);
        const float sc = MulS(static_cast<float>(sign), cosv);
        const float sum = AddS(AddS(AddS(MulS(c1, c1), MulS(c0, c0)), MulS(c2, c2)), MulS(c3, c3));
        const float k = MulS(InverseRoot(sum), sinv);
        r3 = AddS(MulS(sc, a3), MulS(k, c3));
        r2 = AddS(MulS(k, c2), MulS(sc, a2));
        r1 = AddS(MulS(k, c1), MulS(sc, a1));
        r0 = AddS(MulS(k, c0), MulS(sc, a0));
    } else {
        const float u = MulS(SubS(1.0f, t), static_cast<float>(sign));
        const float n1 = AddS(MulS(a1, u), MulS(b1, t));
        const float n0 = AddS(MulS(a0, u), MulS(b0, t));
        const float n2 = AddS(MulS(a2, u), MulS(b2, t));
        const float n3 = AddS(MulS(a3, u), MulS(b3, t));
        const float r = InverseRoot(AddS(AddS(AddS(MulS(n1, n1), MulS(n0, n0)), MulS(n2, n2)), MulS(n3, n3)));
        r3 = MulS(r, n3);
        r2 = MulS(r, n2);
        r1 = MulS(r, n1);
        r0 = MulS(r, n0);
    }
    out[0] = r0;
    out[1] = r1;
    out[2] = r2;
    out[3] = r3;
    return out;
}

// 0xc64040 (lane 261111l)
void Matrix3RotationX(float *m, int32_t stride, float angle)
{
    float s, c;
    SinCos87(angle, s, c);
    m[0] = 1.0f;
    m[stride] = 0.0f;
    m[stride * 2] = 0.0f;
    m[1] = 0.0f;
    m[stride + 1] = c;
    m[stride * 2 + 1] = s;
    m[2] = 0.0f;
    m[stride + 2] = Negated(s);
    m[stride * 2 + 2] = c;
}

// 0xc640c0 (lane 261111l)
void Matrix3RotationY(float *m, int32_t stride, float angle)
{
    float s, c;
    SinCos87(angle, s, c);
    m[0] = c;
    m[stride] = 0.0f;
    m[stride * 2] = Negated(s);
    m[1] = 0.0f;
    m[stride + 1] = 1.0f;
    m[stride * 2 + 1] = 0.0f;
    m[2] = s;
    m[stride + 2] = 0.0f;
    m[stride * 2 + 2] = c;
}

// 0xc64140 (lane 261111l)
void Matrix3Multiply(float *out, const float *a, const float *b)
{
    float x[9], y[9];
    std::memcpy(x, a, sizeof x);
    std::memcpy(y, b, sizeof y);
    // Each sum starts from +0 (a real add: -0 products give +0) and keeps the original's operand order per product.
    auto sum = [](float p0, float p1, float p2) { return AddS(AddS(AddS(p0, 0.0f), p1), p2); };
    float r[9];
    r[8] = sum(MulS(y[6], x[2]), MulS(y[7], x[5]), MulS(y[8], x[8]));
    r[7] = sum(MulS(x[1], y[6]), MulS(x[4], y[7]), MulS(y[8], x[7]));
    r[6] = sum(MulS(x[0], y[6]), MulS(y[7], x[3]), MulS(x[6], y[8]));
    r[5] = sum(MulS(x[2], y[3]), MulS(x[5], y[4]), MulS(x[8], y[5]));
    r[4] = sum(MulS(x[1], y[3]), MulS(x[4], y[4]), MulS(y[5], x[7]));
    r[3] = sum(MulS(x[0], y[3]), MulS(y[4], x[3]), MulS(x[6], y[5]));
    r[2] = sum(MulS(y[0], x[2]), MulS(x[5], y[1]), MulS(x[8], y[2]));
    r[1] = sum(MulS(y[0], x[1]), MulS(x[4], y[1]), MulS(x[7], y[2]));
    r[0] = sum(MulS(y[0], x[0]), MulS(y[1], x[3]), MulS(x[6], y[2]));
    for (int i = 8; i >= 0; --i) out[i] = r[i];
}

namespace {

// The .data table 0xc643a0 reads (0x10859e8, 24 words, written by nothing): (2^i - 1) * 2^-22, exact in float.
constexpr float ToleranceEntry(int i) { return static_cast<float>(double((1u << i) - 1u) / 4194304.0); }
constexpr float kTolerances[24] = {
    ToleranceEntry(0),  ToleranceEntry(1),  ToleranceEntry(2),  ToleranceEntry(3),  ToleranceEntry(4),
    ToleranceEntry(5),  ToleranceEntry(6),  ToleranceEntry(7),  ToleranceEntry(8),  ToleranceEntry(9),
    ToleranceEntry(10), ToleranceEntry(11), ToleranceEntry(12), ToleranceEntry(13), ToleranceEntry(14),
    ToleranceEntry(15), ToleranceEntry(16), ToleranceEntry(17), ToleranceEntry(18), ToleranceEntry(19),
    ToleranceEntry(20), ToleranceEntry(21), ToleranceEntry(22), ToleranceEntry(23)};
static_assert(std::bit_cast<uint32_t>(kTolerances[8]) == 0x387f0000u);
static_assert(std::bit_cast<uint32_t>(kTolerances[23]) == 0x3ffffffeu);

} // namespace

// 0xc643a0 (lane 261111l)
float ToleranceForBits(int32_t bits) { return kTolerances[bits]; }

// 0xae43c0
bool ClampLookAngles(const float *in, float *out)
{
    // The copy as the original makes it (the first two words read together, then the third), so an `out` overlapping
    // `in` ends up the same; the limits then test what `in` holds after the copy.
    uint32_t first[2];
    std::memcpy(first, in, sizeof first);
    std::memcpy(out, first, sizeof first);
    uint32_t third;
    std::memcpy(&third, in + 2, sizeof third);
    std::memcpy(out + 2, &third, sizeof third);
    bool held = false;
    const float pitch = in[0];
    if (kPitchLow > pitch) {
        out[0] = kPitchLow;
        held = true;
    } else if (pitch > kPitchHigh) {
        out[0] = kPitchHigh;
        held = true;
    }
    const float yaw = in[1];
    if (-kYawLimit > yaw) {
        out[1] = -kYawLimit;
        held = true;
    } else if (yaw > kYawLimit) {
        out[1] = kYawLimit;
        held = true;
    }
    const float roll = in[2];
    if (-kRollLimit > roll) {
        out[2] = -kRollLimit;
        return true;
    }
    if (roll > kRollLimit) {
        out[2] = kRollLimit;
        return true;
    }
    return held;
}

// 0xae4780
void LookAngles(bool yUp, const float *d, float *out)
{
    const float xx = MulS(d[0], d[0]);
    if (yUp) {
        const float length = Root(AddS(xx, MulS(d[2], d[2])));
        out[0] = Atan2(Negated(d[1]), length);
        out[1] = Atan2(d[0], d[2]); // read again: `out` may be `d`
    } else {
        const float length = Root(AddS(xx, MulS(d[1], d[1])));
        out[0] = Atan2(Negated(d[2]), length);
        out[1] = Atan2(Negated(d[0]), d[1]);
    }
    out[2] = 0.0f;
}

// 0xae4490
bool CanLookAt(void *subject, cICompositionTreeNode *frame, const float *point)
{
    // receiver: the look-at subject (slots above).
    const bool able = VirtualCall<bool(__thiscall *)(void *), kSubjectCanLook>(subject);
    const bool yUp = Slot77IsNotDefault(subject);
    cICompositionTreeNode *const head =
        VirtualCall<cICompositionTreeNode *(__thiscall *)(void *, uint32_t), kSubjectNode>(subject, kHeadNodeId);
    cITransformNode *parent = nullptr, *grandparent = nullptr;
    bool canLook = false;
    if (able && FindNeck(head, parent, grandparent)) {
        // The neck: the grandparent's world transform moved to the parent's position and turned by the subject's turn.
        cRigidTransform neck = grandparent->GetWorldTransform();
        const cRigidTransform local = parent->GetLocalTransform();
        nGZCompGeom::Vec3 at{local.mValues[0], local.mValues[1], local.mValues[2]};
        Rigid(&neck)->TransformPoint(&at);
        std::memcpy(neck.mValues, &at, sizeof at);
        // receiver: the look-at subject.
        const uint8_t *turnHolder = VirtualCall<const uint8_t *(__thiscall *)(void *), kSubjectTurn>(subject);
        float turn[4], turned[4];
        std::memcpy(turn, turnHolder + kTurnOffset, sizeof turn);
        QuatMultiply(turned, neck.mValues + 3, turn);
        std::memcpy(neck.mValues + 3, turned, sizeof turned);
        cRigidTransform inverse = neck;
        Rigid(&inverse)->Invert();

        // The point in the neck's frame.
        cRigidTransform target = frame->GetWorldTransform();
        nGZCompGeom::Vec3 p{point[0], point[1], point[2]};
        Rigid(&target)->TransformPoint(&p);
        std::memcpy(target.mValues, &p, sizeof p);
        Rigid(&target)->Compose(Rigid(&inverse));

        float angles[3], held[3];
        LookAngles(yUp, target.mValues, angles);
        canLook = !ClampLookAngles(angles, held);
    }
    if (grandparent) grandparent->Release();
    if (parent) parent->Release();
    return canLook;
}

// 0xae75c0
cITransformNode *FindNodeByNameConst(const char *name, cICompositionTreeNode *node, int32_t skipFlag)
{
    cITransformNode *held = nullptr;
    cITransformNode *found = nullptr;
    const char *tag;
    if (node->QueryInterface(cITransformNode::kIid, reinterpret_cast<void **>(&held)) &&
        (tag = GetNodeTag(node)) != nullptr && _stricmp(tag, name) == 0) {
        found = held;
    } else {
        const cChildRange children = node->GetChildrenConst();
        for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
             child = child->GetNextSiblingConst()) {
            if (HasFlag(child, skipFlag)) continue;
            if ((found = FindNodeByNameConst(name, child, skipFlag)) != nullptr) break;
        }
    }
    if (held) held->Release();
    return found;
}

// 0xae76f0
cITransformNode *FindNodeByTagCrc(uint32_t crc, cICompositionTreeNode *node, int32_t skipFlag)
{
    cITransformNode *held = nullptr;
    cITransformNode *found = nullptr;
    bool match = false;
    if (node->QueryInterface(cITransformNode::kIid, reinterpret_cast<void **>(&held))) {
        cIStringPool *const pool = static_cast<cIStringPool *>(GetCachedObjectB());
        match = pool->GetCrc(node->GetTagHandle()) == crc;
    }
    if (match) {
        found = held;
    } else {
        const cChildRange children = node->GetChildren();
        for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
            if (HasFlag(child, skipFlag)) continue;
            if ((found = FindNodeByTagCrc(crc, child, skipFlag)) != nullptr) break;
        }
    }
    if (held) held->Release();
    return found;
}

// 0xae37c0
bool FindIKTargetNode(cICompositionTreeNode *self, void *rig, void *target, uint32_t *id, cICompositionTreeNode **node,
                      void **bone)
{
    using Word = uint32_t(__thiscall *)(void *);
    *node = nullptr;
    *bone = nullptr;
    // receiver: the IK target.
    const uint32_t kind = VirtualCall<Word, kTargetKind>(target);
    if (kind == kKindRigNode) {
        // receiver: the rig.
        if (!VirtualCall<Word, kRigHasNode>(rig)) return false;
        *node = VirtualCall<cICompositionTreeNode *(__thiscall *)(void *, uint32_t), kRigNode>(rig, 0);
    } else if (kind == kKindSelf) {
        *node = self;
    }
    if (!*node) return true;

    void *const rigBase = VirtualBase(rig, 2); // vbtable entry 2: the second virtual base
    // receiver: the rig's second virtual base.
    const bool altId = VirtualCall<bool(__thiscall *)(void *), kRigBaseUsesAltId>(rigBase);
    // receiver: the IK target.
    auto nameId = [&] { return altId ? VirtualCall<Word, kTargetAltNameId>(target) : VirtualCall<Word, kTargetNameId>(target); };
    const uint32_t first = nameId();
    if (first == 0) {
        (*node)->FindExtension(kBoneDataIid, bone, nullptr);
        return *node != nullptr;
    }
    if (first == kRootTargetId) {
        *id = first;
        (*node)->FindExtension(kBoneDataIid, bone, nullptr);
        return true;
    }
    // (The original also makes an empty cRZString here and destroys it unused: nothing to see from outside.)
    *id = nameId(); // asked again
    if (void *const face = AnimatedFaceOf(*node)) {
        // receiver: the animated face (sg/SGUtilities.h, the choreography section): the first id, not the stored one.
        *node = VirtualCall<cICompositionTreeNode *(__thiscall *)(void *, uint32_t, void **), kAnimatedFindTarget>(
            face, first, bone);
        return *node != nullptr;
    }
    // receiver: the rig's second virtual base, then the object it answers.
    void *const settings = VirtualCall<void *(__thiscall *)(void *), kRigBaseSettings>(rigBase);
    const int32_t skipFlag = VirtualCall<int32_t(__thiscall *)(void *), kSettingsSkipFlag>(settings);
    *node = FindNodeByTagCrc(*id, *node, skipFlag);
    if (*node) (*node)->FindExtension(kBoneDataIid, bone, nullptr);
    return *node != nullptr;
}

// 0xae3fb0
bool CanFindIKTargets(void *subject, cICompositionTreeNode *other, void *chains, bool alt, bool *otherMissing)
{
    using Word = uint32_t(__thiscall *)(void *);
    using At = void *(__thiscall *)(void *, uint32_t);
    // receiver: the subject's first virtual base, then the object it answers (asked for the model node).
    void *const holder = VirtualCall<void *(__thiscall *)(void *), kSubjectModel>(VirtualBase(subject, 1));
    cICompositionTreeNode *model = nullptr;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(holder, cICompositionTreeNode::kIid,
                                                                    reinterpret_cast<void **>(&model));
    void *const face = other ? AnimatedFaceOf(other) : nullptr;

    const auto check = [&]() -> bool {
        // receivers: `chains`, each chain, each link, each target (slots in the anonymous namespace).
        const uint32_t chainCount = VirtualCall<Word, kChainsCount>(chains);
        for (uint32_t i = 0; i < chainCount; ++i) {
            void *const chain = VirtualCall<At, kChainsChain>(chains, i);
            const uint32_t linkCount = VirtualCall<Word, kChainLinkCount>(chain);
            for (uint32_t j = 0; j < linkCount; ++j) {
                void *const link = VirtualCall<At, kChainLink>(chain, j);
                const uint32_t at = alt ? VirtualCall<Word, kLinkAltNodeId>(link) : VirtualCall<Word, kLinkNodeId>(link);
                // receiver: the subject.
                if (at != 0 && !VirtualCall<At, kSubjectNode>(subject, at)) {
                    *otherMissing = false;
                    return false;
                }
                const uint32_t targetCount = VirtualCall<Word, kLinkTargetCount>(link);
                for (uint32_t k = 0; k < targetCount; ++k) {
                    void *const target = VirtualCall<At, kLinkTarget>(link, k);
                    const uint32_t kind = VirtualCall<Word, kTargetKind>(target);
                    void *finder; // answers slot 49 (id): the animated face or the subject
                    if (kind == kKindOther) {
                        if (!other) {
                            *otherMissing = true;
                            return false;
                        }
                        finder = face;
                    } else if (kind == kKindSelf) {
                        if (!model) continue;
                        finder = subject;
                    } else {
                        continue;
                    }
                    const uint32_t id =
                        alt ? VirtualCall<Word, kTargetAltNameId>(target) : VirtualCall<Word, kTargetNameId>(target);
                    if (id == 0) continue;
                    if (id == kRootTargetId) return true;
                    void *found;
                    if (finder) {
                        found = VirtualCall<At, kAnimatedFindNode>(finder, id);
                    } else {
                        // Only `other` with no animated face: by the target's name.
                        using Text = const char *(__thiscall *)(void *);
                        // ORIGINAL-BUG: a null name is copied all the same (a null fault in the copy).
                        const std::string name(alt ? VirtualCall<Text, kTargetAltName>(target)
                                                   : VirtualCall<Text, kTargetName>(target));
                        // receiver: the subject.
                        const int32_t skipFlag = VirtualCall<int32_t(__thiscall *)(void *), kSubjectSkipFlag>(subject);
                        found = FindNodeByNameConst(name.c_str(), other, skipFlag);
                    }
                    if (!found) {
                        *otherMissing = kind == kKindOther;
                        return false;
                    }
                }
            }
        }
        return true;
    };
    const bool result = check();
    if (model) model->Release();
    return result;
}

// 0xae2e70
void ShowPlayingAnimations(cICompositionTreeNode *node, void *display)
{
    void *queue = nullptr;
    if (node->FindExtension(kQueueIid, &queue, nullptr)) {
        ClearNodeTextLabels(node, static_cast<const char *>(display));
        const uint32_t tagColour = kLabelColour;
        AddNodeTextLabel(node, static_cast<const char *>(display), &tagColour, GetNodeTag(node));
        // receiver: the animated face (cIChoreoQueue), then each entry, its animation and the animation's resource.
        const int32_t count = VirtualCall<int32_t(__thiscall *)(void *), kQueueCount>(queue);
        for (int32_t i = 0; i < count; ++i) {
            void *const entry = VirtualCall<void *(__thiscall *)(void *, int32_t), kQueueEntry>(queue, i);
            if (!entry) continue;
            AddRefThroughBase(entry);
            if (VirtualCall<bool(__thiscall *)(void *), kEntryActive>(entry) &&
                !VirtualCall<bool(__thiscall *)(void *), kEntryDone>(entry)) {
                void *animation = nullptr;
                if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(VirtualBase(entry, 1), kAnimationIid,
                                                                                  &animation)) {
                    void *const resource =
                        VirtualCall<void *(__thiscall *)(void *), kAnimationResource>(VirtualBase(animation, 2));
                    const char *const name =
                        resource ? VirtualCall<const char *(__thiscall *)(void *), kResourceName>(VirtualBase(resource, 1))
                                 : nullptr;
                    if (name && *name) {
                        const uint32_t colour = kLabelColour;
                        AddNodeTextLabel(node, static_cast<const char *>(display), &colour, name);
                    }
                }
                if (animation) ReleaseThroughBase(animation);
            }
            ReleaseThroughBase(entry);
        }
    }
    if (queue) ReleaseThroughBase(queue);
}

// 0xae6290
bool MakeIKTargetsFromNames(void *animatable, void **out)
{
    using Object = void *(__thiscall *)(void *);
    using Step = cICompositionTreeNode *(__thiscall *)(void *, void **);
    CreateObject(kIkSetClass, kIkSetIid, out);
    void *list = nullptr;
    CreateObject(kTargetListClass, kTargetListIid, &list);
    // ORIGINAL-BUG: neither creation is checked: a failed one faults on the calls that follow.
    RawSlot<kSetList>(*out, list); // receiver: the set (then the list, then the set)
    RawSlot<kListSlot9>(list, 1u);
    RawSlot<kSetMode3>(*out, 0u);
    RawSlot<kSetMode14>(*out, 2u);

    bool result = false;
    void *model = nullptr;
    // receiver: the subject's first virtual base (asked twice), then the holder it answers.
    void *const subjectBase = VirtualBase(animatable, 1);
    if (VirtualCall<Object, kSubjectModel>(subjectBase)) {
        void *const holder = VirtualCall<Object, kSubjectModel>(subjectBase);
        if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(holder, kModelIid, &model)) {
            void *rootSpace = nullptr;
            CreateObject(kRootSpaceClass, kRootSpaceIid, &rootSpace);
            // receiver: the subject (its own slots: the root, then the node walk).
            cICompositionTreeNode *const root = VirtualCall<cICompositionTreeNode *(__thiscall *)(void *),
                                                            kAnimatableRoot>(animatable);
            void *boneSpace = nullptr;
            CreateObject(kBoneSpaceClass, kBoneSpaceIid, &boneSpace);
            void *cursor; // the walk's parent cell: an object whose slot 9 answers the parent's rotation, or null
            for (cICompositionTreeNode *node = VirtualCall<Step, kAnimatableFirst>(animatable, &cursor); node;
                 node = VirtualCall<Step, kAnimatableNext>(animatable, &cursor)) {
                const char *const tag = GetNodeTag(node);
                if (!tag) continue;
                const size_t length = std::strlen(tag);
                if (length == 0 || SkippedName(tag, length)) continue;

                void *target = nullptr;
                CreateObject(kTargetClass, kTargetIid, &target);
                // receiver: the new target. ORIGINAL-BUG: a failed creation is called through null.
                using Space = uint32_t(__thiscall *)(void *, void *, uint32_t);
                float values[3];
                if (node == root) {
                    VirtualCall<Space, kTargetSpace>(target, FaceAtZero(rootSpace), kRootSpaceKind);
                    cRigidTransform local = node->GetLocalTransform();
                    std::memcpy(values, local.Translation(), sizeof values);
                } else {
                    VirtualCall<Space, kTargetSpace>(target, FaceAtZero(boneSpace), kBoneSpaceKind);
                    cRigidTransform local = node->GetLocalTransform();
                    float rotation[4];
                    std::memcpy(rotation, local.mValues + 3, sizeof rotation);
                    if (cursor) {
                        // receiver: the walk's parent object.
                        const float *const parent = VirtualCall<const float *(__thiscall *)(void *), kCursorRotation>(cursor);
                        float inverse[4];
                        QuatInverse(inverse, parent);
                        QuatMultiply(rotation, inverse, rotation);
                    }
                    float euler[3];
                    QuatToEuler(euler, rotation, kEulerOrder);
                    for (int i = 0; i < 3; ++i) values[i] = MulS(euler[i], kDegreesPerRadian);
                }
                RawSlot<kTargetTag>(target, tag);
                for (uint32_t axis = 0; axis < 3; ++axis) RawSlot<kTargetAxis>(target, 1u, axis);
                for (uint32_t axis = 0; axis < 3; ++axis)
                    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, float, uint32_t, uint32_t, uint32_t, uint32_t),
                                kTargetValue>(target, 0u, values[axis], 0u, 0u, axis, 1u);
                RawSlot<kTargetSlot39>(target, 1u);
                RawSlot<kTargetSlot12>(target);
                RawSlot<kListAdd>(list, target); // receiver: the list
                if (target) RawSlot<2>(target);  // Release
            }
            result = RawSlot<kListCount>(list) != 0; // receiver: the list
            if (boneSpace) ReleaseAtZero(boneSpace);
            if (rootSpace) ReleaseAtZero(rootSpace);
        }
    }
    if (model) RawSlot<2>(model); // Release (the model's own vtable)
    if (list) RawSlot<2>(list);
    return result;
}

} // namespace nGZSceneGraph
