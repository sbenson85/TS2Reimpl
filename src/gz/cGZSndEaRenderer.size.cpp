// cGZSndEaRenderer (lane KJ): the whole class in the standalone shape (src/gz/cGZSndEaRenderer.h).
#include "gz/cGZSndEaRenderer.h"

#include "compgeom/Frames.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cRZRefCount.h"      // RZAtomicAdd

#include <cmath>
#include <cstring>
#include <new>

#include <xmmintrin.h>

namespace {

// EA's sound library: original callees nobody has replaced (cdecl unless noted; an int result is the library's status,
// negative on failure).
namespace ea {
void Lock() { OriginalFunction<void(__cdecl *)(), 0xe7362>()(); }
void Unlock() { OriginalFunction<void(__cdecl *)(), 0xe7370>()(); }
int SetLevel(int level) { return OriginalFunction<int(__cdecl *)(int), 0xe737d>()(level); }
int Call7442(uint32_t v) { return OriginalFunction<int(__cdecl *)(uint32_t), 0xe7442>()(v); }
int Query746e(uint32_t v, uint8_t *flag) { return OriginalFunction<int(__cdecl *)(uint32_t, uint8_t *), 0xe746e>()(v, flag); }
int Call74b2(uint32_t v) { return OriginalFunction<int(__cdecl *)(uint32_t), 0xe74b2>()(v); }
int Get74f7(uint32_t *out) { return OriginalFunction<int(__cdecl *)(uint32_t *), 0xe74f7>()(out); }
int Get7506(uint32_t zero, uint32_t v, uint32_t *out)
{
    return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t, uint32_t *), 0xe7506>()(zero, v, out);
}
int Call754c(uint32_t zero, uint32_t v) { return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t), 0xe754c>()(zero, v); }
int Get75ab(uint32_t zero, uint32_t *out) { return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t *), 0xe75ab>()(zero, out); }
int Get75e1(uint32_t v, uint32_t *out) { return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t *), 0xe75e1>()(v, out); }
int Call7617(uint32_t a, uint32_t b) { return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t), 0xe7617>()(a, b); }
int Get7668(uint32_t v, uint32_t *out) { return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t *), 0xe7668>()(v, out); }
int Start(uint32_t zero, void *buffer, uint32_t size)
{
    return OriginalFunction<int(__cdecl *)(uint32_t, void *, uint32_t), 0xe7748>()(zero, buffer, size);
}
bool IsReady() { return OriginalFunction<bool(__cdecl *)(), 0xe772c>()(); }
int Prepare(uint32_t flags) { return OriginalFunction<int(__cdecl *)(uint32_t), 0xe7699>()(flags); }
void Stop() { OriginalFunction<void(__cdecl *)(), 0xe7739>()(); }
void Call7768(uint32_t v) { OriginalFunction<void(__cdecl *)(uint32_t), 0xe7768>()(v); }
int GetMaxVoices(uint32_t *out) { return OriginalFunction<int(__cdecl *)(uint32_t *), 0xe7779>()(out); }
int SetMaxVoices(uint32_t count) { return OriginalFunction<int(__cdecl *)(uint32_t), 0xe7788>()(count); }
int OpenVoice(uint32_t zero, uint32_t index, void **out)
{
    return OriginalFunction<int(__cdecl *)(uint32_t, uint32_t, void **), 0xe779d>()(zero, index, out);
}
// thiscall on the voice handle
void SetVoiceValue(void *voice, uint32_t v) { OriginalFunction<void(__thiscall *)(void *, uint32_t), 0xe7895>()(voice, v); }
void CloseVoice(void *voice) { OriginalFunction<void(__thiscall *)(void *), 0xe78bc>()(voice); }
int SetVoiceLevel(void *voice, float level) { return OriginalFunction<int(__thiscall *)(void *, float), 0xe7811>()(voice, level); }
} // namespace ea

