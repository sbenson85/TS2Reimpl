// cTSAudioAmbienceManager (lane KG): the whole class in the standalone shape (src/audio/cTSAudioAmbienceManager.h).
#include "audio/cTSAudioAmbienceManager.h"

#include "audio/cTSAudioEvent.h"
#include "audio/cTSAudioSoundManager.h" // cITSAudioSoundManager
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZSystemService.h"
#include "misc/UtilitiesC.h"     // GetCachedServiceObjectC: the sound manager
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZRefCount.h"      // RZAtomicAdd
#include "rz/nGZLog.h"           // StoreCurrentThreadId

#include <new>

namespace {

// The sound objects the manager loads (framework class 0x3c1cf59e) and the interface it keeps them by.
constexpr uint32_t kSoundClass = 0x3c1cf59e, kSoundIid = 0xdc1cf595;
constexpr uint32_t kSoundResourceType = 0x2026960b; // the first word of the key a sound loads from (its slot 3)
// The renderer: framework service 0x23cecba0 queried for 0x106077d2 (the 37-byte getter 0x20db8e).
constexpr uint32_t kRendererId = 0x23cecba0, kRendererIid = 0x106077d2;

// The event ids (one per proxy) and the keys of their values.
enum : uint32_t {
    kLoadEvent = 0xabbd0bca, kIsLoadedEvent = 0x2bbd0bd1, kUnloadEvent = 0xebbd0bd6, kPlayEvent = 0x8bbd0bdc,
    kStopEvent = 0x8bbd0be1, kSlot10Event = 0xcbbd0be6, kSlot11Event = 0x6bbd0bee, kSlot12Event = 0x6bbd0c0b,
    kSlot13Event = 0xec487275, kSlot14Event = 0x4c4887ad, kSlot15Event = 0x8c5b5545, kSlot16Event = 0xac5b5550,
    kUnloadAllEvent = 0x8bc2894d,
};
enum : uint32_t {
    kGroupKey = 0x4c3e66eb, kInstanceKey = 0x4c3e66e8, kHighKey = 0xedd1b848, kFlagKey = 0x6cc65bad,
    kValue1011Key = 0x4c3e65f3, kValue12Key = 0x4c3e66ee, kCountKey = 0xac48771a, kFirstPointKey = 0xec487263,
    kSlot14AKey = 0xcc488735, kSlot14BKey = 0xcc48873a, kValue1516Key = 0xac5b5564,
};
constexpr uint32_t kMaxPoints = 9; // a 3 x 3 grid

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The framework service cached at 0x10f5194 (the original's 0x211051).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

cITSAudioSoundManager *SoundManager() { return static_cast<cITSAudioSoundManager *>(GetCachedServiceObjectC()); }

using EventRef = cRZAutoRefCount<cITSAudioEvent>;

} // namespace

cTSAudioAmbienceManager::cTSAudioAmbienceManager() = default;

cTSAudioAmbienceManager::~cTSAudioAmbienceManager() noexcept = default;

cTSAudioAmbienceManager *__cdecl cTSAudioAmbienceManager::Create() { return new cTSAudioAmbienceManager; }

cTSAudioAmbienceManager *__fastcall cTSAudioAmbienceManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioAmbienceManager;
}

// ---- cIGZUnknown ----

// 0x227bbe: 1 -> the event handler face (+8), cITSAudioAmbienceManager -> this, cIGZSndEventHandler -> +8; counted
// through the primary face's AddRef.
bool cTSAudioAmbienceManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1) *out = Handler();
    else if (riid == kIid) *out = static_cast<cITSAudioAmbienceManager *>(this);
    else if (riid == kHandlerIid) *out = Handler();
    else return false;
    AddRef();
    return true;
}

// 0x22769d: the interlocked count's new value.
uint32_t cTSAudioAmbienceManager::AddRef() { return static_cast<uint32_t>(RZAtomicAdd(&mRefs, 1)); }

// 0x227c34: above 1, one less (the new value); at 1 or below, deleted (slot 17), 0.
uint32_t cTSAudioAmbienceManager::Release()
{
    if (static_cast<uint32_t>(mRefs) > 1) return static_cast<uint32_t>(RZAtomicAdd(&mRefs, -1));
    delete this;
    return 0;
}

