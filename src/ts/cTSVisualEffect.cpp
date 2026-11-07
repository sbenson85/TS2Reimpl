#include "ts/cTSVisualEffect.h"

#include "game/original.h"
#include "compgeom/CompGeom.h"
#include "compgeom/Matrix4.h"
#include "compgeom/SeparatingAxes.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/cRZMessage2Standard.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <new>

using fx::EffectsTransform;

namespace {

// Description flag bits.
constexpr uint32_t kFlagStopAtOnce = 0x10;
constexpr uint32_t kFlagNoEvents = 0x40;
constexpr uint32_t kFlagSound = 0x4000;

constexpr uint32_t kEffectFlagSuspended = 1; // the effect's flags (+0x44)
constexpr uint32_t kEffectFlagHidden = 2;
constexpr uint32_t kEffectFlagIndexStep = 4;

constexpr uint32_t kChildFlagNoEvents = 0x20; // a child's flags (+0x44)
constexpr uint32_t kChildFlagIndexes = 8; // a child's flags: takes event indexes

constexpr uint32_t kCameraIid = 0xe51c10f5; // cITransformNode: the camera a resource of kind 2 is
constexpr uint32_t kStringIid = 0x89b7dc8;  // cIGZString: the texture's name, a resource of kind 0

// The camera node's slots the effect calls (the node is a cITransformNode; the numbers are the vtable's) and the manager's.
constexpr int kNodeHasParent = 34; // 0x88: non-zero when the node is in a tree
constexpr int kNodeWorld = 54;     // 0xd8 (out): the world placement (seven floats: translation, rotation x y z w), also returned
constexpr int kNodeMatrix = 139;   // 0x22c (0): an object with the node's 4x4 world matrix at +0x74
constexpr int kNodeRigid = 140;    // 0x230 (out, 0): the world placement again, written to `out` and returned

// The description's flag bits Update and the follow read.
constexpr uint32_t kDescBillboard = 1;       // faces the camera (about the vertical, or aimed when kDescAim)
constexpr uint32_t kDescAim = 2;
constexpr uint32_t kDescCamera = 4;          // follows the camera (and takes none from a resource)
constexpr uint32_t kDescCompose = 0x20;
constexpr uint32_t kDescUnitScale = 0x100;
constexpr uint32_t kDescUnitRotation = 0x200;
constexpr uint32_t kDescFlatten = 0x400;
constexpr uint32_t kDescFrozen = 0x2000;     // a running, updated effect is not moved again
constexpr uint32_t kDescFollowCamera = 0x8000; // the placement eased with the distance to the camera

// The cached camera placement differs from the camera's: the original's integer compare of the float bits (each number made
// two's complement, the difference's top bits tested), so values within a few ulps count as the same.
bool Differs(uint32_t a, uint32_t b)
{
    const uint32_t signA = static_cast<uint32_t>(static_cast<int32_t>(a) >> 31);
    const uint32_t signB = static_cast<uint32_t>(static_cast<int32_t>(b) >> 31);
    const uint32_t d = ((signA >> 1) ^ a) - ((signB >> 1) ^ b) + signB - signA;
    return (((static_cast<int32_t>(d) >> 26) ^ d) & 0xffffffe0u) != 0;
}
bool Differs(float a, float b) { return Differs(std::bit_cast<uint32_t>(a), std::bit_cast<uint32_t>(b)); }

// 0xbe8f20: the rotation back to the identity (the flag cleared, the version counted); scale and translation stay.
void ResetRotation(EffectsTransform &t)
{
    t.flags &= ~EffectsTransform::kRotated;
    ++t.Version();
    t.m[0][0] = 1.0f;
    t.m[0][1] = 0.0f;
    t.m[0][2] = 0.0f;
    t.m[1][0] = 0.0f;
    t.m[1][1] = 1.0f;
    t.m[1][2] = 0.0f;
    t.m[2][0] = 0.0f;
    t.m[2][1] = 0.0f;
    t.m[2][2] = 1.0f;
}

// 0xbe92d0: the placement changed so that it turns about `p` (the point stays where it was): the translation moves by p
// less the rotated, scaled p.
void PivotAdjust(EffectsTransform &t, const fx::Vector3 &p)
{
    const float s = t.scale;
    const float a = p.x * s;
    const float b = p.y * s;
    const float c = p.z * s;
    t.translation.x = (p.x - ((b * t.m[0][1] + a * t.m[0][0]) + c * t.m[0][2])) + t.translation.x;
    t.flags |= EffectsTransform::kTranslated;
    ++t.Version();
    t.translation.y = (p.y - ((t.m[1][1] * b + t.m[1][0] * a) + t.m[1][2] * c)) + t.translation.y;
    t.translation.z = (p.z - ((t.m[2][0] * a + t.m[2][1] * b) + t.m[2][2] * c)) + t.translation.z;
}

// 0xbe93f0: `t` followed by `s`'s placement inside it: s's translation carried through t's rotation and scale and added, the
// rotations multiplied (s's first), the scales multiplied, the flags joined.
void Compose(EffectsTransform &t, const EffectsTransform &s)
{
    const float scale = t.scale;
    const float qx = s.translation.x * scale;
    const float qz = s.translation.z * scale;
    const float qy = s.translation.y * scale;
    t.translation.x = qy * t.m[0][1] + qx * t.m[0][0] + qz * t.m[0][2] + t.translation.x;
    t.translation.y = t.m[1][1] * qy + t.m[1][0] * qx + t.m[1][2] * qz + t.translation.y;
    t.translation.z = t.m[2][0] * qx + t.m[2][1] * qy + t.m[2][2] * qz + t.translation.z;
    float product[9];
    nGZCompGeom::MatrixProduct(product, &s.m[0][0], &t.m[0][0]);
    std::memcpy(&t.m[0][0], product, sizeof(product));
    t.scale = s.scale * t.scale;
    t.flags |= s.flags;
    ++t.Version();
}

// 0xbe8f90: a copy of `source` (the version cleared; the byte at +1 not copied: left zero here, a stack leftover there) with
// `target` composed onto it.
EffectsTransform ComposeCopy(const EffectsTransform &target, const EffectsTransform &source)
{
    EffectsTransform out;
    std::memset(&out, 0, sizeof(out));
    out.flags = source.flags;
    std::memcpy(&out.m[0][0], &source.m[0][0], sizeof(out.m));
    out.scale = source.scale;
    out.translation = source.translation;
    Compose(out, target);
    return out;
}

float Root(float x) { return static_cast<float>(std::sqrt(static_cast<double>(x))); }


// An identity placement: the flags and the version cleared, the byte at +1 left alone.
void InitBlock(EffectsTransform &block)
{
    block.flags = 0;
    block.Version() = 0;
    block.m[0][0] = 1.0f;
    block.m[0][1] = 0.0f;
    block.m[0][2] = 0.0f;
    block.m[1][0] = 0.0f;
    block.m[1][1] = 1.0f;
    block.m[1][2] = 0.0f;
    block.m[2][0] = 0.0f;
    block.m[2][1] = 0.0f;
    block.m[2][2] = 1.0f;
    block.scale = 1.0f;
    block.translation = {0.0f, 0.0f, 0.0f};
}

void *FloatArgument(float value) { return reinterpret_cast<void *>(static_cast<uintptr_t>(std::bit_cast<uint32_t>(value))); }

} // namespace