// The 3 x 3 inverse (0x1a62e0, cdecl: out, in; returns out): ours (src/compgeom/Frames.h).
const float *Invert3x3(float *out, const float *in)
{
    return nGZCompGeom::InverseMatrix3(out, in);
}

// The sound renderer service (the 37-byte getter 0x20db8e: framework slot 5, released at scope end).
constexpr uint32_t kSoundRendererId = 0x23cecba0, kSoundRendererIid = 0x106077d2;
void TakeSoundRendererService(AudioRef &ref)
{
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSoundRendererId, kSoundRendererIid, ref.AsPPVoid());
}
// Its slots this class uses: 30 / 31 add / remove an update target (with its period), 32 asks it to call a function for
// its sounds, 35 / 36 add / remove a renderer.
bool AddUpdateTarget(cIGZUnknown *service, cIGZSndUpdateTarget *target, uint32_t period)
{
    return RawSlot<30, bool>(service, target, period);
}
void RemoveUpdateTarget(cIGZUnknown *service, cIGZSndUpdateTarget *target) { RawSlot<31, void>(service, target); }
void ForEachSound(cIGZUnknown *service, bool(__cdecl *fn)(cIGZUnknown *)) { RawSlot<32, void>(service, fn, 0u, 0u); }
bool AddRenderer(cIGZUnknown *service, cIGZSndRenderer *renderer) { return RawSlot<35, bool>(service, renderer); }
void RemoveRenderer(cIGZUnknown *service, cIGZSndRenderer *renderer) { RawSlot<36, void>(service, renderer); }

constexpr uint32_t kUpdatePeriod = 0x14;       // ms
constexpr uint32_t kListenerAwareIid = 0x6b0446eb; // what NotifySound asks a sound for (its slot 3 is called)
constexpr uint32_t kPrepareFlags = 0x90400;

// Two floats within 32 units in the last place, the original's test (0xb1359a / 0xb13746): sign-magnitude bits made
// two's complement, the difference in [-32, 31].
int32_t Ordered(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits & 0x80000000u ? -int32_t(bits & 0x7fffffffu) : int32_t(bits);
}
bool Near(float field, float value)
{
    const int32_t d = int32_t(uint32_t(Ordered(field)) - uint32_t(Ordered(value)));
    return d >= -32 && d <= 31;
}
bool Near(const cGZSndVec3 &field, const cGZSndVec3 &value)
{
    return Near(field.x, value.x) && Near(field.y, value.y) && Near(field.z, value.z);
}

void Put(float *out, float value)
{
    if (out) *out = value;
}

// Single-precision a * b and a + b with the operands in this order: of two NaN operands SSE returns the first, so the
// order is part of the result (the compiler may swap the operands of a plain * or +).
float Mul(float a, float b) { return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b))); }
float Add(float a, float b) { return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b))); }

} // namespace

// ---- construction ----

cGZSndEaRenderer::cGZSndEaRenderer() = default; // 0xb12ce3

// 0xb12de1: the members (the voices, the service, the buffer).
cGZSndEaRenderer::~cGZSndEaRenderer() noexcept = default;

cGZSndEaRenderer *__cdecl cGZSndEaRenderer::Create() { return new cGZSndEaRenderer; }

cGZSndEaRenderer *__fastcall cGZSndEaRendererAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZSndEaRenderer;
}

// ---- cIGZUnknown ----

