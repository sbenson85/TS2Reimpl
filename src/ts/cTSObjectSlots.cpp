// More primary-vtable slots of cTSObject (field map and conventions: src/ts/cTSObject.h). Slot and field names are tentative.
#include "ts/cTSObject.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZString.h"
#include "simantics/FindBestAction.h"
#include "simantics/TreeSim.h"
#include "ts/cTSObjectHeld.h"
#include "ts/cTSString.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

using namespace tsobject_held;

void cTSObject::vf276(uint32_t a)
{
    At<uint32_t>(this, 0x194) = a;
}

void cTSObject::vf205()
{
    At<int16_t>(this, 0x122) = 0;
    At<int16_t>(this, 0xe0) = -1;
    At<uint16_t>(this, 0xb6) &= 0xfedf;
    // Two dwords (0, -1 as 16-bit pairs), not an array: the original has no /GS cookie in this frame.
    struct {
        int32_t first, second;
    } pairs = {static_cast<int32_t>(0xffff0000), static_cast<int32_t>(0xffff0000)};
    VirtualCall<PairsFn, 197>(this, reinterpret_cast<int16_t *>(&pairs), 1, 0);
    // Arguments are evaluated right to left: the word at +0x1cc is read and pushed first, after the call above, and stays on
    // the stack as the third argument of StartTree (slot 93 takes one).
    At<cTSTreeSim>(this, 4).StartTree(VirtualCall<NoArgPtr, 89>(this), VirtualCall<OneArgRet32, 93>(this, 1) & 0xffff, At<uint16_t>(this, 0x1cc));
}

void cTSObject::vf223()
{
    if (static_cast<uint8_t>(VirtualCall<NoArgRet32, 219>(this)) != 0) return;
    if (At<uint8_t>(this, 0x292) != 0) return;
    At<uint8_t>(this, 0x292) = 1;
    At<uint8_t>(this, 0x2a6) = 1;
    if (VirtualCall<NoArgRet32, 71>(this) == 0) {
        VirtualCall<NoArgVoid, 331>(this);
        VirtualCall<NoArgVoid, 330>(this);
    }
    void *const context = At<void *>(this, 0x40);
    At<uint8_t>(this, 0x291) = 0;
    void *const target = VirtualCall<NoArgPtr, 18>(context);
    if (target) {
        // The original reads the target's vtable before asking for this object's id (slot 47), then calls through that copy.
        void *const *const targetVTable = *static_cast<void *const *const *>(target);
        const uint32_t id = static_cast<uint16_t>(VirtualCall<NoArgRet32, 47>(this));
        reinterpret_cast<OneArgVoid>(targetVTable[21])(target, id);
    }
    At<uint8_t>(this, 0x292) = 0;
}

uint8_t cTSObject::vf182(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    using FillFn = void(__thiscall *)(void *, HeldRefList *);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using HandleFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    constexpr uint32_t kHelperInterface = 0xaae90adb;

    HeldRef helper;
    HeldRefList list;
    void *const sub = &At<uint8_t>(this, 0x74);
    VirtualCall<FillFn, 10>(sub, &list);
    const int32_t count = static_cast<int32_t>(list.mLast - list.mFirst);
    if (count != 0) {
        uint32_t i = 0;
        do {
            void *const entry = list.mFirst[i];
            void **const out = helper.Renew();
            if (VirtualCall<QueryFn, 0>(entry, kHelperInterface, out) != 0) {
                void *const found = helper.Get();
                return VirtualCall<HandleFn, 6>(found, a1, a2, a3, a4, a5, a6);
            }
            ++i;
        } while (i < static_cast<uint32_t>(count));
    }
    return 0;
}