cTSVisualChild::~cTSVisualChild()
{
    std::vector<int32_t>().swap(indexes);
    if (effect) effect->Release();
}

cTSVisualEffect::cTSVisualEffect()
{
    mRefs = 0;
    mUnknown2c = 0;
    mUnknown30 = 0;
    mEffectIndex = -1;
    mUnknown38 = 0;
    mUpdated = 0;
    mRunning = 0;
    mStopping = 0;
    mDescription = nullptr;
    mFlags = 0;
    mParameter48 = 0;
    mParameter4c = 0;
    InitBlock(mBlockA);
    InitBlock(mBlockB);
    InitBlock(mBlockC);
    InitBlock(mBlockD);
    std::memset(mFollow, 0, sizeof(mFollow));
    mFollow[6] = 1.0f;
    mBlendA = -1.0f;
    mBlendB = -1.0f;
}

cTSVisualEffect::~cTSVisualEffect() = default;

// ---- face 0 ----

bool cTSVisualEffect::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = static_cast<cITSVisualEffect *>(this);
    AddRef();
    return true;
}

uint32_t cTSVisualEffect::AddRef() { return ++mRefs; }

uint32_t cTSVisualEffect::Release()
{
    const uint32_t count = --mRefs;
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

void cTSVisualEffect::Slot3(uint32_t a)
{
    if (!mDescription) return;
    for (cTSVisualChild &child : mChildren) child.effect->Start(a);
    if (mParameter48 != 0 && (mDescription->flags & kFlagSound)) {
        // A message for the message server: id = parameter 48, value 0 = parameter 4c, value 1 = 1, carrying this effect; posted
        // through the server's slot 4. (The original calls an allocation failure's null object; here nothing is posted.)
        if (void *service = GetCachedMessageServer()) {
            void *memory = RZMemory::Malloc(cRZMessage2Standard::kSize);
            if (cRZMessage2Standard *message = memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr) {
                message->AddRef();
                message->SetId(mParameter48);
                message->SetValue0(mParameter4c);
                message->SetValue1(1);
                message->SetObject(static_cast<cITSVisualEffect *>(this));
                VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(service, 4)(service, message, 0);
                message->Release();
            }
        }
    }
    if (!mRunning && (mFlags & kEffectFlagIndexStep)) {
        WalkIndex(0);
        mFlags &= ~kEffectFlagIndexStep;
    }
    mUpdated = 0;
    mFlag3a = 1;
    mRunning = 1;
    mStopping = 0;
}

void cTSVisualEffect::Slot4(uint32_t a)
{
    if (!mDescription) return;
    if (a == 0 && (mDescription->flags & kFlagStopAtOnce)) a = 1;
    for (cTSVisualChild &child : mChildren) child.effect->Stop(a);
    if (a != 0) {
        mRunning = 0;
        mStopping = 0;
        return;
    }
    mStopping = 1;
}

void cTSVisualEffect::SetSuspended(bool on)
{
    if (on) mFlags |= kEffectFlagSuspended;
    else mFlags &= ~kEffectFlagSuspended;
}

void cTSVisualEffect::SetHidden(bool on)
{
    if (on != ((mFlags & kEffectFlagHidden) != 0)) static_cast<cITSComponentEffect *>(this)->SetFlagged(!on);
}

void cTSVisualEffect::UpdateInside(const EffectsTransform *parent)
{
    static_cast<cITSComponentEffect *>(this)->Update(&mBlockB, parent);
}

void cTSVisualEffect::UpdateFrom(const EffectsTransform *local)
{
    static_cast<cITSComponentEffect *>(this)->Update(local, &mBlockA);
}

void cTSVisualEffect::SetParameters(uint32_t a, uint32_t b)
{
    mParameter48 = a;
    mParameter4c = b;
}

bool cTSVisualEffect::EventAlias(uint32_t kind, const float *values, uint32_t count) { return Event(kind, values, count * 3); } // the count is of vectors: three floats each

bool cTSVisualEffect::Event(uint32_t kind, const float *values, uint32_t count)
{
    if (mDescription && (mDescription->flags & kFlagNoEvents)) return false;
    if (static_cast<int32_t>(count) < 1) return false;
    if (kind > 0x11) {
        if (kind != 0x101) return false;
        mBlendA = values[1];
        mBlendB = values[4];
        return false;
    }
    // ORIGINAL-BUG: kind 0x11 passes the test above, but the table has entries 0..0x10 only; its entry is one past the
    // vector's end (a heap overflow when the vector is full). Not exercised.
    if (mEntries.empty()) mEntries.resize(0x11);
    cTSVisualEntry &entry = mEntries[kind];
    if (entry.count == 0) {
        entry.offset = static_cast<uint16_t>(mValues.size());
        entry.count = static_cast<uint16_t>(count);
        mValues.resize(static_cast<size_t>(entry.offset) + count);
    } else if (count != entry.count) {
        return false;
    }
    std::memmove(mValues.data() + mEntries[kind].offset, values, count * sizeof(float));
    if (kind == 0xf) {
        // 1.5 * 2^23 added: the sum's low bits are the value rounded to the nearest integer.
        const float sum = values[0] + 12582912.0f;
        WalkIndex(std::bit_cast<int32_t>(sum) - 0x4b400000);
    }
    bool taken = false;
    for (cTSVisualChild &child : mChildren) {
        if ((child.flags & kChildFlagNoEvents) == 0) {
            if (child.effect->HandleEvent(kind, const_cast<float *>(values), reinterpret_cast<void *>(static_cast<uintptr_t>(count))) || taken) taken = true;
        }
    }
    return taken;
}

uint32_t *cTSVisualEffect::Values(uint32_t kind, void *, uint32_t *count)
{
    if (kind < 0x12 && !mEntries.empty()) {
        const uint16_t number = mEntries.data()[kind].count;
        if (number != 0 && number % 3 == 0) {
            if (count) *count = number / 3;
            return mValues.data() + mEntries.data()[kind].offset;
        }
    }
    return nullptr;
}

uint32_t *cTSVisualEffect::ValuesRaw(uint32_t kind, void *, uint32_t *count)
{
    if (kind < 0x12 && !mEntries.empty()) {
        const uint16_t number = mEntries.data()[kind].count;
        if (number != 0) {
            if (count) *count = number;
            return mValues.data() + mEntries.data()[kind].offset;
        }
    }
    return nullptr;
}

cIGZUnknown *cTSVisualEffect::Resource(uint32_t kind)
{
    if (kind < 5 && !mResources.empty()) return mResources.data()[kind].Get();
    return nullptr;
}

EffectsTransform *cTSVisualEffect::GetBlockA(EffectsTransform *out) { std::memcpy(out, &mBlockA, sizeof(EffectsTransform)); return out; }

EffectsTransform *cTSVisualEffect::GetBlockB(EffectsTransform *out) { std::memcpy(out, &mBlockB, sizeof(EffectsTransform)); return out; }

bool cTSVisualEffect::IsFlag0() { return (mFlags & 1) != 0; }

bool cTSVisualEffect::IsFlag1() { return (mFlags & 2) != 0; }

bool cTSVisualEffect::IsEventSet()
{
    // ORIGINAL-BUG: the value is a local that nothing writes (the call's second argument, which the getter ignores), so this
    // is false for every effect; it was meant to read event 0x10's value.
    float value = 0.0f;
    uint32_t count;
    ValuesRaw(0x10, &value, &count);
    return value != 0.0f;
}

void cTSVisualEffect::SetEvent16(bool on)
{
    float value = on ? 1.0f : 0.0f;
    Event(0x10, &value, 1);
}

// ---- face +4 ----

void cTSVisualEffect::Attach(cIGZUnknown *owner)
{
    mUnknown38 = 0;
    mUpdated = 0;
    mRunning = 0;
    mStopping = 0;
    mFlags = 0;
    mParameter48 = 0;
    mBlockA.Reset();
    mBlockB.Reset();
    mBlockC.Reset();
    mBlockD.Reset();
    std::memset(mFollow, 0, sizeof(mFollow));
    mFollow[6] = 1.0f;
    mBlendA = -1.0f;
    mOwner.Assign(static_cast<cITSEffectsManager *>(owner));
    const int32_t kind = mOwner->GetState();
    const bool dynamic = kind == 2 || kind == 3;
    mFlags = dynamic ? mFlags | 1 : mFlags & ~1u;
    mFlags = kind == 3 ? mFlags | 2 : mFlags & ~2u;
}

void cTSVisualEffect::Detach()
{
    if (mRunning) Slot4(1);
    mOwner.Reset();
    mDescription = nullptr;
    mEntries.clear();
    mValues.clear();
    mResources.clear();
    mCamera.Reset();
    mUnknown2c = 0;
    mUnknown30 = 0;
    mEffectIndex = -1;
    mName.clear();
    for (cTSVisualChild &child : mChildren) {
        if (child.effect) child.effect->Detach();
    }
    mChildren.clear();
}

void cTSVisualEffect::AddToScene(void *time, void *counters)
{
    int32_t *count = static_cast<int32_t *>(counters);
    const float t = std::bit_cast<float>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(time)));
    ++count[0];
    mFlag3a = 0;
    if (!(mFlags & kEffectFlagHidden)) Follow();
    for (cTSVisualChild &child : mChildren) {
        if (child.effect->IsRunning()) {
            child.effect->AddToScene(FloatArgument(t * child.weight), counters);
            ++count[1];
        }
    }
}

