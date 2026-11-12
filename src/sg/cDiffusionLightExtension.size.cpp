// nTSSG::cDiffusionLightExtension's cIDiffusionLightExtension slots (the game's face vtable 0xdb6bdc; the class:
// src/sg/cDiffusionLightExtension.h). Each game slot is a vtordisp thunk into its body (`sub ecx, [ecx-4]` then the
// body, which addresses the object from the +0xb4 face); the RVAs below are the thunks', where each status entry starts.
// Floats are copied as floats (movss): NaN payloads pass through as in the original. The setters' copies keep the
// original's load order, which only shows when the argument points into the object itself.
#include "sg/cDiffusionLightExtension.h"

namespace nTSSG {

// 0 (0x2ea27b): every override's flag cleared, in the original's order; the values stay.
void cDiffusionLightExtension::ResetFlags()
{
    mHasMin = false;
    mHasMax = false;
    mHasIntensity = false;
    mHasColor = false;
    mHasIsDirectional = false;
    mHasFalloff = false;
    mHasDirectionalFalloff = false;
    mHasWallIntensityScale = false;
    mHasFloorIntensityScale = false;
    mHasInterFloorFalloff = false;
}

// ---- 1-10: an override and its flag ----

// 1 (0x2ea948): both floats loaded before either is stored.
void cDiffusionLightExtension::SetSourceMin(const float *xy)
{
    const float x = xy[0];
    const float y = xy[1];
    mMin[0] = x;
    mMin[1] = y;
    mHasMin = true;
}

// 2 (0x2ea908): as SetSourceMin.
void cDiffusionLightExtension::SetSourceMax(const float *xy)
{
    const float x = xy[0];
    const float y = xy[1];
    mMax[0] = x;
    mMax[1] = y;
    mHasMax = true;
}

// 3 (0x2ea842)
void cDiffusionLightExtension::SetSourceIntensity(float value)
{
    mIntensity = value;
    mHasIntensity = true;
}

// 4 (0x2ea749): the first two floats loaded and stored (one 8-byte move), then the third read and stored.
void cDiffusionLightExtension::SetSourceColor(const float *rgb)
{
    const float r = rgb[0];
    const float g = rgb[1];
    mColor[0] = r;
    mColor[1] = g;
    mColor[2] = rgb[2];
    mHasColor = true;
}

// 5 (0x2ea8ba): the argument's low byte stored as it is.
void cDiffusionLightExtension::SetSourceIsDirectional(bool value)
{
    mIsDirectional = value;
    mHasIsDirectional = true;
}

// 6 (0x2ea7de)
void cDiffusionLightExtension::SetSourceFalloff(float value)
{
    mFalloff = value;
    mHasFalloff = true;
}

// 7 (0x2ea79e)
void cDiffusionLightExtension::SetSourceDirectionalFalloff(float value)
{
    mDirectionalFalloff = value;
    mHasDirectionalFalloff = true;
}

// 8 (0x2ea966)
void cDiffusionLightExtension::SetSourceWallIntensityScale(float value)
{
    mWallIntensityScale = value;
    mHasWallIntensityScale = true;
}

// 9 (0x2ea81e)
void cDiffusionLightExtension::SetSourceFloorIntensityScale(float value)
{
    mFloorIntensityScale = value;
    mHasFloorIntensityScale = true;
}

// 10 (0x2ea87e)
void cDiffusionLightExtension::SetSourceInterFloorFalloff(float value)
{
    mInterFloorFalloff = value;
    mHasInterFloorFalloff = true;
}

// ---- 11-20: the defaults (no flag). 13 and 16-20 are folded bodies the game shares with unrelated classes:
// 13 shared_vf_2b6a8a, 16 shared_vf_2ea7c2, 17 shared_vf_2ea77a (body 0x2ea790), 18 shared_vf_2748ea (body 0x274900),
// 19 shared_vf_2ea802, 20 shared_vf_2ea862 ----

// 11 (0x2ea926): both floats loaded before either is stored.
void cDiffusionLightExtension::SetDefaultSourceMin(const float *xy)
{
    const float x = xy[0];
    const float y = xy[1];
    mDefaultMin[0] = x;
    mDefaultMin[1] = y;
}

// 12 (0x2ea8de): as SetDefaultSourceMin.
void cDiffusionLightExtension::SetDefaultSourceMax(const float *xy)
{
    const float x = xy[0];
    const float y = xy[1];
    mDefaultMax[0] = x;
    mDefaultMax[1] = y;
}

void cDiffusionLightExtension::SetDefaultSourceIntensity(float value) { mDefaultIntensity = value; } // 13

// 14 (0x2ea728): SetSourceColor's copy order.
void cDiffusionLightExtension::SetDefaultSourceColor(const float *rgb)
{
    const float r = rgb[0];
    const float g = rgb[1];
    mDefaultColor[0] = r;
    mDefaultColor[1] = g;
    mDefaultColor[2] = rgb[2];
}

// 15 (0x2ea8a2): the argument's low byte.
void cDiffusionLightExtension::SetDefaultSourceIsDirectional(bool value) { mDefaultIsDirectional = value; }

void cDiffusionLightExtension::SetDefaultSourceFalloff(float value) { mDefaultFalloff = value; }                       // 16
void cDiffusionLightExtension::SetDefaultSourceDirectionalFalloff(float value) { mDefaultDirectionalFalloff = value; } // 17
void cDiffusionLightExtension::SetDefaultSourceWallIntensityScale(float value) { mDefaultWallIntensityScale = value; } // 18
void cDiffusionLightExtension::SetDefaultSourceFloorIntensityScale(float value) { mDefaultFloorIntensityScale = value; } // 19
void cDiffusionLightExtension::SetDefaultSourceInterFloorFalloff(float value) { mDefaultInterFloorFalloff = value; }   // 20

// ---- 21-30: the override while its flag is set, else the default (pointers to the object's own floats) ----

const float *cDiffusionLightExtension::GetSourceMin() { return mHasMin ? mMin : mDefaultMin; }       // 21 (0x2ea531)
const float *cDiffusionLightExtension::GetSourceMax() { return mHasMax ? mMax : mDefaultMax; }       // 22 (0x2ea50e)
float cDiffusionLightExtension::GetSourceIntensity() { return mHasIntensity ? mIntensity : mDefaultIntensity; } // 23 (0x2ea4ae)
const float *cDiffusionLightExtension::GetSourceColor() { return mHasColor ? mColor : mDefaultColor; } // 24 (0x2ea436)

// 25 (0x2ea4ee): the stored byte (the original writes only al).
bool cDiffusionLightExtension::GetSourceIsDirectional()
{
    return mHasIsDirectional ? mIsDirectional : mDefaultIsDirectional;
}

// 26 (0x2ea46e)
float cDiffusionLightExtension::GetSourceFalloff() { return mHasFalloff ? mFalloff : mDefaultFalloff; }

// 27 (0x2ea451)
float cDiffusionLightExtension::GetSourceDirectionalFalloff()
{
    return mHasDirectionalFalloff ? mDirectionalFalloff : mDefaultDirectionalFalloff;
}

// 28 (0x2ea551)
float cDiffusionLightExtension::GetSourceWallIntensityScale()
{
    return mHasWallIntensityScale ? mWallIntensityScale : mDefaultWallIntensityScale;
}

// 29 (0x2ea48e)
float cDiffusionLightExtension::GetSourceFloorIntensityScale()
{
    return mHasFloorIntensityScale ? mFloorIntensityScale : mDefaultFloorIntensityScale;
}

// 30 (0x2ea4d1)
float cDiffusionLightExtension::GetSourceInterFloorFalloff()
{
    return mHasInterFloorFalloff ? mInterFloorFalloff : mDefaultInterFloorFalloff;
}

} // namespace nTSSG