void cTSObject::vf259()
{
    using QueryFn = void(__thiscall *)(void *, uint32_t, void **);
    using FieldFn = uint32_t(__thiscall *)(void *, uint32_t);
    constexpr uint32_t kInterface = 0x5a6216f6;

    VirtualCall<NoArgVoid, 258>(this);
    HeldRef limits; // the object whose limits apply (esi), held until the end
    if (static_cast<uint8_t>(VirtualCall<NoArgRet32, 104>(this)) != 0) {
        HeldRef asked;
        VirtualCall<QueryFn, 0>(this, kInterface, asked.Renew());
        void *const iface = asked.Get();
        if (!iface) return;
        // The interface answers with itself (slot 6) when it is the one wanted.
        if (VirtualCall<NoArgPtr, 6>(iface) != iface) return;
        void *const context = At<void *>(this, 0x40);
        void *const found = VirtualCall<NoArgPtr, 24>(context);
        void *const holder = VirtualCall<OneArgPtr, 22>(found, At<uint32_t>(this, 0x240));
        if (!holder) return;
        void *const wanted = VirtualCall<NoArgPtr, 4>(holder);
        if (wanted) {
            VirtualCall<NoArgRet32, 1>(wanted);
            limits.Adopt(wanted);
        }
    } else if (void *const definition = At<void *>(this, 0x238)) {
        VirtualCall<NoArgRet32, 1>(definition);
        limits.Adopt(definition);
    }

    void *const service = GetCachedSingleton();
    void *const timer = VirtualCall<NoArgPtr, 23>(service);
    if (void *const object = limits.Get()) {
        if (VirtualCall<FieldFn, 3>(object, 0x25) == 0 && timer &&
            static_cast<uint8_t>(VirtualCall<NoArgRet32, 21>(timer)) == 0) {
            const uint16_t current = At<uint16_t>(this, 0xf8);
            const uint32_t which = VirtualCall<FieldFn, 3>(object, 0x23) != 0 ? 0x23 : 0x24;
            const uint16_t step = static_cast<uint16_t>(VirtualCall<FieldFn, 5>(object, which));
            uint16_t result = static_cast<uint16_t>(current - step);
            const int32_t floor = static_cast<int32_t>(VirtualCall<FieldFn, 3>(object, 0x26));
            if (static_cast<int32_t>(static_cast<int16_t>(result)) < floor)
                result = static_cast<uint16_t>(VirtualCall<FieldFn, 5>(object, 0x26));
            At<uint16_t>(this, 0xf8) = result;
        }
    }
    At<uint16_t>(this, 0x14c) = static_cast<uint16_t>(At<uint16_t>(this, 0x14c) - 1);
}

uint8_t cTSObject::vf284(std::vector<uint16_t> *ids, uint32_t skipOwnTree, const char *name, uint32_t d, uint32_t e, uint32_t skipChildren)
{
    using NameTreeFn = uint32_t(__thiscall *)(void *, cRZString *, uint32_t, uint32_t);
    using CallTreeFn = uint8_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using ByIdFn = void *(__thiscall *)(void *, int32_t);
    using CollectFn = uint8_t(__thiscall *)(void *, std::vector<uint16_t> *, uint32_t, const char *, uint32_t, uint32_t, uint32_t);

    if (VirtualCall<NoArgRet32, 98>(this) == 2) return 0;
    if (static_cast<uint8_t>(skipOwnTree) == 0) {
        if (name) {
            void *const owner = VirtualCall<NoArgPtr, 89>(this);
            uint32_t tree;
            {
                // The original builds the string, asks, then destroys it (also when the call throws).
                HeldString text(name);
                tree = static_cast<uint16_t>(VirtualCall<NameTreeFn, 31>(owner, text.Get(), 0, 0));
            }
            if (tree != 0) {
                const uint32_t id = At<uint16_t>(this, 0x1cc);
                void *const treeSim = &At<uint8_t>(this, 4);
                void *const *const treeSimVTable = *static_cast<void *const *const *>(treeSim);
                void *const treeOwner = VirtualCall<NoArgPtr, 89>(this);
                if (!reinterpret_cast<CallTreeFn>(treeSimVTable[15])(treeSim, treeOwner, id, tree, id, 3, d, 0, 0)) return 0;
            }
        }
        ids->push_back(At<uint16_t>(this, 0x1cc));
    }
    if (static_cast<uint8_t>(skipChildren) == 0) {
        const int32_t count = static_cast<int32_t>(VirtualCall<NoArgRet32, 66>(this));
        for (int32_t i = 0; i < count; ++i) {
            void *const contained = VirtualCall<OneArgPtr, 68>(this, static_cast<uint32_t>(i));
            if (!contained || static_cast<uint16_t>(VirtualCall<NoArgRet32, 4>(contained)) == 0) continue;
            void *const context = At<void *>(this, 0x40);
            void *const objects = VirtualCall<NoArgPtr, 26>(context);
            // The original reads the list's vtable before asking the contained object for its id again.
            void *const *const objectsVTable = *static_cast<void *const *const *>(objects);
            const int16_t objectId = static_cast<int16_t>(VirtualCall<NoArgRet32, 4>(contained));
            void *const target = reinterpret_cast<ByIdFn>(objectsVTable[29])(objects, objectId);
            if (!target) continue;
            const uint8_t ok = VirtualCall<CollectFn, 284>(target, ids, 0, name, d, e, 0);
            if (!ok && static_cast<uint8_t>(e) != 0) return 0;
        }
    }
    return 1;
}

