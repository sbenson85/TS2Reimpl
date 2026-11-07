// cGZSndSys (lane KK): the whole class in the standalone shape (src/gz/cGZSndSys.h).
#include "gz/cGZSndSys.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZMessageQueue.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZMessageServer.h"
#include "rz/cRZStringSub.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StopwatchMore.h"
#include "rz/StringKeyParser.h"  // cRZStringHelpers::LowerCase
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZIniFile.h"       // cRZIniStringHelpers::TrimBoth

#include <intrin.h>
#include <new>
#include <tuple>
#include <utility>

namespace {

constexpr uint32_t kEventType = 0xdb53383b; // the sound event message: its notifications and its type
constexpr uint32_t kEventIid = 0x5b523ba4;  // the sound event's own interface (slot 6: its data)
constexpr uint32_t kSoundIid = 0x206077d2;  // a sound, asked of the update targets
constexpr uint32_t kFastPeriod = 0x14;      // AddUpdateTarget's 20 ms list; anything else is the 250 ms list
constexpr uint32_t kSlowPeriod = 0xfa;
constexpr uint32_t kSlowInterval = 0xfa;    // the 250 ms list runs once the clock passed this
constexpr uint32_t kLastTickRva = 0x10fad88; // TimerTick's last time

// The cached message server (0x9c5a89, rz/RZMessageServer.h).
cIGZUnknown *MessageServer() { return static_cast<cIGZUnknown *>(GetCachedMessageServer()); }

// The framework's slot 5 into a reference released at scope end (0x20dafa, the ScopedRef the original uses).
struct ServiceRef {
    void *p = nullptr;
    ServiceRef(uint32_t id, uint32_t iid)
    {
        if (void *const service = GetServiceObject()) As<ifaces::cIGZFrameWork>(service)->GetSystemService(id, iid, &p);
    }
    ServiceRef(const ServiceRef &) = delete;
    ServiceRef &operator=(const ServiceRef &) = delete;
    ~ServiceRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
};
constexpr uint32_t kNameServiceId = 0x2073215d, kNameServiceIid = 0xe0203660; // its slot 7 is the default name

// Holds a lock (its slots 1 / 2) for the scope.
struct Locked {
    ifaces::cIGZCriticalSection *lock;
    explicit Locked(cRZCriticalSection &l) : lock(As<ifaces::cIGZCriticalSection>(&l)) { lock->Lock(); }
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;
    ~Locked() { lock->Unlock(); }
};

// Raises a busy count (interlocked) for the scope.
struct Busy {
    volatile long &count;
    explicit Busy(volatile long &c) : count(c) { _InterlockedIncrement(&count); }
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;
    ~Busy() { _InterlockedDecrement(&count); }
};

// The sound event (0xb286d4 / 0xb28731, 0x38 bytes from the fixed allocator 0xb2878d): original callees.
void *NewEventMemory() { return OriginalFunction<void *(__cdecl *)(uint32_t), 0xb2878d>()(0x38); }
cIGZUnknown *ConstructEvent(void *memory, uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    using Fn = cIGZUnknown *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return OriginalFunction<Fn, 0xb286d4>()(memory, type, d1, d2, d3, d4);
}
uint8_t *ConstructEmptyEvent(void *memory) { return OriginalFunction<uint8_t *(__thiscall *)(void *), 0xb28731>()(memory); }
constexpr uint32_t kEventMessageFace = 0x2c; // the empty event's face CreateEmptyEvent asks

// The event interface's slot 6: its message data.
cIGZMessage2Standard *EventData(cIGZUnknown *event) { return RawSlot<6, cIGZMessage2Standard *>(event); }

// The message queue (0xc66b0d, 0x20 bytes): slot 3 queues a message, slot 18 takes the next one (false when empty).
void Enqueue(cIGZUnknown *queue, cIGZMessage2Standard *message) { RawSlot<3, void>(queue, message); }
bool Dequeue(cIGZUnknown *queue, cIGZUnknown **out) { return RawSlot<18, bool>(queue, out); }

// The callback timer (cRZCallbackTimer, 0x9ccf32, 0x28 bytes; slot 0 its deleting destructor).
void *ConstructTimer(void *memory) { return static_cast<cRZCallbackTimer *>(memory)->Construct(); }

// A sound's slot 9 (stop) and slot 12 (its id).
void StopSound(cIGZUnknown *sound) { RawSlot<9, void>(sound); }
uint32_t SoundId(cIGZUnknown *sound) { return RawSlot<12, uint32_t>(sound); }

// The cRZString's Right(count) (0x1ad3ed, the result built in `out`).
void RightOf(cRZString *self, cRZString *out, uint32_t count)
{
    Sub(*self).Right(out, count);
}
// The stopwatch's restart (0x1c95ec: start = now, the count cleared): an original callee.
void Restart(cRZStopwatch *clock) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(clock); }