bool cTSVisualEffect::Query(void *kind, void *resource)
{
    return SetResource(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kind)), static_cast<cIGZUnknown *>(resource));
}

void cTSVisualEffect::SetFlagged(bool value)
{
    mFlags = value ? mFlags & ~kEffectFlagHidden : mFlags | kEffectFlagHidden;
    for (cTSVisualChild &child : mChildren) child.effect->SetFlagged(value);
}

void cTSVisualEffect::WalkIndex(int32_t index)
{
    for (cTSVisualChild &child : mChildren) {
        if (child.effect && (child.flags & kChildFlagIndexes) && index >= 0 &&
            static_cast<uint32_t>(index) < child.indexes.size()) {
            if (index > 0) mFlags |= kEffectFlagIndexStep;
            // No bound is checked on the way down (the tables are trusted): the effect's component, its type's table, the
            // description by the component's index.
            cTSEffectsManager *manager = static_cast<cTSEffectsManager *>(mOwner.Get());
            const EffectComponent &component =
                manager->mEffects.data()[mEffectIndex].mComponents.data()[child.indexes.data()[index]];
            cITSComponentDescription *description =
                manager->mTables.data()[component.mType - 1].mDescriptions.data()[component.mIndex].Get();
            if (description) child.effect->Slot11(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(description)));
        }
    }
}