void *cTSObject::vf358()
{
    using ByIdFn = void *(__thiscall *)(void *, int32_t);
    using MemberFn = uint32_t(__thiscall *)(void *, uint32_t);

    constexpr uint32_t kKindA = 0xb, kKindB = 0xc;
    if (VirtualCall<NoArgRet32, 98>(this) != kKindB) return nullptr;
    const int32_t index = static_cast<int32_t>(VirtualCall<NoArgRet32, 57>(this));
    if (index < 0) return nullptr;
    void *const context = At<void *>(this, 0x40);
    void *const manager = VirtualCall<NoArgPtr, 26>(context);
    void *const group = VirtualCall<OneArgPtr, 83>(manager, static_cast<uint32_t>(index));
    if (!group || VirtualCall<NoArgRet32, 8>(group) == 0) return nullptr;

    HeldRef found; // the last member of kind A or B; the reference is dropped again before it is returned
    if (static_cast<int32_t>(VirtualCall<NoArgRet32, 8>(group)) > 0) {
        int32_t i = 0;
        do {
            const uint16_t memberId = static_cast<uint16_t>(VirtualCall<MemberFn, 7>(group, static_cast<uint32_t>(i)));
            const uint16_t ownId = static_cast<uint16_t>(VirtualCall<NoArgRet32, 47>(this));
            if (memberId != ownId) {
                void *const member = reinterpret_cast<ByIdFn>((*static_cast<void *const *const *>(manager))[29])(manager, static_cast<int16_t>(memberId));
                if (member) VirtualCall<NoArgRet32, 1>(member);
                HeldRef held;
                held.Adopt(member);
                if (member) {
                    const uint32_t kind = VirtualCall<NoArgRet32, 98>(member);
                    if ((kind == kKindA || kind == kKindB) && member != found.Get()) {
                        VirtualCall<NoArgRet32, 1>(member);
                        void *const old = found.Get();
                        found.Adopt(member);
                        if (old) ReleaseRef(old);
                    }
                }
            }
            ++i;
        } while (i < static_cast<int32_t>(VirtualCall<NoArgRet32, 8>(group)));
    }
    return found.Get();
}