// ---- the thread proxies ----

bool cTSAudioAmbienceManager::OnRendererThread()
{
    uint32_t thread;
    nGZLog::StoreCurrentThreadId(&thread);
    return RawSlot<51>(mRenderer.Get()) == thread;
}

cITSAudioEvent *cTSAudioAmbienceManager::NewEvent(uint32_t id)
{
    cITSAudioEvent *const e = new cTSAudioEvent(id);
    if (e) e->AddRef();
    return e;
}

void cTSAudioAmbienceManager::PostEvent(cITSAudioEvent *event)
{
    SoundManager()->PostEvent(event);
    if (event) event->Release();
}

// 0x227887 (+4 face's slot 8: the thunk 0x2278f8): the renderer held, this registered with it as an event handler and
// with the framework as a service; false without a renderer (which drops one held before).
bool cTSAudioAmbienceManager::Init()
{
    bool ok = false;
    AudioRef renderer; // 0x20db8e
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kRendererId, kRendererIid, renderer.AsPPVoid());
    mRenderer.Assign(renderer.Get());
    if (mRenderer) {
        RawSlot<15>(mRenderer.Get(), Handler());
        As<ifaces::cIGZFrameWork>(CachedFramework())->AddSystemService(As<ifaces::cIGZSystemService>(Service()));
        ok = true;
    }
    return ok;
}

// 0x2283ff (+4 face's slot 9: the thunk 0x228448): every sound unloaded (or the request posted), unregistered, the
// renderer dropped.
bool cTSAudioAmbienceManager::Shutdown()
{
    UnloadAll();
    if (mRenderer) RawSlot<16>(mRenderer.Get(), Handler());
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveSystemService(As<ifaces::cIGZSystemService>(Service()));
    mRenderer.Assign(nullptr);
    return true;
}

// 0x22864a: on the renderer's thread every sound unloaded, else the request sent to the renderer.
void cTSAudioAmbienceManager::UnloadAll()
{
    if (!mRenderer) return;
    if (OnRendererThread()) {
        DoUnloadAll();
        return;
    }
    cITSAudioEvent *const e = NewEvent(kUnloadAllEvent);
    RawSlot<8>(mRenderer.Get(), e);
    if (e) e->Release();
}

// 0x2279f4
bool cTSAudioAmbienceManager::LoadSound(uint32_t group, uint32_t instance, uint32_t high)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) return DoLoad(group, instance, high);
    cITSAudioEvent *const e = NewEvent(kLoadEvent);
    e->SetUint32(kGroupKey, group);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    const bool sent = RawSlot<8, bool>(mRenderer.Get(), e);
    if (e) e->Release();
    return sent;
}

// 0x227900
bool cTSAudioAmbienceManager::IsSoundLoaded(uint32_t instance, uint32_t high)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) return DoIsLoaded(instance, high);
    cITSAudioEvent *const e = NewEvent(kIsLoadedEvent);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    const bool sent = RawSlot<8, bool>(mRenderer.Get(), e);
    if (e) e->Release();
    return sent;
}

// 0x2286e6
bool cTSAudioAmbienceManager::UnloadSound(uint32_t instance, uint32_t high)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) return DoUnload(instance, high);
    cITSAudioEvent *const e = NewEvent(kUnloadEvent);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    const bool sent = RawSlot<8, bool>(mRenderer.Get(), e);
    if (e) e->Release();
    return sent;
}

// 0x227ad8: true with a renderer, whatever the worker answers.
bool cTSAudioAmbienceManager::Play(uint32_t instance, uint32_t high, uint32_t flag)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) {
        DoPlay(instance, high, flag);
    } else {
        cITSAudioEvent *const e = NewEvent(kPlayEvent);
        e->SetUint32(kInstanceKey, instance);
        e->SetUint32(kHighKey, high);
        e->SetUint32(kFlagKey, static_cast<uint8_t>(flag));
        PostEvent(e);
    }
    return true;
}