// 0xb133f0: the out pointer is cleared first; the system service id gives the primary face.
bool cGZSndEaRenderer::QueryInterface(uint32_t iid, void **out)
{
    *out = nullptr;
    switch (iid) {
    case cIGZSystemService::kIID: *out = static_cast<cIGZSystemService *>(this); break;
    case 1:
    case cIGZSndRenderer::kIid: *out = static_cast<cIGZSndRenderer *>(this); break;
    case cIGZSnd3DListener::kIid: *out = static_cast<cIGZSnd3DListener *>(this); break;
    case cIGZSnd3DListenerEx::kIid: *out = static_cast<cIGZSnd3DListenerEx *>(this); break;
    case cIGZSndEaRenderer::kIid: *out = static_cast<cIGZSndEaRenderer *>(this); break;
    case cIGZSndUpdateTarget::kIid: *out = static_cast<cIGZSndUpdateTarget *>(this); break;
    case cIGZSndEaRendererEffects::kIid: *out = static_cast<cIGZSndEaRendererEffects *>(this); break;
    default: return false;
    }
    AddRef();
    return true;
}

// 0xb12e59
uint32_t cGZSndEaRenderer::AddRef() { return uint32_t(RZAtomicAdd(&mRefCount, 1)); }

// 0xb134b5: the last reference (a count of 0 or 1, unsigned) destroys it.
uint32_t cGZSndEaRenderer::Release()
{
    if (uint32_t(mRefCount) > 1) return uint32_t(RZAtomicAdd(&mRefCount, -1));
    delete this;
    return 0;
}

// ---- cIGZSystemService ----

// 0xb132a5: takes the sound renderer service, starts EA's library on a work buffer and registers with the service; a
// failure after the service was taken shuts down (through the vtable) and fails. When the library's slot 5 query says
// no, it succeeds without starting.
bool cGZSndEaRenderer::Init()
{
    AudioRef service;
    TakeSoundRendererService(service);
    if (!service) return false;
    mService.Assign(service.Get());
    if (!static_cast<cIGZSndEaRenderer *>(this)->EaSlot5()) return true;
    mBuffer.resize(kBufferSize);
    bool ok = ea::Start(0, mBuffer.data(), uint32_t(mBuffer.size())) == 0;
    if (ok) {
        static_cast<cIGZSndEaRenderer *>(this)->EaSlot20(0x1e);
        static_cast<cIGZSndEaRenderer *>(this)->EaSlot18(0, 0x3c);
        static_cast<cIGZSndEaRendererEffects *>(this)->SetMaxVoices(1);
        ok = (ea::IsReady() || ea::Prepare(kPrepareFlags) >= 0) && ea::Call74b2(1) == 0 &&
             AddRenderer(mService.Get(), this) && AddUpdateTarget(mService.Get(), this, kUpdatePeriod);
    }
    if (!ok) {
        static_cast<cIGZSystemService *>(this)->Shutdown();
        return false;
    }
    mInitialized = true;
    return true;
}

// 0xb13c35: when initialised, unregisters from the service and lets it go, closes every voice (through the vtable),
// stops the library and empties the buffer.
bool cGZSndEaRenderer::Shutdown()
{
    if (!mInitialized) return true;
    if (mService) {
        RemoveUpdateTarget(mService.Get(), this);
        RemoveRenderer(mService.Get(), this);
    }
    mService.Assign(nullptr);
    for (uint32_t i = 0; i < mVoices.size(); ++i) static_cast<cIGZSndEaRendererEffects *>(this)->CloseVoice(i);
    mVoices.clear();
    ea::Stop();
    mBuffer.clear();
    mInitialized = false;
    return true;
}

// ---- cIGZSndUpdateTarget ----

// 0xb13d27
void cGZSndEaRenderer::Update(uint32_t)
{
    if (!mDirty) return;
    mDirty = false;
    RebuildListener();
}

// 0xb13d42: the matrix with columns (top x front, top, front), inverted; then the service calls NotifySound on its
// sounds (the service is not checked).
void cGZSndEaRenderer::RebuildListener()
{
    const cGZSndVec3 &t = mTop, &f = mFront;
    float *const m = mMatrix;
    m[0] = Mul(f.z, t.y) - Mul(f.y, t.z);
    m[3] = Mul(f.x, t.z) - Mul(f.z, t.x);
    m[6] = Mul(f.y, t.x) - Mul(f.x, t.y);
    m[1] = t.x;
    m[4] = t.y;
    m[7] = t.z;
    m[2] = f.x;
    m[5] = f.y;
    m[8] = f.z;
    float inverse[9];
    std::memcpy(m, Invert3x3(inverse, m), sizeof(inverse));
    ForEachSound(mService.Get(), &NotifySound);
}