// A name as slots 23 / 24 key it: lowercased, trimmed, a leading '.' dropped.
void NormaliseName(SndSysString &key, const cIGZString *name)
{
    key.s.Copy(*name);
    reinterpret_cast<cRZStringHelpers *>(&key.s)->LowerCase();
    reinterpret_cast<cRZIniStringHelpers *>(&key.s)->TrimBoth();
    if (key.s.mString.c_str()[0] == '.') {
        union Result {
            cRZString s;
            Result() {}
            ~Result() { s.Destruct(); }
        } right;
        RightOf(&key.s, &right.s, static_cast<uint32_t>(key.s.mString.size()) - 1);
        key.s.mString = right.s.mString;
    }
}

} // namespace

// ---- construction ----

// 0xb22fd5: the members in order, then the default name (the name service's slot 7) through SetName.
cGZSndSys::cGZSndSys()
{
    ServiceRef service(kNameServiceId, kNameServiceIid);
    if (service.p) {
        SndSysString text(RawSlot<7, const char *>(service.p));
        cGZSndSys::SetName(*text.s.AsInterface());
    }
}

// 0xb231cd: updates off, then the members in reverse order.
cGZSndSys::~cGZSndSys() noexcept { cGZSndSys::DisableUpdates(); }

// 0xb22378.
cGZSndSys *__cdecl cGZSndSys::Create() { return new cGZSndSys; }

cGZSndSys *__fastcall cGZSndSysAdapters::Construct(void *memory, void *) { return ::new (memory) cGZSndSys; }

// ---- cIGZUnknown ----

// 0xb241e1: 1 and 0x106077d2 -> this, the four faces by their iids; counted through AddRef. No null test on the out.
bool cGZSndSys::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1: *out = static_cast<cIGZSndSys *>(this); break;
    case 0x452294aa: *out = static_cast<cIGZMessageTarget2 *>(this); break;
    case cIGZSndSys::kIid: *out = static_cast<cIGZSndSys *>(this); break;
    case cIGZSnd3DListener::kIid: *out = static_cast<cIGZSnd3DListener *>(this); break;
    case cIGZSnd3DListenerEx::kIid: *out = static_cast<cIGZSnd3DListenerEx *>(this); break;
    case cIGZSystemService::kIID: *out = static_cast<cIGZSystemService *>(this); break;
    default: return false;
    }
    AddRef();
    return true;
}

// Folded 0x2296a0: not interlocked.
uint32_t cGZSndSys::AddRef() { return ++mRefCount; }

// 0xb24435: the last reference (a count of 1 or 0) deletes the system and answers 0.
uint32_t cGZSndSys::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// ---- cIGZSndSys: faces ----

cIGZSnd3DListener *cGZSndSys::AsListener() { return static_cast<cIGZSnd3DListener *>(this); }
cIGZSnd3DListenerEx *cGZSndSys::AsListenerEx() { return static_cast<cIGZSnd3DListenerEx *>(this); }

// ---- cIGZSndSys: events ----

// 0xb234b4: under the lock, an event with the text (its data's SetString), dispatched at once.
bool cGZSndSys::SendEventText(uint32_t type, const cIGZString *text, uint32_t, uint32_t, uint32_t, uint32_t)
{
    Locked hold(mLock);
    bool handled = false;
    cIGZUnknown *event = nullptr;
    if (CreateEvent(type, kEventIid, reinterpret_cast<void **>(&event), 0, 0, 0, 0)) {
        if (!event) return false;
        EventData(event)->SetString(text);
        handled = DispatchEvent(event);
    }
    if (event) event->Release();
    return handled;
}

// 0xb23525: the same with an object and four data words.
bool cGZSndSys::SendEventObject(uint32_t type, cIGZUnknown *object, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    Locked hold(mLock);
    bool handled = false;
    cIGZUnknown *event = nullptr;
    if (CreateEvent(type, kEventIid, reinterpret_cast<void **>(&event), 0, 0, 0, 0)) {
        if (!event) return false;
        EventData(event)->SetIGZUnknown(object);
        EventData(event)->SetData1(d1);
        EventData(event)->SetData2(d2);
        EventData(event)->SetData3(d3);
        EventData(event)->SetData4(d4);
        handled = DispatchEvent(event);
    }
    if (event) event->Release();
    return handled;
}

// 0xb2340d: the same with four data words.
bool cGZSndSys::SendEvent(uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    Locked hold(mLock);
    bool handled = false;
    cIGZUnknown *event = nullptr;
    if (CreateEvent(type, kEventIid, reinterpret_cast<void **>(&event), 0, 0, 0, 0)) {
        if (!event) return false;
        EventData(event)->SetData1(d1);
        EventData(event)->SetData2(d2);
        EventData(event)->SetData3(d3);
        EventData(event)->SetData4(d4);
        handled = DispatchEvent(event);
    }
    if (event) event->Release();
    return handled;
}