void cTSVisualEffect::ReplayEvents()
{
    // The tables, the children and the values are read afresh each time round: what a child does with an event may change them.
    for (uint32_t kind = 0; kind < mEntries.size(); ++kind) {
        if (mEntries.data()[kind].count == 0) continue;
        for (size_t i = 0; i != mChildren.size(); ++i) {
            cTSVisualChild &child = mChildren.data()[i];
            if ((child.flags & kChildFlagNoEvents) == 0) {
                const cTSVisualEntry &entry = mEntries.data()[kind];
                child.effect->HandleEvent(kind, mValues.data() + entry.offset, reinterpret_cast<void *>(static_cast<uintptr_t>(entry.count)));
            }
        }
        if (kind == 0xf) {
            const float sum = std::bit_cast<float>(mValues.data()[mEntries.data()[0xf].offset]) + 12582912.0f;
            WalkIndex(std::bit_cast<int32_t>(sum) - 0x4b400000);
        }
    }
    for (uint32_t kind = 0; kind < mResources.size(); ++kind) {
        if (!mResources.data()[kind].Get()) continue;
        for (size_t i = 0; i != mChildren.size(); ++i) {
            cTSVisualChild &child = mChildren.data()[i];
            if ((child.flags & kChildFlagNoEvents) == 0)
                child.effect->Query(reinterpret_cast<void *>(static_cast<uintptr_t>(kind)), mResources.data()[kind].Get());
        }
    }
}