// 0x228564: true with a renderer, whatever the worker answers.
bool cTSAudioAmbienceManager::StopSound(uint32_t instance, uint32_t high)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) {
        DoStop(instance, high);
    } else {
        cITSAudioEvent *const e = NewEvent(kStopEvent);
        e->SetUint32(kInstanceKey, instance);
        e->SetUint32(kHighKey, high);
        PostEvent(e);
    }
    return true;
}

// 0x228224
void cTSAudioAmbienceManager::Slot10(uint32_t instance, uint32_t high, float value)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot10(instance, high, value);
    cITSAudioEvent *const e = NewEvent(kSlot10Event);
    e->SetUint32(kInstanceKey, instance);
    // ORIGINAL-BUG: the second word goes under the instance key too (overwriting the instance); the handler then reads
    // the second word as the instance and finds no second word (0).
    e->SetUint32(kInstanceKey, high);
    e->SetFloat(kValue1011Key, value);
    PostEvent(e);
}

// 0x228310
void cTSAudioAmbienceManager::Slot11(uint32_t instance, uint32_t high, float value)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot11(instance, high, value);
    cITSAudioEvent *const e = NewEvent(kSlot11Event);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    e->SetFloat(kValue1011Key, value);
    PostEvent(e);
}

// 0x228066
void cTSAudioAmbienceManager::Slot12(uint32_t instance, uint32_t high, float value)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot12(instance, high, value);
    cITSAudioEvent *const e = NewEvent(kSlot12Event);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    e->SetFloat(kValue12Key, value);
    PostEvent(e);
}

// 0x227f5d: `count` points of three floats each.
void cTSAudioAmbienceManager::Slot13(uint32_t instance, uint32_t high, const float *points, uint32_t count)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot13(instance, high, points, count);
    cITSAudioEvent *const e = NewEvent(kSlot13Event);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    e->SetUint32(kCountKey, count);
    for (uint32_t i = 0; i < count; ++i) e->SetVector3(kFirstPointKey + i, points + 3 * i);
    PostEvent(e);
}

// 0x227e4b
void cTSAudioAmbienceManager::Slot14(uint32_t instance, uint32_t high, float a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot14(instance, high, a, b);
    cITSAudioEvent *const e = NewEvent(kSlot14Event);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    e->SetFloat(kSlot14AKey, a);
    e->SetFloat(kSlot14BKey, b);
    PostEvent(e);
}

// 0x227d5c
void cTSAudioAmbienceManager::Slot15(uint32_t instance, uint32_t high, float value)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot15(instance, high, value);
    cITSAudioEvent *const e = NewEvent(kSlot15Event);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    e->SetFloat(kValue1516Key, value);
    PostEvent(e);
}

// 0x227c6d
void cTSAudioAmbienceManager::Slot16(uint32_t instance, uint32_t high, float value)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return DoSlot16(instance, high, value);
    cITSAudioEvent *const e = NewEvent(kSlot16Event);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kHighKey, high);
    e->SetFloat(kValue1516Key, value);
    PostEvent(e);
}

// ---- the workers ----

// 0x22887c: the sound loaded under `instance` (the second word is not compared) queried for its interface.
bool cTSAudioAmbienceManager::FindSound(uint32_t instance, uint32_t, void **out)
{
    const auto it = mSounds.find(instance);
    if (it == mSounds.end()) return false;
    return it->second.sound->QueryInterface(kSoundIid, out);
}

// 0x228bb5
bool cTSAudioAmbienceManager::DoIsLoaded(uint32_t instance, uint32_t high)
{
    AudioRef sound;
    return FindSound(instance, high, sound.AsPPVoid());
}

// 0x228bfe: true when loaded already; else a new sound object loaded from {0x2026960b, group, instance, high} and kept.
bool cTSAudioAmbienceManager::DoLoad(uint32_t group, uint32_t instance, uint32_t high)
{
    if (DoIsLoaded(instance, high)) return true;
    bool loaded = false;
    AudioRef sound;
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (static_cast<uint8_t>(com->GetClassObject(kSoundClass, kSoundIid, sound.AsPPVoid()))) {
        const uint32_t key[4] = {kSoundResourceType, group, instance, high};
        if (RawSlot<3, bool>(sound.Get(), key)) loaded = AddSound(instance, high, sound.Get());
    }
    return loaded;
}

