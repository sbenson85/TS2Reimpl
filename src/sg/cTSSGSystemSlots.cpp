// nTSSG::cTSSGSystem: the primary face's small slots and the reference counting (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/DetailObject.h"
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZRefCount.h"

#include <cstring>
#include <new>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

constexpr uint32_t kSystemIid = 0xba353ce1;
constexpr uint32_t kSystemIid2 = 0xfa353cdb;
constexpr uint32_t kActiveEvent = 0x5b51e806;
constexpr uint32_t kDetailEvent = 0x2b5745f8;
constexpr uint32_t kShadowEvent = 0xca7ccbbc;
constexpr uint32_t kEventManagerCell = 0x10cc8a8;
constexpr uint32_t kLotWorld = 0x0bea28c2;
constexpr uint32_t kDefaultColor = 0xff796c;
constexpr int32_t kCounterFrozen = int32_t(0x80000100u);

// The event data's kind (the dword at +4).
uint32_t KindOf(const void *data) { return static_cast<const uint32_t *>(data)[1]; }

uint8_t &Byte(nTSSG::cTSSGSystem *self, uint32_t offset) { return reinterpret_cast<uint8_t *>(self)[offset]; }

// The listener range the event manager's slot 10 fills: the iterator (slot 4 at the end, slot 5 the current pair,
// slot 6 next), a second reference and the end. All three released, end first.
struct ListenerRange {
    void *iterator = nullptr;
    void *second = nullptr;
    void *end = nullptr;
    ListenerRange() = default;
    ListenerRange(const ListenerRange &) = delete;
    ListenerRange &operator=(const ListenerRange &) = delete;
    ~ListenerRange()
    {
        if (end) Call<2>(end);
        if (second) Call<2>(second);
        if (iterator) Call<2>(iterator);
    }
    bool AtEnd() const { return Call<4, uint8_t>(iterator, end) != 0; }
    // The current listener and its value (two reads of the current pair).
    void *Current(uint32_t *value) const
    {
        uint32_t first[2], next[2];
        Call<5>(iterator, first);
        Call<5>(iterator, next);
        *value = next[1];
        return reinterpret_cast<void *>(uintptr_t(first[0]));
    }
    void Next() const { Call<6>(iterator); }
};

} // namespace

namespace nTSSG {

bool cTSSGSystem::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kSystemIid || iid == kSystemIid2) {
        *out = this;
        Call<1>(this);
        return true;
    }
    return OriginalFunction<bool(__thiscall *)(void *, uint32_t, void **), 0x148310>()(this, iid, out);
}

uint32_t cTSSGSystem::Release()
{
    auto *count = reinterpret_cast<volatile int32_t *>(&mRefCount);
    if ((uint32_t(*count) & 0x7fffffff) == 1) {
        if (GetEventManager()) Call<5>(GetEventManager(), static_cast<void *>(this));
        Call<88>(this, 1u);
        return 0;
    }
    RZAtomicAdd(count, -1);
    return uint32_t(*count) & 0x7fffffff;
}

void *cTSSGSystem::GetEventManager()
{
    void **cell = GameAddress<void *>(kEventManagerCell);
    if (!*cell) {
        struct Held {
            RZAutoRef ref{nullptr};
            ~Held() { ref.ReleaseObject(); }
        } service;
        if (void *framework = GetServiceObject()) Call<5>(framework, 0x5b1e896fu, 0x1b1e895au, &service.ref.mObject);
        if (service.ref.mObject) *cell = Call<3, void *>(service.ref.mObject);
    }
    return *cell;
}

void cTSSGSystem::BehaviorOnEvent(uint32_t event, uint32_t, void *, uint32_t)
{
    if (event != kActiveEvent) return;
    const uint8_t active = Call<6, uint8_t>(GetEventManager(), static_cast<void *>(this));
    auto *count = reinterpret_cast<volatile int32_t *>(&mRefCount);
    if (uint8_t(uint32_t(*count) >> 31) != active) RZAtomicAdd(count, int32_t(0x80000000u));
}

void cTSSGSystem::OnEvent(uint32_t event, uint32_t a, void *data, uint32_t d)
{
    BehaviorOnEvent(event, a, data, d);
    if (event == kDetailEvent) {
        if (KindOf(data) == 2) RefreshDetailFloat();
    } else if (event == kShadowEvent) {
        const uint32_t kind = KindOf(data);
        if (kind == 1) {
            if (mShadowManager.mObject) Call<7>(mShadowManager.mObject);
            Call<39>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
            mCaptureSuspended = 0;
        } else if (kind == 4) {
            mCaptureSuspended = 1;
            Call<39>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
        }
    }
}