bool cTSVisualEffect::SetResource(uint32_t kind, cIGZUnknown *resource)
{
    if ((mDescription && (mDescription->flags & kFlagNoEvents)) || kind > 4) return false;
    if (kind == 2 && (!mDescription || !(mDescription->flags & kDescCamera))) {
        if (!resource) {
            mCamera.Reset();
            return true;
        }
        void *node = nullptr;
        resource->QueryInterface(kCameraIid, &node);
        cIGZUnknown *camera = static_cast<cIGZUnknown *>(node);
        mCamera.Assign(camera);
        if (camera) camera->Release();
        return true;
    }
    cIGZString *name = nullptr;
    if (kind == 0) {
        // ORIGINAL-BUG: a null resource is called through (the query helper's first act is its slot 0).
        void *text = nullptr;
        resource->QueryInterface(kStringIid, &text);
        cIGZString *string = static_cast<cIGZString *>(text);
        if (string) {
            string->AddRef();
            name = string;
            string->Release();
        }
        if (name) mTexture = nGZSceneGraph::cTextureHandle(name->ToChar());
    }
    if (mResources.empty()) mResources.resize(4);
    // ORIGINAL-BUG: kind 4 is one past the table of four (a heap overwrite when the vector is full; Resource() stops at 4 as
    // well, so the slot is never read back).
    mResources.data()[kind].Assign(resource);
    bool taken = false;
    for (cTSVisualChild &child : mChildren) {
        if (!(child.flags & kChildFlagNoEvents)) {
            if (child.effect->Query(reinterpret_cast<void *>(static_cast<uintptr_t>(kind)), resource) || taken) taken = true;
        }
    }
    if (name) name->Release();
    return taken;
}