// 0x2287b5: the sound's slot 35, then the map's entry for `instance` (new or replaced) set to it.
bool cTSAudioAmbienceManager::AddSound(uint32_t instance, uint32_t high, cIGZUnknown *sound)
{
    if (!sound) return false;
    RawSlot<35>(sound);
    AmbientSound entry;
    entry.instance = instance;
    entry.high = high;
    entry.sound.Assign(sound);
    mSounds[instance] = entry;
    return true;
}

// 0x2290d3: the sound's slot 7, then its entry erased.
bool cTSAudioAmbienceManager::DoUnload(uint32_t instance, uint32_t)
{
    const auto it = mSounds.find(instance);
    const bool found = it != mSounds.end();
    if (found) {
        RawSlot<7>(it->second.sound.Get());
        mSounds.erase(it);
    }
    return found;
}

// 0x22909b: from the first, each sound stopped, its slot 7, its entry erased.
void cTSAudioAmbienceManager::DoUnloadAll()
{
    while (!mSounds.empty()) {
        const auto it = mSounds.begin();
        DoStop(it->second.instance, it->second.high);
        RawSlot<7>(it->second.sound.Get());
        mSounds.erase(it);
    }
}

// 0x228cc0: the sound's slot 36 (this external face, the sound), slot 28 (the flag), then slot 8's answer.
bool cTSAudioAmbienceManager::DoPlay(uint32_t instance, uint32_t high, uint32_t flag)
{
    bool played = false;
    AudioRef sound;
    if (FindSound(instance, high, sound.AsPPVoid())) {
        RawSlot<36>(sound.Get(), External(), sound.Get());
        RawSlot<28>(sound.Get(), flag);
        played = RawSlot<8, bool>(sound.Get());
    }
    return played;
}

// 0x229039: the sound's slot 10, then slot 36 with nothing.
bool cTSAudioAmbienceManager::DoStop(uint32_t instance, uint32_t high)
{
    AudioRef sound;
    const bool found = FindSound(instance, high, sound.AsPPVoid());
    if (found) {
        RawSlot<10>(sound.Get());
        RawSlot<36>(sound.Get(), static_cast<void *>(nullptr), static_cast<void *>(nullptr));
    }
    return found;
}

// 0x228f77
void cTSAudioAmbienceManager::DoSlot10(uint32_t instance, uint32_t high, float value)
{
    AudioRef sound;
    if (FindSound(instance, high, sound.AsPPVoid())) RawSlot<14>(sound.Get(), static_cast<int32_t>(value * 10.0f));
}

// 0x228fd8
void cTSAudioAmbienceManager::DoSlot11(uint32_t instance, uint32_t high, float value)
{
    AudioRef sound;
    if (FindSound(instance, high, sound.AsPPVoid())) RawSlot<13>(sound.Get(), static_cast<int32_t>(value * 10.0f));
}

// 0x228f16
void cTSAudioAmbienceManager::DoSlot12(uint32_t instance, uint32_t high, float value)
{
    AudioRef sound;
    if (FindSound(instance, high, sound.AsPPVoid())) RawSlot<11>(sound.Get(), static_cast<int32_t>(value * 1024.0f));
}

// 0x228e3d: grid cell i (column i % 3, row i / 3) on (slot 32) with point i (slot 33, truncated) for each point, the
// cells after them up to 9 off.
void cTSAudioAmbienceManager::DoSlot13(uint32_t instance, uint32_t high, const float *points, uint32_t count)
{
    AudioRef sound;
    if (!FindSound(instance, high, sound.AsPPVoid())) return;
    uint32_t i = 0;
    for (; i < count; ++i, points += 3) {
        RawSlot<32>(sound.Get(), i % 3, i / 3, 1u);
        RawSlot<33>(sound.Get(), i % 3, i / 3, static_cast<int32_t>(points[0]), static_cast<int32_t>(points[1]),
                    static_cast<int32_t>(points[2]));
    }
    for (; i < kMaxPoints; ++i) RawSlot<32>(sound.Get(), i % 3, i / 3, 0u);
}