// 0xb235e1: the handlers in turn until one handles it; after the walk the pending additions and removals, then the held
// objects are let go.
bool cGZSndSys::DispatchEvent(cIGZUnknown *event)
{
    Locked hold(mLock);
    Locked holdHandlers(mHandlersLock);
    _InterlockedIncrement(&mHandlersBusy);
    bool handled = false;
    for (const AudioRef &handler : mHandlers) {
        if (static_cast<cIGZSndEventHandler *>(handler.Get())->HandleEvent(event)) {
            handled = true;
            break;
        }
    }
    if (_InterlockedDecrement(&mHandlersBusy), mHandlersBusy == 0) {
        Busy flushing(mHandlersFlushing);
        while (!mPendingHandlers.empty()) {
            AddEventHandler(static_cast<cIGZSndEventHandler *>(mPendingHandlers.front().Get()));
            mPendingHandlers.erase(mPendingHandlers.begin());
        }
        while (!mPendingHandlerRemovals.empty()) {
            RemoveEventHandler(static_cast<cIGZSndEventHandler *>(mPendingHandlerRemovals.front().Get()));
            mPendingHandlerRemovals.erase(mPendingHandlerRemovals.begin());
        }
    }
    {
        Locked holdHeld(mHeldLock);
        Busy busy(mHeldBusy);
        mHeld.clear();
    }
    return handled;
}

// 0xb240a9: an event with the text, queued for the update.
bool cGZSndSys::PostEventText(uint32_t type, const cIGZString *text, uint32_t, uint32_t, uint32_t, uint32_t)
{
    bool queued = false;
    cIGZUnknown *event = nullptr;
    if (CreateEvent(type, kEventIid, reinterpret_cast<void **>(&event), 0, 0, 0, 0)) {
        if (!event) return false;
        EventData(event)->SetString(text);
        queued = QueueEvent(event);
    }
    if (event) event->Release();
    return queued;
}

// 0xb24107.
bool cGZSndSys::PostEventObject(uint32_t type, cIGZUnknown *object, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    bool queued = false;
    cIGZUnknown *event = nullptr;
    if (CreateEvent(type, kEventIid, reinterpret_cast<void **>(&event), 0, 0, 0, 0)) {
        if (!event) return false;
        EventData(event)->SetIGZUnknown(object);
        EventData(event)->SetData1(d1);
        EventData(event)->SetData2(d2);
        EventData(event)->SetData3(d3);
        EventData(event)->SetData4(d4);
        queued = QueueEvent(event);
    }
    if (event) event->Release();
    return queued;
}

// 0xb24015.
bool cGZSndSys::PostEvent(uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    bool queued = false;
    cIGZUnknown *event = nullptr;
    if (CreateEvent(type, kEventIid, reinterpret_cast<void **>(&event), 0, 0, 0, 0)) {
        if (!event) return false;
        EventData(event)->SetData1(d1);
        EventData(event)->SetData2(d2);
        EventData(event)->SetData3(d3);
        EventData(event)->SetData4(d4);
        queued = QueueEvent(event);
    }
    if (event) event->Release();
    return queued;
}

// 0xb241b0: the event's data on the queue (used unchecked: Init makes the queue). Always true.
bool cGZSndSys::QueueEvent(cIGZUnknown *event)
{
    event->AddRef();
    cIGZUnknown *const queue = mQueue.Get();
    Enqueue(queue, EventData(event));
    event->Release();
    return true;
}

// 0xb23846: a new event (type, four words) asked for `iid`. The event is used unchecked (a failed allocation crashes).
bool cGZSndSys::CreateEvent(uint32_t type, uint32_t iid, void **out, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    void *const memory = NewEventMemory();
    cIGZUnknown *event = nullptr;
    if (memory) {
        event = ConstructEvent(memory, type, d1, d2, d3, d4);
        if (event) event->AddRef();
    }
    const bool ok = event->QueryInterface(iid, out);
    if (event) event->Release();
    return ok;
}

// 0xb2389f: a new empty event; its +0x2c face is asked for `iid` (unchecked, as CreateEvent).
bool cGZSndSys::CreateEmptyEvent(uint32_t iid, void **out)
{
    void *const memory = NewEventMemory();
    uint8_t *const event = memory ? ConstructEmptyEvent(memory) : nullptr;
    auto *const face = event ? reinterpret_cast<cIGZUnknown *>(event + kEventMessageFace) : nullptr;
    if (face) face->AddRef();
    const bool ok = face->QueryInterface(iid, out);
    if (face) face->Release();
    return ok;
}

// 0xb2427d: at the front (once: removed first) unless a dispatch is walking the handlers, then at the front of the pending. Always true.
bool cGZSndSys::AddEventHandler(cIGZSndEventHandler *handler)
{
    Locked hold(mHandlersLock);
    if (mHandlersBusy != 0) {
        AudioRef ref(handler);
        mPendingHandlers.push_front(ref);
    } else {
        RemoveEventHandler(handler);
        AudioRef ref(handler);
        mHandlers.push_front(ref);
    }
    return true;
}