// 0xb12fa6: a sound that knows the listener (interface 0x6b0446eb) is told it changed (its slot 3).
bool __cdecl cGZSndEaRenderer::NotifySound(cIGZUnknown *sound)
{
    cIGZUnknown *aware = nullptr;
    if (sound->QueryInterface(kListenerAwareIid, reinterpret_cast<void **>(&aware))) RawSlot<3, void>(aware);
    if (aware) aware->Release();
    return true;
}

// ---- cIGZSndRenderer ----

// 0xb13a6e
bool cGZSndEaRenderer::SetLevel(int32_t level) { return SetLevelValue(float(level)); }

// 0xb13a4b: EA's level is out of 127.
bool cGZSndEaRenderer::SetLevelValue(float level)
{
    mLevel = level;
    ea::SetLevel(int(level * 127.0f));
    return true;
}

// 0xb13102
int32_t cGZSndEaRenderer::GetLevel()
{
    float level;
    GetLevelValue(&level);
    return int32_t(level);
}

// 0xb130f4
bool cGZSndEaRenderer::GetLevelValue(float *out)
{
    *out = mLevel;
    return true;
}

// 0xb133e4
bool cGZSndEaRenderer::Pause(uint32_t)
{
    mPaused = true;
    return true;
}

// 0xb13d1d
bool cGZSndEaRenderer::Resume()
{
    mPaused = false;
    return true;
}

// ---- cIGZSndEaRenderer ----

// 0xb133c0
bool cGZSndEaRenderer::EaSlot5()
{
    uint8_t flag = 1;
    return ea::Query746e(1, &flag) < 0 ? false : flag != 0;
}

// 0xb13cef
bool cGZSndEaRenderer::EaSlot6(uint32_t value)
{
    ea::Lock();
    ea::Call7768(value);
    ea::Unlock();
    return true;
}

// 0xb13d08
bool cGZSndEaRenderer::EaSlot7()
{
    ea::Lock();
    ea::Call7768(0);
    ea::Unlock();
    return true;
}

// 0xb13268
void cGZSndEaRenderer::GetMatrix(float *out9) { std::memcpy(out9, mMatrix, sizeof(mMatrix)); }

// 0xb136af
void cGZSndEaRenderer::SetFactors(float a, float b, float c)
{
    mFactors = {a, b, c};
    mDirty = true;
}

// 0xb13082
void cGZSndEaRenderer::GetFactors(float *a, float *b, float *c)
{
    *a = mFactors.x;
    *b = mFactors.y;
    *c = mFactors.z;
}

// 0xb12ea3
bool cGZSndEaRenderer::EaSlot11(uint32_t value)
{
    uint8_t flag = 0;
    return ea::Query746e(value, &flag) < 0 ? false : flag != 0;
}

// 0xb13b5d
bool cGZSndEaRenderer::EaSlot12(uint32_t value)
{
    if (!static_cast<cIGZSndEaRenderer *>(this)->EaSlot11(value)) return false;
    return ea::Call74b2(value) >= 0;
}

// 0xb131a5: the out word is written whatever the library did.
bool cGZSndEaRenderer::EaSlot13(uint32_t *out)
{
    uint32_t value;
    const int status = ea::Get74f7(&value);
    *out = value;
    return status >= 0;
}

// 0xb12eca
bool cGZSndEaRenderer::EaSlot14(uint32_t value, uint32_t *out)
{
    uint32_t result;
    const int status = ea::Get7506(0, value, &result);
    *out = result;
    return status >= 0;
}