void cTSObject::vf201(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    using FlagFn = uint8_t(__thiscall *)(void *, uint32_t);
    using Slot187Fn = void(__thiscall *)(void *, int16_t *, uint32_t *, void **, uint16_t *, uint8_t *);
    using Slot9Fn = void(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using Slot24Fn = void(__thiscall *)(void *, uint32_t, void **, uint32_t);
    using FieldFn = uint32_t(__thiscall *)(void *, uint32_t);
    using Slot71Fn = void(__thiscall *)(void *, int32_t, int32_t);
    using Slot238Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint16_t *, uint32_t, uint32_t);
    using Slot236Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using Slot197Fn = void(__thiscall *)(void *, int16_t *, uint32_t, uint32_t);

    cTSTreeSim *const treeSim = &At<cTSTreeSim>(this, 4);
    if (static_cast<uint8_t>(b) != 0) {
        // The argument keeps the caller's upper bytes of the flag dword; only its low byte is made from `a`.
        const uint32_t flag = (b & ~0xffu) | (static_cast<uint16_t>(a) != 0 ? 1u : 0u);
        if (VirtualCall<FlagFn, 43>(treeSim, flag) != 0) return;
        if (VirtualCall<FlagFn, 337>(this, flag) != 0) return;
    }
    // Slot 187 fills these in (the pairs, the tick, an object whose id goes to word 2, a word for word 3, a flag byte nobody reads).
    int16_t pairs[4] = {};
    uint32_t tick = 0;
    void *idObject = nullptr;
    uint16_t word = 0;
    uint8_t flagByte = 0;
    VirtualCall<Slot187Fn, 187>(this, pairs, &tick, &idObject, &word, &flagByte);

    // The object data words are cleared, then these come back from the old values.
    constexpr size_t kDataWords = 0x72;
    uint16_t *const data = &At<uint16_t>(this, 0xa6);
    uint16_t old[kDataWords];
    std::memcpy(old, data, sizeof(old));
    std::memset(data, 0, sizeof(old));
    for (const int index : {0x1d, 0x01, 0x29, 0x2d, 0x2e, 0x2f, 0x18, 0x15, 0x43, 0x37, 0x13, 0x50, 0x51, 0x4f, 0x6a, 0x69, 0x6b}) data[index] = old[index];
    data[0x0b] = At<uint16_t>(this, 0x1cc);
    data[0x4b] = 0xff;
    data[0x39] = 0xff;
    data[0x47] = 0;
    data[0x48] = 0xff;
    data[0x49] = 0xff;
    data[0x4a] = 0xff;
    // The definition (+0x238) is not checked for null anywhere in this function.
    void *definition = At<void *>(this, 0x238);
    data[0x3a] = static_cast<uint16_t>(VirtualCall<FieldFn, 5>(definition, 0x31));

    if (static_cast<uint8_t>(c) != 0) {
        // Both tables are lists of words whose length (a signed word) is read again after every store.
        if (0 < At<int16_t>(this, 0x8c)) {
            int32_t i = 0;
            do {
                At<uint16_t *>(this, 0x88)[i] = 0;
                ++i;
            } while (i < At<int16_t>(this, 0x8c));
        }
        if (0 < At<int16_t>(this, 0x94)) {
            int32_t i = 0;
            do {
                At<uint16_t *>(this, 0x90)[i] = 0;
                ++i;
            } while (i < At<int16_t>(this, 0x94));
        }
    }
    std::memset(&At<uint8_t>(this, 0x96), 0, 16);  // the eight temps
    std::memset(&At<uint8_t>(this, 0x1b0), 0, 16); // the remembered inventory entry

    HeldVirtualBase service;
    VirtualCall<Slot9Fn, 9>(this, 0xfa1c39f7, 0xfa1c39ed, service.Out());
    if (service.Get()) {
        void *stream;
        {
            HeldVirtualBase created;
            VirtualSlot<Slot24Fn>(service.Get(), 24)(service.Get(), 0x0bb263d5, created.Out(), 0);
            stream = created.Get();
            created.Release(); // its base is released here, outside the frame's cleanup range
        }
        if (stream) VirtualCall<OneArgVoid, 8>(stream, 0);
        VirtualCall<NoArgVoid, 282>(this);
    }

    // StartTree's third argument is the id word, read before slot 93 (which takes the 1) runs.
    const uint32_t id = At<uint16_t>(this, 0x1cc);
    const uint32_t tree = VirtualCall<OneArgRet32, 93>(this, 1) & 0xffff;
    void *const owner = VirtualCall<NoArgPtr, 89>(this);
    treeSim->StartTree(owner, tree, id);
    VirtualCall<Slot197Fn, 197>(this, pairs, tick, 0);
    data[2] = idObject ? static_cast<uint16_t>(VirtualCall<NoArgRet32, 47>(idObject)) : 0;
    data[3] = word;
    data[0x17] = 2;
    // The definition pointer is read again before every call.
    definition = At<void *>(this, 0x238);
    if (VirtualCall<FieldFn, 3>(definition, 0xc) != 0) {
        definition = At<void *>(this, 0x238);
        data[4] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 6));
        definition = At<void *>(this, 0x238);
        data[5] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 3));
        definition = At<void *>(this, 0x238);
        data[0xd] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 5));
        definition = At<void *>(this, 0x238);
        data[0x2a] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 4));
    }
    definition = At<void *>(this, 0x238);
    data[0x4e] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 0xd));
    data[7] = static_cast<uint16_t>(VirtualCall<NoArgRet32, 352>(this));
    VirtualCall<NoArgVoid, 258>(this);
    void *const context = At<void *>(this, 0x40);
    void *const manager = VirtualCall<NoArgPtr, 26>(context);
    VirtualCall<Slot71Fn, 71>(manager, static_cast<int16_t>(At<uint16_t>(this, 0x1cc)), -1);
    void *const edith = &At<uint8_t>(this, 0x74);
    if (static_cast<uint8_t>(VirtualCall<NoArgRet32, 104>(this)) == 0) {
        uint16_t args[8] = {static_cast<uint8_t>(d)};
        VirtualCall<Slot238Fn, 238>(this, 0, 0, args, 0, 0);
        definition = At<void *>(this, 0x238);
        bool either = VirtualCall<FieldFn, 3>(definition, 0x6a) != 0;
        if (!either) {
            definition = At<void *>(this, 0x238);
            either = VirtualCall<FieldFn, 3>(definition, 0x6b) != 0;
        }
        if (either) {
            definition = At<void *>(this, 0x238);
            data[0x2a] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 0x6a));
            definition = At<void *>(this, 0x238);
            data[0x52] = static_cast<uint16_t>(VirtualCall<FieldFn, 3>(definition, 0x6b));
        }
        VirtualCall<NoArgVoid, 12>(edith);
        if (static_cast<uint16_t>(a) != 0) VirtualCall<Slot236Fn, 236>(this, 0, 1, 1);
        VirtualCall<NoArgVoid, 233>(this);
    }
    VirtualCall<NoArgVoid, 13>(edith);
}