// 0xb24ed7: the first entry holding it, or pending during a dispatch. Always true.
bool cGZSndSys::RemoveEventHandler(cIGZSndEventHandler *handler)
{
    Locked hold(mHandlersLock);
    if (mHandlersBusy == 0) {
        for (auto it = mHandlers.begin(); it != mHandlers.end(); ++it) {
            if (it->Get() == handler) {
                mHandlers.erase(it);
                break;
            }
        }
    } else {
        AudioRef ref(handler);
        mPendingHandlerRemovals.push_back(ref);
    }
    return true;
}

// 0xb2506e: while running, the queued events to DoMessage, then the update targets (UpdateOne) with the elapsed time.
bool cGZSndSys::Update(uint32_t elapsed)
{
    Locked hold(mLock);
    if (!IsRunning()) return false;
    cIGZUnknown *message = nullptr;
    while (Dequeue(mQueue.Get(), &message)) {
        static_cast<cIGZMessageTarget2 *>(this)->DoMessage(message);
        message->Release();
    }
    uint32_t unused;
    CallUpdateTargets(&UpdateOne, elapsed, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&unused)));
    return true;
}

// 0xb24678: the name copied (the string's slot 17).
void cGZSndSys::SetName(const cIGZString &name)
{
    mName.s.AsInterface()->Copy(name);
    mNameSet = true;
}

// ---- cIGZSndSys: named values ----

// 0xb247b5: false for a name empty after normalising.
bool cGZSndSys::SetNamedValue(const cIGZString *name, uint32_t value)
{
    SndSysString key;
    NormaliseName(key, name);
    if (key.s.mString.size() == 0) return false;
    auto it = mNamedValues.lower_bound(key.s);
    if (it == mNamedValues.end() || SndSysNameLess{}(key.s, it->first))
        it = mNamedValues.emplace_hint(it, std::piecewise_construct, std::forward_as_tuple(key), std::tuple<>());
    it->second = value;
    return true;
}

// 0xb23a10.
bool cGZSndSys::GetNamedValue(const cIGZString *name, uint32_t *out)
{
    SndSysString key;
    NormaliseName(key, name);
    const auto it = mNamedValues.find(key.s);
    if (it == mNamedValues.end()) return false;
    *out = it->second;
    return true;
}

// 0xb23ab4: the value at an index in name order.
bool cGZSndSys::GetNamedValueAt(uint32_t index, uint32_t *out)
{
    auto it = mNamedValues.begin();
    for (; index != 0; --index) {
        if (it == mNamedValues.end()) return false;
        ++it;
    }
    if (it == mNamedValues.end()) return false;
    *out = it->second;
    return true;
}

// 0xb23af8: the name `skip` entries past the given one (looked up as given, not normalised), copied into it.
bool cGZSndSys::GetNameAfter(uint32_t skip, cIGZString *name)
{
    auto it = [&] {
        SndSysString key(*name);
        return mNamedValues.find(key.s);
    }();
    for (; skip != 0; --skip) {
        if (it == mNamedValues.end()) return false;
        ++it;
    }
    if (it == mNamedValues.end()) return false;
    name->Copy(*it->first.s.AsInterface());
    return true;
}

// ---- cIGZSndSys: renderers and update targets ----

// 0xb24865: the level to every renderer (its slot 3). Always true.
bool cGZSndSys::ApplyLevel(uint32_t)
{
    Locked hold(mRenderersLock);
    Busy busy(mRenderersBusy);
    for (const AudioRef &renderer : mRenderers) static_cast<cIGZSndRenderer *>(renderer.Get())->SetLevel(mLevel);
    return true;
}

// 0xb24379: at the back of the 20 ms list (period 20) or the 250 ms one (once: removed first), or at the back of the pending list during a walk.
bool cGZSndSys::AddUpdateTarget(cIGZSndUpdateTarget *target, uint32_t period)
{
    Locked hold(mTargetsLock);
    if (mTargetsBusy == 0) {
        RemoveUpdateTarget(target);
        AudioRef ref(target);
        (period == kFastPeriod ? mFastTargets : mSlowTargets).push_back(ref);
    } else {
        AudioRef ref(target);
        (period == kFastPeriod ? mPendingFast : mPendingSlow).push_back(ref);
    }
    return true;
}

// 0xb24fc0: the first entry holding it in each list, or pending during a walk; nothing once Shutdown began. Always true.
bool cGZSndSys::RemoveUpdateTarget(cIGZSndUpdateTarget *target)
{
    if (mShuttingDown) return true;
    Locked hold(mTargetsLock);
    if (mTargetsBusy == 0) {
        for (RefList *list : {&mFastTargets, &mSlowTargets}) {
            for (auto it = list->begin(); it != list->end(); ++it) {
                if (it->Get() == target) {
                    list->erase(it);
                    break;
                }
            }
        }
    } else {
        AudioRef ref(target);
        mPendingTargetRemovals.push_back(ref);
    }
    return true;
}

// 0xb237b5: fn on each 20 ms target until one answers false, then the same on the 250 ms ones; the last answer (true for
// none).
bool cGZSndSys::ForEachUpdateTarget(UpdateTargetFn fn, uint32_t a, uint32_t b)
{
    Locked hold(mTargetsLock);
    Busy busy(mTargetsBusy);
    bool result = true;
    for (const AudioRef &target : mFastTargets) {
        result = fn(target.Get(), a, b);
        if (!result) break;
    }
    for (const AudioRef &target : mSlowTargets) {
        result = fn(target.Get(), a, b);
        if (!result) break;
    }
    return result;
}