// 0xb13b7c
bool cGZSndEaRenderer::EaSlot15(uint32_t value)
{
    uint32_t unused;
    if (!static_cast<cIGZSndEaRenderer *>(this)->EaSlot14(value, &unused)) return false;
    return ea::Call754c(0, value) >= 0;
}

// 0xb131c4
bool cGZSndEaRenderer::EaSlot16(uint32_t *out)
{
    uint32_t value;
    const int status = ea::Get75ab(0, &value);
    *out = value;
    return status >= 0;
}

// 0xb12ef0: the library's out word is dropped.
bool cGZSndEaRenderer::EaSlot17(uint32_t value, uint32_t)
{
    uint32_t unused;
    return ea::Get75e1(value, &unused) >= 0;
}

// 0xb13afa
bool cGZSndEaRenderer::EaSlot18(uint32_t a, uint32_t b) { return ea::Call7617(a, b) >= 0; }

// 0xb13133
bool cGZSndEaRenderer::EaSlot19(uint32_t value, uint32_t *out)
{
    uint32_t result;
    const int status = ea::Get7668(value, &result);
    *out = result;
    return status >= 0;
}

// 0xb13ae8
bool cGZSndEaRenderer::EaSlot20(uint32_t value) { return ea::Call7442(value) >= 0; }

// 0xb1359a: stored (and marked dirty) only when it moved by more than a few units in the last place.
void cGZSndEaRenderer::SetPositionB(const cGZSndVec3 *v)
{
    if (Near(mPositionB, *v)) return;
    mPositionB = *v;
    mDirty = true;
}

// 0xb13746: the same test for each vector.
void cGZSndEaRenderer::SetListener(const cGZSndVec3 *position, const cGZSndVec3 *top, const cGZSndVec3 *front)
{
    if (!Near(mPosition, *position)) {
        mPosition = *position;
        mDirty = true;
    }
    if (!Near(mTop, *top)) {
        mTop = *top;
        mDirty = true;
    }
    if (!Near(mFront, *front)) {
        mFront = *front;
        mDirty = true;
    }
}

// 0xb13071
void cGZSndEaRenderer::GetPositionB(cGZSndVec3 *out) { *out = mPositionB; }

// 0xb130cf
void cGZSndEaRenderer::GetListener(cGZSndVec3 *position, cGZSndVec3 *top, cGZSndVec3 *front)
{
    *position = mPosition;
    *top = mTop;
    *front = mFront;
}

// ---- cIGZSnd3DListener ----

// 0xb131e6
void cGZSndEaRenderer::GetPosition(float *x, float *y, float *z)
{
    Put(x, mPosition.x);
    Put(y, mPosition.y);
    Put(z, mPosition.z);
}

// 0xb13ba5: the second position follows.
bool cGZSndEaRenderer::SetPosition(float x, float y, float z)
{
    mPosition = {x, y, z};
    mPositionB = mPosition;
    mDirty = true;
    return true;
}

// 0xb13156
void cGZSndEaRenderer::GetOrientation(float *topX, float *topY, float *topZ, float *frontX, float *frontY, float *frontZ)
{
    Put(topX, mTop.x);
    Put(topY, mTop.y);
    Put(topZ, mTop.z);
    Put(frontX, mFront.x);
    Put(frontY, mFront.y);
    Put(frontZ, mFront.z);
}

// 0xb13b11
bool cGZSndEaRenderer::SetOrientation(float topX, float topY, float topZ, float frontX, float frontY, float frontZ)
{
    mTop = {topX, topY, topZ};
    mFront = {frontX, frontY, frontZ};
    mDirty = true;
    return true;
}

// 0xb12e97
cIGZSnd3DListenerEx *cGZSndEaRenderer::AsListenerEx() { return this; }

// ---- cIGZSnd3DListenerEx ----