// 0x228dd2: both values through a 64-bit truncation (_ftol3), the low words to the sound's slot 29.
void cTSAudioAmbienceManager::DoSlot14(uint32_t instance, uint32_t high, float a, float b)
{
    AudioRef sound;
    if (FindSound(instance, high, sound.AsPPVoid()))
        RawSlot<29>(sound.Get(), static_cast<uint32_t>(static_cast<int64_t>(a)), static_cast<uint32_t>(static_cast<int64_t>(b)));
}

// 0x228d86: the value kept on the entry and handed to the sound manager (its slot 48) with the sound.
void cTSAudioAmbienceManager::DoSlot15(uint32_t instance, uint32_t, float value)
{
    const auto it = mSounds.find(instance);
    if (it == mSounds.end()) return;
    it->second.value15 = value;
    SoundManager()->Request48(Word(it->second.sound.Get()), value);
}

// 0x228d3a: as DoSlot15, the sound manager's slot 49.
void cTSAudioAmbienceManager::DoSlot16(uint32_t instance, uint32_t, float value)
{
    const auto it = mSounds.find(instance);
    if (it == mSounds.end()) return;
    it->second.value16 = value;
    SoundManager()->Request49(Word(it->second.sound.Get()), value);
}

// ---- cIGZSndEventHandler (+8) ----

// 0x2276c3: the event queried for cITSAudioEvent, dispatched on its id (the event's own slot 4); the workers that answer
// give the result, the others true; unknown ids, a null event or a failed query false.
bool cTSAudioAmbienceManager::HandleEvent(cIGZUnknown *event)
{
    bool handled = false;
    EventRef e;
    if (event && event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) {
        switch (RawSlot<4>(event)) {
        case kUnloadAllEvent: OnUnloadAllEvent(e.Get()); handled = true; break;
        case kIsLoadedEvent: handled = OnIsLoadedEvent(e.Get()); break;
        case kSlot14Event: OnSlot14Event(e.Get()); handled = true; break;
        case kSlot11Event: OnSlot11Event(e.Get()); handled = true; break;
        case kSlot12Event: OnSlot12Event(e.Get()); handled = true; break;
        case kPlayEvent: handled = OnPlayEvent(e.Get()); break;
        case kStopEvent: handled = OnStopEvent(e.Get()); break;
        case kSlot15Event: OnSlot15Event(e.Get()); handled = true; break;
        case kLoadEvent: handled = OnLoadEvent(e.Get()); break;
        case kSlot16Event: OnSlot16Event(e.Get()); handled = true; break;
        case kSlot10Event: OnSlot10Event(e.Get()); handled = true; break;
        case kUnloadEvent: handled = OnUnloadEvent(e.Get()); break;
        case kSlot13Event: OnSlot13Event(e.Get()); handled = true; break;
        default: break;
        }
    }
    return handled;
}

// The handlers read the event's values last argument first, as the original.

bool cTSAudioAmbienceManager::OnLoadEvent(cITSAudioEvent *event)
{
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    const uint32_t group = event->GetUint32(kGroupKey);
    return DoLoad(group, instance, high);
}

bool cTSAudioAmbienceManager::OnIsLoadedEvent(cITSAudioEvent *event)
{
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    return DoIsLoaded(instance, high);
}

bool cTSAudioAmbienceManager::OnUnloadEvent(cITSAudioEvent *event)
{
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    return DoUnload(instance, high);
}

bool cTSAudioAmbienceManager::OnPlayEvent(cITSAudioEvent *event)
{
    const uint32_t flag = event->GetUint32(kFlagKey) != 0;
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    return DoPlay(instance, high, flag);
}

bool cTSAudioAmbienceManager::OnStopEvent(cITSAudioEvent *event)
{
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    return DoStop(instance, high);
}

void cTSAudioAmbienceManager::OnSlot10Event(cITSAudioEvent *event)
{
    const float value = event->GetFloat(kValue1011Key);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot10(instance, high, value);
}

void cTSAudioAmbienceManager::OnSlot11Event(cITSAudioEvent *event)
{
    const float value = event->GetFloat(kValue1011Key);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot11(instance, high, value);
}