// 0xb23766 / 0xb23406.
bool cGZSndSys::EnableUpdates()
{
    mUpdatesEnabled = true;
    return true;
}
bool cGZSndSys::DisableUpdates()
{
    mUpdatesEnabled = false;
    return true;
}

// 0xb24307: at the back unless already there. Always true.
bool cGZSndSys::AddRenderer(cIGZSndRenderer *renderer)
{
    Locked hold(mRenderersLock);
    if (!HasRenderer(renderer)) {
        Busy busy(mRenderersBusy);
        AudioRef ref(renderer);
        mRenderers.push_back(ref);
    }
    return true;
}

// 0xb24f5d: the first entry holding it.
bool cGZSndSys::RemoveRenderer(cIGZSndRenderer *renderer)
{
    bool removed = false;
    Locked hold(mRenderersLock);
    Busy busy(mRenderersBusy);
    for (auto it = mRenderers.begin(); it != mRenderers.end(); ++it) {
        if (it->Get() == renderer) {
            mRenderers.erase(it);
            removed = true;
            break;
        }
    }
    return removed;
}

// 0xb23e70.
bool cGZSndSys::HasRenderer(cIGZSndRenderer *renderer)
{
    Locked hold(mRenderersLock);
    bool found = false;
    for (const AudioRef &entry : mRenderers)
        if (entry.Get() == renderer) found = true;
    return found;
}

// 0xb238ee: the update target's sound with the id (FindSound) asked for `iid`.
bool cGZSndSys::QuerySound(uint32_t id, uint32_t iid, void **out)
{
    cIGZUnknown *sound = nullptr;
    ForEachUpdateTarget(&FindSound, id, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sound)));
    if (!sound) return false;
    return sound->QueryInterface(iid, out);
}

// 0xb23eb5.
bool cGZSndSys::HasSound(uint32_t id)
{
    cIGZUnknown *sound = nullptr;
    ForEachUpdateTarget(&FindSound, id, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sound)));
    return sound != nullptr;
}

// 0xb23a08: used unchecked (only Init makes the stopwatch; Shutdown frees it).
uint32_t cGZSndSys::GetTime() { return mUptime->Elapsed(); }

// 0xb23c31 / 0xb23c05: the object, counted; whether there is one.
bool cGZSndSys::GetObject104Ref(cIGZUnknown **out)
{
    *out = mObject104.Get();
    if (mObject104) mObject104->AddRef();
    return mObject104.Get() != nullptr;
}
bool cGZSndSys::GetObject108Ref(cIGZUnknown **out)
{
    *out = mObject108.Get();
    if (mObject108) mObject108->AddRef();
    return mObject108.Get() != nullptr;
}

// 0xb2331b: kept (counted) until the end of the next dispatch. The object is used unchecked. Always true.
bool cGZSndSys::HoldUntilDispatched(cIGZUnknown *object)
{
    Locked hold(mHeldLock);
    Busy busy(mHeldBusy);
    object->AddRef();
    {
        AudioRef ref(object);
        mHeld.push_back(ref);
    }
    object->Release();
    return true;
}

// 0xb23edc: every update target's sound (0x206077d2) stopped (its slot 9), the 20 ms list's first.
void cGZSndSys::StopAllSounds()
{
    Locked hold(mLock);
    Locked holdTargets(mTargetsLock);
    _InterlockedIncrement(&mTargetsBusy);
    {
        std::list<cIGZUnknown *> sounds;
        for (const RefList *list : {&mFastTargets, &mSlowTargets}) {
            for (const AudioRef &target : *list) {
                cIGZUnknown *sound = nullptr;
                if (target->QueryInterface(kSoundIid, reinterpret_cast<void **>(&sound))) sounds.push_back(sound);
                if (sound) sound->Release();
            }
        }
        while (!sounds.empty()) {
            StopSound(sounds.front());
            sounds.pop_front();
        }
        _InterlockedDecrement(&mTargetsBusy);
    }
}

// 0xb24b35 / 0xb23c73.
void cGZSndSys::SetFlag14d(bool value) { mFlag14d = value; }
bool cGZSndSys::GetFlag14d() { return mFlag14d; }

// 0xb23c5d: the timer's slot 12 object's slot 7; 0 without a timer.
uint32_t cGZSndSys::GetTimerThreadSlot7()
{
    if (!mTimer) return 0;
    return RawSlot<7, uint32_t>(RawSlot<12, void *>(mTimer));
}

// 0xb24b42: the timer's slot 3. Without a timer the original returns eax as it was (undefined): 0 here.
uint32_t cGZSndSys::TimerSlot3(uint32_t value)
{
    if (!mTimer) return 0;
    return RawSlot<3, uint32_t>(mTimer, value);
}