// 0xb1327b
void cGZSndEaRenderer::GetVector70(float *x, float *y, float *z)
{
    Put(x, mVector70.x);
    Put(y, mVector70.y);
    Put(z, mVector70.z);
}

// 0xb13c08
bool cGZSndEaRenderer::SetVector70(float x, float y, float z)
{
    mVector70 = {x, y, z};
    mDirty = true;
    return true;
}

// 0xb12fda
void cGZSndEaRenderer::GetAll(float *px, float *py, float *pz, float *vx, float *vy, float *vz, float *tx, float *ty,
                              float *tz, float *fx, float *fy, float *fz)
{
    GetPosition(px, py, pz);
    Put(vx, mVector70.x);
    Put(vy, mVector70.y);
    Put(vz, mVector70.z);
    GetOrientation(tx, ty, tz, fx, fy, fz);
}

// 0xb13506
bool cGZSndEaRenderer::SetAll(float px, float py, float pz, float vx, float vy, float vz, float tx, float ty, float tz,
                              float fx, float fy, float fz)
{
    mPosition = {px, py, pz};
    mPositionB = mPosition;
    mVector70 = {vx, vy, vz};
    mTop = {tx, ty, tz};
    mFront = {fx, fy, fz};
    mDirty = true;
    return true;
}

// 0xb130bc
void cGZSndEaRenderer::GetValueAc(float *out) { Put(out, mValueAc); }

// 0xb13733
bool cGZSndEaRenderer::SetValueAc(float value)
{
    mValueAc = value;
    return true;
}

// 0xb13210: the factors' length, summed y, x, z in single precision.
void cGZSndEaRenderer::GetFactorLength(float *out)
{
    if (!out) return;
    const float sum = Add(Add(mFactors.y * mFactors.y, mFactors.x * mFactors.x), mFactors.z * mFactors.z);
    *out = float(std::sqrt(double(sum)));
}

// 0xb13bde
bool cGZSndEaRenderer::SetAllFactors(float value)
{
    mFactors = {value, value, value};
    mDirty = true;
    return true;
}

// 0xb130a9
void cGZSndEaRenderer::GetValueB0(float *out) { Put(out, mValueB0); }

// 0xb13720
bool cGZSndEaRenderer::SetValueB0(float value)
{
    mValueB0 = value;
    return true;
}

// ---- cIGZSndEaRendererEffects ----

// 0xb13119
uint32_t cGZSndEaRenderer::GetMaxVoices()
{
    uint32_t count;
    return ea::GetMaxVoices(&count) < 0 ? 0 : count;
}

// 0xb13a88: the handle table grows to the library's count when asked for more than it holds; more voices than the library
// has can be set only before Init.
bool cGZSndEaRenderer::SetMaxVoices(uint32_t count)
{
    uint32_t have = 0;
    if (ea::GetMaxVoices(&have) < 0) return false;
    if (count > mVoices.size()) mVoices.resize(have);
    if (count <= have) return true;
    if (mInitialized) return false;
    return ea::SetMaxVoices(count) >= 0;
}

// 0xb12f0b
bool cGZSndEaRenderer::OpenVoice(uint32_t index, uint32_t value)
{
    if (index >= mVoices.size()) return false;
    void *voice = nullptr;
    if (ea::OpenVoice(0, index, &voice) < 0) return false;
    mVoices[index] = voice;
    if (value) ea::SetVoiceValue(voice, value);
    return true;
}

// 0xb12f68
bool cGZSndEaRenderer::CloseVoice(uint32_t index)
{
    if (index >= mVoices.size()) return false;
    if (mVoices[index]) ea::CloseVoice(mVoices[index]);
    mVoices[index] = nullptr;
    return true;
}

// 0xb136e3
bool cGZSndEaRenderer::SetVoiceLevel(uint32_t index, float level)
{
    if (index >= mVoices.size() || !mVoices[index]) return false;
    return ea::SetVoiceLevel(mVoices[index], level) >= 0;
}