void cTSSGSystem::NotifyListeners(uint32_t kind, uint32_t a, uint32_t b)
{
    if (mRefCount >= 0) return;
    ListenerRange range;
    Call<10>(GetEventManager(), &range, static_cast<void *>(this), kind);
    while (!range.AtEnd()) {
        uint32_t value;
        void *listener = range.Current(&value);
        Call<3>(listener, value, a, b, static_cast<void *>(this));
        range.Next();
    }
}

bool cTSSGSystem::AskListeners(uint32_t kind, uint32_t a, uint32_t b)
{
    if (mRefCount >= 0) return false;
    bool every = true, any = false;
    {
        ListenerRange range;
        Call<10>(GetEventManager(), &range, static_cast<void *>(this), kind);
        while (!range.AtEnd()) {
            any = true;
            uint32_t value;
            void *listener = range.Current(&value);
            const bool yes = Call<4, uint8_t>(listener, value, a, b) != 0;
            every = yes && every;
            range.Next();
        }
    }
    return every && any;
}

void cTSSGSystem::EventManagerSlot3(uint32_t a, uint32_t b, uint32_t c)
{
    Call<3>(GetEventManager(), static_cast<void *>(this), a, b, c);
}

void cTSSGSystem::EventManagerSlot4(uint32_t a, uint32_t b, uint32_t c)
{
    Call<4>(GetEventManager(), static_cast<void *>(this), a, b, c);
}

void cTSSGSystem::SetRefC0(void *object)
{
    void *old = mRefC0.mObject;
    if (object == old) return;
    if (object) Call<1>(VirtualBase(object, 4, 1));
    mRefC0.mObject = object;
    if (old) Call<2>(VirtualBase(old, 4, 1));
}

void cTSSGSystem::AdvanceCounters(int32_t delta)
{
    if (mCounter194 != kCounterFrozen) mCounter194 += delta;
    if (mCounter198) mCounter198 += delta;
}

void cTSSGSystem::SetValue38(void *value)
{
    mUnknown38 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
    void *object = Call<52, void *>(this);
    if (!object) return;
    mCounter198 = value ? Call<11, int32_t>(VirtualBase(object, 4, 2)) : 0;
}

void cTSSGSystem::SetParserValue(uint32_t value)
{
    mFlag16B = 0;
    if (!mParserTarget || Call<6>(mParserTarget) == value) return;
    Call<5>(mParserTarget, value);
    ReadLotTunables(Call<9, void *>(mCommandParser.mObject));
    void *target = Call<25, void *>(Call<49, void *>(this));
    void *source = Call<17, void *>(Call<33, void *>(this));
    Call<9>(target, source);
}

bool cTSSGSystem::IsSlot56()
{
    if (!mRefB4.mObject || !mRef124.mObject) return false;
    if (!Call<33, void *>(mRef124.mObject)) return false;
    return !Call<60, uint8_t>(Call<33, void *>(mRef124.mObject));
}

uint32_t cTSSGSystem::AppObjectSlot20() { return Call<20>(Call<22, void *>(GetCachedSingleton())); }

uint32_t cTSSGSystem::AppObjectSlot25() { return Call<25>(Call<22, void *>(GetCachedSingleton())); }

bool cTSSGSystem::BothOptions18E() { return Byte(this, 0x18f) && Byte(this, 0x18e); }

float cTSSGSystem::GetLotTunable(int32_t which)
{
    if (which == 3) return mLotTunables[1];
    if (which == 4) return mLotTunables[2];
    return mLotTunables[0];
}

cTSSGSystem::Color4 cTSSGSystem::GetColor(uint32_t index)
{
    Color4 color;
    std::memcpy(color.v, index < 8 ? static_cast<const void *>(mColors[index]) : GameAddress<const void>(kDefaultColor),
                sizeof(color.v));
    return color;
}

bool cTSSGSystem::GetLotBounds(void *out)
{
    if (!mObjectFadeSet || Call<5>(GetCachedSlot23()) != kLotWorld) return false;
    std::memcpy(out, &mObjectFade, sizeof(mObjectFade));
    return true;
}

void cTSSGSystem::FillTarget(void *target) { OriginalFunction<void(__cdecl *)(void *), 0x274a50>()(target); }

void cTSSGSystem::RefreshDetailFloat()
{
    if (!mFlag16A || !mFlag16B) return;
    float value;
    value = mAnimClock;
    Call<17>(nGZSceneGraph::GetDetailObject(), value);
}

cTSSGSystem *cTSSGSystem::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, 0x3b8);
    return this;
}

bool cTSSGSystemMessageFace::QueryInterface(uint32_t iid, void **out)
{
    return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 8)->QueryInterface(iid, out);
}

uint32_t cTSSGSystemMessageFace::Release() { return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 8)->Release(); }

bool cTSSGSystemServiceSub::QueryInterface(uint32_t iid, void **out)
{
    return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 0xc)->QueryInterface(iid, out);
}

uint32_t cTSSGSystemServiceSub::Release() { return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 0xc)->Release(); }

} // namespace nTSSG