// 0xb2485b.
void cGZSndSys::SetRunning(bool running) { mRunning = running; }

// ---- cIGZSystemService ----

// 0xb23bff (slots 5 and 12).
int32_t cGZSndSys::GetServicePriority() { return kPriority; }
int32_t cGZSndSys::GetServiceTickPriority() { return kPriority; }

// 0xb23ca1: under the lock: the default name unless one was set; the event notifications from the message server; when
// enabled and not yet running, the queue, the two director objects, the stopwatches started and the 20 ms timer with the
// tick (running when the timer started). Always true.
bool cGZSndSys::Init()
{
    Locked hold(mLock);
    if (!mNameSet) {
        ServiceRef service(kNameServiceId, kNameServiceIid);
        if (service.p) {
            SndSysString text(RawSlot<7, const char *>(service.p));
            SetName(*text.s.AsInterface());
        }
    }
    RawSlot<5, void>(MessageServer(), static_cast<cIGZMessageTarget2 *>(this), kEventType);
    if (IsEnabled() && !IsRunning()) {
        void *const memory = ::operator new(0x20);
        mQueue.Assign(memory ? static_cast<cIGZUnknown *>(cGZMessageQueueAdapters::Construct2(memory, nullptr)) : nullptr);
        {
            auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
            void **const slot = mObject104.AsPPVoid();
            com->GetClassObject(0x9d4c6b95, 0x5d4c6b90, slot);
        }
        {
            auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
            void **const slot = mObject108.AsPPVoid();
            com->GetClassObject(0xbdf613b4, 0x3df613a1, slot);
        }
        auto *const uptime = static_cast<cRZStopwatch *>(::operator new(sizeof(cRZStopwatch)));
        mUptime = uptime ? uptime->Construct(4) : nullptr;
        mUptime->Start();
        mClock.Start();
        void *const timerMemory = ::operator new(0x28);
        mTimer = timerMemory ? ConstructTimer(timerMemory) : nullptr;
        RawSlot<1, void>(mTimer, kFastPeriod);                                 // the period
        RawSlot<9, void>(mTimer, &TimerTick, static_cast<cIGZSndSys *>(this)); // the callback and its argument
        const bool started = RawSlot<5, bool>(mTimer, kFastPeriod, 0u);
        RawSlot<5, void>(RawSlot<12, void *>(mTimer), 4u);                     // the timer thread's priority
        SetRunning(started);
    }
    return true;
}