void cTSVisualEffect::Follow()
{
    // ORIGINAL-BUG: the description is not checked (an effect made without one crashes here).
    const uint32_t flags = mDescription->flags;
    const bool needs = (flags & kDescBillboard) || (flags & kDescAim) || (flags & kDescFollowCamera) || (flags & kDescCamera);
    bool refreshed = false;
    cITSComponentEffect *face = static_cast<cITSComponentEffect *>(this);
    if (cIGZUnknown *camera = mCamera.Get()) {
        if (!VirtualSlot<int(__thiscall *)(void *)>(camera, kNodeHasParent)(camera)) {
            Slot4(1);
            return;
        }
        float placement[7];
        VirtualSlot<float *(__thiscall *)(void *, float *)>(camera, kNodeWorld)(camera, placement);
        bool moved = Differs(placement[0], mFollow[0]) || Differs(placement[1], mFollow[1]) || Differs(placement[2], mFollow[2]);
        if (!moved) {
            // The rotation moved, and not just by its sign (a quaternion and its negative are one rotation).
            const bool differs = Differs(placement[3], mFollow[3]) || Differs(placement[4], mFollow[4]) ||
                                 Differs(placement[5], mFollow[5]) || Differs(placement[6], mFollow[6]);
            if (differs) {
                const uint32_t sign = 0x80000000u;
                moved = Differs(std::bit_cast<uint32_t>(placement[3]), std::bit_cast<uint32_t>(mFollow[3]) ^ sign) ||
                        Differs(std::bit_cast<uint32_t>(placement[4]), std::bit_cast<uint32_t>(mFollow[4]) ^ sign) ||
                        Differs(std::bit_cast<uint32_t>(placement[5]), std::bit_cast<uint32_t>(mFollow[5]) ^ sign) ||
                        Differs(std::bit_cast<uint32_t>(placement[6]), std::bit_cast<uint32_t>(mFollow[6]) ^ sign);
            }
        }
        if (moved || needs) {
            refreshed = true;
            ++mBlockB.Version();
            mBlockB.scale = 1.0f;
            float matrix[9];
            reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(placement)->ToMatrix(matrix);
            std::memcpy(&mBlockB.m[0][0], matrix, sizeof(matrix));
            mBlockB.flags |= EffectsTransform::kRotated;
            ++mBlockB.Version();
            mBlockB.flags |= EffectsTransform::kTranslated;
            ++mBlockB.Version();
            mBlockB.translation = {placement[0], placement[1], placement[2]};
            face->Update(&mBlockB, &mBlockA);
        }
    }
    if (!needs && !refreshed && mBlendA < 0.0f) return;
    face->Update(&mBlockB, &mBlockA);
    if (cIGZUnknown *camera = mCamera.Get()) {
        float scratch[7];
        const float *placement = VirtualSlot<float *(__thiscall *)(void *, float *)>(camera, kNodeWorld)(camera, scratch);
        std::memcpy(mFollow, placement, sizeof(mFollow));
    }
}

