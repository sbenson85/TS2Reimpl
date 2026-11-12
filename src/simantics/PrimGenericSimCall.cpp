#include "rz/RZMessageServer.h"
#include "simantics/SimGeometry.h"
#include "simantics/PrimGenericSimCall.h"

#include "game/original.h"
#include "simantics/PrimGenericSimCallOriginals.h"
#include "simantics/PrimGenericSimCallOriginalsB.h"
#include "simantics/PrimXCommon.h"
#include "misc/HexParse.h"
#include "world/WorldTileRects.h"

using primX::Call;

// SimAntics opcode 0x01, GenericSimCall: the skeleton (prologue, switch, shared helpers). The cases live in
// PrimGenericSimCall<Group>.cpp. See PrimGenericSimCall.h for the status.

uint32_t PrimGscObject::GenericSimCall(void *frame, const PrimParamView *param)
{
    ::GenericSimCall call(this, frame, param);
    return call.Run();
}

GenericSimCall::GenericSimCall(PrimGscObject *self, void *frame, const PrimParamView *param)
    : mSelf(self), mFrame(frame), mParam(param)
{
}

int16_t GenericSimCall::Arg(int32_t i)
{
    return Call<int16_t>(mSelf, 19, i);
}

void GenericSimCall::SetArg(int32_t i, uint32_t value)
{
    Call<void>(mSelf, 20, i, value);
}

void *GenericSimCall::Context()
{
    return mSelf->mContext;
}

void *GenericSimCall::ContextSlot(int slot)
{
    return Call<void *>(Context(), slot);
}

void *GenericSimCall::StackObject()
{
    // The frame's slot 12 returns the id word; the object table (kept from the prologue) finds the object.
    void *obj = Call<void *>(mL1c, 29, static_cast<int32_t>(Call<int16_t>(mFrame, 12)));
    if (!obj) Throw(0xa);
    return obj;
}

void GenericSimCall::Throw(int16_t code)
{
    throw code;
}

bool GenericSimCall::CheckAttribute(int32_t a, int32_t b)
{
    return gscorig::CheckAttribute(a, b);
}

uint32_t GenericSimCall::Run()
{
    // Prologue: four calls on the context holder, in this order; the results stay for the whole call.
    mL2c = ContextSlot(9);
    mL24 = ContextSlot(10);
    mL1c = ContextSlot(26);
    mL18c = ContextSlot(24);
    // The word at param +4 (through the identity helper 0x141120 and the +4 helper 0x18ea60), sign-extended.
    const int32_t which = static_cast<int16_t>(mParam->mWord4);
    return Dispatch(which);
}

// ---- the original functions the cases call (declared in PrimGenericSimCallOriginals.h) ----------------------------------

namespace gscorig {

bool CheckAttribute(int32_t a, int32_t b)
{
    using Fn = uint32_t(__cdecl *)(int32_t, int32_t);
    return (OriginalFunction<Fn, 0x57ee10>()(a, b) & 0xff) != 0;
}

void *CachedObject211da3()
{
    return OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
}

uint32_t ObjectFlags(void *object, uint32_t mask)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x57d860>()(object, mask);
}

void *MessageServer()
{
    return GetCachedMessageServer();
}

bool PlaceObject(void *object, void *block, void *position)
{
    using Fn = uint8_t(__cdecl *)(void *, void *, void *, float, float, int32_t, int32_t);
    return OriginalFunction<Fn, 0x7e8c60>()(object, block, position, 0.0f, 0.0f, 0, 0) != 0;
}

void ConstructList(void *bytes)
{
    OriginalFunction<void *(__thiscall *)(void *), 0x57c6d0>()(bytes);
}

void DestructList(void *bytes)
{
    OriginalFunction<void *(__thiscall *)(void *), 0x1d20f0>()(bytes);
}

void ParseGuid(void *out16, const char *text)
{
    hexparse::ParseGuidText(out16, text);
}

uint8_t CheckStackObject(void *object)
{
    return OriginalFunction<uint8_t(__cdecl *)(void *), 0x54b2d0>()(object);
}

} // namespace gscorig

namespace gscorig {

const uint8_t *NullGuid()
{
    return OriginalFunction<const uint8_t *(__cdecl *)(), 0x4f8e50>()();
}

void *TileService()
{
    return OriginalFunction<void *(__cdecl *)(), 0x212e61>()();
}

void NotchQuaternion(float *quaternion, uint32_t notch)
{
    simgeom::NotchQuaternion(quaternion, static_cast<int32_t>(notch));
}

void RotateVector(float *vector, const float *quaternion)
{
    simgeom::RotateByQuaternion(vector, quaternion);
}

const int32_t *FirstTileOfBox(int32_t *scratch, const float *box)
{
    return reinterpret_cast<const int32_t *>(worldrect::SegmentToQuadrants(reinterpret_cast<worldmath::TileQuadrant *>(scratch), box));
}

const int32_t *SecondTileOfBox(int32_t *scratch, const float *box)
{
    return reinterpret_cast<const int32_t *>(worldrect::ReversedSegmentToQuadrants(reinterpret_cast<worldmath::TileQuadrant *>(scratch), box));
}

void *ObjectOnTile(const int32_t *tile, int32_t mode)
{
    return OriginalFunction<void *(__cdecl *)(const int32_t *, int32_t), 0x7deff0>()(tile, mode);
}

bool PositionBlocked(const float *position, float radius, int32_t flagA, int32_t flagB)
{
    return (OriginalFunction<uint32_t(__cdecl *)(const float *, float, int32_t, int32_t), 0x7e7e00>()(position, radius, flagA, flagB) & 0xff) != 0;
}

bool PlaceObjectMode(void *object, void *second, void *third, int32_t mode)
{
    using Fn = uint8_t(__cdecl *)(void *, void *, void *, float, float, int32_t, int32_t);
    return OriginalFunction<Fn, 0x7e8c60>()(object, second, third, 0.0f, 0.0f, mode, 0) != 0;
}

float SineOf(float angle)
{
    return simgeom::SineOfFloat(angle);
}

float CosineOf(float angle)
{
    return simgeom::CosineOfFloat(angle);
}

} // namespace gscorig