// 0xb24c1a: the timer stopped (and deleted), the notifications dropped, every update target's sound stopped, every list,
// the named values and the three objects emptied. Always true.
bool cGZSndSys::Shutdown()
{
    mShuttingDown = true;
    {
        Locked hold(mLock);
        if (mTimer) RawSlot<7, void>(mTimer);
        if (mUptime) stopwatch_more::Stop(mUptime, nullptr);
    }
    Locked hold(mLock);
    RawSlot<6, void>(MessageServer(), static_cast<cIGZMessageTarget2 *>(this), kEventType);
    if (mTimer) {
        RawSlot<7, void>(mTimer);
        if (mTimer) RawSlot<0, void>(mTimer, 1u);
        mTimer = nullptr;
    }
    if (mUptime) {
        stopwatch_more::Stop(mUptime, nullptr);
        if (cRZStopwatch *const uptime = mUptime) ::operator delete(uptime, sizeof(cRZStopwatch));
        mUptime = nullptr;
    }
    {
        Locked holdHandlers(mHandlersLock);
        Busy busy(mHandlersBusy);
        mHandlers.clear();
        mPendingHandlers.clear();
        mPendingHandlerRemovals.clear();
    }
    {
        Locked holdTargets(mTargetsLock);
        cIGZUnknown *sound = nullptr;
        {
            Busy busy(mTargetsBusy);
            for (const RefList *list : {&mFastTargets, &mSlowTargets}) {
                for (const AudioRef &target : *list) {
                    if (sound) {
                        cIGZUnknown *const old = sound;
                        sound = nullptr;
                        old->Release();
                    }
                    if (target->QueryInterface(kSoundIid, reinterpret_cast<void **>(&sound))) StopSound(sound);
                }
            }
        }
        mFastTargets.clear();
        mSlowTargets.clear();
        mPendingFast.clear();
        mPendingSlow.clear();
        mPendingTargetRemovals.clear();
        if (sound) sound->Release();
    }
    SetRunning(false);
    mFastTargets.clear();
    mSlowTargets.clear();
    mPendingFast.clear();
    mPendingSlow.clear();
    mPendingTargetRemovals.clear();
    mRenderers.clear();
    mHandlers.clear();
    mPendingHandlers.clear();
    mPendingHandlerRemovals.clear();
    mNamedValues.clear();
    mHeld.clear();
    mObject104.Assign(nullptr);
    mObject108.Assign(nullptr);
    mQueue.Assign(nullptr);
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0xb236f3: a sound event message (type 0xdb53383b) whose event interface answers is dispatched while there is a renderer
// (true without one).
bool cGZSndSys::DoMessage(void *message)
{
    auto *const msg = static_cast<cIGZMessage2 *>(message);
    if (msg->GetType() != kEventType) return false;
    cIGZUnknown *event = nullptr;
    bool result = msg->QueryInterface(kEventIid, reinterpret_cast<void **>(&event));
    if (result && event) {
        if (GetRendererCount() != 0) result = DispatchEvent(event);
    } else {
        result = false;
    }
    if (event) event->Release();
    return result;
}

// ---- the listener (+0xc): stored, then handed to every renderer's listener (0x406077d2) ----

namespace {

template <class Iface, class Fn> void ForEachRendererFace(cGZSndSys &sys, uint32_t iid, Fn &&fn)
{
    Locked hold(sys.mRenderersLock);
    Busy busy(sys.mRenderersBusy);
    for (const AudioRef &renderer : sys.mRenderers) {
        Iface *face = nullptr;
        if (renderer->QueryInterface(iid, reinterpret_cast<void **>(&face))) fn(face);
        if (face) face->Release();
    }
}

} // namespace

// 0xb23390.
void cGZSndSys::ListenerSlot5()
{
    ForEachRendererFace<cIGZSnd3DListener>(*this, cIGZSnd3DListener::kIid,
                                           [](cIGZSnd3DListener *l) { l->ListenerSlot5(); });
}

// 0xb23bc9.
void cGZSndSys::GetPosition(float *x, float *y, float *z)
{
    *x = mPosition[0];
    *y = mPosition[1];
    *z = mPosition[2];
}

// 0xb249d9.
bool cGZSndSys::SetPosition(float x, float y, float z)
{
    mPosition[0] = x;
    mPosition[1] = y;
    mPosition[2] = z;
    ForEachRendererFace<cIGZSnd3DListener>(*this, cIGZSnd3DListener::kIid,
                                           [&](cIGZSnd3DListener *l) { l->SetPosition(x, y, z); });
    return true;
}

// 0xb23b7c.
void cGZSndSys::GetOrientation(float *topX, float *topY, float *topZ, float *frontX, float *frontY, float *frontZ)
{
    *topX = mOrientation[0];
    *topY = mOrientation[1];
    *topZ = mOrientation[2];
    *frontX = mOrientation[3];
    *frontY = mOrientation[4];
    *frontZ = mOrientation[5];
}

// 0xb248ba.
bool cGZSndSys::SetOrientation(float topX, float topY, float topZ, float frontX, float frontY, float frontZ)
{
    mOrientation[0] = topX;
    mOrientation[1] = topY;
    mOrientation[2] = topZ;
    mOrientation[3] = frontX;
    mOrientation[4] = frontY;
    mOrientation[5] = frontZ;
    ForEachRendererFace<cIGZSnd3DListener>(*this, cIGZSnd3DListener::kIid, [&](cIGZSnd3DListener *l) {
        l->SetOrientation(topX, topY, topZ, frontX, frontY, frontZ);
    });
    return true;
}

// ---- the listener-ex (+0x10): the same with each renderer's listener-ex (0x5b30e7b3) ----

// 0xb23c7a.
void cGZSndSys::GetVector70(float *x, float *y, float *z)
{
    *x = mVector70[0];
    *y = mVector70[1];
    *z = mVector70[2];
}

// 0xb24b51.
bool cGZSndSys::SetVector70(float x, float y, float z)
{
    mVector70[0] = x;
    mVector70[1] = y;
    mVector70[2] = z;
    ForEachRendererFace<cIGZSnd3DListenerEx>(*this, cIGZSnd3DListenerEx::kIid,
                                             [&](cIGZSnd3DListenerEx *l) { l->SetVector70(x, y, z); });
    return true;
}

// 0xb2395b: position, vector, orientation (written in the order position, orientation, vector).
void cGZSndSys::GetAll(float *px, float *py, float *pz, float *vx, float *vy, float *vz, float *tx, float *ty, float *tz,
                       float *fx, float *fy, float *fz)
{
    *px = mPosition[0];
    *py = mPosition[1];
    *pz = mPosition[2];
    *tx = mOrientation[0];
    *ty = mOrientation[1];
    *tz = mOrientation[2];
    *fx = mOrientation[3];
    *fy = mOrientation[4];
    *fz = mOrientation[5];
    *vx = mVector70[0];
    *vy = mVector70[1];
    *vz = mVector70[2];
}

// 0xb2446f: hands every renderer the stored values (the listener-ex's vector when it has that face, then the listener's
// orientation and position). Always true.
// ORIGINAL-BUG: the twelve arguments are ignored; neither stored nor passed on (the renderers get the old values).
bool cGZSndSys::SetAll(float, float, float, float, float, float, float, float, float, float, float, float)
{
    const float px = mPosition[0], py = mPosition[1], pz = mPosition[2];
    const float tx = mOrientation[0], ty = mOrientation[1], tz = mOrientation[2];
    const float fx = mOrientation[3], fy = mOrientation[4], fz = mOrientation[5];
    const float vx = mVector70[0], vy = mVector70[1], vz = mVector70[2];
    Locked hold(mRenderersLock);
    Busy busy(mRenderersBusy);
    for (const AudioRef &renderer : mRenderers) {
        cIGZSnd3DListener *listener = nullptr;
        if (renderer->QueryInterface(cIGZSnd3DListener::kIid, reinterpret_cast<void **>(&listener))) {
            cIGZSnd3DListenerEx *ex = nullptr;
            if (renderer->QueryInterface(cIGZSnd3DListenerEx::kIid, reinterpret_cast<void **>(&ex)))
                ex->SetVector70(vx, vy, vz);
            listener->SetOrientation(tx, ty, tz, fx, fy, fz);
            listener->SetPosition(px, py, pz);
            if (ex) ex->Release();
        }
        if (listener) listener->Release();
    }
    return true;
}

// 0xb239f9 / 0xb24722.
void cGZSndSys::GetValueAc(float *out) { *out = mValueAc; }
bool cGZSndSys::SetValueAc(float value)
{
    mValueAc = value;
    ForEachRendererFace<cIGZSnd3DListenerEx>(*this, cIGZSnd3DListenerEx::kIid,
                                             [&](cIGZSnd3DListenerEx *l) { l->SetValueAc(value); });
    return true;
}

// 0xb23bf0 / 0xb24aa2.
void cGZSndSys::GetFactorLength(float *out) { *out = mFactor; }
bool cGZSndSys::SetAllFactors(float value)
{
    mFactor = value;
    ForEachRendererFace<cIGZSnd3DListenerEx>(*this, cIGZSnd3DListenerEx::kIid,
                                             [&](cIGZSnd3DListenerEx *l) { l->SetAllFactors(value); });
    return true;
}

// 0xb239ea / 0xb2468f.
void cGZSndSys::GetValueB0(float *out) { *out = mValueB0; }
bool cGZSndSys::SetValueB0(float value)
{
    mValueB0 = value;
    ForEachRendererFace<cIGZSnd3DListenerEx>(*this, cIGZSnd3DListenerEx::kIid,
                                             [&](cIGZSnd3DListenerEx *l) { l->SetValueB0(value); });
    return true;
}

// ---- non-virtual ----

// 0xb250f0.
bool cGZSndSys::CallUpdateTargets(UpdateTargetFn fn, uint32_t a, uint32_t b)
{
    if (!mUpdatesEnabled) return true;
    Locked hold(mTargetsLock);
    _InterlockedIncrement(&mTargetsBusy);
    for (const AudioRef &target : mFastTargets)
        if (!fn(target.Get(), a, b)) break;
    if (mClock.Elapsed() > kSlowInterval) {
        Restart(&mClock);
        for (const AudioRef &target : mSlowTargets)
            if (!fn(target.Get(), a, b)) break;
    }
    if (_InterlockedDecrement(&mTargetsBusy), mTargetsBusy == 0) {
        Busy flushing(mTargetsFlushing);
        while (!mPendingTargetRemovals.empty()) {
            RemoveUpdateTarget(static_cast<cIGZSndUpdateTarget *>(mPendingTargetRemovals.front().Get()));
            mPendingTargetRemovals.erase(mPendingTargetRemovals.begin());
        }
        while (!mPendingFast.empty()) {
            AddUpdateTarget(static_cast<cIGZSndUpdateTarget *>(mPendingFast.front().Get()), kFastPeriod);
            mPendingFast.erase(mPendingFast.begin());
        }
        while (!mPendingSlow.empty()) {
            AddUpdateTarget(static_cast<cIGZSndUpdateTarget *>(mPendingSlow.front().Get()), kSlowPeriod);
            mPendingSlow.erase(mPendingSlow.begin());
        }
    }
    return true;
}

// 0xb250e0.
bool __cdecl cGZSndSys::UpdateOne(cIGZUnknown *target, uint32_t elapsed, uint32_t)
{
    static_cast<cIGZSndUpdateTarget *>(target)->Update(elapsed);
    return true;
}

// 0xb2376d. `out` is a cIGZUnknown ** (ForEachUpdateTarget's second word).
// ORIGINAL-BUG: the sound is handed out after its reference is released (QuerySound then asks it unchecked).
bool __cdecl cGZSndSys::FindSound(cIGZUnknown *target, uint32_t id, uint32_t out)
{
    bool more = false;
    cIGZUnknown *sound = nullptr;
    if (target->QueryInterface(kSoundIid, reinterpret_cast<void **>(&sound)) && SoundId(sound) == id)
        *reinterpret_cast<cIGZUnknown **>(static_cast<uintptr_t>(out)) = sound;
    else
        more = true;
    if (sound) sound->Release();
    return more;
}

// 0xb23923.
void __cdecl cGZSndSys::TimerTick(cGZSndSys *self)
{
    const uint32_t now = self->GetTime();
    uint32_t &last = *GameAddress<uint32_t>(kLastTickRva);
    const uint32_t elapsed = now < last ? 0 : now - last;
    self->Update(elapsed);
    last = now;
}