void cTSVisualEffect::Update(const EffectsTransform *local, const EffectsTransform *parent)
{
    if (!mDescription) return;
    const uint32_t flags = mDescription->flags;
    if (mUpdated && mRunning && (flags & kDescFrozen)) return;
    mBlockB = *local;
    mBlockA = *parent;
    if (flags & kDescUnitScale) {
        ++mBlockB.Version();
        ++mBlockA.Version();
        mBlockB.scale = 1.0f;
        mBlockA.scale = 1.0f;
    }
    if (flags & kDescUnitRotation) {
        ResetRotation(mBlockB);
        ResetRotation(mBlockA);
    }
    mBlockD = mBlockB;
    mBlockC = mBlockA;
    if (0.0f <= mBlendA) {
        // Blended with the owner's progress: C's scale and the pivot D's translation stay fixed under it.
        const float progress = *reinterpret_cast<const float *>(static_cast<const uint8_t *>(mOwner->GetParams()) + 0xc);
        const fx::Vector3 pivot = mBlockD.translation;
        mBlockC.flags |= 1;
        ++mBlockC.Version();
        const float blend = (mBlendB - mBlendA) * progress + mBlendA;
        mBlockC.scale = blend * mBlockC.scale;
        mBlockC.flags |= EffectsTransform::kTranslated;
        ++mBlockC.Version();
        mBlockC.translation.x = (pivot.x - pivot.x * blend) + mBlockC.translation.x;
        mBlockC.translation.y = (pivot.y - pivot.y * blend) + mBlockC.translation.y;
        mBlockC.translation.z = (pivot.z - pivot.z * blend) + mBlockC.translation.z;
    }
    if (flags & kDescFollowCamera) {
        fx::Vector3 point = mBlockD.translation;
        mBlockC.Apply(&point);
        if (cIGZUnknown *camera = mOwner->GetCamera()) {
            float tmp[7];
            const float *rigid = VirtualSlot<float *(__thiscall *)(void *, float *, int)>(camera, kNodeRigid)(camera, tmp, 0);
            float inverse[7];
            std::memcpy(inverse, rigid, sizeof(inverse));
            reinterpret_cast<nGZCompGeom::RigidTransform3 *>(inverse)->Invert();
            const uint8_t *object = VirtualSlot<const uint8_t *(__thiscall *)(void *, int)>(camera, kNodeMatrix)(camera, 0);
            float view[16];
            reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(inverse)->ToMatrix4(view);
            float product[16];
            nGZCompGeom::Matrix4Multiply(product, view, reinterpret_cast<const float *>(object + 0x74));
            const float sum = ((product[5] * product[5] + product[1] * product[1]) + product[9] * product[9]) +
                              ((product[4] * product[4] + product[0] * product[0]) + product[8] * product[8]);
            const float length = Root(sum);
            const float depth = ((point.y * product[13] + point.x * product[12]) + point.z * product[14]) + product[15];
            const float distance = (length * 10.0f) / depth;
            // The distance limited to the description's range (an ease at both ends, or a hard clamp when flag bit 16).
            const float low = *reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(mDescription) + 0x14);
            const float high = *reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(mDescription) + 0x18);
            const float over = distance - low;
            float limited;
            if (!((reinterpret_cast<const uint8_t *>(mDescription))[2] & 1)) {
                const float above = (std::fabs(over) + over) * 0.5f + low;
                const float room = high - above;
                const float t = ((high - (std::fabs(room) + room) * 0.5f) - low) / (high - low);
                const float tt = t * t;
                const float ttt = tt * t;
                limited = (tt * 3.0f - (ttt + ttt)) * (high - low) + low;
            } else {
                const float above = high - ((std::fabs(over) + over) * 0.5f + low);
                limited = high - (std::fabs(above) + above) * 0.5f;
            }
            if (limited != distance) {
                EffectsTransform scale;
                InitBlock(scale);
                scale.scale = limited / distance;
                ++scale.Version();
                PivotAdjust(scale, mBlockD.translation);
                Compose(mBlockC, scale);
            }
        }
    }
    if (flags & kDescFlatten) {
        // Only the turn about the vertical is kept: the first column's horizontal part, normalised.
        const float length = Root(mBlockD.m[1][0] * mBlockD.m[1][0] + mBlockD.m[0][0] * mBlockD.m[0][0]);
        float e[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        if (0.0f < length) {
            e[1][0] = mBlockD.m[1][0] / length;
            e[0][0] = mBlockD.m[0][0] / length;
            e[0][1] = -e[1][0];
            e[1][1] = e[0][0];
        }
        mBlockD.flags |= EffectsTransform::kRotated;
        ++mBlockD.Version();
        std::memcpy(&mBlockD.m[0][0], e, sizeof(e));
    }
    if (flags & kDescCompose) {
        mBlockC = ComposeCopy(mBlockD, mBlockC);
        mBlockD.Reset();
    }
    if (flags & kDescBillboard) {
        EffectsTransform turn;
        InitBlock(turn);
        fx::Vector3 point = mBlockD.translation;
        mBlockC.Apply(&point);
        if (cIGZUnknown *camera = mOwner->GetCamera()) {
            if (!(flags & kDescAim)) {
                // Turned about the vertical to face the camera's sideways direction.
                float tmp[7];
                const float *rigid = VirtualSlot<float *(__thiscall *)(void *, float *, int)>(camera, kNodeRigid)(camera, tmp, 0);
                float v[3] = {0.0f, 1.0f, 0.0f};
                reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(rigid)->RotateVector(v);
                const float angle = static_cast<float>(std::atan2(static_cast<double>(v[1]), static_cast<double>(v[0])));
                const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
                const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
                float (&m)[3][3] = turn.m;
                const float m01 = m[0][1], m11 = m[1][1], m21 = m[2][1];
                const float ns = -s;
                m[1][1] = c * m11 + m[1][0] * ns;
                m[2][1] = c * m21 + m[2][0] * ns;
                m[0][1] = c * m01 + m[0][0] * ns;
                m[0][0] = m[0][0] * c + s * m01;
                m[1][0] = m[1][0] * c + s * m11;
                m[2][0] = s * m21 + m[2][0] * c;
            } else {
                // Aimed at the camera: the first column points at it, the others from the cross with the camera's forward.
                float out[7];
                VirtualSlot<float *(__thiscall *)(void *, float *, int)>(camera, kNodeRigid)(camera, out, 0);
                const float cx = out[0], cy = out[1], cz = out[2];
                float v[3] = {0.0f, 0.0f, 1.0f};
                reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(out)->RotateVector(v);
                float dx = cx - point.x;
                float dy = cy - point.y;
                float dz = cz - point.z;
                const double squared = static_cast<double>((dy * dy + dx * dx) + dz * dz);
                if (!(squared <= 1e-06)) {
                    const float length = static_cast<float>(std::sqrt(squared));
                    dx = dx / length;
                    dy = dy / length;
                    dz = dz / length;
                }
                const float a = dz * v[1] - dy * v[2];
                const float b = dx * v[2] - dz * v[0];
                const float d = dy * v[0] - dx * v[1];
                const float length = Root((a * a + b * b) + d * d);
                float r01, r02, r11, r12, r21, r22;
                if (length <= 0.0f) {
                    r01 = 1.0f;
                    r11 = 0.0f;
                    r21 = 0.0f;
                    r02 = 0.0f;
                    r12 = 1.0f;
                    r22 = 0.0f;
                } else {
                    const float na = a / length;
                    const float nb = b / length;
                    const float nd = d / length;
                    r01 = na;
                    r11 = nb;
                    r21 = nd;
                    r02 = nd * dy - dz * nb;
                    r12 = dz * na - nd * dx;
                    r22 = dx * nb - dy * na;
                }
                turn.m[0][0] = dx;
                turn.m[0][1] = r01;
                turn.m[0][2] = r02;
                turn.m[1][0] = dy;
                turn.m[1][1] = r11;
                turn.m[1][2] = r12;
                turn.m[2][0] = dz;
                turn.m[2][1] = r21;
                turn.m[2][2] = r22;
            }
            turn.flags |= EffectsTransform::kRotated;
            ++turn.Version();
        }
        PivotAdjust(turn, point);
        mBlockC = ComposeCopy(mBlockC, turn);
    }
    for (cTSVisualChild &child : mChildren) {
        EffectsTransform placed;
        std::memset(&placed, 0, sizeof(placed));
        placed.flags = mBlockD.flags;
        std::memcpy(&placed.m[0][0], &mBlockD.m[0][0], sizeof(placed.m));
        placed.scale = mBlockD.scale;
        placed.translation = mBlockD.translation;
        Compose(placed, child.placement);
        child.effect->Update(&placed, &mBlockC);
    }
    mUpdated = 1;
}

// ---- the RVA hook ----

cTSVisualEffect *__fastcall cTSVisualEffectAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSVisualEffect;
}
