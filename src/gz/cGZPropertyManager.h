#pragma once
#include "gz/cGZPropertySet.h"   // cGZPropertySetString, GZServiceString, cKeyStringTraits
#include "gz/cIGZMessageTarget2.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>

// The application's property manager (lane PU, milestone I): the service 0x0ac1887e (priority 0) that cTSAppFW registers.
// A string-keyed property set that also tells registered message targets when a property changes. RTTI
// cGZPropertyManagerT<cKeyStringTraitsT<ibasic_string<char>, 0x4bb0d269, 0x6bb22f38>, cGZPropertySetString>; bases
// cGZPropertySetString at +0, cIGZPropertyManagerT<string traits> at +0x24, cRZSystemService at +0x28 (its cRZUnknownMT at
// +0x2c). 0x4c bytes. Game vtables (1.18.0.187): 0xe98c60 (+0, 15 slots), 0xe98ca0 (+0x24, 7), 0xe98cc0 (+0x28, 13),
// 0xe98cf8 (+0x2c, 6); the constructor stores 0xe98c30 (the interface's own) at +0x24 first. Users: the settings readers
// (cTSAudioSoundManager, the CAS system, the window manager, LoadUIScript, COM directors, the language utility, the SG
// system) ask for the set (iid 0x4bb0d269) or for this face (0x6bb22f38) and watch properties through slot 4.

// cIGZPropertyManagerT<cKeyStringTraits> (interface id 0x6bb22f38; game vtable 0xe98c30, slots 3-6 _purecall).
template <class Traits> class cIGZPropertyManagerT;
using cIGZPropertyManagerString = cIGZPropertyManagerT<cKeyStringTraits>;

template <> class cIGZPropertyManagerT<cKeyStringTraits> : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x6bb22f38;
    // slot 3: the property set the manager is (the game's shared 4-byte body 0x47e380, `lea eax, [ecx - 0x24]`).
    virtual cIGZPropertySetString *GetPropertySet() = 0;
    // slot 4 (ret 0x10): watch `key` for `target` with messages of type `messageType`. A target is held once per key
    // (a second registration of the same target for the key is refused, whatever its type). On a new registration: when
    // the set has the key, `defaultValue` (if any) is offered to the set (kept only for a key without an entry) and the
    // target is sent the current value at once; otherwise a `defaultValue` is stored and every watcher of the key told.
    // Returns whether the target was added.
    virtual bool AddNotification(cIGZMessageTarget2 *target, uint32_t messageType, const GZServiceString *key,
                                 void *defaultValue) = 0;
    // slot 5 (ret 0xc): stop watching `key` for `target` (any message type: the type is not part of the match). True when
    // the key was watched by anyone.
    virtual bool RemoveNotification(cIGZMessageTarget2 *target, uint32_t messageType, const GZServiceString *key) = 0;
    // slot 6 (ret 4): stop every watch of `target`. True when there was one.
    virtual bool RemoveAllNotifications(cIGZMessageTarget2 *target) = 0;
};

// The original's template, by name (so our RTTI name is the original's).
template <class Traits, class Set> class cGZPropertyManagerT;
using cGZPropertyManagerString = cGZPropertyManagerT<cKeyStringTraits, cGZPropertySetString>;

namespace gzpm {
// One watcher of a key: a counted target (AddRef'd while held, released when dropped) and the message type it asked for.
// 8 bytes, the original's inner tree value (node +0x10 / +0x14).
struct Watcher {
    Watcher(cIGZMessageTarget2 *target, uint32_t messageType) : mTarget(target), mMessageType(messageType)
    {
        if (mTarget) mTarget->AddRef();
    }
    Watcher(const Watcher &other) : mTarget(other.mTarget), mMessageType(other.mMessageType)
    {
        if (mTarget) mTarget->AddRef();
    }
    Watcher &operator=(const Watcher &) = delete;
    ~Watcher()
    {
        if (mTarget) mTarget->Release(); // 0x6bfabd
    }

    cIGZMessageTarget2 *mTarget;
    uint32_t mMessageType;
};
static_assert(sizeof(Watcher) == 8);

// The watchers of a key are ordered by the target pointer alone (unsigned compare): the message type takes no part, so a
// target is held once per key.
struct WatcherLess {
    bool operator()(const Watcher &a, const Watcher &b) const { return std::less<const void *>()(a.mTarget, b.mTarget); }
};
using WatcherSet = std::set<Watcher, WatcherLess>;

// The keys compare case-insensitively (0xc0ece7: _stricmp of the two strings' text < 0).
struct KeyLess {
    bool operator()(const GZServiceString &a, const GZServiceString &b) const;
};
using WatchMap = std::map<GZServiceString, WatcherSet, KeyLess>;
static_assert(sizeof(WatchMap) == 8, "the map is a head pointer and a size");
} // namespace gzpm