void cTSObject::vf202(int32_t phase, const void *eventArgs)
{
    using Slot197Fn = void(__thiscall *)(void *, void *, uint32_t, uint32_t);
    using FieldFn = uint32_t(__thiscall *)(void *, uint32_t);
    using QueryFn = void(__thiscall *)(void *, uint32_t, void **);
    using ServiceFn = void(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using MaskFn = uint8_t(__thiscall *)(void *, uint32_t);
    using TwoArgVoid = void(__thiscall *)(void *, uint32_t, uint32_t);
    using Slot238Fn = void(__thiscall *)(void *, uint32_t, uint32_t, const void *, uint32_t, uint32_t);
    using Slot201Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    using Notify238Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using Slot9Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using TextFn = uint8_t(__thiscall *)(void *, const char *, uint32_t);
    constexpr uint32_t kTextTable = 0x85;

    VirtualCall<NoArgVoid, 215>(this);
    cTSTreeSim *const treeSim = &At<cTSTreeSim>(this, 4);
    VirtualCall<OneArgVoid, 46>(treeSim, static_cast<uint32_t>(phase));
    VirtualCall<OneArgVoid, 340>(this, static_cast<uint32_t>(phase));
    {
        // The vtable is read once. Slot 33 gives the pairs (this + 0x1d0); the dword at +0x1d8 is read before it runs.
        void *const *const vtable = *reinterpret_cast<void *const *const *>(this);
        const uint32_t tick = At<uint32_t>(this, 0x1d8);
        void *const pairs = reinterpret_cast<NoArgPtr>(vtable[33])(this);
        reinterpret_cast<Slot197Fn>(vtable[197])(this, pairs, tick, 1);
    }

    // The phase picks what is done: below 0x20 the flags change first and both the limits and the service steps run, 0x20 the
    // same without the flags, 0x21 and 0x22 only the service step, above that neither.
    bool limitsStep = false, serviceStep = false;
    if (phase < 0x20) {
        uint16_t flags = At<uint16_t>(this, 0xfa);
        if (flags & 1) flags &= 0xfffd;
        else if (flags & 2) flags &= 0xfffe;
        else flags |= 3;
        At<uint16_t>(this, 0xfa) = flags;
        if (static_cast<int8_t>(flags) < 0) At<uint16_t>(this, 0xfa) = flags & 0xff7e;
        At<uint16_t>(this, 0xfc) |= 0xa;
        limitsStep = serviceStep = true;
    } else if (phase < 0x21) {
        limitsStep = serviceStep = true;
    } else if (phase < 0x23) {
        serviceStep = true;
    }

    if (limitsStep) {
        // With forwarded primitives the limits only apply when the interface this object leads to is this object itself.
        bool applies = true;
        if (static_cast<uint8_t>(VirtualCall<NoArgRet32, 104>(this)) != 0) {
            HeldRef asked;
            VirtualCall<QueryFn, 0>(this, 0x5a6216f6, asked.Renew());
            // No null checks on the interface or on what it leads to.
            void *const iface = asked.Get();
            void *const first = VirtualCall<NoArgPtr, 6>(iface);
            void *const second = VirtualCall<NoArgPtr, 11>(first);
            applies = second == this;
        }
        if (applies) {
            void *const info = VirtualCall<NoArgPtr, 61>(this);
            void *const a = VirtualCall<NoArgPtr, 27>(info);
            void *const limits = VirtualCall<NoArgPtr, 4>(a);
            const uint16_t low = static_cast<uint16_t>(VirtualCall<FieldFn, 5>(limits, 0x12));
            const uint16_t high = static_cast<uint16_t>(VirtualCall<FieldFn, 5>(limits, 0x26));
            if (At<int16_t>(this, 0xf8) < static_cast<int16_t>(high)) At<uint16_t>(this, 0xf8) = low;
        }
    }
    if (serviceStep) {
        void *const framework = GetDirectorFrameworkObject();
        HeldRef out;
        VirtualCall<ServiceFn, 3>(framework, 0xfba4da53, 0xfba4da4d, out.Renew());
        // No null check on the service.
        void *const service = out.Get();
        At<uint16_t>(this, 0xae) = static_cast<uint16_t>(VirtualCall<FieldFn, 11>(service, 1));
    }

    // The object remembered by number (+0x2a4) is forgotten unless it is still a valid target.
    if (const uint16_t number = At<uint16_t>(this, 0x2a4)) {
        void *target = nullptr;
        if (void *const entry = At<ObjectRegistry *>(this, 0x1c4)->Find(static_cast<int16_t>(number))) {
            target = VirtualCall<NoArgPtr, 280>(entry);
            if (target) VirtualCall<NoArgRet32, 1>(target);
        }
        HeldRef held;
        held.Adopt(target);
        bool keep = false;
        if (target && VirtualCall<MaskFn, 25>(target, 0x10000) == 0 && VirtualCall<MaskFn, 25>(target, 0x20000) == 0 &&
            VirtualCall<NoArgRet32, 61>(target) != 0) {
            void *const second = VirtualCall<NoArgPtr, 61>(target);
            keep = VirtualCall<NoArgRet32, 28>(second) == 0xec987278;
        }
        if (!keep) At<uint16_t>(this, 0x2a4) = 0;
    }

    const uint32_t noArguments[2] = {0, 0};
    VirtualCall<Slot238Fn, 238>(this, 2, 0, eventArgs ? eventArgs : noArguments, 0, 0);
    void *const owner240 = At<void *>(this, 0x240);
    bool run = false;
    if (static_cast<uint8_t>(VirtualCall<NoArgRet32, 44>(owner240)) != 0) {
        void *const holder = At<void *>(this, 0x48);
        run = static_cast<uint8_t>(VirtualCall<NoArgRet32, 16>(holder)) != 0;
    }
    const uint8_t bit = static_cast<uint8_t>((At<uint32_t>(this, 0x190) >> 2) & 1);
    if (run || bit != 0) {
        VirtualCall<TwoArgVoid, 204>(this, 0, 0);
        // The original passes the dword of its first parameter's stack slot with the low byte replaced by the bit; the callee
        // (vf201) looks at the low byte only.
        VirtualCall<Slot201Fn, 201>(this, 0, 1, bit, 0);
        VirtualCall<Notify238Fn, 238>(this, 0x2b, 0, 0, 0, 0);
        ReloadReferences(true);
        if (static_cast<uint8_t>(VirtualCall<NoArgRet32, 188>(this)) != 0) VirtualCall<OneArgVoid, 193>(this, 1);
    }

    if (At<uint32_t>(this, 0x190) & 4) {
        // A text of the object's group is offered to a service (slot 9) that may answer with a reaction (slot 78).
        void *const owner = VirtualCall<NoArgPtr, 89>(this);
        void *const info = VirtualCall<NoArgPtr, 61>(this);
        const uint32_t group = VirtualCall<NoArgRet32, 11>(owner);
        const uint16_t entry = static_cast<uint16_t>(VirtualCall<NoArgRet32, 34>(info) + 1);
        HeldTSString text(kTextTable, entry, group);
        if (text.Get()->mString.Strlen() != 0) {
            HeldVirtualBase iface;
            if (VirtualCall<Slot9Fn, 9>(this, 0xfa1c39f7, 0xfa1c39ed, iface.Out()) != 0) {
                void *const service = iface.Get();
                void *const *const serviceVTable = *static_cast<void *const *const *>(service);
                const char *const chars = text.Get()->mString.ToChar();
                if (reinterpret_cast<TextFn>(serviceVTable[78])(service, chars, 1) != 0) {
                    void *const reaction = VirtualCall<NoArgPtr, 120>(service);
                    if (VirtualCall<NoArgRet32, 71>(reaction) == 0) VirtualCall<NoArgVoid, 221>(this);
                }
            }
        }
    }

    if (const uint32_t count = VirtualCall<NoArgRet32, 291>(this)) {
        (void)count;
        uint32_t i = 0;
        do {
            void *const *const vtable = *reinterpret_cast<void *const *const *>(this);
            const uint32_t item = reinterpret_cast<OneArgRet32>(vtable[293])(this, i);
            reinterpret_cast<OneArgVoid>(vtable[290])(this, item);
            ++i;
        } while (i < VirtualCall<NoArgRet32, 291>(this));
    }
    VirtualCall<NoArgVoid, 233>(this);
    VirtualCall<NoArgVoid, 216>(this);
    VirtualCall<OneArgVoid, 296>(this, 0);
}

namespace {

// Two float values (compared by their bits) are near when their sortable-integer difference is within -32..31: negative floats
// are flipped to sort below the positive ones, and each sign bit corrects the difference by one.
bool NearBits(uint32_t a, uint32_t b)
{
    const uint32_t signA = a >> 31, signB = b >> 31;
    const uint32_t keyA = ((0u - signA) >> 1) ^ a, keyB = ((0u - signB) >> 1) ^ b;
    const int32_t difference = static_cast<int32_t>(keyA - keyB - signB + signA);
    // The original tests ((d >> 26) ^ d) & ~31 == 0, which holds exactly for d in -32..31 (any shift of 5 or more gives that range).
    return difference >= -32 && difference <= 31;
}

// A wall segment (two corners as floats, a level number each: x1 y1 level1 x2 y2 level2 as six dwords) matches another when the
// corners are near and the levels equal, in either direction.
bool SameSegment(const uint32_t *a, const uint32_t *b)
{
    if (a[2] == b[2] && NearBits(a[0], b[0]) && NearBits(a[1], b[1]) && a[5] == b[5] && NearBits(a[3], b[3]) && NearBits(a[4], b[4])) return true;
    return a[5] == b[2] && NearBits(a[3], b[0]) && NearBits(a[4], b[1]) && a[2] == b[5] && NearBits(a[0], b[3]) && NearBits(a[1], b[4]);
}

// The loop of vf157 and vf158: slot 153 first, then every record (counted by `countSlot`, fetched by `recordSlot`) is compared.
uint8_t AnySegmentMatches(void *self, const uint32_t *wanted, int countSlot, int recordSlot)
{
    using RecordFn = void(__thiscall *)(void *, uint32_t *, int32_t);
    VirtualCall<NoArgVoid, 153>(self);
    const int32_t count = static_cast<int32_t>(VirtualSlot<NoArgRet32>(self, countSlot)(self));
    for (int32_t i = 0; i < count; ++i) {
        uint32_t record[6];
        VirtualSlot<RecordFn>(self, recordSlot)(self, record, i);
        if (SameSegment(record, wanted)) return 1;
    }
    return 0;
}

} // namespace

uint8_t cTSObject::vf157(const uint32_t *segment)
{
    return AnySegmentMatches(this, segment, 159, 163);
}

uint8_t cTSObject::vf158(const uint32_t *segment)
{
    return AnySegmentMatches(this, segment, 160, 165);
}

void *cTSObject::vf324(void *out, uint32_t a2, float a3)
{
    using FillFn = void(__thiscall *)(void *, HeldRefList *);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using HelperFn = void(__thiscall *)(void *, void *, uint32_t, float);
    constexpr uint32_t kHelperInterface = 0xaae90adb;

    HeldRef helper;
    HeldRefList list;
    void *const sub = &At<uint8_t>(this, 0x74);
    VirtualCall<FillFn, 10>(sub, &list);
    const int32_t count = static_cast<int32_t>(list.mLast - list.mFirst);
    if (count != 0) {
        uint32_t i = 0;
        do {
            void *const entry = list.mFirst[i];
            void **const slot = helper.Renew();
            if (VirtualCall<QueryFn, 0>(entry, kHelperInterface, slot) != 0) {
                void *const found = helper.Get();
                VirtualCall<HelperFn, 4>(found, out, a2, a3);
                return out;
            }
            ++i;
        } while (i < static_cast<uint32_t>(count));
    }
    // No helper: the default 12 bytes of the game's globals.
    std::memcpy(out, GameAddress<uint8_t>(0x10daf34), 8);
    std::memcpy(static_cast<uint8_t *>(out) + 8, GameAddress<uint8_t>(0x10daf3c), 4);
    return out;
}