void cTSAudioAmbienceManager::OnSlot12Event(cITSAudioEvent *event)
{
    const float value = event->GetFloat(kValue12Key);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot12(instance, high, value);
}

void cTSAudioAmbienceManager::OnSlot13Event(cITSAudioEvent *event)
{
    // ORIGINAL-BUG: the points go into a 9-point buffer on the stack whatever the count says; a count above 9 overruns it
    // (in the original, onto its return address). Slot 13 never caps the count it posts.
    float points[kMaxPoints][3];
    const uint32_t count = event->GetUint32(kCountKey);
    for (uint32_t i = 0; i < count; ++i) event->GetVector3(kFirstPointKey + i, points[i]);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot13(instance, high, points[0], count);
}

void cTSAudioAmbienceManager::OnSlot14Event(cITSAudioEvent *event)
{
    const float b = event->GetFloat(kSlot14BKey);
    const float a = event->GetFloat(kSlot14AKey);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot14(instance, high, a, b);
}

void cTSAudioAmbienceManager::OnSlot15Event(cITSAudioEvent *event)
{
    const float value = event->GetFloat(kValue1516Key);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot15(instance, high, value);
}

void cTSAudioAmbienceManager::OnSlot16Event(cITSAudioEvent *event)
{
    const float value = event->GetFloat(kValue1516Key);
    const uint32_t high = event->GetUint32(kHighKey);
    const uint32_t instance = event->GetUint32(kInstanceKey);
    DoSlot16(instance, high, value);
}

// 0x228b7f: the event is not read.
void cTSAudioAmbienceManager::OnUnloadAllEvent(cITSAudioEvent *) { DoUnloadAll(); }

// ---- cIExternalSoundSystem (+0xc) ----

// 0x228450: a default event (id 0) with the three words and fixed values (and three more without `flag`'s low byte), the
// sound manager's slot 22 (b, c, d, the event), then its slot 26 (d, a, 2, 1).
bool cTSAudioAmbienceManager::ExternalSlot3(uint32_t a, uint32_t, uint32_t b, uint32_t c, uint32_t d, uint32_t flag,
                                            uint32_t, float value)
{
    cITSAudioEvent *const e = new cTSAudioEvent();
    if (e) e->AddRef();
    e->SetUint32(0xffaec905, b);
    e->SetUint32(0xff0d6bf5, c);
    e->SetUint32(0xff595ffd, 2000);
    e->SetUint32(0xffff4e79, static_cast<uint32_t>(static_cast<int64_t>(value)));
    e->SetUint32(0xff050a94, 0x20);
    if (!static_cast<uint8_t>(flag)) {
        e->SetUint32(0xffcc156f, 1);
        e->SetUint32(0xffaab623, 4);
        e->SetUint32(0xffa6a614, 0x40);
    }
    SoundManager()->Request22(b, c, d, e);
    SoundManager()->Request26(d, a, 2, 1);
    if (e) e->Release();
    return true;
}

// 0x228633
bool cTSAudioAmbienceManager::ExternalSlot4(uint32_t, uint32_t a, uint32_t)
{
    SoundManager()->Request27(a, 0);
    return true;
}

// 0x2281aa: the three floats as a position.
bool cTSAudioAmbienceManager::ExternalSlot5(uint32_t, uint32_t a, float x, float y, float z)
{
    const float position[3] = {x, y, z};
    SoundManager()->Request34(a, position);
    return true;
}

// 0x2281fa
bool cTSAudioAmbienceManager::ExternalSlot6(uint32_t, uint32_t a, float value)
{
    SoundManager()->Request36(a, value, 0.0f);
    return true;
}

// 0x228186
bool cTSAudioAmbienceManager::ExternalSlot7(uint32_t, uint32_t a, float value)
{
    SoundManager()->Request44(a, value);
    return true;
}

// 0x228155
bool cTSAudioAmbienceManager::ExternalSlot8(uint32_t, uint32_t a, float x, float y)
{
    SoundManager()->Request45(a, x, y);
    return true;
}

// 0x2279cf: the sound manager's answer.
bool cTSAudioAmbienceManager::ExternalSlot9(uint32_t, uint32_t a) { return SoundManager()->Query41(a); }