// The class in the standalone shape (docs/standalone-shape.md). The game's constructor 0xc26045 is hooked to
// cGZPropertyManagerStringAdapters::Construct (placement new of this class), and our cTSAppFW builds it through that adapter:
// every manager carries our vtables. Nothing else in the image reaches the original's members (they are called only from
// its vtables and from each other), so they are in effect through that one hook.
template <>
class cGZPropertyManagerT<cKeyStringTraits, cGZPropertySetString> : public cGZPropertySetString,
                                                                     public cIGZPropertyManagerString,
                                                                     public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x4c;
    static constexpr uint32_t kVTable = 0xe98c60;          // +0x00 (the game's, for tests)
    static constexpr uint32_t kManagerVTable = 0xe98ca0;   // +0x24
    static constexpr uint32_t kSystemVTable = 0xe98cc0;    // +0x28
    static constexpr uint32_t kUnknownVTable = 0xe98cf8;   // +0x2c
    static constexpr uint32_t kInterfaceVTable = 0xe98c30; // the interface's own (stored at +0x24 during construction)
    static constexpr uint32_t kServiceId = 0x0ac1887e;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kManagerFace = 0x24;
    static constexpr uint32_t kSystemFace = 0x28;

    cGZPropertyManagerT() : rz::cRZSystemService(kServiceId, kPriority) {} // 0xc26045
    // 0xc26651: the watchers (each target released), the service part, then the set (key text, entries). Deleting: 0xc26790
    // (+0 slot 13), thunk 0xc26778 (+0x2c slot 3).
    ~cGZPropertyManagerT() override = default;
    cGZPropertyManagerT(const cGZPropertyManagerT &) = delete;
    cGZPropertyManagerT &operator=(const cGZPropertyManagerT &) = delete;

    // cIGZUnknown, every face (the +0x24 / +0x28 / +0x2c ones through the compiler's thunks: the game's 0xc297e2 / 0xc297ea /
    // 0xc297f2, AddRef shared_vf_47e370 / 0xc274ae / shared_vf_b28835, Release 0xc2987a / 0xc29882 / 0xc2988a).
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc29798: this face, else the set's, else the service's
    uint32_t AddRef() override;  // the set's count (+4): game shared_vf_146a50 (jmp to the set's AddRef 0x148fb0)
    uint32_t Release() override; // the set's (shared_vf_24284d, jmp to 0x1bf485): deletes at a count of 1 or below

    // cIGZPropertySetString: only SetProperty differs from the set's.
    bool SetProperty(const GZServiceString *key, void *value) override; // 0xc299e2: the set's, then the watchers told

    // cIGZPropertyManagerString
    cIGZPropertySetString *GetPropertySet() override; // shared 0x47e380 (4 bytes)
    bool AddNotification(cIGZMessageTarget2 *target, uint32_t messageType, const GZServiceString *key,
                         void *defaultValue) override;                                                            // 0xc272c0
    bool RemoveNotification(cIGZMessageTarget2 *target, uint32_t messageType, const GZServiceString *key) override; // 0xc2994a
    bool RemoveAllNotifications(cIGZMessageTarget2 *target) override;                                             // 0xc298bf

    // cIGZSystemService (the rest are cRZSystemService's)
    bool Shutdown() override; // 0xc2a1cc: every watch dropped, the set cleared (slot 5); true

    // 0xc28d0f: every watcher of `key` sent the key and its current value (one message, its type set per watcher).
    void NotifyWatchers(const GZServiceString *key);
    // 0x1bf595: `value` stored for `key` only when the set has no entry for it (the key text is interned either way).
    void SetDefault(const GZServiceString *key, void *value);
    // 0x1bf539: the entry-table half of SetDefault (by key offset).
    void AddEntryIfAbsent(const uint32_t *keyOffset, void *value);

    gzpm::WatchMap mWatchers; // +0x44 key -> its watchers
};
static_assert(sizeof(cGZPropertyManagerString) == cGZPropertyManagerString::kSize, "cGZPropertyManagerString size");
static_assert(offsetof(cGZPropertyManagerString, mServiceId) == 0x34, "cGZPropertyManagerString layout: the service part at +0x28");
static_assert(offsetof(cGZPropertyManagerString, mWatchers) == 0x44, "cGZPropertyManagerString layout");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx. The image's one caller is
// cTSAppFW::AddApplicationServicesHere (ours), which calls this directly.
struct cGZPropertyManagerStringAdapters {
    static cGZPropertyManagerString *__fastcall Construct(void *memory, void *edx); // 0xc26045
};
